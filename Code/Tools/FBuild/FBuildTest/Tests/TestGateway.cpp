// TestGateway.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildTest/Tests/FBuildTest.h"
#include "Tools/FBuild/FBuildTest/Tests/DistributedTestHelpers.h"
#include "Tools/FBuild/FBuildTest/Tests/SyntheticToolchain.h"

#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/Gateway/GatewayWire.h"
#include "Tools/FBuild/FBuildCore/Helpers/JobStream.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/Protocol/Server.h"
#include "Tools/FBuild/FBuildCore/Protocol/WorkerSendQueue.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/JobQueueRemote.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Containers/Move.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/Mem/Mem.h"
#include "Core/Network/TCPConnectionPool.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Mutex.h"
#include "Core/Process/Thread.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Timer.h"

#include <string.h>

// Defines
//------------------------------------------------------------------------------
namespace
{
    constexpr uint16_t kGatewayTestDownstreamPort = Protocol::kTestPort + 1;

    // Listens as a worker would and records the messages that arrive, in order
    class MessageRecorder : public TCPConnectionPool
    {
    public:
        MessageRecorder() = default;
        virtual ~MessageRecorder() override { ShutdownAllConnections(); }
        explicit MessageRecorder( const MessageRecorder & other ) = delete;
        MessageRecorder & operator=( const MessageRecorder & other ) = delete;

        size_t GetNumMessages() const
        {
            MutexHolder mh( m_Mutex );
            return m_Types.GetSize();
        }
        uint8_t GetType( size_t index ) const
        {
            MutexHolder mh( m_Mutex );
            return m_Types[ index ];
        }
        void GetPayload( size_t index, Array<char> & outPayload ) const
        {
            MutexHolder mh( m_Mutex );
            outPayload = m_Payloads[ index ];
        }

    private:
        virtual void OnReceive( const ConnectionInfo *, void * data, uint32_t size, bool & keepMemory ) override
        {
            keepMemory = true; // freed below
            void * payload = nullptr;
            if ( m_CurrentMessage == nullptr )
            {
                m_CurrentMessage = static_cast<const Protocol::IMessage *>( data );
                if ( m_CurrentMessage->HasPayload() )
                {
                    return;
                }
            }
            else
            {
                payload = data;
            }

            {
                MutexHolder mh( m_Mutex );
                m_Types.Append( (uint8_t)m_CurrentMessage->GetType() );
                Array<char> & recorded = m_Payloads.EmplaceBack();
                if ( payload )
                {
                    recorded.Append( static_cast<const char *>( payload ), static_cast<const char *>( payload ) + size );
                }
            }
            FREE( (void *)m_CurrentMessage );
            FREE( payload );
            m_CurrentMessage = nullptr;
        }

        mutable Mutex m_Mutex;
        Array<uint8_t> m_Types;
        Array<Array<char>> m_Payloads; // Empty for a message without one
        const Protocol::IMessage * m_CurrentMessage = nullptr;
    };

    // Records when the queue reports a PCH delivered, and what had been sent by then
    class DeliveryRecorder : public WorkerSendQueue::Listener
    {
    public:
        virtual ~DeliveryRecorder() = default;
        virtual void OnPchDelivered( uint64_t pchId ) override
        {
            m_PchIds.Append( pchId );
            m_PchsSentBefore.Append( m_Queue ? m_Queue->GetStats( SendKind::Pch ).m_Messages : 0 );
        }

        const WorkerSendQueue * m_Queue = nullptr;
        Array<uint64_t> m_PchIds;         // Send thread only, until it is joined
        Array<uint32_t> m_PchsSentBefore;
    };

    // Runs a queue on its own thread, as a connection's send thread does, connected to a recorder
    class QueueRunner
    {
    public:
        explicit QueueRunner( WorkerSendQueue & queue )
            : m_Queue( queue )
        {
        }
        explicit QueueRunner( const QueueRunner & other ) = delete;
        QueueRunner & operator=( const QueueRunner & other ) = delete;
        ~QueueRunner()
        {
            Stop();
            m_Pool.ShutdownAllConnections();
        }

        bool Start()
        {
            m_Connection = m_Pool.Connect( AStackString( "127.0.0.1" ), kGatewayTestDownstreamPort, 2000 );
            if ( m_Connection == nullptr )
            {
                return false;
            }
            m_Thread.Start( ThreadFunc, "SendQueueTest", this );
            return true;
        }
        bool HasReturned() const { return m_Returned.Load(); }
        void Stop()
        {
            if ( m_Thread.IsRunning() )
            {
                m_Queue.Quit();
                m_Thread.Join();
            }
        }

    private:
        static uint32_t ThreadFunc( void * param )
        {
            QueueRunner * runner = static_cast<QueueRunner *>( param );
            runner->m_Queue.Run( runner->m_Pool, runner->m_Connection );
            runner->m_Returned.Store( true );
            return 0;
        }

        WorkerSendQueue & m_Queue;
        TCPConnectionPool m_Pool;
        const ConnectionInfo * m_Connection = nullptr;
        Thread m_Thread;
        Atomic<bool> m_Returned{ false };
    };

    // Records what a gateway Server hands its forwarder
    class StubForwarder : public Server::Forwarder
    {
    public:
        explicit StubForwarder( uint32_t poolSlots, const Array<uint64_t> & toolIds = Array<uint64_t>() )
            : m_PoolSlots( poolSlots )
            , m_ToolIds( toolIds )
        {
        }
        virtual ~StubForwarder() = default;
        explicit StubForwarder( const StubForwarder & other ) = delete;
        StubForwarder & operator=( const StubForwarder & other ) = delete;

        struct Received
        {
            uint32_t    m_ClientId = 0;
            uint64_t    m_ToolId = 0;
            uint32_t    m_FileId = 0;   // Of a file
            int16_t     m_Level = 0;    // Of a job
            Array<char> m_Data;
        };
        struct StoredPch
        {
            uint64_t    m_PchId;
            uint32_t    m_UncompressedSize;
            uint32_t    m_CompressedSize;
        };

        uint32_t GetNumPoolSlotQueries() const { return m_NumPoolSlotQueries.Load(); }
        void GetInventoryClients( Array<uint32_t> & out ) const { MutexHolder mh( m_Mutex ); out = m_InventoryClients; }
        void GetJobs( Array<Received> & out ) const { MutexHolder mh( m_Mutex ); out = m_Jobs; }
        void GetManifests( Array<Received> & out ) const { MutexHolder mh( m_Mutex ); out = m_Manifests; }
        void GetFiles( Array<Received> & out ) const { MutexHolder mh( m_Mutex ); out = m_Files; }
        void GetStoredPchs( Array<StoredPch> & out ) const { MutexHolder mh( m_Mutex ); out = m_StoredPchs; }
        void GetGoneClients( Array<uint32_t> & out ) const { MutexHolder mh( m_Mutex ); out = m_GoneClients; }
        size_t GetNumJobs() const { MutexHolder mh( m_Mutex ); return m_Jobs.GetSize(); }
        size_t GetNumToolchainMessages() const { MutexHolder mh( m_Mutex ); return m_Manifests.GetSize() + m_Files.GetSize(); }
        size_t GetNumStoredPchs() const { MutexHolder mh( m_Mutex ); return m_StoredPchs.GetSize(); }
        size_t GetNumGoneClients() const { MutexHolder mh( m_Mutex ); return m_GoneClients.GetSize(); }

        virtual uint32_t GetPoolSlots() const override
        {
            m_NumPoolSlotQueries.Increment();
            return m_PoolSlots;
        }
        virtual void GetToolchainInventory( uint32_t clientId, Array<uint64_t> & outToolIds ) override
        {
            MutexHolder mh( m_Mutex );
            m_InventoryClients.Append( clientId );
            outToolIds = m_ToolIds;
        }
        virtual void OnClientJob( uint32_t clientId, const Protocol::MsgJob & msg, void * jobData, size_t jobDataSize ) override
        {
            Received job;
            job.m_ClientId = clientId;
            job.m_ToolId = msg.GetToolId();
            job.m_Level = msg.GetResultCompressionLevel();
            job.m_Data.Append( static_cast<const char *>( jobData ), static_cast<const char *>( jobData ) + jobDataSize );
            FREE( jobData );
            MutexHolder mh( m_Mutex );
            m_Jobs.Append( job );
        }
        virtual void OnClientManifest( uint32_t clientId, uint64_t toolId, const void * payload, size_t payloadSize ) override
        {
            MutexHolder mh( m_Mutex );
            Record( m_Manifests, clientId, toolId, 0, payload, payloadSize );
        }
        virtual void OnClientFile( uint32_t clientId, uint64_t toolId, uint32_t fileId, const void * payload, size_t payloadSize ) override
        {
            MutexHolder mh( m_Mutex );
            Record( m_Files, clientId, toolId, fileId, payload, payloadSize );
        }
        virtual void OnPchStored( uint64_t pchId, uint32_t uncompressedSize, uint32_t compressedSize ) override
        {
            MutexHolder mh( m_Mutex );
            m_StoredPchs.Append( StoredPch{ pchId, uncompressedSize, compressedSize } );
        }
        virtual void OnClientGone( uint32_t clientId ) override
        {
            // No Server lock is held here, so the server can be called back
            bool queued = false;
            if ( m_Server )
            {
                MemoryStream payload;
                payload.Write( clientId );
                queued = m_Server->QueueResult( clientId, false, Move( payload ) );
            }
            MutexHolder mh( m_Mutex );
            m_GoneClients.Append( clientId );
            m_QueuedWhenGone.Append( queued );
        }

        // Set to have each OnClientGone try to queue a result for the client
        Server * m_Server = nullptr;
        void GetQueuedWhenGone( Array<bool> & out ) const { MutexHolder mh( m_Mutex ); out = m_QueuedWhenGone; }

    private:
        static void Record( Array<Received> & to, uint32_t clientId, uint64_t toolId, uint32_t fileId, const void * payload, size_t payloadSize )
        {
            Received & r = to.EmplaceBack();
            r.m_ClientId = clientId;
            r.m_ToolId = toolId;
            r.m_FileId = fileId;
            r.m_Data.Append( static_cast<const char *>( payload ), static_cast<const char *>( payload ) + payloadSize );
        }

        const uint32_t m_PoolSlots;
        const Array<uint64_t> m_ToolIds;
        mutable Atomic<uint32_t> m_NumPoolSlotQueries{ 0 };
        mutable Mutex m_Mutex;
        Array<uint32_t> m_InventoryClients;
        Array<Received> m_Jobs;
        Array<Received> m_Manifests;
        Array<Received> m_Files;
        Array<StoredPch> m_StoredPchs;
        Array<uint32_t> m_GoneClients;
        Array<bool> m_QueuedWhenGone;   // What QueueResult returned in each OnClientGone
    };

    // A serialized job as far as the gateway reads it: the id first, the PCH id last, and the name
    // (Job::Serialize) between them as a marker
    void MakeJob( uint32_t jobId, const char * marker, uint64_t pchId, MemoryStream & out )
    {
        out.Write( jobId );
        out.Write( AStackString<>( marker ) );
        out.Write( pchId );
    }

    // The chunks of a PCH, as a client sends them after its handshake
    bool SendPchFromProbe( DistributedTest::HandshakeProbe & probe, const ConnectionInfo * ci, const DistributedTest::CompressedPch & pch )
    {
        const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( pch.m_CompressedSize, Protocol::kPchChunkSize );
        for ( uint32_t i = 0; i < numChunks; ++i )
        {
            const Protocol::MsgPchFile msg( pch.m_Id, (uint32_t)pch.m_Content.GetSize(), pch.m_CompressedSize, Protocol::kPchChunkSize, i );
            if ( probe.SendMessage( ci,
                                    msg,
                                    static_cast<const char *>( pch.m_Compressed.GetResult() ) + ( (size_t)i * Protocol::kPchChunkSize ),
                                    Protocol::MsgPchFile::GetChunkLength( pch.m_CompressedSize, Protocol::kPchChunkSize, i ) ) == false )
            {
                return false;
            }
        }
        return true;
    }

    size_t CountFilesUnder( const AString & directory )
    {
        Array<AString> files;
        FileIO::GetFiles( directory, AStackString<>( "*" ), true, &files );
        return files.GetSize();
    }

    bool AnyPathContains( const AString & directory, const char * text )
    {
        Array<AString> files;
        FileIO::GetFiles( directory, AStackString<>( "*" ), true, &files );
        for ( const AString & file : files )
        {
            if ( file.Find( text ) )
            {
                return true;
            }
        }
        return false;
    }
}

// TestGateway
//------------------------------------------------------------------------------
class TestGateway : public FBuildTest
{
private:
    DECLARE_TESTS

    virtual void PreTest() const override;
    virtual void PostTest() const override;

    void SlowClientDoesNotHoldBackAnother() const;
    void SendQueueSendsQueuedMessagesBeforePchChunks() const;
    void SendQueueReadsFileSlicesAtSendTime() const;
    void SendQueueStreamCompressesJobs() const;
    void SendQueueStopsOnAFileItCannotRead() const;
    void PeekJobReadsTheIdsOfASerializedJob() const;
    void SetJobIdChangesOnlyTheFirstFourBytes() const;
    void ResultHeaderIsReadAndPatched() const;
    void ResultCutShortIsRejected() const;
    void LanesUseRealThreadIndexes() const;
    void LostResultIsASystemError() const;
    void GatewayAdvertisesTheForwarderPool() const;
    void GatewayWithoutCapacityClosesTheHandshake() const;
    void GatewayRefusesAClientOfAnotherMajor() const;
    void GatewayHandsDecodedJobsToTheForwarder() const;
    void GatewayHandsToolchainMessagesToTheForwarder() const;
    void GatewayReportsAStoredPch() const;
    void GatewayResultReachesOnlyALiveClient() const;
    void PureGatewayNeverTouchesTheJobQueue() const;
    void AskingASlowClientForAPchDoesNotHoldBackAnother() const;

    // Waits for something that happens on another thread
    static constexpr float kWaitTimeoutSeconds = 30.0f;
    template <class CONDITION>
    static bool WaitUntil( const CONDITION & condition, float timeoutSeconds = kWaitTimeoutSeconds )
    {
        const Timer timer;
        while ( condition() == false )
        {
            if ( timer.GetElapsed() > timeoutSeconds )
            {
                return false;
            }
            Thread::Sleep( 1 );
        }
        return true;
    }

    // Connects the probe and completes its handshake. The id of its client on the server, or 0.
    static uint32_t ConnectProbe( Server & server, DistributedTest::HandshakeProbe & probe, const ConnectionInfo ** outConnection = nullptr );
    // The cache root of a pure gateway that shares the process with a real Server
    static void GetGatewayCacheDir( AString & outDir );
    static void AddActiveJobs( Server & server, uint32_t clientId, uint32_t numJobs );
    static uint32_t GetNumJobsActive( Server & server, uint32_t clientId );
    static size_t CountMessages( const DistributedTest::HandshakeProbe & probe, uint8_t type );

    mutable SyntheticToolchain::ScratchWorkerCache m_WorkerCache;
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestGateway )
    REGISTER_TEST( SlowClientDoesNotHoldBackAnother )
    REGISTER_TEST( SendQueueSendsQueuedMessagesBeforePchChunks )
    REGISTER_TEST( SendQueueReadsFileSlicesAtSendTime )
    REGISTER_TEST( SendQueueStreamCompressesJobs )
    REGISTER_TEST( SendQueueStopsOnAFileItCannotRead )
    REGISTER_TEST( PeekJobReadsTheIdsOfASerializedJob )
    REGISTER_TEST( SetJobIdChangesOnlyTheFirstFourBytes )
    REGISTER_TEST( ResultHeaderIsReadAndPatched )
    REGISTER_TEST( ResultCutShortIsRejected )
    REGISTER_TEST( LanesUseRealThreadIndexes )
    REGISTER_TEST( LostResultIsASystemError )
    REGISTER_TEST( GatewayAdvertisesTheForwarderPool )
    REGISTER_TEST( GatewayWithoutCapacityClosesTheHandshake )
    REGISTER_TEST( GatewayRefusesAClientOfAnotherMajor )
    REGISTER_TEST( GatewayHandsDecodedJobsToTheForwarder )
    REGISTER_TEST( GatewayHandsToolchainMessagesToTheForwarder )
    REGISTER_TEST( GatewayReportsAStoredPch )
    REGISTER_TEST( GatewayResultReachesOnlyALiveClient )
    REGISTER_TEST( PureGatewayNeverTouchesTheJobQueue )
    REGISTER_TEST( AskingASlowClientForAPchDoesNotHoldBackAnother )
REGISTER_TESTS_END

// PreTest
//------------------------------------------------------------------------------
/*virtual*/ void TestGateway::PreTest() const
{
    FBuildTest::PreTest();

    AStackString<> codeDir;
    GetCodeDir( codeDir );
    m_WorkerCache.Enter( codeDir, "Gateway" );
}

// PostTest
//------------------------------------------------------------------------------
/*virtual*/ void TestGateway::PostTest() const
{
    m_WorkerCache.Leave();

    FBuildTest::PostTest();
}

// ConnectProbe
//------------------------------------------------------------------------------
/*static*/ uint32_t TestGateway::ConnectProbe( Server & server, DistributedTest::HandshakeProbe & probe, const ConnectionInfo ** outConnection )
{
    const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
    if ( ci == nullptr )
    {
        return 0;
    }
    if ( outConnection )
    {
        *outConnection = ci;
    }
    const Protocol::MsgConnection msg;
    if ( ( probe.Send( ci, &msg, sizeof( msg ) ) == false ) ||
         ( WaitUntil( [ &probe ]() { return ( probe.GetNumMessages() >= 3 ); } ) == false ) )
    {
        return 0;
    }

    // The newest client has the highest id
    uint32_t id = 0;
    MutexHolder mh( server.m_ClientListMutex );
    for ( const Server::ClientState * cs : server.m_ClientList )
    {
        id = ( cs->m_Id > id ) ? cs->m_Id : id;
    }
    return id;
}

// GetGatewayCacheDir
//------------------------------------------------------------------------------
/*static*/ void TestGateway::GetGatewayCacheDir( AString & outDir )
{
    VERIFY( FBuild::GetTempDir( outDir ) );
    outDir += "gateway_tmp";
    outDir += NATIVE_SLASH;
}

// AddActiveJobs
//------------------------------------------------------------------------------
/*static*/ void TestGateway::AddActiveJobs( Server & server, uint32_t clientId, uint32_t numJobs )
{
    MutexHolder mh( server.m_ClientListMutex );
    for ( Server::ClientState * cs : server.m_ClientList )
    {
        if ( cs->m_Id == clientId )
        {
            for ( uint32_t i = 0; i < numJobs; ++i )
            {
                cs->m_NumJobsActive.Increment();
            }
        }
    }
}

// GetNumJobsActive
//------------------------------------------------------------------------------
/*static*/ uint32_t TestGateway::GetNumJobsActive( Server & server, uint32_t clientId )
{
    MutexHolder mh( server.m_ClientListMutex );
    for ( const Server::ClientState * cs : server.m_ClientList )
    {
        if ( cs->m_Id == clientId )
        {
            return cs->m_NumJobsActive.Load();
        }
    }
    return 0;
}

// CountMessages
//------------------------------------------------------------------------------
/*static*/ size_t TestGateway::CountMessages( const DistributedTest::HandshakeProbe & probe, uint8_t type )
{
    size_t count = 0;
    const size_t numMessages = probe.GetNumMessages();
    for ( size_t i = 0; i < numMessages; ++i )
    {
        count += ( probe.GetMessageType( i ) == type ) ? 1 : 0;
    }
    return count;
}

// SlowClientDoesNotHoldBackAnother
//------------------------------------------------------------------------------
void TestGateway::SlowClientDoesNotHoldBackAnother() const
{
    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    DistributedTest::HandshakeProbe a;
    DistributedTest::HandshakeProbe b;
    const uint32_t idA = ConnectProbe( s, a );
    TEST_ASSERT( idA != 0 );
    const uint32_t idB = ConnectProbe( s, b );
    TEST_ASSERT( ( idB != 0 ) && ( idB != idA ) );
    a.StallReading( true );

    // More than the socket buffers hold, so the sends to A back up
    const uint32_t kNumLargeResults = 4;
    Array<char> large;
    large.SetSize( 64 * MEGABYTE );
    memset( large.Begin(), 0x5A, large.GetSize() );
    AddActiveJobs( s, idA, kNumLargeResults );
    AddActiveJobs( s, idB, 1 );
    for ( uint32_t i = 0; i < kNumLargeResults; ++i )
    {
        MemoryStream payload( large.GetSize() );
        payload.WriteBuffer( large.Begin(), large.GetSize() );
        TEST_ASSERT( s.QueueResult( idA, false, Move( payload ) ) );
    }
    MemoryStream small;
    small.Write( (uint32_t)1 );
    TEST_ASSERT( s.QueueResult( idB, false, Move( small ) ) );

    // B's result arrives while A's outbox still holds results it cannot send
    TEST_ASSERT( WaitUntil( [ &b ]() { return ( CountMessages( b, Protocol::MSG_JOB_RESULT ) == 1 ); }, 2.0f ) );
    TEST_ASSERT( CountMessages( a, Protocol::MSG_JOB_RESULT ) == 0 );
    TEST_ASSERT( GetNumJobsActive( s, idA ) > 0 );

    a.StallReading( false );
    TEST_ASSERT( WaitUntil( [ &a ]() { return ( CountMessages( a, Protocol::MSG_JOB_RESULT ) == kNumLargeResults ); } ) );

    a.ShutdownAllConnections();
    b.ShutdownAllConnections();
}

// SendQueueSendsQueuedMessagesBeforePchChunks
//------------------------------------------------------------------------------
void TestGateway::SendQueueSendsQueuedMessagesBeforePchChunks() const
{
    MessageRecorder recorder;
    TEST_ASSERT( recorder.Listen( kGatewayTestDownstreamPort ) );
    DeliveryRecorder listener;
    WorkerSendQueue queue( listener, AStackString( "127.0.0.1" ) );

    // Queued before the send thread starts, so that it takes them together
    const char chunk[] = "chunk";
    queue.Push( SendQueueItem( Protocol::MsgPchFile( 0x77, 100, sizeof( chunk ), sizeof( chunk ), 0 ),
                               ConstMemoryStream( chunk, sizeof( chunk ) ),
                               SendKind::Pch,
                               100,
                               false,
                               0x77 ) );
    queue.Push( SendQueueItem( Protocol::MsgConnection() ) );
    const char job[] = "job";
    queue.Push( SendQueueItem( Protocol::MsgJob( 1, 0 ), ConstMemoryStream( job, sizeof( job ) ), SendKind::Job, sizeof( job ), false ) );

    QueueRunner runner( queue );
    TEST_ASSERT( runner.Start() );
    TEST_ASSERT( WaitUntil( [ &recorder ]() { return ( recorder.GetNumMessages() == 3 ); } ) );
    runner.Stop();

    TEST_ASSERT( recorder.GetType( 0 ) == Protocol::MSG_CONNECTION );
    TEST_ASSERT( recorder.GetType( 1 ) == Protocol::MSG_JOB );
    TEST_ASSERT( recorder.GetType( 2 ) == Protocol::MSG_PCH_FILE );
}

// SendQueueReadsFileSlicesAtSendTime
//------------------------------------------------------------------------------
void TestGateway::SendQueueReadsFileSlicesAtSendTime() const
{
    MessageRecorder recorder;
    TEST_ASSERT( recorder.Listen( kGatewayTestDownstreamPort ) );
    DeliveryRecorder listener;
    WorkerSendQueue queue( listener, AStackString( "127.0.0.1" ) );
    listener.m_Queue = &queue;

    AStackString<> path;
    TEST_ASSERT( FBuild::GetTempDir( path ) );
    path += "SendQueueSlice.bin";
    queue.Push( SendQueueItem( Protocol::MsgPchFile( 0x77, 100, 5, 5, 0 ), path, 3, 5, SendKind::Pch, 100, 0x77, true ) );

    // Written after the push
    {
        char bytes[ 16 ];
        for ( size_t i = 0; i < sizeof( bytes ); ++i )
        {
            bytes[ i ] = (char)i;
        }
        FileStream f;
        TEST_ASSERT( f.Open( path.Get(), FileStream::WRITE_ONLY ) );
        TEST_ASSERT( f.Write( bytes, sizeof( bytes ) ) == sizeof( bytes ) );
    }

    QueueRunner runner( queue );
    TEST_ASSERT( runner.Start() );
    TEST_ASSERT( WaitUntil( [ &recorder ]() { return ( recorder.GetNumMessages() == 1 ); } ) );
    runner.Stop();

    Array<char> payload;
    recorder.GetPayload( 0, payload );
    TEST_ASSERT( payload.GetSize() == 5 );
    for ( size_t i = 0; i < payload.GetSize(); ++i )
    {
        TEST_ASSERT( payload[ i ] == (char)( 3 + i ) );
    }

    // Once, before the chunk was sent
    TEST_ASSERT( listener.m_PchIds.GetSize() == 1 );
    TEST_ASSERT( listener.m_PchIds[ 0 ] == 0x77 );
    TEST_ASSERT( listener.m_PchsSentBefore[ 0 ] == 0 );
    TEST_ASSERT( queue.GetStats( SendKind::Pch ).m_Messages == 1 );
    TEST_ASSERT( queue.GetNumTransformedPchs() == 1 );
}

// SendQueueStreamCompressesJobs
//------------------------------------------------------------------------------
void TestGateway::SendQueueStreamCompressesJobs() const
{
    MessageRecorder recorder;
    TEST_ASSERT( recorder.Listen( kGatewayTestDownstreamPort ) );
    DeliveryRecorder listener;
    WorkerSendQueue queue( listener, AStackString( "127.0.0.1" ) );

    Array<char> jobs[ 2 ];
    DistributedTest::MakeTestContent( 200 * 1024, 1, jobs[ 0 ] );
    DistributedTest::MakeTestContent( 200 * 1024, 2, jobs[ 1 ] );
    for ( const Array<char> & job : jobs )
    {
        queue.Push( SendQueueItem( Protocol::MsgJob( 1, 0 ), ConstMemoryStream( job.Begin(), job.GetSize() ), SendKind::Job, job.GetSize(), true ) );
    }
    TEST_ASSERT( queue.GetNumUnsentJobs() == 2 );

    QueueRunner runner( queue );
    TEST_ASSERT( runner.Start() );
    TEST_ASSERT( WaitUntil( [ &recorder ]() { return ( recorder.GetNumMessages() == 2 ); } ) );
    runner.Stop();
    TEST_ASSERT( queue.GetNumUnsentJobs() == 0 );

    // Each is compressed against the one before it
    JobStreamDecoder decoder;
    for ( size_t i = 0; i < 2; ++i )
    {
        TEST_ASSERT( recorder.GetType( i ) == Protocol::MSG_JOB );
        Array<char> payload;
        recorder.GetPayload( i, payload );
        TEST_ASSERT( payload.GetSize() < jobs[ i ].GetSize() );
        void * decoded = nullptr;
        size_t decodedSize = 0;
        TEST_ASSERT( decoder.Decode( payload.Begin(), payload.GetSize(), decoded, decodedSize ) );
        const bool same = ( decodedSize == jobs[ i ].GetSize() ) && ( memcmp( decoded, jobs[ i ].Begin(), decodedSize ) == 0 );
        FREE( decoded );
        TEST_ASSERT( same );
    }

    const DistributionSendStats & stats = queue.GetStats( SendKind::Job );
    TEST_ASSERT( stats.m_Messages == 2 );
    TEST_ASSERT( stats.m_RawBytes == ( jobs[ 0 ].GetSize() + jobs[ 1 ].GetSize() ) );
}

// SendQueueStopsOnAFileItCannotRead
//------------------------------------------------------------------------------
void TestGateway::SendQueueStopsOnAFileItCannotRead() const
{
    MessageRecorder recorder;
    TEST_ASSERT( recorder.Listen( kGatewayTestDownstreamPort ) );
    DeliveryRecorder listener;
    WorkerSendQueue queue( listener, AStackString( "127.0.0.1" ) );

    AStackString<> path;
    TEST_ASSERT( FBuild::GetTempDir( path ) );
    path += "SendQueueMissing.bin";
    TEST_ASSERT( FileIO::FileExists( path.Get() ) == false );
    queue.Push( SendQueueItem( Protocol::MsgPchFile( 0x77, 100, 10, 5, 0 ), path, 0, 5, SendKind::Pch, 100, 0, false ) );
    queue.Push( SendQueueItem( Protocol::MsgPchFile( 0x77, 100, 10, 5, 1 ), path, 5, 5, SendKind::Pch, 0, 0x77, false ) );

    // It stops by itself, without sending what follows
    QueueRunner runner( queue );
    TEST_ASSERT( runner.Start() );
    TEST_ASSERT( WaitUntil( [ &runner ]() { return runner.HasReturned(); } ) );
    TEST_ASSERT( queue.IsQuitting() );
    runner.Stop();
    TEST_ASSERT( recorder.GetNumMessages() == 0 );
    TEST_ASSERT( listener.m_PchIds.IsEmpty() );
}

// MakeResult
//------------------------------------------------------------------------------
// The layout Server::FinalizeCompletedJobs writes
namespace
{
    void MakeResult( MemoryStream & ms, uint32_t jobId, uint16_t threadIndex, const char * data, uint32_t dataSize )
    {
        ms.Write( jobId );
        ms.Write( AStackString( "obj/file.obj" ) );
        ms.Write( true );
        ms.Write( false );
        Array<AString> messages;
        messages.EmplaceBack( "first message" );
        messages.EmplaceBack( "second" );
        ms.Write( messages );
        ms.Write( (uint32_t)1234 );
        ms.Write( threadIndex );
        ms.Write( dataSize );
        ms.WriteBuffer( data, dataSize );
    }
}

// PeekJobReadsTheIdsOfASerializedJob
//------------------------------------------------------------------------------
void TestGateway::PeekJobReadsTheIdsOfASerializedJob() const
{
    // The layout Job::Serialize writes: id, name, working dir, node properties, data, pch id
    MemoryStream ms;
    ms.Write( (uint32_t)0xA1B2C3D4 );
    ms.Write( AStackString( "obj/file.obj" ) );
    ms.Write( AStackString( "C:\\work" ) );
    ms.Write( (uint32_t)777 ); // Node properties
    ms.Write( (uint32_t)5 );
    ms.WriteBuffer( "hello", 5 );
    ms.Write( (uint64_t)0x1122334455667788ull );

    uint32_t jobId = 0;
    uint64_t pchId = 0;
    TEST_ASSERT( GatewayWire::PeekJob( ms.GetData(), ms.GetSize(), jobId, pchId ) );
    TEST_ASSERT( jobId == 0xA1B2C3D4 );
    TEST_ASSERT( pchId == 0x1122334455667788ull );

    // Too short to hold both
    TEST_ASSERT( GatewayWire::PeekJob( ms.GetData(), GatewayWire::kMinJobSize - 1, jobId, pchId ) == false );
    TEST_ASSERT( GatewayWire::PeekJob( ms.GetData(), GatewayWire::kMinJobSize, jobId, pchId ) );
}

// SetJobIdChangesOnlyTheFirstFourBytes
//------------------------------------------------------------------------------
void TestGateway::SetJobIdChangesOnlyTheFirstFourBytes() const
{
    char job[ 32 ];
    for ( size_t i = 0; i < sizeof( job ); ++i )
    {
        job[ i ] = (char)( i + 1 );
    }
    char expected[ sizeof( job ) ];
    memcpy( expected, job, sizeof( job ) );
    const uint32_t newId = 0x0A0B0C0D;
    memcpy( expected, &newId, sizeof( newId ) );

    GatewayWire::SetJobId( job, sizeof( job ), newId );
    TEST_ASSERT( memcmp( job, expected, sizeof( job ) ) == 0 );

    uint32_t jobId = 0;
    uint64_t pchId = 0;
    TEST_ASSERT( GatewayWire::PeekJob( job, sizeof( job ), jobId, pchId ) );
    TEST_ASSERT( jobId == newId );
}

// ResultHeaderIsReadAndPatched
//------------------------------------------------------------------------------
void TestGateway::ResultHeaderIsReadAndPatched() const
{
    const char data[] = "result data bytes";
    MemoryStream ms;
    MakeResult( ms, 42, 1003, data, sizeof( data ) );

    GatewayWire::ResultHeader header;
    TEST_ASSERT( GatewayWire::ReadResultHeader( ms.GetData(), ms.GetSize(), header ) );
    TEST_ASSERT( header.m_JobId == 42 );
    TEST_ASSERT( header.m_Name == "obj/file.obj" );
    TEST_ASSERT( header.m_Ok );
    TEST_ASSERT( header.m_SystemError == false );
    TEST_ASSERT( header.m_ThreadIndex == 1003 );

    // Only the job id and the thread index change
    Array<char> patched;
    patched.SetSize( ms.GetSize() );
    memcpy( patched.Begin(), ms.GetData(), ms.GetSize() );
    GatewayWire::PatchResult( patched.Begin(), patched.GetSize(), header, 7, 1260 );

    GatewayWire::ResultHeader after;
    TEST_ASSERT( GatewayWire::ReadResultHeader( patched.Begin(), patched.GetSize(), after ) );
    TEST_ASSERT( after.m_JobId == 7 );
    TEST_ASSERT( after.m_ThreadIndex == 1260 );
    TEST_ASSERT( after.m_Name == header.m_Name );
    TEST_ASSERT( after.m_Ok == header.m_Ok );
    TEST_ASSERT( after.m_SystemError == header.m_SystemError );
    TEST_ASSERT( after.m_ThreadIndexOffset == header.m_ThreadIndexOffset );

    Array<char> expected;
    expected.SetSize( ms.GetSize() );
    memcpy( expected.Begin(), ms.GetData(), ms.GetSize() );
    const uint32_t newJobId = 7;
    const uint16_t newThread = 1260;
    memcpy( expected.Begin(), &newJobId, sizeof( newJobId ) );
    memcpy( expected.Begin() + header.m_ThreadIndexOffset, &newThread, sizeof( newThread ) );
    TEST_ASSERT( memcmp( patched.Begin(), expected.Begin(), expected.GetSize() ) == 0 );
    TEST_ASSERT( memcmp( patched.Begin() + patched.GetSize() - sizeof( data ), data, sizeof( data ) ) == 0 );
}

// ResultCutShortIsRejected
//------------------------------------------------------------------------------
void TestGateway::ResultCutShortIsRejected() const
{
    const char data[] = "result data bytes";
    MemoryStream ms;
    MakeResult( ms, 42, 1003, data, sizeof( data ) );

    // Every cut that loses part of the header or of the data is refused
    GatewayWire::ResultHeader header;
    for ( size_t size = 0; size < ms.GetSize(); ++size )
    {
        TEST_ASSERT( GatewayWire::ReadResultHeader( ms.GetData(), size, header ) == false );
    }
    TEST_ASSERT( GatewayWire::ReadResultHeader( ms.GetData(), ms.GetSize(), header ) );

    // A name that claims more than the message holds
    MemoryStream hostile;
    hostile.Write( (uint32_t)1 );
    hostile.Write( (uint32_t)0xFFFFFFF0 );
    hostile.WriteBuffer( "abc", 3 );
    TEST_ASSERT( GatewayWire::ReadResultHeader( hostile.GetData(), hostile.GetSize(), header ) == false );
}

// LanesUseRealThreadIndexes
//------------------------------------------------------------------------------
void TestGateway::LanesUseRealThreadIndexes() const
{
    TEST_ASSERT( GatewayWire::MakeLane( 0, 1001 ) == 1001 ); // The gateway's own pool keeps its indexes
    TEST_ASSERT( GatewayWire::MakeLane( 1, 1001 ) == 1257 );
    TEST_ASSERT( GatewayWire::MakeLane( 1, 1000 + 999 ) == 1511 ); // Clamped
    TEST_ASSERT( GatewayWire::MakeLane( 2, 1001 ) == 1513 );

    uint32_t previousHighest = 0;
    for ( uint32_t worker = 0; worker <= GatewayWire::kMaxWorkerIndex; ++worker )
    {
        uint32_t lowest = 0xFFFFFFFF;
        uint32_t highest = 0;
        for ( uint32_t thread = 0; thread <= 0xFFFF; thread += 7 )
        {
            const uint32_t lane = GatewayWire::MakeLane( worker, (uint16_t)thread );
            TEST_ASSERT( lane >= 1001 );
            lowest = ( lane < lowest ) ? lane : lowest;
            highest = ( lane > highest ) ? lane : highest;
        }
        TEST_ASSERT( ( worker == 0 ) || ( lowest > previousHighest ) );
        previousHighest = highest;
    }
    TEST_ASSERT( previousHighest <= 0xFFFF );
}

// LostResultIsASystemError
//------------------------------------------------------------------------------
void TestGateway::LostResultIsASystemError() const
{
    MemoryStream ms;
    GatewayWire::WriteLostResult( ms, 99, "lost on worker" );

    GatewayWire::ResultHeader header;
    TEST_ASSERT( GatewayWire::ReadResultHeader( ms.GetData(), ms.GetSize(), header ) );
    TEST_ASSERT( header.m_JobId == 99 );
    TEST_ASSERT( header.m_Ok == false );
    TEST_ASSERT( header.m_SystemError );

    // The client reads it with its own layout, and finds the message
    ConstMemoryStream reader( ms.GetData(), ms.GetSize() );
    uint32_t jobId;
    AStackString name;
    bool ok;
    bool systemError;
    Array<AString> messages;
    TEST_ASSERT( reader.Read( jobId ) && reader.Read( name ) && reader.Read( ok ) && reader.Read( systemError ) && reader.Read( messages ) );
    TEST_ASSERT( ( messages.GetSize() == 1 ) && ( messages[ 0 ] == "lost on worker" ) );
}

// GatewayAdvertisesTheForwarderPool
//------------------------------------------------------------------------------
void TestGateway::GatewayAdvertisesTheForwarderPool() const
{
    Array<uint64_t> toolIds;
    toolIds.Append( 0x11 );
    toolIds.Append( 0x22 );
    StubForwarder forwarder( 340, toolIds );
    AStackString<> cacheDir;
    GetGatewayCacheDir( cacheDir );
    Server s( forwarder, cacheDir );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    DistributedTest::HandshakeProbe a;
    const uint32_t idA = ConnectProbe( s, a );
    TEST_ASSERT( idA != 0 );
    TEST_ASSERT( a.GetNumMessages() == 3 );
    TEST_ASSERT( a.GetMessageType( 0 ) == Protocol::MSG_CONNECTION_ACK );
    TEST_ASSERT( a.GetMessageType( 1 ) == Protocol::MSG_PCH_INVENTORY );
    TEST_ASSERT( a.GetMessageType( 2 ) == Protocol::MSG_TOOLCHAIN_INVENTORY );
    TEST_ASSERT( a.GetAckCapacity() == 340 );
    Array<uint64_t> advertised;
    a.GetToolchainIds( advertised );
    TEST_ASSERT( ( advertised.GetSize() == 2 ) && ( advertised[ 0 ] == 0x11 ) && ( advertised[ 1 ] == 0x22 ) );

    // Every client is offered the whole pool
    DistributedTest::HandshakeProbe b;
    const uint32_t idB = ConnectProbe( s, b );
    TEST_ASSERT( ( idB != 0 ) && ( idB != idA ) );
    TEST_ASSERT( b.GetAckCapacity() == 340 );

    Array<uint32_t> inventoryClients;
    forwarder.GetInventoryClients( inventoryClients );
    TEST_ASSERT( ( inventoryClients.GetSize() == 2 ) && ( inventoryClients[ 0 ] == idA ) && ( inventoryClients[ 1 ] == idB ) );
    TEST_ASSERT( JobQueueRemote::IsValid() == false );

    a.ShutdownAllConnections();
    b.ShutdownAllConnections();
}

// GatewayWithoutCapacityClosesTheHandshake
//------------------------------------------------------------------------------
void TestGateway::GatewayWithoutCapacityClosesTheHandshake() const
{
    StubForwarder forwarder( 0 );
    AStackString<> cacheDir;
    GetGatewayCacheDir( cacheDir );
    Server s( forwarder, cacheDir );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    DistributedTest::HandshakeProbe probe;
    const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
    TEST_ASSERT( ci );
    const Protocol::MsgConnection msg;
    TEST_ASSERT( probe.Send( ci, &msg, sizeof( msg ) ) );

    // Closed without an ack, so the client builds elsewhere
    TEST_ASSERT( WaitUntil( [ &probe ]() { return probe.IsDisconnected(); } ) );
    TEST_ASSERT( probe.GetNumMessages() == 0 );
    TEST_ASSERT( forwarder.GetNumPoolSlotQueries() == 1 );
    Array<uint32_t> inventoryClients;
    forwarder.GetInventoryClients( inventoryClients );
    TEST_ASSERT( inventoryClients.IsEmpty() );

    uint32_t numRefused = 0;
    TEST_ASSERT( DistributedTest::CountInStatsLogSince( s.GetLogPath(), 0, "refused addr=", numRefused ) );
    TEST_ASSERT( numRefused == 1 );
    uint32_t numNoCapacity = 0;
    TEST_ASSERT( DistributedTest::CountInStatsLogSince( s.GetLogPath(), 0, "reason=no-capacity", numNoCapacity ) );
    TEST_ASSERT( numNoCapacity == 1 );
}

// GatewayRefusesAClientOfAnotherMajor
//------------------------------------------------------------------------------
void TestGateway::GatewayRefusesAClientOfAnotherMajor() const
{
    StubForwarder forwarder( 4 );
    AStackString<> cacheDir;
    GetGatewayCacheDir( cacheDir );
    Server s( forwarder, cacheDir );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    DistributedTest::HandshakeProbe probe;
    const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
    TEST_ASSERT( ci );

    // The major follows the message header
    const Protocol::MsgConnection msg;
    char handshake[ sizeof( msg ) ];
    memcpy( handshake, &msg, sizeof( msg ) );
    const uint32_t otherMajor = ( Protocol::kVersionMajor + 1 );
    memcpy( handshake + sizeof( Protocol::IMessage ), &otherMajor, sizeof( otherMajor ) );
    TEST_ASSERT( probe.Send( ci, handshake, sizeof( handshake ) ) );

    TEST_ASSERT( WaitUntil( [ &probe ]() { return probe.IsDisconnected(); } ) );
    TEST_ASSERT( probe.GetNumMessages() == 1 );
    TEST_ASSERT( probe.GetMessageType( 0 ) == Protocol::MSG_REFUSED );
    TEST_ASSERT( probe.GetRefusedMajor() == Protocol::kVersionMajor );
    TEST_ASSERT( forwarder.GetNumPoolSlotQueries() == 0 );
}

// GatewayHandsDecodedJobsToTheForwarder
//------------------------------------------------------------------------------
void TestGateway::GatewayHandsDecodedJobsToTheForwarder() const
{
    StubForwarder forwarder( 4 );
    AStackString<> cacheDir;
    GetGatewayCacheDir( cacheDir );
    Server s( forwarder, cacheDir );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    DistributedTest::HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    const uint32_t id = ConnectProbe( s, probe, &ci );
    TEST_ASSERT( id != 0 );

    // The second is compressed against the first, so they are decoded in order
    MemoryStream first;
    MakeJob( 7, "first job", 0x99, first );
    MemoryStream second;
    MakeJob( 8, "second job", 0, second );
    JobStreamEncoder encoder;
    TEST_ASSERT( probe.SendJob( ci, encoder, 0xAB, 3, first ) );
    TEST_ASSERT( probe.SendJob( ci, encoder, 0xAB, -1, second ) );
    TEST_ASSERT( WaitUntil( [ &forwarder ]() { return ( forwarder.GetNumJobs() == 2 ); } ) );

    Array<StubForwarder::Received> jobs;
    forwarder.GetJobs( jobs );
    const MemoryStream * sent[] = { &first, &second };
    const int16_t levels[] = { 3, -1 };
    for ( size_t i = 0; i < 2; ++i )
    {
        TEST_ASSERT( jobs[ i ].m_ClientId == id );
        TEST_ASSERT( jobs[ i ].m_ToolId == 0xAB );
        TEST_ASSERT( jobs[ i ].m_Level == levels[ i ] );
        TEST_ASSERT( jobs[ i ].m_Data.GetSize() == sent[ i ]->GetSize() );
        TEST_ASSERT( memcmp( jobs[ i ].m_Data.Begin(), sent[ i ]->GetData(), sent[ i ]->GetSize() ) == 0 );
    }
    uint32_t jobId = 0;
    uint64_t pchId = 0;
    TEST_ASSERT( GatewayWire::PeekJob( jobs[ 0 ].m_Data.Begin(), jobs[ 0 ].m_Data.GetSize(), jobId, pchId ) );
    TEST_ASSERT( ( jobId == 7 ) && ( pchId == 0x99 ) );

    // Each counts until its result is sent
    TEST_ASSERT( GetNumJobsActive( s, id ) == 2 );
    TEST_ASSERT( JobQueueRemote::IsValid() == false );

    probe.ShutdownAllConnections();
}

// GatewayHandsToolchainMessagesToTheForwarder
//------------------------------------------------------------------------------
void TestGateway::GatewayHandsToolchainMessagesToTheForwarder() const
{
    StubForwarder forwarder( 4 );
    AStackString<> cacheDir;
    GetGatewayCacheDir( cacheDir );
    Server s( forwarder, cacheDir );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    DistributedTest::HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    const uint32_t id = ConnectProbe( s, probe, &ci );
    TEST_ASSERT( id != 0 );

    // A toolchain a worker would unpack and keep
    const uint64_t toolId = 0x11;
    const char * const content = "the compiler";
    AStackString<> root;
    SyntheticToolchain::GetRootPath( root );
    AStackString<> fileName( root );
    fileName += "tool.exe";
    MemoryStream manifest;
    SyntheticToolchain::WriteManifest( manifest, toolId, root, fileName, content );
    TEST_ASSERT( probe.SendMessage( ci, Protocol::MsgManifest( toolId ), manifest.GetData(), manifest.GetSize() ) );
    TEST_ASSERT( probe.SendMessage( ci, Protocol::MsgFile( toolId, 0 ), content, strlen( content ) ) );
    TEST_ASSERT( WaitUntil( [ &forwarder ]() { return ( forwarder.GetNumToolchainMessages() == 2 ); } ) );

    Array<StubForwarder::Received> manifests;
    forwarder.GetManifests( manifests );
    TEST_ASSERT( manifests.GetSize() == 1 );
    TEST_ASSERT( ( manifests[ 0 ].m_ClientId == id ) && ( manifests[ 0 ].m_ToolId == toolId ) );
    TEST_ASSERT( manifests[ 0 ].m_Data.GetSize() == manifest.GetSize() );
    TEST_ASSERT( memcmp( manifests[ 0 ].m_Data.Begin(), manifest.GetData(), manifest.GetSize() ) == 0 );

    Array<StubForwarder::Received> files;
    forwarder.GetFiles( files );
    TEST_ASSERT( files.GetSize() == 1 );
    TEST_ASSERT( ( files[ 0 ].m_ClientId == id ) && ( files[ 0 ].m_ToolId == toolId ) && ( files[ 0 ].m_FileId == 0 ) );
    TEST_ASSERT( files[ 0 ].m_Data.GetSize() == strlen( content ) );
    TEST_ASSERT( memcmp( files[ 0 ].m_Data.Begin(), content, strlen( content ) ) == 0 );

    // Nothing is unpacked
    AStackString<> workerCache;
    TEST_ASSERT( FBuild::GetWorkerCacheDir( workerCache ) );
    AStackString<> toolchainDir;
    toolchainDir.Format( "%stoolchain.%016" PRIx64, workerCache.Get(), toolId );
    TEST_ASSERT( FileIO::DirectoryExists( toolchainDir ) == false );
    TEST_ASSERT( AnyPathContains( workerCache, "toolchain." ) == false );
    TEST_ASSERT( AnyPathContains( cacheDir, "toolchain." ) == false );

    probe.ShutdownAllConnections();
}

// GatewayReportsAStoredPch
//------------------------------------------------------------------------------
void TestGateway::GatewayReportsAStoredPch() const
{
    StubForwarder forwarder( 4 );
    AStackString<> cacheDir;
    GetGatewayCacheDir( cacheDir );
    Server s( forwarder, cacheDir );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    DistributedTest::HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    TEST_ASSERT( ConnectProbe( s, probe, &ci ) != 0 );

    DistributedTest::CompressedPch pch;
    DistributedTest::MakeCompressedPch( 2 * MEGABYTE, 1, pch );
    TEST_ASSERT( SendPchFromProbe( probe, ci, pch ) );
    TEST_ASSERT( WaitUntil( [ &forwarder ]() { return ( forwarder.GetNumStoredPchs() == 1 ); } ) );

    Array<StubForwarder::StoredPch> stored;
    forwarder.GetStoredPchs( stored );
    TEST_ASSERT( stored[ 0 ].m_PchId == pch.m_Id );
    TEST_ASSERT( stored[ 0 ].m_UncompressedSize == pch.m_Content.GetSize() );
    TEST_ASSERT( stored[ 0 ].m_CompressedSize == pch.m_CompressedSize );

    // Kept under the gateway's root, and nothing under the worker cache
    AStackString<> path;
    TEST_ASSERT( s.FindCachedPch( pch.m_Id, path ) );
    TEST_ASSERT( path.BeginsWith( cacheDir ) );
    TEST_ASSERT( FileIO::FileExists( path.Get() ) );
    TEST_ASSERT( s.GetLogPath().BeginsWith( cacheDir ) );
    AStackString<> workerCache;
    TEST_ASSERT( FBuild::GetWorkerCacheDir( workerCache ) );
    TEST_ASSERT( CountFilesUnder( workerCache ) == 0 );

    probe.ShutdownAllConnections();
}

// GatewayResultReachesOnlyALiveClient
//------------------------------------------------------------------------------
void TestGateway::GatewayResultReachesOnlyALiveClient() const
{
    StubForwarder forwarder( 4 );
    AStackString<> cacheDir;
    GetGatewayCacheDir( cacheDir );
    Server s( forwarder, cacheDir );
    forwarder.m_Server = &s;
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    DistributedTest::HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    const uint32_t id = ConnectProbe( s, probe, &ci );
    TEST_ASSERT( id != 0 );

    MemoryStream job;
    MakeJob( 7, "job", 0, job );
    JobStreamEncoder encoder;
    TEST_ASSERT( probe.SendJob( ci, encoder, 0xAB, 0, job ) );
    TEST_ASSERT( WaitUntil( [ &forwarder ]() { return ( forwarder.GetNumJobs() == 1 ); } ) );

    MemoryStream result;
    MakeResult( result, 7, 1001, "obj", 3 );
    TEST_ASSERT( s.QueueResult( id, false, Move( result ) ) );
    Array<GatewayWire::ResultHeader> results;
    TEST_ASSERT( WaitUntil( [ &probe, &results ]() { probe.GetResults( results ); return ( results.GetSize() == 1 ); } ) );
    TEST_ASSERT( results[ 0 ].m_JobId == 7 );
    TEST_ASSERT( WaitUntil( [ &s, id ]() { return ( GetNumJobsActive( s, id ) == 0 ); } ) );

    // Once the forwarder hears the client has gone, nothing more is queued for it
    s.DisconnectClient( id );
    TEST_ASSERT( WaitUntil( [ &forwarder ]() { return ( forwarder.GetNumGoneClients() == 1 ); } ) );
    Array<uint32_t> gone;
    forwarder.GetGoneClients( gone );
    TEST_ASSERT( gone[ 0 ] == id );
    Array<bool> queuedWhenGone;
    forwarder.GetQueuedWhenGone( queuedWhenGone );
    TEST_ASSERT( queuedWhenGone[ 0 ] == false ); // Tried within OnClientGone itself
    MemoryStream late;
    MakeResult( late, 7, 1001, "obj", 3 );
    TEST_ASSERT( s.QueueResult( id, false, Move( late ) ) == false );
    TEST_ASSERT( WaitUntil( [ &probe ]() { return probe.IsDisconnected(); } ) );
}

// PureGatewayNeverTouchesTheJobQueue
//------------------------------------------------------------------------------
void TestGateway::PureGatewayNeverTouchesTheJobQueue() const
{
    TEST_ASSERT( JobQueueRemote::IsValid() == false ); // No other Server in the process

    StubForwarder forwarder( 4 );
    AStackString<> cacheDir;
    GetGatewayCacheDir( cacheDir );
    {
        Server s( forwarder, cacheDir );
        TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

        DistributedTest::HandshakeProbe probe;
        const ConnectionInfo * ci = nullptr;
        TEST_ASSERT( ConnectProbe( s, probe, &ci ) != 0 );

        const char manifest[] = "manifest";
        const char file[] = "file";
        TEST_ASSERT( probe.SendMessage( ci, Protocol::MsgManifest( 0x11 ), manifest, sizeof( manifest ) ) );
        TEST_ASSERT( probe.SendMessage( ci, Protocol::MsgFile( 0x11, 0 ), file, sizeof( file ) ) );
        DistributedTest::CompressedPch pch;
        DistributedTest::MakeCompressedPch( 64 * 1024, 2, pch );
        TEST_ASSERT( SendPchFromProbe( probe, ci, pch ) );
        MemoryStream job;
        MakeJob( 7, "job", pch.m_Id, job );
        JobStreamEncoder encoder;
        TEST_ASSERT( probe.SendJob( ci, encoder, 0x11, 0, job ) );

        TEST_ASSERT( WaitUntil( [ &forwarder ]() { return ( forwarder.GetNumJobs() == 1 ) &&
                                                          ( forwarder.GetNumToolchainMessages() == 2 ) &&
                                                          ( forwarder.GetNumStoredPchs() == 1 ); } ) );
        TEST_ASSERT( JobQueueRemote::IsValid() == false );

        probe.ShutdownAllConnections();
        TEST_ASSERT( WaitUntil( [ &forwarder ]() { return ( forwarder.GetNumGoneClients() == 1 ); } ) );
    }
    TEST_ASSERT( JobQueueRemote::IsValid() == false );
    TEST_ASSERT( forwarder.GetNumGoneClients() == 1 );
}

// AskingASlowClientForAPchDoesNotHoldBackAnother
//------------------------------------------------------------------------------
namespace
{
    // Asks on its own thread, so that a call that waits for a slow client fails the test instead of hanging it
    struct PchAsker
    {
        static uint32_t ThreadFunc( void * param )
        {
            PchAsker * asker = static_cast<PchAsker *>( param );
            asker->m_Server->AskClientForPch( asker->m_SlowClient, asker->m_SlowPchId );
            asker->m_Server->AskClientForPch( asker->m_SlowClient, asker->m_SlowPchId ); // Asked once until it arrives
            asker->m_Server->AskClientForPch( asker->m_OtherClient, asker->m_OtherPchId );
            asker->m_Done.Store( true );
            return 0;
        }

        Server *            m_Server = nullptr;
        uint32_t            m_SlowClient = 0;
        uint64_t            m_SlowPchId = 0;
        uint32_t            m_OtherClient = 0;
        uint64_t            m_OtherPchId = 0;
        Atomic<bool>        m_Done{ false };
    };
}

void TestGateway::AskingASlowClientForAPchDoesNotHoldBackAnother() const
{
    StubForwarder forwarder( 4 );
    AStackString<> cacheDir;
    GetGatewayCacheDir( cacheDir );
    Server s( forwarder, cacheDir );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    DistributedTest::HandshakeProbe a;
    DistributedTest::HandshakeProbe b;
    const uint32_t idA = ConnectProbe( s, a );
    TEST_ASSERT( idA != 0 );
    const uint32_t idB = ConnectProbe( s, b );
    TEST_ASSERT( ( idB != 0 ) && ( idB != idA ) );
    a.StallReading( true );

    // More than the socket buffers hold, so A's outbox is held in a send
    const uint32_t kNumLargeResults = 4;
    Array<char> large;
    large.SetSize( 64 * MEGABYTE );
    memset( large.Begin(), 0x5A, large.GetSize() );
    AddActiveJobs( s, idA, kNumLargeResults );
    for ( uint32_t i = 0; i < kNumLargeResults; ++i )
    {
        MemoryStream payload( large.GetSize() );
        payload.WriteBuffer( large.Begin(), large.GetSize() );
        TEST_ASSERT( s.QueueResult( idA, false, Move( payload ) ) );
    }
    TEST_ASSERT( WaitUntil( [ &s, idA ]() { return ( GetNumJobsActive( s, idA ) < kNumLargeResults ); } ) );
    Thread::Sleep( 100 ); // It has taken A's send mutex

    PchAsker asker;
    asker.m_Server = &s;
    asker.m_SlowClient = idA;
    asker.m_SlowPchId = 0x1234;
    asker.m_OtherClient = idB;
    asker.m_OtherPchId = 0x5678;
    Thread thread;
    thread.Start( PchAsker::ThreadFunc, "PchAsker", &asker );
    const bool returned = WaitUntil( [ &asker ]() { return asker.m_Done.Load(); }, 2.0f );
    const bool otherAsked = WaitUntil( [ &b ]() { return ( b.GetNumPchNeeded() == 1 ); }, 2.0f );
    a.StallReading( false );
    thread.Join();
    TEST_ASSERT( returned );
    TEST_ASSERT( otherAsked );
    TEST_ASSERT( b.GetPchNeededId( 0 ) == 0x5678 );

    // A is asked once, after the results queued before
    TEST_ASSERT( WaitUntil( [ &a ]() { return ( a.GetNumPchNeeded() == 1 ); } ) );
    TEST_ASSERT( CountMessages( a, Protocol::MSG_JOB_RESULT ) == kNumLargeResults );
    Thread::Sleep( 100 );
    TEST_ASSERT( a.GetNumPchNeeded() == 1 );
    TEST_ASSERT( a.GetPchNeededId( 0 ) == 0x1234 );

    a.ShutdownAllConnections();
    b.ShutdownAllConnections();
}

//------------------------------------------------------------------------------
