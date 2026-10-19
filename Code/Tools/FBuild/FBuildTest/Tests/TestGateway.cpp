// TestGateway.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildTest/Tests/FBuildTest.h"
#include "Tools/FBuild/FBuildTest/Tests/DistributedTestHelpers.h"
#include "Tools/FBuild/FBuildTest/Tests/SyntheticToolchain.h"

#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/Gateway/DownstreamConnection.h"
#include "Tools/FBuild/FBuildCore/Gateway/Gateway.h"
#include "Tools/FBuild/FBuildCore/Gateway/GatewayConfig.h"
#include "Tools/FBuild/FBuildCore/Gateway/GatewayScheduler.h"
#include "Tools/FBuild/FBuildCore/Gateway/GatewayWire.h"
#include "Tools/FBuild/FBuildCore/Gateway/ToolchainStash.h"
#include "Tools/FBuild/FBuildCore/Helpers/JobStream.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchLease.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/Protocol/Server.h"
#include "Tools/FBuild/FBuildCore/Protocol/WorkerSendQueue.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/JobQueueRemote.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/PchDispatch.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Containers/Move.h"
#include "Core/Containers/UniquePtr.h"
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
    void StashKeepsWhatTheClientSent() const;
    void StashSurvivesARestart() const;
    void PartialStashIsDroppedAtStart() const;
    void StashRejectsWhatDoesNotMatch() const;
    void DuplicateUploadsAreIgnored() const;
    void StashEvictsTheLeastRecentlyUsedToolchainNotInUse() const;
    void PinnedToolchainIsNeverEvicted() const;
    void FileWithTheManifestSlotIdIsRejected() const;
    void AdvertisedPinSurvivesUntilTheToolchainArrives() const;
    void ReleaseWithoutUseKeepsLaterPins() const;
    void DroppingAnIncompleteToolchainKeepsItsPins() const;
    void ReUploadDuringAPendingDeleteEndsIntact() const;
    void SchedulerTakesClientsInTurn() const;
    void SchedulerPrefersAWorkerHoldingThePch() const;
    void SchedulerTakesAJobItsWorkerHoldsFirst() const;
    void SchedulerNeverGivesAWorkerAJobItIsUnableToTake() const;
    void SchedulerUsesNoMoreSlotsThanAWorkerHas() const;
    void FailedJobIsRetriedElsewhereAtMostTwice() const;
    void RetryUsesTheSameWorkerWhenThereIsNoOther() const;
    void RetryWaitsForAConnectedWorkerWithoutSlots() const;
    void RetriedJobGoesFirst() const;
    void RemovedWorkerReportsItsRunningJobs() const;
    void RemovedClientDropsOnlyItsWaitingJobs() const;
    void WaitingForCountsOnlyJobsAWorkerCanTake() const;
    void GatewayConfigReadsHostsAndPorts() const;
    void DownstreamBecomesReadyWithTheWorkerInventories() const;
    void DownstreamSendsToolchainPchThenJob() const;
    void DownstreamSendsAJobOnlyBehindItsPch() const;
    void DownstreamResultIsHandedOver() const;
    void DownstreamAskingForAPchGetsItAgain() const;
    void DownstreamAskingTooOftenIsClosed() const;
    void DownstreamCompletingJobsMayAskForAPchAgain() const;
    void DownstreamAskingForAnUnknownPchIsClosed() const;
    void DownstreamOfAnotherMajorIsLost() const;
    void UnreachableDownstreamIsLost() const;
    void GatewayAdvertisesTheSumOfItsWorkers() const;
    void GatewayForwardsAJobAndItsResult() const;
    void JobsWaitForTheirToolchainAtTheGateway() const;
    void JobForAMissingToolchainRequestsIt() const;
    void AdvertisedToolchainIsPinnedForTheConnection() const;
    void GatewayAsksTheClientForAMissingPch() const;
    void PchCrossesTheUplinkOnceForTwoWorkers() const;
    void ResultsGoBackToTheClientThatSentTheJob() const;
    void ThreadLanesOfTwoWorkersDoNotCollide() const;
    void ClientsAreServedInTurn() const;
    void GatewayLogsForwarding() const;

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
    // Where a test's stash lives; it is empty at the start of every test
    static void GetStashDir( AString & outDir );
    static void AddActiveJobs( Server & server, uint32_t clientId, uint32_t numJobs );
    static uint32_t GetNumJobsActive( Server & server, uint32_t clientId );
    static size_t CountMessages( const DistributedTest::HandshakeProbe & probe, uint8_t type );
    // A pure gateway in front of numWorkers workers, which listen from kGatewayTestDownstreamPort on
    static void MakeGatewayConfig( uint32_t numWorkers, GatewayConfig & outConfig );
    static Gateway::Stats GetGatewayStats( const Gateway & gateway );
    static bool WaitForReadyWorkers( const Gateway & gateway, uint32_t numWorkers );

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
    REGISTER_TEST( StashKeepsWhatTheClientSent )
    REGISTER_TEST( StashSurvivesARestart )
    REGISTER_TEST( PartialStashIsDroppedAtStart )
    REGISTER_TEST( StashRejectsWhatDoesNotMatch )
    REGISTER_TEST( DuplicateUploadsAreIgnored )
    REGISTER_TEST( StashEvictsTheLeastRecentlyUsedToolchainNotInUse )
    REGISTER_TEST( PinnedToolchainIsNeverEvicted )
    REGISTER_TEST( FileWithTheManifestSlotIdIsRejected )
    REGISTER_TEST( AdvertisedPinSurvivesUntilTheToolchainArrives )
    REGISTER_TEST( ReleaseWithoutUseKeepsLaterPins )
    REGISTER_TEST( DroppingAnIncompleteToolchainKeepsItsPins )
    REGISTER_TEST( ReUploadDuringAPendingDeleteEndsIntact )
    REGISTER_TEST( SchedulerTakesClientsInTurn )
    REGISTER_TEST( SchedulerPrefersAWorkerHoldingThePch )
    REGISTER_TEST( SchedulerTakesAJobItsWorkerHoldsFirst )
    REGISTER_TEST( SchedulerNeverGivesAWorkerAJobItIsUnableToTake )
    REGISTER_TEST( SchedulerUsesNoMoreSlotsThanAWorkerHas )
    REGISTER_TEST( FailedJobIsRetriedElsewhereAtMostTwice )
    REGISTER_TEST( RetryUsesTheSameWorkerWhenThereIsNoOther )
    REGISTER_TEST( RetryWaitsForAConnectedWorkerWithoutSlots )
    REGISTER_TEST( RetriedJobGoesFirst )
    REGISTER_TEST( RemovedWorkerReportsItsRunningJobs )
    REGISTER_TEST( RemovedClientDropsOnlyItsWaitingJobs )
    REGISTER_TEST( WaitingForCountsOnlyJobsAWorkerCanTake )
    REGISTER_TEST( GatewayConfigReadsHostsAndPorts )
    REGISTER_TEST( DownstreamBecomesReadyWithTheWorkerInventories )
    REGISTER_TEST( DownstreamSendsToolchainPchThenJob )
    REGISTER_TEST( DownstreamSendsAJobOnlyBehindItsPch )
    REGISTER_TEST( DownstreamResultIsHandedOver )
    REGISTER_TEST( DownstreamAskingForAPchGetsItAgain )
    REGISTER_TEST( DownstreamAskingTooOftenIsClosed )
    REGISTER_TEST( DownstreamCompletingJobsMayAskForAPchAgain )
    REGISTER_TEST( DownstreamAskingForAnUnknownPchIsClosed )
    REGISTER_TEST( DownstreamOfAnotherMajorIsLost )
    REGISTER_TEST( UnreachableDownstreamIsLost )
    REGISTER_TEST( GatewayAdvertisesTheSumOfItsWorkers )
    REGISTER_TEST( GatewayForwardsAJobAndItsResult )
    REGISTER_TEST( JobsWaitForTheirToolchainAtTheGateway )
    REGISTER_TEST( JobForAMissingToolchainRequestsIt )
    REGISTER_TEST( AdvertisedToolchainIsPinnedForTheConnection )
    REGISTER_TEST( GatewayAsksTheClientForAMissingPch )
    REGISTER_TEST( PchCrossesTheUplinkOnceForTwoWorkers )
    REGISTER_TEST( ResultsGoBackToTheClientThatSentTheJob )
    REGISTER_TEST( ThreadLanesOfTwoWorkersDoNotCollide )
    REGISTER_TEST( ClientsAreServedInTurn )
    REGISTER_TEST( GatewayLogsForwarding )
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

// GetStashDir
//------------------------------------------------------------------------------
/*static*/ void TestGateway::GetStashDir( AString & outDir )
{
    VERIFY( FBuild::GetTempDir( outDir ) );
    outDir += "stash_tmp";
    outDir += NATIVE_SLASH;
    outDir += "gateway";
    outDir += NATIVE_SLASH;
    outDir += "toolchains";
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

// Toolchain stash helpers
//------------------------------------------------------------------------------
namespace
{
    // A manifest of numFiles files, as a client sends it (the stash reads only its header)
    void MakeStashManifest( MemoryStream & ms, uint64_t toolId, uint32_t numFiles )
    {
        ms.Write( toolId );
        ms.Write( AStackString( "C:\\toolchain\\" ) );
        ms.Write( numFiles );
        for ( uint32_t i = 0; i < numFiles; ++i )
        {
            AStackString<> name;
            name.Format( "C:\\toolchain\\file%u.exe", i );
            ms.Write( name );
            ms.Write( (uint64_t)1 );
            ms.Write( (uint64_t)( 100 + i ) );
            ms.Write( (uint32_t)( 10 + i ) );
        }
        ms.Write( (uint32_t)0 );
    }

    ToolchainStash::Result AddStashManifest( ToolchainStash & stash, uint64_t toolId, uint32_t numFiles )
    {
        MemoryStream ms;
        MakeStashManifest( ms, toolId, numFiles );
        return stash.AddManifest( toolId, ms.GetData(), ms.GetSize() );
    }

    // What a test sends as a file of a toolchain
    void FillStashFile( uint64_t toolId, uint32_t fileId, Array<char> & out )
    {
        out.SetSize( 100 + fileId );
        memset( out.Begin(), (int)( 'a' + ( ( toolId + fileId ) % 26 ) ), out.GetSize() );
    }

    // A complete toolchain of numFiles files
    void AddCompleteToStash( ToolchainStash & stash, uint64_t toolId, uint32_t numFiles = 1 )
    {
        TEST_ASSERT( AddStashManifest( stash, toolId, numFiles ) != ToolchainStash::STASH_INVALID );
        for ( uint32_t i = 0; i < numFiles; ++i )
        {
            Array<char> bytes;
            FillStashFile( toolId, i, bytes );
            TEST_ASSERT( stash.AddFile( toolId, i, bytes.Begin(), bytes.GetSize() ) != ToolchainStash::STASH_INVALID );
        }
        TEST_ASSERT( stash.IsComplete( toolId ) );
    }

    bool StoredFileEquals( const AString & path, const void * expected, size_t expectedSize )
    {
        FileStream f;
        if ( f.Open( path.Get() ) == false )
        {
            return false;
        }
        Array<char> bytes;
        bytes.SetSize( (size_t)f.GetFileSize() );
        return ( f.ReadBuffer( bytes.Begin(), bytes.GetSize() ) == bytes.GetSize() ) &&
               ( bytes.GetSize() == expectedSize ) &&
               ( memcmp( bytes.Begin(), expected, expectedSize ) == 0 );
    }

    bool StashHoldsFile( const ToolchainStash & stash, uint64_t toolId, uint32_t fileId, const Array<char> & expected )
    {
        AStackString<> path;
        uint32_t size = 0;
        return stash.GetFile( toolId, fileId, path, size ) &&
               ( size == expected.GetSize() ) &&
               StoredFileEquals( path, expected.Begin(), expected.GetSize() );
    }

    bool IsInList( const Array<uint64_t> & ids, uint64_t id )
    {
        return ( ids.Find( id ) != nullptr );
    }

    void GetStashToolchainDir( const AString & stashDir, uint64_t toolId, AString & outDir )
    {
        outDir = stashDir;
        outDir.AppendFormat( "toolchain.%016" PRIx64, toolId );
    }
}

// StashKeepsWhatTheClientSent
//------------------------------------------------------------------------------
void TestGateway::StashKeepsWhatTheClientSent() const
{
    AStackString<> dir;
    GetStashDir( dir );
    ToolchainStash stash( dir );

    const uint64_t toolId = 0x1234ABCD;
    MemoryStream manifest;
    MakeStashManifest( manifest, toolId, 2 );
    TEST_ASSERT( stash.AddManifest( toolId, manifest.GetData(), manifest.GetSize() ) == ToolchainStash::STASH_INCOMPLETE );
    TEST_ASSERT( stash.HasManifest( toolId ) );
    TEST_ASSERT( stash.IsComplete( toolId ) == false );
    TEST_ASSERT( stash.GetNumFiles( toolId ) == 2 );
    Array<uint32_t> missing;
    stash.GetMissingFiles( toolId, missing );
    TEST_ASSERT( ( missing.GetSize() == 2 ) && ( missing[ 0 ] == 0 ) && ( missing[ 1 ] == 1 ) );
    Array<uint64_t> complete;
    stash.GetComplete( complete );
    TEST_ASSERT( complete.IsEmpty() );

    Array<char> file0;
    Array<char> file1;
    FillStashFile( toolId, 0, file0 );
    FillStashFile( toolId, 1, file1 );
    TEST_ASSERT( stash.AddFile( toolId, 1, file1.Begin(), file1.GetSize() ) == ToolchainStash::STASH_INCOMPLETE ); // In any order
    stash.GetMissingFiles( toolId, missing );
    TEST_ASSERT( ( missing.GetSize() == 1 ) && ( missing[ 0 ] == 0 ) );
    TEST_ASSERT( stash.AddFile( toolId, 0, file0.Begin(), file0.GetSize() ) == ToolchainStash::STASH_COMPLETE );
    TEST_ASSERT( stash.IsComplete( toolId ) );
    stash.GetMissingFiles( toolId, missing );
    TEST_ASSERT( missing.IsEmpty() );
    stash.GetComplete( complete );
    TEST_ASSERT( ( complete.GetSize() == 1 ) && ( complete[ 0 ] == toolId ) );

    // The payloads as they arrived, in the layout the gateway sends them from
    AStackString<> path;
    uint32_t size = 0;
    TEST_ASSERT( stash.GetManifestFile( toolId, path, size ) );
    TEST_ASSERT( size == manifest.GetSize() );
    TEST_ASSERT( path.BeginsWith( dir ) );
    TEST_ASSERT( path.Find( "toolchain.000000001234abcd" ) );
    TEST_ASSERT( path.EndsWith( "manifest.bin" ) );
    TEST_ASSERT( StoredFileEquals( path, manifest.GetData(), manifest.GetSize() ) );
    TEST_ASSERT( StashHoldsFile( stash, toolId, 0, file0 ) );
    TEST_ASSERT( StashHoldsFile( stash, toolId, 1, file1 ) );
    TEST_ASSERT( stash.GetFile( toolId, 1, path, size ) && path.EndsWith( "file.1.bin" ) );
    TEST_ASSERT( stash.GetFile( toolId, 2, path, size ) == false );
    TEST_ASSERT( stash.GetFile( toolId + 1, 0, path, size ) == false );
    TEST_ASSERT( stash.GetManifestFile( toolId + 1, path, size ) == false );

    // Only the payloads: no temporary file is left
    TEST_ASSERT( CountFilesUnder( dir ) == 3 );
}

// StashSurvivesARestart
//------------------------------------------------------------------------------
void TestGateway::StashSurvivesARestart() const
{
    AStackString<> dir;
    GetStashDir( dir );
    {
        ToolchainStash stash( dir );
        AddCompleteToStash( stash, 0x11, 2 );
        AddCompleteToStash( stash, 0x22, 1 );
    }

    ToolchainStash restarted( dir );
    Array<uint64_t> complete;
    restarted.GetComplete( complete );
    TEST_ASSERT( complete.GetSize() == 2 );
    TEST_ASSERT( IsInList( complete, 0x11 ) && IsInList( complete, 0x22 ) );
    TEST_ASSERT( restarted.GetNumFiles( 0x11 ) == 2 );
    TEST_ASSERT( restarted.IsComplete( 0x22 ) );
    Array<char> file;
    FillStashFile( 0x11, 1, file );
    TEST_ASSERT( StashHoldsFile( restarted, 0x11, 1, file ) );
    MemoryStream manifest;
    MakeStashManifest( manifest, 0x11, 2 );
    AStackString<> path;
    uint32_t size = 0;
    TEST_ASSERT( restarted.GetManifestFile( 0x11, path, size ) );
    TEST_ASSERT( StoredFileEquals( path, manifest.GetData(), manifest.GetSize() ) );

    // What arrives after the restart is a duplicate
    TEST_ASSERT( restarted.AddFile( 0x11, 1, "other", 5 ) == ToolchainStash::STASH_COMPLETE );
    TEST_ASSERT( StashHoldsFile( restarted, 0x11, 1, file ) );
}

// PartialStashIsDroppedAtStart
//------------------------------------------------------------------------------
void TestGateway::PartialStashIsDroppedAtStart() const
{
    AStackString<> dir;
    GetStashDir( dir );
    {
        ToolchainStash stash( dir );
        AddCompleteToStash( stash, 0xC0, 1 );
        TEST_ASSERT( AddStashManifest( stash, 0xA0, 2 ) == ToolchainStash::STASH_INCOMPLETE ); // Manifest only
        TEST_ASSERT( AddStashManifest( stash, 0xB0, 2 ) == ToolchainStash::STASH_INCOMPLETE ); // And a file
        Array<char> file;
        FillStashFile( 0xB0, 0, file );
        TEST_ASSERT( stash.AddFile( 0xB0, 0, file.Begin(), file.GetSize() ) == ToolchainStash::STASH_INCOMPLETE );
    }

    // What a run that stopped in the middle of a write leaves
    AStackString<> leftover;
    GetStashToolchainDir( dir, 0xC0, leftover );
    leftover += NATIVE_SLASH;
    leftover += "file.0.bin.tmp";
    {
        FileStream f;
        TEST_ASSERT( f.Open( leftover.Get(), FileStream::WRITE_ONLY ) );
        TEST_ASSERT( f.Write( "x", 1 ) == 1 );
    }
    AStackString<> garbage;
    GetStashToolchainDir( dir, 0xD0, garbage );
    TEST_ASSERT( FileIO::EnsurePathExists( garbage ) );
    garbage += NATIVE_SLASH;
    garbage += "manifest.bin";
    {
        FileStream f;
        TEST_ASSERT( f.Open( garbage.Get(), FileStream::WRITE_ONLY ) );
        TEST_ASSERT( f.Write( "garbage", 7 ) == 7 );
    }

    ToolchainStash restarted( dir );
    Array<uint64_t> complete;
    restarted.GetComplete( complete );
    TEST_ASSERT( ( complete.GetSize() == 1 ) && ( complete[ 0 ] == 0xC0 ) );
    const uint64_t dropped[] = { 0xA0, 0xB0, 0xD0 };
    for ( const uint64_t id : dropped )
    {
        TEST_ASSERT( restarted.HasManifest( id ) == false );
        AStackString<> droppedDir;
        GetStashToolchainDir( dir, id, droppedDir );
        TEST_ASSERT( FileIO::DirectoryExists( droppedDir ) == false );
    }
    TEST_ASSERT( FileIO::FileExists( leftover.Get() ) == false );
    TEST_ASSERT( CountFilesUnder( dir ) == 2 );
}

// StashRejectsWhatDoesNotMatch
//------------------------------------------------------------------------------
void TestGateway::StashRejectsWhatDoesNotMatch() const
{
    AStackString<> dir;
    GetStashDir( dir );
    ToolchainStash stash( dir );

    // Another tool id than the manifest carries
    MemoryStream manifest;
    MakeStashManifest( manifest, 0x11, 2 );
    TEST_ASSERT( stash.AddManifest( 0x12, manifest.GetData(), manifest.GetSize() ) == ToolchainStash::STASH_INVALID );

    // No files
    MemoryStream empty;
    MakeStashManifest( empty, 0x13, 0 );
    TEST_ASSERT( stash.AddManifest( 0x13, empty.GetData(), empty.GetSize() ) == ToolchainStash::STASH_INVALID );

    // Cut short
    const size_t cuts[] = { 0, 7, 20 };
    for ( const size_t cut : cuts )
    {
        TEST_ASSERT( stash.AddManifest( 0x11, manifest.GetData(), cut ) == ToolchainStash::STASH_INVALID );
    }

    // A file before its manifest, from a client that never sent one
    TEST_ASSERT( stash.AddFile( 0x11, 0, "bytes", 5 ) == ToolchainStash::STASH_INVALID );
    TEST_ASSERT( stash.HasManifest( 0x11 ) == false );
    TEST_ASSERT( stash.GetNumFiles( 0x11 ) == 0 );
    stash.DropIncomplete( 0x11 ); // Nothing to drop
    stash.ReleaseUse( 0x11 );
    TEST_ASSERT( CountFilesUnder( dir ) == 0 );

    // A file the manifest does not list
    TEST_ASSERT( stash.AddManifest( 0x11, manifest.GetData(), manifest.GetSize() ) == ToolchainStash::STASH_INCOMPLETE );
    TEST_ASSERT( stash.AddFile( 0x11, 2, "bytes", 5 ) == ToolchainStash::STASH_INVALID );
    TEST_ASSERT( stash.AddFile( 0x11, 0xFFFFFFFF, "bytes", 5 ) == ToolchainStash::STASH_INVALID );

    // The same toolchain described with another number of files
    MemoryStream other;
    MakeStashManifest( other, 0x11, 3 );
    TEST_ASSERT( stash.AddManifest( 0x11, other.GetData(), other.GetSize() ) == ToolchainStash::STASH_INVALID );

    // None of it changed what is held
    TEST_ASSERT( stash.GetNumFiles( 0x11 ) == 2 );
    Array<uint32_t> missing;
    stash.GetMissingFiles( 0x11, missing );
    TEST_ASSERT( missing.GetSize() == 2 );
    TEST_ASSERT( CountFilesUnder( dir ) == 1 );
}

// DuplicateUploadsAreIgnored
//------------------------------------------------------------------------------
void TestGateway::DuplicateUploadsAreIgnored() const
{
    AStackString<> dir;
    GetStashDir( dir );
    ToolchainStash stash( dir );

    const uint64_t toolId = 0x77;
    MemoryStream manifest;
    MakeStashManifest( manifest, toolId, 2 );
    TEST_ASSERT( stash.AddManifest( toolId, manifest.GetData(), manifest.GetSize() ) == ToolchainStash::STASH_INCOMPLETE );
    Array<char> file0;
    Array<char> file1;
    FillStashFile( toolId, 0, file0 );
    FillStashFile( toolId, 1, file1 );
    TEST_ASSERT( stash.AddFile( toolId, 0, file0.Begin(), file0.GetSize() ) == ToolchainStash::STASH_INCOMPLETE );

    // Copies that differ, so that a replacement would show
    MemoryStream otherManifest;
    MakeStashManifest( otherManifest, toolId, 2 );
    otherManifest.Write( (uint32_t)0x5555 ); // Trailing bytes
    Array<char> otherFile;
    otherFile.SetSize( 17 );
    memset( otherFile.Begin(), 'Z', otherFile.GetSize() );

    AStackString<> manifestPath;
    uint32_t manifestSize = 0;
    TEST_ASSERT( stash.GetManifestFile( toolId, manifestPath, manifestSize ) );
    TEST_ASSERT( stash.AddManifest( toolId, otherManifest.GetData(), otherManifest.GetSize() ) == ToolchainStash::STASH_INCOMPLETE );
    TEST_ASSERT( stash.AddFile( toolId, 0, otherFile.Begin(), otherFile.GetSize() ) == ToolchainStash::STASH_INCOMPLETE );
    TEST_ASSERT( stash.IsComplete( toolId ) == false );
    uint32_t size = 0;
    TEST_ASSERT( stash.GetManifestFile( toolId, manifestPath, size ) && ( size == manifestSize ) );
    TEST_ASSERT( StoredFileEquals( manifestPath, manifest.GetData(), manifest.GetSize() ) );
    TEST_ASSERT( StashHoldsFile( stash, toolId, 0, file0 ) );

    TEST_ASSERT( stash.AddFile( toolId, 1, file1.Begin(), file1.GetSize() ) == ToolchainStash::STASH_COMPLETE );
    TEST_ASSERT( stash.AddFile( toolId, 1, otherFile.Begin(), otherFile.GetSize() ) == ToolchainStash::STASH_COMPLETE );
    TEST_ASSERT( stash.AddManifest( toolId, otherManifest.GetData(), otherManifest.GetSize() ) == ToolchainStash::STASH_COMPLETE );
    TEST_ASSERT( stash.AddFile( toolId, 0, otherFile.Begin(), otherFile.GetSize() ) == ToolchainStash::STASH_COMPLETE );
    TEST_ASSERT( stash.GetManifestFile( toolId, manifestPath, size ) && ( size == manifestSize ) );
    TEST_ASSERT( StoredFileEquals( manifestPath, manifest.GetData(), manifest.GetSize() ) );
    TEST_ASSERT( StashHoldsFile( stash, toolId, 0, file0 ) );
    TEST_ASSERT( StashHoldsFile( stash, toolId, 1, file1 ) );
    TEST_ASSERT( CountFilesUnder( dir ) == 3 );
}

// StashEvictsTheLeastRecentlyUsedToolchainNotInUse
//------------------------------------------------------------------------------
void TestGateway::StashEvictsTheLeastRecentlyUsedToolchainNotInUse() const
{
    AStackString<> dir;
    GetStashDir( dir );
    ToolchainStash stash( dir );

    // Ids 1..kMaxToolchains, the first the least recently used
    for ( uint64_t id = 1; id <= ToolchainStash::kMaxToolchains; ++id )
    {
        AddCompleteToStash( stash, id );
    }
    Array<uint64_t> complete;
    stash.GetComplete( complete );
    TEST_ASSERT( complete.GetSize() == ToolchainStash::kMaxToolchains );
    TEST_ASSERT( complete[ 0 ] == ToolchainStash::kMaxToolchains ); // Most recently used first
    TEST_ASSERT( complete[ ToolchainStash::kMaxToolchains - 1 ] == 1 );

    // 1 is in use, and 2 is read: 3 is what is left to go
    stash.AddUse( 1 );
    AStackString<> path;
    uint32_t size = 0;
    TEST_ASSERT( stash.GetFile( 2, 0, path, size ) );
    AddCompleteToStash( stash, 100 );

    stash.GetComplete( complete );
    TEST_ASSERT( complete.GetSize() == ToolchainStash::kMaxToolchains );
    TEST_ASSERT( complete[ 0 ] == 100 );
    TEST_ASSERT( IsInList( complete, 1 ) && IsInList( complete, 2 ) );
    TEST_ASSERT( IsInList( complete, 3 ) == false );
    TEST_ASSERT( stash.HasManifest( 3 ) == false );
    AStackString<> evictedDir;
    GetStashToolchainDir( dir, 3, evictedDir );
    TEST_ASSERT( FileIO::DirectoryExists( evictedDir ) == false );
    TEST_ASSERT( CountFilesUnder( dir ) == ( 2 * ToolchainStash::kMaxToolchains ) );

    // One being received is never what goes, but it takes a place
    TEST_ASSERT( AddStashManifest( stash, 200, 2 ) == ToolchainStash::STASH_INCOMPLETE );
    stash.GetComplete( complete );
    TEST_ASSERT( complete.GetSize() == ( ToolchainStash::kMaxToolchains - 1 ) );
    TEST_ASSERT( stash.HasManifest( 200 ) );
    TEST_ASSERT( IsInList( complete, 1 ) );
}

// PinnedToolchainIsNeverEvicted
//------------------------------------------------------------------------------
void TestGateway::PinnedToolchainIsNeverEvicted() const
{
    AStackString<> dir;
    GetStashDir( dir );
    ToolchainStash stash( dir );

    AddCompleteToStash( stash, 1000 );
    stash.AddUse( 1000 );
    stash.AddUse( 1000 ); // Two clients advertised it
    for ( uint64_t id = 1; id <= ( 2 * ToolchainStash::kMaxToolchains ); ++id )
    {
        AddCompleteToStash( stash, id );
    }
    TEST_ASSERT( stash.IsComplete( 1000 ) );
    Array<char> file;
    FillStashFile( 1000, 0, file );
    Array<uint64_t> complete;
    stash.GetComplete( complete );
    TEST_ASSERT( complete.GetSize() == ToolchainStash::kMaxToolchains );

    // Held until the last use ends, then it goes like any other
    stash.ReleaseUse( 1000 );
    for ( uint64_t id = 5000; id < ( 5000 + ToolchainStash::kMaxToolchains ); ++id )
    {
        AddCompleteToStash( stash, id );
    }
    TEST_ASSERT( StashHoldsFile( stash, 1000, 0, file ) );
    stash.ReleaseUse( 1000 );
    for ( uint64_t id = 6000; id < ( 6000 + ToolchainStash::kMaxToolchains ); ++id )
    {
        AddCompleteToStash( stash, id );
    }
    TEST_ASSERT( stash.IsComplete( 1000 ) == false );
    stash.GetComplete( complete );
    TEST_ASSERT( complete.GetSize() == ToolchainStash::kMaxToolchains );
    AStackString<> evictedDir;
    GetStashToolchainDir( dir, 1000, evictedDir );
    TEST_ASSERT( FileIO::DirectoryExists( evictedDir ) == false );
}

// FileWithTheManifestSlotIdIsRejected
//------------------------------------------------------------------------------
void TestGateway::FileWithTheManifestSlotIdIsRejected() const
{
    AStackString<> dir;
    GetStashDir( dir );
    ToolchainStash stash( dir );

    // For a toolchain nobody has sent a manifest of
    TEST_ASSERT( stash.AddFile( 0x31, 0xFFFFFFFF, "bytes", 5 ) == ToolchainStash::STASH_INVALID );
    TEST_ASSERT( stash.HasManifest( 0x31 ) == false );
    TEST_ASSERT( stash.IsComplete( 0x31 ) == false );
    TEST_ASSERT( stash.GetNumFiles( 0x31 ) == 0 );
    Array<uint64_t> complete;
    stash.GetComplete( complete );
    TEST_ASSERT( complete.IsEmpty() );
    TEST_ASSERT( CountFilesUnder( dir ) == 0 );

    // And for one that exists
    TEST_ASSERT( AddStashManifest( stash, 0x32, 1 ) == ToolchainStash::STASH_INCOMPLETE );
    TEST_ASSERT( stash.AddFile( 0x32, 0xFFFFFFFF, "bytes", 5 ) == ToolchainStash::STASH_INVALID );
    TEST_ASSERT( CountFilesUnder( dir ) == 1 );
}

// AdvertisedPinSurvivesUntilTheToolchainArrives
//------------------------------------------------------------------------------
namespace
{
    // Fills the stash past its limit with toolchains nobody uses
    void AddPressureToStash( ToolchainStash & stash, uint64_t firstId )
    {
        for ( uint64_t id = firstId; id < ( firstId + ( 2 * ToolchainStash::kMaxToolchains ) ); ++id )
        {
            AddCompleteToStash( stash, id );
        }
    }
}

void TestGateway::AdvertisedPinSurvivesUntilTheToolchainArrives() const
{
    AStackString<> dir;
    GetStashDir( dir );
    ToolchainStash stash( dir );

    // A job pins its toolchain before the manifest arrives
    stash.AddUse( 50 );
    TEST_ASSERT( stash.HasManifest( 50 ) == false );
    AddCompleteToStash( stash, 50 );
    AddPressureToStash( stash, 1000 );
    TEST_ASSERT( stash.IsComplete( 50 ) );

    // Only the matching release lets it go
    stash.ReleaseUse( 50 );
    AddPressureToStash( stash, 2000 );
    TEST_ASSERT( stash.IsComplete( 50 ) == false );
}

// ReleaseWithoutUseKeepsLaterPins
//------------------------------------------------------------------------------
void TestGateway::ReleaseWithoutUseKeepsLaterPins() const
{
    AStackString<> dir;
    GetStashDir( dir );
    ToolchainStash stash( dir );

    // A release with nothing to release leaves no debt for a later use to pay
    stash.ReleaseUse( 60 );
    stash.AddUse( 60 );
    AddCompleteToStash( stash, 60 );
    AddPressureToStash( stash, 1000 );
    TEST_ASSERT( stash.IsComplete( 60 ) );

    // Extra releases do not take a later pin with them
    stash.AddUse( 61 );
    AddCompleteToStash( stash, 61 );
    stash.ReleaseUse( 61 );
    stash.ReleaseUse( 61 );
    stash.AddUse( 61 );
    AddPressureToStash( stash, 2000 );
    TEST_ASSERT( stash.IsComplete( 61 ) );
    stash.ReleaseUse( 61 );
    AddPressureToStash( stash, 3000 );
    TEST_ASSERT( stash.IsComplete( 61 ) == false );
}

// DroppingAnIncompleteToolchainKeepsItsPins
//------------------------------------------------------------------------------
void TestGateway::DroppingAnIncompleteToolchainKeepsItsPins() const
{
    AStackString<> dir;
    GetStashDir( dir );
    ToolchainStash stash( dir );

    stash.AddUse( 80 );
    TEST_ASSERT( AddStashManifest( stash, 80, 2 ) == ToolchainStash::STASH_INCOMPLETE );
    stash.DropIncomplete( 80 );
    TEST_ASSERT( stash.HasManifest( 80 ) == false );

    // Another client sends it again
    AddCompleteToStash( stash, 80, 2 );
    AddPressureToStash( stash, 1000 );
    TEST_ASSERT( stash.IsComplete( 80 ) );
    stash.ReleaseUse( 80 );
    AddPressureToStash( stash, 2000 );
    TEST_ASSERT( stash.IsComplete( 80 ) == false );
}

// ReUploadDuringAPendingDeleteEndsIntact
//------------------------------------------------------------------------------
namespace
{
    struct StalledDelete
    {
        static void Hook( void * param )
        {
            StalledDelete * self = static_cast<StalledDelete *>( param );
            self->m_Entered.Store( true );
            while ( self->m_Release.Load() == false )
            {
                Thread::Sleep( 1 );
            }
        }

        Atomic<bool> m_Entered{ false };
        Atomic<bool> m_Release{ false };
    };

    struct StashUploader
    {
        static uint32_t EvictorFunc( void * param )
        {
            // Takes the place after the last one, which evicts toolchain 1
            StashUploader * self = static_cast<StashUploader *>( param );
            MemoryStream ms;
            MakeStashManifest( ms, 500, 1 );
            self->m_Stash->AddManifest( 500, ms.GetData(), ms.GetSize() );
            Array<char> bytes;
            FillStashFile( 500, 0, bytes );
            self->m_Stash->AddFile( 500, 0, bytes.Begin(), bytes.GetSize() );
            return 0;
        }
        static uint32_t ReUploadFunc( void * param )
        {
            StashUploader * self = static_cast<StashUploader *>( param );
            MemoryStream ms;
            MakeStashManifest( ms, 1, 2 );
            self->m_ManifestResult = self->m_Stash->AddManifest( 1, ms.GetData(), ms.GetSize() );
            Array<char> bytes;
            for ( uint32_t i = 0; i < 2; ++i )
            {
                FillStashFile( 1, i, bytes );
                self->m_FileResult = self->m_Stash->AddFile( 1, i, bytes.Begin(), bytes.GetSize() );
            }
            self->m_Done.Store( true );
            return 0;
        }

        ToolchainStash *            m_Stash = nullptr;
        ToolchainStash::Result      m_ManifestResult = ToolchainStash::STASH_INVALID;
        ToolchainStash::Result      m_FileResult = ToolchainStash::STASH_INVALID;
        Atomic<bool>                m_Done{ false };
    };
}

void TestGateway::ReUploadDuringAPendingDeleteEndsIntact() const
{
    AStackString<> dir;
    GetStashDir( dir );
    ToolchainStash stash( dir );
    for ( uint64_t id = 1; id <= ToolchainStash::kMaxToolchains; ++id )
    {
        AddCompleteToStash( stash, id, 2 );
    }

    // Toolchain 1 is evicted, and its directory delete stalls
    StalledDelete stall;
    stash.SetBeforeDeleteHook( StalledDelete::Hook, &stall );
    StashUploader uploader;
    uploader.m_Stash = &stash;
    Thread evictor;
    evictor.Start( StashUploader::EvictorFunc, "StashEvictor", &uploader );
    TEST_ASSERT( WaitUntil( [ &stall ]() { return stall.m_Entered.Load(); } ) );
    TEST_ASSERT( stash.IsComplete( 1 ) == false );

    // A client sends it again meanwhile
    Thread reuploader;
    reuploader.Start( StashUploader::ReUploadFunc, "StashReUpload", &uploader );
    Thread::Sleep( 100 );
    stall.m_Release.Store( true );
    evictor.Join();
    const bool done = WaitUntil( [ &uploader ]() { return uploader.m_Done.Load(); } );
    reuploader.Join();
    stash.SetBeforeDeleteHook( nullptr, nullptr );
    TEST_ASSERT( done );

    // What the delete removed is what was evicted, not what arrived
    TEST_ASSERT( uploader.m_ManifestResult != ToolchainStash::STASH_INVALID );
    TEST_ASSERT( uploader.m_FileResult == ToolchainStash::STASH_COMPLETE );
    TEST_ASSERT( stash.IsComplete( 1 ) );
    Array<char> file;
    for ( uint32_t i = 0; i < 2; ++i )
    {
        FillStashFile( 1, i, file );
        TEST_ASSERT( StashHoldsFile( stash, 1, i, file ) );
    }
    MemoryStream manifest;
    MakeStashManifest( manifest, 1, 2 );
    AStackString<> path;
    uint32_t size = 0;
    TEST_ASSERT( stash.GetManifestFile( 1, path, size ) );
    TEST_ASSERT( StoredFileEquals( path, manifest.GetData(), manifest.GetSize() ) );
}

// Scheduler helpers
//------------------------------------------------------------------------------
namespace
{
    // Every worker is able and holds nothing, except as set. The first matching rule wins; 0 matches any id.
    class FakeAffinity
    {
    public:
        void Set( uint32_t workerId, uint64_t pchId, uint64_t toolId, GatewayScheduler::Affinity affinity )
        {
            m_Rules.Append( Rule{ workerId, pchId, toolId, affinity } );
        }

        GatewayScheduler::Affinity operator()( uint32_t workerId, uint64_t pchId, uint64_t toolId ) const
        {
            for ( const Rule & rule : m_Rules )
            {
                if ( ( rule.m_WorkerId == workerId ) &&
                     ( ( rule.m_PchId == 0 ) || ( rule.m_PchId == pchId ) ) &&
                     ( ( rule.m_ToolId == 0 ) || ( rule.m_ToolId == toolId ) ) )
                {
                    return rule.m_Affinity;
                }
            }
            return GatewayScheduler::AFFINITY_NONE;
        }

    private:
        struct Rule
        {
            uint32_t m_WorkerId;
            uint64_t m_PchId;
            uint64_t m_ToolId;
            GatewayScheduler::Affinity m_Affinity;
        };
        Array<Rule> m_Rules;
    };

    bool Starts( GatewayScheduler & scheduler, const FakeAffinity & affinity, uint32_t jobId, uint32_t workerId )
    {
        GatewayScheduler::Assignment assignment;
        return scheduler.Next( affinity, assignment ) &&
               ( assignment.m_JobId == jobId ) &&
               ( assignment.m_WorkerId == workerId );
    }

    bool StartsNothing( GatewayScheduler & scheduler, const FakeAffinity & affinity )
    {
        GatewayScheduler::Assignment assignment;
        return ( scheduler.Next( affinity, assignment ) == false );
    }

    // A job that ran on the worker ends
    void Complete( GatewayScheduler & scheduler, uint32_t jobId, uint32_t workerId )
    {
        scheduler.ReleaseSlot( workerId );
        scheduler.OnCompleted( jobId );
    }

    constexpr uint64_t kSchedulerTool = 0x7001;
}

// SchedulerTakesClientsInTurn
//------------------------------------------------------------------------------
void TestGateway::SchedulerTakesClientsInTurn() const
{
    const FakeAffinity affinity;
    GatewayScheduler scheduler;
    scheduler.SetWorkerSlots( 1, 8 );
    for ( uint32_t jobId = 101; jobId <= 106; ++jobId )
    {
        scheduler.AddJob( jobId, 1, 0, kSchedulerTool );
    }
    scheduler.AddJob( 201, 2, 0, kSchedulerTool );
    scheduler.AddJob( 202, 2, 0, kSchedulerTool );

    // A, B, A, B, then A for the rest
    const uint32_t expected[] = { 101, 201, 102, 202, 103, 104, 105, 106 };
    for ( const uint32_t jobId : expected )
    {
        TEST_ASSERT( Starts( scheduler, affinity, jobId, 1 ) );
    }
    TEST_ASSERT( StartsNothing( scheduler, affinity ) );
    TEST_ASSERT( scheduler.GetNumRunning() == 8 );
}

// SchedulerPrefersAWorkerHoldingThePch
//------------------------------------------------------------------------------
void TestGateway::SchedulerPrefersAWorkerHoldingThePch() const
{
    const uint64_t pchHeld = 0x50;
    const uint64_t pchBeingSent = 0x51;
    const uint64_t pchNowhere = 0x52;
    FakeAffinity affinity;
    affinity.Set( 2, pchHeld, 0, GatewayScheduler::AFFINITY_HOLDS );
    affinity.Set( 3, pchBeingSent, 0, GatewayScheduler::AFFINITY_RECEIVING );

    GatewayScheduler scheduler;
    for ( uint32_t workerId = 1; workerId <= 3; ++workerId )
    {
        scheduler.SetWorkerSlots( workerId, 4 );
    }
    scheduler.AddJob( 1, 1, pchHeld, kSchedulerTool );
    scheduler.AddJob( 2, 1, pchHeld, kSchedulerTool );
    scheduler.AddJob( 3, 1, pchBeingSent, kSchedulerTool );
    scheduler.AddJob( 4, 1, pchNowhere, kSchedulerTool );

    TEST_ASSERT( Starts( scheduler, affinity, 1, 2 ) );
    TEST_ASSERT( Starts( scheduler, affinity, 2, 2 ) );  // Holding beats more free slots
    TEST_ASSERT( Starts( scheduler, affinity, 3, 3 ) );  // Receiving beats the lower id
    TEST_ASSERT( Starts( scheduler, affinity, 4, 1 ) );  // The most free slots, then the lower id

    scheduler.AddJob( 5, 1, pchNowhere, kSchedulerTool );
    scheduler.SetWorkerSlots( 3, 8 );
    TEST_ASSERT( Starts( scheduler, affinity, 5, 3 ) );  // More free slots beat the lower id
}

// SchedulerTakesAJobItsWorkerHoldsFirst
//------------------------------------------------------------------------------
void TestGateway::SchedulerTakesAJobItsWorkerHoldsFirst() const
{
    // Jobs 1..n, each with its own PCH; the worker holds those of job 2 and job n
    const uint32_t numJobs = GatewayScheduler::kScanDepth + 2;
    FakeAffinity affinity;
    affinity.Set( 1, 0x100 + 2, 0, GatewayScheduler::AFFINITY_HOLDS );
    affinity.Set( 1, 0x100 + numJobs, 0, GatewayScheduler::AFFINITY_HOLDS );

    GatewayScheduler scheduler;
    scheduler.SetWorkerSlots( 1, 32 );
    for ( uint32_t jobId = 1; jobId <= numJobs; ++jobId )
    {
        scheduler.AddJob( jobId, 1, 0x100 + jobId, kSchedulerTool );
    }

    TEST_ASSERT( Starts( scheduler, affinity, 2, 1 ) );
    TEST_ASSERT( Starts( scheduler, affinity, 1, 1 ) );          // Job n is beyond kScanDepth
    TEST_ASSERT( Starts( scheduler, affinity, numJobs, 1 ) );    // Now it is within it
    TEST_ASSERT( Starts( scheduler, affinity, 3, 1 ) );
}

// SchedulerNeverGivesAWorkerAJobItIsUnableToTake
//------------------------------------------------------------------------------
void TestGateway::SchedulerNeverGivesAWorkerAJobItIsUnableToTake() const
{
    const uint64_t otherTool = 0x7002;
    FakeAffinity affinity;
    affinity.Set( 1, 0, kSchedulerTool, GatewayScheduler::AFFINITY_UNABLE );

    GatewayScheduler scheduler;
    scheduler.SetWorkerSlots( 1, 4 );
    scheduler.SetWorkerSlots( 2, 1 );
    scheduler.AddJob( 1, 1, 0, kSchedulerTool );
    scheduler.AddJob( 2, 1, 0, kSchedulerTool );
    scheduler.AddJob( 3, 1, 0, otherTool );

    TEST_ASSERT( Starts( scheduler, affinity, 1, 2 ) );  // Not worker 1, for all its free slots
    TEST_ASSERT( Starts( scheduler, affinity, 3, 1 ) );  // Job 2 has no free worker able to take it
    TEST_ASSERT( StartsNothing( scheduler, affinity ) );
    TEST_ASSERT( scheduler.GetNumWaiting() == 1 );

    Complete( scheduler, 1, 2 );
    TEST_ASSERT( Starts( scheduler, affinity, 2, 2 ) );
}

// SchedulerUsesNoMoreSlotsThanAWorkerHas
//------------------------------------------------------------------------------
void TestGateway::SchedulerUsesNoMoreSlotsThanAWorkerHas() const
{
    // Worker 2 keeps a slot free throughout, for jobs it is unable to take
    FakeAffinity affinity;
    affinity.Set( 2, 0, kSchedulerTool, GatewayScheduler::AFFINITY_UNABLE );
    GatewayScheduler scheduler;
    scheduler.SetWorkerSlots( 1, 2 );
    scheduler.SetWorkerSlots( 2, 1 );
    TEST_ASSERT( scheduler.GetTotalSlots() == 3 );
    for ( uint32_t jobId = 1; jobId <= 6; ++jobId )
    {
        scheduler.AddJob( jobId, 1, 0, kSchedulerTool );
    }
    TEST_ASSERT( Starts( scheduler, affinity, 1, 1 ) );
    TEST_ASSERT( Starts( scheduler, affinity, 2, 1 ) );
    TEST_ASSERT( StartsNothing( scheduler, affinity ) );

    // Fewer slots than running: nothing new until it is below
    scheduler.SetWorkerSlots( 1, 1 );
    TEST_ASSERT( scheduler.GetTotalSlots() == 2 );
    Complete( scheduler, 1, 1 );
    TEST_ASSERT( StartsNothing( scheduler, affinity ) );
    Complete( scheduler, 2, 1 );
    TEST_ASSERT( Starts( scheduler, affinity, 3, 1 ) );
    TEST_ASSERT( StartsNothing( scheduler, affinity ) );

    scheduler.SetWorkerSlots( 1, 3 );
    TEST_ASSERT( Starts( scheduler, affinity, 4, 1 ) );
    TEST_ASSERT( Starts( scheduler, affinity, 5, 1 ) );
    TEST_ASSERT( StartsNothing( scheduler, affinity ) );
    TEST_ASSERT( scheduler.GetNumRunning() == 3 );
    TEST_ASSERT( scheduler.GetNumWaiting() == 1 );
}

// FailedJobIsRetriedElsewhereAtMostTwice
//------------------------------------------------------------------------------
void TestGateway::FailedJobIsRetriedElsewhereAtMostTwice() const
{
    const FakeAffinity affinity;
    GatewayScheduler scheduler;
    for ( uint32_t workerId = 1; workerId <= 3; ++workerId )
    {
        scheduler.SetWorkerSlots( workerId, 1 );
    }
    scheduler.AddJob( 1, 1, 0, kSchedulerTool );

    // Each attempt avoids the workers that failed it, though the tie-break favours them
    TEST_ASSERT( Starts( scheduler, affinity, 1, 1 ) );
    TEST_ASSERT( scheduler.OnFailed( 1, 1 ) == GatewayScheduler::OUTCOME_RETRY );
    scheduler.ReleaseSlot( 1 );
    TEST_ASSERT( Starts( scheduler, affinity, 1, 2 ) );
    TEST_ASSERT( scheduler.OnFailed( 1, 2 ) == GatewayScheduler::OUTCOME_RETRY );
    scheduler.ReleaseSlot( 2 );
    TEST_ASSERT( Starts( scheduler, affinity, 1, 3 ) );
    TEST_ASSERT( scheduler.OnFailed( 1, 3 ) == GatewayScheduler::OUTCOME_GIVE_UP );
    scheduler.ReleaseSlot( 3 );

    TEST_ASSERT( scheduler.GetNumWaiting() == 0 );
    TEST_ASSERT( scheduler.GetNumRunning() == 0 );
    TEST_ASSERT( StartsNothing( scheduler, affinity ) );
}

// RetryUsesTheSameWorkerWhenThereIsNoOther
//------------------------------------------------------------------------------
void TestGateway::RetryUsesTheSameWorkerWhenThereIsNoOther() const
{
    // Worker 2 is unable to take the job
    FakeAffinity affinity;
    affinity.Set( 2, 0, kSchedulerTool, GatewayScheduler::AFFINITY_UNABLE );
    GatewayScheduler scheduler;
    scheduler.SetWorkerSlots( 1, 1 );
    scheduler.SetWorkerSlots( 2, 1 );
    scheduler.AddJob( 1, 1, 0, kSchedulerTool );

    TEST_ASSERT( Starts( scheduler, affinity, 1, 1 ) );
    TEST_ASSERT( scheduler.OnFailed( 1, 1 ) == GatewayScheduler::OUTCOME_RETRY );
    scheduler.ReleaseSlot( 1 );
    TEST_ASSERT( Starts( scheduler, affinity, 1, 1 ) );
    TEST_ASSERT( scheduler.OnFailed( 1, 1 ) == GatewayScheduler::OUTCOME_RETRY );
    scheduler.ReleaseSlot( 1 );
    TEST_ASSERT( Starts( scheduler, affinity, 1, 1 ) );
    TEST_ASSERT( scheduler.OnFailed( 1, 1 ) == GatewayScheduler::OUTCOME_GIVE_UP );
}

// RetryWaitsForAConnectedWorkerWithoutSlots
//------------------------------------------------------------------------------
void TestGateway::RetryWaitsForAConnectedWorkerWithoutSlots() const
{
    // Worker 2 is connected and idle, with no slots yet (no demand, no allowance)
    const FakeAffinity affinity;
    GatewayScheduler scheduler;
    scheduler.SetWorkerSlots( 1, 1 );
    scheduler.SetWorkerSlots( 2, 0 );
    scheduler.AddJob( 1, 1, 0, kSchedulerTool );

    TEST_ASSERT( Starts( scheduler, affinity, 1, 1 ) );
    TEST_ASSERT( scheduler.OnFailed( 1, 1 ) == GatewayScheduler::OUTCOME_RETRY );
    scheduler.ReleaseSlot( 1 );

    // Not back on worker 1; worker 2's demand counts the job, and it runs there once it has a slot
    TEST_ASSERT( StartsNothing( scheduler, affinity ) );
    TEST_ASSERT( scheduler.GetNumWaitingFor( 1, affinity ) == 0 );
    TEST_ASSERT( scheduler.GetNumWaitingFor( 2, affinity ) == 1 );
    scheduler.SetWorkerSlots( 2, 1 );
    TEST_ASSERT( Starts( scheduler, affinity, 1, 2 ) );
}

// RetriedJobGoesFirst
//------------------------------------------------------------------------------
void TestGateway::RetriedJobGoesFirst() const
{
    const FakeAffinity affinity;
    GatewayScheduler scheduler;
    scheduler.SetWorkerSlots( 1, 1 );
    scheduler.SetWorkerSlots( 2, 1 );
    for ( uint32_t jobId = 1; jobId <= 3; ++jobId )
    {
        scheduler.AddJob( jobId, 1, 0, kSchedulerTool );
    }
    TEST_ASSERT( Starts( scheduler, affinity, 1, 1 ) );
    TEST_ASSERT( Starts( scheduler, affinity, 2, 2 ) );
    TEST_ASSERT( scheduler.OnFailed( 1, 1 ) == GatewayScheduler::OUTCOME_RETRY );
    scheduler.ReleaseSlot( 1 );
    Complete( scheduler, 2, 2 );

    // Job 1 is ahead of job 3 again, and goes to the worker that did not fail it
    TEST_ASSERT( Starts( scheduler, affinity, 1, 2 ) );
    TEST_ASSERT( Starts( scheduler, affinity, 3, 1 ) );
}

// RemovedWorkerReportsItsRunningJobs
//------------------------------------------------------------------------------
void TestGateway::RemovedWorkerReportsItsRunningJobs() const
{
    const FakeAffinity affinity;
    GatewayScheduler scheduler;
    scheduler.SetWorkerSlots( 1, 2 );
    scheduler.SetWorkerSlots( 2, 1 );
    for ( uint32_t jobId = 1; jobId <= 4; ++jobId )
    {
        scheduler.AddJob( jobId, 1, 0, kSchedulerTool );
    }
    TEST_ASSERT( Starts( scheduler, affinity, 1, 1 ) );
    TEST_ASSERT( Starts( scheduler, affinity, 2, 1 ) );
    TEST_ASSERT( Starts( scheduler, affinity, 3, 2 ) );

    Array<uint32_t> running;
    scheduler.RemoveWorker( 1, running );
    TEST_ASSERT( running.GetSize() == 2 );
    TEST_ASSERT( running.Find( 1u ) && running.Find( 2u ) );
    TEST_ASSERT( scheduler.GetTotalSlots() == 1 );
    TEST_ASSERT( scheduler.GetNumRunning() == 3 );  // Until each one fails

    TEST_ASSERT( scheduler.OnFailed( 1, 1 ) == GatewayScheduler::OUTCOME_RETRY );
    TEST_ASSERT( scheduler.OnFailed( 2, 1 ) == GatewayScheduler::OUTCOME_RETRY );
    TEST_ASSERT( StartsNothing( scheduler, affinity ) );
    Complete( scheduler, 3, 2 );
    TEST_ASSERT( Starts( scheduler, affinity, 1, 2 ) );

    // The worker connects again with none of its old work, and job 2 still avoids it
    scheduler.SetWorkerSlots( 1, 1 );
    TEST_ASSERT( Starts( scheduler, affinity, 4, 1 ) );
    TEST_ASSERT( StartsNothing( scheduler, affinity ) );
    TEST_ASSERT( scheduler.GetNumWaiting() == 1 );
}

// RemovedClientDropsOnlyItsWaitingJobs
//------------------------------------------------------------------------------
void TestGateway::RemovedClientDropsOnlyItsWaitingJobs() const
{
    const FakeAffinity affinity;
    GatewayScheduler scheduler;
    scheduler.SetWorkerSlots( 1, 2 );
    scheduler.AddJob( 1, 1, 0, kSchedulerTool );
    scheduler.AddJob( 2, 1, 0, kSchedulerTool );
    scheduler.AddJob( 3, 1, 0, kSchedulerTool );
    scheduler.AddJob( 4, 2, 0, kSchedulerTool );
    scheduler.AddJob( 5, 2, 0, kSchedulerTool );
    scheduler.AddJob( 6, 3, 0, kSchedulerTool );
    TEST_ASSERT( Starts( scheduler, affinity, 1, 1 ) );
    TEST_ASSERT( Starts( scheduler, affinity, 4, 1 ) );

    Array<uint32_t> waiting;
    scheduler.RemoveClient( 1, waiting );
    TEST_ASSERT( waiting.GetSize() == 2 );
    TEST_ASSERT( waiting.Find( 2u ) && waiting.Find( 3u ) );
    TEST_ASSERT( scheduler.GetNumWaiting() == 2 );
    TEST_ASSERT( scheduler.GetNumRunning() == 2 );

    // The turn stays with the client after the last one served
    Complete( scheduler, 4, 1 );
    TEST_ASSERT( Starts( scheduler, affinity, 6, 1 ) );
    TEST_ASSERT( StartsNothing( scheduler, affinity ) );

    // A running job of a client that has gone is not retried
    TEST_ASSERT( scheduler.OnFailed( 1, 1 ) == GatewayScheduler::OUTCOME_GIVE_UP );
    scheduler.ReleaseSlot( 1 );
    TEST_ASSERT( Starts( scheduler, affinity, 5, 1 ) );
    TEST_ASSERT( scheduler.GetNumWaiting() == 0 );
    TEST_ASSERT( scheduler.GetNumRunning() == 2 );
}

// WaitingForCountsOnlyJobsAWorkerCanTake
//------------------------------------------------------------------------------
void TestGateway::WaitingForCountsOnlyJobsAWorkerCanTake() const
{
    // Worker 1 is able to take jobs for one tool only
    const uint64_t otherTool = 0x7002;
    FakeAffinity affinity;
    affinity.Set( 1, 0, kSchedulerTool, GatewayScheduler::AFFINITY_NONE );
    affinity.Set( 1, 0, 0, GatewayScheduler::AFFINITY_UNABLE );
    GatewayScheduler scheduler;
    scheduler.SetWorkerSlots( 1, 1 );
    scheduler.SetWorkerSlots( 2, 1 );
    scheduler.AddJob( 1, 1, 0, kSchedulerTool );
    scheduler.AddJob( 2, 1, 0, otherTool );
    scheduler.AddJob( 3, 2, 0, otherTool );
    scheduler.AddJob( 4, 2, 0, kSchedulerTool );
    TEST_ASSERT( scheduler.GetNumWaitingFor( 1, affinity ) == 2 );
    TEST_ASSERT( scheduler.GetNumWaitingFor( 2, affinity ) == 4 );

    // Not a running job
    TEST_ASSERT( Starts( scheduler, affinity, 1, 1 ) );
    TEST_ASSERT( scheduler.GetNumWaitingFor( 1, affinity ) == 1 );
    TEST_ASSERT( scheduler.GetNumWaitingFor( 2, affinity ) == 3 );

    // Nor one the worker failed, while another able worker has not
    TEST_ASSERT( scheduler.OnFailed( 1, 1 ) == GatewayScheduler::OUTCOME_RETRY );
    scheduler.ReleaseSlot( 1 );
    TEST_ASSERT( scheduler.GetNumWaitingFor( 1, affinity ) == 1 );
    TEST_ASSERT( scheduler.GetNumWaitingFor( 2, affinity ) == 4 );
}

// GatewayConfigReadsHostsAndPorts
//------------------------------------------------------------------------------
void TestGateway::GatewayConfigReadsHostsAndPorts() const
{
    const char * const text = "# The site's workers\r\n"
                              "alpha\n"
                              "\n"
                              "  beta:4000   ; the fast one\n"
                              "gamma.local:31264#\n"
                              "beta:4001\n";
    GatewayConfig config;
    AStackString<> error;
    TEST_ASSERT( GatewayConfig::Parse( AStackString<>( text ), config, error ) );
    TEST_ASSERT( config.m_Workers.GetSize() == 4 );
    TEST_ASSERT( config.m_Workers[ 0 ].m_Host == "alpha" );
    TEST_ASSERT( config.m_Workers[ 0 ].m_Port == Protocol::kPort );
    TEST_ASSERT( config.m_Workers[ 1 ].m_Host == "beta" );
    TEST_ASSERT( config.m_Workers[ 1 ].m_Port == 4000 );
    TEST_ASSERT( config.m_Workers[ 2 ].m_Host == "gamma.local" );
    TEST_ASSERT( config.m_Workers[ 2 ].m_Port == 31264 );
    TEST_ASSERT( config.m_Workers[ 3 ].m_Host == "beta" );
    TEST_ASSERT( config.m_Workers[ 3 ].m_Port == 4001 );
    TEST_ASSERT( config.m_CacheDir.IsEmpty() );
    TEST_ASSERT( config.m_JobTimeoutSeconds == 600.0f );
    TEST_ASSERT( config.m_ReconnectSeconds == 10.0f );
    TEST_ASSERT( config.m_StatsSeconds == 60.0f );

    // A list with no worker is read; the gateway then refuses its clients
    TEST_ASSERT( GatewayConfig::Parse( AStackString<>( "; none yet\n\n" ), config, error ) );
    TEST_ASSERT( config.m_Workers.IsEmpty() );

    // Each error names its line
    struct Bad
    {
        const char * m_Text;
        const char * m_Line;
    };
    const Bad bad[] = {
        { "alpha\nbeta\nALPHA\n", "line 3" },   // The same worker twice
        { "alpha\nalpha:31264\n", "line 2" },   // Also the same worker, on the default port
        { ":4000\n", "line 1" },
        { "alpha:0\n", "line 1" },
        { "alpha:65536\n", "line 1" },
        { "alpha:\n", "line 1" },
        { "alpha:12x\n", "line 1" },
    };
    for ( const Bad & b : bad )
    {
        error.Clear();
        TEST_ASSERT( GatewayConfig::Parse( AStackString<>( b.m_Text ), config, error ) == false );
        TEST_ASSERT( error.Find( b.m_Line ) );
    }

    // From a file
    AStackString<> path;
    VERIFY( FBuild::GetTempDir( path ) );
    path += "gateway_workers.txt";
    {
        FileStream f;
        TEST_ASSERT( f.Open( path.Get(), FileStream::WRITE_ONLY ) );
        TEST_ASSERT( f.Write( text, strlen( text ) ) == strlen( text ) );
    }
    GatewayConfig loaded;
    TEST_ASSERT( GatewayConfig::Load( path, loaded, error ) );
    TEST_ASSERT( loaded.m_Workers.GetSize() == 4 );
    TEST_ASSERT( loaded.m_Workers[ 1 ].m_Host == "beta" );
    TEST_ASSERT( loaded.m_Workers[ 1 ].m_Port == 4000 );

    path += ".missing";
    error.Clear();
    TEST_ASSERT( GatewayConfig::Load( path, loaded, error ) == false );
    TEST_ASSERT( error.Find( path.Get() ) );
}

// Downstream connections
//------------------------------------------------------------------------------
namespace
{
    constexpr uint64_t kDownstreamTool = 0x7101;

    // A worker that answers each job as it is told to. Its results have the layout of
    // Server::FinalizeCompletedJobs and are named by the job's name field (MakeJob's marker).
    class EchoWorker : public DistributedTest::FakeWorker
    {
    public:
        enum Answer
        {
            ANSWER_OK,
            ANSWER_COMPILE_FAILURE,
            ANSWER_SYSTEM_ERROR,
            ANSWER_NEVER,       // Held until AnswerHeldJobs
            ANSWER_DISCONNECT,  // The connection is closed when a job arrives
        };
        struct ReceivedJob
        {
            uint32_t    m_JobId = 0;
            uint64_t    m_PchId = 0;
            uint64_t    m_ToolId = 0;
            AString     m_Name;
        };

        explicit EchoWorker( uint32_t slots,
                             Answer answer = ANSWER_OK,
                             const Array<uint64_t> & pchInventory = Array<uint64_t>(),
                             const Array<uint64_t> & toolchainInventory = Array<uint64_t>(),
                             uint16_t threadIndex = 1001 )
            : FakeWorker( slots, toolchainInventory, pchInventory )
            , m_ThreadIndex( threadIndex )
            , m_Answer( answer )
            , m_HeldPchs( pchInventory )
        {
        }
        virtual ~EchoWorker() override
        {
            StallReading( false );
            ShutdownAllConnections();
        }
        explicit EchoWorker( const EchoWorker & other ) = delete;
        EchoWorker & operator=( const EchoWorker & other ) = delete;

        void GetJobs( Array<ReceivedJob> & out ) const { MutexHolder mh( m_Mutex ); out = m_Jobs; }
        size_t GetNumJobs() const { MutexHolder mh( m_Mutex ); return m_Jobs.GetSize(); }
        void GetMessageTypes( Array<uint8_t> & out ) const { MutexHolder mh( m_Mutex ); out = m_Types; } // After the handshake, in order
        uint64_t GetPchBytes() const { MutexHolder mh( m_Mutex ); return m_PchBytes; }
        uint32_t GetNumPchCopies() const { MutexHolder mh( m_Mutex ); return m_NumPchCopies; } // Last chunks received
        void SetAnswer( Answer answer ) { MutexHolder mh( m_Mutex ); m_Answer = answer; }

        // As a real Server does (Server::Process( MsgJob )): a job whose PCH is neither held nor being received
        // makes the worker ask for it, once until a copy arrives
        void AskForMissingPchs() { MutexHolder mh( m_Mutex ); m_AsksForMissingPchs = true; }
        uint32_t GetNumPchAsks() const { MutexHolder mh( m_Mutex ); return m_NumPchAsks; }

        void AnswerHeldJobs( Answer answer = ANSWER_OK )
        {
            Array<HeldJob> held;
            {
                MutexHolder mh( m_Mutex );
                held.Swap( m_Held );
            }
            for ( const HeldJob & job : held )
            {
                SendResult( job.m_Connection, job.m_JobId, job.m_Name, answer );
            }
        }

        // While set, what arrives is not read, so what the gateway sends backs up
        void StallReading( bool stall )
        {
            m_StallReading.Store( stall );
            if ( stall == false )
            {
                m_StallSemaphore.Signal();
            }
        }

        // As a worker that has lost a PCH a job waits for
        bool AskForPch( uint64_t pchId )
        {
            const ConnectionInfo * connection = nullptr;
            {
                MutexHolder mh( m_Mutex );
                connection = m_Connection;
            }
            return connection && Protocol::MsgPchNeeded( pchId ).Send( connection );
        }

    protected:
        virtual void Handshake( const ConnectionInfo * connection ) override
        {
            {
                MutexHolder mh( m_Mutex );
                m_Connection = connection;
            }
            m_Decoder.Replace( FNEW( JobStreamDecoder ) ); // A new connection starts a new job stream
            FakeWorker::Handshake( connection );
        }

    private:
        struct HeldJob
        {
            const ConnectionInfo *  m_Connection = nullptr;
            uint32_t                m_JobId = 0;
            AString                 m_Name;
        };

        virtual void OnMessage( const ConnectionInfo * connection, const Protocol::IMessage & message, void * payload, uint32_t payloadSize ) override
        {
            while ( m_StallReading.Load() )
            {
                m_StallSemaphore.Wait( 10 );
            }

            {
                MutexHolder mh( m_Mutex );
                m_Types.Append( (uint8_t)message.GetType() );
            }
            if ( message.GetType() == Protocol::MSG_PCH_FILE )
            {
                const Protocol::MsgPchFile & msg = static_cast<const Protocol::MsgPchFile &>( message );
                const bool isLast = ( ( msg.GetChunkIndex() + 1 ) == Protocol::MsgPchFile::GetNumChunks( msg.GetCompressedSize(), msg.GetChunkSize() ) );
                MutexHolder mh( m_Mutex );
                m_PchBytes += payloadSize;
                m_NumPchCopies += isLast ? 1 : 0;
                if ( isLast )
                {
                    m_ReceivingPchs.FindAndErase( msg.GetPchId() );
                    m_AskedPchs.FindAndErase( msg.GetPchId() );
                    if ( m_HeldPchs.Find( msg.GetPchId() ) == nullptr )
                    {
                        m_HeldPchs.Append( msg.GetPchId() );
                    }
                }
                else if ( m_ReceivingPchs.Find( msg.GetPchId() ) == nullptr )
                {
                    m_ReceivingPchs.Append( msg.GetPchId() );
                }
            }
            else if ( ( message.GetType() == Protocol::MSG_JOB ) && payload )
            {
                OnJob( connection, static_cast<const Protocol::MsgJob &>( message ), payload, payloadSize );
            }
        }

        void OnJob( const ConnectionInfo * connection, const Protocol::MsgJob & msg, const void * payload, uint32_t payloadSize )
        {
            void * job = nullptr;
            size_t jobSize = 0;
            if ( m_Decoder.Get()->Decode( payload, payloadSize, job, jobSize ) == false )
            {
                return;
            }
            ReceivedJob received;
            received.m_ToolId = msg.GetToolId();
            const bool peeked = GatewayWire::PeekJob( job, jobSize, received.m_JobId, received.m_PchId );
            ConstMemoryStream ms( job, jobSize );
            uint32_t jobId = 0;
            const bool named = peeked && ms.Read( jobId ) && ms.Read( received.m_Name );
            FREE( job );
            if ( named == false )
            {
                return;
            }

            Answer answer;
            bool askForPch = false;
            {
                MutexHolder mh( m_Mutex );
                m_Jobs.Append( received );
                answer = m_Answer;
                const uint64_t pchId = received.m_PchId;
                if ( m_AsksForMissingPchs && ( pchId != 0 ) && ( m_HeldPchs.Find( pchId ) == nullptr ) &&
                     ( m_ReceivingPchs.Find( pchId ) == nullptr ) && ( m_AskedPchs.Find( pchId ) == nullptr ) )
                {
                    m_AskedPchs.Append( pchId );
                    ++m_NumPchAsks;
                    askForPch = true;
                }
                if ( answer == ANSWER_NEVER )
                {
                    HeldJob & held = m_Held.EmplaceBack();
                    held.m_Connection = connection;
                    held.m_JobId = received.m_JobId;
                    held.m_Name = received.m_Name;
                }
            }
            if ( askForPch )
            {
                Protocol::MsgPchNeeded( received.m_PchId ).Send( connection );
            }
            if ( answer == ANSWER_DISCONNECT )
            {
                Disconnect( connection );
            }
            else if ( answer != ANSWER_NEVER )
            {
                SendResult( connection, received.m_JobId, received.m_Name, answer );
            }
        }

        void SendResult( const ConnectionInfo * connection, uint32_t jobId, const AString & name, Answer answer )
        {
            const bool ok = ( answer == ANSWER_OK );
            Array<AString> messages;
            if ( ok == false )
            {
                messages.EmplaceBack( "echo worker failure" );
            }
            MemoryStream ms;
            ms.Write( jobId );
            ms.Write( name );
            ms.Write( ok );
            ms.Write( answer == ANSWER_SYSTEM_ERROR );
            ms.Write( messages );
            ms.Write( (uint32_t)1 ); // Build time
            ms.Write( m_ThreadIndex );
            ms.Write( (uint32_t)name.GetLength() ); // The data: the name again
            ms.WriteBuffer( name.Get(), name.GetLength() );
            const Protocol::MsgJobResult result( 0 );
            result.Send( connection, ms );
        }

        const uint16_t m_ThreadIndex;
        mutable Mutex m_Mutex;
        Answer m_Answer;
        const ConnectionInfo * m_Connection = nullptr;  // The latest
        Array<ReceivedJob> m_Jobs;
        Array<HeldJob> m_Held;
        Array<uint8_t> m_Types;
        uint64_t m_PchBytes = 0;
        uint32_t m_NumPchCopies = 0;
        Array<uint64_t> m_HeldPchs;         // Advertised, or a copy arrived
        Array<uint64_t> m_ReceivingPchs;    // Some chunks arrived
        Array<uint64_t> m_AskedPchs;        // Asked for, and no copy arrived since
        bool m_AsksForMissingPchs = false;
        uint32_t m_NumPchAsks = 0;
        UniquePtr<JobStreamDecoder> m_Decoder;  // Receive thread
        Atomic<bool> m_StallReading{ false };
        Semaphore m_StallSemaphore;
    };

    // Answers the handshake with an ack this version cannot use
    class OddAckWorker : public EchoWorker
    {
    public:
        enum Reply
        {
            ACK_OF_ANOTHER_MAJOR,
            ACK_TOO_SHORT,  // The version fields and a little more
        };
        explicit OddAckWorker( Reply reply ) : EchoWorker( 4 ), m_Reply( reply ) {}
        virtual ~OddAckWorker() override = default;
        explicit OddAckWorker( const OddAckWorker & other ) = delete;
        OddAckWorker & operator=( const OddAckWorker & other ) = delete;

    private:
        virtual void Handshake( const ConnectionInfo * connection ) override
        {
            const Protocol::MsgConnectionAck ack( 4 );
            uint8_t bytes[ sizeof( ack ) ];
            memcpy( bytes, &ack, sizeof( ack ) );
            size_t size = sizeof( bytes );
            if ( m_Reply == ACK_OF_ANOTHER_MAJOR )
            {
                bytes[ sizeof( Protocol::IMessage ) + sizeof( uint16_t ) ] = (uint8_t)( Protocol::kVersionMajor + 1 ); // After the worker version
            }
            else
            {
                size = Protocol::kHandshakePrefixSize + 2;
            }
            Send( connection, bytes, size );

            // The inventories follow, as they would from a worker of this version
            SendInventory( connection, Protocol::MsgPchInventory( 0 ), Array<uint64_t>() );
            SendInventory( connection, Protocol::MsgToolchainInventory( 0 ), Array<uint64_t>() );
        }

        const Reply m_Reply;
    };

    // Plays the gateway: the PCHs and toolchains it stores, and what the connection tells it
    class RecordingOwner : public DownstreamConnection::Owner, public PchLease::Owner
    {
    public:
        RecordingOwner() = default;
        virtual ~RecordingOwner() = default;
        explicit RecordingOwner( const RecordingOwner & other ) = delete;
        RecordingOwner & operator=( const RecordingOwner & other ) = delete;

        // Before the connection starts
        void AddPch( const StoredPch & pch ) { m_Pchs.Append( pch ); }
        void AddToolchain( uint64_t toolId, uint32_t numFiles )
        {
            // The payloads as a client sent them: their content is not looked at on the way
            Toolchain & toolchain = m_Toolchains.EmplaceBack();
            toolchain.m_ToolId = toolId;
            for ( uint32_t i = 0; i <= numFiles; ++i )
            {
                const uint32_t fileId = ( i == numFiles ) ? DownstreamConnection::kManifestFileId : i;
                AString & path = toolchain.m_Paths.EmplaceBack();
                VERIFY( FBuild::GetTempDir( path ) );
                path.AppendFormat( "downstream_tool_%u_%u.bin", (uint32_t)toolId, fileId );
                char bytes[ 100 ];
                memset( bytes, (int)i, sizeof( bytes ) );
                FileStream f;
                VERIFY( f.Open( path.Get(), FileStream::WRITE_ONLY ) );
                VERIFY( f.Write( bytes, sizeof( bytes ) ) == sizeof( bytes ) );
                toolchain.m_FileIds.Append( fileId );
            }
        }
        // A lease of the kind FindPch hands out, as the gateway takes for a job it forwards
        PchLease Lease( uint64_t pchId )
        {
            m_NumLeases.Increment();
            return PchLease( this, pchId );
        }

        uint32_t GetNumReady() const { return m_NumReady.Load(); }
        uint32_t GetNumLost() const { return m_NumLost.Load(); }
        uint32_t GetNumLeases() const { return m_NumLeases.Load(); }
        uint32_t GetNumPchLookups() const { return m_NumPchLookups.Load(); }
        GatewayScheduler::Affinity GetAffinityWhenReady() const { MutexHolder mh( m_Mutex ); return m_AffinityWhenReady; }
        uint32_t GetCapacityWhenReady() const { MutexHolder mh( m_Mutex ); return m_CapacityWhenReady; }
        void GetResults( Array<GatewayWire::ResultHeader> & out ) const { MutexHolder mh( m_Mutex ); out = m_Results; }
        size_t GetNumResults() const { MutexHolder mh( m_Mutex ); return m_Results.GetSize(); }
        bool WasAnyResultCompressed() const { MutexHolder mh( m_Mutex ); return m_AnyCompressed; }

        // What the connection says of these ids when it becomes ready
        uint64_t m_ReadyPchId = 0;
        uint64_t m_ReadyToolId = 0;

    private:
        struct Toolchain
        {
            uint64_t            m_ToolId = 0;
            Array<uint32_t>     m_FileIds;
            Array<AString>      m_Paths;
        };

        virtual void OnDownstreamReady( DownstreamConnection & connection ) override
        {
            MutexHolder mh( m_Mutex );
            m_AffinityWhenReady = connection.GetAffinity( m_ReadyPchId, m_ReadyToolId );
            m_CapacityWhenReady = connection.GetCapacity();
            m_NumReady.Increment();
        }
        virtual void OnDownstreamResult( DownstreamConnection &, bool compressed, void * payload, uint32_t payloadSize ) override
        {
            GatewayWire::ResultHeader header;
            const bool read = GatewayWire::ReadResultHeader( payload, payloadSize, header );
            FREE( payload );
            MutexHolder mh( m_Mutex );
            m_AnyCompressed |= compressed;
            if ( read )
            {
                m_Results.Append( header );
            }
        }
        virtual void OnDownstreamLost( DownstreamConnection & ) override { m_NumLost.Increment(); }
        virtual bool FindPch( uint64_t pchId, StoredPch & outPch, PchLease & outLease ) override
        {
            m_NumPchLookups.Increment();
            for ( const StoredPch & pch : m_Pchs )
            {
                if ( pch.m_Id == pchId )
                {
                    outPch = pch;
                    outLease = Lease( pchId );
                    return true;
                }
            }
            return false;
        }
        virtual bool FindToolchainFile( uint64_t toolId, uint32_t fileId, AString & outPath, uint32_t & outSize ) override
        {
            for ( const Toolchain & toolchain : m_Toolchains )
            {
                const uint32_t * found = ( toolchain.m_ToolId == toolId ) ? toolchain.m_FileIds.Find( fileId ) : nullptr;
                if ( found )
                {
                    outPath = toolchain.m_Paths[ (size_t)( found - toolchain.m_FileIds.Begin() ) ];
                    outSize = 100;
                    return true;
                }
            }
            return false;
        }
        virtual uint32_t GetToolchainNumFiles( uint64_t toolId ) override
        {
            for ( const Toolchain & toolchain : m_Toolchains )
            {
                if ( toolchain.m_ToolId == toolId )
                {
                    return (uint32_t)( toolchain.m_FileIds.GetSize() - 1 ); // Not the manifest
                }
            }
            return 0;
        }
        virtual void ReleasePchLease( uint64_t ) override { m_NumLeases.Decrement(); }

        Array<StoredPch> m_Pchs;
        Array<Toolchain> m_Toolchains;
        mutable Mutex m_Mutex;
        GatewayScheduler::Affinity m_AffinityWhenReady = GatewayScheduler::AFFINITY_UNABLE;
        uint32_t m_CapacityWhenReady = 0;
        Array<GatewayWire::ResultHeader> m_Results;
        bool m_AnyCompressed = false;
        Atomic<uint32_t> m_NumReady{ 0 };
        Atomic<uint32_t> m_NumLost{ 0 };
        Atomic<uint32_t> m_NumLeases{ 0 };
        Atomic<uint32_t> m_NumPchLookups{ 0 };
    };

    GatewayConfig::Worker GetEchoWorkerAddress()
    {
        GatewayConfig::Worker worker;
        worker.m_Host = "127.0.0.1";
        worker.m_Port = kGatewayTestDownstreamPort;
        return worker;
    }

    // A stored PCH as the gateway replays it. The connection sends the file as it is, so its bytes need not be zstd.
    StoredPch WriteDownstreamPch( uint64_t pchId, uint32_t size )
    {
        StoredPch pch;
        pch.m_Id = pchId;
        pch.m_UncompressedSize = 2 * size;
        pch.m_CompressedSize = size;
        VERIFY( FBuild::GetTempDir( pch.m_File ) );
        pch.m_File.AppendFormat( "downstream_pch_%u.bin", (uint32_t)pchId );
        Array<char> bytes;
        bytes.SetSize( size );
        uint32_t x = (uint32_t)pchId;
        for ( char & c : bytes )
        {
            x = ( x * 1664525u ) + 1013904223u;
            c = (char)( x >> 24 );
        }
        FileStream f;
        VERIFY( f.Open( pch.m_File.Get(), FileStream::WRITE_ONLY ) );
        VERIFY( f.Write( bytes.Begin(), size ) == size );
        return pch;
    }

    void SendDownstreamJob( DownstreamConnection & connection, RecordingOwner & owner, uint32_t jobId, const char * marker, const StoredPch * pch, uint64_t toolId = kDownstreamTool )
    {
        MemoryStream job;
        MakeJob( jobId, marker, pch ? pch->m_Id : 0, job );
        connection.SendJob( Protocol::MsgJob( toolId, 0 ), ConstMemoryStream( Move( job ) ), pch, pch ? owner.Lease( pch->m_Id ) : PchLease() );
    }
}

// DownstreamBecomesReadyWithTheWorkerInventories
//------------------------------------------------------------------------------
void TestGateway::DownstreamBecomesReadyWithTheWorkerInventories() const
{
    const uint64_t heldPch = 0xA101;
    const uint64_t otherPch = 0xA102;
    const uint64_t otherTool = 0x7102;
    Array<uint64_t> pchs;
    pchs.Append( heldPch );
    Array<uint64_t> tools;
    tools.Append( kDownstreamTool );
    EchoWorker worker( 8, EchoWorker::ANSWER_OK, pchs, tools );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );

    RecordingOwner owner;
    owner.m_ReadyPchId = heldPch;
    owner.m_ReadyToolId = kDownstreamTool;
    {
        DownstreamConnection connection( owner, GetEchoWorkerAddress(), 3 );
        TEST_ASSERT( connection.GetIndex() == 3 );
        AStackString<> address;
        address.Format( "127.0.0.1:%u", (uint32_t)kGatewayTestDownstreamPort );
        TEST_ASSERT( connection.GetAddress() == address );
        TEST_ASSERT( connection.GetCapacity() == 0 );

        connection.Start();
        TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumReady() == 1 ); } ) );

        // Both inventories were in by then
        TEST_ASSERT( owner.GetAffinityWhenReady() == GatewayScheduler::AFFINITY_HOLDS );
        TEST_ASSERT( owner.GetCapacityWhenReady() == 8 );
        TEST_ASSERT( connection.GetCapacity() == 8 );
        TEST_ASSERT( connection.GetAffinity( heldPch, kDownstreamTool ) == GatewayScheduler::AFFINITY_HOLDS );
        TEST_ASSERT( connection.GetAffinity( 0, kDownstreamTool ) == GatewayScheduler::AFFINITY_HOLDS );
        TEST_ASSERT( connection.GetAffinity( otherPch, kDownstreamTool ) == GatewayScheduler::AFFINITY_NONE );
        TEST_ASSERT( connection.GetAffinity( heldPch, otherTool ) == GatewayScheduler::AFFINITY_NONE );
        TEST_ASSERT( connection.HasEnded() == false );
        TEST_ASSERT( owner.GetNumLost() == 0 );
    }

    // Told once, from the destructor
    TEST_ASSERT( owner.GetNumLost() == 1 );
    TEST_ASSERT( owner.GetNumReady() == 1 );
}

// DownstreamSendsToolchainPchThenJob
//------------------------------------------------------------------------------
void TestGateway::DownstreamSendsToolchainPchThenJob() const
{
    EchoWorker worker( 4 );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );

    // More chunks than the socket buffers hold, so a stalled worker keeps the PCH on its way
    const uint32_t pchSize = ( 32 * Protocol::kPchChunkSize ) + 1000;
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( pchSize, Protocol::kPchChunkSize );
    RecordingOwner owner;
    const StoredPch pch = WriteDownstreamPch( 0xB201, pchSize );
    owner.AddPch( pch );
    owner.AddToolchain( kDownstreamTool, 1 );

    DownstreamConnection connection( owner, GetEchoWorkerAddress(), 1 );
    connection.Start();
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumReady() == 1 ); } ) );
    TEST_ASSERT( connection.GetAffinity( pch.m_Id, kDownstreamTool ) == GatewayScheduler::AFFINITY_NONE );

    worker.StallReading( true );
    SendDownstreamJob( connection, owner, 5, "m5", &pch );
    TEST_ASSERT( connection.GetAffinity( pch.m_Id, kDownstreamTool ) == GatewayScheduler::AFFINITY_RECEIVING );
    TEST_ASSERT( owner.GetNumLeases() == 1 ); // Held while the chunks are queued
    worker.StallReading( false );

    TEST_ASSERT( WaitUntil( [ &worker ]() { return ( worker.GetNumPchCopies() == 1 ); } ) );
    TEST_ASSERT( connection.GetAffinity( pch.m_Id, kDownstreamTool ) == GatewayScheduler::AFFINITY_HOLDS );
    TEST_ASSERT( owner.GetNumLeases() == 0 );
    TEST_ASSERT( worker.GetPchBytes() == pchSize );
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumResults() == 1 ); } ) );

    Array<uint8_t> types;
    worker.GetMessageTypes( types );
    TEST_ASSERT( types.GetSize() == ( 3 + numChunks ) );
    TEST_ASSERT( types[ 0 ] == Protocol::MSG_MANIFEST );
    TEST_ASSERT( types[ 1 ] == Protocol::MSG_FILE );
    for ( size_t i = 2; i < ( 2 + numChunks ); ++i )
    {
        TEST_ASSERT( types[ i ] == Protocol::MSG_PCH_FILE );
    }
    TEST_ASSERT( types.Top() == Protocol::MSG_JOB );

    // What the worker holds is not sent again, and the job's lease is not kept
    SendDownstreamJob( connection, owner, 6, "m6", &pch );
    TEST_ASSERT( connection.GetAffinity( pch.m_Id, kDownstreamTool ) == GatewayScheduler::AFFINITY_HOLDS );
    TEST_ASSERT( owner.GetNumLeases() == 0 );
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumResults() == 2 ); } ) );
    Thread::Sleep( 100 ); // Anything sent after the job would have arrived by now
    worker.GetMessageTypes( types );
    TEST_ASSERT( types.GetSize() == ( 4 + numChunks ) );
    TEST_ASSERT( types.Top() == Protocol::MSG_JOB );
    TEST_ASSERT( worker.GetNumPchCopies() == 1 );

    Array<EchoWorker::ReceivedJob> jobs;
    worker.GetJobs( jobs );
    TEST_ASSERT( jobs.GetSize() == 2 );
    TEST_ASSERT( ( jobs[ 0 ].m_JobId == 5 ) && ( jobs[ 0 ].m_PchId == pch.m_Id ) && ( jobs[ 0 ].m_ToolId == kDownstreamTool ) );
    TEST_ASSERT( ( jobs[ 1 ].m_JobId == 6 ) && ( jobs[ 1 ].m_Name == "m6" ) );
}

// DownstreamSendsAJobOnlyBehindItsPch
//------------------------------------------------------------------------------
void TestGateway::DownstreamSendsAJobOnlyBehindItsPch() const
{
    // A PCH smaller than the socket buffers is handed to the socket at once. A job that reached the
    // worker ahead of it would make the worker ask for it, and it would be sent twice.
    Array<uint64_t> tools;
    tools.Append( kDownstreamTool );
    EchoWorker worker( 4, EchoWorker::ANSWER_OK, Array<uint64_t>(), tools );
    worker.AskForMissingPchs();
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    RecordingOwner owner;
    const StoredPch small = WriteDownstreamPch( 0xB501, 5000 );
    const StoredPch large = WriteDownstreamPch( 0xB502, ( 3 * Protocol::kPchChunkSize ) + 1000 );
    owner.AddPch( small );
    owner.AddPch( large );
    DownstreamConnection connection( owner, GetEchoWorkerAddress(), 1 );
    connection.Start();
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumReady() == 1 ); } ) );

    // Two jobs for each, the second sent while the PCH is on its way
    SendDownstreamJob( connection, owner, 5, "m5", &small );
    SendDownstreamJob( connection, owner, 6, "m6", &small );
    SendDownstreamJob( connection, owner, 7, "m7", &large );
    SendDownstreamJob( connection, owner, 8, "m8", &large );
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumResults() == 4 ); } ) );
    Thread::Sleep( 100 ); // A copy sent again would have arrived by now

    TEST_ASSERT( worker.GetNumPchAsks() == 0 );
    TEST_ASSERT( worker.GetNumPchCopies() == 2 );
    TEST_ASSERT( owner.GetNumPchLookups() == 0 );
    TEST_ASSERT( owner.GetNumLeases() == 0 );
    TEST_ASSERT( connection.HasEnded() == false );
}

// DownstreamResultIsHandedOver
//------------------------------------------------------------------------------
void TestGateway::DownstreamResultIsHandedOver() const
{
    Array<uint64_t> tools;
    tools.Append( kDownstreamTool );
    EchoWorker worker( 4, EchoWorker::ANSWER_OK, Array<uint64_t>(), tools, 1007 );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    RecordingOwner owner;
    DownstreamConnection connection( owner, GetEchoWorkerAddress(), 1 );
    connection.Start();
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumReady() == 1 ); } ) );

    SendDownstreamJob( connection, owner, 9, "m9", nullptr );
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumResults() == 1 ); } ) );

    // A failure the worker reports is the owner's to judge
    worker.SetAnswer( EchoWorker::ANSWER_SYSTEM_ERROR );
    SendDownstreamJob( connection, owner, 10, "m10", nullptr );
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumResults() == 2 ); } ) );

    Array<GatewayWire::ResultHeader> results;
    owner.GetResults( results );
    TEST_ASSERT( ( results[ 0 ].m_JobId == 9 ) && ( results[ 0 ].m_Name == "m9" ) );
    TEST_ASSERT( results[ 0 ].m_Ok && ( results[ 0 ].m_SystemError == false ) );
    TEST_ASSERT( results[ 0 ].m_ThreadIndex == 1007 );
    TEST_ASSERT( ( results[ 1 ].m_JobId == 10 ) && ( results[ 1 ].m_Name == "m10" ) );
    TEST_ASSERT( ( results[ 1 ].m_Ok == false ) && results[ 1 ].m_SystemError );
    TEST_ASSERT( owner.WasAnyResultCompressed() == false );
    TEST_ASSERT( connection.HasEnded() == false );
    TEST_ASSERT( owner.GetNumLost() == 0 );

    // The worker had the toolchain, so only the jobs were sent
    Array<uint8_t> types;
    worker.GetMessageTypes( types );
    TEST_ASSERT( types.GetSize() == 2 );
    TEST_ASSERT( ( types[ 0 ] == Protocol::MSG_JOB ) && ( types[ 1 ] == Protocol::MSG_JOB ) );
}

// DownstreamAskingForAPchGetsItAgain
//------------------------------------------------------------------------------
void TestGateway::DownstreamAskingForAPchGetsItAgain() const
{
    Array<uint64_t> tools;
    tools.Append( kDownstreamTool );
    EchoWorker worker( 4, EchoWorker::ANSWER_OK, Array<uint64_t>(), tools );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    RecordingOwner owner;
    const StoredPch pch = WriteDownstreamPch( 0xB301, 5000 );
    owner.AddPch( pch );
    DownstreamConnection connection( owner, GetEchoWorkerAddress(), 1 );
    connection.Start();
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumReady() == 1 ); } ) );

    SendDownstreamJob( connection, owner, 5, "m5", &pch );
    TEST_ASSERT( WaitUntil( [ &worker ]() { return ( worker.GetNumPchCopies() == 1 ); } ) );

    TEST_ASSERT( worker.AskForPch( pch.m_Id ) );
    TEST_ASSERT( WaitUntil( [ &worker ]() { return ( worker.GetNumPchCopies() == 2 ); } ) );
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumLeases() == 0 ); } ) );
    TEST_ASSERT( owner.GetNumPchLookups() == 1 );
    TEST_ASSERT( worker.GetPchBytes() == ( 2 * 5000 ) );
    TEST_ASSERT( connection.GetAffinity( pch.m_Id, kDownstreamTool ) == GatewayScheduler::AFFINITY_HOLDS );
    TEST_ASSERT( connection.HasEnded() == false );
    TEST_ASSERT( owner.GetNumLost() == 0 );
}

// DownstreamAskingTooOftenIsClosed
//------------------------------------------------------------------------------
void TestGateway::DownstreamAskingTooOftenIsClosed() const
{
    Array<uint64_t> tools;
    tools.Append( kDownstreamTool );
    EchoWorker worker( 4, EchoWorker::ANSWER_OK, Array<uint64_t>(), tools );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    RecordingOwner owner;
    const StoredPch pch = WriteDownstreamPch( 0xB401, 5000 );
    owner.AddPch( pch );
    DownstreamConnection connection( owner, GetEchoWorkerAddress(), 1 );
    connection.Start();
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumReady() == 1 ); } ) );

    SendDownstreamJob( connection, owner, 5, "m5", &pch );
    TEST_ASSERT( WaitUntil( [ &worker ]() { return ( worker.GetNumPchCopies() == 1 ); } ) );

    // Each copy is asked for once it has arrived, so none is still on its way
    for ( uint32_t i = 1; i <= PchSendState::kMaxResends; ++i )
    {
        TEST_ASSERT( worker.AskForPch( pch.m_Id ) );
        TEST_ASSERT( WaitUntil( [ &worker, i ]() { return ( worker.GetNumPchCopies() == ( i + 1 ) ); } ) );
    }
    TEST_ASSERT( connection.HasEnded() == false );

    TEST_ASSERT( worker.AskForPch( pch.m_Id ) );
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumLost() == 1 ); } ) );
    TEST_ASSERT( connection.HasEnded() );
    TEST_ASSERT( strcmp( connection.GetCloseReason(), "pch" ) == 0 );
    TEST_ASSERT( worker.GetNumPchCopies() == ( PchSendState::kMaxResends + 1 ) );
    TEST_ASSERT( owner.GetNumLeases() == 0 );
}

// DownstreamCompletingJobsMayAskForAPchAgain
//------------------------------------------------------------------------------
void TestGateway::DownstreamCompletingJobsMayAskForAPchAgain() const
{
    Array<uint64_t> tools;
    tools.Append( kDownstreamTool );
    EchoWorker worker( 4, EchoWorker::ANSWER_OK, Array<uint64_t>(), tools );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    RecordingOwner owner;
    const StoredPch pch = WriteDownstreamPch( 0xB601, 5000 );
    owner.AddPch( pch );
    DownstreamConnection connection( owner, GetEchoWorkerAddress(), 1 );
    connection.Start();
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumReady() == 1 ); } ) );

    SendDownstreamJob( connection, owner, 5, "m5", &pch );
    TEST_ASSERT( WaitUntil( [ &worker ]() { return ( worker.GetNumPchCopies() == 1 ); } ) );

    // A healthy worker that evicts a hot PCH again and again, with jobs for it completing in between
    const uint32_t numAsks = ( 2 * PchSendState::kMaxResends ) + 1;
    for ( uint32_t i = 1; i <= numAsks; ++i )
    {
        TEST_ASSERT( worker.AskForPch( pch.m_Id ) );
        TEST_ASSERT( WaitUntil( [ &worker, i ]() { return ( worker.GetNumPchCopies() == ( i + 1 ) ); } ) );
        connection.OnJobCompleted( pch.m_Id );
    }
    TEST_ASSERT( connection.HasEnded() == false );
    TEST_ASSERT( owner.GetNumLost() == 0 );
    TEST_ASSERT( owner.GetNumPchLookups() == numAsks );
}

// DownstreamAskingForAnUnknownPchIsClosed
//------------------------------------------------------------------------------
void TestGateway::DownstreamAskingForAnUnknownPchIsClosed() const
{
    EchoWorker worker( 4 );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    RecordingOwner owner;
    DownstreamConnection connection( owner, GetEchoWorkerAddress(), 1 );
    connection.Start();
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumReady() == 1 ); } ) );

    TEST_ASSERT( worker.AskForPch( 0xDEAD0002 ) );
    TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumLost() == 1 ); } ) );
    TEST_ASSERT( connection.HasEnded() );
    TEST_ASSERT( strcmp( connection.GetCloseReason(), "pch" ) == 0 );
    TEST_ASSERT( owner.GetNumPchLookups() == 1 );
    TEST_ASSERT( worker.GetNumPchCopies() == 0 );
}

// DownstreamOfAnotherMajorIsLost
//------------------------------------------------------------------------------
void TestGateway::DownstreamOfAnotherMajorIsLost() const
{
    const OddAckWorker::Reply replies[] = { OddAckWorker::ACK_OF_ANOTHER_MAJOR, OddAckWorker::ACK_TOO_SHORT };
    for ( const OddAckWorker::Reply reply : replies )
    {
        OddAckWorker worker( reply );
        TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
        RecordingOwner owner;
        DownstreamConnection connection( owner, GetEchoWorkerAddress(), 1 );
        connection.Start();
        TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumLost() == 1 ); } ) );
        TEST_ASSERT( connection.HasEnded() );
        TEST_ASSERT( strcmp( connection.GetCloseReason(), "handshake" ) == 0 );
        TEST_ASSERT( owner.GetNumReady() == 0 );
        TEST_ASSERT( connection.GetCapacity() == 0 );
    }
}

// UnreachableDownstreamIsLost
//------------------------------------------------------------------------------
void TestGateway::UnreachableDownstreamIsLost() const
{
    RecordingOwner owner;
    {
        DownstreamConnection connection( owner, GetEchoWorkerAddress(), 1 ); // Nothing listens there
        connection.Start();
        TEST_ASSERT( WaitUntil( [ &owner ]() { return ( owner.GetNumLost() == 1 ); } ) );
        TEST_ASSERT( connection.HasEnded() );
        TEST_ASSERT( connection.GetCloseReason() == nullptr );
        TEST_ASSERT( owner.GetNumReady() == 0 );
    }
    TEST_ASSERT( owner.GetNumLost() == 1 );

    // One that was never started is lost when it is destroyed
    {
        DownstreamConnection connection( owner, GetEchoWorkerAddress(), 2 );
    }
    TEST_ASSERT( owner.GetNumLost() == 2 );
}

// Gateway helpers
//------------------------------------------------------------------------------
namespace
{
    constexpr uint64_t kGatewayTool = 0x7201;

    // A toolchain's messages, as a client sends them ahead of its jobs
    bool SendToolchainManifest( DistributedTest::HandshakeProbe & probe, const ConnectionInfo * ci, uint64_t toolId, uint32_t numFiles )
    {
        MemoryStream manifest;
        MakeStashManifest( manifest, toolId, numFiles );
        return probe.SendMessage( ci, Protocol::MsgManifest( toolId ), manifest.GetData(), manifest.GetSize() );
    }
    bool SendToolchainFile( DistributedTest::HandshakeProbe & probe, const ConnectionInfo * ci, uint64_t toolId, uint32_t fileId )
    {
        Array<char> bytes;
        FillStashFile( toolId, fileId, bytes );
        return probe.SendMessage( ci, Protocol::MsgFile( toolId, fileId ), bytes.Begin(), bytes.GetSize() );
    }
    bool SendToolchain( DistributedTest::HandshakeProbe & probe, const ConnectionInfo * ci, uint64_t toolId = kGatewayTool )
    {
        return SendToolchainManifest( probe, ci, toolId, 1 ) && SendToolchainFile( probe, ci, toolId, 0 );
    }

    bool SendGatewayJob( DistributedTest::HandshakeProbe & probe,
                         const ConnectionInfo * ci,
                         JobStreamEncoder & encoder,
                         uint32_t jobId,
                         const char * marker,
                         uint64_t pchId = 0,
                         uint64_t toolId = kGatewayTool )
    {
        MemoryStream job;
        MakeJob( jobId, marker, pchId, job );
        return probe.SendJob( ci, encoder, toolId, 0, job );
    }

    size_t GetNumResults( const DistributedTest::HandshakeProbe & probe )
    {
        Array<GatewayWire::ResultHeader> results;
        probe.GetResults( results );
        return results.GetSize();
    }

    size_t GetNumToolchainRequests( const DistributedTest::HandshakeProbe & probe )
    {
        Array<DistributedTest::HandshakeProbe::ToolchainRequest> requests;
        probe.GetToolchainRequests( requests );
        return requests.GetSize();
    }

    bool ReadWholeFile( const AString & path, AString & outContent )
    {
        FileStream f;
        if ( f.Open( path.Get(), FileStream::READ_ONLY ) == false )
        {
            return false;
        }
        const uint32_t size = (uint32_t)f.GetFileSize();
        outContent.SetLength( size );
        return ( f.Read( outContent.Get(), size ) == size );
    }
}

// MakeGatewayConfig
//------------------------------------------------------------------------------
/*static*/ void TestGateway::MakeGatewayConfig( uint32_t numWorkers, GatewayConfig & outConfig )
{
    GetGatewayCacheDir( outConfig.m_CacheDir );
    for ( uint32_t i = 0; i < numWorkers; ++i )
    {
        GatewayConfig::Worker & worker = outConfig.m_Workers.EmplaceBack();
        worker.m_Host = "127.0.0.1";
        worker.m_Port = (uint16_t)( kGatewayTestDownstreamPort + i );
    }
}

// GetGatewayStats
//------------------------------------------------------------------------------
/*static*/ Gateway::Stats TestGateway::GetGatewayStats( const Gateway & gateway )
{
    Gateway::Stats stats;
    gateway.GetStats( stats );
    return stats;
}

// WaitForReadyWorkers
//------------------------------------------------------------------------------
/*static*/ bool TestGateway::WaitForReadyWorkers( const Gateway & gateway, uint32_t numWorkers )
{
    return WaitUntil( [ &gateway, numWorkers ]() { return ( GetGatewayStats( gateway ).m_ReadyWorkers == numWorkers ); } );
}

// GatewayAdvertisesTheSumOfItsWorkers
//------------------------------------------------------------------------------
void TestGateway::GatewayAdvertisesTheSumOfItsWorkers() const
{
    EchoWorker w1( 8 );
    EchoWorker w2( 16 );
    TEST_ASSERT( w1.Listen( kGatewayTestDownstreamPort ) );
    TEST_ASSERT( w2.Listen( kGatewayTestDownstreamPort + 1 ) );
    GatewayConfig config;
    MakeGatewayConfig( 2, config );
    Gateway gateway( config );
    TEST_ASSERT( gateway.Listen( Protocol::kTestPort ) );
    TEST_ASSERT( WaitForReadyWorkers( gateway, 2 ) );

    const Gateway::Stats stats = GetGatewayStats( gateway );
    TEST_ASSERT( stats.m_Workers == 2 );
    TEST_ASSERT( stats.m_PoolSlots == 24 );

    DistributedTest::HandshakeProbe probe;
    TEST_ASSERT( ConnectProbe( gateway.GetServer(), probe ) != 0 );
    TEST_ASSERT( probe.GetAckCapacity() == 24 );
    probe.ShutdownAllConnections();
}

// GatewayForwardsAJobAndItsResult
//------------------------------------------------------------------------------
void TestGateway::GatewayForwardsAJobAndItsResult() const
{
    EchoWorker worker( 4 );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    GatewayConfig config;
    MakeGatewayConfig( 1, config );
    Gateway gateway( config );
    TEST_ASSERT( gateway.Listen( Protocol::kTestPort ) );
    TEST_ASSERT( WaitForReadyWorkers( gateway, 1 ) );

    DistributedTest::HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    TEST_ASSERT( ConnectProbe( gateway.GetServer(), probe, &ci ) != 0 );
    TEST_ASSERT( SendToolchain( probe, ci ) );
    JobStreamEncoder encoder;
    TEST_ASSERT( SendGatewayJob( probe, ci, encoder, 5, "m5" ) );

    // The client's id comes back, and the thread is on worker 1's lane
    Array<GatewayWire::ResultHeader> results;
    TEST_ASSERT( WaitUntil( [ &probe, &results ]() { probe.GetResults( results ); return ( results.GetSize() == 1 ); } ) );
    TEST_ASSERT( results[ 0 ].m_JobId == 5 );
    TEST_ASSERT( results[ 0 ].m_Name == "m5" );
    TEST_ASSERT( results[ 0 ].m_Ok );
    TEST_ASSERT( GatewayWire::MakeLane( 1, 1001 ) == 1257 );
    TEST_ASSERT( results[ 0 ].m_ThreadIndex == 1257 );

    Array<EchoWorker::ReceivedJob> jobs;
    worker.GetJobs( jobs );
    TEST_ASSERT( ( jobs.GetSize() == 1 ) && ( jobs[ 0 ].m_Name == "m5" ) && ( jobs[ 0 ].m_ToolId == kGatewayTool ) );
    Array<uint8_t> types;
    worker.GetMessageTypes( types );
    TEST_ASSERT( types.GetSize() == 3 );
    TEST_ASSERT( ( types[ 0 ] == Protocol::MSG_MANIFEST ) && ( types[ 1 ] == Protocol::MSG_FILE ) && ( types[ 2 ] == Protocol::MSG_JOB ) );

    // Nothing is held once the result has gone back
    TEST_ASSERT( WaitUntil( [ &gateway ]() { return ( GetGatewayStats( gateway ).m_HeldJobBytes == 0 ); } ) );
    const Gateway::Stats stats = GetGatewayStats( gateway );
    TEST_ASSERT( ( stats.m_Forwarded == 1 ) && ( stats.m_Running == 0 ) && ( stats.m_Waiting == 0 ) && ( stats.m_Parked == 0 ) );
    probe.ShutdownAllConnections();
}

// JobsWaitForTheirToolchainAtTheGateway
//------------------------------------------------------------------------------
void TestGateway::JobsWaitForTheirToolchainAtTheGateway() const
{
    EchoWorker worker( 4 );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    GatewayConfig config;
    MakeGatewayConfig( 1, config );
    Gateway gateway( config );
    TEST_ASSERT( gateway.Listen( Protocol::kTestPort ) );
    TEST_ASSERT( WaitForReadyWorkers( gateway, 1 ) );

    DistributedTest::HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    TEST_ASSERT( ConnectProbe( gateway.GetServer(), probe, &ci ) != 0 );

    // The job arrives while its toolchain is half sent
    TEST_ASSERT( SendToolchainManifest( probe, ci, kGatewayTool, 2 ) );
    TEST_ASSERT( SendToolchainFile( probe, ci, kGatewayTool, 0 ) );
    JobStreamEncoder encoder;
    TEST_ASSERT( SendGatewayJob( probe, ci, encoder, 5, "m5" ) );
    TEST_ASSERT( WaitUntil( [ &gateway ]() { return ( GetGatewayStats( gateway ).m_Parked == 1 ); } ) );
    Thread::Sleep( 100 );
    TEST_ASSERT( worker.GetNumJobs() == 0 );

    TEST_ASSERT( SendToolchainFile( probe, ci, kGatewayTool, 1 ) );
    TEST_ASSERT( WaitUntil( [ &probe ]() { return ( GetNumResults( probe ) == 1 ); } ) );
    Array<uint8_t> types;
    worker.GetMessageTypes( types );
    TEST_ASSERT( types.GetSize() == 4 );
    TEST_ASSERT( ( types[ 0 ] == Protocol::MSG_MANIFEST ) && ( types[ 1 ] == Protocol::MSG_FILE ) && ( types[ 2 ] == Protocol::MSG_FILE ) );
    TEST_ASSERT( types[ 3 ] == Protocol::MSG_JOB );

    // The client was sending it, so it was not asked for it
    TEST_ASSERT( GetNumToolchainRequests( probe ) == 0 );
    TEST_ASSERT( GetGatewayStats( gateway ).m_ToolchainRequests == 0 );
    probe.ShutdownAllConnections();
}

// JobForAMissingToolchainRequestsIt
//------------------------------------------------------------------------------
void TestGateway::JobForAMissingToolchainRequestsIt() const
{
    EchoWorker worker( 4 );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    GatewayConfig config;
    MakeGatewayConfig( 1, config );
    Gateway gateway( config );
    TEST_ASSERT( gateway.Listen( Protocol::kTestPort ) );
    TEST_ASSERT( WaitForReadyWorkers( gateway, 1 ) );

    DistributedTest::HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    TEST_ASSERT( ConnectProbe( gateway.GetServer(), probe, &ci ) != 0 );
    Array<uint64_t> advertised;
    probe.GetToolchainIds( advertised );
    TEST_ASSERT( advertised.IsEmpty() );

    // A job for a toolchain it never sent
    JobStreamEncoder encoder;
    TEST_ASSERT( SendGatewayJob( probe, ci, encoder, 5, "m5" ) );
    Array<DistributedTest::HandshakeProbe::ToolchainRequest> requests;
    TEST_ASSERT( WaitUntil( [ &probe ]() { return ( GetNumToolchainRequests( probe ) == 1 ); } ) );
    probe.GetToolchainRequests( requests );
    TEST_ASSERT( requests[ 0 ].m_IsManifest && ( requests[ 0 ].m_ToolId == kGatewayTool ) );

    TEST_ASSERT( SendToolchainManifest( probe, ci, kGatewayTool, 1 ) );
    TEST_ASSERT( WaitUntil( [ &probe ]() { return ( GetNumToolchainRequests( probe ) == 2 ); } ) );
    probe.GetToolchainRequests( requests );
    TEST_ASSERT( ( requests[ 1 ].m_IsManifest == false ) && ( requests[ 1 ].m_ToolId == kGatewayTool ) && ( requests[ 1 ].m_FileId == 0 ) );
    TEST_ASSERT( worker.GetNumJobs() == 0 );

    TEST_ASSERT( SendToolchainFile( probe, ci, kGatewayTool, 0 ) );
    TEST_ASSERT( WaitUntil( [ &probe ]() { return ( GetNumResults( probe ) == 1 ); } ) );
    Array<uint8_t> types;
    worker.GetMessageTypes( types );
    TEST_ASSERT( types.GetSize() == 3 );
    TEST_ASSERT( ( types[ 0 ] == Protocol::MSG_MANIFEST ) && ( types[ 1 ] == Protocol::MSG_FILE ) && ( types[ 2 ] == Protocol::MSG_JOB ) );
    TEST_ASSERT( GetGatewayStats( gateway ).m_ToolchainRequests == 2 );
    TEST_ASSERT( GetNumToolchainRequests( probe ) == 2 );
    probe.ShutdownAllConnections();
}

// AdvertisedToolchainIsPinnedForTheConnection
//------------------------------------------------------------------------------
void TestGateway::AdvertisedToolchainIsPinnedForTheConnection() const
{
    // The stash holds the toolchain from an earlier run
    GatewayConfig config;
    MakeGatewayConfig( 1, config );
    {
        AStackString<> stashDir( config.m_CacheDir );
        stashDir.AppendFormat( "gateway%ctoolchains%c", (char)NATIVE_SLASH, (char)NATIVE_SLASH );
        ToolchainStash stash( stashDir );
        AddCompleteToStash( stash, kGatewayTool );
    }

    EchoWorker worker( 4 );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    Gateway gateway( config );
    TEST_ASSERT( gateway.Listen( Protocol::kTestPort ) );
    TEST_ASSERT( WaitForReadyWorkers( gateway, 1 ) );

    DistributedTest::HandshakeProbe a;
    const ConnectionInfo * ciA = nullptr;
    TEST_ASSERT( ConnectProbe( gateway.GetServer(), a, &ciA ) != 0 );
    Array<uint64_t> advertised;
    a.GetToolchainIds( advertised );
    TEST_ASSERT( ( advertised.GetSize() == 1 ) && ( advertised[ 0 ] == kGatewayTool ) );

    // Another client fills the stash, which evicts what it can
    {
        DistributedTest::HandshakeProbe b;
        const ConnectionInfo * ciB = nullptr;
        TEST_ASSERT( ConnectProbe( gateway.GetServer(), b, &ciB ) != 0 );
        const uint64_t firstTool = 0x8001;
        const uint64_t lastTool = firstTool + ToolchainStash::kMaxToolchains - 1;
        for ( uint64_t toolId = firstTool; toolId <= lastTool; ++toolId )
        {
            TEST_ASSERT( SendToolchain( b, ciB, toolId ) );
        }
        TEST_ASSERT( WaitUntil( [ &gateway, lastTool ]() { return gateway.m_Stash.IsComplete( lastTool ); } ) );
        TEST_ASSERT( gateway.m_Stash.IsComplete( firstTool ) == false );
        TEST_ASSERT( gateway.m_Stash.IsComplete( kGatewayTool ) );
        b.ShutdownAllConnections();
        TEST_ASSERT( WaitUntil( [ &gateway ]() { return ( GetGatewayStats( gateway ).m_Clients == 1 ); } ) );
    }
    TEST_ASSERT( gateway.m_Stash.IsComplete( kGatewayTool ) );

    // A's job is forwarded with the toolchain, and nothing is asked of A
    JobStreamEncoder encoder;
    TEST_ASSERT( SendGatewayJob( a, ciA, encoder, 5, "m5" ) );
    TEST_ASSERT( WaitUntil( [ &a ]() { return ( GetNumResults( a ) == 1 ); } ) );
    TEST_ASSERT( GetNumToolchainRequests( a ) == 0 );
    Array<uint8_t> types;
    worker.GetMessageTypes( types );
    TEST_ASSERT( types.GetSize() == 3 );
    TEST_ASSERT( ( types[ 0 ] == Protocol::MSG_MANIFEST ) && ( types[ 1 ] == Protocol::MSG_FILE ) && ( types[ 2 ] == Protocol::MSG_JOB ) );

    // Once A has gone, the toolchain can be evicted
    a.ShutdownAllConnections();
    TEST_ASSERT( WaitUntil( [ &gateway ]() { return ( GetGatewayStats( gateway ).m_Clients == 0 ); } ) );
    for ( uint64_t toolId = 0x9001; toolId < ( 0x9001 + ToolchainStash::kMaxToolchains ); ++toolId )
    {
        AddCompleteToStash( gateway.m_Stash, toolId );
    }
    TEST_ASSERT( WaitUntil( [ &gateway ]() { return ( gateway.m_Stash.IsComplete( kGatewayTool ) == false ); } ) );
}

// GatewayAsksTheClientForAMissingPch
//------------------------------------------------------------------------------
void TestGateway::GatewayAsksTheClientForAMissingPch() const
{
    EchoWorker worker( 4 );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    GatewayConfig config;
    MakeGatewayConfig( 1, config );
    Gateway gateway( config );
    TEST_ASSERT( gateway.Listen( Protocol::kTestPort ) );
    TEST_ASSERT( WaitForReadyWorkers( gateway, 1 ) );

    DistributedTest::HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    TEST_ASSERT( ConnectProbe( gateway.GetServer(), probe, &ci ) != 0 );
    TEST_ASSERT( SendToolchain( probe, ci ) );

    DistributedTest::CompressedPch pch;
    DistributedTest::MakeCompressedPch( 64 * 1024, 3, pch );
    JobStreamEncoder encoder;
    TEST_ASSERT( SendGatewayJob( probe, ci, encoder, 5, "m5", pch.m_Id ) );
    TEST_ASSERT( WaitUntil( [ &probe ]() { return ( probe.GetNumPchNeeded() == 1 ); } ) );
    TEST_ASSERT( probe.GetPchNeededId( 0 ) == pch.m_Id );
    TEST_ASSERT( GetGatewayStats( gateway ).m_Parked == 1 );
    TEST_ASSERT( worker.GetNumJobs() == 0 );

    TEST_ASSERT( SendPchFromProbe( probe, ci, pch ) );
    Array<GatewayWire::ResultHeader> results;
    TEST_ASSERT( WaitUntil( [ &probe, &results ]() { probe.GetResults( results ); return ( results.GetSize() == 1 ); } ) );
    TEST_ASSERT( ( results[ 0 ].m_JobId == 5 ) && results[ 0 ].m_Ok );
    TEST_ASSERT( worker.GetNumPchCopies() == 1 );
    TEST_ASSERT( worker.GetPchBytes() == pch.m_CompressedSize );
    TEST_ASSERT( probe.GetNumPchNeeded() == 1 );
    probe.ShutdownAllConnections();
}

// PchCrossesTheUplinkOnceForTwoWorkers
//------------------------------------------------------------------------------
void TestGateway::PchCrossesTheUplinkOnceForTwoWorkers() const
{
    EchoWorker w1( 1, EchoWorker::ANSWER_NEVER );
    EchoWorker w2( 1, EchoWorker::ANSWER_NEVER );
    TEST_ASSERT( w1.Listen( kGatewayTestDownstreamPort ) );
    TEST_ASSERT( w2.Listen( kGatewayTestDownstreamPort + 1 ) );
    GatewayConfig config;
    MakeGatewayConfig( 2, config );
    Gateway gateway( config );
    TEST_ASSERT( gateway.Listen( Protocol::kTestPort ) );
    TEST_ASSERT( WaitForReadyWorkers( gateway, 2 ) );

    DistributedTest::HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    TEST_ASSERT( ConnectProbe( gateway.GetServer(), probe, &ci ) != 0 );
    TEST_ASSERT( SendToolchain( probe, ci ) );
    DistributedTest::CompressedPch pch;
    DistributedTest::MakeCompressedPch( 2 * MEGABYTE, 4, pch );
    TEST_ASSERT( SendPchFromProbe( probe, ci, pch ) );

    // One slot each, so the two jobs go to the two workers
    JobStreamEncoder encoder;
    TEST_ASSERT( SendGatewayJob( probe, ci, encoder, 5, "m5", pch.m_Id ) );
    TEST_ASSERT( SendGatewayJob( probe, ci, encoder, 6, "m6", pch.m_Id ) );
    TEST_ASSERT( WaitUntil( [ &w1, &w2 ]() { return ( w1.GetNumJobs() == 1 ) && ( w2.GetNumJobs() == 1 ); } ) );
    TEST_ASSERT( ( w1.GetNumPchCopies() == 1 ) && ( w1.GetPchBytes() == pch.m_CompressedSize ) );
    TEST_ASSERT( ( w2.GetNumPchCopies() == 1 ) && ( w2.GetPchBytes() == pch.m_CompressedSize ) );
    w1.AnswerHeldJobs();
    w2.AnswerHeldJobs();
    TEST_ASSERT( WaitUntil( [ &probe ]() { return ( GetNumResults( probe ) == 2 ); } ) );

    // The client sent it once, and was never asked for it
    TEST_ASSERT( probe.GetNumPchNeeded() == 0 );
    TEST_ASSERT( GetGatewayStats( gateway ).m_PchBytesForwarded == ( 2 * (uint64_t)pch.m_CompressedSize ) );
    probe.ShutdownAllConnections();
}

// ResultsGoBackToTheClientThatSentTheJob
//------------------------------------------------------------------------------
void TestGateway::ResultsGoBackToTheClientThatSentTheJob() const
{
    EchoWorker worker( 4 );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    GatewayConfig config;
    MakeGatewayConfig( 1, config );
    Gateway gateway( config );
    TEST_ASSERT( gateway.Listen( Protocol::kTestPort ) );
    TEST_ASSERT( WaitForReadyWorkers( gateway, 1 ) );

    DistributedTest::HandshakeProbe a;
    DistributedTest::HandshakeProbe b;
    const ConnectionInfo * ciA = nullptr;
    const ConnectionInfo * ciB = nullptr;
    TEST_ASSERT( ConnectProbe( gateway.GetServer(), a, &ciA ) != 0 );
    TEST_ASSERT( ConnectProbe( gateway.GetServer(), b, &ciB ) != 0 );
    TEST_ASSERT( SendToolchain( a, ciA ) && SendToolchain( b, ciB ) );

    // The same job id from each
    JobStreamEncoder encoderA;
    JobStreamEncoder encoderB;
    TEST_ASSERT( SendGatewayJob( a, ciA, encoderA, 5, "a" ) );
    TEST_ASSERT( SendGatewayJob( b, ciB, encoderB, 5, "b" ) );
    TEST_ASSERT( WaitUntil( [ &a, &b ]() { return ( GetNumResults( a ) == 1 ) && ( GetNumResults( b ) == 1 ); } ) );
    Thread::Sleep( 100 ); // A result sent to the wrong client would have arrived by now

    Array<GatewayWire::ResultHeader> results;
    a.GetResults( results );
    TEST_ASSERT( ( results.GetSize() == 1 ) && ( results[ 0 ].m_JobId == 5 ) && ( results[ 0 ].m_Name == "a" ) );
    b.GetResults( results );
    TEST_ASSERT( ( results.GetSize() == 1 ) && ( results[ 0 ].m_JobId == 5 ) && ( results[ 0 ].m_Name == "b" ) );
    TEST_ASSERT( worker.GetNumJobs() == 2 );
    a.ShutdownAllConnections();
    b.ShutdownAllConnections();
}

// ThreadLanesOfTwoWorkersDoNotCollide
//------------------------------------------------------------------------------
void TestGateway::ThreadLanesOfTwoWorkersDoNotCollide() const
{
    EchoWorker w1( 1, EchoWorker::ANSWER_NEVER ); // Both answer from thread 1001
    EchoWorker w2( 1, EchoWorker::ANSWER_NEVER );
    TEST_ASSERT( w1.Listen( kGatewayTestDownstreamPort ) );
    TEST_ASSERT( w2.Listen( kGatewayTestDownstreamPort + 1 ) );
    GatewayConfig config;
    MakeGatewayConfig( 2, config );
    Gateway gateway( config );
    TEST_ASSERT( gateway.Listen( Protocol::kTestPort ) );
    TEST_ASSERT( WaitForReadyWorkers( gateway, 2 ) );

    DistributedTest::HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    TEST_ASSERT( ConnectProbe( gateway.GetServer(), probe, &ci ) != 0 );
    TEST_ASSERT( SendToolchain( probe, ci ) );
    JobStreamEncoder encoder;
    TEST_ASSERT( SendGatewayJob( probe, ci, encoder, 5, "m5" ) );
    TEST_ASSERT( SendGatewayJob( probe, ci, encoder, 6, "m6" ) );
    TEST_ASSERT( WaitUntil( [ &w1, &w2 ]() { return ( w1.GetNumJobs() == 1 ) && ( w2.GetNumJobs() == 1 ); } ) );
    w1.AnswerHeldJobs();
    w2.AnswerHeldJobs();
    Array<GatewayWire::ResultHeader> results;
    TEST_ASSERT( WaitUntil( [ &probe, &results ]() { probe.GetResults( results ); return ( results.GetSize() == 2 ); } ) );

    // Worker 1's lanes start at 1257, worker 2's at 1513
    Array<EchoWorker::ReceivedJob> jobsOf1;
    w1.GetJobs( jobsOf1 );
    for ( const GatewayWire::ResultHeader & result : results )
    {
        const bool fromWorker1 = ( result.m_Name == jobsOf1[ 0 ].m_Name );
        TEST_ASSERT( result.m_ThreadIndex == ( fromWorker1 ? 1257 : 1513 ) );
    }
    TEST_ASSERT( results[ 0 ].m_ThreadIndex != results[ 1 ].m_ThreadIndex );
    probe.ShutdownAllConnections();
}

// ClientsAreServedInTurn
//------------------------------------------------------------------------------
void TestGateway::ClientsAreServedInTurn() const
{
    EchoWorker worker( 1, EchoWorker::ANSWER_NEVER );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    GatewayConfig config;
    MakeGatewayConfig( 1, config );
    Gateway gateway( config );
    TEST_ASSERT( gateway.Listen( Protocol::kTestPort ) );
    TEST_ASSERT( WaitForReadyWorkers( gateway, 1 ) );

    DistributedTest::HandshakeProbe a;
    DistributedTest::HandshakeProbe b;
    const ConnectionInfo * ciA = nullptr;
    const ConnectionInfo * ciB = nullptr;
    TEST_ASSERT( ConnectProbe( gateway.GetServer(), a, &ciA ) != 0 );
    TEST_ASSERT( ConnectProbe( gateway.GetServer(), b, &ciB ) != 0 );
    TEST_ASSERT( SendToolchain( a, ciA ) && SendToolchain( b, ciB ) );

    // A's first job takes the only slot. Then A has three waiting, and B, which came later, three.
    JobStreamEncoder encoderA;
    JobStreamEncoder encoderB;
    TEST_ASSERT( SendGatewayJob( a, ciA, encoderA, 1, "a1" ) );
    TEST_ASSERT( WaitUntil( [ &worker ]() { return ( worker.GetNumJobs() == 1 ); } ) );
    TEST_ASSERT( SendGatewayJob( a, ciA, encoderA, 2, "a2" ) );
    TEST_ASSERT( SendGatewayJob( a, ciA, encoderA, 3, "a3" ) );
    TEST_ASSERT( SendGatewayJob( a, ciA, encoderA, 4, "a4" ) );
    TEST_ASSERT( WaitUntil( [ &gateway ]() { return ( GetGatewayStats( gateway ).m_Waiting == 3 ); } ) );
    TEST_ASSERT( SendGatewayJob( b, ciB, encoderB, 1, "b1" ) );
    TEST_ASSERT( SendGatewayJob( b, ciB, encoderB, 2, "b2" ) );
    TEST_ASSERT( SendGatewayJob( b, ciB, encoderB, 3, "b3" ) );
    TEST_ASSERT( WaitUntil( [ &gateway ]() { return ( GetGatewayStats( gateway ).m_Waiting == 6 ); } ) );

    // Each result frees the slot for the next turn
    for ( size_t i = 1; i < 7; ++i )
    {
        worker.AnswerHeldJobs();
        TEST_ASSERT( WaitUntil( [ &worker, i ]() { return ( worker.GetNumJobs() == ( i + 1 ) ); } ) );
    }
    worker.AnswerHeldJobs();

    const char * const expected[] = { "a1", "a2", "b1", "a3", "b2", "a4", "b3" };
    Array<EchoWorker::ReceivedJob> jobs;
    worker.GetJobs( jobs );
    TEST_ASSERT( jobs.GetSize() == 7 );
    for ( size_t i = 0; i < 7; ++i )
    {
        TEST_ASSERT( jobs[ i ].m_Name == expected[ i ] );
    }
    TEST_ASSERT( WaitUntil( [ &a, &b ]() { return ( GetNumResults( a ) == 4 ) && ( GetNumResults( b ) == 3 ); } ) );
    a.ShutdownAllConnections();
    b.ShutdownAllConnections();
}

// GatewayLogsForwarding
//------------------------------------------------------------------------------
void TestGateway::GatewayLogsForwarding() const
{
    EchoWorker worker( 4 );
    TEST_ASSERT( worker.Listen( kGatewayTestDownstreamPort ) );
    DistributedTest::CompressedPch pch;
    DistributedTest::MakeCompressedPch( 64 * 1024, 5, pch );
    GatewayConfig config;
    MakeGatewayConfig( 1, config );
    AStackString<> logPath;
    uint32_t clientId = 0;
    {
        Gateway gateway( config );
        TEST_ASSERT( gateway.Listen( Protocol::kTestPort ) );
        TEST_ASSERT( WaitForReadyWorkers( gateway, 1 ) );
        logPath = gateway.GetServer().GetLogPath();

        DistributedTest::HandshakeProbe probe;
        const ConnectionInfo * ci = nullptr;
        clientId = ConnectProbe( gateway.GetServer(), probe, &ci );
        TEST_ASSERT( clientId != 0 );
        TEST_ASSERT( SendToolchain( probe, ci ) );
        TEST_ASSERT( SendPchFromProbe( probe, ci, pch ) );
        JobStreamEncoder encoder;
        TEST_ASSERT( SendGatewayJob( probe, ci, encoder, 5, "m5", pch.m_Id ) );
        TEST_ASSERT( WaitUntil( [ &probe ]() { return ( GetNumResults( probe ) == 1 ); } ) );
        probe.ShutdownAllConnections();
    }

    // The gateway's own log, with the stats line it wrote as it shut down
    TEST_ASSERT( logPath.BeginsWith( config.m_CacheDir ) );
    AString log;
    TEST_ASSERT( ReadWholeFile( logPath, log ) );
    AStackString<> expected;
    expected.Format( "gateway worker-ready worker=127.0.0.1:%u slots=4", (uint32_t)kGatewayTestDownstreamPort );
    TEST_ASSERT( log.Find( expected.Get() ) );
    expected.Format( "gateway job-forward client=%u job=5 attempt=", clientId );
    TEST_ASSERT( log.Find( expected.Get() ) );
    expected.Format( " worker=127.0.0.1:%u pch=0x%016" PRIx64, (uint32_t)kGatewayTestDownstreamPort, pch.m_Id );
    TEST_ASSERT( log.Find( expected.Get() ) );
    expected.Format( "gateway pch-forward pch=0x%016" PRIx64 " worker=127.0.0.1:%u bytes=%u", pch.m_Id, (uint32_t)kGatewayTestDownstreamPort, pch.m_CompressedSize );
    TEST_ASSERT( log.Find( expected.Get() ) );
    const char * stats = log.Find( "gateway stats clients=" );
    TEST_ASSERT( stats );
    TEST_ASSERT( strstr( stats, " workers=1/1 pool=4 waiting=0 parked=0 running=0 heldMiB=0 forwarded=1 retries=0 giveups=0 timeouts=0 pchMiB=0" ) );
}

//------------------------------------------------------------------------------
