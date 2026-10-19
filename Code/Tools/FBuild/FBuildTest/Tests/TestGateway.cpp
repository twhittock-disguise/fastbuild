// TestGateway.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildTest/Tests/FBuildTest.h"
#include "Tools/FBuild/FBuildTest/Tests/DistributedTestHelpers.h"
#include "Tools/FBuild/FBuildTest/Tests/SyntheticToolchain.h"

#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/Protocol/Server.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Containers/Move.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/Process/Thread.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Timer.h"

#include <string.h>

// TestGateway
//------------------------------------------------------------------------------
class TestGateway : public FBuildTest
{
private:
    DECLARE_TESTS

    virtual void PreTest() const override;
    virtual void PostTest() const override;

    void SlowClientDoesNotHoldBackAnother() const;

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

//------------------------------------------------------------------------------
