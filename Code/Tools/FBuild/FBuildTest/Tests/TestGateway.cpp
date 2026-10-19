// TestGateway.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildTest/Tests/FBuildTest.h"
#include "Tools/FBuild/FBuildTest/Tests/DistributedTestHelpers.h"
#include "Tools/FBuild/FBuildTest/Tests/SyntheticToolchain.h"

#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/Helpers/JobStream.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/Protocol/Server.h"
#include "Tools/FBuild/FBuildCore/Protocol/WorkerSendQueue.h"

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
    static uint32_t ConnectProbe( Server & server, DistributedTest::HandshakeProbe & probe );
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
/*static*/ uint32_t TestGateway::ConnectProbe( Server & server, DistributedTest::HandshakeProbe & probe )
{
    const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
    if ( ci == nullptr )
    {
        return 0;
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

//------------------------------------------------------------------------------
