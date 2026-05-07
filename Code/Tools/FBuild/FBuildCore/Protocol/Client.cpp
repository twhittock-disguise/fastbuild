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
#include "Tools/FBuild/FBuildCore/Helpers/PchDataCache.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/Helpers/ToolManifest.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/Job.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/JobQueue.h"

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
#define CONNECTION_REATTEMPT_DELAY_TIME ( 10.0f )
#define SYSTEM_ERROR_ATTEMPT_COUNT ( 3u )
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
    bool m_DenyListed = false; // Misbehaving workers are disabled for the rest of the build
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
                                  ConstMemoryStream && payload )
        : m_Message( msg.GetSize() ) // Prealloc exact size needed
        , m_HasPayload( true )
        , m_Payload( Move( payload ) )
    {
        VERIFY( m_Message.WriteBuffer( &msg, msg.GetSize() ) == msg.GetSize() );
    }

    // non copyable
    explicit ClientSendQueueItem( const ClientSendQueueItem & other ) = delete;
    void operator=( const ClientSendQueueItem & other ) = delete;

    // movable
    explicit ClientSendQueueItem( ClientSendQueueItem && other ) = default;
    ClientSendQueueItem & operator=( ClientSendQueueItem && other ) = default;

    MemoryStream m_Message;
    bool m_HasPayload = false;
    ConstMemoryStream m_Payload;
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

    ClientWorkerInfo * GetWorker() const { return m_Worker; }

    void PushJobsToWorker();

    template <class T>
    void EnqueueSend( const T & msg );
    template <class T>
    void EnqueueSend( const T & msg, ConstMemoryStream && payload );

private:
    virtual void OnDisconnected( const ConnectionInfo * connection ) override;
    virtual void OnReceive( const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory ) override;

    void Process( const Protocol::MsgJobResult *, const void * payload, size_t payloadSize );
    void Process( const Protocol::MsgJobResultCompressed * msg, const void * payload, size_t payloadSize );
    void Process( const ConnectionInfo * connection, const Protocol::MsgRequestManifest * msg );
    void Process( const ConnectionInfo * connection, const Protocol::MsgRequestFile * msg );
    void Process( const Protocol::MsgConnectionAck * msg );
    void Process( const Protocol::MsgPchInventory * msg, const void * payload, size_t payloadSize );

    void ProcessJobResultCommon( bool isCompressed, const void * payload, size_t payloadSize );

    const ToolManifest * FindManifest( uint64_t toolId ) const;
    bool WriteFileToDisk( const AString & fileName, const MultiBuffer & multiBuffer, size_t index ) const;

    // Thread to process sending messages
    static uint32_t SendQueueThreadFuncStatic( void * param );
    void SendQueueThreadFunc();
    void SendQueueMainLoop( const ConnectionInfo * ci );

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
    Atomic<uint8_t> m_ProtocolVersionMinor;

    // Push-based job tracking
    Atomic<uint32_t> m_WorkerCapacity{ 0 };
    Atomic<uint32_t> m_JobsInFlight{ 0 };
    Array<uint64_t> m_SentToolchains; // Protected by m_Mutex
    Array<uint64_t> m_SentPchIds;     // Protected by m_Mutex

    // Queue of messages to send
    Mutex m_SendQueueMutex;
    Array<ClientSendQueueItem> m_SendQueue;
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
uint32_t Client::GetNumConnections() const
{
    return ClientWorkerInfo::s_NumConnections.Load();
}

//------------------------------------------------------------------------------
/*virtual*/ void ClientToWorkerConnection::OnDisconnected( const ConnectionInfo * /*connection*/ )
{
    MutexHolder mh( m_Mutex );
    DIST_INFO( "Disconnected: %s\n", m_Worker->m_Address.Get() );
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

    // This is usually null here, but might need to be freed if
    // we had the connection drop between message and payload
    FREE( (void *)( m_CurrentMessage ) );
    m_CurrentMessage = nullptr;

    // Signal send thread to exit
    m_SendThreadQuit.Store( true );
    m_SendThreadWakeSemaphore.Signal();

    // Wait for send thread to exit so that TCPConnectionPool can safely
    // free the ConnectionInfo when this function returns
    // (i.e. ensure the send thread has finished using it)
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
        workerInfo.m_ConnectionDelayTimer.SetElapsed( CONNECTION_REATTEMPT_DELAY_TIME );
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
        if ( worker.m_DenyListed )
        {
            continue;
        }

        // Have we tried this worker very recently?
        if ( worker.m_ConnectionDelayTimer.GetElapsed() < CONNECTION_REATTEMPT_DELAY_TIME )
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
            Process( msg );
            break;
        }
        case Protocol::MSG_PCH_INVENTORY:
        {
            const Protocol::MsgPchInventory * msg = static_cast<const Protocol::MsgPchInventory *>( imsg );
            Process( msg, payload, payloadSize );
            break;
        }
        default:
        {
            // unknown message type
            ASSERT( false ); // this indicates a protocol bug
            DIST_INFO( "Protocol Error: %s\n", m_Worker->m_Address.Get() );
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
void ClientToWorkerConnection::Process( const Protocol::MsgConnectionAck * msg )
{
    PROFILE_SECTION( "MsgConnectionAck" );

    // The connection would be dropped and we would not get an ack if the major
    // protocol version is mismatched.
    ASSERT( msg->GetProtocolVersionMajor() == Protocol::kVersionMajor );

    // Take note of additional server info
    m_WorkerVersion.Store( msg->GetWorkerVersion() );
    m_ProtocolVersionMinor.Store( msg->GetProtocolVersionMinor() );
    m_WorkerCapacity.Store( msg->GetWorkerCapacity() );
    DIST_INFO( " - Worker %s is v%u.%u (protocol v%u.%u) capacity=%u\n",
               m_Worker->m_Address.Get(),
               ( m_WorkerVersion.Load() / 100U ),
               ( m_WorkerVersion.Load() % 100U ),
               Protocol::kVersionMajor,
               m_ProtocolVersionMinor.Load(),
               msg->GetWorkerCapacity() );

    // Push initial batch of jobs to fill worker capacity
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
    if ( numEntries == 0 )
    {
        return;
    }

    // Validate payload size
    const size_t expectedSize = (size_t)numEntries * sizeof( uint64_t );
    if ( payloadSize < expectedSize )
    {
        DIST_INFO( " - Worker %s sent truncated PCH inventory (%zu < %zu)\n",
                   m_Worker->m_Address.Get(), payloadSize, expectedSize );
        return;
    }

    // Pre-populate m_SentPchIds with all IDs the worker already has
    const uint64_t * pchIds = static_cast<const uint64_t *>( payload );
    {
        MutexHolder mh( m_Mutex );
        for ( uint32_t i = 0; i < numEntries; ++i )
        {
            if ( m_SentPchIds.Find( pchIds[ i ] ) == nullptr )
            {
                m_SentPchIds.Append( pchIds[ i ] );
            }
        }
    }

    DIST_INFO( " - Worker %s has %u cached PCH entries\n",
               m_Worker->m_Address.Get(), numEntries );
}

// PushJobsToWorker
//------------------------------------------------------------------------------
void ClientToWorkerConnection::PushJobsToWorker()
{
    PROFILE_SECTION( "PushJobsToWorker" );

    const uint32_t capacity = m_WorkerCapacity.Load();
    const uint32_t inFlight = m_JobsInFlight.Load();
    if ( ( inFlight >= capacity ) || m_Worker->m_DenyListed || ( capacity == 0 ) )
    {
        return;
    }

    const uint8_t workerMinorProtocolVersion = m_ProtocolVersionMinor.Load();
    uint32_t toSend = capacity - inFlight;

    while ( toSend > 0 )
    {
        Job * job = JobQueue::Get().GetDistributableJobToProcess( true, workerMinorProtocolVersion );
        if ( job == nullptr )
        {
            break;
        }

        const Node * n = job->GetNode()->CastTo<ObjectNode>()->GetCompiler();
        ASSERT( n );
        const ToolManifest & manifest = n->CastTo<CompilerNode>()->GetManifest();
        const uint64_t toolId = manifest.GetToolId();

        // Push toolchain if not yet sent to this worker
        {
            MutexHolder mh( m_Mutex );
            if ( m_SentToolchains.Find( toolId ) == nullptr )
            {
                MemoryStream ms;
                manifest.SerializeForRemote( ms );
                EnqueueSend( Protocol::MsgManifest( toolId ),
                             Move( ConstMemoryStream( Move( ms ) ) ) );

                const Array<ToolManifestFile> & files = manifest.GetFiles();
                for ( uint32_t i = 0; i < files.GetSize(); ++i )
                {
                    size_t dataSize = 0;
                    const void * data = manifest.GetFileData( i, dataSize );
                    if ( data )
                    {
                        EnqueueSend( Protocol::MsgFile( toolId, i ),
                                     Move( ConstMemoryStream( data, dataSize ) ) );
                    }
                }
                m_SentToolchains.Append( toolId );
            }
        }

        // Push PCH data if not yet sent to this worker
        const uint64_t pchId = job->GetPchId();
        if ( pchId != 0 )
        {
            // Check under lock whether we've already sent this PCH
            bool needsSend = false;
            PchDataCache::Entry entry;
            {
                MutexHolder mh( m_Mutex );
                if ( m_SentPchIds.Find( pchId ) == nullptr )
                {
                    needsSend = PchDataCache::Get().Find( pchId, entry );
                    if ( needsSend )
                    {
                        m_SentPchIds.Append( pchId ); // Mark as in-flight to prevent duplicates
                    }
                }
            }

            // Read + compress outside the lock to avoid blocking the connection
            if ( needsSend )
            {
                FileStream pchFile;
                if ( pchFile.Open( entry.filePath.Get(), FileStream::READ_ONLY ) == false )
                {
                    FLOG_WARN( "PCH distribution: failed to open '%s' for sending\n", entry.filePath.Get() );
                }
                else
                {
                    const uint32_t fileSize = entry.uncompressedSize;
                    UniquePtr<char, FreeDeletor> pchData( (char *)ALLOC( fileSize ) );
                    if ( pchFile.Read( pchData.Get(), fileSize ) == fileSize )
                    {
                        pchFile.Close();

                        Compressor c;
                        c.Compress( pchData.Get(), fileSize );

                        MemoryStream ms;
                        ms.WriteBuffer( c.GetResult(), c.GetResultSize() );

                        EnqueueSend( Protocol::MsgPchFile( pchId, entry.uncompressedSize ),
                                     Move( ConstMemoryStream( Move( ms ) ) ) );

                        DIST_INFO( " - Sent PCH 0x%016" PRIx64 " to %s (%zu bytes compressed)\n",
                                   pchId, m_Worker->m_Address.Get(), c.GetResultSize() );
                    }
                }
            }
        }

        // Track and send job
        {
            MutexHolder mh( m_Mutex );
            m_Jobs.Append( job );
        }
        m_JobsInFlight.Increment();

        if ( FBuild::Get().GetOptions().m_ShowCommandSummary )
        {
            FLOG_OUTPUT( "-> Obj: %s <REMOTE: %s>\n",
                         job->GetNode()->GetName().Get(), m_Worker->m_Address.Get() );
        }
        FLOG_MONITOR( "START_JOB %s \"%s\" \n", m_Worker->m_Address.Get(), job->GetNode()->GetName().Get() );

        MemoryStream stream;
        job->Serialize( stream );

        // Determine compression level for results
        int16_t resultCompressionLevel = -1;
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

        EnqueueSend( Protocol::MsgJob( toolId, resultCompressionLevel ),
                     Move( ConstMemoryStream( Move( stream ) ) ) );

        toSend--;
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
        VERIFY( m_Jobs.FindDerefAndErase( jobId ) );
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
        m_Worker->m_DenyListed = true;

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

        // Store to cache if needed
        const bool writeToCache = FBuild::Get().GetOptions().m_UseCacheWrite &&
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

        // Decompress if needed
        MultiBuffer mb( data, dataSize );
        if ( isCompressed )
        {
            mb.Decompress();
        }

        const AString & nodeName = objectNode->GetName();
        if ( Node::EnsurePathExistsForFile( nodeName ) == false )
        {
            FLOG_ERROR( "Failed to create path for '%s'", nodeName.Get() );
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
                 Move( ms ) );
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
    {
        MutexHolder lock( m_SendQueueMutex );
        m_SendQueue.EmplaceBack( msg );
    }
    m_SendThreadWakeSemaphore.Signal();
}

//------------------------------------------------------------------------------
template <class T>
void ClientToWorkerConnection::EnqueueSend( const T & msg, ConstMemoryStream && payload )
{
    {
        MutexHolder lock( m_SendQueueMutex );
        m_SendQueue.EmplaceBack( msg, Move( payload ) );
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

    // Note terminated connection
    ASSERT( ClientWorkerInfo::s_NumConnections.Load() > 0 );
    ClientWorkerInfo::s_NumConnections.Decrement();
}

//------------------------------------------------------------------------------
void ClientToWorkerConnection::SendQueueMainLoop( const ConnectionInfo * ci )
{
    // Send messages
    while ( true )
    {
        // Take queued messages. Drain fully before blocking on semaphore.
        Array<ClientSendQueueItem> sendQueue;
        {
            MutexHolder lock( m_SendQueueMutex );
            m_SendQueue.Swap( sendQueue );
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
            PROFILE_SECTION( "SendMsg" );

            // Send message
            const bool sendOk = item.m_HasPayload ? Send( ci,
                                                          item.m_Message.GetData(),
                                                          item.m_Message.GetSize(),
                                                          item.m_Payload.GetData(),
                                                          item.m_Payload.GetSize() )
                                                  : Send( ci,
                                                          item.m_Message.GetData(),
                                                          item.m_Message.GetSize() );

            // If the send fails, we should be disconnected
            ASSERT( sendOk || ( m_SendThreadQuit.Load() == true ) );
            (void)sendOk;

            // If signaled to exit for any reason, don't process additional items
            // (exit signal can come from a send failure above, or a receive failure
            // on the TCPConnectionPool receive thread)
            if ( m_SendThreadQuit.Load() == true )
            {
                return;
            }
        }
    }
}

//------------------------------------------------------------------------------
