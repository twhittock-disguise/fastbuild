// Client.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Client.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Graph/CompilerNode.h"
#include "Tools/FBuild/FBuildCore/Graph/FileNode.h"
#include "Tools/FBuild/FBuildCore/Graph/Node.h"
#include "Tools/FBuild/FBuildCore/Graph/ObjectNode.h"
#include "Tools/FBuild/FBuildCore/Helpers/BuildProfiler.h"
#include "Tools/FBuild/FBuildCore/Helpers/MultiBuffer.h"
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/DistributionStats.h"
#include "Tools/FBuild/FBuildCore/Helpers/JobStream.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchDataCache.h"
#include "Tools/FBuild/FBuildCore/Protocol/PrioritySendQueue.h"
#include "Tools/FBuild/FBuildCore/Protocol/SendKind.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/Helpers/ToolManifest.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/Job.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/JobQueue.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/PchDispatch.h"

// Core
#include "Core/Containers/UniquePtr.h"
#include "Core/Env/ErrorFormat.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/Mem/Mem.h"
#include "Core/Math/Random.h"
#include "Core/Network/TCPConnectionPool.h"
#include "Core/Process/Atomic.h"
#include "Core/Profile/Profile.h"
#include "Core/Time/Timer.h"

// Defines
//------------------------------------------------------------------------------
#define SYSTEM_ERROR_ATTEMPT_COUNT ( 3u )
#define MAX_UNSENT_JOBS ( 4u ) // Per connection. Bounds the uncompressed job data waiting for the send thread.
#define DIST_INFO( ... ) do { if ( m_DetailedLogging ) { FLOG_OUTPUT( __VA_ARGS__ ); } } while ( false )

//------------------------------------------------------------------------------
class ClientWorkerInfo
{
public:
    // Represents a worker in the pool. Information stored here is tracked
    // persists even when not connected.

    AString m_Address; // Address to connect to
    Timer m_ConnectionDelayTimer; // Throttle for reconnection attempts
    bool m_InUse = false; // Connecting, connected or awaiting cleanup
    Atomic<bool> m_DenyListed{ false }; // Misbehaving workers are disabled for the rest of the build (see Client::DenyListWorker)
    Atomic<bool> m_ReportedHandshakeFailure{ false }; // The user is told once per worker that its handshake failed
    uint32_t m_UniqueId = 0; // Index for profiling purposes

    // Static Data
    static inline Atomic<uint32_t> s_NumConnections{ 0 }; // Track current number of connections
};

//------------------------------------------------------------------------------
class ClientSendQueueItem
{
public:
    ClientSendQueueItem() = default;
    explicit ClientSendQueueItem( const Protocol::IMessage & msg )
        : m_Message( msg.GetSize() ) // Prealloc exact size needed
        , m_HasPayload( false )
    {
        VERIFY( m_Message.WriteBuffer( &msg, msg.GetSize() ) == msg.GetSize() );
    }
    explicit ClientSendQueueItem( const Protocol::IMessage & msg,
                                  ConstMemoryStream && payload,
                                  SendKind kind,
                                  uint64_t rawBytes,
                                  bool streamCompress,
                                  uint64_t pchId = 0 )
        : m_Message( msg.GetSize() ) // Prealloc exact size needed
        , m_HasPayload( true )
        , m_Payload( Move( payload ) )
        , m_Kind( kind )
        , m_RawBytes( rawBytes )
        , m_StreamCompress( streamCompress )
        , m_PchId( pchId )
    {
        VERIFY( m_Message.WriteBuffer( &msg, msg.GetSize() ) == msg.GetSize() );
    }

    // A payload read from a file when it is sent, so queued data is not held in memory
    explicit ClientSendQueueItem( const Protocol::IMessage & msg,
                                  const AString & payloadFile,
                                  uint64_t payloadOffset,
                                  uint32_t payloadLength,
                                  SendKind kind,
                                  uint64_t rawBytes,
                                  uint64_t pchId,
                                  bool pchTransformed )
        : m_Message( msg.GetSize() ) // Prealloc exact size needed
        , m_HasPayload( true )
        , m_PayloadFile( payloadFile )
        , m_PayloadOffset( payloadOffset )
        , m_PayloadLength( payloadLength )
        , m_Kind( kind )
        , m_RawBytes( rawBytes )
        , m_PchId( pchId )
        , m_PchTransformed( pchTransformed )
    {
        VERIFY( m_Message.WriteBuffer( &msg, msg.GetSize() ) == msg.GetSize() );
    }

    // non copyable
    explicit ClientSendQueueItem( const ClientSendQueueItem & other ) = delete;
    void operator=( const ClientSendQueueItem & other ) = delete;

    // movable
    explicit ClientSendQueueItem( ClientSendQueueItem && other ) = default;
    ClientSendQueueItem & operator=( ClientSendQueueItem && other ) = default;

    bool IsBulk() const { return IsBulkSendKind( m_Kind ); }

    MemoryStream m_Message;
    bool m_HasPayload = false;
    ConstMemoryStream m_Payload;
    AString m_PayloadFile; // If set, the payload is this slice of the file
    uint64_t m_PayloadOffset = 0;
    uint32_t m_PayloadLength = 0;
    SendKind m_Kind = SendKind::Control;
    uint64_t m_RawBytes = 0; // Size before compression, for stats
    bool m_StreamCompress = false; // Payload is compressed by the send thread, against the jobs sent before it
    uint64_t m_PchId = 0; // The PCH the payload completes, if it is the last chunk of one
    bool m_PchTransformed = false; // Of that PCH: it is the form that a transform makes (see PchTransform)
    Timer m_Enqueued; // For how long it waited to be sent
};

//------------------------------------------------------------------------------
class ClientToWorkerConnection : public TCPConnectionPool
{
public:
    ClientToWorkerConnection( Client * client,
                              bool detailedLogging,
                              ClientWorkerInfo * worker );
    virtual ~ClientToWorkerConnection() override;

    // Non-copyable
    explicit ClientToWorkerConnection( const ClientToWorkerConnection & other ) = delete;
    ClientToWorkerConnection & operator=( const ClientToWorkerConnection & other ) = delete;

    [[nodiscard]] bool IsComplete() { return m_Complete.Load(); }
    void PrepareForShutdown() { m_ShuttingDown.Store( true ); } // Closing before the handshake is then not a failure

    ClientWorkerInfo * GetWorker() const { return m_Worker; }

    void PushToolchainsToWorker();
    uint32_t GetFreeSlots() const;
    void MarkProgress() { m_LastProgressTime.Store( (uint64_t)Timer::GetNow() ); }
    void CountHolders( PchDemand & demand ) const;
    void QueuePchsToWorker( PchDemand & demand );
    void PushJobsToWorker();
    void SendPendingPchs();

    template <class T>
    void EnqueueSend( const T & msg );
    template <class T>
    void EnqueueSend( const T & msg,
                      ConstMemoryStream && payload,
                      SendKind kind = SendKind::Control,
                      uint64_t rawBytes = 0,
                      uint64_t pchId = 0 );
    void EnqueueFileSend( const Protocol::IMessage & msg,
                          const AString & payloadFile,
                          uint64_t payloadOffset,
                          uint32_t payloadLength,
                          SendKind kind,
                          uint64_t rawBytes,
                          uint64_t pchId,
                          bool pchTransformed );
    void EnqueueJob( const Protocol::MsgJob & msg, ConstMemoryStream && payload );

private:
    virtual void OnDisconnected( const ConnectionInfo * connection ) override;
    virtual void OnReceive( const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory ) override;

    void Process( const Protocol::MsgJobResult *, const void * payload, size_t payloadSize );
    void Process( const Protocol::MsgJobResultCompressed * msg, const void * payload, size_t payloadSize );
    void Process( const ConnectionInfo * connection, const Protocol::MsgRequestManifest * msg );
    void Process( const ConnectionInfo * connection, const Protocol::MsgRequestFile * msg );
    void Process( const ConnectionInfo * connection, const Protocol::MsgConnectionAck * msg, uint32_t size );
    void Process( const ConnectionInfo * connection, const Protocol::MsgRefused * msg );
    void Process( const Protocol::MsgPchInventory * msg, const void * payload, size_t payloadSize );
    void Process( const Protocol::MsgToolchainInventory * msg, const void * payload, size_t payloadSize );
    void Process( const Protocol::MsgPchNeeded * msg );

    void ProcessJobResultCommon( bool isCompressed, const void * payload, size_t payloadSize );
    void Refuse( const ConnectionInfo * connection, uint32_t workerMajor, uint32_t workerMinor );
    void OnInventoriesComplete();

    bool QueueToolchain( const ToolManifest & manifest );
    const ToolManifest * FindManifest( uint64_t toolId ) const;
    bool WriteFileToDisk( const AString & fileName, const MultiBuffer & multiBuffer, size_t index ) const;

    // Thread to process sending messages
    static uint32_t SendQueueThreadFuncStatic( void * param );
    void SendQueueThreadFunc();
    void SendQueueMainLoop( const ConnectionInfo * ci );
    bool SendItem( const ConnectionInfo * ci, const ClientSendQueueItem & item ); // false if the thread should stop
    void WriteStats() const;

    // Initialization data
    Client * const m_Client; // Owner Client class
    const bool m_DetailedLogging; // DIST_INFO control
    ClientWorkerInfo * const m_Worker; // Associated worker info

    mutable Mutex m_Mutex;
    const Protocol::IMessage * m_CurrentMessage = nullptr;
    Array<Job *> m_Jobs; // jobs we've sent to this server

    // Send Thread
    Thread m_SendThread;
    Semaphore m_SendThreadWakeSemaphore;
    Atomic<bool> m_SendThreadQuit;
    Atomic<bool> m_Complete;

    // Info returned by worker
    Atomic<uint16_t> m_WorkerVersion;
    Atomic<bool> m_HandshakeDone{ false };
    Atomic<bool> m_ShuttingDown{ false };

    // Push-based job tracking
    Atomic<uint32_t> m_WorkerCapacity{ 0 };
    Atomic<uint32_t> m_JobsInFlight{ 0 };
    Atomic<bool> m_AwaitingInventory{ true }; // Jobs wait for the inventories the worker advertises, so they are not sent what it already has
    Array<uint64_t> m_SentToolchains; // Protected by m_Mutex
    Array<uint64_t> m_CachedToolchains; // Reported by the worker and not used yet. Protected by m_Mutex
    Atomic<uint32_t> m_NumSkippedToolchains{ 0 }; // Toolchains the worker had, so we did not send them
    Atomic<uint32_t> m_NumTransformedPchs{ 0 };   // PCHs delivered in the form that a transform makes (see PchTransform)
    PchSendState m_PchState;          // Protected by m_Mutex
    Array<uint64_t> m_PendingPchIds;  // Needed by this worker, not yet queued. Protected by m_Mutex
    uint32_t m_ToolchainsGeneration = 0; // Client thread only: the toolchains pushed so far
    Atomic<uint32_t> m_UnsentJobs{ 0 }; // Queued for the send thread, not yet sent
    JobStreamEncoder m_JobEncoder; // Send thread only. A new connection starts a new stream.

    DistributionSendStats m_SendStats[ (size_t)SendKind::NumKinds ]; // Send thread only
    Timer m_ConnectionTimer;
    Atomic<uint32_t> m_ResultsReceived{ 0 };
    Atomic<uint64_t> m_LastProgressTime{ 0 }; // Timer::GetNow() at the handshake, the last job sent or the last result
    Atomic<uint64_t> m_ResultBytesReceived{ 0 };
    Atomic<uint64_t> m_ResultRawBytes{ 0 }; // Result data once decompressed

    // Queue of messages to send. The chunks of a PCH go out behind everything else.
    Mutex m_SendQueueMutex;
    PrioritySendQueue<ClientSendQueueItem> m_SendQueue;
};

// CONSTRUCTOR
//------------------------------------------------------------------------------
Client::Client( const Array<AString> & explicitWorkerList,
                uint16_t port,
                uint32_t workerConnectionLimit,
                bool detailedLogging )
    : m_StaticWorkerList( explicitWorkerList )
    , m_DetailedLogging( detailedLogging )
    , m_WorkerConnectionLimit( workerConnectionLimit )
    , m_Port( port )
{
    // allocate space for server state management
    m_ActiveConnections.SetCapacity( workerConnectionLimit );

    m_Thread.Start( ThreadFuncStatic, "Client", this );
}

// DESTRUCTOR
//------------------------------------------------------------------------------
Client::~Client()
{
    PROFILE_FUNCTION;

    m_ShouldExit.Store( true );
    m_Thread.Join();
}

//------------------------------------------------------------------------------
void Client::AddToolchain( const ToolManifest * manifest )
{
    MutexHolder mh( m_ToolchainsMutex );
    if ( m_Toolchains.Find( manifest ) == nullptr )
    {
        m_Toolchains.Append( manifest );
        m_ToolchainsGeneration.Increment();
    }
}

//------------------------------------------------------------------------------
void Client::GetToolchains( Array<const ToolManifest *> & outManifests ) const
{
    MutexHolder mh( m_ToolchainsMutex );
    outManifests = m_Toolchains;
}

//------------------------------------------------------------------------------
uint32_t Client::GetNumConnections() const
{
    return ClientWorkerInfo::s_NumConnections.Load();
}

//------------------------------------------------------------------------------
uint32_t Client::GetNumDenyListedWorkers() const
{
    return m_NumDenyListedWorkers.Load();
}

//------------------------------------------------------------------------------
void Client::DenyListWorker( ClientWorkerInfo & worker )
{
    // A worker is only deny-listed from the receive thread of its own connection
    if ( worker.m_DenyListed.Load() == false )
    {
        worker.m_DenyListed.Store( true );
        m_NumDenyListedWorkers.Increment();
    }
}

//------------------------------------------------------------------------------
/*virtual*/ void ClientToWorkerConnection::OnDisconnected( const ConnectionInfo * /*connection*/ )
{
    {
        MutexHolder mh( m_Mutex );
        DIST_INFO( "Disconnected: %s\n", m_Worker->m_Address.Get() );

        // Before the jobs are returned: a job that is added after that sees it (PushJobsToWorker) and is returned by whoever added it
        m_SendThreadQuit.Store( true );
        if ( ( m_HandshakeDone.Load() == false ) && ( m_ShuttingDown.Load() == false ) && ( m_Worker->m_ReportedHandshakeFailure.Load() == false ) && ( m_Worker->m_DenyListed.Load() == false ) )
        {
            m_Worker->m_ReportedHandshakeFailure.Store( true );
            FLOG_OUTPUT( "Distributed Compilation : worker %s closed the connection before the handshake finished\n", m_Worker->m_Address.Get() );
        }
        if ( m_Jobs.IsEmpty() == false )
        {
            for ( Job * job : m_Jobs )
            {
                FLOG_MONITOR( "FINISH_JOB TIMEOUT %s \"%s\" \n",
                              m_Worker->m_Address.Get(),
                              job->GetNode()->GetName().Get() );
                JobQueue::Get().ReturnUnfinishedDistributableJob( job );
            }
            m_Jobs.Clear();
        }
    }

    // This is usually null here, but might need to be freed if
    // we had the connection drop between message and payload
    FREE( (void *)( m_CurrentMessage ) );
    m_CurrentMessage = nullptr;

    // Signal send thread to exit
    m_SendThreadWakeSemaphore.Signal();

    // Wait for send thread to exit so that TCPConnectionPool can safely
    // free the ConnectionInfo when this function returns
    // (i.e. ensure the send thread has finished using it). The lock is not held
    // while waiting: the send thread takes it to count the last chunk of a PCH delivered.
    while ( m_Complete.Load() == false )
    {
        Thread::Sleep( 1 );
    }
}

// ThreadFuncStatic
//------------------------------------------------------------------------------
/*static*/ uint32_t Client::ThreadFuncStatic( void * param )
{
    PROFILE_SET_THREAD_NAME( "ClientThread" );

    Client * c = (Client *)param;
    c->ThreadFunc();
    return 0;
}

// ThreadFunc
//------------------------------------------------------------------------------
void Client::ThreadFunc()
{
    PROFILE_FUNCTION;

    for ( ;; )
    {
        // Get list of workers in pool
        FindPotentialWorkers();
        if ( m_ShouldExit.Load() )
        {
            break;
        }

        // Initiate connections to new workers if needed
        ConnectToWorkers();
        if ( m_ShouldExit.Load() )
        {
            break;
        }

        // Ensure workers are updated with the state of available work
        CommunicateJobAvailability();
        if ( m_ShouldExit.Load() )
        {
            break;
        }

        Thread::Sleep( 1 );
        if ( m_ShouldExit.Load() )
        {
            break;
        }
    }

    // cleanup
    for ( UniquePtr<ClientToWorkerConnection> & serverState : m_ActiveConnections )
    {
        serverState->PrepareForShutdown();
        serverState->ShutdownAllConnections();
    }
    m_ActiveConnections.Clear();
}

//------------------------------------------------------------------------------
void Client::FindPotentialWorkers()
{
    // TODO:B Allow list of workers to be updated after startup
    if ( m_WorkerDiscoveryDone == true )
    {
        return;
    }
    m_WorkerDiscoveryDone = true;

    // Worker list from Settings takes priority over discovery
    if ( m_StaticWorkerList.IsEmpty() == false )
    {
        RegisterFoundWorkers( m_StaticWorkerList, nullptr );
    }
    else
    {
        // check for workers through brokerage or environment
        StackArray<AString> discoveredWorkers;
        m_WorkerBrokerage.FindWorkers( discoveredWorkers );
        RegisterFoundWorkers( discoveredWorkers, &m_WorkerBrokerage.GetBrokerageRootPaths() );
    }
}

//------------------------------------------------------------------------------
void Client::RegisterFoundWorkers( const Array<AString> & workerList,
                                   const AString * brokeragePaths )
{
    AStackString brokerageInfo;
    if ( brokeragePaths )
    {
        brokerageInfo.Format( "(from '%s')", brokeragePaths->Get() );
    }
    FLOG_OUTPUT( "Distributed Compilation : %zu Workers in pool %s\n",
                 workerList.GetSize(),
                 brokerageInfo.Get() );

    // Create state tracking structures so we can record state related to each
    // worker, whether connected or not
    m_WorkerPool.SetCapacity( workerList.GetSize() );
    for ( const AString & worker : workerList )
    {
        ClientWorkerInfo & workerInfo = *m_WorkerPool.EmplaceBack( FNEW( ClientWorkerInfo ) ).Get();
        workerInfo.m_Address = worker;
        // Modify timer so there is no connection delay for first attempt
        workerInfo.m_ConnectionDelayTimer.SetElapsed( FBuild::Get().GetOptions().m_DistributionReconnectDelay );
        workerInfo.m_UniqueId = static_cast<uint32_t>( workerList.GetSize() );
    }

    // randomize the start index to better distribute workers when there
    // are many workers/clients - otherwise all clients will attempt to connect
    // to the same subset of workers
    Random r;
    m_NextWorkerIndex = r.GetRandIndex( static_cast<uint32_t>( m_WorkerPool.GetSize() ) );
}

//------------------------------------------------------------------------------
void Client::ConnectToWorkers()
{
    PROFILE_FUNCTION;

    // note how many connections we have
    size_t numConnections = m_ActiveConnections.GetSize();

    // Clean up any disconnections
    if ( numConnections > 0 )
    {
        // Cleanup any newly disconnected workers (join threads)
        for ( int32_t i = static_cast<int32_t>( numConnections - 1 );
              i >= 0;
              --i )
        {
            UniquePtr<ClientToWorkerConnection> & connection = m_ActiveConnections[ static_cast<size_t>( i ) ];
            if ( connection->IsComplete() )
            {
                // Allow worker to be connected to again later if appropriate
                connection->GetWorker()->m_InUse = false;

                // Remove from list of active connections
                m_ActiveConnections.Erase( &connection );
                numConnections--;
            }
        }
    }

    // Find someone to connect to
    for ( size_t i = 0; i < m_WorkerPool.GetSize(); i++ )
    {
        // Limit maximum concurrent connections
        if ( numConnections >= m_WorkerConnectionLimit )
        {
            return;
        }

        // If we're connected to every possible worker already
        if ( numConnections >= m_WorkerPool.GetSize() )
        {
            return;
        }

        // Get the next worker to potentially connect to, using the offset
        // into the list and wrapping around
        ClientWorkerInfo & worker = *m_WorkerPool[ m_NextWorkerIndex++ ].Get();
        m_NextWorkerIndex = ( m_NextWorkerIndex % m_WorkerPool.GetSize() );

        // Already connected? (or still in the process of connecting)
        if ( worker.m_InUse == true )
        {
            continue;
        }

        // Ignore deny listed workers
        if ( worker.m_DenyListed.Load() )
        {
            continue;
        }

        // Have we tried this worker very recently?
        if ( worker.m_ConnectionDelayTimer.GetElapsed() < FBuild::Get().GetOptions().m_DistributionReconnectDelay )
        {
            continue;
        }

        // Initiate new connection
        m_ActiveConnections.EmplaceBack( FNEW( ClientToWorkerConnection( this,
                                                                         m_DetailedLogging,
                                                                         &worker ) ) );

        // Mark worker as in use
        worker.m_InUse = true;

        // Track add
        numConnections++;
    }
}

// CommunicateJobAvailability
//------------------------------------------------------------------------------
void Client::CommunicateJobAvailability()
{
    PROFILE_FUNCTION;

    // The PCHs the queued jobs need, and who has them. A PCH queued for one worker counts for the
    // workers after it.
    PchDemand demand;
    JobQueue::Get().CountDistributableJobsByPch( demand.m_PchIds, demand.m_NumWaiting );
    demand.m_NumHolders.SetSize( demand.m_PchIds.GetSize() );
    for ( uint32_t & numHolders : demand.m_NumHolders )
    {
        numHolders = 0;
    }
    for ( const UniquePtr<ClientToWorkerConnection> & ss : m_ActiveConnections )
    {
        ss->CountHolders( demand );
    }

    // Toolchains and PCHs are sent whether or not jobs are waiting
    for ( UniquePtr<ClientToWorkerConnection> & ss : m_ActiveConnections )
    {
        ss->PushToolchainsToWorker();
        ss->QueuePchsToWorker( demand );
        ss->SendPendingPchs();
    }

    // Early out when there are no distributable jobs
    if ( JobQueue::Get().GetNumDistributableJobsAvailable() == 0 )
    {
        return;
    }

    // Push jobs directly to workers that have capacity
    for ( UniquePtr<ClientToWorkerConnection> & ss : m_ActiveConnections )
    {
        ss->PushJobsToWorker();
    }
}

// OnReceive
//------------------------------------------------------------------------------
/*virtual*/ void ClientToWorkerConnection::OnReceive( const ConnectionInfo * connection,
                                                      void * data,
                                                      uint32_t size,
                                                      bool & keepMemory )
{
    keepMemory = true; // we'll take care of freeing the memory

    // are we expecting a msg, or the payload for a msg?
    void * payload = nullptr;
    size_t payloadSize = 0;
    if ( m_CurrentMessage == nullptr )
    {
        // message
        if ( Protocol::IsValidHeader( data, size ) == false )
        {
            DIST_INFO( "Protocol Error: %s sent an invalid message (%u bytes)\n", m_Worker->m_Address.Get(), size );
            FREE( data );
            Disconnect( connection );
            return;
        }
        m_CurrentMessage = static_cast<const Protocol::IMessage *>( data );
        if ( m_CurrentMessage->HasPayload() )
        {
            return;
        }
    }
    else
    {
        // payload
        ASSERT( m_CurrentMessage->HasPayload() );
        payload = data;
        payloadSize = size;
    }

    // determine message type
    const Protocol::IMessage * imsg = m_CurrentMessage;
    const Protocol::MessageType messageType = imsg->GetType();

    PROTOCOL_DEBUG( "Server -> Client : %u (%s)\n", messageType, GetProtocolMessageDebugName( messageType ) );

    switch ( messageType )
    {
        case Protocol::MSG_JOB_RESULT:
        {
            const Protocol::MsgJobResult * msg = static_cast<const Protocol::MsgJobResult *>( imsg );
            Process( msg, payload, payloadSize );
            break;
        }
        case Protocol::MSG_JOB_RESULT_COMPRESSED:
        {
            const Protocol::MsgJobResultCompressed * msg = static_cast<const Protocol::MsgJobResultCompressed *>( imsg );
            Process( msg, payload, payloadSize );
            break;
        }
        case Protocol::MSG_REQUEST_MANIFEST:
        {
            const Protocol::MsgRequestManifest * msg = static_cast<const Protocol::MsgRequestManifest *>( imsg );
            Process( connection, msg );
            break;
        }
        case Protocol::MSG_REQUEST_FILE:
        {
            const Protocol::MsgRequestFile * msg = static_cast<const Protocol::MsgRequestFile *>( imsg );
            Process( connection, msg );
            break;
        }
        case Protocol::MSG_CONNECTION_ACK:
        {
            const Protocol::MsgConnectionAck * msg = static_cast<const Protocol::MsgConnectionAck *>( imsg );
            Process( connection, msg, size );
            break;
        }
        case Protocol::MSG_REFUSED:
        {
            const Protocol::MsgRefused * msg = static_cast<const Protocol::MsgRefused *>( imsg );
            Process( connection, msg );
            break;
        }
        case Protocol::MSG_PCH_INVENTORY:
        {
            const Protocol::MsgPchInventory * msg = static_cast<const Protocol::MsgPchInventory *>( imsg );
            Process( msg, payload, payloadSize );
            break;
        }
        case Protocol::MSG_TOOLCHAIN_INVENTORY:
        {
            const Protocol::MsgToolchainInventory * msg = static_cast<const Protocol::MsgToolchainInventory *>( imsg );
            Process( msg, payload, payloadSize );
            break;
        }
        case Protocol::MSG_PCH_NEEDED:
        {
            const Protocol::MsgPchNeeded * msg = static_cast<const Protocol::MsgPchNeeded *>( imsg );
            Process( msg );
            break;
        }
        default:
        {
            // unknown message type
            ASSERT( false ); // this indicates a protocol bug
            DIST_INFO( "Protocol Error: %s sent an unknown message (type %u)\n", m_Worker->m_Address.Get(), (uint32_t)messageType );
            Disconnect( connection );
            break;
        }
    }

    // free everything
    FREE( (void *)( m_CurrentMessage ) );
    FREE( payload );
    m_CurrentMessage = nullptr;
}

// Process( MsgJobResult )
//------------------------------------------------------------------------------
void ClientToWorkerConnection::Process( const Protocol::MsgJobResult * /*msg*/,
                                        const void * payload,
                                        size_t payloadSize )
{
    PROFILE_SECTION( "MsgJobResult" );
    const bool compressed = false;
    ProcessJobResultCommon( compressed, payload, payloadSize );
}

// Process( MsgJobResultCompressed )
//------------------------------------------------------------------------------
void ClientToWorkerConnection::Process( const Protocol::MsgJobResultCompressed * /*msg*/,
                                        const void * payload,
                                        size_t payloadSize )
{
    PROFILE_SECTION( "MsgJobResultCompressed" );
    const bool compressed = true;
    ProcessJobResultCommon( compressed, payload, payloadSize );
}

// Process( MsgConnectionAck )
//------------------------------------------------------------------------------
void ClientToWorkerConnection::Process( const ConnectionInfo * connection, const Protocol::MsgConnectionAck * msg, uint32_t size )
{
    PROFILE_SECTION( "MsgConnectionAck" );

    // An ack has no payload (see IsValidHeader), so size is that of the message itself. Only the version
    // fields are the same in every major version.
    if ( msg->GetProtocolVersionMajor() != Protocol::kVersionMajor )
    {
        Refuse( connection, msg->GetProtocolVersionMajor(), msg->GetProtocolVersionMinor() );
        return;
    }
    if ( size < sizeof( Protocol::MsgConnectionAck ) )
    {
        DIST_INFO( "Protocol Error: %s sent a handshake that is too short (%u bytes)\n", m_Worker->m_Address.Get(), size );
        Disconnect( connection );
        return;
    }

    // Take note of additional server info
    m_WorkerVersion.Store( msg->GetWorkerVersion() );
    m_WorkerCapacity.Store( msg->GetWorkerCapacity() );
    MarkProgress();
    m_HandshakeDone.Store( true );
    AStackString<> protocolVersion;
    Protocol::FormatVersion( protocolVersion, Protocol::kVersionMajor, msg->GetProtocolVersionMinor() );
    DIST_INFO( " - Worker %s is v%u.%u (protocol %s) capacity=%u\n",
               m_Worker->m_Address.Get(),
               ( m_WorkerVersion.Load() / 100U ),
               ( m_WorkerVersion.Load() % 100U ),
               protocolVersion.Get(),
               msg->GetWorkerCapacity() );
}

// Process( MsgRefused )
//------------------------------------------------------------------------------
void ClientToWorkerConnection::Process( const ConnectionInfo * connection, const Protocol::MsgRefused * msg )
{
    PROFILE_SECTION( "MsgRefused" );
    Refuse( connection, msg->GetProtocolVersionMajor(), msg->GetProtocolVersionMinor() );
}

// Refuse
//------------------------------------------------------------------------------
// The worker is another version: tell the user once, and leave it alone for the rest of the build
void ClientToWorkerConnection::Refuse( const ConnectionInfo * connection, uint32_t workerMajor, uint32_t workerMinor )
{
    if ( m_Worker->m_ReportedHandshakeFailure.Load() == false )
    {
        m_Worker->m_ReportedHandshakeFailure.Store( true );
        AStackString<> workerVersion;
        AStackString<> ourVersion;
        Protocol::FormatVersion( workerVersion, workerMajor, workerMinor );
        Protocol::GetVersionString( ourVersion );
        FLOG_OUTPUT( "Distributed Compilation : worker %s runs protocol %s, this FBuild is %s: update FBuild and FBuildWorker together\n",
                     m_Worker->m_Address.Get(), workerVersion.Get(), ourVersion.Get() );
    }
    m_Client->DenyListWorker( *m_Worker );
    Disconnect( connection );
}

// OnInventoriesComplete
//------------------------------------------------------------------------------
void ClientToWorkerConnection::OnInventoriesComplete()
{
    m_AwaitingInventory.Store( false );
    PushJobsToWorker();
}

// Process( MsgPchInventory )
//------------------------------------------------------------------------------
void ClientToWorkerConnection::Process( const Protocol::MsgPchInventory * msg,
                                        const void * payload,
                                        size_t payloadSize )
{
    PROFILE_SECTION( "MsgPchInventory" );

    const uint32_t numEntries = msg->GetNumEntries();
    const size_t expectedSize = (size_t)numEntries * sizeof( uint64_t );
    if ( payloadSize < expectedSize )
    {
        DIST_INFO( " - Worker %s sent truncated PCH inventory (%zu < %zu)\n",
                   m_Worker->m_Address.Get(), payloadSize, expectedSize );
    }
    else if ( numEntries > 0 )
    {
        {
            MutexHolder mh( m_Mutex );
            m_PchState.OnInventory( static_cast<const uint64_t *>( payload ), numEntries );
        }
        DIST_INFO( " - Worker %s has %u cached PCH entries\n",
                   m_Worker->m_Address.Get(), numEntries );
    }
}

// Process( MsgToolchainInventory )
//------------------------------------------------------------------------------
void ClientToWorkerConnection::Process( const Protocol::MsgToolchainInventory * msg,
                                        const void * payload,
                                        size_t payloadSize )
{
    PROFILE_SECTION( "MsgToolchainInventory" );

    const uint32_t numEntries = msg->GetNumEntries();
    if ( payloadSize < ( (size_t)numEntries * sizeof( uint64_t ) ) )
    {
        DIST_INFO( " - Worker %s sent truncated toolchain inventory\n", m_Worker->m_Address.Get() );
    }
    else if ( numEntries > 0 )
    {
        // Pre-populate m_SentToolchains with all the toolchains the worker already has
        const uint64_t * toolIds = static_cast<const uint64_t *>( payload );
        MutexHolder mh( m_Mutex );
        for ( uint32_t i = 0; i < numEntries; ++i )
        {
            if ( m_SentToolchains.Find( toolIds[ i ] ) == nullptr )
            {
                m_SentToolchains.Append( toolIds[ i ] );
                m_CachedToolchains.Append( toolIds[ i ] );
            }
        }
        DIST_INFO( " - Worker %s has %u cached toolchains\n", m_Worker->m_Address.Get(), numEntries );
    }

    // Both inventories have now been processed
    OnInventoriesComplete();
}

// Process( MsgPchNeeded )
//------------------------------------------------------------------------------
void ClientToWorkerConnection::Process( const Protocol::MsgPchNeeded * msg )
{
    PROFILE_SECTION( "MsgPchNeeded" );

    const uint64_t pchId = msg->GetPchId();
    PchDataCache::Entry entry;
    const bool known = ( pchId != 0 ) && PchDataCache::Get().Find( pchId, entry );

    // Jobs sent to this worker would wait for the PCH for good: close the connection so they
    // return to the queue, and leave the worker alone for the rest of the build
    const auto giveUp = [ this ]()
    {
        m_Client->DenyListWorker( *m_Worker );
        m_SendThreadQuit.Store( true );
        m_SendThreadWakeSemaphore.Signal();
    };

    MutexHolder mh( m_Mutex );
    if ( known == false )
    {
        FLOG_WARN( "Distributed Compilation : worker %s asked for PCH 0x%016" PRIx64 ", which is not known here: not using it any more\n",
                   m_Worker->m_Address.Get(), pchId );
        giveUp();
        return;
    }

    switch ( m_PchState.OnNeeded( pchId ) )
    {
        case PchSendState::NEEDED_QUEUED:
            DIST_INFO( " - Worker %s needs PCH 0x%016" PRIx64 " again\n", m_Worker->m_Address.Get(), pchId );
            if ( m_PendingPchIds.Find( pchId ) == nullptr )
            {
                m_PendingPchIds.Append( pchId );
            }
            break;
        case PchSendState::NEEDED_ALREADY_QUEUED:
            break;
        case PchSendState::NEEDED_LIMIT:
            FLOG_WARN( "Distributed Compilation : worker %s lost PCH 0x%016" PRIx64 " %u times without making progress: not using it any more\n",
                       m_Worker->m_Address.Get(), pchId, PchSendState::kMaxResends );
            giveUp();
            break;
    }
}

// QueueToolchain
//------------------------------------------------------------------------------
bool ClientToWorkerConnection::QueueToolchain( const ToolManifest & manifest )
{
    const uint64_t toolId = manifest.GetToolId();
    if ( toolId == 0 )
    {
        return false; // Not known yet
    }

    MutexHolder mh( m_Mutex );
    if ( m_SentToolchains.Find( toolId ) == nullptr )
    {
        MemoryStream ms;
        manifest.SerializeForRemote( ms );
        EnqueueSend( Protocol::MsgManifest( toolId ),
                     Move( ConstMemoryStream( Move( ms ) ) ),
                     SendKind::Toolchain );

        const Array<ToolManifestFile> & files = manifest.GetFiles();
        for ( uint32_t i = 0; i < files.GetSize(); ++i )
        {
            size_t dataSize = 0;
            const void * data = manifest.GetFileData( i, dataSize );
            if ( data )
            {
                // Not chunked: a large file holds up the jobs queued after it
                EnqueueSend( Protocol::MsgFile( toolId, i ),
                             Move( ConstMemoryStream( data, dataSize ) ),
                             SendKind::Toolchain,
                             files[ i ].GetUncompressedContentSize() );
            }
        }
        m_SentToolchains.Append( toolId );
    }
    else if ( m_CachedToolchains.FindAndErase( toolId ) )
    {
        m_NumSkippedToolchains.Increment();
        DIST_INFO( " - Toolchain 0x%016" PRIx64 " is cached on %s\n", toolId, m_Worker->m_Address.Get() );
    }
    return true;
}

// PushToolchainsToWorker
//------------------------------------------------------------------------------
void ClientToWorkerConnection::PushToolchainsToWorker()
{
    // Toolchains go out ahead of any PCH queued after them: they are small and every job needs one
    if ( m_AwaitingInventory.Load() || m_Worker->m_DenyListed.Load() || m_SendThreadQuit.Load() )
    {
        return;
    }

    const uint32_t generation = m_Client->GetToolchainsGeneration();
    if ( generation == m_ToolchainsGeneration )
    {
        return;
    }

    Array<const ToolManifest *> manifests;
    m_Client->GetToolchains( manifests );
    bool allQueued = true;
    for ( const ToolManifest * manifest : manifests )
    {
        allQueued &= QueueToolchain( *manifest );
    }
    if ( allQueued )
    {
        m_ToolchainsGeneration = generation;
    }
}

// GetFreeSlots
//------------------------------------------------------------------------------
uint32_t ClientToWorkerConnection::GetFreeSlots() const
{
    const uint32_t capacity = m_WorkerCapacity.Load();
    const uint32_t inFlight = m_JobsInFlight.Load();
    return ( inFlight >= capacity ) ? 0 : ( capacity - inFlight );
}

// CountHolders
//------------------------------------------------------------------------------
void ClientToWorkerConnection::CountHolders( PchDemand & demand ) const
{
    if ( m_Worker->m_DenyListed.Load() || m_SendThreadQuit.Load() )
    {
        return;
    }

    const float secondsSinceProgress = (float)( Timer::GetNow() - (int64_t)m_LastProgressTime.Load() ) * Timer::GetFrequencyInvFloat();
    if ( PchDispatch::CountsAsHolder( m_AwaitingInventory.Load(), m_WorkerCapacity.Load(), GetFreeSlots(), secondsSinceProgress ) == false )
    {
        return;
    }

    MutexHolder mh( m_Mutex );
    PchDispatch::AddHolders( demand, m_PchState );
}

// QueuePchsToWorker
//------------------------------------------------------------------------------
void ClientToWorkerConnection::QueuePchsToWorker( PchDemand & demand )
{
    if ( m_AwaitingInventory.Load() || m_Worker->m_DenyListed.Load() || m_SendThreadQuit.Load() )
    {
        return;
    }

    // A worker with no free slot cannot use a PCH yet. The PCHs for jobs it already holds are
    // on their way, or are sent when it asks for them.
    const uint32_t freeSlots = GetFreeSlots();
    if ( freeSlots == 0 )
    {
        return;
    }

    // Keep the uplink busy: queue the next PCH while the current is sent, rather than once a job
    // that needs it has been taken
    MutexHolder mh( m_Mutex );
    PchDispatch::QueuePchs(
        demand,
        m_PchState,
        freeSlots,
        []( uint64_t pchId )
        {
            PchDataCache::Entry entry;
            return PchDataCache::Get().Find( pchId, entry ) && ( PchDataCache::Get().HasCompressionFailed( pchId ) == false );
        },
        [ this ]( uint64_t pchId ) { m_PendingPchIds.Append( pchId ); } );
}

// PushJobsToWorker
//------------------------------------------------------------------------------
void ClientToWorkerConnection::PushJobsToWorker()
{
    PROFILE_SECTION( "PushJobsToWorker" );

    uint32_t toSend = GetFreeSlots();
    if ( ( toSend == 0 ) || m_Worker->m_DenyListed.Load() || m_SendThreadQuit.Load() || m_AwaitingInventory.Load() )
    {
        return;
    }

    // The jobs already on the worker that wait for their PCH, and the PCHs on the worker or on
    // their way. Kept up to date locally as jobs are taken.
    PchWorkerState pchState;
    pchState.m_MaxHeld = PchDispatch::MaxHeldJobs( m_WorkerCapacity.Load() );
    {
        MutexHolder mh( m_Mutex );
        pchState.m_OnWorker = m_PchState.GetOnWorker();
        pchState.m_Sending = m_PchState.GetSending();
        for ( const Job * job : m_Jobs )
        {
            const uint64_t pchId = job->GetPchId();
            if ( ( pchId != 0 ) && ( m_PchState.IsOnWorker( pchId ) == false ) )
            {
                ++pchState.m_NumHeld;
            }
        }
    }

    while ( toSend > 0 )
    {
        // Jobs are compressed as they are sent. Take no more than the send thread can use,
        // as the rest are better left for other workers than held as uncompressed copies.
        if ( m_UnsentJobs.Load() >= MAX_UNSENT_JOBS )
        {
            break;
        }

        Job * job = JobQueue::Get().GetDistributableJobToProcess( true, &pchState );
        if ( job == nullptr )
        {
            break;
        }

        const Node * n = job->GetNode()->CastTo<ObjectNode>()->GetCompiler();
        ASSERT( n );
        const ToolManifest & manifest = n->CastTo<CompilerNode>()->GetManifest();
        const uint64_t toolId = manifest.GetToolId();

        QueueToolchain( manifest );

        // A job is only given a PCH that is on its way (QueuePchsToWorker), so a failed one is
        // not waited for
        const uint64_t pchId = job->GetPchId();
        if ( pchId != 0 )
        {
            if ( PchDataCache::Get().HasCompressionFailed( pchId ) )
            {
                JobQueue::Get().ReturnUnfinishedDistributableJob( job );
                break;
            }
            if ( pchState.m_OnWorker.Find( pchId ) == nullptr )
            {
                ++pchState.m_NumHeld;
            }
        }

        // Track and send job. Not if the connection ended since the check above: OnDisconnected has returned the
        // jobs it had, and one added now would not be.
        bool isTracked = false;
        Client::RunTestHook( Client::BEFORE_JOB_TRACKED, m_SendThreadQuit );
        {
            MutexHolder mh( m_Mutex );
            if ( m_SendThreadQuit.Load() == false )
            {
                m_Jobs.Append( job );
                isTracked = true;
            }
        }
        if ( isTracked == false )
        {
            JobQueue::Get().ReturnUnfinishedDistributableJob( job );
            break;
        }
        m_JobsInFlight.Increment();
        MarkProgress();

        if ( FBuild::Get().GetOptions().m_ShowCommandSummary )
        {
            FLOG_OUTPUT( "-> Obj: %s <REMOTE: %s>\n",
                         job->GetNode()->GetName().Get(), m_Worker->m_Address.Get() );
        }
        FLOG_MONITOR( "START_JOB %s \"%s\" \n", m_Worker->m_Address.Get(), job->GetNode()->GetName().Get() );

        MemoryStream stream;
        job->Serialize( stream );

        // Determine compression level for results
        int16_t resultCompressionLevel = (int16_t)Compressor::kZstdTransferLevel;
        if ( FBuild::IsValid() )
        {
            const int16_t cacheCompressionLevel = FBuild::Get().GetOptions().m_CacheCompressionLevel;
            if ( ( cacheCompressionLevel != 0 ) &&
                 ( FBuild::Get().GetOptions().m_UseCacheWrite ) &&
                 ( job->GetNode()->CastTo<ObjectNode>()->ShouldUseCache() ) )
            {
                resultCompressionLevel = Math::Max( resultCompressionLevel, cacheCompressionLevel );
            }
        }

        const bool allowZstdUse = true;
        job->SetResultCompressionLevel( resultCompressionLevel, allowZstdUse );

        EnqueueJob( Protocol::MsgJob( toolId, resultCompressionLevel ),
                    Move( ConstMemoryStream( Move( stream ) ) ) );

        toSend--;
    }
}

// SendPendingPchs
//------------------------------------------------------------------------------
void ClientToWorkerConnection::SendPendingPchs()
{
    MutexHolder mh( m_Mutex );
    if ( m_PendingPchIds.IsEmpty() )
    {
        return;
    }

    Array<PchDataCache::ReadyPch> ready;
    bool allOk = PchDataCache::Get().TakeReadyPchs( m_PendingPchIds, m_PchState.GetOnWorker(), ready );
    const uint32_t chunkSize = Math::Clamp( FBuild::Get().GetOptions().m_DistributionPchChunkSize,
                                            Protocol::kMinPchChunkSize,
                                            Protocol::kMaxPchChunkSize );
    for ( const PchDataCache::ReadyPch & pch : ready )
    {
        // One message per chunk, so that jobs can go out between them. The PCH counts as sent
        // once its last chunk has.
        if ( pch.dataSize > 0xFFFFFFFFull ) // PchDataCache::Record refuses files that would come to this
        {
            FLOG_ERROR( "PCH distribution: the compressed PCH 0x%016" PRIx64 " is too large to send\n", pch.pchId );
            allOk = false;
            continue;
        }
        const uint32_t compressedSize = (uint32_t)pch.dataSize;
        const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( compressedSize, chunkSize );
        for ( uint32_t i = 0; i < numChunks; ++i )
        {
            // Each chunk is read from the file as it is sent
            const bool isLast = ( ( i + 1 ) == numChunks );
            EnqueueFileSend( Protocol::MsgPchFile( pch.pchId, pch.uncompressedSize, compressedSize, chunkSize, i ),
                             pch.filePath,
                             (uint64_t)i * chunkSize,
                             Protocol::MsgPchFile::GetChunkLength( compressedSize, chunkSize, i ),
                             SendKind::Pch,
                             ( i == 0 ) ? pch.uncompressedSize : 0,
                             isLast ? pch.pchId : 0,
                             pch.transformed );
        }

        DIST_INFO( " - Sent PCH 0x%016" PRIx64 " to %s (%u bytes, %zu bytes compressed%s, %u chunks)\n",
                   pch.pchId, m_Worker->m_Address.Get(), pch.uncompressedSize, pch.dataSize,
                   pch.transformed ? " with the PCH transform" : "", numChunks );
    }

    if ( allOk == false )
    {
        // Jobs sent to this worker would wait forever for their PCH. Close the connection
        // so they return to the queue, where other workers or the local build take them.
        m_SendThreadQuit.Store( true );
        m_SendThreadWakeSemaphore.Signal();
    }
}

// ProcessJobResultCommon
//------------------------------------------------------------------------------
void ClientToWorkerConnection::ProcessJobResultCommon( bool isCompressed,
                                                       const void * payload,
                                                       size_t payloadSize )
{
    // Take note of the current time. We'll consider the job to have completed at this time.
    // Doing it as soon as possible makes it more accurate, as work below can take a non-trivial
    // amount of time. (For example OnReturnRemoteJob when cancelling the local job in a race)
    const int64_t receivedResultEndTime = Timer::GetNow();

    // Decrement in-flight count immediately
    m_JobsInFlight.Decrement();

    m_ResultsReceived.Increment();
    MarkProgress();
    m_ResultBytesReceived.Add( payloadSize );

    ConstMemoryStream ms( payload, payloadSize );

    uint32_t jobId = 0;
    ms.Read( jobId );

    AStackString name;
    ms.Read( name );

    bool result = false;
    ms.Read( result );

    bool systemError = false;
    ms.Read( systemError );

    Array<AString> messages;
    ms.Read( messages );

    uint32_t buildTime;
    ms.Read( buildTime );

    uint16_t remoteThreadId = 0;
    ms.Read( remoteThreadId );

    // get result data (built data or errors if failed)
    uint32_t dataSize = 0;
    ms.Read( dataSize );
    const void * data = (const char *)ms.GetData() + ms.Tell();

    {
        MutexHolder mh( m_Mutex );
        Job ** found = m_Jobs.FindDeref( jobId );
        ASSERT( found );
        if ( found )
        {
            if ( ( systemError == false ) && ( ( *found )->GetPchId() != 0 ) )
            {
                m_PchState.OnJobCompleted( ( *found )->GetPchId() );
            }
            m_Jobs.EraseIndex( (size_t)( found - m_Jobs.Begin() ) );
        }
    }

    // Has the job been cancelled in the interim?
    // (Due to a Race by the main thread for example)
    bool raceLost = false;
    bool raceWon = false;
    const Node * node = nullptr;
    uint32_t jobSystemErrorCount = 0;
    Job * job = JobQueue::Get().OnReturnRemoteJob( jobId,
                                                   systemError,
                                                   raceLost, // Set by OnReturnRemoteJob
                                                   raceWon, // Set by OnReturnRemoteJob
                                                   node, // Set by OnReturnRemoteJob
                                                   jobSystemErrorCount ); // Set by OnReturnRemoteJob

    // Prepare failure output if needed
    AStackString<8192> failureOutput;
    if ( result == false )
    {
        failureOutput.Format( "PROBLEM: %s\n", node->GetName().Get() );

        // When invoked from MSBuild (directly or from Visual Studio) and
        // using -distverbose we might output a remote error string for a
        // "system error" which is then retried. The build may ultimately
        // succeed and return 0, but MSBuild uses regexs to pattern match
        // these strings and force an error which we don't want.
        // So we "clean" these strings to tweak the message slightly to
        // avoid that
        const bool applyMSBuildWorkAround = systemError &&
                                            m_DetailedLogging; //-distverbose
        if ( applyMSBuildWorkAround )
        {
            for ( const AString & message : messages )
            {
                AStackString messageCleaned;
                Node::CleanMessageToPreventMSBuildFailure( message, messageCleaned );
                failureOutput += messageCleaned;
            }
        }
        else
        {
            failureOutput.AppendList( messages );
        }
    }

    // For system failures, mark worker so no more jobs are scheduled to it
    if ( systemError )
    {
        m_Client->DenyListWorker( *m_Worker );

        // -distverbose message
        DIST_INFO( "Remote System Failure!\n"
                   " - Deny listed Worker: %s\n"
                   " - Node              : %s\n"
                   " - Job Error Count   : %u / %u\n"
                   " - Details           :\n"
                   "%s",
                   m_Worker->m_Address.Get(),
                   node->GetName().Get(),
                   jobSystemErrorCount,
                   SYSTEM_ERROR_ATTEMPT_COUNT,
                   failureOutput.Get() );
    }

    // Handle build profiling output
    if ( BuildProfiler::IsValid() )
    {
        // Chose description.
        // NOTE:
        // * String lifetime must extend past BuildProfiler destruction
        // * String contents feeds into color selection for profiling json
        const char * resultStr = "Compile";
        if ( systemError )
        {
            if ( raceWon )
            {
                resultStr = "(System Failure) (Race Won) Compile";
            }
            else if ( raceLost )
            {
                resultStr = "(System Failure) (Race Lost) Compile";
            }
            else
            {
                resultStr = "(System Failure) Compile";
            }
        }
        else if ( !result )
        {
            if ( raceWon )
            {
                resultStr = "(Failure) (Race Won) Compile";
            }
            else if ( raceLost )
            {
                resultStr = "(Failure) (Race Lost) Compile";
            }
            else
            {
                resultStr = "(Failure) Compile";
            }
        }

        // Record information about worker
        const int64_t start = receivedResultEndTime - (int64_t)( ( (double)buildTime / 1000 ) * (double)Timer::GetFrequency() );
        BuildProfiler::Get().RecordRemote( m_Worker->m_UniqueId,
                                           m_Worker->m_Address,
                                           remoteThreadId,
                                           start,
                                           receivedResultEndTime,
                                           resultStr,
                                           node->GetName().Get() );
    }

    // Handle verbose logging
    if ( m_DetailedLogging )
    {
        const char * resultStr = "";
        if ( systemError )
        {
            if ( raceWon )
            {
                resultStr = " (System Failure) (Race Won)";
            }
            else if ( raceLost )
            {
                resultStr = " (System Failure) (Race Lost)";
            }
            else
            {
                resultStr = " (System Failure)";
            }
        }
        else if ( !result )
        {
            if ( raceWon )
            {
                resultStr = " (Failure) (Race Won)";
            }
            else if ( raceLost )
            {
                resultStr = " (Failure) (Race Lost)";
            }
            else
            {
                resultStr = " (Failure)";
            }
        }
        DIST_INFO( "Got Result: %s - %s%s\n",
                   m_Worker->m_Address.Get(),
                   node->GetName().Get(),
                   resultStr );
    }

    if ( FLog::IsMonitorEnabled() )
    {
        AStackString msgBuffer;
        Job::GetMessagesForMonitorLog( messages, msgBuffer );

        FLOG_MONITOR( "FINISH_JOB %s %s \"%s\" \"%s\"\n",
                      result ? "SUCCESS" : "ERROR",
                      m_Worker->m_Address.Get(),
                      node->GetName().Get(),
                      msgBuffer.Get() );
    }

    // Should remote job be discarded?
    // Can happen in cases such as:
    //   a) local job won a race
    //   b) local race started and remote job was a system failure
    if ( job == nullptr )
    {
        // don't save result as we were cancelled
        return;
    }

    job->SetMessages( messages );

    if ( result == true )
    {
        // built ok - serialize to disc

        ObjectNode * objectNode = node->CastTo<ObjectNode>();

        // Decompress first: a corrupt result must not reach the cache
        MultiBuffer mb( data, dataSize );
        const bool decompressed = ( isCompressed == false ) || mb.Decompress();
        if ( decompressed )
        {
            m_ResultRawBytes.Add( mb.GetDataSize() );
        }

        // Store to cache if needed
        const bool writeToCache = decompressed &&
                                  FBuild::Get().GetOptions().m_UseCacheWrite &&
                                  objectNode->ShouldUseCache();
        if ( writeToCache )
        {
            if ( isCompressed )
            {
                // Write already compressed result to cache
                objectNode->WriteToCache_FromCompressedData( job,
                                                             data,
                                                             dataSize,
                                                             0 ); // compression time is remote and unknown
            }
            else
            {
                // Compress and write result to cache
                objectNode->WriteToCache_FromUncompressedData( job, data, dataSize );
            }
        }

        const AString & nodeName = objectNode->GetName();
        if ( decompressed == false )
        {
            FLOG_ERROR( "Failed to decompress the result for '%s' from %s\n", nodeName.Get(), m_Worker->m_Address.Get() );
            objectNode->SetStatFlag( Node::STATS_FAILED );
            result = false;
        }
        else if ( Node::EnsurePathExistsForFile( nodeName ) == false )
        {
            FLOG_ERROR( "Failed to create path for '%s'", nodeName.Get() );
            objectNode->SetStatFlag( Node::STATS_FAILED );
            result = false;
        }
        else
        {
            size_t fileIndex = 0;

            const ObjectNode * on = job->GetNode()->CastTo<ObjectNode>();

            // 1. Object file
            result = WriteFileToDisk( nodeName, mb, fileIndex++ );

            // 2. PDB file (optional)
            if ( result && on->IsUsingPDB() )
            {
                AStackString pdbName;
                on->GetPDBName( pdbName );
                result = WriteFileToDisk( pdbName, mb, fileIndex++ );
            }

            // 3. .nativecodeanalysis.xml (optional)
            if ( result && on->IsUsingStaticAnalysisMSVC() )
            {
                AStackString xmlFileName;
                on->GetNativeAnalysisXMLPath( xmlFileName );
                result = WriteFileToDisk( xmlFileName, mb, fileIndex++ );
            }

            // 4. .alt.obj (optional)
            if ( result && on->IsUsingDynamicDeopt() )
            {
                AStackString altObjName;
                on->GetAltObjPath( altObjName );
                result = WriteFileToDisk( altObjName, mb, fileIndex++ );
            }

            if ( result )
            {
                // record new file time
                objectNode->RecordStampFromBuiltFile();

                // record time taken to build
                objectNode->SetLastBuildTime( buildTime );
                objectNode->SetStatFlag( Node::STATS_BUILT );
                objectNode->SetStatFlag( Node::STATS_BUILT_REMOTE );
            }
            else
            {
                objectNode->SetStatFlag( Node::STATS_FAILED );
            }
        }

        // get list of messages during remote work
        AStackString msgBuffer;
        job->GetMessagesForLog( msgBuffer );

        if ( objectNode->IsMSVC() )
        {
            if ( objectNode->IsWarningsAsErrorsMSVC() == false )
            {
                FileNode::HandleWarningsMSVC( job, objectNode->GetName(), msgBuffer );
            }
        }
        else if ( objectNode->IsClangCl() )
        {
            if ( objectNode->IsWarningsAsErrorsMSVC() == false )
            {
                FileNode::HandleWarningsClangCl( job, objectNode->GetName(), msgBuffer );
            }
        }
        else if ( objectNode->IsClang() || objectNode->IsGCC() )
        {
            if ( objectNode->IsWarningsAsErrorsClangGCC() == false )
            {
                FileNode::HandleWarningsClangGCC( job, objectNode->GetName(), msgBuffer );
            }
        }
    }
    else
    {
        m_ResultRawBytes.Add( dataSize ); // Failure output is not compressed
        ( (FileNode *)node )->SetStatFlag( Node::STATS_FAILED );

        // was it a system error?
        if ( systemError )
        {
            // should we retry on another worker?
            if ( jobSystemErrorCount < SYSTEM_ERROR_ATTEMPT_COUNT )
            {
                // re-queue job which will be re-attempted on another worker
                JobQueue::Get().ReturnUnfinishedDistributableJob( job );
                return;
            }

            // failed too many times on different workers, add info about this to
            // error output
            if ( failureOutput.EndsWith( '\n' ) == false )
            {
                failureOutput += '\n';
            }
            failureOutput.AppendFormat( "FBuild: Error: Task failed on %u different workers\n", (uint32_t)SYSTEM_ERROR_ATTEMPT_COUNT );
        }

        Node::DumpOutput( nullptr, failureOutput, nullptr );
    }

    JobQueue::Get().FinishedProcessingJob( job,
                                           result ? Node::BuildResult::eOk : Node::BuildResult::eFailed,
                                           true ); // remote job

    // Push more jobs to fill freed capacity
    PushJobsToWorker();
}

// Process( MsgRequestManifest )
//------------------------------------------------------------------------------
void ClientToWorkerConnection::Process( const ConnectionInfo * connection,
                                        const Protocol::MsgRequestManifest * msg )
{
    PROFILE_SECTION( "MsgRequestManifest" );

    // find a job associated with this client with this toolId
    const uint64_t toolId = msg->GetToolId();
    ASSERT( toolId );
    const ToolManifest * manifest = FindManifest( toolId );

    if ( manifest == nullptr )
    {
        // client asked for a manifest that is not valid
        ASSERT( false ); // this indicates a logic bug
        Disconnect( connection );
        return;
    }

    MemoryStream ms;
    manifest->SerializeForRemote( ms );

    // Send manifest to worker
    EnqueueSend( Protocol::MsgManifest( toolId ),
                 Move( ConstMemoryStream( Move( ms ) ) ) );
}

// Process ( MsgRequestFile )
//------------------------------------------------------------------------------
void ClientToWorkerConnection::Process( const ConnectionInfo * connection,
                                        const Protocol::MsgRequestFile * msg )
{
    PROFILE_SECTION( "MsgRequestFile" );

    // find a job associated with this client with this toolId
    const uint64_t toolId = msg->GetToolId();
    ASSERT( toolId != 0 ); // server should not request 'no sync' tool id
    const ToolManifest * manifest = FindManifest( toolId );

    if ( manifest == nullptr )
    {
        // client asked for a manifest that is not valid
        ASSERT( false ); // this indicates a logic bug
        Disconnect( connection );
        return;
    }

    const uint32_t fileId = msg->GetFileId();
    size_t dataSize( 0 );
    const void * data = manifest->GetFileData( fileId, dataSize );
    if ( !data )
    {
        ASSERT( false ); // something is terribly wrong
        Disconnect( connection );
        return;
    }

    ConstMemoryStream ms( data, dataSize );

    // Send file to worker
    EnqueueSend( Protocol::MsgFile( toolId, fileId ),
                 Move( ms ),
                 SendKind::Toolchain );
}

// FindManifest
//------------------------------------------------------------------------------
const ToolManifest * ClientToWorkerConnection::FindManifest( uint64_t toolId ) const
{
    MutexHolder mh( m_Mutex );

    for ( const Job * job : m_Jobs )
    {
        const Node * n = job->GetNode()->CastTo<ObjectNode>()->GetCompiler();
        const ToolManifest & m = n->CastTo<CompilerNode>()->GetManifest();
        if ( m.GetToolId() == toolId )
        {
            // found a job with the same toolid
            return &m;
        }
    }

    return nullptr;
}

// WriteFileToDisk
//------------------------------------------------------------------------------
bool ClientToWorkerConnection::WriteFileToDisk( const AString & fileName, const MultiBuffer & multiBuffer, size_t index ) const
{
    if ( multiBuffer.ExtractFile( index, fileName ) == false )
    {
        FLOG_ERROR( "Failed to create file. Error: %s File: '%s'", LAST_ERROR_STR, fileName.Get() );
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
ClientToWorkerConnection::ClientToWorkerConnection( Client * client,
                                                    bool detailedLogging,
                                                    ClientWorkerInfo * worker )
    : m_Client( client )
    , m_DetailedLogging( detailedLogging )
    , m_Worker( worker )
{
    SetConnectSendBufferSize( Protocol::kClientSendBufferSize );
    m_SendThread.Start( SendQueueThreadFuncStatic, "ClientSend", this );
}

//------------------------------------------------------------------------------
ClientToWorkerConnection::~ClientToWorkerConnection()
{
    m_SendThreadQuit.Store( true );
    m_SendThreadWakeSemaphore.Signal();
    m_SendThread.Join();
}

//------------------------------------------------------------------------------
template <class T>
void ClientToWorkerConnection::EnqueueSend( const T & msg )
{
    ClientSendQueueItem item( msg );
    {
        MutexHolder lock( m_SendQueueMutex );
        m_SendQueue.Push( Move( item ) );
    }
    m_SendThreadWakeSemaphore.Signal();
}

//------------------------------------------------------------------------------
template <class T>
void ClientToWorkerConnection::EnqueueSend( const T & msg,
                                            ConstMemoryStream && payload,
                                            SendKind kind,
                                            uint64_t rawBytes,
                                            uint64_t pchId )
{
    ClientSendQueueItem item( msg, Move( payload ), kind, rawBytes, false, pchId );
    {
        MutexHolder lock( m_SendQueueMutex );
        m_SendQueue.Push( Move( item ) );
    }
    m_SendThreadWakeSemaphore.Signal();
}

//------------------------------------------------------------------------------
void ClientToWorkerConnection::EnqueueFileSend( const Protocol::IMessage & msg,
                                                const AString & payloadFile,
                                                uint64_t payloadOffset,
                                                uint32_t payloadLength,
                                                SendKind kind,
                                                uint64_t rawBytes,
                                                uint64_t pchId,
                                                bool pchTransformed )
{
    ClientSendQueueItem item( msg, payloadFile, payloadOffset, payloadLength, kind, rawBytes, pchId, pchTransformed );
    {
        MutexHolder lock( m_SendQueueMutex );
        m_SendQueue.Push( Move( item ) );
    }
    m_SendThreadWakeSemaphore.Signal();
}

//------------------------------------------------------------------------------
void ClientToWorkerConnection::EnqueueJob( const Protocol::MsgJob & msg, ConstMemoryStream && payload )
{
    m_UnsentJobs.Increment();
    const uint64_t rawBytes = payload.GetSize();
    ClientSendQueueItem item( msg, Move( payload ), SendKind::Job, rawBytes, true );
    {
        MutexHolder lock( m_SendQueueMutex );
        m_SendQueue.Push( Move( item ) );
    }
    m_SendThreadWakeSemaphore.Signal();
}

//------------------------------------------------------------------------------
/*static*/ uint32_t ClientToWorkerConnection::SendQueueThreadFuncStatic( void * param )
{
    ClientToWorkerConnection * c = static_cast<ClientToWorkerConnection *>( param );

    c->SendQueueThreadFunc();

    // Thread is safe to join and ClientToServerConnection can be freed after that
    c->m_Complete.Store( true );

    c->ShutdownAllConnections();

    return 0;
}

//------------------------------------------------------------------------------
void ClientToWorkerConnection::SendQueueThreadFunc()
{
    PROFILE_SET_THREAD_NAME( AStackString().Format( "C2S_%s", m_Worker->m_Address.Get() ).Get() );
    PROFILE_FUNCTION;

    // Initiate connection
    DIST_INFO( "Connecting to: %s\n", m_Worker->m_Address.Get() );
    const ConnectionInfo * ci = Connect( m_Worker->m_Address,
                                         m_Client->GetPort(),
                                         2000, // 2s timeout
                                         this );
    if ( ci == nullptr )
    {
        // Connection failed
        DIST_INFO( " - connection: %s (FAILED)\n", m_Worker->m_Address.Get() );
        m_Worker->m_ConnectionDelayTimer.Restart(); // reset connection attempt delay
        return;
    }

    // Note completed connection
    ClientWorkerInfo::s_NumConnections.Increment();
    DIST_INFO( " - connection: %s (OK)\n", m_Worker->m_Address.Get() );

    // Send initial connection handshake
    const Protocol::MsgConnection msg;
    if ( msg.Send( ci ) )
    {
        SendQueueMainLoop( ci );
    }
    WriteStats();

    // A worker that drops the connection before the handshake is not tried again straight away
    if ( m_HandshakeDone.Load() == false )
    {
        m_Worker->m_ConnectionDelayTimer.Restart();
    }

    // Note terminated connection
    ASSERT( ClientWorkerInfo::s_NumConnections.Load() > 0 );
    ClientWorkerInfo::s_NumConnections.Decrement();
}

// WriteStats
//------------------------------------------------------------------------------
void ClientToWorkerConnection::WriteStats() const
{
    DistributionConnectionStats stats;
    stats.m_Toolchain = m_SendStats[ (size_t)SendKind::Toolchain ];
    stats.m_Pch = m_SendStats[ (size_t)SendKind::Pch ];
    stats.m_Jobs = m_SendStats[ (size_t)SendKind::Job ];
    stats.m_ResultsReceived = m_ResultsReceived.Load();
    stats.m_ResultBytesReceived = m_ResultBytesReceived.Load();
    stats.m_ResultRawBytes = m_ResultRawBytes.Load();

    // Connections that never got as far as sending work are not interesting
    if ( ( stats.m_Toolchain.m_Messages + stats.m_Pch.m_Messages + stats.m_Jobs.m_Messages ) == 0 )
    {
        return;
    }

    stats.m_WorkerAddress = m_Worker->m_Address;
    stats.m_ConnectedTime = m_ConnectionTimer.GetElapsed();
    stats.m_NumSkippedToolchains = m_NumSkippedToolchains.Load();
    stats.m_NumTransformedPchs = m_NumTransformedPchs.Load();
    DistributionStats::WriteToTempLog( stats );
}

//------------------------------------------------------------------------------
void ClientToWorkerConnection::SendQueueMainLoop( const ConnectionInfo * ci )
{
    // Send messages
    while ( true )
    {
        // Take the queued messages. Drain fully before blocking on semaphore.
        Array<ClientSendQueueItem> sendQueue;
        {
            MutexHolder lock( m_SendQueueMutex );
            m_SendQueue.Take( sendQueue );
        }

        // If nothing to send, block until signaled
        if ( sendQueue.IsEmpty() )
        {
            m_SendThreadWakeSemaphore.Wait();

            // When client has been signaled to exit, we should exit as well
            if ( m_SendThreadQuit.Load() == true )
            {
                break;
            }
            continue; // Re-check queue after waking
        }

        for ( const ClientSendQueueItem & item : sendQueue )
        {
            if ( SendItem( ci, item ) == false )
            {
                return;
            }
        }
    }
}

//------------------------------------------------------------------------------
bool ClientToWorkerConnection::SendItem( const ConnectionInfo * ci, const ClientSendQueueItem & item )
{
    PROFILE_SECTION( "SendMsg" );

    const double queueDelay = (double)item.m_Enqueued.GetElapsed();

    // Compress here, in the order jobs go out, as each is compressed against the ones before it
    const void * payload = item.m_Payload.GetData();
    size_t payloadSize = item.m_Payload.GetSize();
    UniquePtr<char, FreeDeletor> fileSlice;
    if ( item.m_PayloadFile.IsEmpty() == false )
    {
        // Only this slice of a large file is in memory
        FileStream file;
        fileSlice.Replace( static_cast<char *>( ALLOC( item.m_PayloadLength ) ) );
        if ( ( file.Open( item.m_PayloadFile.Get(), FileStream::READ_ONLY ) == false ) ||
             ( file.Seek( item.m_PayloadOffset ) == false ) ||
             ( file.Read( fileSlice.Get(), item.m_PayloadLength ) != item.m_PayloadLength ) )
        {
            FLOG_WARN( "Failed to read '%s' to send it to %s. Closing the connection.\n",
                       item.m_PayloadFile.Get(), m_Worker->m_Address.Get() );
            m_SendThreadQuit.Store( true );
            return false;
        }
        payload = fileSlice.Get();
        payloadSize = item.m_PayloadLength;
    }
    double encodeTime = 0.0;
    if ( item.m_StreamCompress )
    {
        PROFILE_SECTION( "CompressJob" );
        const Timer encodeTimer;
        if ( m_JobEncoder.Encode( payload, payloadSize ) == false )
        {
            FLOG_WARN( "Failed to compress a job for %s. Closing the connection.\n", m_Worker->m_Address.Get() );
            m_SendThreadQuit.Store( true );
            return false;
        }
        payload = m_JobEncoder.GetResult();
        payloadSize = m_JobEncoder.GetResultSize();
        encodeTime = encodeTimer.GetElapsed();
    }

    // The last chunk of a PCH counts as delivered before it is sent: a worker that has lost the PCH asks
    // again as soon as it has it, which can be before this thread returns from the send
    if ( item.m_PchId != 0 )
    {
        Client::RunTestHook( Client::BEFORE_PCH_DELIVERED, m_SendThreadQuit );
        MutexHolder mh( m_Mutex );
        m_PchState.OnDelivered( item.m_PchId );
    }

    // Send message
    const Timer sendTimer;
    const bool sendOk = item.m_HasPayload ? Send( ci,
                                                  item.m_Message.GetData(),
                                                  item.m_Message.GetSize(),
                                                  payload,
                                                  payloadSize )
                                          : Send( ci,
                                                  item.m_Message.GetData(),
                                                  item.m_Message.GetSize() );
    if ( item.m_Kind == SendKind::Job )
    {
        m_UnsentJobs.Decrement();
    }

    // If the send fails, we should be disconnected
    ASSERT( sendOk || ( m_SendThreadQuit.Load() == true ) );

    if ( sendOk )
    {
        if ( item.m_PchTransformed && ( item.m_PchId != 0 ) )
        {
            m_NumTransformedPchs.Increment(); // Delivered
        }
        DistributionSendStats & stats = m_SendStats[ (size_t)item.m_Kind ];
        stats.m_Messages++;
        stats.m_RawBytes += item.m_RawBytes;
        stats.m_SentBytes += ( item.m_Message.GetSize() + ( item.m_HasPayload ? payloadSize : 0 ) );
        stats.m_SendTime += sendTimer.GetElapsed();
        stats.m_EncodeTime += encodeTime;
        stats.m_LastSentAt = m_ConnectionTimer.GetElapsed();
        if ( item.m_Kind != SendKind::Pch ) // Bulk data is meant to wait
        {
            stats.m_MaxQueueDelay = Math::Max( stats.m_MaxQueueDelay, queueDelay );
        }
    }

    // If signaled to exit for any reason, don't process additional items
    // (exit signal can come from a send failure above, or a receive failure
    // on the TCPConnectionPool receive thread)
    return ( m_SendThreadQuit.Load() == false );
}

//------------------------------------------------------------------------------
