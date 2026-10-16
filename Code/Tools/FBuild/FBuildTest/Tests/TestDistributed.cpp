// TestDistributed.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildTest/Tests/FBuildTest.h"
#include "Tools/FBuild/FBuildTest/Tests/SyntheticToolchain.h"

#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/Graph/CompilerNode.h"
#include "Tools/FBuild/FBuildCore/Graph/ObjectNode.h"
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/DistributionStats.h"
#include "Tools/FBuild/FBuildCore/Helpers/FileChunks.h"
#include "Tools/FBuild/FBuildCore/Helpers/JobStream.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchBlob.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchDataCache.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchId.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchTransform.h"
#include "Tools/FBuild/FBuildCore/Helpers/StreamDecompressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/TempFiles.h"
#include "Tools/FBuild/FBuildCore/Helpers/ToolManifest.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/Protocol/Server.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/Job.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/JobQueueRemote.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/PchDispatch.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/WorkerThreadRemote.h"

// Core
#include "Core/Containers/UniquePtr.h"
#include "Core/Env/WindowsHeader.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Math/xxHash.h"
#include "Core/Mem/Mem.h"
#include "Core/Network/TCPConnectionPool.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Process.h"
#include "Core/Process/Thread.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Time.h"
#include "Core/Time/Timer.h"
#include "Core/Tracing/Tracing.h"

#include <stdlib.h>
#include <string.h>

// Defines
//------------------------------------------------------------------------------
#if !defined( __has_feature )
    #define __has_feature( ... ) 0
#endif

// TestDistributed
//------------------------------------------------------------------------------
namespace
{
    class NoCapacityWorker;
    class PchSender;
    struct CapturedJob;
    struct CompressedPch;
}

class TestDistributed : public FBuildTest
{
public:
    // Lets a test's Server, which is no friend of Server, hand a message on to it
    static void ReceiveAsServer( Server & server, const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory );

private:
    DECLARE_TESTS

    virtual void PreTest() const override;
    virtual void PostTest() const override;

    void TestWith1RemoteWorkerThread() const;
    void TestWith4RemoteWorkerThreads() const;
    void WithPCH() const;
    void WithPCHMacroUndefs() const;
    void PchCacheBudget() const;
    void PchRawBudget() const;
    void PchCacheEvictionPolicy() const;
    void PchStartupScanAppliesTheBudget() const;
    void PchBudgetFollowsTheFreeSpace() const;
    void PchInsertEvictsToTheBudget() const;
    void PchInsertCountsTheSizeOnDisk() const;
    void PchIsStoredCompressed() const;
    void PchIsUnpackedWhenAJobNeedsIt() const;
    void PchThatCannotBeUnpackedIsDropped() const;
    void PchThatCannotBeWrittenIsKept() const;
    void PchThatCannotBeWrittenReturnsItsJobs() const;
    void CorruptPchIsAskedForAgain() const;
    void CorruptPchIsReturnedToAClientThatCannotAsk() const;
    void PchUnpackIsSingleFlight() const;
    void UnpackSkipsAPchNoJobNeeds() const;
    void UnpackedCopyLivesWithItsJobs() const;
    void UnusedCopiesAreKeptToABudget() const;
    void UnusedCopiesGoWhenIdle() const;
    void LeasedPchIsNotEvicted() const;
    void PchsAreEvictedByRecency() const;
    void UnusedCopyGoesWhenTheFreeSpaceIsUnknown() const;
    void PchOfAnotherCompressionIsNotStoredAgain() const;
    void PchStartupScanKeepsTheCompressedStore() const;
    void PchStoreIsUsedThroughTheBuild() const;
    void PchLeasesAreReleasedByEveryEnd() const;
    void PchCompressedCache() const;
    void PchCompressRetry() const;
    void PchTakeReadyPchs() const;
    void PchChangedAfterItWasRecorded() const;
    void PchIdStreamsLikeOneShot() const;
    void PchRecordReadsTheFileOnce() const;
    void PchCompressionAroundChunkSizes() const;
    void PchCompressedFilesAreCleanedUp() const;
    void PchCompressionNeedsAPlaceToWrite() const;
    void PchStartupScanRemovesStaleTempFiles() const;
    void PchEvictionRemovesTheDirectory() const;
    void PchStartupScanRemovesEmptyDirectories() const;
    void DistributionStatsLog() const;
    void PayloadsAcceptedByWorker() const;
    void PchChunksReachTheWorker() const;
    void PchLeftHalfSentIsDropped() const;
    void PchInChunksThroughTheBuild() const;
    void ToolchainEviction() const;
    void ToolchainCacheStartupScan() const;
    void ToolchainCachedAcrossWorkerRestart() const;
    void ToolchainCorruptedFileIsResent() const;
    void ToolchainInventoryFollowsAck() const;
    void ToolchainInventoryIsBounded() const;
    void JobStreamAcrossReconnect() const;
    void ResultsAreCompressed() const;
    void CorruptResultIsRejected() const;
    void SendMutexHoldsBackResults() const;
    void ToolchainIsSentBeforeAnyJob() const;
    void PchIsSentBeforeAnyJobIsDispatched() const;
    void CachedToolchainIsNotSent() const;
    void ToolchainIsNotSentWhenNothingIsDistributed() const;
    void MismatchedProtocolVersionIsRefused() const;
    void FramesOfAnotherMajorAreRefused() const;
    void HandshakeLayoutIsFrozen() const;
    void VersionIsShownWithTheProtocol() const;
    void ShortMessagesAreRejected() const;
    void HeadersWithTheWrongPayloadAreRejected() const;
    void LongerMessagesAreAccepted() const;
    void HostNameStaysBounded() const;
#if defined( __WINDOWS__ )
    void HostNameIsBoundedByTheFrame() const;
#endif
    void CapabilitiesAreExchanged() const;
    void PchNeededMessageAndCapability() const;
    void WorkerAsksForAMissingPch() const;
    void WorkerDoesNotAskAClientWithoutTheCapability() const;
    void WorkerDoesNotAskForAPchBeingReceived() const;
    void WorkerAsksAgainWhenAPchItHadIsNeededAgain() const;
    void ClientSendsAPchAgainWhenAsked() const;
    void ClientGivesUpOnAWorkerThatKeepsLosingAPch() const;
    void ClientGivesUpOnAWorkerThatAsksForAnUnknownPch() const;
    void EvictedPchIsSentAgain() const;
    void MSVCWorkerFailuresAreRetriedElsewhere() const;
    void MSVCFailureOfARemoteJobIsASystemError() const;
    void ServerSendsOnlyWhatTheClientAdvertises() const;
    void ClientRefusesAWorkerThatRefusesIt() const;
    void ClientRefusesAnAckOfAnotherMajor() const;
    void ClientRejectsAShortAck() const;
    void ClientRejectsAnAckWithAPayload() const;
    void ClientDoesNotWaitForInventoriesTheWorkerLacks() const;
    void ClientIgnoresCapabilitiesItDidNotAdvertise() const;
    void ClientIsNotStuckWhenTheWorkerDropsDuringAPchSend() const;
    void PchTransformCapabilityIsNegotiated() const;
    void PchIsCompressedInTheFormsWorkersRead() const;
    void PchTransformThatDoesNotInvertIsNotSent() const;
    void PchIsCompressedWithTheWideWindow() const;
    void WorkerStoresATransformedPch() const;
    void WorkerAcceptsAPlainPchFromAnOldClient() const;
    void WorkerRejectsATransformedPchThatIsNotValid() const;
    void StoredTransformedPchThatCannotBeInvertedIsDropped() const;
#if defined( __WINDOWS__ )
    void ClientSendsTheTransformedPchOnlyToWorkersThatReadIt() const;
    void WorkersOfBothKindsAreSentTheirForms() const;
    void PchTransformedThroughTheBuild() const;
    void PchTransformFallsBackThroughTheBuild() const;
#endif
    void RegressionTest_RemoteCrashOnErrorFormatting();
    void TestLocalRace();
    void RemoteRaceWinRemote();
#if defined( DEBUG )
    void RemoteRaceSystemFailure();
#endif
    void AnonymousNamespaces();
    void ErrorsAreCorrectlyReported_MSVC() const;
    void ErrorsAreCorrectlyReported_Clang() const;
    void WarningsAreCorrectlyReported_MSVC() const;
    void WarningsAreCorrectlyReported_Clang() const;
    void ShutdownMemoryLeak() const;
    void TestForceInclude() const;
    void TestZiDebugFormat() const;
    void TestZiDebugFormat_Local() const;
    void D8049_ToolLongDebugRecord() const;
    void DynamicDeoptimization() const;
    void CleanMessageToPreventMSBuildFailure() const;

    void TestHelper( const char * target,
                     uint32_t numRemoteWorkers,
                     bool shouldFail = false,
                     bool allowRace = false ) const;

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

    // PchDataCache helpers
    struct CompressionRequest
    {
        PchDataCache *                  m_Cache = nullptr;
        const PchDataCache::Entry *     m_Entry = nullptr;
        Atomic<bool> *                  m_Go = nullptr;
        PchDataCache::CompressedState   m_State = PchDataCache::COMPRESSING;
        AString                         m_FilePath;
        uint64_t                        m_Size = 0;
    };
    static void MakeTestContent( uint32_t size, uint32_t seed, Array<char> & outContent );
    static bool WriteTestFile( const char * path, uint32_t size, Array<char> & outContent, uint32_t seed = 0 );
    static bool MakeSmallFile( const AString & dir, const char * name, AString & outFile );
    static PchDataCache::CompressedState WaitForCompression( PchDataCache & cache,
                                                             const PchDataCache::Entry & entry,
                                                             AString & outFilePath,
                                                             uint64_t & outSize );
    static bool LoadFile( const AString & path, Array<char> & outData );
    static bool DecodesTo( const void * data, size_t dataSize, const Array<char> & content );
    static bool DecodesToFile( const AString & path, const Array<char> & content );
    static uint32_t RequestCompression( void * param );
    void BuildOnNewWorker( uint32_t & outManifestsReceived,
                           uint32_t & outToolFilesReceived,
                           AString & outWorkerToolchainFile ) const;

    // Drops the worker's connections once a job is being built on its toolchain
    struct ConnectionDrop
    {
        Server *        m_Server = nullptr;
        Atomic<bool>    m_Stop{ false };
        Atomic<bool>    m_Dropped{ false };
    };
    static bool WorkerIsBuildingAJob( Server & server );
    static uint32_t DropConnectionOnFirstJob( void * param );

    // What the PCH cache of a Server may hold, in place of what its disk allows
    inline static Server::PchCacheLimits s_PchCacheLimits = { 0, 0, 0 };
    inline static bool s_PchCacheLimitsKnown = true;
    inline static Atomic<uint64_t> s_LastCacheBytes{ 0 }; // What the Server said the cache holds, when it asked
    static bool GetTestPchCacheLimits( uint64_t cacheBytes, Server::PchCacheLimits & outLimits );
    static void SetPchCacheLimits( Server & server, uint64_t bytes, uint32_t minEntries, uint64_t rawBytes = ( 1ull << 30 ) );
    static void SetPchCacheLimitsUnknown( Server & server ); // As when the free space cannot be read

    // A worker that makes room for another PCH when its second job arrives
    static void EvictForTheSecondJob( Server & server, void * user );

    void CapturePchJob( CapturedJob & outJob ) const;

    // The compressed PCH store, and the working copies of its PCHs
    struct StoredPch
    {
        uint64_t    m_Id = 0;
        AString     m_StoredFile;
        AString     m_RawFile;
        Array<char> m_Content;
        uint64_t    m_StoredSize = 0;
    };
    static void AddStoredBlob( Server & server, uint64_t pchId, const void * blob, size_t blobSize, bool writeFile, AString & outStoredFile );
    static void AddStoredPch( Server & server, uint32_t size, uint32_t seed, StoredPch & outPch );
    static Job * MakePchJob( Server & server, uint64_t pchId ); // Holds the PCH, until it is deleted
    static bool IsPchUnpacked( Server & server, uint64_t pchId );
    static bool HasRawEntry( Server & server, uint64_t pchId );
    static uint32_t NumLeases( Server & server, uint64_t pchId );
    static void CompressPch( CompressedPch & pch ); // Of its content
    static void MakeCompressedPch( uint32_t size, uint32_t seed, CompressedPch & outPch );
    static bool SendPch( PchSender & sender, const CompressedPch & pch );
    static uint32_t TotalLeases( Server & server );
    static bool IsPchBeingUnpacked( Server & server, uint64_t pchId );
    static uint32_t NumWaitingJobsWithAPch( Server & server );
    static void SetPchRecency( Server & server, uint64_t pchId, uint64_t fileTime ); // Of the compressed PCH
    struct ResolveRequest
    {
        Server *        m_Server = nullptr;
        Atomic<bool> *  m_Go = nullptr;
        Job *           m_Jobs[ 2 ] = {};
        bool            m_Resolved = false;
    };
    static uint32_t ResolveThread( void * param );
    static uint32_t CountWaitingJobs( Server & server );
    static void DropWaitingJobs( Server & server ); // As if they had been built

    // Holds a connection's send mutex while its first job completes
    struct SendHold
    {
        Server *        m_Server = nullptr;
        Atomic<bool>    m_Stop{ false };
        Atomic<bool>    m_BuildDone{ false };
        Atomic<bool>    m_ReachedSend{ false };
        Atomic<bool>    m_StateMutexFree{ false };
        Atomic<bool>    m_ResultDeliveredWhileHeld{ false };
    };
    static uint32_t HoldSendMutexOnFirstJob( void * param );
    void BuildPchLibraryUntilStopped( NoCapacityWorker & worker, int when, uint32_t clientCapabilities = Protocol::kCapabilities ) const;

    // PCH transform helpers
    static bool WritePchLikeFile( const char * path, uint32_t size, uint32_t seed, Array<char> & outContent );
    static PchDataCache::CompressedState WaitForForm( PchDataCache & cache, const PchDataCache::Entry & entry, bool allowTransform, AString & outFilePath, uint64_t & outSize, bool & outTransformed );
    static bool BlobDecodesTo( const Array<char> & blob, const Array<char> & content ); // As a worker decodes a PCH: the transform is undone
    static uint32_t GetBlobType( const Array<char> & blob ); // The first word of its header
    static uint32_t CountInStatsLog( const char * text );
    static void ClearWorkerPchStore();

    // Totals over the lines starting with linePrefix in the client's stats log, for lines like
    // "  jobs:      12 msgs, 1.1 MB raw, 40.2 KB sent, ..." (first is raw, second is sent or received)
    static void ReadStats( const AString & logPath, const char * linePrefix, uint32_t & outNumBlocks, uint64_t & outRawBytes, uint64_t & outOtherBytes, uint32_t * outNumMessages = nullptr );

    mutable SyntheticToolchain::ScratchWorkerCache m_WorkerCache;
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestDistributed )
    REGISTER_TEST( TestWith1RemoteWorkerThread )
    REGISTER_TEST( TestWith4RemoteWorkerThreads )
    REGISTER_TEST( WithPCH )
#if defined( __WINDOWS__ )
    REGISTER_TEST( WithPCHMacroUndefs )
#endif
    REGISTER_TEST( PchCacheBudget )
    REGISTER_TEST( PchRawBudget )
    REGISTER_TEST( PchCacheEvictionPolicy )
    REGISTER_TEST( PchStartupScanAppliesTheBudget )
    REGISTER_TEST( PchBudgetFollowsTheFreeSpace )
    REGISTER_TEST( PchInsertEvictsToTheBudget )
    REGISTER_TEST( PchInsertCountsTheSizeOnDisk )
    REGISTER_TEST( PchIsStoredCompressed )
    REGISTER_TEST( PchIsUnpackedWhenAJobNeedsIt )
    REGISTER_TEST( PchThatCannotBeUnpackedIsDropped )
    REGISTER_TEST( PchThatCannotBeWrittenIsKept )
    REGISTER_TEST( PchUnpackIsSingleFlight )
    REGISTER_TEST( UnpackSkipsAPchNoJobNeeds )
    REGISTER_TEST( UnpackedCopyLivesWithItsJobs )
    REGISTER_TEST( UnusedCopiesAreKeptToABudget )
    REGISTER_TEST( UnusedCopiesGoWhenIdle )
    REGISTER_TEST( LeasedPchIsNotEvicted )
    REGISTER_TEST( PchsAreEvictedByRecency )
    REGISTER_TEST( UnusedCopyGoesWhenTheFreeSpaceIsUnknown )
    REGISTER_TEST( PchStartupScanKeepsTheCompressedStore )
    REGISTER_TEST( PchOfAnotherCompressionIsNotStoredAgain )
    REGISTER_TEST( PchCompressedCache )
    REGISTER_TEST( PchCompressRetry )
    REGISTER_TEST( PchTakeReadyPchs )
    REGISTER_TEST( PchChangedAfterItWasRecorded )
    REGISTER_TEST( PchIdStreamsLikeOneShot )
    REGISTER_TEST( PchRecordReadsTheFileOnce )
    REGISTER_TEST( PchCompressionAroundChunkSizes )
    REGISTER_TEST( PchCompressedFilesAreCleanedUp )
    REGISTER_TEST( PchCompressionNeedsAPlaceToWrite )
    REGISTER_TEST( PchStartupScanRemovesStaleTempFiles )
    REGISTER_TEST( PchEvictionRemovesTheDirectory )
    REGISTER_TEST( PchStartupScanRemovesEmptyDirectories )
    REGISTER_TEST( DistributionStatsLog )
    REGISTER_TEST( PayloadsAcceptedByWorker )
    REGISTER_TEST( PchChunksReachTheWorker )
    REGISTER_TEST( PchLeftHalfSentIsDropped )
    REGISTER_TEST( PchInChunksThroughTheBuild )
    REGISTER_TEST( ToolchainEviction )
    REGISTER_TEST( ToolchainCacheStartupScan )
    REGISTER_TEST( ToolchainCachedAcrossWorkerRestart )
    REGISTER_TEST( ToolchainCorruptedFileIsResent )
    REGISTER_TEST( ToolchainInventoryFollowsAck )
    REGISTER_TEST( ToolchainInventoryIsBounded )
    REGISTER_TEST( JobStreamAcrossReconnect )
    REGISTER_TEST( ResultsAreCompressed )
    REGISTER_TEST( CorruptResultIsRejected )
    REGISTER_TEST( SendMutexHoldsBackResults )
#if defined( __WINDOWS__ )
    REGISTER_TEST( ToolchainIsSentBeforeAnyJob )
    REGISTER_TEST( PchIsSentBeforeAnyJobIsDispatched )
    REGISTER_TEST( CachedToolchainIsNotSent )
    REGISTER_TEST( ToolchainIsNotSentWhenNothingIsDistributed )
#endif
    REGISTER_TEST( MismatchedProtocolVersionIsRefused )
    REGISTER_TEST( FramesOfAnotherMajorAreRefused )
    REGISTER_TEST( HandshakeLayoutIsFrozen )
    REGISTER_TEST( VersionIsShownWithTheProtocol )
    REGISTER_TEST( ShortMessagesAreRejected )
    REGISTER_TEST( HeadersWithTheWrongPayloadAreRejected )
    REGISTER_TEST( LongerMessagesAreAccepted )
    REGISTER_TEST( HostNameStaysBounded )
#if defined( __WINDOWS__ )
    REGISTER_TEST( HostNameIsBoundedByTheFrame )
#endif
    REGISTER_TEST( CapabilitiesAreExchanged )
    REGISTER_TEST( PchNeededMessageAndCapability )
#if defined( __WINDOWS__ ) // PCH distribution is for MSVC
    REGISTER_TEST( WorkerAsksForAMissingPch )
    REGISTER_TEST( WorkerDoesNotAskAClientWithoutTheCapability )
    REGISTER_TEST( WorkerDoesNotAskForAPchBeingReceived )
    REGISTER_TEST( WorkerAsksAgainWhenAPchItHadIsNeededAgain )
    REGISTER_TEST( ClientSendsAPchAgainWhenAsked )
    REGISTER_TEST( ClientGivesUpOnAWorkerThatKeepsLosingAPch )
    REGISTER_TEST( ClientGivesUpOnAWorkerThatAsksForAnUnknownPch )
    REGISTER_TEST( EvictedPchIsSentAgain )
    REGISTER_TEST( PchThatCannotBeWrittenReturnsItsJobs )
    REGISTER_TEST( CorruptPchIsAskedForAgain )
    REGISTER_TEST( CorruptPchIsReturnedToAClientThatCannotAsk )
    REGISTER_TEST( PchStoreIsUsedThroughTheBuild )
    REGISTER_TEST( PchLeasesAreReleasedByEveryEnd )
    REGISTER_TEST( MSVCFailureOfARemoteJobIsASystemError )
#endif
    REGISTER_TEST( MSVCWorkerFailuresAreRetriedElsewhere )
    REGISTER_TEST( ServerSendsOnlyWhatTheClientAdvertises )
    REGISTER_TEST( ClientRefusesAWorkerThatRefusesIt )
    REGISTER_TEST( ClientRefusesAnAckOfAnotherMajor )
    REGISTER_TEST( ClientRejectsAShortAck )
    REGISTER_TEST( ClientRejectsAnAckWithAPayload )
    REGISTER_TEST( ClientDoesNotWaitForInventoriesTheWorkerLacks )
    REGISTER_TEST( ClientIgnoresCapabilitiesItDidNotAdvertise )
    REGISTER_TEST( ClientIsNotStuckWhenTheWorkerDropsDuringAPchSend )
    REGISTER_TEST( PchTransformCapabilityIsNegotiated )
    REGISTER_TEST( PchIsCompressedInTheFormsWorkersRead )
    REGISTER_TEST( PchTransformThatDoesNotInvertIsNotSent )
    REGISTER_TEST( PchIsCompressedWithTheWideWindow )
    REGISTER_TEST( WorkerStoresATransformedPch )
    REGISTER_TEST( WorkerAcceptsAPlainPchFromAnOldClient )
    REGISTER_TEST( WorkerRejectsATransformedPchThatIsNotValid )
    REGISTER_TEST( StoredTransformedPchThatCannotBeInvertedIsDropped )
#if defined( __WINDOWS__ )
    REGISTER_TEST( ClientSendsTheTransformedPchOnlyToWorkersThatReadIt )
    REGISTER_TEST( WorkersOfBothKindsAreSentTheirForms )
    REGISTER_TEST( PchTransformedThroughTheBuild )
    REGISTER_TEST( PchTransformFallsBackThroughTheBuild )
#endif
    REGISTER_TEST( RegressionTest_RemoteCrashOnErrorFormatting )
    REGISTER_TEST( TestLocalRace )
    REGISTER_TEST( RemoteRaceWinRemote )
#if defined( DEBUG )
    REGISTER_TEST( RemoteRaceSystemFailure )
#endif
    REGISTER_TEST( AnonymousNamespaces )
    REGISTER_TEST( ShutdownMemoryLeak )
#if defined( __WINDOWS__ )
    REGISTER_TEST( ErrorsAreCorrectlyReported_MSVC ) // TODO:B Enable for OSX and Linux
    REGISTER_TEST( ErrorsAreCorrectlyReported_Clang ) // TODO:B Enable for OSX and Linux
    REGISTER_TEST( WarningsAreCorrectlyReported_MSVC ) // TODO:B Enable for OSX and Linux
    REGISTER_TEST( WarningsAreCorrectlyReported_Clang ) // TODO:B Enable for OSX and Linux
    REGISTER_TEST( TestForceInclude )
    REGISTER_TEST( TestZiDebugFormat )
    REGISTER_TEST( TestZiDebugFormat_Local )
    REGISTER_TEST( D8049_ToolLongDebugRecord )
    REGISTER_TEST( DynamicDeoptimization )
#endif
    REGISTER_TEST( CleanMessageToPreventMSBuildFailure )
REGISTER_TESTS_END

// PreTest
//------------------------------------------------------------------------------
/*virtual*/ void TestDistributed::PreTest() const
{
    FBuildTest::PreTest();

    // The workers started by these tests keep toolchains and PCHs where a real worker on the
    // machine would, and trim what they find there
    AStackString<> codeDir;
    GetCodeDir( codeDir );
    m_WorkerCache.Enter( codeDir, "Distributed" );
}

// PostTest
//------------------------------------------------------------------------------
/*virtual*/ void TestDistributed::PostTest() const
{
    m_WorkerCache.Leave();

    FBuildTest::PostTest();
}

// Test
//------------------------------------------------------------------------------
void TestDistributed::TestHelper( const char * target, uint32_t numRemoteWorkers, bool shouldFail, bool allowRace ) const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
    options.m_AllowLocalRace = allowRace;
    FBuildForTest fBuild( options );

    TEST_ASSERT( fBuild.Initialize() );

    // start a client to emulate the other end
    Server s( numRemoteWorkers );
    s.Listen( Protocol::kTestPort );

    // clean up anything left over from previous runs
    Array<AString> files;
    FileIO::GetFiles( AStackString( "../tmp/Test/Distributed" ), AStackString( "*.*" ), true, &files );
    for ( const AString & file : files )
    {
        FileIO::FileDelete( file.Get() );
    }

    if ( !shouldFail )
    {
        TEST_ASSERT( FileIO::FileExists( target ) == false );
    }

    const bool pass = fBuild.Build( target );
    if ( !shouldFail )
    {
        TEST_ASSERT( pass );

        // make sure all output files are as expected
        TEST_ASSERT( FileIO::FileExists( target ) );
    }
}

// TestWith1RemoteWorkerThread
//------------------------------------------------------------------------------
void TestDistributed::TestWith1RemoteWorkerThread() const
{
    const char * target( "../tmp/Test/Distributed/dist.lib" );
    TestHelper( target, 1 );
}

// TestWith4RemoteWorkerThreads
//------------------------------------------------------------------------------
void TestDistributed::TestWith4RemoteWorkerThreads() const
{
    const char * target( "../tmp/Test/Distributed/dist.lib" );
    TestHelper( target, 4 );
}

// WithPCH
//------------------------------------------------------------------------------
void TestDistributed::WithPCH() const
{
    const char * target( "../tmp/Test/Distributed/distpch.lib" );
    TestHelper( target, 4 );
}

// WithPCHMacroUndefs
//------------------------------------------------------------------------------
void TestDistributed::WithPCHMacroUndefs() const
{
    // Tests that the #undef block generated alongside the .pch prevents
    // macro re-expansion on the remote worker. The PCH defines MYMACRO and
    // includes a header that uses push_macro/undef/pop_macro to temporarily
    // disable it. Without the undef block, the remote compile would fail
    // because /Yu restores the macro state and re-expands the function name.
    const char * target( "../tmp/Test/Distributed/PCHMacro/distpchmacro.lib" );
    TestHelper( target, 4 );
}

// PchNeededMessageAndCapability
//------------------------------------------------------------------------------
void TestDistributed::PchNeededMessageAndCapability() const
{
    // 24.1 adds a message and a capability to 24.0, which a 24.0 peer does not know and never gets
    TEST_ASSERT( ( Protocol::kCapabilities & Protocol::CAP_PCH_REQUEST ) != 0 );

    // Only both sides advertising it makes it available
    TEST_ASSERT( Protocol::Negotiate( Protocol::kCapabilities, Protocol::kCapabilities ) == Protocol::kCapabilities );
    TEST_ASSERT( ( Protocol::Negotiate( Protocol::CAP_PCH_INVENTORY | Protocol::CAP_TOOLCHAIN_INVENTORY, Protocol::kCapabilities ) & Protocol::CAP_PCH_REQUEST ) == 0 );

    const Protocol::MsgPchNeeded msg( 0x1122334455667788ull );
    TEST_ASSERT( msg.GetType() == Protocol::MSG_PCH_NEEDED );
    TEST_ASSERT( msg.HasPayload() == false );
    TEST_ASSERT( msg.GetSize() == sizeof( Protocol::MsgPchNeeded ) );
    TEST_ASSERT( msg.GetPchId() == 0x1122334455667788ull );
    TEST_ASSERT( Protocol::IsValidHeader( &msg, sizeof( msg ) ) );
}

// PchCacheBudget
//------------------------------------------------------------------------------
void TestDistributed::PchCacheBudget() const
{
    const uint64_t GiB = ( 1024ull * 1024 * 1024 );
    const uint64_t kNoLimit = ~(uint64_t)0;
    auto isAbout = []( uint64_t actual, uint64_t expected )
    {
        const uint64_t difference = ( actual > expected ) ? ( actual - expected ) : ( expected - actual );
        return ( difference < ( 1024 * 1024 ) );
    };

    // A big disk keeps what the limit allows, however much the cache holds
    TEST_ASSERT( Server::CalculatePchCacheBudget( 1800 * GiB, 0 ) == Server::kMaxPchCacheBytes );
    TEST_ASSERT( Server::CalculatePchCacheBudget( 1800 * GiB, 12 * GiB ) == Server::kMaxPchCacheBytes );

    // A small disk keeps a share of what could be used: free space and what the cache already has
    TEST_ASSERT( isAbout( Server::CalculatePchCacheBudget( 10 * GiB, 0 ), 5 * GiB ) );
    TEST_ASSERT( isAbout( Server::CalculatePchCacheBudget( 12 * GiB, 0 ), 6 * GiB ) );
    TEST_ASSERT( isAbout( Server::CalculatePchCacheBudget( 6 * GiB, 4 * GiB ), 5 * GiB ) );

    // The compressed PCHs of a large project (a few GiB) fit a small disk: 4 GiB free is room for 2 GiB of them
    TEST_ASSERT( isAbout( Server::CalculatePchCacheBudget( 4 * GiB, 0 ), 2 * GiB ) );

    // The most it keeps is 8 GiB
    TEST_ASSERT( isAbout( Server::CalculatePchCacheBudget( 16 * GiB, 0 ), 8 * GiB ) );
    TEST_ASSERT( Server::CalculatePchCacheBudget( 20 * GiB, 0 ) == ( 8 * GiB ) );

    // The bytes the cache holds are counted: the budget does not shrink as the cache fills the disk
    TEST_ASSERT( isAbout( Server::CalculatePchCacheBudget( 4 * GiB, 6 * GiB ), 5 * GiB ) );
    TEST_ASSERT( isAbout( Server::CalculatePchCacheBudget( 0, 5 * GiB ), ( 5 * GiB ) / 2 ) );
    TEST_ASSERT( Server::CalculatePchCacheBudget( 0, 5 * GiB ) > Server::CalculatePchCacheBudget( 0, 4 * GiB ) );

    // A disk that is nearly full still gets the floor
    TEST_ASSERT( Server::CalculatePchCacheBudget( 0, 0 ) == Server::kMinPchCacheBytes );
    TEST_ASSERT( Server::CalculatePchCacheBudget( 1 * GiB, 0 ) == Server::kMinPchCacheBytes );
    TEST_ASSERT( Server::CalculatePchCacheBudget( 2 * GiB, 0 ) == Server::kMinPchCacheBytes );
    TEST_ASSERT( isAbout( Server::CalculatePchCacheBudget( 3 * GiB, 0 ), ( 3 * GiB ) / 2 ) ); // Above it
    TEST_ASSERT( Server::CalculatePchCacheBudget( 3 * GiB, 0 ) > Server::kMinPchCacheBytes );

    // Values near the limit of the type do not wrap around
    TEST_ASSERT( Server::CalculatePchCacheBudget( kNoLimit, kNoLimit ) == Server::kMaxPchCacheBytes );
    TEST_ASSERT( Server::CalculatePchCacheBudget( kNoLimit, 1 ) == Server::kMaxPchCacheBytes );
}

// PchRawBudget
//------------------------------------------------------------------------------
void TestDistributed::PchRawBudget() const
{
    const uint64_t GiB = ( 1024ull * 1024 * 1024 );
    const uint64_t kNoLimit = ~(uint64_t)0;
    auto isAbout = []( uint64_t actual, uint64_t expected )
    {
        const uint64_t difference = ( actual > expected ) ? ( actual - expected ) : ( expected - actual );
        return ( difference < ( 1024 * 1024 ) );
    };

    // What is kept for the next jobs is 2 GiB at most, however large the disk
    TEST_ASSERT( Server::CalculateRawPchBudget( 1800 * GiB, 0 ) == Server::kMaxRawPchBytes );
    TEST_ASSERT( Server::CalculateRawPchBudget( kNoLimit, kNoLimit ) == Server::kMaxRawPchBytes );
    TEST_ASSERT( Server::CalculateRawPchBudget( 12 * GiB, 0 ) == Server::kMaxRawPchBytes );

    // A small disk keeps a fifth of what could be used, counting what the cache holds
    TEST_ASSERT( isAbout( Server::CalculateRawPchBudget( 6 * GiB, 0 ), ( 6 * GiB ) / 5 ) );
    TEST_ASSERT( isAbout( Server::CalculateRawPchBudget( 2 * GiB, 3 * GiB ), 1 * GiB ) );
    TEST_ASSERT( Server::CalculateRawPchBudget( 2 * GiB, 4 * GiB ) > Server::CalculateRawPchBudget( 2 * GiB, 3 * GiB ) );

    // A disk that is full keeps none
    TEST_ASSERT( Server::CalculateRawPchBudget( 0, 0 ) == 0 );
}

// PchCacheEvictionPolicy
//------------------------------------------------------------------------------
void TestDistributed::PchCacheEvictionPolicy() const
{
    const uint64_t kNoLimit = ~(uint64_t)0;

    // Entries are deliberately not in age order, so the sort is exercised. Each takes 100 bytes.
    Array<Server::PchCacheEntry> entries;
    entries.Append( Server::PchCacheEntry{ 3, AString( "dummy3" ), 300, 100 } );
    entries.Append( Server::PchCacheEntry{ 1, AString( "dummy1" ), 100, 100 } );
    entries.Append( Server::PchCacheEntry{ 4, AString( "dummy4" ), 400, 100 } );
    entries.Append( Server::PchCacheEntry{ 2, AString( "dummy2" ), 200, 100 } );

    Array<uint64_t> inUse;
    Array<size_t> evictions;

    // Within the budget: nothing is evicted
    Server::SelectPchEvictions( entries, 400, 0, inUse, evictions );
    TEST_ASSERT( evictions.IsEmpty() );

    // Over it by two PCHs: the two oldest go, whatever their position
    Server::SelectPchEvictions( entries, 200, 0, inUse, evictions );
    TEST_ASSERT( evictions.GetSize() == 2 );
    TEST_ASSERT( entries[ evictions[ 0 ] ].pchId == 1 );
    TEST_ASSERT( entries[ evictions[ 1 ] ].pchId == 2 );

    // What counts is the bytes: 250 holds two, 199 only one
    Server::SelectPchEvictions( entries, 250, 0, inUse, evictions );
    TEST_ASSERT( evictions.GetSize() == 2 );
    Server::SelectPchEvictions( entries, 199, 0, inUse, evictions );
    TEST_ASSERT( evictions.GetSize() == 3 );
    TEST_ASSERT( entries[ evictions[ 2 ] ].pchId == 3 );

    // PCHs differ in size, so one large old PCH makes room for several small ones: the oldest alone is
    // enough here, though the count of entries is not over any limit
    {
        Array<Server::PchCacheEntry> mixed;
        mixed.Append( Server::PchCacheEntry{ 11, AString( "dummy11" ), 100, 300 } );
        mixed.Append( Server::PchCacheEntry{ 12, AString( "dummy12" ), 200, 100 } );
        mixed.Append( Server::PchCacheEntry{ 13, AString( "dummy13" ), 300, 100 } );
        mixed.Append( Server::PchCacheEntry{ 14, AString( "dummy14" ), 400, 100 } );
        Server::SelectPchEvictions( mixed, 400, 0, inUse, evictions );
        TEST_ASSERT( evictions.GetSize() == 1 );
        TEST_ASSERT( mixed[ evictions[ 0 ] ].pchId == 11 );

        // and when the newest is the large one, the oldest small ones go until it fits
        mixed[ 0 ].size = 100;
        mixed[ 3 ].size = 300;
        Server::SelectPchEvictions( mixed, 400, 0, inUse, evictions );
        TEST_ASSERT( evictions.GetSize() == 2 );
        TEST_ASSERT( mixed[ evictions[ 0 ] ].pchId == 11 );
        TEST_ASSERT( mixed[ evictions[ 1 ] ].pchId == 12 );
    }

    // The oldest is in use: it is skipped and the next-oldest go instead
    inUse.Append( 1 );
    Server::SelectPchEvictions( entries, 200, 0, inUse, evictions );
    TEST_ASSERT( evictions.GetSize() == 2 );
    TEST_ASSERT( entries[ evictions[ 0 ] ].pchId == 2 );
    TEST_ASSERT( entries[ evictions[ 1 ] ].pchId == 3 );

    // Everything but the newest is in use: only what is free can go, so the budget is exceeded
    inUse.Append( 2 );
    inUse.Append( 3 );
    Server::SelectPchEvictions( entries, 200, 0, inUse, evictions );
    TEST_ASSERT( evictions.GetSize() == 1 );
    TEST_ASSERT( entries[ evictions[ 0 ] ].pchId == 4 );

    // The newest is the only one free but is itself in use (just received): nothing to evict
    inUse.Append( 4 );
    Server::SelectPchEvictions( entries, 200, 0, inUse, evictions );
    TEST_ASSERT( evictions.IsEmpty() );

    // A PCH that is larger than the whole budget is kept while it is the only one in use
    {
        Array<Server::PchCacheEntry> big;
        big.Append( Server::PchCacheEntry{ 21, AString( "dummy21" ), 100, 1000 } );
        Array<uint64_t> bigInUse;
        bigInUse.Append( 21 );
        Server::SelectPchEvictions( big, 10, 0, bigInUse, evictions );
        TEST_ASSERT( evictions.IsEmpty() );
    }

    // The floor: the newest PCHs stay however far over the budget they are
    inUse.Clear();
    Server::SelectPchEvictions( entries, 0, 3, inUse, evictions );
    TEST_ASSERT( evictions.GetSize() == 1 );
    TEST_ASSERT( entries[ evictions[ 0 ] ].pchId == 1 );
    Server::SelectPchEvictions( entries, 0, 4, inUse, evictions );
    TEST_ASSERT( evictions.IsEmpty() );
    Server::SelectPchEvictions( entries, 0, 0, inUse, evictions );
    TEST_ASSERT( evictions.GetSize() == 4 );

    // However small the PCHs are, there is a limit to how many are kept: the oldest go
    {
        const uint32_t kNumEntries = ( Server::kMaxCachedPchEntries + 44 );
        Array<Server::PchCacheEntry> many;
        for ( uint32_t i = 0; i < kNumEntries; ++i )
        {
            many.Append( Server::PchCacheEntry{ 1000 + i, AString( "dummy" ), (uint64_t)( 5000 - i ), 0 } ); // The later ones are older
        }
        Server::SelectPchEvictions( many, kNoLimit, 0, inUse, evictions );
        TEST_ASSERT( evictions.GetSize() == 44 );
        for ( const size_t index : evictions )
        {
            TEST_ASSERT( index >= Server::kMaxCachedPchEntries );
        }
    }
}

// PchStartupScanAppliesTheBudget
//------------------------------------------------------------------------------
void TestDistributed::PchStartupScanAppliesTheBudget() const
{
    // Six PCHs of 5 bytes on disk, written one after the other
    AStackString<> root;
    VERIFY( FBuild::GetWorkerCacheDir( root ) );
    const uint32_t kNumPchs = 6;
    Array<AString> files;
    for ( uint32_t i = 0; i < kNumPchs; ++i )
    {
        AStackString<> dir;
        dir.Format( "%spch.%016" PRIx64 "%c", root.Get(), (uint64_t)( 0xA000 + i ), (char)NATIVE_SLASH );
        AStackString<> file;
        TEST_ASSERT( MakeSmallFile( dir, PchBlob::kStoredFileName, file ) );
        files.Append( file );
        Thread::Sleep( 30 );
    }

    Server s( 1 );
    TEST_ASSERT( s.m_PchCache.GetSize() == kNumPchs ); // The real budget has room for them
    for ( const Server::PchCacheEntry & e : s.m_PchCache )
    {
        TEST_ASSERT( e.size == 5 );
    }

    // Scanned again with room for two of them, the newest are kept. What the cache holds is what the files come to.
    SetPchCacheLimits( s, 10, 1 );
    s.m_PchCache.Clear();
    s.m_PchCacheScanned = false;
    s.ScanPchCache();
    TEST_ASSERT( s_LastCacheBytes.Load() == ( kNumPchs * 5 ) );
    TEST_ASSERT( s.m_PchCache.GetSize() == 2 );
    AStackString<> path;
    TEST_ASSERT( s.FindCachedPch( 0xA004, path ) );
    TEST_ASSERT( s.FindCachedPch( 0xA005, path ) );
    for ( uint32_t i = 0; i < kNumPchs; ++i )
    {
        TEST_ASSERT( FileIO::FileExists( files[ i ].Get() ) == ( i >= 4 ) );
    }
}

// PchDataCache helpers
//------------------------------------------------------------------------------
/*static*/ void TestDistributed::MakeTestContent( uint32_t size, uint32_t seed, Array<char> & outContent )
{
    // Compressible, with some variation
    outContent.SetSize( size );
    for ( uint32_t i = 0; i < size; ++i )
    {
        outContent[ i ] = (char)( ( i % 251 ) ^ ( ( i / 4096 ) & 0xFF ) ^ seed );
    }
}

/*static*/ bool TestDistributed::MakeSmallFile( const AString & dir, const char * name, AString & outFile )
{
    outFile = dir;
    outFile += name;
    FileStream f;
    const char data[] = "data";
    return FileIO::EnsurePathExistsForFile( outFile ) &&
           f.Open( outFile.Get(), FileStream::WRITE_ONLY ) &&
           ( f.Write( data, sizeof( data ) ) == sizeof( data ) );
}

/*static*/ bool TestDistributed::WriteTestFile( const char * path, uint32_t size, Array<char> & outContent, uint32_t seed )
{
    MakeTestContent( size, seed, outContent );

    if ( FileIO::EnsurePathExistsForFile( AStackString<>( path ) ) == false )
    {
        return false;
    }
    FileStream f;
    return f.Open( path, FileStream::WRITE_ONLY ) && ( f.Write( outContent.Begin(), size ) == size );
}

/*static*/ PchDataCache::CompressedState TestDistributed::WaitForCompression( PchDataCache & cache,
                                                                              const PchDataCache::Entry & entry,
                                                                              AString & outFilePath,
                                                                              uint64_t & outSize )
{
    PchDataCache::CompressedState state = PchDataCache::COMPRESSING;
    WaitUntil( [ & ]()
    {
        state = cache.GetCompressed( entry, outFilePath, outSize );
        return ( state != PchDataCache::COMPRESSING );
    } );
    return state;
}

/*static*/ bool TestDistributed::LoadFile( const AString & path, Array<char> & outData )
{
    FileStream f;
    if ( f.Open( path.Get(), FileStream::READ_ONLY ) == false )
    {
        return false;
    }
    outData.SetSize( (size_t)f.GetFileSize() );
    return ( f.Read( outData.Begin(), (uint32_t)outData.GetSize() ) == outData.GetSize() );
}

// What a worker does with a PCH or toolchain file payload
/*static*/ bool TestDistributed::DecodesTo( const void * data, size_t dataSize, const Array<char> & content )
{
    Compressor c;
    return Compressor::IsValidData( data, dataSize ) &&
           c.Decompress( data ) &&
           ( c.GetResultSize() == content.GetSize() ) &&
           ( memcmp( c.GetResult(), content.Begin(), content.GetSize() ) == 0 );
}

/*static*/ bool TestDistributed::DecodesToFile( const AString & path, const Array<char> & content )
{
    Array<char> blob;
    return LoadFile( path, blob ) && DecodesTo( blob.Begin(), blob.GetSize(), content );
}

/*static*/ uint32_t TestDistributed::RequestCompression( void * param )
{
    CompressionRequest * request = static_cast<CompressionRequest *>( param );
    while ( request->m_Go->Load() == false )
    {
        Thread::Sleep( 0 );
    }
    request->m_State = WaitForCompression( *request->m_Cache, *request->m_Entry, request->m_FilePath, request->m_Size );
    return 0;
}

// PchCompressedCache
//------------------------------------------------------------------------------
void TestDistributed::PchCompressedCache() const
{
    PchDataCache cache;

    // Large enough that compression takes long enough for the requests to overlap
    Array<char> content;
    const AStackString<> path( "../tmp/Test/Distributed/PchCompressedCache/test.pch" );
    const uint32_t fileSize = ( 24 * MEGABYTE );
    TEST_ASSERT( WriteTestFile( path.Get(), fileSize, content ) );

    PchDataCache::Entry entry;
    entry.pchId = PchId::Calc( content.Begin(), fileSize ); // The id is the hash of the file
    entry.uncompressedSize = fileSize;
    entry.filePath = path;
    cache.Store( entry );

    // Several connections ask at once
    const uint32_t kNumRequesters = 8;
    Atomic<bool> go( false );
    CompressionRequest requests[ kNumRequesters ];
    Thread threads[ kNumRequesters ];
    for ( uint32_t i = 0; i < kNumRequesters; ++i )
    {
        requests[ i ].m_Cache = &cache;
        requests[ i ].m_Entry = &entry;
        requests[ i ].m_Go = &go;
        threads[ i ].Start( RequestCompression, "PchRequest", &requests[ i ] );
    }
    go.Store( true );
    for ( uint32_t i = 0; i < kNumRequesters; ++i )
    {
        threads[ i ].Join();
    }

    // One compression, one file shared by all of them
    TEST_ASSERT( cache.GetNumCompressions() == 1 );
    for ( const CompressionRequest & request : requests )
    {
        TEST_ASSERT( request.m_State == PchDataCache::READY );
        TEST_ASSERT( request.m_FilePath == requests[ 0 ].m_FilePath );
        TEST_ASSERT( request.m_Size == requests[ 0 ].m_Size );
    }
    TEST_ASSERT( requests[ 0 ].m_Size < fileSize );
    TEST_ASSERT( DecodesToFile( requests[ 0 ].m_FilePath, content ) );
}

// PchCompressRetry
//------------------------------------------------------------------------------
void TestDistributed::PchCompressRetry() const
{
    const uint32_t kMaxAttempts = 3;
    const uint32_t kRetryDelayMS = 300;
    PchDataCache cache( kMaxAttempts, kRetryDelayMS );

    // A PCH that cannot be read yet: the file appears after the first attempt failed
    {
        Array<char> content;
        const AStackString<> path( "../tmp/Test/Distributed/PchCompressRetry/appears.pch" );
        const uint32_t fileSize = ( 1 * MEGABYTE );
        FileIO::FileDelete( path.Get() );
        MakeTestContent( fileSize, 0, content );

        PchDataCache::Entry entry;
        entry.pchId = PchId::Calc( content.Begin(), fileSize );
        entry.uncompressedSize = fileSize;
        entry.filePath = path;
        cache.Store( entry );

        AStackString<> filePath;
        uint64_t size = 0;
        TEST_ASSERT( cache.GetCompressed( entry, filePath, size ) == PchDataCache::COMPRESSING );

        TEST_ASSERT( WaitUntil( [ & ]() { return ( cache.GetFailedAttempts( entry.pchId ) != 0 ); } ) );
        TEST_ASSERT( cache.GetFailedAttempts( entry.pchId ) == 1 );
        TEST_ASSERT( WriteTestFile( path.Get(), fileSize, content ) );

        TEST_ASSERT( WaitForCompression( cache, entry, filePath, size ) == PchDataCache::READY );
        TEST_ASSERT( DecodesToFile( filePath, content ) );
        TEST_ASSERT( cache.HasCompressionFailed( entry.pchId ) == false );
    }

    // A PCH shorter than recorded keeps failing: the retries are spaced out, bounded, and the failure stays
    {
        Array<char> content;
        const AStackString<> path( "../tmp/Test/Distributed/PchCompressRetry/short.pch" );
        TEST_ASSERT( WriteTestFile( path.Get(), 100, content ) );

        PchDataCache::Entry entry;
        entry.pchId = 0x2;
        entry.uncompressedSize = 1024;
        entry.filePath = path;
        cache.Store( entry );

        const Timer timer;
        AStackString<> filePath;
        uint64_t size = 0;
        TEST_ASSERT( WaitForCompression( cache, entry, filePath, size ) == PchDataCache::FAILED );
        TEST_ASSERT( timer.GetElapsed() >= ( ( kMaxAttempts - 1 ) * kRetryDelayMS * 0.001f * 0.9f ) );
        TEST_ASSERT( cache.GetFailedAttempts( entry.pchId ) == kMaxAttempts );
        TEST_ASSERT( cache.HasCompressionFailed( entry.pchId ) );

        // Asking again neither retries nor starts another compression
        TEST_ASSERT( cache.GetCompressed( entry, filePath, size ) == PchDataCache::FAILED );
        TEST_ASSERT( cache.GetFailedAttempts( entry.pchId ) == kMaxAttempts );
        TEST_ASSERT( cache.GetNumCompressions() == 2 );
    }
}

// PchTakeReadyPchs
//------------------------------------------------------------------------------
void TestDistributed::PchTakeReadyPchs() const
{
    PchDataCache cache( 2, 10 );

    Array<char> content;
    const AStackString<> goodPath( "../tmp/Test/Distributed/PchTakeReadyPchs/good.pch" );
    const AStackString<> goodTooPath( "../tmp/Test/Distributed/PchTakeReadyPchs/goodtoo.pch" );
    const AStackString<> shortPath( "../tmp/Test/Distributed/PchTakeReadyPchs/short.pch" );
    const uint32_t fileSize = ( 1 * MEGABYTE );
    TEST_ASSERT( WriteTestFile( goodPath.Get(), fileSize, content ) );
    Array<char> goodTooContent;
    TEST_ASSERT( WriteTestFile( goodTooPath.Get(), fileSize, goodTooContent, 1 ) );
    Array<char> shortContent;
    TEST_ASSERT( WriteTestFile( shortPath.Get(), 100, shortContent ) );

    // The id of a PCH is the hash of the file
    const uint64_t kGood = PchId::Calc( content.Begin(), fileSize );
    const uint64_t kGoodToo = PchId::Calc( goodTooContent.Begin(), fileSize );
    const uint64_t kUnreadable = 2;
    const uint64_t kUnknown = 3;
    const uint64_t kWorkerHasIt = 4;
    PchDataCache::Entry entry;
    entry.pchId = kGood;
    entry.uncompressedSize = fileSize;
    entry.filePath = goodPath;
    cache.Store( entry );
    entry.pchId = kGoodToo;
    entry.filePath = goodTooPath;
    cache.Store( entry );
    entry.pchId = kUnreadable;
    entry.uncompressedSize = 1024; // The file is shorter
    entry.filePath = shortPath;
    cache.Store( entry );
    entry.pchId = kWorkerHasIt;
    entry.uncompressedSize = fileSize;
    entry.filePath = goodPath;
    cache.Store( entry );

    // Have both readable ones compressed, so they are ready together
    AStackString<> filePath;
    uint64_t dataSize = 0;
    PchDataCache::Entry goodEntry;
    TEST_ASSERT( cache.Find( kGood, goodEntry ) );
    TEST_ASSERT( WaitForCompression( cache, goodEntry, filePath, dataSize ) == PchDataCache::READY );
    TEST_ASSERT( cache.Find( kGoodToo, goodEntry ) );
    TEST_ASSERT( WaitForCompression( cache, goodEntry, filePath, dataSize ) == PchDataCache::READY );

    // One still compressing does not hold up the PCHs after it, and stays pending
    Array<uint64_t> pending;
    pending.Append( kUnreadable );
    pending.Append( kGoodToo );
    pending.Append( kGood );
    Array<uint64_t> onWorker;
    Array<PchDataCache::ReadyPch> ready;
    TEST_ASSERT( cache.TakeReadyPchs( pending, onWorker, ready ) );
    TEST_ASSERT( pending.GetSize() == 1 );
    TEST_ASSERT( pending[ 0 ] == kUnreadable );

    // Ready ones come back in the order they were asked for
    TEST_ASSERT( ready.GetSize() == 2 );
    TEST_ASSERT( ready[ 0 ].pchId == kGoodToo );
    TEST_ASSERT( ready[ 1 ].pchId == kGood );
    TEST_ASSERT( ready[ 1 ].uncompressedSize == fileSize );
    TEST_ASSERT( DecodesToFile( ready[ 1 ].filePath, content ) );

    // The rest: the unreadable one is reported once then dropped, an unknown one and one the
    // worker has are dropped without being sent
    pending.Append( kUnknown );
    pending.Append( kWorkerHasIt );
    onWorker.Append( kWorkerHasIt ); // Reported in the worker's inventory
    bool allOk = true;
    TEST_ASSERT( WaitUntil( [ & ]()
    {
        if ( cache.TakeReadyPchs( pending, onWorker, ready ) == false )
        {
            allOk = false;
        }
        return pending.IsEmpty();
    } ) );
    TEST_ASSERT( allOk == false );
    TEST_ASSERT( ready.GetSize() == 2 );
}

// PchChangedAfterItWasRecorded
//------------------------------------------------------------------------------
void TestDistributed::PchChangedAfterItWasRecorded() const
{
    // The id of a PCH is the hash of its file. A file rewritten since it was recorded does not hash
    // to it, so it is not sent: a worker would refuse it, and would be sent the same data again.
    PchDataCache cache( 2, 10 );
    cache.SetTempDir( AStackString<>( "../tmp/Test/Distributed/PchChangedAfterItWasRecorded/tmp/" ) );
    Array<char> content;
    const AStackString<> path( "../tmp/Test/Distributed/PchChangedAfterItWasRecorded/test.pch" );
    const uint32_t fileSize = ( 1 * MEGABYTE );
    TEST_ASSERT( WriteTestFile( path.Get(), fileSize, content ) );

    PchDataCache::Entry entry;
    entry.pchId = PchId::Calc( content.Begin(), fileSize );
    entry.uncompressedSize = fileSize;
    entry.filePath = path;
    cache.Store( entry );

    // Rewritten to the same size
    Array<char> other;
    TEST_ASSERT( WriteTestFile( path.Get(), fileSize, other, 1 ) );

    AStackString<> filePath;
    uint64_t size = 0;
    TEST_ASSERT( WaitForCompression( cache, entry, filePath, size ) == PchDataCache::FAILED );
    TEST_ASSERT( cache.HasCompressionFailed( entry.pchId ) );

    // Nothing is left behind
    AStackString<> sendDir;
    TEST_ASSERT( cache.GetSendDirectory( sendDir ) );
    Array<AString> left;
    FileIO::GetFiles( sendDir, AStackString( "*" ), true, &left );
    TEST_ASSERT( left.IsEmpty() );
}

// PchIdStreamsLikeOneShot
//------------------------------------------------------------------------------
void TestDistributed::PchIdStreamsLikeOneShot() const
{
    // The id of a file read in pieces is the id of its content, whatever the sizes of the file and the pieces
    const uint32_t kPiece = FileChunks::kChunkSize;
    const uint32_t sizes[] = { 1, ( kPiece - 1 ), kPiece, ( kPiece + 1 ), ( 3 * kPiece ), ( 3 * kPiece ) + 7 };
    const AStackString<> path( "../tmp/Test/Distributed/PchIdStreamsLikeOneShot/test.bin" );
    for ( const uint32_t size : sizes )
    {
        Array<char> content;
        TEST_ASSERT( WriteTestFile( path.Get(), size, content ) );
        uint64_t id = 0;
        uint64_t fileSize = 0;
        TEST_ASSERT( PchId::CalcFile( path, id, fileSize ) );
        TEST_ASSERT( fileSize == size );
        TEST_ASSERT( id == PchId::Calc( content.Begin(), size ) );
    }

    // An empty file, and one that is not there
    {
        FileStream f;
        TEST_ASSERT( f.Open( path.Get(), FileStream::WRITE_ONLY ) );
    }
    uint64_t id = 0;
    uint64_t fileSize = 1;
    TEST_ASSERT( PchId::CalcFile( path, id, fileSize ) );
    TEST_ASSERT( fileSize == 0 );
    TEST_ASSERT( id == PchId::Calc( "", 0 ) );

    // The algorithm is XXH3-64: a worker built from another version has to agree
    TEST_ASSERT( PchId::Calc( "", 0 ) == 0x2d06800538d394c2ull );
    TEST_ASSERT( PchId::Calc( "abc", 3 ) == 0x78af5f94892f3950ull );
    Array<char> longer;
    longer.SetSize( 1280 );
    for ( size_t i = 0; i < longer.GetSize(); ++i )
    {
        longer[ i ] = (char)( i % 256 );
    }
    TEST_ASSERT( PchId::Calc( longer.Begin(), longer.GetSize() ) == 0x4844b009e164352eull );
    TEST_ASSERT( PchId::CalcFile( AStackString<>( "../tmp/Test/Distributed/PchIdStreamsLikeOneShot/missing.bin" ), id, fileSize ) == false );
}

// PchRecordReadsTheFileOnce
//------------------------------------------------------------------------------
namespace
{
    struct RecordRequest
    {
        PchDataCache *          m_Cache = nullptr;
        const AString *         m_Path = nullptr;
        Atomic<uint32_t> *      m_Arrived = nullptr;
        PchDataCache::Entry     m_Entry;
        bool                    m_Ok = false;
    };

    uint32_t RecordInThread( void * param )
    {
        RecordRequest * request = static_cast<RecordRequest *>( param );
        request->m_Arrived->Increment();
        request->m_Ok = request->m_Cache->Record( *request->m_Path, request->m_Entry );
        return 0;
    }

    // What a thread that is about to read the file waits for: every requester to have asked for it, and
    // then a moment for them to reach the cache. A thread that was not made to wait for the one reading
    // would read as well.
    struct ContendedRead
    {
        Atomic<uint32_t> *  m_Arrived = nullptr;
        uint32_t            m_NumRequesters = 0;
        Atomic<bool>        m_TimedOut{ false };
    };

    void WaitForRequesters( void * userData )
    {
        ContendedRead * contended = static_cast<ContendedRead *>( userData );
        const Timer timer;
        while ( contended->m_Arrived->Load() < contended->m_NumRequesters )
        {
            if ( timer.GetElapsed() > 10.0f )
            {
                contended->m_TimedOut.Store( true );
                return;
            }
            Thread::Sleep( 1 );
        }
        Thread::Sleep( 50 );
    }
}

void TestDistributed::PchRecordReadsTheFileOnce() const
{
    PchDataCache cache;
    Array<char> content;
    const AStackString<> path( "../tmp/Test/Distributed/PchRecordReadsTheFileOnce/test.pch" );
    const uint32_t fileSize = ( 8 * MEGABYTE );
    TEST_ASSERT( WriteTestFile( path.Get(), fileSize, content ) );
    const uint64_t expectedId = PchId::Calc( content.Begin(), fileSize );

    // Several jobs need the PCH at once, and the one that reads it is held until all have asked
    const uint32_t kNumRequesters = 8;
    Atomic<uint32_t> arrived( 0 );
    ContendedRead contended;
    contended.m_Arrived = &arrived;
    contended.m_NumRequesters = kNumRequesters;
    cache.SetHashHook( WaitForRequesters, &contended );
    RecordRequest requests[ kNumRequesters ];
    Thread threads[ kNumRequesters ];
    for ( uint32_t i = 0; i < kNumRequesters; ++i )
    {
        requests[ i ].m_Cache = &cache;
        requests[ i ].m_Path = &path;
        requests[ i ].m_Arrived = &arrived;
        threads[ i ].Start( RecordInThread, "PchRecord", &requests[ i ] );
    }
    for ( uint32_t i = 0; i < kNumRequesters; ++i )
    {
        threads[ i ].Join();
    }
    TEST_ASSERT( contended.m_TimedOut.Load() == false );

    // One read of the file, and all of them have what it gave
    TEST_ASSERT( cache.GetNumFilesHashed() == 1 );
    for ( const RecordRequest & request : requests )
    {
        TEST_ASSERT( request.m_Ok );
        TEST_ASSERT( request.m_Entry.pchId == expectedId );
        TEST_ASSERT( request.m_Entry.uncompressedSize == fileSize );
        TEST_ASSERT( request.m_Entry.filePath == path );
    }

    // Asking again does not read it again
    PchDataCache::Entry again;
    TEST_ASSERT( cache.Record( path, again ) );
    TEST_ASSERT( again.pchId == expectedId );
    TEST_ASSERT( cache.GetNumFilesHashed() == 1 );

    // A file that is not there, or is empty, is not recorded
    PchDataCache::Entry none;
    TEST_ASSERT( cache.Record( AStackString<>( "../tmp/Test/Distributed/PchRecordReadsTheFileOnce/missing.pch" ), none ) == false );
    const AStackString<> emptyPath( "../tmp/Test/Distributed/PchRecordReadsTheFileOnce/empty.pch" );
    {
        FileStream f;
        TEST_ASSERT( f.Open( emptyPath.Get(), FileStream::WRITE_ONLY ) );
    }
    TEST_ASSERT( cache.Record( emptyPath, none ) == false );
}

// PchCompressionAroundChunkSizes
//------------------------------------------------------------------------------
void TestDistributed::PchCompressionAroundChunkSizes() const
{
    // Read and compressed in pieces: the file the worker decodes is the PCH, whatever the sizes
    const uint32_t kPiece = FileChunks::kChunkSize;
    const uint32_t sizes[] = { 1, ( kPiece - 1 ), kPiece, ( kPiece + 1 ), ( 2 * kPiece ), ( 3 * kPiece ) + 5 };
    PchDataCache cache;
    for ( const uint32_t size : sizes )
    {
        Array<char> content;
        AStackString<> path;
        path.Format( "../tmp/Test/Distributed/PchCompressionAroundChunkSizes/%u.pch", size );
        TEST_ASSERT( WriteTestFile( path.Get(), size, content ) );

        PchDataCache::Entry entry;
        entry.pchId = PchId::Calc( content.Begin(), size );
        entry.uncompressedSize = size;
        entry.filePath = path;
        cache.Store( entry );

        AStackString<> filePath;
        uint64_t compressedSize = 0;
        TEST_ASSERT( WaitForCompression( cache, entry, filePath, compressedSize ) == PchDataCache::READY );
        TEST_ASSERT( DecodesToFile( filePath, content ) );

        // What compresses is sent compressed. One byte cannot, so it is kept as it is.
        if ( size == 1 )
        {
            TEST_ASSERT( compressedSize == ( size + Compressor::kHeaderSize ) );
        }
        else if ( size >= 1024 )
        {
            TEST_ASSERT( compressedSize < size );
        }
    }

    // Data that does not compress is kept as it is, with a header
    {
        Array<char> content;
        MakeRandomBytes( ( 2 * kPiece ) + 3, 7, content );
        const AStackString<> path( "../tmp/Test/Distributed/PchCompressionAroundChunkSizes/random.pch" );
        {
            FileStream f;
            TEST_ASSERT( f.Open( path.Get(), FileStream::WRITE_ONLY ) );
            TEST_ASSERT( f.Write( content.Begin(), (uint32_t)content.GetSize() ) == content.GetSize() );
        }
        PchDataCache::Entry entry;
        entry.pchId = PchId::Calc( content.Begin(), content.GetSize() );
        entry.uncompressedSize = (uint32_t)content.GetSize();
        entry.filePath = path;
        cache.Store( entry );

        AStackString<> filePath;
        uint64_t compressedSize = 0;
        TEST_ASSERT( WaitForCompression( cache, entry, filePath, compressedSize ) == PchDataCache::READY );
        TEST_ASSERT( compressedSize == ( content.GetSize() + Compressor::kHeaderSize ) );
        TEST_ASSERT( DecodesToFile( filePath, content ) );
    }
}

// PchCompressedFilesAreCleanedUp
//------------------------------------------------------------------------------
void TestDistributed::PchCompressedFilesAreCleanedUp() const
{
    const AStackString<> parent( "../tmp/Test/Distributed/PchCompressedFilesAreCleanedUp/tmp/" );
    PchDataCache cache;
    cache.SetTempDir( parent );
    AStackString<> dir;
    TEST_ASSERT( cache.GetSendDirectory( dir ) );
    Array<AString> old;
    FileIO::GetFiles( dir, AStackString( "*" ), true, &old );
    for ( const AString & file : old )
    {
        FileIO::FileDelete( file.Get() );
    }

    // What a process that crashed left, what another that is running may be using however old it is,
    // what this one is using, and what is not ours
    auto nameOf = [ & ]( AString & outName, uint32_t processId, uint32_t number )
    {
        outName.Format( "%s%u.%u%s", PchDataCache::kTempFilePrefix, processId, number, PchDataCache::kTempFileExtension );
    };
    AStackString<> name;
    AStackString<> gone;
    AStackString<> goneToo;
    AStackString<> running;
    AStackString<> own;
    AStackString<> unrelated;
    nameOf( name, GetIdOfNoProcess(), 1 );
    TEST_ASSERT( MakeSmallFile( dir, name.Get(), gone ) );
    nameOf( name, GetIdOfAnotherProcess(), 2 );
    TEST_ASSERT( MakeSmallFile( dir, name.Get(), running ) );
    nameOf( name, Process::GetCurrentId(), 3 );
    TEST_ASSERT( MakeSmallFile( dir, name.Get(), own ) );
    TEST_ASSERT( MakeSmallFile( dir, "notes.txt", unrelated ) );
#if defined( __WINDOWS__ )
    const uint64_t kDay = ( 24ULL * 3600ULL * 10000000ULL );
#else
    const uint64_t kDay = ( 24ULL * 3600ULL * 1000000000ULL );
#endif
    const uint64_t longAgo = ( Time::GetCurrentFileTime() - kDay );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( running, longAgo ) );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( own, longAgo ) );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( unrelated, longAgo ) );

    // A compressed PCH is a file here until the cache is cleared
    Array<char> content;
    const AStackString<> path( "../tmp/Test/Distributed/PchCompressedFilesAreCleanedUp/test.pch" );
    const uint32_t fileSize = ( 1 * MEGABYTE );
    TEST_ASSERT( WriteTestFile( path.Get(), fileSize, content ) );
    PchDataCache::Entry entry;
    entry.pchId = PchId::Calc( content.Begin(), fileSize );
    entry.uncompressedSize = fileSize;
    entry.filePath = path;
    cache.Store( entry );
    AStackString<> compressed;
    uint64_t compressedSize = 0;
    TEST_ASSERT( WaitForCompression( cache, entry, compressed, compressedSize ) == PchDataCache::READY );
    TEST_ASSERT( compressed.BeginsWith( dir ) ); // Where it was told to
    TEST_ASSERT( compressed.Find( PchDataCache::kTempFilePrefix ) );
    TEST_ASSERT( FileIO::FileExists( compressed.Get() ) );
    TEST_ASSERT( DecodesToFile( compressed, content ) );

    // The leftover of the process that is gone was removed when the directory was first used
    TEST_ASSERT( FileIO::FileExists( gone.Get() ) == false );
    TEST_ASSERT( FileIO::FileExists( running.Get() ) );
    TEST_ASSERT( FileIO::FileExists( own.Get() ) );
    TEST_ASSERT( FileIO::FileExists( unrelated.Get() ) );

    // That happens once: what appears later is not looked for by every compression
    nameOf( name, GetIdOfNoProcess(), 4 );
    TEST_ASSERT( MakeSmallFile( dir, name.Get(), goneToo ) );
    Array<char> second;
    const AStackString<> path2( "../tmp/Test/Distributed/PchCompressedFilesAreCleanedUp/test2.pch" );
    TEST_ASSERT( WriteTestFile( path2.Get(), fileSize, second, 3 ) );
    PchDataCache::Entry entry2;
    entry2.pchId = PchId::Calc( second.Begin(), fileSize );
    entry2.uncompressedSize = fileSize;
    entry2.filePath = path2;
    cache.Store( entry2 );
    AStackString<> compressed2;
    TEST_ASSERT( WaitForCompression( cache, entry2, compressed2, compressedSize ) == PchDataCache::READY );
    TEST_ASSERT( FileIO::FileExists( goneToo.Get() ) );

    cache.Clear();
    TEST_ASSERT( FileIO::FileExists( compressed.Get() ) == false );
    TEST_ASSERT( FileIO::FileExists( compressed2.Get() ) == false );
}

// PchCompressionNeedsAPlaceToWrite
//------------------------------------------------------------------------------
void TestDistributed::PchCompressionNeedsAPlaceToWrite() const
{
    // A directory that cannot be made, as a file is where it would go
    const AStackString<> blocker( "../tmp/Test/Distributed/PchCompressionNeedsAPlaceToWrite/blocker.txt" );
    {
        Array<char> ignored;
        TEST_ASSERT( WriteTestFile( blocker.Get(), 16, ignored ) );
    }
    PchDataCache cache( 2, 10 );
    cache.SetTempDir( AStackString<>( "../tmp/Test/Distributed/PchCompressionNeedsAPlaceToWrite/blocker.txt/sub/" ) );

    Array<char> content;
    const AStackString<> path( "../tmp/Test/Distributed/PchCompressionNeedsAPlaceToWrite/test.pch" );
    const uint32_t fileSize = ( 1 * MEGABYTE );
    TEST_ASSERT( WriteTestFile( path.Get(), fileSize, content ) );
    PchDataCache::Entry entry;
    entry.pchId = PchId::Calc( content.Begin(), fileSize );
    entry.uncompressedSize = fileSize;
    entry.filePath = path;
    cache.Store( entry );

    // It fails, as a PCH that cannot be read does, and leaves nothing behind
    AStackString<> filePath;
    uint64_t size = 0;
    TEST_ASSERT( WaitForCompression( cache, entry, filePath, size ) == PchDataCache::FAILED );
    TEST_ASSERT( cache.HasCompressionFailed( entry.pchId ) );
    TEST_ASSERT( cache.GetFailedAttempts( entry.pchId ) == 2 );
}

// PchStartupScanRemovesStaleTempFiles
//------------------------------------------------------------------------------
void TestDistributed::PchStartupScanRemovesStaleTempFiles() const
{
    // What a crashed worker left in a PCH directory next to the PCH it had
    const uint64_t kPchId = 0x7001;
    AStackString<> pchFile;
    VERIFY( FBuild::GetWorkerCacheDir( pchFile ) );
    pchFile.AppendFormat( "pch.%016" PRIx64 "%c%s", kPchId, (char)NATIVE_SLASH, PchBlob::kStoredFileName );
    AStackString<> dir;
    dir.Assign( pchFile.Get(), pchFile.FindLast( NATIVE_SLASH ) + 1 );
    Array<AString> old;
    FileIO::GetFiles( dir, AStackString( "*" ), true, &old );
    for ( const AString & file : old )
    {
        FileIO::FileDelete( file.Get() );
    }

    auto nameOf = [ & ]( AString & outName, uint32_t processId, uint32_t number )
    {
        outName.Format( "incoming.%u.%u.pch", processId, number );
    };
    AStackString<> name;
    AStackString<> pch;
    AStackString<> stale;
    AStackString<> running;
    AStackString<> own;
    AStackString<> unrelated;
    TEST_ASSERT( MakeSmallFile( dir, PchBlob::kStoredFileName, pch ) );
    nameOf( name, GetIdOfNoProcess(), 1 );
    TEST_ASSERT( MakeSmallFile( dir, name.Get(), stale ) );
    nameOf( name, GetIdOfAnotherProcess(), 2 );
    TEST_ASSERT( MakeSmallFile( dir, name.Get(), running ) );
    nameOf( name, Process::GetCurrentId(), 3 );
    TEST_ASSERT( MakeSmallFile( dir, name.Get(), own ) );
    TEST_ASSERT( MakeSmallFile( dir, "notes.txt", unrelated ) );

    Server s( 1 );

    // The PCH is registered, the leftover of a process that is gone is removed, and so is nothing
    // that a running process may still be writing, that is being written by this one, or that is not ours
    AStackString<> cachedPath;
    TEST_ASSERT( s.FindCachedPch( kPchId, cachedPath ) );
    TEST_ASSERT( s.m_PchCache.GetSize() == 1 );
    TEST_ASSERT( s.m_PchCache[ 0 ].size == 5 ); // "data" and its terminator
    TEST_ASSERT( FileIO::FileExists( pch.Get() ) );
    TEST_ASSERT( FileIO::FileExists( stale.Get() ) == false );
    TEST_ASSERT( FileIO::FileExists( running.Get() ) );
    TEST_ASSERT( FileIO::FileExists( own.Get() ) );
    TEST_ASSERT( FileIO::FileExists( unrelated.Get() ) );
}

// PchBudgetFollowsTheFreeSpace
//------------------------------------------------------------------------------
/*static*/ bool TestDistributed::GetTestPchCacheLimits( uint64_t cacheBytes, Server::PchCacheLimits & outLimits )
{
    s_LastCacheBytes.Store( cacheBytes );
    if ( s_PchCacheLimitsKnown == false )
    {
        return false;
    }
    outLimits = s_PchCacheLimits;
    return true;
}

/*static*/ void TestDistributed::SetPchCacheLimits( Server & server, uint64_t bytes, uint32_t minEntries, uint64_t rawBytes )
{
    s_PchCacheLimits = { bytes, minEntries, rawBytes };
    s_PchCacheLimitsKnown = true;
    server.m_PchCacheLimitsFunc = &GetTestPchCacheLimits;
}

/*static*/ void TestDistributed::SetPchCacheLimitsUnknown( Server & server )
{
    s_PchCacheLimitsKnown = false;
    server.m_PchCacheLimitsFunc = &GetTestPchCacheLimits;
}

void TestDistributed::PchBudgetFollowsTheFreeSpace() const
{
    const uint64_t GiB = ( 1024ull * 1024 * 1024 );
    {
        Server s( 1 );

        // What the server is told is what it keeps to, for the bytes it says the cache holds
        SetPchCacheLimits( s, 123456, 2 );
        Server::PchCacheLimits limits = s.GetPchCacheLimits( 777 );
        TEST_ASSERT( limits.m_Bytes == 123456 );
        TEST_ASSERT( limits.m_MinEntries == 2 );
        TEST_ASSERT( s_LastCacheBytes.Load() == 777 );

        // When the free space cannot be read, the cache is kept small: more than the smallest cache may be too much
        SetPchCacheLimitsUnknown( s );
        limits = s.GetPchCacheLimits( 5 * GiB );
        TEST_ASSERT( limits.m_Bytes == Server::kMinPchCacheBytes );
        TEST_ASSERT( limits.m_MinEntries == Server::kMinCachedPchEntries );
        TEST_ASSERT( limits.m_RawBytes == 0 ); // Nothing is kept unpacked for later
    }

    // The disk of the cache is read when nothing replaces it
    {
        Server real( 1 );
        const Server::PchCacheLimits limits = real.GetPchCacheLimits( 0 );
        TEST_ASSERT( limits.m_Bytes >= Server::kMinPchCacheBytes );
        TEST_ASSERT( limits.m_Bytes <= Server::kMaxPchCacheBytes );
        TEST_ASSERT( limits.m_MinEntries == Server::kMinCachedPchEntries );
        TEST_ASSERT( limits.m_RawBytes <= Server::kMaxRawPchBytes );
    }

    // The bytes the cache holds count towards what it may keep: 6 PCHs of 1.5 GiB on a volume with 10 GiB
    // free are 9 GiB of a possible 19, of which half is capped at 8 GiB. Not counting them, it would be 5 GiB.
    Array<Server::PchCacheEntry> entries;
    for ( uint32_t i = 0; i < 6; ++i )
    {
        entries.Append( Server::PchCacheEntry{ 1 + i, AString( "dummy" ), (uint64_t)( 100 + i ), ( 3 * GiB ) / 2 } );
    }
    const uint64_t budget = Server::CalculatePchCacheBudget( 10 * GiB, Server::CountPchBytes( entries ) );
    Server::EvictPchs( entries, budget, 1, Array<uint64_t>() );
    TEST_ASSERT( entries.GetSize() == 5 );
    TEST_ASSERT( entries[ 0 ].pchId == 2 ); // The oldest went
}

// PchEvictionRemovesTheDirectory
//------------------------------------------------------------------------------
void TestDistributed::PchEvictionRemovesTheDirectory() const
{
    // Two more PCHs than the budget has room for, the two oldest first
    AStackString<> root;
    VERIFY( FBuild::GetWorkerCacheDir( root ) );
    const uint32_t kMaxPchs = 20;
    const uint32_t kNumPchs = ( kMaxPchs + 2 );
    Array<Server::PchCacheEntry> entries;
    Array<AString> dirs;
    for ( uint32_t i = 0; i < kNumPchs; ++i )
    {
        AStackString<> dir;
        dir.Format( "%spch.%016" PRIx64 "%c", root.Get(), (uint64_t)( 0x8000 + i ), (char)NATIVE_SLASH );
        AStackString<> file( dir );
        file += PchBlob::kStoredFileName;
        TEST_ASSERT( FileIO::EnsurePathExistsForFile( file ) );
        FileStream f;
        TEST_ASSERT( f.Open( file.Get(), FileStream::WRITE_ONLY ) );
        const char data[] = "data";
        TEST_ASSERT( f.Write( data, sizeof( data ) ) == sizeof( data ) );

        Server::PchCacheEntry entry;
        entry.pchId = ( 0x8000 + i );
        entry.filePath = file;
        entry.lastWriteTime = ( 1000 + i );
        entry.size = 1;
        entries.Append( entry );
        dirs.Append( dir );
    }

    Server::EvictPchs( entries, kMaxPchs, 0, Array<uint64_t>() );

    // The oldest are gone, with their directories, and the rest are as they were
    TEST_ASSERT( entries.GetSize() == kMaxPchs );
    for ( uint32_t i = 0; i < kNumPchs; ++i )
    {
        const bool evicted = ( i < 2 );
        AStackString<> file( dirs[ i ] );
        file += PchBlob::kStoredFileName;
        TEST_ASSERT( FileIO::FileExists( file.Get() ) == ( evicted == false ) );
        TEST_ASSERT( FileIO::DirectoryExists( dirs[ i ] ) == ( evicted == false ) );
    }
}

// PchStartupScanRemovesEmptyDirectories
//------------------------------------------------------------------------------
void TestDistributed::PchStartupScanRemovesEmptyDirectories() const
{
    AStackString<> root;
    VERIFY( FBuild::GetWorkerCacheDir( root ) );
    auto makeDir = [ & ]( uint64_t pchId, const char * fileName, AString & outDir )
    {
        outDir.Format( "%spch.%016" PRIx64 "%c", root.Get(), pchId, (char)NATIVE_SLASH );
        TEST_ASSERT( FileIO::EnsurePathExists( outDir ) );
        if ( fileName )
        {
            AStackString<> file( outDir );
            file += fileName;
            FileStream f;
            TEST_ASSERT( f.Open( file.Get(), FileStream::WRITE_ONLY ) );
            const char data[] = "data";
            TEST_ASSERT( f.Write( data, sizeof( data ) ) == sizeof( data ) );
        }
    };

    // What evicted and crashed workers leave: an empty PCH directory, one with a PCH, and one with a
    // PCH being received by another process
    AStackString<> emptyDir;
    AStackString<> pchDir;
    AStackString<> receivingDir;
    AStackString<> otherDir;
    AStackString<> receivingName;
    receivingName.Format( "incoming.%u.1.pch", GetIdOfAnotherProcess() ); // A process that is running
    makeDir( 0x9001, nullptr, emptyDir );
    makeDir( 0x9002, PchBlob::kStoredFileName, pchDir );
    makeDir( 0x9003, receivingName.Get(), receivingDir );
    otherDir.Format( "%sother%c", root.Get(), (char)NATIVE_SLASH ); // Not a PCH directory
    TEST_ASSERT( FileIO::EnsurePathExists( otherDir ) );

    Server s( 1 );

    TEST_ASSERT( FileIO::DirectoryExists( emptyDir ) == false );
    TEST_ASSERT( FileIO::DirectoryExists( pchDir ) );
    TEST_ASSERT( FileIO::DirectoryExists( receivingDir ) );
    TEST_ASSERT( FileIO::DirectoryExists( otherDir ) );
    AStackString<> cachedPath;
    TEST_ASSERT( s.FindCachedPch( 0x9002, cachedPath ) );
}

// DistributionStatsLog
//------------------------------------------------------------------------------
void TestDistributed::DistributionStatsLog() const
{
    // Format
    {
        DistributionConnectionStats stats;
        stats.m_WorkerAddress = "10.0.0.7";
        stats.m_ConnectedTime = 12.5;
        stats.m_Toolchain.m_Messages = 18;
        stats.m_Toolchain.m_RawBytes = ( 40 * (uint64_t)MEGABYTE );
        stats.m_Toolchain.m_SentBytes = ( 2 * (uint64_t)MEGABYTE );
        stats.m_Toolchain.m_SendTime = 1.0;
        stats.m_Pch.m_Messages = 1;
        stats.m_Pch.m_SentBytes = ( 1 * (uint64_t)MEGABYTE );
        stats.m_Pch.m_SendTime = 0.5;
        stats.m_Jobs.m_Messages = 3;
        stats.m_Jobs.m_RawBytes = ( 24 * (uint64_t)MEGABYTE );
        stats.m_Jobs.m_SentBytes = ( 3 * (uint64_t)MEGABYTE );
        stats.m_Jobs.m_SendTime = 0.5;
        stats.m_Jobs.m_EncodeTime = 0.4;
        stats.m_Jobs.m_MaxQueueDelay = 0.2;
        stats.m_NumSkippedToolchains = 2;
        stats.m_ResultsReceived = 3;
        stats.m_ResultBytesReceived = ( 1 * (uint64_t)MEGABYTE );
        stats.m_ResultRawBytes = ( 4 * (uint64_t)MEGABYTE );

        AStackString<> text;
        DistributionStats::Format( stats, "2026-01-02 03:04:05", text );
        TEST_ASSERT( text.Find( "[2026-01-02 03:04:05] worker 10.0.0.7, connected 12.5s" ) );
        TEST_ASSERT( text.Find( "toolchain: 18 msgs, 40.0 MB raw, 2.0 MB sent" ) );
        TEST_ASSERT( text.Find( "jobs:      3 msgs, 24.0 MB raw, 3.0 MB sent, send time 0.5s, encode time 0.4s, max wait 0.2s, done at" ) );
        TEST_ASSERT( text.Find( "pch:       1 msgs, 0.0 KB raw, 1.0 MB sent, send time 0.5s, done at" ) );
        TEST_ASSERT( text.Find( "toolchain: 2 cached on the worker, not sent" ) );
        TEST_ASSERT( text.Find( "results:   3 received, 4.0 MB raw, 1.0 MB received" ) );
        TEST_ASSERT( text.Find( "upload:    6.0 MB in 2.0s of send time = 3.00 MB/s (24.0 Mbit/s)" ) );
    }

    // No send time: no rate rather than a division by zero
    {
        DistributionConnectionStats stats;
        stats.m_WorkerAddress = "10.0.0.7";
        stats.m_Jobs.m_SentBytes = 1000;
        AStackString<> text;
        DistributionStats::Format( stats, "t", text );
        TEST_ASSERT( text.Find( "= 0.00 MB/s (0.0 Mbit/s)" ) );

        // Small amounts show in KB, not as 0.0 MB
        TEST_ASSERT( text.Find( "0.0 KB raw, 1.0 KB sent" ) );
        TEST_ASSERT( text.Find( "cached on the worker" ) == nullptr );
    }

    // The log moves to <path>.old instead of growing past its limit
    {
        const AStackString<> path( "../tmp/Test/Distributed/DistributionStatsLog/stats.log" );
        const AStackString<> oldPath( "../tmp/Test/Distributed/DistributionStatsLog/stats.log.old" );
        TEST_ASSERT( FileIO::EnsurePathExistsForFile( path ) );
        FileIO::FileDelete( path.Get() );
        FileIO::FileDelete( oldPath.Get() );

        const uint64_t kMaxLogSize = 250;
        const char kBlocks[] = { 'a', 'b', 'c', 'd', 'e' };
        for ( const char b : kBlocks )
        {
            AStackString<> block;
            for ( uint32_t i = 0; i < 100; ++i )
            {
                block += b;
            }
            TEST_ASSERT( DistributionStats::AppendToLog( path, block, kMaxLogSize ) );
        }

        // c and d fit together, e would pass the limit: the log holds e, .old holds c and d
        auto readFile = []( const AString & file, AString & outContent )
        {
            FileStream f;
            if ( f.Open( file.Get(), FileStream::READ_ONLY ) == false )
            {
                return false;
            }
            Array<char> bytes;
            bytes.SetSize( (size_t)f.GetFileSize() );
            if ( f.Read( bytes.Begin(), bytes.GetSize() ) != bytes.GetSize() )
            {
                return false;
            }
            outContent.Assign( bytes.Begin(), bytes.Begin() + bytes.GetSize() );
            return true;
        };
        AStackString<> log;
        AStackString<> old;
        TEST_ASSERT( readFile( path, log ) );
        TEST_ASSERT( readFile( oldPath, old ) );
        TEST_ASSERT( log.GetLength() == 100 );
        TEST_ASSERT( log[ 0 ] == 'e' );
        TEST_ASSERT( old.GetLength() == 200 );
        TEST_ASSERT( old[ 0 ] == 'c' );
        TEST_ASSERT( old[ 199 ] == 'd' );
    }
}

// PCH sending
//------------------------------------------------------------------------------
namespace
{
    // Sends the chunks of a PCH to a worker, as a coordinator does
    class PchSender : public TCPConnectionPool
    {
    public:
        PchSender() = default;
        virtual ~PchSender() override { ShutdownAllConnections(); }
        explicit PchSender( const PchSender & other ) = delete;
        PchSender & operator=( const PchSender & other ) = delete;

        bool ConnectToWorker()
        {
            m_Connection = Connect( AStackString<>( "127.0.0.1" ), Protocol::kTestPort, 2000 );
            return ( m_Connection != nullptr );
        }

        void DisconnectFromWorker()
        {
            Disconnect( m_Connection );
            m_Connection = nullptr;
        }

        // The chunks from firstChunk up to, not including, endChunk
        bool SendChunks( uint64_t pchId,
                         uint32_t uncompressedSize,
                         const void * compressedData,
                         uint32_t compressedSize,
                         uint32_t chunkSize,
                         uint32_t firstChunk,
                         uint32_t endChunk )
        {
            for ( uint32_t i = firstChunk; i < endChunk; ++i )
            {
                const Protocol::MsgPchFile msg( pchId, uncompressedSize, compressedSize, chunkSize, i );
                const ConstMemoryStream payload( static_cast<const char *>( compressedData ) + ( (size_t)i * chunkSize ),
                                                 Protocol::MsgPchFile::GetChunkLength( compressedSize, chunkSize, i ) );
                if ( msg.Send( m_Connection, payload ) == false )
                {
                    return false;
                }
            }
            return true;
        }

    private:
        const ConnectionInfo * m_Connection = nullptr;
    };

    // A PCH as a client sends it
    struct CompressedPch
    {
        CompressedPch() = default;
        explicit CompressedPch( const CompressedPch & other ) = delete;
        CompressedPch & operator=( const CompressedPch & other ) = delete;

        Array<char>     m_Content;
        Compressor      m_Compressed;
        uint32_t        m_CompressedSize = 0;
        uint64_t        m_Id = 0;
    };

    // Where a worker keeps a PCH
    void GetWorkerPchDir( uint64_t pchId, AString & outDir )
    {
        VERIFY( FBuild::GetWorkerCacheDir( outDir ) );
        outDir.AppendFormat( "pch.%016" PRIx64 "%c", pchId, (char)NATIVE_SLASH );
    }
}

// PchAndToolchainPayloadsAcceptedByWorker
//------------------------------------------------------------------------------
void TestDistributed::PayloadsAcceptedByWorker() const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    Array<char> content;
    const AStackString<> path( "../tmp/Test/Distributed/PayloadsAcceptedByWorker/test.pch" );
    const uint32_t fileSize = ( 2 * MEGABYTE );
    TEST_ASSERT( WriteTestFile( path.Get(), fileSize, content ) );

    // The sender's payload for a PCH goes through the worker's receive path. The worker's cache
    // starts empty, so it has to decode the PCH. A PCH's id is the hash of its content.
    PchDataCache cache;
    PchDataCache::Entry entry;
    entry.pchId = PchId::Calc( content.Begin(), fileSize );
    entry.uncompressedSize = fileSize;
    entry.filePath = path;
    cache.Store( entry );
    AStackString<> compressedPath;
    uint64_t dataSize = 0;
    TEST_ASSERT( WaitForCompression( cache, entry, compressedPath, dataSize ) == PchDataCache::READY );
    Array<char> data;
    TEST_ASSERT( LoadFile( compressedPath, data ) );

    Server worker( 1 );
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
    PchSender sender;
    TEST_ASSERT( sender.ConnectToWorker() );
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)dataSize, Protocol::kPchChunkSize );
    TEST_ASSERT( sender.SendChunks( entry.pchId, fileSize, data.Begin(), (uint32_t)dataSize, Protocol::kPchChunkSize, 0, numChunks ) );

    AStackString<> cachedPath;
    TEST_ASSERT( WaitUntil( [ & ]() { return worker.FindCachedPch( entry.pchId, cachedPath ); } ) );

    // The worker keeps it as it was sent, compressed
    {
        FileStream f;
        TEST_ASSERT( f.Open( cachedPath.Get(), FileStream::READ_ONLY ) );
        TEST_ASSERT( f.GetFileSize() == dataSize );
        Array<char> onWorker;
        onWorker.SetSize( dataSize );
        TEST_ASSERT( f.Read( onWorker.Begin(), dataSize ) == dataSize );
        TEST_ASSERT( memcmp( onWorker.Begin(), data.Begin(), dataSize ) == 0 );
    }
    TEST_ASSERT( DecodesToFile( cachedPath, content ) );
    FileIO::FileDelete( cachedPath.Get() );
    FileIO::DirectoryDelete( AStackString<>( cachedPath.Get(), cachedPath.FindLast( NATIVE_SLASH ) ) );

    // A PCH that does not compress is sent as the file holds it, in several chunks
    {
        Array<char> random;
        MakeRandomBytes( ( 3 * MEGABYTE ) + 777, 5, random );
        const AStackString<> randomPath( "../tmp/Test/Distributed/PayloadsAcceptedByWorker/random.pch" );
        {
            FileStream f;
            TEST_ASSERT( FileIO::EnsurePathExistsForFile( randomPath ) );
            TEST_ASSERT( f.Open( randomPath.Get(), FileStream::WRITE_ONLY ) );
            TEST_ASSERT( f.Write( random.Begin(), (uint32_t)random.GetSize() ) == random.GetSize() );
        }
        PchDataCache::Entry randomEntry;
        randomEntry.pchId = PchId::Calc( random.Begin(), random.GetSize() );
        randomEntry.uncompressedSize = (uint32_t)random.GetSize();
        randomEntry.filePath = randomPath;
        cache.Store( randomEntry );
        AStackString<> randomCompressed;
        uint64_t randomDataSize = 0;
        TEST_ASSERT( WaitForCompression( cache, randomEntry, randomCompressed, randomDataSize ) == PchDataCache::READY );
        Array<char> randomData;
        TEST_ASSERT( LoadFile( randomCompressed, randomData ) );
        const uint32_t randomChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)randomDataSize, Protocol::kPchChunkSize );
        TEST_ASSERT( randomChunks >= 4 );
        TEST_ASSERT( sender.SendChunks( randomEntry.pchId, randomEntry.uncompressedSize, randomData.Begin(), (uint32_t)randomDataSize, Protocol::kPchChunkSize, 0, randomChunks ) );
        AStackString<> randomCached;
        TEST_ASSERT( WaitUntil( [ & ]() { return worker.FindCachedPch( randomEntry.pchId, randomCached ); } ) );
        FileStream f;
        TEST_ASSERT( f.Open( randomCached.Get(), FileStream::READ_ONLY ) );
        TEST_ASSERT( f.GetFileSize() == randomDataSize );
        Array<char> randomOnWorker;
        randomOnWorker.SetSize( randomDataSize );
        TEST_ASSERT( f.Read( randomOnWorker.Begin(), randomOnWorker.GetSize() ) == randomOnWorker.GetSize() );
        TEST_ASSERT( memcmp( randomOnWorker.Begin(), randomData.Begin(), randomDataSize ) == 0 );
        f.Close();
        TEST_ASSERT( DecodesToFile( randomCached, random ) );
    }

    // The same for a toolchain file: what the sender stores is what ReceiveFileData decodes
    const ToolManifestFile toolFile( AStackString<>( "tool.exe" ), 1, 1, fileSize );
    toolFile.StoreCompressedContent( content.Begin(), fileSize );
    size_t toolDataSize = 0;
    const void * toolData = toolFile.GetFileData( toolDataSize );
    TEST_ASSERT( toolData );
    TEST_ASSERT( toolDataSize < fileSize );
    TEST_ASSERT( DecodesTo( toolData, toolDataSize, content ) );
}

// PchChunksReachTheWorker
//------------------------------------------------------------------------------
void TestDistributed::PchChunksReachTheWorker() const
{
    // A PCH of several chunks, through a connection as a coordinator makes one
    Array<char> content;
    MakeRandomBytes( ( 3 * MEGABYTE ) + 777, 1, content );
    Compressor compressed;
    compressed.CompressZstd( content.Begin(), content.GetSize(), Compressor::kZstdTransferLevel );
    const uint32_t compressedSize = (uint32_t)compressed.GetResultSize();
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( compressedSize, Protocol::kPchChunkSize );
    TEST_ASSERT( numChunks >= 4 );
    const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );

    Server worker( 1 );
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
    PchSender sender;
    TEST_ASSERT( sender.ConnectToWorker() );
    TEST_ASSERT( sender.SendChunks( pchId, (uint32_t)content.GetSize(), compressed.GetResult(), compressedSize, Protocol::kPchChunkSize, 0, numChunks ) );

    AStackString<> cachedPath;
    TEST_ASSERT( WaitUntil( [ & ]() { return worker.FindCachedPch( pchId, cachedPath ); } ) );
    FileStream f;
    TEST_ASSERT( f.Open( cachedPath.Get(), FileStream::READ_ONLY ) );
    TEST_ASSERT( f.GetFileSize() == compressedSize );
    Array<char> onWorker;
    onWorker.SetSize( compressedSize );
    TEST_ASSERT( f.Read( onWorker.Begin(), onWorker.GetSize() ) == onWorker.GetSize() );
    TEST_ASSERT( memcmp( onWorker.Begin(), compressed.GetResult(), compressedSize ) == 0 );
    f.Close();
    TEST_ASSERT( DecodesToFile( cachedPath, content ) );
}

// PchLeftHalfSentIsDropped
//------------------------------------------------------------------------------
void TestDistributed::PchLeftHalfSentIsDropped() const
{
    Array<char> content;
    MakeRandomBytes( ( 3 * MEGABYTE ) + 777, 2, content );
    Compressor compressed;
    compressed.CompressZstd( content.Begin(), content.GetSize(), Compressor::kZstdTransferLevel );
    const uint32_t compressedSize = (uint32_t)compressed.GetResultSize();
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( compressedSize, Protocol::kPchChunkSize );
    TEST_ASSERT( numChunks >= 4 );
    const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
    AStackString<> pchDir;
    GetWorkerPchDir( pchId, pchDir );

    Server worker( 1 );
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
    auto numClients = [ &worker ]()
    {
        MutexHolder mh( worker.m_ClientListMutex );
        return worker.m_ClientList.GetSize();
    };

    // The connection ends with half of the PCH sent
    {
        PchSender sender;
        TEST_ASSERT( sender.ConnectToWorker() );
        TEST_ASSERT( WaitUntil( [ & ]() { return numClients() == 1; } ) );
        TEST_ASSERT( sender.SendChunks( pchId, (uint32_t)content.GetSize(), compressed.GetResult(), compressedSize, Protocol::kPchChunkSize, 0, 2 ) );

        // Both chunks have been taken in by the worker
        TEST_ASSERT( WaitUntil( [ & ]()
        {
            MutexHolder mh( worker.m_ClientListMutex );
            return ( worker.m_ClientList.GetSize() == 1 ) && ( worker.m_ClientList[ 0 ]->m_PchReceiver.GetNumChunksReceived() >= 2 );
        } ) );

        sender.DisconnectFromWorker();
        TEST_ASSERT( WaitUntil( [ & ]() { return numClients() == 0; } ) );
    }

    // Nothing is on disk, and the worker does not have the PCH
    Array<AString> files;
    FileIO::GetFiles( pchDir, AStackString( "*" ), true, &files );
    TEST_ASSERT( files.IsEmpty() );
    AStackString<> cachedPath;
    TEST_ASSERT( worker.FindCachedPch( pchId, cachedPath ) == false );

    // It is sent again from the start
    PchSender sender;
    TEST_ASSERT( sender.ConnectToWorker() );
    TEST_ASSERT( sender.SendChunks( pchId, (uint32_t)content.GetSize(), compressed.GetResult(), compressedSize, Protocol::kPchChunkSize, 0, numChunks ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return worker.FindCachedPch( pchId, cachedPath ); } ) );
}

// PchInChunksThroughTheBuild
//------------------------------------------------------------------------------
void TestDistributed::PchInChunksThroughTheBuild() const
{
    // The PCH of the library is sent in pieces far smaller than usual
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // The jobs need the PCH on the worker
    options.m_DistributionPchChunkSize = Protocol::kMinPchChunkSize;
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    Array<AString> files;
    FileIO::GetFiles( AStackString( "../tmp/Test/Distributed" ), AStackString( "*.*" ), true, &files );
    for ( const AString & file : files )
    {
        FileIO::FileDelete( file.Get() );
    }

    const char * target( "../tmp/Test/Distributed/distpch.lib" );
    {
        Server worker( 1 );
        TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
        TEST_ASSERT( fBuild.Build( target ) );
        TEST_ASSERT( FileIO::FileExists( target ) );
    }

    // It went in several messages
    AStackString<> logPath;
    TEST_ASSERT( FBuild::GetTempDir( logPath ) );
    logPath += "FBuild-distribution-stats.log";
    uint32_t numConnections = 0;
    uint32_t numMessages = 0;
    uint64_t rawBytes = 0;
    uint64_t sentBytes = 0;
    ReadStats( logPath, "  pch: ", numConnections, rawBytes, sentBytes, &numMessages );
    TEST_ASSERT( numConnections == 1 );
    TEST_ASSERT( numMessages >= 3 );

    // Nothing is left of the pieces on the worker
    AStackString<> cacheDir;
    TEST_ASSERT( FBuild::GetWorkerCacheDir( cacheDir ) );
    Array<AString> cached;
    FileIO::GetFiles( cacheDir, AStackString( "*" ), true, &cached );
    uint32_t numPchs = 0;
    for ( const AString & file : cached )
    {
        if ( file.EndsWith( PchBlob::kStoredFileName ) )
        {
            ++numPchs;
        }
        else if ( file.EndsWith( ".pch" ) )
        {
            // A working copy may be there still. A piece of a PCH that was being received or unpacked is not.
            TEST_ASSERT( file.EndsWith( PchBlob::kRawFileName ) );
        }
    }
    TEST_ASSERT( numPchs >= 1 );
}

// ToolchainEviction
//------------------------------------------------------------------------------
void TestDistributed::ToolchainEviction() const
{
    Server s( 1 );

    // More toolchains than a worker keeps, the least recently used first
    const uint32_t kNumToolchains = ( Server::kMaxCachedToolchains + 3 );
    const uint64_t kFirstToolId = 0x1000;
    Array<AString> toolFiles;
    for ( uint32_t i = 0; i < kNumToolchains; ++i )
    {
        AStackString<> content;
        content.Format( "compiler %u", i );
        ToolManifest * manifest = SyntheticToolchain::Create( kFirstToolId + i, content.Get() );
        TEST_ASSERT( manifest );
        manifest->MarkUsed();
        AStackString<> toolFile;
        manifest->GetRemoteFilePath( 0, toolFile );
        toolFiles.Append( toolFile );
        {
            MutexHolder mh( s.m_ToolManifestsMutex );
            s.m_Tools.Append( manifest );
        }
        Thread::Sleep( 20 ); // File times have a limited resolution
    }

    // The least recently used is being used by a job. The next one was only just received.
    ToolManifest * inUse = s.m_Tools[ 0 ];
    inUse->AcquireUse();
    const ToolManifest * justReceived = s.m_Tools[ 1 ];
    s.EvictToolchains( justReceived );

    // Three had to go: the least recently used that were neither
    auto kept = [ &s ]( uint64_t toolId )
    {
        MutexHolder mh( s.m_ToolManifestsMutex );
        return ( s.m_Tools.FindDeref( toolId ) != nullptr );
    };
    for ( uint32_t i = 0; i < kNumToolchains; ++i )
    {
        const bool shouldBeKept = ( i < 2 ) || ( i >= 5 );
        TEST_ASSERT( kept( kFirstToolId + i ) == shouldBeKept );
        TEST_ASSERT( FileIO::FileExists( toolFiles[ i ].Get() ) == shouldBeKept ); // What is evicted is also removed from disk
    }

    inUse->ReleaseUse();
}

// ToolchainCacheStartupScan
//------------------------------------------------------------------------------
void TestDistributed::ToolchainCacheStartupScan() const
{
    // What a worker left on disk: more toolchains than it keeps, one of them modified since,
    // and the directory of a toolchain that another worker is still receiving
    const uint32_t kNumToolchains = ( Server::kMaxCachedToolchains + 2 );
    const uint64_t kFirstToolId = 0x3000;
    const uint32_t kModified = ( kNumToolchains - 2 );  // Among the most recent
    const uint32_t kOldest = 0;
    Array<AString> toolFiles;
    Array<AString> manifestFiles;
    for ( uint32_t i = 0; i < kNumToolchains; ++i )
    {
        AStackString<> content;
        content.Format( "compiler %u", i );
        UniquePtr<ToolManifest> manifest( SyntheticToolchain::Create( kFirstToolId + i, content.Get() ) );
        TEST_ASSERT( manifest.Get() && manifest->SaveToDisk() );

        AStackString<> toolFile;
        manifest->GetRemoteFilePath( 0, toolFile );
        toolFiles.Append( toolFile );
        AStackString<> manifestFile;
        manifest->GetPersistedPath( manifestFile );
        manifestFiles.Append( manifestFile );
    } // The files are no longer held open

    // The more recent, the later the manifest was written
    const uint64_t kStep = 1000000000ULL;
    const uint64_t base = FileIO::GetFileLastWriteTime( manifestFiles[ 0 ] );
    for ( uint32_t i = 0; i < kNumToolchains; ++i )
    {
        TEST_ASSERT( FileIO::SetFileLastWriteTime( manifestFiles[ i ], base + ( i * kStep ) ) );
    }

    Array<char> content;
    {
        FileStream in;
        TEST_ASSERT( in.Open( toolFiles[ kModified ].Get() ) );
        content.SetSize( (size_t)in.GetFileSize() );
        TEST_ASSERT( in.Read( content.Begin(), content.GetSize() ) == content.GetSize() );
    }
    content[ 2 ] ^= 0x55;
    {
        FileStream out;
        TEST_ASSERT( out.Open( toolFiles[ kModified ].Get(), FileStream::WRITE_ONLY ) );
        TEST_ASSERT( out.Write( content.Begin(), content.GetSize() ) == content.GetSize() );
    }

    const ToolManifest receiving( 0x3F00 );
    AStackString<> receivingFile;
    receiving.GetRemotePath( receivingFile );
    TEST_ASSERT( FileIO::EnsurePathExists( receivingFile ) );
    receivingFile += "tool.exe";
    {
        FileStream f;
        TEST_ASSERT( f.Open( receivingFile.Get(), FileStream::WRITE_ONLY ) );
    }

    Server s( 1 );

    // The most recent that are intact are loaded: the modified one does not count towards what is kept
    auto isLoaded = [ &s ]( uint64_t toolId )
    {
        MutexHolder mh( s.m_ToolManifestsMutex );
        return ( s.m_Tools.FindDeref( toolId ) != nullptr );
    };
    uint32_t numLoaded = 0;
    for ( uint32_t i = 0; i < kNumToolchains; ++i )
    {
        const bool shouldBeLoaded = ( i != kModified ) && ( i != kOldest );
        TEST_ASSERT( isLoaded( kFirstToolId + i ) == shouldBeLoaded );
        numLoaded += ( shouldBeLoaded ? 1 : 0 );

        // What is not loaded is removed, so it does not take up disk space for ever
        TEST_ASSERT( FileIO::FileExists( toolFiles[ i ].Get() ) == shouldBeLoaded );
        TEST_ASSERT( FileIO::FileExists( manifestFiles[ i ].Get() ) == shouldBeLoaded );
    }
    TEST_ASSERT( numLoaded == Server::kMaxCachedToolchains );

    // A directory without a manifest may be another worker's, so it is left alone
    TEST_ASSERT( FileIO::FileExists( receivingFile.Get() ) );
}

// BuildOnNewWorker
//------------------------------------------------------------------------------
// Builds with a worker that is created for the build, so it only knows what it
// has kept on disk from earlier builds.
void TestDistributed::BuildOnNewWorker( uint32_t & outManifestsReceived,
                                        uint32_t & outToolFilesReceived,
                                        AString & outWorkerToolchainFile ) const
{
    const char * target( "../tmp/Test/Distributed/dist.lib" );

    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    Server s( 1 );
    s.Listen( Protocol::kTestPort );

    Array<AString> files;
    FileIO::GetFiles( AStackString( "../tmp/Test/Distributed" ), AStackString( "*.*" ), true, &files );
    for ( const AString & file : files )
    {
        FileIO::FileDelete( file.Get() );
    }

    TEST_ASSERT( fBuild.Build( target ) );
    TEST_ASSERT( FileIO::FileExists( target ) );

    outManifestsReceived = s.m_NumManifestsReceived.Load();
    outToolFilesReceived = s.m_NumToolFilesReceived.Load();

    // Where the worker keeps the compiler used for the objects
    Array<const Node *> objects;
    fBuild.GetNodesOfType( Node::OBJECT_NODE, objects );
    outWorkerToolchainFile.Clear();
    for ( const Node * object : objects )
    {
        if ( object->GetName().Find( "a_normal" ) )
        {
            const ToolManifest & manifest = object->CastTo<ObjectNode>()->GetCompiler()->GetManifest();
            manifest.GetRemoteFilePath( 0, outWorkerToolchainFile );
            break;
        }
    }
    TEST_ASSERT( outWorkerToolchainFile.IsEmpty() == false );
}

// ToolchainCachedAcrossWorkerRestart
//------------------------------------------------------------------------------
void TestDistributed::ToolchainCachedAcrossWorkerRestart() const
{
    uint32_t manifests( 0 );
    uint32_t files( 0 );
    AStackString toolchainFile;

    // Nothing is cached, so the toolchain is sent
    BuildOnNewWorker( manifests, files, toolchainFile );
    TEST_ASSERT( manifests > 0 );
    TEST_ASSERT( files > 0 );

    // A new worker finds the toolchain on disk, so the coordinator never sends it
    BuildOnNewWorker( manifests, files, toolchainFile );
    TEST_ASSERT( manifests == 0 );
    TEST_ASSERT( files == 0 );
}

// ToolchainCorruptedFileIsResent
//------------------------------------------------------------------------------
void TestDistributed::ToolchainCorruptedFileIsResent() const
{
    uint32_t manifests( 0 );
    uint32_t files( 0 );
    AStackString toolchainFile;
    BuildOnNewWorker( manifests, files, toolchainFile );

    // Change one byte of the worker's copy of the compiler, keeping the size
    {
        FileStream in;
        TEST_ASSERT( in.Open( toolchainFile.Get() ) );
        const size_t size = (size_t)in.GetFileSize();
        TEST_ASSERT( size > 0 );
        UniquePtr<char, FreeDeletor> data( (char *)ALLOC( size ) );
        TEST_ASSERT( in.Read( data.Get(), size ) == size );
        in.Close();

        data.Get()[ size / 2 ] ^= 0x55;

        FileStream out;
        TEST_ASSERT( out.Open( toolchainFile.Get(), FileStream::WRITE_ONLY ) );
        TEST_ASSERT( out.Write( data.Get(), size ) == size );
    }

    // The worker does not trust the toolchain, so it is not advertised and is sent again
    BuildOnNewWorker( manifests, files, toolchainFile );
    TEST_ASSERT( manifests > 0 );
    TEST_ASSERT( files > 0 );

    // The sent copy replaced the corrupt one
    BuildOnNewWorker( manifests, files, toolchainFile );
    TEST_ASSERT( manifests == 0 );
    TEST_ASSERT( files == 0 );
}

// WorkerIsBuildingAJob
//------------------------------------------------------------------------------
/*static*/ bool TestDistributed::WorkerIsBuildingAJob( Server & server )
{
    // The toolchain the job needs is complete...
    {
        MutexHolder mh( server.m_ToolManifestsMutex );
        bool synchronized = false;
        for ( const ToolManifest * tool : server.m_Tools )
        {
            synchronized = ( synchronized || tool->IsSynchronized() );
        }
        if ( synchronized == false )
        {
            return false;
        }
    }

    // ...and a job has arrived
    MutexHolder mh( server.m_ClientListMutex );
    for ( const Server::ClientState * cs : server.m_ClientList )
    {
        if ( cs->m_NumJobsActive.Load() > 0 )
        {
            return true;
        }
    }
    return false;
}

// DropConnectionOnFirstJob
//------------------------------------------------------------------------------
/*static*/ uint32_t TestDistributed::DropConnectionOnFirstJob( void * param )
{
    ConnectionDrop * drop = static_cast<ConnectionDrop *>( param );
    while ( drop->m_Stop.Load() == false )
    {
        if ( WorkerIsBuildingAJob( *drop->m_Server ) )
        {
            // The worker keeps listening, so the coordinator can connect again
            Array<const ConnectionInfo *> connections;
            {
                MutexHolder mh( drop->m_Server->m_ClientListMutex );
                for ( const Server::ClientState * cs : drop->m_Server->m_ClientList )
                {
                    connections.Append( cs->m_Connection );
                }
            }
            for ( const ConnectionInfo * connection : connections )
            {
                drop->m_Server->Disconnect( connection );
            }
            drop->m_Dropped.Store( true );
            break;
        }
        Thread::Sleep( 1 );
    }
    return 0;
}

// ReadStats
//------------------------------------------------------------------------------
/*static*/ void TestDistributed::ReadStats( const AString & logPath, const char * linePrefix, uint32_t & outNumBlocks, uint64_t & outRawBytes, uint64_t & outOtherBytes, uint32_t * outNumMessages )
{
    outNumBlocks = 0;
    outRawBytes = 0;
    outOtherBytes = 0;
    if ( outNumMessages )
    {
        *outNumMessages = 0;
    }

    FileStream f;
    if ( f.Open( logPath.Get(), FileStream::READ_ONLY ) == false )
    {
        return;
    }
    Array<char> log;
    log.SetSize( (size_t)f.GetFileSize() + 1 );
    if ( f.Read( log.Begin(), log.GetSize() - 1 ) != ( log.GetSize() - 1 ) )
    {
        return;
    }
    log[ log.GetSize() - 1 ] = 0;

    // One block per connection. Sizes are written with a KB or MB unit.
    auto readBytes = []( char *& p )
    {
        const double amount = strtod( p, &p );
        const bool megabytes = ( p[ 1 ] == 'M' );
        return (uint64_t)( amount * ( megabytes ? 1048576.0 : 1024.0 ) );
    };
    const char * line = log.Begin();
    while ( ( line = strstr( line, linePrefix ) ) != nullptr )
    {
        char * p = nullptr;
        const uint32_t numMessages = (uint32_t)strtoul( line + strlen( linePrefix ), &p, 10 );
        p = strstr( p, ", " );
        if ( p == nullptr )
        {
            break;
        }
        p += 2;
        const uint64_t raw = readBytes( p );
        p = strstr( p, "raw, " );
        if ( p == nullptr )
        {
            break;
        }
        p += 5;
        const uint64_t other = readBytes( p );

        ++outNumBlocks;
        outRawBytes += raw;
        outOtherBytes += other;
        if ( outNumMessages )
        {
            *outNumMessages += numMessages;
        }
        line = p;
    }
}

// JobStreamAcrossReconnect
//------------------------------------------------------------------------------
void TestDistributed::JobStreamAcrossReconnect() const
{
    const char * target( "../tmp/Test/Distributed/JobStream/JobStream.lib" );

    // The connection is dropped under a job, so the build only completes if the coordinator
    // reconnects and its jobs reach the worker again on a new stream
    {
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );

        Server s( 1 );
        TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

        Array<AString> files;
        FileIO::GetFiles( AStackString( "../tmp/Test/Distributed/JobStream" ), AStackString( "*.*" ), true, &files );
        for ( const AString & file : files )
        {
            FileIO::FileDelete( file.Get() );
        }

        ConnectionDrop drop;
        drop.m_Server = &s;
        Thread dropper;
        dropper.Start( DropConnectionOnFirstJob, "DropConnection", &drop );
        const bool built = fBuild.Build( target );
        drop.m_Stop.Store( true );
        dropper.Join();

        TEST_ASSERT( drop.m_Dropped.Load() );
        TEST_ASSERT( built );
        TEST_ASSERT( FileIO::FileExists( target ) );

        // The toolchain was complete before the connection was dropped, so the worker still had it
        TEST_ASSERT( s.m_NumManifestsReceived.Load() == 1 );
    }

    // Each connection reported what it sent once it was closed
    AStackString<> logPath;
    TEST_ASSERT( FBuild::GetTempDir( logPath ) );
    logPath += "FBuild-distribution-stats.log";
    uint32_t numConnections;
    uint64_t rawBytes;
    uint64_t sentBytes;
    ReadStats( logPath, "  jobs: ", numConnections, rawBytes, sentBytes );
    TEST_ASSERT( numConnections >= 2 );
    TEST_ASSERT( rawBytes > 0 );

    // The jobs share most of their source, so each connection pays for it once. Compressing the
    // jobs one by one would cost about 10% of their size.
    TEST_ASSERT( ( sentBytes * 20 ) < rawBytes );
}

// ResultsAreCompressed
//------------------------------------------------------------------------------
void TestDistributed::ResultsAreCompressed() const
{
    const char * outputDir( "../tmp/Test/Distributed/ResultCompression" );
    const char * target( "../tmp/Test/Distributed/ResultCompression/ResultCompression.lib" );

    // PreTest emptied the temp directory, so only this build's connections are in the log
    AStackString<> logPath;
    TEST_ASSERT( FBuild::GetTempDir( logPath ) );
    logPath += "FBuild-distribution-stats.log";

    {
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );

        Server s( 1 );
        TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

        Array<AString> files;
        FileIO::GetFiles( AStackString( outputDir ), AStackString( "*.*" ), true, &files );
        for ( const AString & file : files )
        {
            FileIO::FileDelete( file.Get() );
        }

        TEST_ASSERT( fBuild.Build( target ) );
        TEST_ASSERT( FileIO::FileExists( target ) );
    }

    // The object file returned by the worker is intact: it still holds the whole table
    Array<AString> objects;
    FileIO::GetFiles( AStackString( outputDir ), AStackString( "Table.*" ), false, &objects );
    TEST_ASSERT( objects.GetSize() == 1 );
    FileStream f;
    TEST_ASSERT( f.Open( objects[ 0 ].Get(), FileStream::READ_ONLY ) );
    Array<char> object;
    object.SetSize( (size_t)f.GetFileSize() );
    TEST_ASSERT( f.Read( object.Begin(), object.GetSize() ) == object.GetSize() );
    const char pattern[ 8 ] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint32_t longestRun = 0;
    uint32_t run = 0;
    for ( size_t i = 0; ( i + sizeof( pattern ) ) <= object.GetSize(); )
    {
        if ( memcmp( object.Begin() + i, pattern, sizeof( pattern ) ) == 0 )
        {
            ++run;
            i += sizeof( pattern );
            longestRun = Math::Max( longestRun, run );
        }
        else
        {
            run = 0;
            ++i;
        }
    }
    TEST_ASSERT( longestRun == 4096 ); // 64 x 64 repeats of the pattern

    // The object is mostly a repeated table, so it returns at a small fraction of its size
    uint32_t numConnections;
    uint64_t rawBytes;
    uint64_t receivedBytes;
    ReadStats( logPath, "  results: ", numConnections, rawBytes, receivedBytes );
    TEST_ASSERT( numConnections >= 1 );
    TEST_ASSERT( rawBytes >= 32768 );
    TEST_ASSERT( receivedBytes > 0 );
    TEST_ASSERT( ( receivedBytes * 10 ) < rawBytes );
}

// Fake workers
//------------------------------------------------------------------------------
namespace
{
    // A worker that completes the handshake and leaves the rest to the test
    class FakeWorker : public TCPConnectionPool
    {
    public:
        explicit FakeWorker( uint8_t capacity,
                             const Array<uint64_t> & toolchainInventory = Array<uint64_t>(),
                             uint32_t capabilities = Protocol::kCapabilities )
            : FakeWorker( capacity, toolchainInventory, capabilities, capabilities )
        {
        }
        // The capabilities it sends the inventories for, and the ones it claims in the ack
        FakeWorker( uint8_t capacity, const Array<uint64_t> & toolchainInventory, uint32_t capabilities, uint32_t ackCapabilities )
            : m_Capacity( capacity )
            , m_ToolchainInventory( toolchainInventory )
            , m_Capabilities( capabilities )
            , m_AckCapabilities( ackCapabilities )
        {
        }
        explicit FakeWorker( const FakeWorker & other ) = delete;
        FakeWorker & operator=( const FakeWorker & other ) = delete;

        uint32_t GetNumConnections() const { return m_NumConnections.Load(); }
        uint32_t GetClientCapabilities() const { return m_ClientCapabilities.Load(); }

        // Seconds after this worker was created that each connection's handshake arrived
        void GetConnectionTimes( Array<float> & outTimes ) const
        {
            MutexHolder mh( m_ConnectionTimesMutex );
            outTimes = m_ConnectionTimes;
        }

    protected:
        // The reply to the client's handshake: the ack, then the inventories the capabilities have
        virtual void Handshake( const ConnectionInfo * connection )
        {
            const Protocol::MsgConnectionAck ack( m_Capacity, m_AckCapabilities );
            ack.Send( connection );
            if ( m_Capabilities & Protocol::CAP_PCH_INVENTORY )
            {
                const Protocol::MsgPchInventory pchInventory( 0 );
                pchInventory.Send( connection );
            }
            if ( m_Capabilities & Protocol::CAP_TOOLCHAIN_INVENTORY )
            {
                const Protocol::MsgToolchainInventory toolchainInventory( (uint32_t)m_ToolchainInventory.GetSize() );
                if ( m_ToolchainInventory.IsEmpty() )
                {
                    toolchainInventory.Send( connection );
                }
                else
                {
                    MemoryStream ids;
                    for ( const uint64_t toolId : m_ToolchainInventory )
                    {
                        ids.Write( toolId );
                    }
                    toolchainInventory.Send( connection, ids );
                }
            }
        }

        // Every message after the handshake. The payload is null for a message that has none.
        virtual void OnMessage( const ConnectionInfo * connection,
                                const Protocol::IMessage & message,
                                void * payload,
                                uint32_t payloadSize ) = 0;

    private:
        virtual void OnReceive( const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory ) override
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

            if ( m_CurrentMessage->GetType() == Protocol::MSG_CONNECTION )
            {
                {
                    MutexHolder mh( m_ConnectionTimesMutex );
                    m_ConnectionTimes.Append( m_Timer.GetElapsed() );
                }
                m_NumConnections.Increment();
                m_ClientCapabilities.Store( static_cast<const Protocol::MsgConnection *>( m_CurrentMessage )->GetCapabilities() );
                Handshake( connection );
            }
            else
            {
                OnMessage( connection, *m_CurrentMessage, payload, size );
            }

            FREE( (void *)m_CurrentMessage );
            FREE( payload );
            m_CurrentMessage = nullptr;
        }

        const uint8_t m_Capacity;
        const Array<uint64_t> m_ToolchainInventory;
        const uint32_t m_Capabilities;
        const uint32_t m_AckCapabilities;
        const Protocol::IMessage * m_CurrentMessage = nullptr;
        Atomic<uint32_t> m_NumConnections{ 0 };
        Atomic<uint32_t> m_ClientCapabilities{ 0 };
        const Timer m_Timer;
        mutable Mutex m_ConnectionTimesMutex;
        Array<float> m_ConnectionTimes;
    };

    // A worker that answers every job with a result that is not valid compressed data
    class CorruptResultWorker : public FakeWorker
    {
    public:
        explicit CorruptResultWorker( uint32_t capabilities = Protocol::kCapabilities ) : FakeWorker( 4, Array<uint64_t>(), capabilities ) {}
        CorruptResultWorker( uint32_t capabilities, uint32_t ackCapabilities ) : FakeWorker( 4, Array<uint64_t>(), capabilities, ackCapabilities ) {}
        explicit CorruptResultWorker( const CorruptResultWorker & other ) = delete;
        CorruptResultWorker & operator=( const CorruptResultWorker & other ) = delete;

        uint32_t GetNumResultsSent() const { return m_NumResultsSent.Load(); }

    private:
        virtual void OnMessage( const ConnectionInfo * connection,
                                const Protocol::IMessage & message,
                                void * payload,
                                uint32_t payloadSize ) override
        {
            if ( message.GetType() == Protocol::MSG_JOB )
            {
                void * job = nullptr;
                size_t jobSize = 0;
                if ( m_Decoder.Decode( payload, payloadSize, job, jobSize ) )
                {
                    uint32_t jobId = 0; // The first thing a job holds
                    ConstMemoryStream js( job, jobSize );
                    js.Read( jobId );
                    FREE( job );
                    SendCorruptResult( connection, jobId );
                }
            }
        }

        void SendCorruptResult( const ConnectionInfo * connection, uint32_t jobId )
        {
            MemoryStream ms;
            ms.Write( jobId );
            ms.Write( AStackString( "corrupt" ) );
            ms.Write( true );  // Built ok, so the data should be object files
            ms.Write( false ); // Not a system error
            ms.Write( Array<AString>() );
            ms.Write( (uint32_t)0 ); // Build time
            ms.Write( (uint16_t)0 ); // Thread
            char garbage[ 64 ];
            memset( garbage, 0xFF, sizeof( garbage ) );
            ms.Write( (uint32_t)sizeof( garbage ) );
            ms.WriteBuffer( garbage, sizeof( garbage ) );
            const Protocol::MsgJobResultCompressed msg( 4 );
            msg.Send( connection, ms );
            m_NumResultsSent.Increment();
        }

        JobStreamDecoder m_Decoder;
        Atomic<uint32_t> m_NumResultsSent{ 0 };
    };
}

// CorruptResultIsRejected
//------------------------------------------------------------------------------
void TestDistributed::CorruptResultIsRejected() const
{
    const char * outputDir( "../tmp/Test/Distributed/ResultCompression" );
    const char * target( "../tmp/Test/Distributed/ResultCompression/ResultCompression.lib" );

    // Results are cached as they arrive, so use a cache that starts empty
    AStackString<> cacheDir;
    GetCodeDir( cacheDir );
    cacheDir += "../tmp/Test/Distributed/Cache/";
    Array<AString> cached;
    FileIO::GetFiles( cacheDir, AStackString( "*" ), true, &cached );
    for ( const AString & file : cached )
    {
        FileIO::FileDelete( file.Get() );
    }

    Array<AString> files;
    FileIO::GetFiles( AStackString( outputDir ), AStackString( "*.*" ), true, &files );
    for ( const AString & file : files )
    {
        FileIO::FileDelete( file.Get() );
    }

    bool built = true;
    {
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_UseCacheWrite = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );

        CorruptResultWorker worker;
        TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );

        built = fBuild.Build( target );
        TEST_ASSERT( worker.GetNumResultsSent() >= 1 );
        worker.ShutdownAllConnections();
    }

    // The job fails, and nothing from the result is used
    TEST_ASSERT( built == false );
    Array<AString> objects;
    FileIO::GetFiles( AStackString( outputDir ), AStackString( "Table.*" ), false, &objects );
    TEST_ASSERT( objects.IsEmpty() );
    Array<AString> cachedAfter;
    FileIO::GetFiles( cacheDir, AStackString( "*" ), true, &cachedAfter );
    TEST_ASSERT( cachedAfter.IsEmpty() );
}

// HoldSendMutexOnFirstJob
//------------------------------------------------------------------------------
/*static*/ uint32_t TestDistributed::HoldSendMutexOnFirstJob( void * param )
{
    SendHold * hold = static_cast<SendHold *>( param );

    // The client sends its toolchain once the worker has sent everything of the handshake, and
    // sends the job after it: taking the mutex when the toolchain arrives is before any result,
    // and cannot hold up the handshake. The connection stays up, as the build waits for the result.
    Server::ClientState * cs = nullptr;
    while ( ( cs == nullptr ) && ( hold->m_Stop.Load() == false ) )
    {
        if ( hold->m_Server->m_NumManifestsReceived.Load() > 0 )
        {
            MutexHolder mh( hold->m_Server->m_ClientListMutex );
            if ( hold->m_Server->m_ClientList.GetSize() == 1 )
            {
                cs = hold->m_Server->m_ClientList[ 0 ];
            }
        }
        else
        {
            Thread::Sleep( 1 );
        }
    }
    if ( cs == nullptr )
    {
        return 0;
    }

    cs->m_SendMutex.Lock();

    // The job arrives and is built, but its result has to wait to be sent
    const bool jobArrived = WaitUntil( [ cs ]() { return ( cs->m_NumJobsActive.Load() > 0 ); } );
    hold->m_ReachedSend.Store( jobArrived && WaitUntil( [ cs ]() { return ( cs->m_NumJobsActive.Load() == 0 ); } ) );
    if ( hold->m_ReachedSend.Load() )
    {
        // What guards the connection's jobs is another lock: jobs can still be received meanwhile
        hold->m_StateMutexFree.Store( WaitUntil( [ cs ]()
        {
            if ( cs->m_Mutex.TryLock() )
            {
                cs->m_Mutex.Unlock();
                return true;
            }
            return false;
        }, 1.0f ) );
        Thread::Sleep( 200 );
        hold->m_ResultDeliveredWhileHeld.Store( hold->m_BuildDone.Load() );
    }

    cs->m_SendMutex.Unlock();
    return 0;
}

// SendMutexHoldsBackResults
//------------------------------------------------------------------------------
void TestDistributed::SendMutexHoldsBackResults() const
{
    const char * outputDir( "../tmp/Test/Distributed/ResultCompression" );
    const char * target( "../tmp/Test/Distributed/ResultCompression/ResultCompression.lib" );

    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    Array<AString> files;
    FileIO::GetFiles( AStackString( outputDir ), AStackString( "*.*" ), true, &files );
    for ( const AString & file : files )
    {
        FileIO::FileDelete( file.Get() );
    }

    SendHold hold;
    hold.m_Server = &s;
    Thread holder;
    holder.Start( HoldSendMutexOnFirstJob, "HoldSendMutex", &hold );
    const bool built = fBuild.Build( target );
    hold.m_BuildDone.Store( true );
    hold.m_Stop.Store( true );
    holder.Join();

    // Nothing is sent on a connection while its send mutex is held, and the lock that guards the
    // connection's jobs is a different one
    TEST_ASSERT( hold.m_ReachedSend.Load() );
    TEST_ASSERT( hold.m_ResultDeliveredWhileHeld.Load() == false );
    TEST_ASSERT( hold.m_StateMutexFree.Load() );
    TEST_ASSERT( built );
}

// Build helpers
//------------------------------------------------------------------------------
namespace
{
    // Aborts the build once a condition holds, or after a timeout, so that a test fails instead of hanging.
    // Does nothing if the build ends first.
    class BuildAborter
    {
    public:
        template <class CONDITION>
        BuildAborter( const CONDITION & condition, float timeoutSeconds )
            : m_Condition( &condition )
            , m_Check( []( const void * c ) { return ( *static_cast<const CONDITION *>( c ) )(); } )
            , m_TimeoutSeconds( timeoutSeconds )
        {
            m_Thread.Start( ThreadFunc, "BuildAborter", this );
        }
        ~BuildAborter() { m_Stop.Store( true ); m_Thread.Join(); }
        explicit BuildAborter( const BuildAborter & other ) = delete;
        BuildAborter & operator=( const BuildAborter & other ) = delete;

        bool HasReached() const { return m_Reached.Load(); }    // The condition held
        bool HasTimedOut() const { return m_TimedOut.Load(); }  // It did not hold in time

    private:
        static uint32_t ThreadFunc( void * param )
        {
            BuildAborter * a = static_cast<BuildAborter *>( param );
            const Timer timer;
            while ( a->m_Stop.Load() == false )
            {
                if ( a->m_Check( a->m_Condition ) )
                {
                    a->m_Reached.Store( true );
                    break;
                }
                if ( timer.GetElapsed() > a->m_TimeoutSeconds )
                {
                    a->m_TimedOut.Store( true );
                    break;
                }
                Thread::Sleep( 1 );
            }
            if ( a->m_Stop.Load() == false )
            {
                FBuild::AbortBuild();
            }
            return 0;
        }
        const void * const m_Condition;
        bool ( * const m_Check )( const void * );
        const float m_TimeoutSeconds;
        Thread m_Thread;
        Atomic<bool> m_Stop{ false };
        Atomic<bool> m_Reached{ false };
        Atomic<bool> m_TimedOut{ false };
    };

    // Watches the workers a build gives up on, which it does while the build runs
    class DenyListPoller
    {
    public:
        explicit DenyListPoller( const FBuild & build ) : m_Build( build ) { m_Thread.Start( ThreadFunc, "DenyListPoller", this ); }
        ~DenyListPoller() { m_Stop.Store( true ); m_Thread.Join(); }
        explicit DenyListPoller( const DenyListPoller & other ) = delete;
        DenyListPoller & operator=( const DenyListPoller & other ) = delete;
        uint32_t GetMax() const { return m_Max.Load(); }

    private:
        static uint32_t ThreadFunc( void * param )
        {
            DenyListPoller * p = static_cast<DenyListPoller *>( param );
            while ( p->m_Stop.Load() == false )
            {
                const uint32_t n = p->m_Build.GetNumDenyListedWorkers();
                if ( n > p->m_Max.Load() )
                {
                    p->m_Max.Store( n );
                }
                Thread::Sleep( 1 );
            }
            return 0;
        }
        const FBuild & m_Build;
        Thread m_Thread;
        Atomic<bool> m_Stop{ false };
        Atomic<uint32_t> m_Max{ 0 };
    };

    struct LocalBuildResult
    {
        bool        m_Built = false;
        float       m_Seconds = 0.0f;
        uint32_t    m_MaxDenyListed = 0; // The most workers the build had given up on at one time
    };

    // Builds the library that compiles here, with distribution allowed and a worker listening that is
    // given no job. The reconnect delay is short so that a worker that is tried again shows within the build.
    LocalBuildResult BuildPchLocal( FakeWorker & worker, float reconnectDelay = 10.0f, uint32_t capabilities = Protocol::kCapabilities )
    {
        LocalBuildResult result;
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_ForceCleanBuild = true;
        options.m_DistributionReconnectDelay = reconnectDelay;
        options.m_DistributionCapabilities = capabilities;
        FBuildForTest fBuild( options );
        if ( ( fBuild.Initialize() == false ) || ( worker.Listen( Protocol::kTestPort ) == false ) )
        {
            return result;
        }
        DenyListPoller poller( fBuild );
        const Timer timer;
        result.m_Built = fBuild.Build( "../tmp/Test/Distributed/Local/distpchlocal.lib" );
        result.m_Seconds = timer.GetElapsed();
        worker.ShutdownAllConnections();
        result.m_MaxDenyListed = poller.GetMax();
        return result;
    }
}

// Toolchain sending
//------------------------------------------------------------------------------
namespace
{
    // A worker that completes the handshake but has no capacity, so no job is ever sent to it
    class NoCapacityWorker : public FakeWorker
    {
    public:
        static constexpr uint8_t kNoCapacity = 0;

        explicit NoCapacityWorker( const Array<uint64_t> & toolchainInventory = Array<uint64_t>() )
            : FakeWorker( kNoCapacity, toolchainInventory )
        {
        }
        explicit NoCapacityWorker( uint32_t capabilities )
            : FakeWorker( kNoCapacity, Array<uint64_t>(), capabilities )
        {
        }
        explicit NoCapacityWorker( const NoCapacityWorker & other ) = delete;
        NoCapacityWorker & operator=( const NoCapacityWorker & other ) = delete;

        uint32_t GetNumManifests() const { return m_NumManifests.Load(); }
        uint32_t GetNumFiles() const { return m_NumFiles.Load(); }
        uint32_t GetNumPchFiles() const { return m_NumPchFiles.Load(); }
        uint32_t GetNumJobs() const { return m_NumJobs.Load(); }
        uint64_t GetLastToolId() const { return m_LastToolId.Load(); }

        // What the first chunk of the last PCH came with: the type in the header of the data, and the sizes
        uint32_t GetPchType() const { return m_PchType.Load(); }
        uint32_t GetPchCompressedSize() const { return m_PchCompressedSize.Load(); }
        uint32_t GetPchUncompressedSize() const { return m_PchUncompressedSize.Load(); }

    private:
        virtual void OnMessage( const ConnectionInfo *, const Protocol::IMessage & message, void * payload, uint32_t payloadSize ) override
        {
            switch ( message.GetType() )
            {
                case Protocol::MSG_MANIFEST:
                    m_LastToolId.Store( static_cast<const Protocol::MsgManifest &>( message ).GetToolId() );
                    m_NumManifests.Increment();
                    break;
                case Protocol::MSG_FILE: m_NumFiles.Increment(); break;
                case Protocol::MSG_PCH_FILE:
                {
                    const Protocol::MsgPchFile & pchFile = static_cast<const Protocol::MsgPchFile &>( message );
                    if ( ( pchFile.GetChunkIndex() == 0 ) && ( payloadSize >= Compressor::kHeaderSize ) )
                    {
                        uint32_t type;
                        memcpy( &type, payload, sizeof( type ) );
                        m_PchType.Store( type );
                        m_PchCompressedSize.Store( pchFile.GetCompressedSize() );
                        m_PchUncompressedSize.Store( pchFile.GetUncompressedSize() );
                    }
                    m_NumPchFiles.Increment();
                    break;
                }
                case Protocol::MSG_JOB: m_NumJobs.Increment(); break;
                default: break;
            }
        }

        Atomic<uint32_t> m_PchType{ 0xFFFFFFFF };
        Atomic<uint32_t> m_PchCompressedSize{ 0 };
        Atomic<uint32_t> m_PchUncompressedSize{ 0 };
        Atomic<uint32_t> m_NumManifests{ 0 };
        Atomic<uint32_t> m_NumFiles{ 0 };
        Atomic<uint32_t> m_NumPchFiles{ 0 };
        Atomic<uint32_t> m_NumJobs{ 0 };
        Atomic<uint64_t> m_LastToolId{ 0 };
    };

    // What a build is stopped on
    struct BuildStop
    {
        enum When
        {
            TOOLCHAIN_ARRIVES,  // The manifest and a file have reached the worker
            PCH_ARRIVES,        // A PCH has reached the worker. A toolchain is sent ahead of it.
        };
    };
}

// Builds the library with a PCH against a worker that never gets a job, until the build is stopped
void TestDistributed::BuildPchLibraryUntilStopped( NoCapacityWorker & worker, int when, uint32_t clientCapabilities ) const
{
    FBuildTestOptions options;
    options.m_DistributionCapabilities = clientCapabilities;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // jobs stay queued for the remote worker
    options.m_AllowLocalRace = false;
    options.m_ForceCleanBuild = true;
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );

    auto reached = [ &worker, when ]()
    {
        return ( when == BuildStop::TOOLCHAIN_ARRIVES ) ? ( ( worker.GetNumManifests() >= 1 ) && ( worker.GetNumFiles() >= 1 ) )
                                                        : ( worker.GetNumPchFiles() >= 1 );
    };
    BuildAborter aborter( reached, kWaitTimeoutSeconds );
    const bool built = fBuild.Build( "../tmp/Test/Distributed/distpch.lib" );
    worker.ShutdownAllConnections();

    TEST_ASSERT( built == false ); // Aborted
    TEST_ASSERT( aborter.HasReached() );
    TEST_ASSERT( worker.GetNumJobs() == 0 );
}

// ToolchainIsSentBeforeAnyJob
//------------------------------------------------------------------------------
void TestDistributed::ToolchainIsSentBeforeAnyJob() const
{
    // The worker has no capacity, so the build never sends it a job. It is sent the toolchain
    // regardless, once the PCH starts building, so the upload does not wait for the first job.
    NoCapacityWorker worker;
    BuildPchLibraryUntilStopped( worker, BuildStop::TOOLCHAIN_ARRIVES );
    TEST_ASSERT( worker.GetNumManifests() >= 1 );
    TEST_ASSERT( worker.GetNumFiles() >= 1 );
}

// PchIsSentBeforeAnyJobIsDispatched
//------------------------------------------------------------------------------
void TestDistributed::PchIsSentBeforeAnyJobIsDispatched() const
{
    // The worker has no capacity, so no job is dispatched to it. The PCH the waiting jobs need
    // is queued for sending anyway, so the uplink does not wait for a job to be taken.
    NoCapacityWorker worker;
    BuildPchLibraryUntilStopped( worker, BuildStop::PCH_ARRIVES );
    TEST_ASSERT( worker.GetNumPchFiles() >= 1 );
}

// CachedToolchainIsNotSent
//------------------------------------------------------------------------------
void TestDistributed::CachedToolchainIsNotSent() const
{
    // Learn the toolchain's id from a first build
    uint64_t toolId = 0;
    {
        NoCapacityWorker worker;
        BuildPchLibraryUntilStopped( worker, BuildStop::TOOLCHAIN_ARRIVES );
        toolId = worker.GetLastToolId();
        TEST_ASSERT( toolId != 0 );
    }

    // A worker that says it has the toolchain is not sent it
    Array<uint64_t> inventory;
    inventory.Append( toolId );
    NoCapacityWorker worker( inventory );
    BuildPchLibraryUntilStopped( worker, BuildStop::PCH_ARRIVES );
    TEST_ASSERT( worker.GetNumConnections() >= 1 );
    TEST_ASSERT( worker.GetNumManifests() == 0 );
    TEST_ASSERT( worker.GetNumFiles() == 0 );
}

// ToolchainIsNotSentWhenNothingIsDistributed
//------------------------------------------------------------------------------
void TestDistributed::ToolchainIsNotSentWhenNothingIsDistributed() const
{
    // The library is compiled here, so workers have no use for the toolchain
    NoCapacityWorker worker;
    TEST_ASSERT( BuildPchLocal( worker ).m_Built );
    TEST_ASSERT( worker.GetNumConnections() >= 1 );
    TEST_ASSERT( worker.GetNumManifests() == 0 );
    TEST_ASSERT( worker.GetNumFiles() == 0 );
}

// Handshake tests
//------------------------------------------------------------------------------
namespace
{
    // Connects to a worker and records the messages it sends back, in order
    class HandshakeProbe : public TCPConnectionPool
    {
    public:
        size_t GetNumMessages() const
        {
            MutexHolder mh( m_Mutex );
            return m_MessageTypes.GetSize();
        }
        uint8_t GetMessageType( size_t index ) const
        {
            MutexHolder mh( m_Mutex );
            return m_MessageTypes[ index ];
        }
        size_t GetNumPchNeeded() const
        {
            MutexHolder mh( m_Mutex );
            return m_PchNeededIds.GetSize();
        }
        uint64_t GetPchNeededId( size_t index ) const
        {
            MutexHolder mh( m_Mutex );
            return m_PchNeededIds[ index ];
        }
        bool IsDisconnected() const { return m_Disconnected.Load(); }
        uint32_t GetRefusedMajor() const { return m_RefusedMajor.Load(); }
        uint32_t GetRefusedMinor() const { return m_RefusedMinor.Load(); }
        uint32_t GetAckCapabilities() const { return m_AckCapabilities.Load(); }
        void GetToolchainIds( Array<uint64_t> & outToolIds ) const
        {
            MutexHolder mh( m_Mutex );
            outToolIds = m_ToolchainIds;
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
                m_MessageTypes.Append( (uint8_t)m_CurrentMessage->GetType() );
                if ( m_CurrentMessage->GetType() == Protocol::MSG_REFUSED )
                {
                    const Protocol::MsgRefused * refused = static_cast<const Protocol::MsgRefused *>( m_CurrentMessage );
                    m_RefusedMajor.Store( refused->GetProtocolVersionMajor() );
                    m_RefusedMinor.Store( refused->GetProtocolVersionMinor() );
                }
                if ( m_CurrentMessage->GetType() == Protocol::MSG_PCH_NEEDED )
                {
                    m_PchNeededIds.Append( static_cast<const Protocol::MsgPchNeeded *>( m_CurrentMessage )->GetPchId() );
                }
                if ( m_CurrentMessage->GetType() == Protocol::MSG_CONNECTION_ACK )
                {
                    m_AckCapabilities.Store( static_cast<const Protocol::MsgConnectionAck *>( m_CurrentMessage )->GetCapabilities() );
                }
                if ( ( m_CurrentMessage->GetType() == Protocol::MSG_TOOLCHAIN_INVENTORY ) && payload )
                {
                    const uint64_t * toolIds = static_cast<const uint64_t *>( payload );
                    for ( size_t i = 0; i < ( size / sizeof( uint64_t ) ); ++i )
                    {
                        m_ToolchainIds.Append( toolIds[ i ] );
                    }
                }
            }

            FREE( (void *)m_CurrentMessage );
            FREE( payload );
            m_CurrentMessage = nullptr;
        }
        virtual void OnDisconnected( const ConnectionInfo * ) override { m_Disconnected.Store( true ); }

        mutable Mutex m_Mutex;
        Array<uint8_t> m_MessageTypes;
        Array<uint64_t> m_ToolchainIds;
        Array<uint64_t> m_PchNeededIds;
        Atomic<bool> m_Disconnected{ false };
        Atomic<uint32_t> m_RefusedMajor{ 0 };
        Atomic<uint32_t> m_RefusedMinor{ 0 };
        Atomic<uint32_t> m_AckCapabilities{ 0 };
        const Protocol::IMessage * m_CurrentMessage = nullptr;
    };

    // The layout of the handshake messages that every major keeps, as numbers: what peers of another
    // major find by (see Protocol.h). A change to the protocol that moves one of them has to change these too.
    constexpr uint32_t kGoldenConnectionSize = 80;
    constexpr uint32_t kGoldenMajor23ConnectionSize = 76;   // 23.x had no capabilities
    constexpr uint32_t kGoldenPrefixSize = 8;               // Header and version fields
    constexpr uint32_t kGoldenConnectionMajorOffset = 4;    // uint32_t
    constexpr uint32_t kGoldenHostNameOffset = 12;          // char[ 64 ]
    constexpr uint32_t kGoldenCapabilitiesOffset = 76;      // uint32_t
    constexpr uint32_t kGoldenAckMajorOffset = 6;           // uint8_t, also in MsgRefused

    // The handshake of a client of this version, as bytes a test can change
    struct HandshakeBytes
    {
        explicit HandshakeBytes( uint32_t capabilities = Protocol::kCapabilities )
        {
            const Protocol::MsgConnection msg( capabilities );
            static_assert( sizeof( bytes ) == sizeof( msg ), "Size of the handshake" );
            memcpy( bytes, &msg, sizeof( msg ) );
        }
        void SetMajor( uint32_t major ) { memcpy( bytes + kGoldenConnectionMajorOffset, &major, sizeof( major ) ); }
        void SetCapabilities( uint32_t capabilities ) { memcpy( bytes + kGoldenCapabilitiesOffset, &capabilities, sizeof( capabilities ) ); }
        void SetHostName( const char * name ) // Zero terminated if it is shorter than the 64 bytes there are
        {
            memset( bytes + kGoldenHostNameOffset, 0, 64 );
            memcpy( bytes + kGoldenHostNameOffset, name, Math::Min<size_t>( strlen( name ), 64 ) );
        }
        void FillHostName( char c ) { memset( bytes + kGoldenHostNameOffset, c, 64 ); }
        uint8_t bytes[ kGoldenConnectionSize ];
    };

    // A worker that answers the handshake in a way that is not valid for this version
    class OddHandshakeWorker : public FakeWorker
    {
    public:
        enum Reply
        {
            REFUSE_AS_NEWER_MAJOR,  // MSG_REFUSED from a worker of the next major
            ACK_OF_OLDER_MAJOR,     // An ack of the previous major
            ACK_TOO_SHORT,          // An ack of this major that stops after the version fields
            ACK_WITH_PAYLOAD,       // An ack whose header says a payload follows, which an ack never has
        };
        explicit OddHandshakeWorker( Reply reply ) : FakeWorker( 4 ), m_Reply( reply ) {}
        explicit OddHandshakeWorker( const OddHandshakeWorker & other ) = delete;
        OddHandshakeWorker & operator=( const OddHandshakeWorker & other ) = delete;

    private:
        virtual void Handshake( const ConnectionInfo * connection ) override
        {
            uint8_t bytes[ sizeof( Protocol::MsgConnectionAck ) ];
            size_t size = sizeof( bytes );
            switch ( m_Reply )
            {
                case REFUSE_AS_NEWER_MAJOR:
                {
                    const Protocol::MsgRefused refused;
                    size = sizeof( refused );
                    memcpy( bytes, &refused, size );
                    bytes[ kGoldenAckMajorOffset ] = (uint8_t)( Protocol::kVersionMajor + 1 );
                    break;
                }
                case ACK_OF_OLDER_MAJOR:
                {
                    const Protocol::MsgConnectionAck ack( 4 );
                    memcpy( bytes, &ack, size );
                    bytes[ kGoldenAckMajorOffset ] = (uint8_t)( Protocol::kVersionMajor - 1 );
                    break;
                }
                case ACK_TOO_SHORT:
                {
                    const Protocol::MsgConnectionAck ack( 4 );
                    memcpy( bytes, &ack, size );
                    size = ( kGoldenPrefixSize + 2 ); // The version fields and a little more
                    break;
                }
                case ACK_WITH_PAYLOAD:
                {
                    const Protocol::MsgConnectionAck ack( 4 );
                    memcpy( bytes, &ack, size );

                    // The header alone, announcing a payload. The ack in full follows as that payload.
                    uint8_t header[ kGoldenPrefixSize ];
                    memcpy( header, bytes, sizeof( header ) );
                    header[ 2 ] = 1;
                    Send( connection, header, sizeof( header ) );
                    break;
                }
            }
            Send( connection, bytes, (uint32_t)size );
        }
        virtual void OnMessage( const ConnectionInfo *, const Protocol::IMessage &, void *, uint32_t ) override {}

        const Reply m_Reply;
    };

    uint32_t CountOccurrences( const AString & text, const char * what )
    {
        uint32_t count = 0;
        const char * pos = text.Find( what );
        while ( pos != nullptr )
        {
            ++count;
            pos = text.Find( what, pos + AString::StrLen( what ) );
        }
        return count;
    }
}

// ToolchainInventoryFollowsAck
//------------------------------------------------------------------------------
void TestDistributed::ToolchainInventoryFollowsAck() const
{
    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    HandshakeProbe probe;
    const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
    TEST_ASSERT( ci );
    const Protocol::MsgConnection msg;
    TEST_ASSERT( probe.Send( ci, &msg, sizeof( msg ) ) );

    TEST_ASSERT( WaitUntil( [ &probe ]() { return ( probe.GetNumMessages() >= 3 ); } ) );

    // The coordinator holds back its jobs until the last of these arrives
    TEST_ASSERT( probe.GetNumMessages() == 3 );
    TEST_ASSERT( probe.GetMessageType( 0 ) == Protocol::MSG_CONNECTION_ACK );
    TEST_ASSERT( probe.GetMessageType( 1 ) == Protocol::MSG_PCH_INVENTORY );
    TEST_ASSERT( probe.GetMessageType( 2 ) == Protocol::MSG_TOOLCHAIN_INVENTORY );

    probe.ShutdownAllConnections();
}

// ToolchainInventoryIsBounded
//------------------------------------------------------------------------------
void TestDistributed::ToolchainInventoryIsBounded() const
{
    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    // More toolchains than a worker keeps, the least recently used first
    const uint32_t kNumToolchains = ( Server::kMaxCachedToolchains + 2 );
    const uint64_t kFirstToolId = 0x2000;
    for ( uint32_t i = 0; i < kNumToolchains; ++i )
    {
        AStackString<> content;
        content.Format( "compiler %u", i );
        ToolManifest * manifest = SyntheticToolchain::Create( kFirstToolId + i, content.Get() );
        TEST_ASSERT( manifest );
        manifest->MarkUsed();
        {
            MutexHolder mh( s.m_ToolManifestsMutex );
            s.m_Tools.Append( manifest );
        }
        Thread::Sleep( 20 ); // File times have a limited resolution
    }
    auto isKept = [ kNumToolchains ]( uint32_t index ) { return ( index >= ( kNumToolchains - Server::kMaxCachedToolchains ) ); };

    HandshakeProbe probe;
    const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
    TEST_ASSERT( ci );
    const Protocol::MsgConnection msg;
    TEST_ASSERT( probe.Send( ci, &msg, sizeof( msg ) ) );
    TEST_ASSERT( WaitUntil( [ &probe ]() { return ( probe.GetNumMessages() >= 3 ); } ) );

    // Only the most recently used are offered, so a connection can not hold on to more than a worker keeps
    Array<uint64_t> advertised;
    probe.GetToolchainIds( advertised );
    TEST_ASSERT( advertised.GetSize() == Server::kMaxCachedToolchains );
    {
        MutexHolder mh( s.m_ToolManifestsMutex );
        for ( uint32_t i = 0; i < kNumToolchains; ++i )
        {
            const uint64_t toolId = ( kFirstToolId + i );
            TEST_ASSERT( ( advertised.Find( toolId ) != nullptr ) == isKept( i ) );

            // and what is offered has to stay for as long as the connection does
            TEST_ASSERT( ( *s.m_Tools.FindDeref( toolId ) )->IsInUse() == isKept( i ) );
        }
    }

    // Without the connection nothing holds the toolchains, so the worker trims to what it keeps
    probe.ShutdownAllConnections();
    auto numToolchains = [ &s ]()
    {
        MutexHolder mh( s.m_ToolManifestsMutex );
        return s.m_Tools.GetSize();
    };
    TEST_ASSERT( WaitUntil( [ & ]() { return ( numToolchains() == Server::kMaxCachedToolchains ); } ) );
    {
        MutexHolder mh( s.m_ToolManifestsMutex );
        for ( uint32_t i = 0; i < kNumToolchains; ++i )
        {
            ToolManifest ** found = s.m_Tools.FindDeref( kFirstToolId + i );
            TEST_ASSERT( ( found != nullptr ) == isKept( i ) );
            TEST_ASSERT( ( found == nullptr ) || ( ( *found )->IsInUse() == false ) ); // The connection let go
        }
    }
}

// MismatchedProtocolVersionIsRefused
//------------------------------------------------------------------------------
void TestDistributed::MismatchedProtocolVersionIsRefused() const
{
    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    HandshakeProbe probe;
    const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
    TEST_ASSERT( ci );

    // Handshake from a client with the previous protocol version
    HandshakeBytes handshake;
    handshake.SetMajor( Protocol::kVersionMajor - 1 );
    TEST_ASSERT( probe.Send( ci, handshake.bytes, sizeof( handshake.bytes ) ) );

    // The worker says which version it is, and drops the connection without acknowledging it
    TEST_ASSERT( WaitUntil( [ &probe ]() { return probe.IsDisconnected(); } ) );
    TEST_ASSERT( probe.GetNumMessages() == 1 );
    TEST_ASSERT( probe.GetMessageType( 0 ) == Protocol::MSG_REFUSED );
    TEST_ASSERT( probe.GetRefusedMajor() == Protocol::kVersionMajor );
    TEST_ASSERT( probe.GetRefusedMinor() == Protocol::kVersionMinor );

    probe.ShutdownAllConnections();
}

// FramesOfAnotherMajorAreRefused
//------------------------------------------------------------------------------
void TestDistributed::FramesOfAnotherMajorAreRefused() const
{
    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    // The handshake of a 23.x client is shorter than this version's, and a frame of another major is
    // only read as far as it goes: past its version fields, to the host name, and no further
    const uint32_t frameSizes[] = {
        kGoldenMajor23ConnectionSize,
        kGoldenHostNameOffset,  // Stops before the host name
        kGoldenPrefixSize,      // Only the header and the version
    };
    for ( const uint32_t frameSize : frameSizes )
    {
        HandshakeBytes handshake;
        handshake.SetMajor( 23 );

        HandshakeProbe probe;
        const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
        TEST_ASSERT( ci );
        TEST_ASSERT( probe.Send( ci, handshake.bytes, frameSize ) );
        TEST_ASSERT( WaitUntil( [ &probe ]() { return probe.IsDisconnected(); } ) );
        TEST_ASSERT( probe.GetNumMessages() == 1 );
        TEST_ASSERT( probe.GetMessageType( 0 ) == Protocol::MSG_REFUSED );
        probe.ShutdownAllConnections();
    }
}

// HandshakeLayoutIsFrozen
//------------------------------------------------------------------------------
void TestDistributed::HandshakeLayoutIsFrozen() const
{
    // A peer of another major tells versions apart by these bytes. They are the ones this version
    // sends, apart from the platform, host name and worker version.
    const Protocol::MsgConnection connection;
    uint8_t connectionBytes[ sizeof( connection ) ];
    memcpy( connectionBytes, &connection, sizeof( connection ) );
    TEST_ASSERT( sizeof( connection ) == kGoldenConnectionSize );
    const uint8_t connectionHeader[] = { 1 /*MSG_CONNECTION*/, kGoldenConnectionSize, 0 /*no payload*/, 0 };
    TEST_ASSERT( memcmp( connectionBytes, connectionHeader, sizeof( connectionHeader ) ) == 0 );
    TEST_ASSERT( connectionBytes[ kGoldenConnectionMajorOffset ] == Protocol::kVersionMajor );
    TEST_ASSERT( connectionBytes[ kGoldenConnectionMajorOffset + 1 ] == 0 ); // The rest of the uint32_t

    const Protocol::MsgConnectionAck ack( 4 );
    uint8_t ackBytes[ sizeof( ack ) ];
    memcpy( ackBytes, &ack, sizeof( ack ) );
    const uint8_t ackHeader[] = { 7 /*MSG_CONNECTION_ACK*/, 16, 0, 0 };
    TEST_ASSERT( memcmp( ackBytes, ackHeader, sizeof( ackHeader ) ) == 0 );
    TEST_ASSERT( ackBytes[ kGoldenAckMajorOffset ] == Protocol::kVersionMajor );
    TEST_ASSERT( ackBytes[ kGoldenAckMajorOffset + 1 ] == Protocol::kVersionMinor );
    TEST_ASSERT( ackBytes[ kGoldenPrefixSize ] == 4 ); // The capacity follows the version fields

    const Protocol::MsgRefused refused;
    uint8_t refusedBytes[ sizeof( refused ) ];
    memcpy( refusedBytes, &refused, sizeof( refused ) );
    const uint8_t refusedHeader[] = { 13 /*MSG_REFUSED*/, kGoldenPrefixSize, 0, 0 };
    TEST_ASSERT( memcmp( refusedBytes, refusedHeader, sizeof( refusedHeader ) ) == 0 );
    TEST_ASSERT( refusedBytes[ kGoldenAckMajorOffset ] == Protocol::kVersionMajor );
    TEST_ASSERT( refusedBytes[ kGoldenAckMajorOffset + 1 ] == Protocol::kVersionMinor );

    // and the handshake a 23.x peer sends is found by them
    alignas( 8 ) uint8_t connection23[ kGoldenMajor23ConnectionSize ] = { 1, kGoldenMajor23ConnectionSize, 0, 0, 23, 0, 0, 0, 1 /*platform*/, 6 /*minor*/ };
    memcpy( connection23 + kGoldenHostNameOffset, "old-client", 11 );
    TEST_ASSERT( Protocol::IsValidHeader( connection23, sizeof( connection23 ) ) );
    const Protocol::MsgConnection * old = reinterpret_cast<const Protocol::MsgConnection *>( connection23 );
    TEST_ASSERT( old->GetProtocolVersion() == 23 );
    AStackString<> oldName;
    old->GetHostName( sizeof( connection23 ), oldName );
    TEST_ASSERT( oldName == "old-client" );

    alignas( 8 ) const uint8_t ack23[ 12 ] = { 7, 12, 0, 0, 0x34, 0x12 /*worker version*/, 23, 6 /*minor*/, 4 /*capacity*/ };
    TEST_ASSERT( Protocol::IsValidHeader( ack23, sizeof( ack23 ) ) );
    const Protocol::MsgConnectionAck * oldAck = reinterpret_cast<const Protocol::MsgConnectionAck *>( ack23 );
    TEST_ASSERT( oldAck->GetProtocolVersionMajor() == 23 );
    TEST_ASSERT( oldAck->GetWorkerVersion() == 0x1234 );
}

// VersionIsShownWithTheProtocol
//------------------------------------------------------------------------------
void TestDistributed::VersionIsShownWithTheProtocol() const
{
    // Mixed fleets are told apart by it
    const size_t outputBefore = GetRecordedOutput().GetLength();
    FBuildOptions options;
    char program[] = "FBuild.exe";
    char version[] = "-version";
    char * argv[] = { program, version };
    TEST_ASSERT( options.ProcessCommandLine( 2, argv ) == FBuildOptions::OPTIONS_OK_AND_QUIT );

    AStackString expected;
    expected.Format( "(protocol v%u.%u)", Protocol::kVersionMajor, (uint32_t)Protocol::kVersionMinor );
    TEST_ASSERT( GetRecordedOutput().Find( expected.Get(), GetRecordedOutput().Get() + outputBefore ) );
}

// ShortMessagesAreRejected
//------------------------------------------------------------------------------
void TestDistributed::ShortMessagesAreRejected() const
{
    // Every message needs the whole of its header, apart from the handshakes, which need their version
    // fields, and announces a payload only if its type has one
    struct Expected
    {
        Protocol::MessageType   m_Type;
        uint32_t                m_MinSize;
        Protocol::PayloadRule   m_Payload;
    };
    const Expected expected[] = {
        { Protocol::MSG_CONNECTION,             kGoldenPrefixSize,                          Protocol::PAYLOAD_NEVER },
        { Protocol::MSG_JOB,                    sizeof( Protocol::MsgJob ),                 Protocol::PAYLOAD_ALWAYS },
        { Protocol::MSG_JOB_RESULT,             sizeof( Protocol::MsgJobResult ),           Protocol::PAYLOAD_ALWAYS },
        { Protocol::MSG_JOB_RESULT_COMPRESSED,  sizeof( Protocol::MsgJobResultCompressed ), Protocol::PAYLOAD_ALWAYS },
        { Protocol::MSG_MANIFEST,               sizeof( Protocol::MsgManifest ),            Protocol::PAYLOAD_ALWAYS },
        { Protocol::MSG_FILE,                   sizeof( Protocol::MsgFile ),                Protocol::PAYLOAD_ALWAYS },
        { Protocol::MSG_CONNECTION_ACK,         kGoldenPrefixSize,                          Protocol::PAYLOAD_NEVER },
        { Protocol::MSG_PCH_FILE,               sizeof( Protocol::MsgPchFile ),             Protocol::PAYLOAD_ALWAYS },
        { Protocol::MSG_PCH_INVENTORY,          sizeof( Protocol::MsgPchInventory ),        Protocol::PAYLOAD_OPTIONAL },
        { Protocol::MSG_REQUEST_MANIFEST,       sizeof( Protocol::MsgRequestManifest ),     Protocol::PAYLOAD_NEVER },
        { Protocol::MSG_REQUEST_FILE,           sizeof( Protocol::MsgRequestFile ),         Protocol::PAYLOAD_NEVER },
        { Protocol::MSG_TOOLCHAIN_INVENTORY,    sizeof( Protocol::MsgToolchainInventory ),  Protocol::PAYLOAD_OPTIONAL },
        { Protocol::MSG_REFUSED,                kGoldenPrefixSize,                          Protocol::PAYLOAD_NEVER },
        { Protocol::MSG_PCH_NEEDED,             sizeof( Protocol::MsgPchNeeded ),           Protocol::PAYLOAD_NEVER },
    };
    TEST_ASSERT( ( sizeof( expected ) / sizeof( expected[ 0 ] ) ) == ( Protocol::NUM_MESSAGES - 1 ) ); // A row for every type
    for ( const Expected & e : expected )
    {
        TEST_ASSERT( Protocol::GetMinMessageSize( e.m_Type ) == e.m_MinSize );
        TEST_ASSERT( Protocol::GetPayloadRule( e.m_Type ) == e.m_Payload );

        uint8_t frame[ 256 ];
        memset( frame, 0, sizeof( frame ) );
        frame[ 0 ] = (uint8_t)e.m_Type;
        frame[ 2 ] = ( e.m_Payload == Protocol::PAYLOAD_ALWAYS ) ? 1 : 0; // HasPayload
        TEST_ASSERT( Protocol::IsValidHeader( frame, e.m_MinSize - 1 ) == false );
        TEST_ASSERT( Protocol::IsValidHeader( frame, e.m_MinSize ) );
        TEST_ASSERT( Protocol::IsValidHeader( frame, e.m_MinSize + 8 ) );

        // The payload has to be announced exactly when the type has one
        frame[ 2 ] = ( e.m_Payload == Protocol::PAYLOAD_ALWAYS ) ? 0 : 1;
        TEST_ASSERT( Protocol::IsValidHeader( frame, e.m_MinSize ) == ( e.m_Payload == Protocol::PAYLOAD_OPTIONAL ) );
    }
    const uint8_t unknownTypes[] = { 0, (uint8_t)Protocol::NUM_MESSAGES, 200 };
    for ( const uint8_t type : unknownTypes )
    {
        const uint8_t frame[ 16 ] = { type };
        TEST_ASSERT( Protocol::IsValidHeader( frame, sizeof( frame ) ) == false );
    }
    TEST_ASSERT( Protocol::IsValidHeader( nullptr, 0 ) == false );
    const uint8_t tooShortForAType[ 3 ] = { Protocol::MSG_JOB, 0, 0 };
    TEST_ASSERT( Protocol::IsValidHeader( tooShortForAType, sizeof( tooShortForAType ) ) == false );

    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    // The worker closes the connection to a client that sends one, without answering
    HandshakeBytes handshake;
    const Protocol::MsgJob job( 1, 0 );
    struct Frame { const void * data; uint32_t size; };
    const Frame frames[] = {
        { handshake.bytes, 3 },                                         // Not even a header
        { handshake.bytes, kGoldenPrefixSize - 1 },                     // A handshake that stops before the version
        { &job, (uint32_t)sizeof( job ) - 1 },                          // A job without its last byte
        { handshake.bytes, kGoldenConnectionSize - 1 },                 // A handshake of this version that is too short for it
    };
    for ( const Frame & frame : frames )
    {
        HandshakeProbe probe;
        const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
        TEST_ASSERT( ci );
        TEST_ASSERT( probe.Send( ci, frame.data, frame.size ) );
        TEST_ASSERT( WaitUntil( [ &probe ]() { return probe.IsDisconnected(); } ) );
        TEST_ASSERT( probe.GetNumMessages() == 0 );
        probe.ShutdownAllConnections();
    }
}

// HeadersWithTheWrongPayloadAreRejected
//------------------------------------------------------------------------------
void TestDistributed::HeadersWithTheWrongPayloadAreRejected() const
{
    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    // A header that announces a payload its type does not have makes the next frame a payload, and the
    // handshake handler would then read the header as the whole message. Only the header is that long.
    HandshakeBytes handshake;
    uint8_t connectionHeader[ sizeof( handshake.bytes ) ]; // A whole handshake, so that only the flag is wrong
    memcpy( connectionHeader, handshake.bytes, sizeof( connectionHeader ) );
    connectionHeader[ 2 ] = 1;

    // and a header without the payload its type always has leaves the handler with nothing to read
    const Protocol::MsgJob job( 1, 0 );
    uint8_t jobHeader[ sizeof( job ) ];
    memcpy( jobHeader, &job, sizeof( job ) );
    jobHeader[ 2 ] = 0;

    struct Case { const void * m_Header; uint32_t m_HeaderSize; const void * m_Payload; uint32_t m_PayloadSize; };
    const Case cases[] = {
        { connectionHeader, sizeof( connectionHeader ), handshake.bytes, sizeof( handshake.bytes ) },
        { jobHeader, sizeof( jobHeader ), nullptr, 0 },
    };
    for ( const Case & c : cases )
    {
        HandshakeProbe probe;
        const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
        TEST_ASSERT( ci );
        TEST_ASSERT( probe.Send( ci, c.m_Header, c.m_HeaderSize ) );
        if ( c.m_Payload )
        {
            probe.Send( ci, c.m_Payload, c.m_PayloadSize ); // The worker may have closed the connection already
        }
        TEST_ASSERT( WaitUntil( [ &probe ]() { return probe.IsDisconnected(); } ) );
        TEST_ASSERT( probe.GetNumMessages() == 0 ); // No ack
        probe.ShutdownAllConnections();
    }
}

// LongerMessagesAreAccepted
//------------------------------------------------------------------------------
void TestDistributed::LongerMessagesAreAccepted() const
{
    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    // A later minor version may add to a message, and only the part that is known is read
    HandshakeBytes handshake;
    uint8_t longer[ sizeof( handshake.bytes ) + 8 ];
    memset( longer, 0xAB, sizeof( longer ) );
    memcpy( longer, handshake.bytes, sizeof( handshake.bytes ) );
    longer[ 1 ] = (uint8_t)sizeof( longer ); // Its own size

    HandshakeProbe probe;
    const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
    TEST_ASSERT( ci );
    TEST_ASSERT( probe.Send( ci, longer, sizeof( longer ) ) );
    TEST_ASSERT( WaitUntil( [ &probe ]() { return ( probe.GetNumMessages() >= 3 ); } ) );
    TEST_ASSERT( probe.GetMessageType( 0 ) == Protocol::MSG_CONNECTION_ACK );
    TEST_ASSERT( probe.IsDisconnected() == false );
    probe.ShutdownAllConnections();
}

// HostNameStaysBounded
//------------------------------------------------------------------------------
void TestDistributed::HostNameStaysBounded() const
{
    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    // A client does not have to terminate its host name
    HandshakeBytes handshake;
    handshake.FillHostName( 'A' );

    HandshakeProbe probe;
    const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
    TEST_ASSERT( ci );
    TEST_ASSERT( probe.Send( ci, handshake.bytes, sizeof( handshake.bytes ) ) );
    TEST_ASSERT( WaitUntil( [ &probe ]() { return ( probe.GetNumMessages() >= 1 ); } ) );

    uint32_t hostNameLength = 0;
    {
        MutexHolder mh( s.m_ClientListMutex );
        TEST_ASSERT( s.m_ClientList.GetSize() == 1 );
        MutexHolder mh2( s.m_ClientList[ 0 ]->m_Mutex );
        hostNameLength = (uint32_t)s.m_ClientList[ 0 ]->m_HostName.GetLength();
    }
    TEST_ASSERT( hostNameLength == 64 );
    probe.ShutdownAllConnections();
}

// HostNameIsBoundedByTheFrame
//------------------------------------------------------------------------------
#if defined( __WINDOWS__ )
void TestDistributed::HostNameIsBoundedByTheFrame() const
{
    // A frame that ends right before an inaccessible page: reading past it faults
    const size_t pageSize = 4096;
    char * pages = (char *)::VirtualAlloc( nullptr, pageSize * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    TEST_ASSERT( pages );
    DWORD oldProtection = 0;
    TEST_ASSERT( ::VirtualProtect( pages + pageSize, pageSize, PAGE_NOACCESS, &oldProtection ) );

    HandshakeBytes handshake;
    handshake.SetHostName( "build-pc" );
    struct Case { uint32_t m_FrameSize; const char * m_Expected; };
    const Case cases[] = {
        { 0, "" }, { 8, "" }, { kGoldenHostNameOffset, "" },    // Stops before the host name
        { kGoldenHostNameOffset + 4, "buil" },                  // or in the middle of it
        { kGoldenMajor23ConnectionSize, "build-pc" },
        { kGoldenConnectionSize, "build-pc" },
    };
    for ( const Case & c : cases )
    {
        char * frame = ( pages + pageSize - c.m_FrameSize );
        memcpy( frame, handshake.bytes, c.m_FrameSize );
        AStackString<> name;
        reinterpret_cast<const Protocol::MsgConnection *>( frame )->GetHostName( c.m_FrameSize, name );
        TEST_ASSERT( name == c.m_Expected );
    }

    // and a name that is not terminated stops where the message does
    handshake.FillHostName( 'A' );
    char * frame = ( pages + pageSize - kGoldenMajor23ConnectionSize );
    memcpy( frame, handshake.bytes, kGoldenMajor23ConnectionSize );
    AStackString<> name;
    reinterpret_cast<const Protocol::MsgConnection *>( frame )->GetHostName( kGoldenMajor23ConnectionSize, name );
    TEST_ASSERT( name.GetLength() == 64 );

    ::VirtualFree( pages, 0, MEM_RELEASE );
}
#endif

// CapabilitiesAreExchanged
//------------------------------------------------------------------------------
void TestDistributed::CapabilitiesAreExchanged() const
{
    // The worker tells the client what both have: what the client advertised and the worker knows
    {
        Server s( 1 );
        TEST_ASSERT( s.Listen( Protocol::kTestPort ) );
        HandshakeProbe probe;
        const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
        TEST_ASSERT( ci );
        HandshakeBytes handshake( Protocol::CAP_TOOLCHAIN_INVENTORY | ( 1u << 31 ) ); // One it has, and one nobody does
        TEST_ASSERT( probe.Send( ci, handshake.bytes, sizeof( handshake.bytes ) ) );
        TEST_ASSERT( WaitUntil( [ &probe ]() { return ( probe.GetNumMessages() >= 1 ); } ) );
        TEST_ASSERT( probe.GetAckCapabilities() == Protocol::CAP_TOOLCHAIN_INVENTORY );
        probe.ShutdownAllConnections();
    }

    // The client has to say what it has: the worker does not assume anything
    {
        NoCapacityWorker worker;
        TEST_ASSERT( BuildPchLocal( worker ).m_Built );
        TEST_ASSERT( worker.GetNumConnections() >= 1 );
        TEST_ASSERT( worker.GetClientCapabilities() == Protocol::kCapabilities );
    }
    {
        NoCapacityWorker worker;
        TEST_ASSERT( BuildPchLocal( worker, 10.0f, Protocol::CAP_PCH_INVENTORY ).m_Built );
        TEST_ASSERT( worker.GetClientCapabilities() == Protocol::CAP_PCH_INVENTORY );
    }
}

// ServerSendsOnlyWhatTheClientAdvertises
//------------------------------------------------------------------------------
void TestDistributed::ServerSendsOnlyWhatTheClientAdvertises() const
{
    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    struct Case { uint32_t capabilities; size_t numMessages; };
    const Case cases[] = {
        { 0, 1 },                                                                           // The ack alone
        { Protocol::CAP_PCH_INVENTORY, 2 },                                                 // and the PCH inventory
        { Protocol::CAP_PCH_INVENTORY | Protocol::CAP_TOOLCHAIN_INVENTORY, 3 },             // and the toolchain inventory
        { Protocol::CAP_TOOLCHAIN_INVENTORY | ( 1u << 31 ), 2 },                            // A bit nobody knows is ignored
    };
    for ( const Case & c : cases )
    {
        HandshakeProbe probe;
        const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
        TEST_ASSERT( ci );
        HandshakeBytes handshake( c.capabilities );
        TEST_ASSERT( probe.Send( ci, handshake.bytes, sizeof( handshake.bytes ) ) );
        TEST_ASSERT( WaitUntil( [ & ]() { return ( probe.GetNumMessages() >= c.numMessages ); } ) );
        Thread::Sleep( 200 ); // Anything more would have come by now
        TEST_ASSERT( probe.GetNumMessages() == c.numMessages );
        TEST_ASSERT( probe.GetMessageType( 0 ) == Protocol::MSG_CONNECTION_ACK );
        probe.ShutdownAllConnections();
    }
}

// ClientRefusesAWorkerThatRefusesIt, ClientRefusesAnAckOfAnotherMajor, ClientRejectsAShortAck, ClientRejectsAnAckWithAPayload
//------------------------------------------------------------------------------
namespace
{
    // How long a client waits before it tries a worker again in these tests, and how many of them a build
    // has to last for "not again" to mean something
    constexpr float kShortReconnectDelay = 0.05f;
    constexpr float kMinBuildSeconds = ( 4 * kShortReconnectDelay );

    // The text of the line that says a worker's version is not this one
    void FormatRefusal( AString & outText, uint32_t workerMajor )
    {
        outText.Format( "worker 127.0.0.1 runs protocol v%u.%u, this FBuild is v%u.%u: update FBuild and FBuildWorker together",
                        workerMajor, (uint32_t)Protocol::kVersionMinor, Protocol::kVersionMajor, (uint32_t)Protocol::kVersionMinor );
    }

    // The attempts a client makes to a worker are at least a reconnect delay apart
    bool AreSpacedByTheReconnectDelay( const FakeWorker & worker )
    {
        Array<float> times;
        worker.GetConnectionTimes( times );
        for ( size_t i = 1; i < times.GetSize(); ++i )
        {
            if ( ( times[ i ] - times[ i - 1 ] ) < ( kShortReconnectDelay * 0.9f ) )
            {
                return false;
            }
        }
        return true;
    }
}

void TestDistributed::ClientRefusesAWorkerThatRefusesIt() const
{
    OddHandshakeWorker worker( OddHandshakeWorker::REFUSE_AS_NEWER_MAJOR );
    const LocalBuildResult result = BuildPchLocal( worker, kShortReconnectDelay );
    TEST_ASSERT( result.m_Built );
    TEST_ASSERT( result.m_Seconds >= kMinBuildSeconds );

    // The user is told once, with both versions, and the worker is left alone for the rest of the build
    AStackString expected;
    FormatRefusal( expected, Protocol::kVersionMajor + 1 );
    TEST_ASSERT( CountOccurrences( GetRecordedOutput(), expected.Get() ) == 1 );
    TEST_ASSERT( result.m_MaxDenyListed == 1 );
    TEST_ASSERT( worker.GetNumConnections() == 1 );
}

void TestDistributed::ClientRefusesAnAckOfAnotherMajor() const
{
    OddHandshakeWorker worker( OddHandshakeWorker::ACK_OF_OLDER_MAJOR );
    const LocalBuildResult result = BuildPchLocal( worker, kShortReconnectDelay );
    TEST_ASSERT( result.m_Built );
    TEST_ASSERT( result.m_Seconds >= kMinBuildSeconds );

    AStackString expected;
    FormatRefusal( expected, Protocol::kVersionMajor - 1 );
    TEST_ASSERT( CountOccurrences( GetRecordedOutput(), expected.Get() ) == 1 );
    TEST_ASSERT( result.m_MaxDenyListed == 1 );
    TEST_ASSERT( worker.GetNumConnections() == 1 );
}

void TestDistributed::ClientRejectsAShortAck() const
{
    OddHandshakeWorker worker( OddHandshakeWorker::ACK_TOO_SHORT );
    const LocalBuildResult result = BuildPchLocal( worker, kShortReconnectDelay );
    TEST_ASSERT( result.m_Built );
    TEST_ASSERT( result.m_Seconds >= kMinBuildSeconds );

    // An ack of this version that stops short is a broken worker, not another version. It is dropped and
    // the user is told once that the handshake did not finish, and it is tried again, but not at once.
    const AString & output = GetRecordedOutput();
    TEST_ASSERT( CountOccurrences( output, "closed the connection before the handshake finished" ) == 1 );
    TEST_ASSERT( CountOccurrences( output, " runs protocol v" ) == 0 );
    TEST_ASSERT( result.m_MaxDenyListed == 0 );
    TEST_ASSERT( worker.GetNumConnections() >= 2 );
    TEST_ASSERT( AreSpacedByTheReconnectDelay( worker ) );
}

void TestDistributed::ClientRejectsAnAckWithAPayload() const
{
    // The header of an ack that announces a payload is not an ack: it is dropped like a broken worker
    OddHandshakeWorker worker( OddHandshakeWorker::ACK_WITH_PAYLOAD );
    const LocalBuildResult result = BuildPchLocal( worker, kShortReconnectDelay );
    TEST_ASSERT( result.m_Built );
    TEST_ASSERT( result.m_Seconds >= kMinBuildSeconds );

    TEST_ASSERT( CountOccurrences( GetRecordedOutput(), "closed the connection before the handshake finished" ) == 1 );
    TEST_ASSERT( result.m_MaxDenyListed == 0 );
    TEST_ASSERT( worker.GetNumConnections() >= 2 );
    TEST_ASSERT( AreSpacedByTheReconnectDelay( worker ) );
}

// ClientDoesNotWaitForInventoriesTheWorkerLacks
//------------------------------------------------------------------------------
void TestDistributed::ClientDoesNotWaitForInventoriesTheWorkerLacks() const
{
    // A worker that does not have an inventory does not send it, so jobs must not wait for it
    const uint32_t workerCapabilities[] = {
        0,
        Protocol::CAP_PCH_INVENTORY,
        Protocol::CAP_TOOLCHAIN_INVENTORY,
        Protocol::CAP_PCH_INVENTORY | Protocol::CAP_TOOLCHAIN_INVENTORY,
    };
    for ( const uint32_t capabilities : workerCapabilities )
    {
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );

        CorruptResultWorker worker( capabilities );
        TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );

        auto never = []() { return false; };
        BuildAborter aborter( never, 20.0f );
        fBuild.Build( "../tmp/Test/Distributed/ResultCompression/ResultCompression.lib" );
        worker.ShutdownAllConnections();

        TEST_ASSERT( aborter.HasTimedOut() == false );
        TEST_ASSERT( worker.GetNumResultsSent() >= 1 ); // A job reached the worker
    }
}

// ClientIgnoresCapabilitiesItDidNotAdvertise
//------------------------------------------------------------------------------
void TestDistributed::ClientIgnoresCapabilitiesItDidNotAdvertise() const
{
    // A worker that claims more than the client advertised, but sends only what was asked for, must not
    // be waited on for the rest
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true;
    options.m_DistributionCapabilities = Protocol::CAP_PCH_INVENTORY;
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    CorruptResultWorker worker( Protocol::CAP_PCH_INVENTORY, Protocol::kCapabilities ); // Sends one, claims both
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );

    auto never = []() { return false; };
    BuildAborter aborter( never, 20.0f );
    fBuild.Build( "../tmp/Test/Distributed/ResultCompression/ResultCompression.lib" );
    worker.ShutdownAllConnections();

    TEST_ASSERT( aborter.HasTimedOut() == false );
    TEST_ASSERT( worker.GetClientCapabilities() == Protocol::CAP_PCH_INVENTORY );
    TEST_ASSERT( worker.GetNumResultsSent() >= 1 );
}

// MSVCWorkerFailuresAreRetriedElsewhere
//------------------------------------------------------------------------------
void TestDistributed::MSVCWorkerFailuresAreRetriedElsewhere() const
{
    // What the compiler prints when the worker is at fault, not the code: the job is tried on another worker
    const char * const workerFailures[] =
    {
        "c1xx : fatal error C1356: unable to find mspdbcore.dll",
        "fatal error C1902: Program database manager mismatch; please check your installation",
        "fatal error C1082: Cannot access program database",
        "fatal error C1085: Cannot write compiler generated file",
        "fatal error C1088: Cannot flush compiler generated file",
        "fatal error C1083: Cannot open compiler intermediate file",
        "fatal error C1060: compiler is out of heap space",
        "fatal error C1001: Internal compiler error",
        "fatal error: No space left on device",
        "a.cpp(1): error C2065: 'x': undeclared identifier\nc1xx : fatal error C1356: unable to find mspdbcore.dll", // Among other output
    };
    for ( const char * text : workerFailures )
    {
        TEST_ASSERT( ObjectNode::IsMSVCSystemFailure( AStackString<>( text ) ) );
    }

    // Problems of the code are left to the user, as are settings that make the compiler run out of memory
    const char * const codeFailures[] =
    {
        "",
        "a.cpp(1): error C2065: 'x': undeclared identifier",
        "a.cpp(3): fatal error C1189: #error:  not supported",
        "fatal error C1060: compiler is out of heap space\nfatal error C1076: compiler limit : internal heap limit reached; use /Zm to specify a higher limit",
        "fatal error C1060: compiler is out of heap space\nerror C3859: virtual memory range for PCH exceeded; please recompile with a command line option of '-Zmvalue' or greater",
    };
    for ( const char * text : codeFailures )
    {
        TEST_ASSERT( ObjectNode::IsMSVCSystemFailure( AStackString<>( text ) ) == false );
    }
}

// Raw clients of a worker
//------------------------------------------------------------------------------
namespace
{
    // A job as a client sends it to a worker, taken from a build
    struct CapturedJob
    {
        uint64_t        m_ToolId = 0;
        uint64_t        m_PchId = 0;
        Array<char>     m_Data;     // The job, before it is compressed for the stream
    };

    // A worker that keeps the first job it is sent, and does not answer it
    class JobCapturingWorker : public FakeWorker
    {
    public:
        JobCapturingWorker() : FakeWorker( 4 ) {}
        explicit JobCapturingWorker( const JobCapturingWorker & other ) = delete;
        JobCapturingWorker & operator=( const JobCapturingWorker & other ) = delete;

        bool HasJob() const { return m_HasJob.Load(); }
        const CapturedJob & GetJob() const { return m_Job; } // Once there is one

    private:
        virtual void OnMessage( const ConnectionInfo *, const Protocol::IMessage & message, void * payload, uint32_t payloadSize ) override
        {
            if ( ( message.GetType() != Protocol::MSG_JOB ) || m_HasJob.Load() )
            {
                return;
            }
            void * job = nullptr;
            size_t jobSize = 0;
            if ( m_Decoder.Decode( payload, payloadSize, job, jobSize ) == false )
            {
                return;
            }
            m_Job.m_ToolId = static_cast<const Protocol::MsgJob &>( message ).GetToolId();
            m_Job.m_Data.SetSize( jobSize );
            memcpy( m_Job.m_Data.Begin(), job, jobSize );
            ConstMemoryStream ms( job, jobSize );
            const Job parsed( ms );
            m_Job.m_PchId = parsed.GetPchId();
            FREE( job );
            m_HasJob.Store( true );
        }

        JobStreamDecoder m_Decoder;
        CapturedJob m_Job;
        Atomic<bool> m_HasJob{ false };
    };

    // The job as it would be sent if it used another PCH. A job that was read cannot be written again
    // (it has no node to write), so its id is replaced where Job::Serialize puts it, at the end.
    CapturedJob WithPch( const CapturedJob & job, uint64_t pchId )
    {
        TEST_ASSERT( job.m_PchId != 0 );
        TEST_ASSERT( job.m_Data.GetSize() > sizeof( uint64_t ) );
        CapturedJob result = job;
        char * idBytes = ( result.m_Data.Begin() + result.m_Data.GetSize() - sizeof( uint64_t ) );
        uint64_t oldPchId = 0;
        memcpy( &oldPchId, idBytes, sizeof( oldPchId ) );
        TEST_ASSERT( oldPchId == job.m_PchId ); // It is where it is looked for
        memcpy( idBytes, &pchId, sizeof( pchId ) );
        result.m_PchId = pchId;

        ConstMemoryStream in( result.m_Data.Begin(), result.m_Data.GetSize() );
        const Job parsed( in );
        TEST_ASSERT( parsed.GetPchId() == pchId );
        return result;
    }

    // Sends a job to a worker as the client that made it would
    bool SendCapturedJob( const ConnectionInfo * connection, JobStreamEncoder & encoder, const CapturedJob & job )
    {
        if ( encoder.Encode( job.m_Data.Begin(), job.m_Data.GetSize() ) == false )
        {
            return false;
        }
        const Protocol::MsgJob msg( job.m_ToolId, 0 );
        const ConstMemoryStream payload( encoder.GetResult(), encoder.GetResultSize() );
        return msg.Send( connection, payload );
    }

    // Sends the handshake of a client advertising the capabilities and waits for the ack and inventories
    // that answer it. Sent after other messages it also says they have been dealt with, as a connection
    // is served in order: whatever they made the worker send came before the ack.
    bool Handshake( HandshakeProbe & probe, const ConnectionInfo * connection, uint32_t capabilities, float timeoutSeconds )
    {
        const size_t numBefore = probe.GetNumMessages();
        const HandshakeBytes handshake( capabilities );
        if ( probe.Send( connection, handshake.bytes, sizeof( handshake.bytes ) ) == false )
        {
            return false;
        }
        const Timer timer;
        const size_t numExpected = 1u + ( ( capabilities & Protocol::CAP_PCH_INVENTORY ) ? 1u : 0u ) + ( ( capabilities & Protocol::CAP_TOOLCHAIN_INVENTORY ) ? 1u : 0u );
        while ( probe.GetNumMessages() < ( numBefore + numExpected ) )
        {
            if ( timer.GetElapsed() > timeoutSeconds )
            {
                return false;
            }
            Thread::Sleep( 1 );
        }
        return true;
    }

    // Connects to a worker and completes the handshake, as a client advertising the capabilities would
    bool ConnectAndHandshake( HandshakeProbe & probe, uint32_t capabilities, const ConnectionInfo * & outConnection, float timeoutSeconds )
    {
        outConnection = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
        return ( outConnection != nullptr ) && Handshake( probe, outConnection, capabilities, timeoutSeconds );
    }
}

// Runs a build that sends a job for a PCH to a worker, and keeps the job
void TestDistributed::CapturePchJob( CapturedJob & outJob ) const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // jobs stay queued for the remote worker
    options.m_AllowLocalRace = false;
    options.m_ForceCleanBuild = true;
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    JobCapturingWorker worker;
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );

    auto reached = [ &worker ]() { return worker.HasJob(); };
    BuildAborter aborter( reached, kWaitTimeoutSeconds );
    fBuild.Build( "../tmp/Test/Distributed/distpch.lib" );
    worker.ShutdownAllConnections();

    TEST_ASSERT( aborter.HasReached() );
    outJob.m_ToolId = worker.GetJob().m_ToolId;
    outJob.m_PchId = worker.GetJob().m_PchId;
    outJob.m_Data = worker.GetJob().m_Data;
    TEST_ASSERT( outJob.m_ToolId != 0 );
    TEST_ASSERT( outJob.m_PchId != 0 );
}

// The jobs waiting on all the connections of a worker
/*static*/ uint32_t TestDistributed::CountWaitingJobs( Server & server )
{
    uint32_t numWaiting = 0;
    MutexHolder mh( server.m_ClientListMutex );
    for ( Server::ClientState * cs : server.m_ClientList )
    {
        MutexHolder mh2( cs->m_Mutex );
        numWaiting += (uint32_t)cs->m_WaitingJobs.GetSize();
    }
    return numWaiting;
}

/*static*/ void TestDistributed::DropWaitingJobs( Server & server )
{
    MutexHolder mh( server.m_ClientListMutex );
    for ( Server::ClientState * cs : server.m_ClientList )
    {
        MutexHolder mh2( cs->m_Mutex );
        for ( Job * waiting : cs->m_WaitingJobs )
        {
            FDELETE waiting;
        }
        cs->m_WaitingJobs.Clear();
    }
}

// MSVCFailureOfARemoteJobIsASystemError
//------------------------------------------------------------------------------
void TestDistributed::MSVCFailureOfARemoteJobIsASystemError() const
{
    CapturedJob captured;
    CapturePchJob( captured );

    // As a worker handles the result of a compile that failed
    auto systemErrorsAfter = [ & ]( const char * output )
    {
        ConstMemoryStream in( captured.m_Data.Begin(), captured.m_Data.GetSize() );
        Job job( in );
        ObjectNode::HandleSystemFailures( &job, 2, AStackString<>( output ), AString::GetEmpty() );
        return job.GetSystemErrorCount();
    };
    TEST_ASSERT( systemErrorsAfter( "c1xx : fatal error C1356: unable to find mspdbcore.dll" ) == 1 );
    TEST_ASSERT( systemErrorsAfter( "a.cpp(1): error C2065: 'x': undeclared identifier" ) == 0 );
}

// WorkerAsksForAMissingPch
//------------------------------------------------------------------------------
void TestDistributed::WorkerAsksForAMissingPch() const
{
    CapturedJob job;
    CapturePchJob( job );

    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );
    HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    TEST_ASSERT( ConnectAndHandshake( probe, Protocol::kCapabilities, ci, kWaitTimeoutSeconds ) );

    // A job for a PCH the worker does not have: it asks for it, naming it
    JobStreamEncoder encoder;
    TEST_ASSERT( SendCapturedJob( ci, encoder, job ) );
    TEST_ASSERT( WaitUntil( [ &probe ]() { return ( probe.GetNumPchNeeded() >= 1 ); } ) );
    TEST_ASSERT( probe.GetPchNeededId( 0 ) == job.m_PchId );

    // More jobs for it do not make it ask again, as it is not here yet
    TEST_ASSERT( SendCapturedJob( ci, encoder, job ) );
    TEST_ASSERT( SendCapturedJob( ci, encoder, job ) );
    TEST_ASSERT( Handshake( probe, ci, Protocol::kCapabilities, kWaitTimeoutSeconds ) ); // Answered after the jobs
    TEST_ASSERT( CountWaitingJobs( s ) == 3 );
    TEST_ASSERT( probe.GetNumPchNeeded() == 1 );
    probe.ShutdownAllConnections();
}

// WorkerDoesNotAskAClientWithoutTheCapability
//------------------------------------------------------------------------------
void TestDistributed::WorkerDoesNotAskAClientWithoutTheCapability() const
{
    CapturedJob job;
    CapturePchJob( job );

    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    // A client of 24.0 does not know the message, so it must never be sent one
    HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    const uint32_t capabilities24_0 = ( Protocol::CAP_PCH_INVENTORY | Protocol::CAP_TOOLCHAIN_INVENTORY );
    TEST_ASSERT( ConnectAndHandshake( probe, capabilities24_0, ci, kWaitTimeoutSeconds ) );
    JobStreamEncoder encoder;
    TEST_ASSERT( SendCapturedJob( ci, encoder, job ) );
    TEST_ASSERT( Handshake( probe, ci, capabilities24_0, kWaitTimeoutSeconds ) ); // Answered after the job
    TEST_ASSERT( CountWaitingJobs( s ) == 1 ); // It has the job, and the PCH is missing
    TEST_ASSERT( probe.GetNumPchNeeded() == 0 );
    probe.ShutdownAllConnections();
}

// WorkerDoesNotAskForAPchBeingReceived
//------------------------------------------------------------------------------
void TestDistributed::WorkerDoesNotAskForAPchBeingReceived() const
{
    CapturedJob job;
    CapturePchJob( job );

    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    // One connection is in the middle of sending the PCH: the first of three chunks has arrived
    HandshakeProbe receiving;
    const ConnectionInfo * receivingConnection = nullptr;
    TEST_ASSERT( ConnectAndHandshake( receiving, Protocol::kCapabilities, receivingConnection, kWaitTimeoutSeconds ) );
    Array<char> chunk;
    chunk.SetSize( Protocol::kMinPchChunkSize );
    memset( chunk.Begin(), 0, chunk.GetSize() );
    const Protocol::MsgPchFile firstChunk( job.m_PchId, 65536, ( 2 * Protocol::kMinPchChunkSize ) + 100, Protocol::kMinPchChunkSize, 0 );
    TEST_ASSERT( firstChunk.Send( receivingConnection, ConstMemoryStream( chunk.Begin(), chunk.GetSize() ) ) );
    TEST_ASSERT( WaitUntil( [ & ]()
    {
        MutexHolder mh( s.m_ClientListMutex );
        for ( const Server::ClientState * cs : s.m_ClientList )
        {
            if ( cs->m_PchReceiver.IsReceiving( job.m_PchId ) )
            {
                return true;
            }
        }
        return false;
    } ) );

    // A job for the PCH arrives on another connection: it is on its way, so it is not asked for
    HandshakeProbe waiting;
    const ConnectionInfo * waitingConnection = nullptr;
    TEST_ASSERT( ConnectAndHandshake( waiting, Protocol::kCapabilities, waitingConnection, kWaitTimeoutSeconds ) );
    JobStreamEncoder encoder;
    TEST_ASSERT( SendCapturedJob( waitingConnection, encoder, job ) );
    TEST_ASSERT( Handshake( waiting, waitingConnection, Protocol::kCapabilities, kWaitTimeoutSeconds ) ); // Answered after the job
    TEST_ASSERT( CountWaitingJobs( s ) == 1 );
    TEST_ASSERT( waiting.GetNumPchNeeded() == 0 );
    TEST_ASSERT( receiving.GetNumPchNeeded() == 0 );

    // The connection sending it goes away, so it will not arrive: the job's client is asked, once
    receiving.ShutdownAllConnections();
    TEST_ASSERT( WaitUntil( [ &waiting ]() { return ( waiting.GetNumPchNeeded() >= 1 ); } ) );
    Thread::Sleep( 100 ); // The disconnect is dealt with on another thread than the connection's, so nothing sent on it says it is done
    TEST_ASSERT( waiting.GetNumPchNeeded() == 1 );
    TEST_ASSERT( waiting.GetPchNeededId( 0 ) == job.m_PchId );
    waiting.ShutdownAllConnections();
}

// WorkerAsksAgainWhenAPchItHadIsNeededAgain
//------------------------------------------------------------------------------
void TestDistributed::WorkerAsksAgainWhenAPchItHadIsNeededAgain() const
{
    // A job for a PCH of our own
    CapturedJob captured;
    CapturePchJob( captured );
    CompressedPch pchs[ 2 ];
    for ( uint32_t i = 0; i < 2; ++i )
    {
        MakeRandomBytes( 100000, ( 31 + i ), pchs[ i ].m_Content );
        CompressPch( pchs[ i ] );
    }
    const CapturedJob job = WithPch( captured, pchs[ 0 ].m_Id );

    Server s( 1 );
    SetPchCacheLimits( s, 1, 0 ); // Room for the PCH that has just arrived only
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );
    HandshakeProbe client;
    const ConnectionInfo * ci = nullptr;
    TEST_ASSERT( ConnectAndHandshake( client, Protocol::kCapabilities, ci, kWaitTimeoutSeconds ) );

    // The PCH is asked for
    JobStreamEncoder encoder;
    TEST_ASSERT( SendCapturedJob( ci, encoder, job ) );
    TEST_ASSERT( WaitUntil( [ &client ]() { return ( client.GetNumPchNeeded() >= 1 ); } ) );
    TEST_ASSERT( client.GetPchNeededId( 0 ) == pchs[ 0 ].m_Id );

    // and sent, over another connection. The job waits for its toolchain, which never comes.
    PchSender sender;
    TEST_ASSERT( sender.ConnectToWorker() );
    TEST_ASSERT( SendPch( sender, pchs[ 0 ] ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.IsPchCached( pchs[ 0 ].m_Id ); } ) );

    // The job is gone (as one that has been built is). Another PCH arrives and pushes the first out, once
    // it is not being unpacked: a PCH that is is not pushed out.
    TEST_ASSERT( WaitUntil( [ & ]() { return IsPchUnpacked( s, pchs[ 0 ].m_Id ); } ) );
    DropWaitingJobs( s );
    TEST_ASSERT( SendPch( sender, pchs[ 1 ] ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.IsPchCached( pchs[ 1 ].m_Id ) && ( s.IsPchCached( pchs[ 0 ].m_Id ) == false ); } ) );

    // A job for the first one again: the worker asks again, as the PCH has arrived since it asked
    TEST_ASSERT( SendCapturedJob( ci, encoder, job ) );
    TEST_ASSERT( WaitUntil( [ &client ]() { return ( client.GetNumPchNeeded() >= 2 ); } ) );
    TEST_ASSERT( Handshake( client, ci, Protocol::kCapabilities, kWaitTimeoutSeconds ) ); // Answered after the job
    TEST_ASSERT( client.GetNumPchNeeded() == 2 );
    TEST_ASSERT( client.GetPchNeededId( 1 ) == pchs[ 0 ].m_Id );
    client.ShutdownAllConnections();
}

// Fake workers that lose PCHs
//------------------------------------------------------------------------------
namespace
{
    // A worker that is sent PCHs and holds the jobs for them, as a worker does, but that loses them
    class PchLosingWorker : public FakeWorker
    {
    public:
        enum Mode
        {
            KEEP_THE_SECOND_COPY,   // Loses the first copy of a PCH, as one that evicts it at once does, then keeps it
            LOSE_EVERY_COPY,        // Never keeps one
        };

        explicit PchLosingWorker( Mode mode ) : FakeWorker( 4 ), m_Mode( mode ) {}
        explicit PchLosingWorker( const PchLosingWorker & other ) = delete;
        PchLosingWorker & operator=( const PchLosingWorker & other ) = delete;

        uint32_t GetNumPchsReceived() const { return m_NumPchsReceived.Load(); }   // Complete ones
        uint32_t GetNumRequestsSent() const { return m_NumRequestsSent.Load(); }
        uint32_t GetNumResultsSent() const { return m_NumResultsSent.Load(); }

    private:
        struct HeldJob
        {
            uint32_t m_JobId;
            uint64_t m_PchId;
        };

        virtual void OnMessage( const ConnectionInfo * connection, const Protocol::IMessage & message, void * payload, uint32_t payloadSize ) override
        {
            if ( message.GetType() == Protocol::MSG_PCH_FILE )
            {
                const Protocol::MsgPchFile & msg = static_cast<const Protocol::MsgPchFile &>( message );
                if ( ( msg.GetChunkIndex() + 1 ) == Protocol::MsgPchFile::GetNumChunks( msg.GetCompressedSize(), msg.GetChunkSize() ) )
                {
                    OnPchComplete( connection, msg.GetPchId() );
                }
            }
            else if ( message.GetType() == Protocol::MSG_JOB )
            {
                void * job = nullptr;
                size_t jobSize = 0;
                if ( m_Decoder.Decode( payload, payloadSize, job, jobSize ) )
                {
                    ConstMemoryStream ms( job, jobSize );
                    const Job parsed( ms );
                    const uint32_t jobId = parsed.GetJobId();
                    const uint64_t pchId = parsed.GetPchId();
                    FREE( job );
                    OnJob( connection, jobId, pchId );
                }
            }
        }

        void OnJob( const ConnectionInfo * connection, uint32_t jobId, uint64_t pchId )
        {
            if ( ( pchId == 0 ) || m_Kept.Find( pchId ) )
            {
                SendResult( connection, jobId );
                return;
            }

            // Held until the PCH is here. It is asked for if the copy that came was lost.
            m_Held.Append( HeldJob{ jobId, pchId } );
            if ( m_Lost.Find( pchId ) && ( m_Asked.Find( pchId ) == nullptr ) )
            {
                m_Asked.Append( pchId );
                Ask( connection, pchId );
            }
        }

        void OnPchComplete( const ConnectionInfo * connection, uint64_t pchId )
        {
            m_NumPchsReceived.Increment();
            m_Asked.FindAndErase( pchId );

            if ( ( m_Mode == KEEP_THE_SECOND_COPY ) && m_Lost.Find( pchId ) )
            {
                m_Kept.Append( pchId );
                for ( size_t i = m_Held.GetSize(); i > 0; --i )
                {
                    if ( m_Held[ i - 1 ].m_PchId == pchId )
                    {
                        SendResult( connection, m_Held[ i - 1 ].m_JobId );
                        m_Held.EraseIndex( i - 1 );
                    }
                }
                return;
            }

            // Lost: jobs that wait for it make the worker ask for it again
            if ( m_Lost.Find( pchId ) == nullptr )
            {
                m_Lost.Append( pchId );
            }
            for ( const HeldJob & held : m_Held )
            {
                if ( held.m_PchId == pchId )
                {
                    m_Asked.Append( pchId );
                    Ask( connection, pchId );
                    break;
                }
            }
        }

        void Ask( const ConnectionInfo * connection, uint64_t pchId )
        {
            const Protocol::MsgPchNeeded msg( pchId );
            msg.Send( connection );
            m_NumRequestsSent.Increment();
        }

        void SendResult( const ConnectionInfo * connection, uint32_t jobId )
        {
            // A result that is not valid compressed data: the job fails, but it is finished, which is what is looked at
            MemoryStream ms;
            ms.Write( jobId );
            ms.Write( AStackString( "lost" ) );
            ms.Write( true );  // Built ok, so the data should be object files
            ms.Write( false ); // Not a system error
            ms.Write( Array<AString>() );
            ms.Write( (uint32_t)0 ); // Build time
            ms.Write( (uint16_t)0 ); // Thread
            char garbage[ 64 ];
            memset( garbage, 0xFF, sizeof( garbage ) );
            ms.Write( (uint32_t)sizeof( garbage ) );
            ms.WriteBuffer( garbage, sizeof( garbage ) );
            const Protocol::MsgJobResultCompressed msg( 4 );
            msg.Send( connection, ms );
            m_NumResultsSent.Increment();
        }

        const Mode m_Mode;
        JobStreamDecoder m_Decoder;
        Array<HeldJob> m_Held;
        Array<uint64_t> m_Lost;     // PCHs a copy was received of and dropped
        Array<uint64_t> m_Kept;
        Array<uint64_t> m_Asked;    // Asked for, and not received since
        Atomic<uint32_t> m_NumPchsReceived{ 0 };
        Atomic<uint32_t> m_NumRequestsSent{ 0 };
        Atomic<uint32_t> m_NumResultsSent{ 0 };
    };

    // A worker that asks for a PCH nobody has when it is sent its first job, and does nothing else
    class UnknownPchAskingWorker : public FakeWorker
    {
    public:
        static constexpr uint64_t kUnknownPchId = 0xDEAD0001;

        UnknownPchAskingWorker() : FakeWorker( 4 ) {}
        explicit UnknownPchAskingWorker( const UnknownPchAskingWorker & other ) = delete;
        UnknownPchAskingWorker & operator=( const UnknownPchAskingWorker & other ) = delete;

        uint32_t GetNumJobsReceived() const { return m_NumJobsReceived.Load(); }

    private:
        virtual void OnMessage( const ConnectionInfo * connection, const Protocol::IMessage & message, void *, uint32_t ) override
        {
            if ( ( message.GetType() == Protocol::MSG_JOB ) && ( m_NumJobsReceived.Increment() == 1 ) )
            {
                const Protocol::MsgPchNeeded msg( kUnknownPchId );
                msg.Send( connection );
            }
        }

        Atomic<uint32_t> m_NumJobsReceived{ 0 };
    };
}

// ClientSendsAPchAgainWhenAsked
//------------------------------------------------------------------------------
void TestDistributed::ClientSendsAPchAgainWhenAsked() const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // the jobs can only finish on the worker
    options.m_AllowLocalRace = false; // a job that is stuck on the worker is not rescued by building it here too
    options.m_ForceCleanBuild = true;
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    // The worker drops the PCH once it has it, and holds the jobs for it until a second copy comes. They
    // only finish if the client sends it again when asked.
    PchLosingWorker worker( PchLosingWorker::KEEP_THE_SECOND_COPY );
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );

    // What it answers with is not object files, so the build itself fails: what counts is that it ends
    auto never = []() { return false; };
    BuildAborter aborter( never, 20.0f );
    fBuild.Build( "../tmp/Test/Distributed/distpch.lib" );
    worker.ShutdownAllConnections();

    TEST_ASSERT( aborter.HasTimedOut() == false );
    TEST_ASSERT( worker.GetNumRequestsSent() >= 1 );
    TEST_ASSERT( worker.GetNumPchsReceived() >= 2 );
    TEST_ASSERT( worker.GetNumResultsSent() >= 1 );
}

// ClientGivesUpOnAWorkerThatKeepsLosingAPch
//------------------------------------------------------------------------------
void TestDistributed::ClientGivesUpOnAWorkerThatKeepsLosingAPch() const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_AllowLocalRace = false; // a job that is stuck on the worker is not rescued by building it here too
    options.m_ForceCleanBuild = true;
    options.m_DistributionReconnectDelay = 600.0f; // Not tried again within the build
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    PchLosingWorker worker( PchLosingWorker::LOSE_EVERY_COPY );
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );

    // The client sends the PCH again a number of times, then closes the connection and does not use the worker
    // again, so that the jobs held for it are built here
    DenyListPoller poller( fBuild );
    auto never = []() { return false; };
    BuildAborter aborter( never, 60.0f );
    const bool built = fBuild.Build( "../tmp/Test/Distributed/distpch.lib" );
    worker.ShutdownAllConnections();

    TEST_ASSERT( aborter.HasTimedOut() == false );
    TEST_ASSERT( built );
    TEST_ASSERT( worker.GetNumPchsReceived() == ( 1 + PchSendState::kMaxResends ) ); // It never makes progress, so the bound is all of them
    TEST_ASSERT( worker.GetNumResultsSent() == 0 ); // Nothing came back from it: the objects were built here
    TEST_ASSERT( poller.GetMax() == 1 ); // and the worker was given up on
}

// ClientGivesUpOnAWorkerThatAsksForAnUnknownPch
//------------------------------------------------------------------------------
void TestDistributed::ClientGivesUpOnAWorkerThatAsksForAnUnknownPch() const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_AllowLocalRace = false; // a job that is stuck on the worker is not rescued by building it here too
    options.m_ForceCleanBuild = true;
    options.m_DistributionReconnectDelay = 600.0f; // Not tried again within the build
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    UnknownPchAskingWorker worker;
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );

    // The job it holds could never start: the connection is closed and the worker not used again, so that
    // the jobs go back to the queue and are built here
    DenyListPoller poller( fBuild );
    auto never = []() { return false; };
    BuildAborter aborter( never, 60.0f );
    const bool built = fBuild.Build( "../tmp/Test/Distributed/distpch.lib" );
    worker.ShutdownAllConnections();

    TEST_ASSERT( aborter.HasTimedOut() == false );
    TEST_ASSERT( built );
    TEST_ASSERT( worker.GetNumJobsReceived() >= 1 );
    TEST_ASSERT( poller.GetMax() == 1 );
    TEST_ASSERT( CountOccurrences( GetRecordedOutput(), "asked for PCH 0x00000000dead0001, which is not known here" ) == 1 );
}

// EvictedPchIsSentAgain
//------------------------------------------------------------------------------
namespace
{
    // What the worker of EvictedPchIsSentAgain does when its second job arrives
    struct SecondJobEviction
    {
        bool    m_HadAPch = false;
        bool    m_HasOnlyTheOtherPch = false;
    };

    // A worker that does something when its second job arrives, before it is dealt with
    class SecondJobHookServer : public Server
    {
    public:
        typedef void ( *Hook )( Server & server, void * user );

        SecondJobHookServer( Hook hook, void * user ) : Server( 1 ), m_Hook( hook ), m_User( user ) {}
        explicit SecondJobHookServer( const SecondJobHookServer & other ) = delete;
        SecondJobHookServer & operator=( const SecondJobHookServer & other ) = delete;

        // PCHs that started to arrive from a coordinator after the hook was done
        uint32_t GetNumPchsStartedAfterHook() const { return m_NumPchsStartedAfterHook.Load(); }

    private:
        virtual void OnReceive( const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory ) override
        {
            // A message, then its payload if it has one
            bool isMessage = false;
            {
                MutexHolder mh( m_Mutex );
                isMessage = ( m_AwaitingPayload.FindAndErase( connection ) == false );
                if ( isMessage && static_cast<const Protocol::IMessage *>( data )->HasPayload() )
                {
                    m_AwaitingPayload.Append( connection );
                }
            }
            if ( isMessage )
            {
                const Protocol::IMessage * message = static_cast<const Protocol::IMessage *>( data );
                if ( ( message->GetType() == Protocol::MSG_JOB ) && ( m_NumJobs.Increment() == 2 ) )
                {
                    m_Hook( *this, m_User );
                    m_HookDone.Store( true );
                }
                if ( ( message->GetType() == Protocol::MSG_PCH_FILE ) &&
                     ( static_cast<const Protocol::MsgPchFile *>( message )->GetChunkIndex() == 0 ) &&
                     m_HookDone.Load() )
                {
                    m_NumPchsStartedAfterHook.Increment();
                }
            }
            TestDistributed::ReceiveAsServer( *this, connection, data, size, keepMemory );
        }

        const Hook m_Hook;
        void * const m_User;
        Mutex m_Mutex;
        Array<const ConnectionInfo *> m_AwaitingPayload;
        Atomic<uint32_t> m_NumJobs{ 0 };
        Atomic<bool> m_HookDone{ false };
        Atomic<uint32_t> m_NumPchsStartedAfterHook{ 0 };
    };
}

/*static*/ void TestDistributed::ReceiveAsServer( Server & server, const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory )
{
    server.Server::OnReceive( connection, data, size, keepMemory );
}

// Another PCH arrives and pushes out the PCHs that no job uses: the one the first job used
/*static*/ void TestDistributed::EvictForTheSecondJob( Server & server, void * user )
{
    SecondJobEviction * eviction = static_cast<SecondJobEviction *>( user );

    // The first job has been built, so the worker has what it used
    {
        MutexHolder mh( server.m_PchCacheMutex );
        eviction->m_HadAPch = ( server.m_PchCache.IsEmpty() == false );
    }

    Array<char> content;
    MakeRandomBytes( 100000, 41, content );
    Compressor compressed;
    compressed.CompressZstd( content.Begin(), content.GetSize(), Compressor::kZstdTransferLevel );
    const uint64_t otherPchId = PchId::Calc( content.Begin(), content.GetSize() );
    const uint32_t compressedSize = (uint32_t)compressed.GetResultSize();

    // It is sent from a connection of its own, which is only made now, as it would take a share of the capacity of the worker
    PchSender sender;
    if ( sender.ConnectToWorker() &&
         sender.SendChunks( otherPchId, (uint32_t)content.GetSize(), compressed.GetResult(), compressedSize, Protocol::kPchChunkSize, 0, Protocol::MsgPchFile::GetNumChunks( compressedSize, Protocol::kPchChunkSize ) ) )
    {
        eviction->m_HasOnlyTheOtherPch = WaitUntil( [ & ]()
        {
            MutexHolder mh( server.m_PchCacheMutex );
            return ( server.m_PchCache.GetSize() == 1 ) && ( server.m_PchCache[ 0 ].pchId == otherPchId );
        } );
    }
}

void TestDistributed::EvictedPchIsSentAgain() const
{
    // The worker has room for one job at a time, so that the second is sent after the first is done
    struct CapacityOfOne
    {
        CapacityOfOne() : m_Original( WorkerThreadRemote::GetNumCPUsToUse() ) { WorkerThreadRemote::SetNumCPUsToUse( 1 ); }
        ~CapacityOfOne() { WorkerThreadRemote::SetNumCPUsToUse( m_Original ); }
        explicit CapacityOfOne( const CapacityOfOne & other ) = delete;
        CapacityOfOne & operator=( const CapacityOfOne & other ) = delete;
        const uint32_t m_Original;
    } capacityOfOne;

    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // the jobs can only finish on the worker
    options.m_AllowLocalRace = false; // a job that is stuck on the worker is not rescued by building it here too
    options.m_ForceCleanBuild = true;
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    // Anything but the PCH that has just arrived or is in use is evicted
    SecondJobEviction eviction;
    SecondJobHookServer s( &EvictForTheSecondJob, &eviction );
    SetPchCacheLimits( s, 1, 0 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    auto never = []() { return false; };
    BuildAborter aborter( never, 60.0f ); // A job that waits for a PCH that is not sent again does not end
    const bool built = fBuild.Build( "../tmp/Test/Distributed/distpch.lib" );

    // The PCH of the first job was gone before the second arrived, the worker asked for it, and the client
    // sent it: it only does when asked. The build finished.
    TEST_ASSERT( aborter.HasTimedOut() == false );
    TEST_ASSERT( eviction.m_HadAPch );
    TEST_ASSERT( eviction.m_HasOnlyTheOtherPch );
    TEST_ASSERT( s.GetNumPchsStartedAfterHook() >= 1 );
    TEST_ASSERT( built );
}

// PchInsertEvictsToTheBudget
//------------------------------------------------------------------------------
void TestDistributed::PchInsertEvictsToTheBudget() const
{
    Server s( 1 );
    SetPchCacheLimits( s, 1, 0 ); // Room for nothing beyond the PCH that has just arrived
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );

    PchSender sender;
    TEST_ASSERT( sender.ConnectToWorker() );
    uint64_t pchIds[ 2 ] = {};
    for ( uint32_t i = 0; i < 2; ++i )
    {
        Array<char> content;
        MakeRandomBytes( 100000, ( 21 + i ), content );
        Compressor compressed;
        compressed.CompressZstd( content.Begin(), content.GetSize(), Compressor::kZstdTransferLevel );
        pchIds[ i ] = PchId::Calc( content.Begin(), content.GetSize() );
        const uint32_t compressedSize = (uint32_t)compressed.GetResultSize();
        TEST_ASSERT( sender.SendChunks( pchIds[ i ], (uint32_t)content.GetSize(), compressed.GetResult(), compressedSize, Protocol::kPchChunkSize, 0, Protocol::MsgPchFile::GetNumChunks( compressedSize, Protocol::kPchChunkSize ) ) );
        TEST_ASSERT( WaitUntil( [ & ]() { return s.IsPchCached( pchIds[ i ] ); } ) );
    }

    // The first is gone, with its file, and the one that has just arrived is kept
    AStackString<> firstDir;
    GetWorkerPchDir( pchIds[ 0 ], firstDir );
    TEST_ASSERT( WaitUntil( [ & ]() { return ( s.IsPchCached( pchIds[ 0 ] ) == false ); } ) );
    TEST_ASSERT( FileIO::DirectoryExists( firstDir ) == false );
    TEST_ASSERT( s.IsPchCached( pchIds[ 1 ] ) );
    AStackString<> secondPath;
    TEST_ASSERT( s.FindCachedPch( pchIds[ 1 ], secondPath ) );
    TEST_ASSERT( FileIO::FileExists( secondPath.Get() ) );
}

// PchInsertCountsTheSizeOnDisk
//------------------------------------------------------------------------------
void TestDistributed::PchInsertCountsTheSizeOnDisk() const
{
    // PCHs that compress well: what the cache counts is what is on disk, which is the compressed PCH
    const uint32_t kSize = 100000;
    CompressedPch pchs[ 2 ];
    for ( uint32_t i = 0; i < 2; ++i )
    {
        pchs[ i ].m_Content.SetSize( kSize );
        memset( pchs[ i ].m_Content.Begin(), (int)( 'a' + i ), kSize );
        CompressPch( pchs[ i ] );
        TEST_ASSERT( pchs[ i ].m_CompressedSize < ( kSize / 10 ) );
    }

    // Room for one and a half of them, which is less than one unpacked: a PCH that is not
    // counted as it is stored would not fit. A second one only fits if the first goes.
    const uint64_t kStoredSize = pchs[ 0 ].m_CompressedSize;
    TEST_ASSERT( pchs[ 1 ].m_CompressedSize == kStoredSize );
    TEST_ASSERT( ( ( kStoredSize * 3 ) / 2 ) < kSize );
    Server s( 1 );
    SetPchCacheLimits( s, ( kStoredSize * 3 ) / 2, 0 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );
    PchSender sender;
    TEST_ASSERT( sender.ConnectToWorker() );

    // The first is cached as what is on disk, which is what the cache is counted by
    TEST_ASSERT( SendPch( sender, pchs[ 0 ] ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.IsPchCached( pchs[ 0 ].m_Id ); } ) );
    AStackString<> path;
    TEST_ASSERT( s.FindCachedPch( pchs[ 0 ].m_Id, path ) );
    FileStream file;
    TEST_ASSERT( file.Open( path.Get() ) );
    TEST_ASSERT( file.GetFileSize() == kStoredSize );
    file.Close();
    {
        MutexHolder mh( s.m_PchCacheMutex );
        TEST_ASSERT( s.m_PchCache.GetSize() == 1 );
        TEST_ASSERT( s.m_PchCache[ 0 ].size == kStoredSize );
    }
    TEST_ASSERT( s_LastCacheBytes.Load() == kStoredSize ); // The one that arrived

    // A second makes twice that, which is over: the first, which is the older, goes
    SetPchRecency( s, pchs[ 0 ].m_Id, 1 );
    TEST_ASSERT( SendPch( sender, pchs[ 1 ] ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.IsPchCached( pchs[ 1 ].m_Id ); } ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return ( s.IsPchCached( pchs[ 0 ].m_Id ) == false ); } ) );
    TEST_ASSERT( s_LastCacheBytes.Load() == ( 2 * kStoredSize ) );
    TEST_ASSERT( s.IsPchCached( pchs[ 1 ].m_Id ) );

    // A scan of the directory finds the sizes the inserts counted
    SetPchCacheLimits( s, ( 4 * kStoredSize ), 0 );
    s.m_PchCache.Clear();
    s.m_PchCacheScanned = false;
    s.ScanPchCache();
    TEST_ASSERT( s.IsPchCached( pchs[ 1 ].m_Id ) );
    {
        MutexHolder mh( s.m_PchCacheMutex );
        TEST_ASSERT( s.m_PchCache.GetSize() == 1 );
        TEST_ASSERT( s.m_PchCache[ 0 ].size == kStoredSize );
    }
    TEST_ASSERT( s_LastCacheBytes.Load() == kStoredSize );
}

// Compressed PCH store
//------------------------------------------------------------------------------
// The compressed PCH of the content, as a worker is sent it, in a file of the worker cache. It is
// in the cache of the Server (as a PCH that was received is), unless it is to be found by a scan.
/*static*/ void TestDistributed::AddStoredBlob( Server & server, uint64_t pchId, const void * blob, size_t blobSize, bool writeFile, AString & outStoredFile )
{
    AStackString<> dir;
    GetWorkerPchDir( pchId, dir );
    Array<AString> old;
    FileIO::GetFiles( dir, AStackString( "*" ), true, &old );
    for ( const AString & file : old )
    {
        FileIO::FileDelete( file.Get() );
    }
    outStoredFile = dir;
    outStoredFile += PchBlob::kStoredFileName;
    if ( writeFile )
    {
        FileStream f;
        TEST_ASSERT( FileIO::EnsurePathExistsForFile( outStoredFile ) );
        TEST_ASSERT( f.Open( outStoredFile.Get(), FileStream::WRITE_ONLY ) );
        TEST_ASSERT( f.Write( blob, blobSize ) == blobSize );
    }

    Server::PchCacheEntry entry;
    entry.pchId = pchId;
    entry.filePath = outStoredFile;
    entry.lastWriteTime = Time::GetCurrentFileTime();
    entry.size = blobSize;
    MutexHolder mh( server.m_PchCacheMutex );
    server.m_PchCache.Append( entry );
}

/*static*/ void TestDistributed::AddStoredPch( Server & server, uint32_t size, uint32_t seed, StoredPch & outPch )
{
    MakeTestContent( size, seed, outPch.m_Content );
    Compressor compressed;
    compressed.CompressZstd( outPch.m_Content.Begin(), outPch.m_Content.GetSize(), Compressor::kZstdTransferLevel );
    outPch.m_Id = PchId::Calc( outPch.m_Content.Begin(), outPch.m_Content.GetSize() );
    outPch.m_StoredSize = compressed.GetResultSize();
    AddStoredBlob( server, outPch.m_Id, compressed.GetResult(), compressed.GetResultSize(), true, outPch.m_StoredFile );
    TEST_ASSERT( Server::GetRawPchPath( outPch.m_StoredFile, outPch.m_RawFile ) );
}

/*static*/ Job * TestDistributed::MakePchJob( Server & server, uint64_t pchId )
{
    Job * job = FNEW( Job( nullptr ) );
    job->SetPchId( pchId );
    job->SetPchLease( server.AcquirePchLease( pchId ) );
    return job;
}

/*static*/ bool TestDistributed::IsPchUnpacked( Server & server, uint64_t pchId )
{
    MutexHolder mh( server.m_PchCacheMutex );
    const Server::PchRawEntry * raw = server.FindRawPch( pchId );
    return ( raw && ( raw->state == Server::PchRawEntry::READY ) );
}

/*static*/ bool TestDistributed::HasRawEntry( Server & server, uint64_t pchId )
{
    MutexHolder mh( server.m_PchCacheMutex );
    return ( server.FindRawPch( pchId ) != nullptr );
}

/*static*/ uint32_t TestDistributed::NumLeases( Server & server, uint64_t pchId )
{
    MutexHolder mh( server.m_PchCacheMutex );
    const Server::PchRawEntry * raw = server.FindRawPch( pchId );
    return raw ? raw->numLeases : 0;
}

/*static*/ void TestDistributed::CompressPch( CompressedPch & pch )
{
    pch.m_Compressed.CompressZstd( pch.m_Content.Begin(), pch.m_Content.GetSize(), Compressor::kZstdTransferLevel );
    pch.m_CompressedSize = (uint32_t)pch.m_Compressed.GetResultSize();
    pch.m_Id = PchId::Calc( pch.m_Content.Begin(), pch.m_Content.GetSize() );
}

/*static*/ void TestDistributed::MakeCompressedPch( uint32_t size, uint32_t seed, CompressedPch & outPch )
{
    MakeTestContent( size, seed, outPch.m_Content );
    CompressPch( outPch );
}

/*static*/ bool TestDistributed::SendPch( PchSender & sender, const CompressedPch & pch )
{
    return sender.SendChunks( pch.m_Id,
                              (uint32_t)pch.m_Content.GetSize(),
                              pch.m_Compressed.GetResult(),
                              pch.m_CompressedSize,
                              Protocol::kPchChunkSize,
                              0,
                              Protocol::MsgPchFile::GetNumChunks( pch.m_CompressedSize, Protocol::kPchChunkSize ) );
}

/*static*/ uint32_t TestDistributed::TotalLeases( Server & server )
{
    MutexHolder mh( server.m_PchCacheMutex );
    uint32_t numLeases = 0;
    for ( const Server::PchRawEntry & raw : server.m_PchRaw )
    {
        numLeases += raw.numLeases;
    }
    return numLeases;
}

/*static*/ bool TestDistributed::IsPchBeingUnpacked( Server & server, uint64_t pchId )
{
    MutexHolder mh( server.m_PchCacheMutex );
    const Server::PchRawEntry * raw = server.FindRawPch( pchId );
    return ( raw && ( raw->state == Server::PchRawEntry::UNPACKING ) );
}

// The jobs waiting on the connections of a worker that have been given their PCH
/*static*/ uint32_t TestDistributed::NumWaitingJobsWithAPch( Server & server )
{
    uint32_t numJobs = 0;
    MutexHolder mh( server.m_ClientListMutex );
    for ( Server::ClientState * cs : server.m_ClientList )
    {
        MutexHolder mh2( cs->m_Mutex );
        for ( const Job * job : cs->m_WaitingJobs )
        {
            numJobs += job->GetPchCachePath().IsEmpty() ? 0 : 1;
        }
    }
    return numJobs;
}

// Makes a PCH the least recently used, or any other place in the order, without waiting for the clock
/*static*/ void TestDistributed::SetPchRecency( Server & server, uint64_t pchId, uint64_t fileTime )
{
    MutexHolder mh( server.m_PchCacheMutex );
    Server::PchCacheEntry * stored = server.FindStoredPch( pchId );
    TEST_ASSERT( stored );
    stored->lastWriteTime = fileTime;
}

// Resolves the PCH of jobs, as the threads that serve connections do
/*static*/ uint32_t TestDistributed::ResolveThread( void * param )
{
    ResolveRequest * request = static_cast<ResolveRequest *>( param );
    while ( request->m_Go->Load() == false )
    {
        Thread::Sleep( 1 );
    }
    const Timer timer;
    for ( ;; )
    {
        bool all = true;
        for ( Job * job : request->m_Jobs )
        {
            if ( job->GetPchCachePath().IsEmpty() && ( request->m_Server->TryResolvePch( *job ) == false ) )
            {
                all = false;
            }
        }
        if ( all )
        {
            request->m_Resolved = true;
            break;
        }
        if ( timer.GetElapsed() > kWaitTimeoutSeconds )
        {
            break;
        }
        Thread::Sleep( 1 );
    }
    return 0;
}

// PchIsStoredCompressed
//------------------------------------------------------------------------------
void TestDistributed::PchIsStoredCompressed() const
{
    CompressedPch pch;
    MakeCompressedPch( ( 3 * MEGABYTE ) + 5, 1, pch );
    TEST_ASSERT( pch.m_CompressedSize < ( pch.m_Content.GetSize() / 4 ) );

    Server worker( 1 );
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
    PchSender sender;
    TEST_ASSERT( sender.ConnectToWorker() );
    TEST_ASSERT( SendPch( sender, pch ) );
    AStackString<> storedFile;
    TEST_ASSERT( WaitUntil( [ & ]() { return worker.FindCachedPch( pch.m_Id, storedFile ); } ) );

    // What is kept is what was sent, in a file that says how it is stored
    TEST_ASSERT( storedFile.EndsWith( PchBlob::kStoredFileName ) );
    Array<char> onDisk;
    TEST_ASSERT( LoadFile( storedFile, onDisk ) );
    TEST_ASSERT( onDisk.GetSize() == pch.m_CompressedSize );
    TEST_ASSERT( memcmp( onDisk.Begin(), pch.m_Compressed.GetResult(), pch.m_CompressedSize ) == 0 );
    {
        MutexHolder mh( worker.m_PchCacheMutex );
        TEST_ASSERT( worker.m_PchCache.GetSize() == 1 );
        TEST_ASSERT( worker.m_PchCache[ 0 ].size == pch.m_CompressedSize );

        // A working copy is made for a job, and no job has asked: none is made or asked for
        TEST_ASSERT( worker.m_PchRaw.IsEmpty() );
        TEST_ASSERT( worker.m_PchUnpackQueue.IsEmpty() );
    }
    AStackString<> rawFile;
    TEST_ASSERT( Server::GetRawPchPath( storedFile, rawFile ) );
    TEST_ASSERT( FileIO::FileExists( rawFile.Get() ) == false );
    AStackString<> dir;
    GetWorkerPchDir( pch.m_Id, dir );
    TEST_ASSERT( CountFilesIn( dir ) == 1 );
    TEST_ASSERT( worker.m_NumPchsUnpacked.Load() == 0 );
    TEST_ASSERT( worker.m_NumPchUnpackAttempts.Load() == 0 );
}

// PchIsUnpackedWhenAJobNeedsIt
//------------------------------------------------------------------------------
void TestDistributed::PchIsUnpackedWhenAJobNeedsIt() const
{
    Server s( 1 );
    StoredPch pch;
    AddStoredPch( s, ( 3 * MEGABYTE ) + 5, 2, pch );
    TEST_ASSERT( FileIO::FileExists( pch.m_RawFile.Get() ) == false );

    // The job is not given the PCH until the copy is there. Asking again does not make another.
    Job * job = MakePchJob( s, pch.m_Id );
    TEST_ASSERT( s.TryResolvePch( *job ) == false );
    TEST_ASSERT( job->GetPchCachePath().IsEmpty() );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.TryResolvePch( *job ); } ) );

    // It is where the compile finds it, and is the PCH
    TEST_ASSERT( job->GetPchCachePath() == pch.m_RawFile );
    Array<char> raw;
    TEST_ASSERT( LoadFile( pch.m_RawFile, raw ) );
    TEST_ASSERT( raw.GetSize() == pch.m_Content.GetSize() );
    TEST_ASSERT( memcmp( raw.Begin(), pch.m_Content.Begin(), raw.GetSize() ) == 0 );
    TEST_ASSERT( s.m_NumPchsUnpacked.Load() == 1 );

    // The compressed PCH stays, and no temporary file
    TEST_ASSERT( FileIO::FileExists( pch.m_StoredFile.Get() ) );
    AStackString<> dir( pch.m_StoredFile.Get(), pch.m_StoredFile.FindLast( NATIVE_SLASH ) + 1 );
    TEST_ASSERT( CountFilesIn( dir ) == 2 );

    // The next job is given the copy that is there, and nothing is unpacked for it
    Job * another = MakePchJob( s, pch.m_Id );
    TEST_ASSERT( s.TryResolvePch( *another ) );
    TEST_ASSERT( another->GetPchCachePath() == pch.m_RawFile );
    TEST_ASSERT( s.m_NumPchsUnpacked.Load() == 1 );
    TEST_ASSERT( s.m_NumPchUnpackAttempts.Load() == 1 );

    FDELETE another;
    FDELETE job;
}

// PchThatCannotBeUnpackedIsDropped
//------------------------------------------------------------------------------
void TestDistributed::PchThatCannotBeUnpackedIsDropped() const
{
    CompressedPch pch;
    MakeCompressedPch( ( 2 * MEGABYTE ) + 3, 3, pch );
    CompressedPch other;
    MakeCompressedPch( ( 2 * MEGABYTE ) + 3, 4, other );
    Compressor lz4;
    lz4.Compress( pch.m_Content.Begin(), pch.m_Content.GetSize(), 1 );

    // What is stored under the id of the PCH, and indexed, that is not it
    enum Case { OTHER_PCH, DAMAGED, TRUNCATED, EMPTY, LZ4, MISSING, NUM_CASES };
    for ( uint32_t which = 0; which < NUM_CASES; ++which )
    {
        Server s( 1 );

        Array<char> blob;
        const void * source = pch.m_Compressed.GetResult();
        size_t sourceSize = pch.m_CompressedSize;
        if ( which == OTHER_PCH )
        {
            source = other.m_Compressed.GetResult();
            sourceSize = other.m_CompressedSize;
        }
        else if ( which == LZ4 )
        {
            source = lz4.GetResult();
            sourceSize = lz4.GetResultSize();
        }
        blob.SetSize( sourceSize );
        memcpy( blob.Begin(), source, sourceSize );
        if ( which == DAMAGED )
        {
            for ( size_t i = ( Compressor::kHeaderSize + 100 ); i < blob.GetSize(); i += ( blob.GetSize() / 9 ) )
            {
                blob[ i ] = (char)( blob[ i ] ^ 0x5A );
            }
        }
        else if ( which == TRUNCATED )
        {
            blob.SetSize( blob.GetSize() / 2 );
        }
        else if ( which == EMPTY )
        {
            blob.SetSize( 0 );
        }
        AStackString<> storedFile;
        AddStoredBlob( s, pch.m_Id, blob.Begin(), blob.GetSize(), ( which != MISSING ), storedFile );
        AStackString<> rawFile;
        TEST_ASSERT( Server::GetRawPchPath( storedFile, rawFile ) );
        AStackString<> dir( storedFile.Get(), storedFile.FindLast( NATIVE_SLASH ) + 1 );

        // The job waits, as it does for a PCH that is not there, and is not given a copy
        Job * job = MakePchJob( s, pch.m_Id );
        TEST_ASSERT( s.TryResolvePch( *job ) == false );
        TEST_ASSERT( WaitUntil( [ & ]() { return ( s.IsPchCached( pch.m_Id ) == false ); } ) );
        TEST_ASSERT( WaitUntil( [ & ]() { return ( s.m_PchUnpackQueue.IsEmpty() && ( IsPchBeingUnpacked( s, pch.m_Id ) == false ) ); } ) );
        TEST_ASSERT( s.TryResolvePch( *job ) == false );
        TEST_ASSERT( job->GetPchCachePath().IsEmpty() );
        TEST_ASSERT( s.m_NumPchsUnpacked.Load() == 0 );

        // A damaged PCH is not unpacked again, as it would be no better
        TEST_ASSERT( s.m_NumPchUnpackAttempts.Load() == 1 );

        // It is not kept, nor are pieces of it: the PCH can be sent again
        TEST_ASSERT( FileIO::FileExists( storedFile.Get() ) == false );
        TEST_ASSERT( FileIO::FileExists( rawFile.Get() ) == false );
        TEST_ASSERT( FileIO::DirectoryExists( dir ) == false );

        // The job still holds its PCH, until it is gone
        TEST_ASSERT( NumLeases( s, pch.m_Id ) == 1 );
        FDELETE job;
        TEST_ASSERT( HasRawEntry( s, pch.m_Id ) == false );
    }
}

// PchThatCannotBeWrittenIsKept
//------------------------------------------------------------------------------
void TestDistributed::PchThatCannotBeWrittenIsKept() const
{
    Server s( 1 );

    // A working copy nothing uses, which makes room when there is none
    StoredPch idle;
    AddStoredPch( s, 100000, 14, idle );
    {
        Job * job = MakePchJob( s, idle.m_Id );
        TEST_ASSERT( WaitUntil( [ & ]() { return s.TryResolvePch( *job ); } ) );
        FDELETE job;
    }
    TEST_ASSERT( FileIO::FileExists( idle.m_RawFile.Get() ) );
    TEST_ASSERT( s.m_NumPchUnpackAttempts.Load() == 1 );

    // The PCH of a job that is good, but its working copy cannot be written: a directory is where it goes
    StoredPch pch;
    AddStoredPch( s, ( 2 * MEGABYTE ) + 3, 15, pch );
    TEST_ASSERT( FileIO::EnsurePathExists( pch.m_RawFile ) );
    Job * job = MakePchJob( s, pch.m_Id );
    TEST_ASSERT( s.TryResolvePch( *job ) == false );
    TEST_ASSERT( WaitUntil( [ & ]() { return ( s.m_PchUnpackQueue.IsEmpty() && ( IsPchBeingUnpacked( s, pch.m_Id ) == false ) ); } ) );

    // It was tried again once the copies nothing uses were gone
    TEST_ASSERT( s.m_NumPchUnpackAttempts.Load() == 3 );
    TEST_ASSERT( FileIO::FileExists( idle.m_RawFile.Get() ) == false );
    TEST_ASSERT( HasRawEntry( s, idle.m_Id ) == false );
    TEST_ASSERT( s.m_NumPchsUnpacked.Load() == 1 );

    // The PCH is good, so it is kept, and nothing is left of the attempts. Sending it again would not help.
    TEST_ASSERT( s.IsPchCached( pch.m_Id ) );
    TEST_ASSERT( FileIO::FileExists( pch.m_StoredFile.Get() ) );
    AStackString<> dir( pch.m_StoredFile.Get(), pch.m_StoredFile.FindLast( NATIVE_SLASH ) + 1 );
    Array<AString> left;
    FileIO::GetFiles( dir, AStackString( "unpack.*" ), true, &left );
    TEST_ASSERT( left.IsEmpty() );
    TEST_ASSERT( job->GetPchCachePath().IsEmpty() );
    TEST_ASSERT( NumLeases( s, pch.m_Id ) == 1 );
    FDELETE job;
    TEST_ASSERT( HasRawEntry( s, pch.m_Id ) == false );

    // With room, the next job is given its copy
    TEST_ASSERT( FileIO::DirectoryDelete( pch.m_RawFile ) );
    Job * next = MakePchJob( s, pch.m_Id );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.TryResolvePch( *next ); } ) );
    TEST_ASSERT( FileIO::FileExists( pch.m_RawFile.Get() ) );
    FDELETE next;
}

// PchThatCannotBeWrittenReturnsItsJobs
//------------------------------------------------------------------------------
void TestDistributed::PchThatCannotBeWrittenReturnsItsJobs() const
{
    CapturedJob captured;
    CapturePchJob( captured );
    CompressedPch pch;
    MakeCompressedPch( 200000, 8, pch );
    const CapturedJob job = WithPch( captured, pch.m_Id );

    // The worker has the PCH, but cannot write a working copy of it: a directory is where it goes
    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );
    AStackString<> storedFile;
    AddStoredBlob( s, pch.m_Id, pch.m_Compressed.GetResult(), pch.m_CompressedSize, true, storedFile );
    AStackString<> rawFile;
    TEST_ASSERT( Server::GetRawPchPath( storedFile, rawFile ) );
    TEST_ASSERT( FileIO::EnsurePathExists( rawFile ) );

    // A job for it cannot be built here, so the client has it back: a PCH the client sends again would
    // be as good as the one the worker has, and no better
    HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    TEST_ASSERT( ConnectAndHandshake( probe, Protocol::kCapabilities, ci, kWaitTimeoutSeconds ) );
    JobStreamEncoder encoder;
    TEST_ASSERT( SendCapturedJob( ci, encoder, job ) );
    TEST_ASSERT( WaitUntil( [ &probe ]() { return probe.IsDisconnected(); } ) );
    TEST_ASSERT( probe.GetNumPchNeeded() == 0 );
    TEST_ASSERT( s.m_NumPchUnpackAttempts.Load() == 2 ); // Once more, with the unused copies gone
    TEST_ASSERT( s.IsPchCached( pch.m_Id ) );
    TEST_ASSERT( FileIO::FileExists( storedFile.Get() ) );

    // Its jobs went with it, and their hold on the PCH
    TEST_ASSERT( WaitUntil( [ & ]() { return ( CountWaitingJobs( s ) == 0 ) && ( TotalLeases( s ) == 0 ); } ) );
    TEST_ASSERT( HasRawEntry( s, pch.m_Id ) == false );
    TEST_ASSERT( FileIO::DirectoryDelete( rawFile ) );
}

// CorruptPchIsAskedForAgain
//------------------------------------------------------------------------------
void TestDistributed::CorruptPchIsAskedForAgain() const
{
    CapturedJob captured;
    CapturePchJob( captured );
    CompressedPch pch;
    MakeCompressedPch( 200000, 5, pch );
    const CapturedJob job = WithPch( captured, pch.m_Id );

    // The worker has a file that is not the PCH, as it indexed it
    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );
    Array<char> damaged;
    damaged.SetSize( pch.m_CompressedSize );
    memcpy( damaged.Begin(), pch.m_Compressed.GetResult(), pch.m_CompressedSize );
    for ( size_t i = ( Compressor::kHeaderSize + 50 ); i < damaged.GetSize(); i += ( damaged.GetSize() / 7 ) )
    {
        damaged[ i ] = (char)( damaged[ i ] ^ 0x33 );
    }
    AStackString<> storedFile;
    AddStoredBlob( s, pch.m_Id, damaged.Begin(), damaged.GetSize(), true, storedFile );

    // A job for it finds out, and the client is asked for the PCH
    HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    TEST_ASSERT( ConnectAndHandshake( probe, Protocol::kCapabilities, ci, kWaitTimeoutSeconds ) );
    JobStreamEncoder encoder;
    TEST_ASSERT( SendCapturedJob( ci, encoder, job ) );
    TEST_ASSERT( WaitUntil( [ &probe ]() { return ( probe.GetNumPchNeeded() >= 1 ); } ) );
    TEST_ASSERT( probe.GetPchNeededId( 0 ) == pch.m_Id );
    TEST_ASSERT( s.IsPchCached( pch.m_Id ) == false );
    TEST_ASSERT( FileIO::FileExists( storedFile.Get() ) == false );
    TEST_ASSERT( CountWaitingJobs( s ) == 1 );
    TEST_ASSERT( NumWaitingJobsWithAPch( s ) == 0 );
    TEST_ASSERT( IsPchUnpacked( s, pch.m_Id ) == false );

    // Sent again, it is unpacked for the job that is waiting
    PchSender sender;
    TEST_ASSERT( sender.ConnectToWorker() );
    TEST_ASSERT( SendPch( sender, pch ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return ( NumWaitingJobsWithAPch( s ) == 1 ); } ) );
    TEST_ASSERT( IsPchUnpacked( s, pch.m_Id ) );
    TEST_ASSERT( CountWaitingJobs( s ) == 1 ); // For its toolchain, which never comes
    TEST_ASSERT( s.IsPchCached( pch.m_Id ) );

    // and the client is not asked again: whatever the worker sent before it answers this was sent by then
    TEST_ASSERT( Handshake( probe, ci, Protocol::kCapabilities, kWaitTimeoutSeconds ) );
    TEST_ASSERT( probe.GetNumPchNeeded() == 1 );
    probe.ShutdownAllConnections();
}

// CorruptPchIsReturnedToAClientThatCannotAsk
//------------------------------------------------------------------------------
void TestDistributed::CorruptPchIsReturnedToAClientThatCannotAsk() const
{
    CapturedJob captured;
    CapturePchJob( captured );
    CompressedPch pch;
    MakeCompressedPch( 200000, 9, pch );
    const CapturedJob job = WithPch( captured, pch.m_Id );

    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );
    Array<char> damaged;
    damaged.SetSize( pch.m_CompressedSize );
    memcpy( damaged.Begin(), pch.m_Compressed.GetResult(), pch.m_CompressedSize );
    for ( size_t i = ( Compressor::kHeaderSize + 50 ); i < damaged.GetSize(); i += ( damaged.GetSize() / 7 ) )
    {
        damaged[ i ] = (char)( damaged[ i ] ^ 0x33 );
    }
    AStackString<> storedFile;
    AddStoredBlob( s, pch.m_Id, damaged.Begin(), damaged.GetSize(), true, storedFile );

    // A client of 24.0 cannot be asked for the PCH, so it would wait for ever: its jobs are given back
    HandshakeProbe probe;
    const ConnectionInfo * ci = nullptr;
    const uint32_t capabilities24_0 = ( Protocol::CAP_PCH_INVENTORY | Protocol::CAP_TOOLCHAIN_INVENTORY );
    TEST_ASSERT( ConnectAndHandshake( probe, capabilities24_0, ci, kWaitTimeoutSeconds ) );
    JobStreamEncoder encoder;
    TEST_ASSERT( SendCapturedJob( ci, encoder, job ) );
    TEST_ASSERT( WaitUntil( [ &probe ]() { return probe.IsDisconnected(); } ) );
    TEST_ASSERT( probe.GetNumPchNeeded() == 0 );

    // The PCH that is not good is gone, and the jobs and their hold on it with the connection
    TEST_ASSERT( s.m_NumPchUnpackAttempts.Load() == 1 );
    TEST_ASSERT( s.IsPchCached( pch.m_Id ) == false );
    TEST_ASSERT( FileIO::FileExists( storedFile.Get() ) == false );
    TEST_ASSERT( WaitUntil( [ & ]() { return ( CountWaitingJobs( s ) == 0 ) && ( TotalLeases( s ) == 0 ); } ) );
}

// PchUnpackIsSingleFlight
//------------------------------------------------------------------------------
void TestDistributed::PchUnpackIsSingleFlight() const
{
    Server s( 1 );
    StoredPch pch;
    AddStoredPch( s, ( 6 * MEGABYTE ), 6, pch );

    // Jobs on several threads need the PCH at the same time
    const uint32_t kNumThreads = 4;
    Atomic<bool> go{ false };
    ResolveRequest requests[ kNumThreads ];
    Thread threads[ kNumThreads ];
    for ( uint32_t i = 0; i < kNumThreads; ++i )
    {
        requests[ i ].m_Server = &s;
        requests[ i ].m_Go = &go;
        for ( Job *& job : requests[ i ].m_Jobs )
        {
            job = MakePchJob( s, pch.m_Id );
        }
        threads[ i ].Start( ResolveThread, "ResolveJobs", &requests[ i ] );
    }
    TEST_ASSERT( NumLeases( s, pch.m_Id ) == ( 2 * kNumThreads ) );
    go.Store( true );
    for ( Thread & thread : threads )
    {
        thread.Join();
    }

    // One copy was made, for all of them
    for ( const ResolveRequest & request : requests )
    {
        TEST_ASSERT( request.m_Resolved );
        for ( const Job * job : request.m_Jobs )
        {
            TEST_ASSERT( job->GetPchCachePath() == pch.m_RawFile );
        }
    }
    TEST_ASSERT( s.m_NumPchsUnpacked.Load() == 1 );
    AStackString<> dir( pch.m_StoredFile.Get(), pch.m_StoredFile.FindLast( NATIVE_SLASH ) + 1 );
    TEST_ASSERT( CountFilesIn( dir ) == 2 );

    for ( ResolveRequest & request : requests )
    {
        for ( Job * job : request.m_Jobs )
        {
            FDELETE job;
        }
    }
    TEST_ASSERT( NumLeases( s, pch.m_Id ) == 0 );
}

// UnpackSkipsAPchNoJobNeeds
//------------------------------------------------------------------------------
void TestDistributed::UnpackSkipsAPchNoJobNeeds() const
{
    Server s( 1 );
    StoredPch pch;
    AddStoredPch( s, ( 1 * MEGABYTE ), 19, pch );

    // Queued to be unpacked for a job that was gone by the time its turn came
    {
        MutexHolder mh( s.m_PchCacheMutex );
        Server::PchRawEntry raw;
        raw.pchId = pch.m_Id;
        raw.state = Server::PchRawEntry::UNPACKING;
        s.m_PchRaw.Append( raw );
    }
    s.UnpackPch( pch.m_Id );
    TEST_ASSERT( s.m_NumPchUnpackAttempts.Load() == 0 );
    TEST_ASSERT( HasRawEntry( s, pch.m_Id ) == false );
    TEST_ASSERT( FileIO::FileExists( pch.m_RawFile.Get() ) == false );
    TEST_ASSERT( s.IsPchCached( pch.m_Id ) );
}

// UnpackedCopyLivesWithItsJobs
//------------------------------------------------------------------------------
void TestDistributed::UnpackedCopyLivesWithItsJobs() const
{
    Server s( 1 );
    SetPchCacheLimits( s, ( 1ull << 30 ), 0, 0 ); // Nothing is kept for later: a copy is only there while jobs use it
    StoredPch pch;
    AddStoredPch( s, ( 2 * MEGABYTE ), 7, pch );
    auto resolve = [ & ]( Job & job ) { TEST_ASSERT( WaitUntil( [ & ]() { return s.TryResolvePch( job ); } ) ); };

    Job * a = MakePchJob( s, pch.m_Id );
    Job * b = MakePchJob( s, pch.m_Id );
    resolve( *a );
    resolve( *b );
    TEST_ASSERT( NumLeases( s, pch.m_Id ) == 2 );

    // It stays while any job needs it
    s.TrimRawPchs( Time::GetCurrentFileTime() );
    TEST_ASSERT( FileIO::FileExists( pch.m_RawFile.Get() ) );
    FDELETE a;
    TEST_ASSERT( NumLeases( s, pch.m_Id ) == 1 );
    s.TrimRawPchs( Time::GetCurrentFileTime() );
    TEST_ASSERT( FileIO::FileExists( pch.m_RawFile.Get() ) );

    // And goes when the last does, as nothing is kept for later
    FDELETE b;
    TEST_ASSERT( NumLeases( s, pch.m_Id ) == 0 );
    s.TrimRawPchs( Time::GetCurrentFileTime() );
    TEST_ASSERT( FileIO::FileExists( pch.m_RawFile.Get() ) == false );
    TEST_ASSERT( HasRawEntry( s, pch.m_Id ) == false );
    TEST_ASSERT( FileIO::FileExists( pch.m_StoredFile.Get() ) ); // The PCH is still cached
    TEST_ASSERT( s.IsPchCached( pch.m_Id ) );

    // The next job has it made again
    Job * c = MakePchJob( s, pch.m_Id );
    resolve( *c );
    TEST_ASSERT( s.m_NumPchsUnpacked.Load() == 2 );

    // With room for it, the copy stays for the job after, which does not unpack it again
    SetPchCacheLimits( s, ( 1ull << 30 ), 0, ( 1ull << 30 ) );
    FDELETE c;
    s.TrimRawPchs( Time::GetCurrentFileTime() );
    TEST_ASSERT( FileIO::FileExists( pch.m_RawFile.Get() ) );
    Job * d = MakePchJob( s, pch.m_Id );
    TEST_ASSERT( s.TryResolvePch( *d ) ); // At once
    TEST_ASSERT( s.m_NumPchsUnpacked.Load() == 2 );
    FDELETE d;
}

// FileTimeOfSeconds
//------------------------------------------------------------------------------
namespace
{
    // File time ticks in some seconds
    uint64_t FileTimeOfSeconds( uint32_t seconds )
    {
        uint64_t ticksPerSecond = 1;
        while ( Time::FileTimeToSeconds( ticksPerSecond ) == 0 )
        {
            ticksPerSecond *= 10;
        }
        return ( ticksPerSecond * seconds );
    }
}

// UnusedCopiesAreKeptToABudget
//------------------------------------------------------------------------------
void TestDistributed::UnusedCopiesAreKeptToABudget() const
{
    auto makeEntry = []( uint64_t id, Server::PchRawEntry::State state, uint32_t numLeases, uint32_t lastUsedSeconds )
    {
        Server::PchRawEntry e;
        e.pchId = id;
        e.state = state;
        e.numLeases = numLeases;
        e.size = 100;
        e.lastUsed = FileTimeOfSeconds( lastUsedSeconds );
        return e;
    };
    Array<Server::PchRawEntry> entries;
    entries.Append( makeEntry( 1, Server::PchRawEntry::READY, 0, 300 ) );
    entries.Append( makeEntry( 2, Server::PchRawEntry::READY, 0, 100 ) );
    entries.Append( makeEntry( 3, Server::PchRawEntry::READY, 0, 200 ) );
    entries.Append( makeEntry( 4, Server::PchRawEntry::READY, 1, 10 ) );        // In use, and the oldest
    entries.Append( makeEntry( 5, Server::PchRawEntry::UNPACKING, 0, 10 ) );    // Being made
    entries.Append( makeEntry( 6, Server::PchRawEntry::ABSENT, 2, 10 ) );       // Not there
    Array<size_t> trims;
    auto trimmedId = [ & ]( size_t n ) { return entries[ trims[ n ] ].pchId; };

    // Room for all three that nothing uses: nothing goes
    Server::SelectRawPchTrims( entries, 300, 0, 0, trims );
    TEST_ASSERT( trims.IsEmpty() );

    // Room for two: the one used longest ago goes
    Server::SelectRawPchTrims( entries, 200, 0, 0, trims );
    TEST_ASSERT( trims.GetSize() == 1 );
    TEST_ASSERT( trimmedId( 0 ) == 2 );

    // Room for one: the two used longest ago go, the older first
    Server::SelectRawPchTrims( entries, 100, 0, 0, trims );
    TEST_ASSERT( trims.GetSize() == 2 );
    TEST_ASSERT( trimmedId( 0 ) == 2 );
    TEST_ASSERT( trimmedId( 1 ) == 3 );

    // Room for none: all that nothing uses go, and what is used or not a copy never does
    Server::SelectRawPchTrims( entries, 0, 0, 0, trims );
    TEST_ASSERT( trims.GetSize() == 3 );
    for ( size_t n = 0; n < trims.GetSize(); ++n )
    {
        TEST_ASSERT( ( trimmedId( n ) == 1 ) || ( trimmedId( n ) == 2 ) || ( trimmedId( n ) == 3 ) );
    }

    // Those not used for more than a time go, however much room there is
    Server::SelectRawPchTrims( entries, 1000, FileTimeOfSeconds( 330 ), 60, trims );
    TEST_ASSERT( trims.GetSize() == 2 ); // Used 230 and 130 seconds ago, and not 30
    TEST_ASSERT( trimmedId( 0 ) == 2 );
    TEST_ASSERT( trimmedId( 1 ) == 3 );
    Server::SelectRawPchTrims( entries, 1000, FileTimeOfSeconds( 160 ), 60, trims );
    TEST_ASSERT( trims.IsEmpty() ); // Used 60 seconds ago is not more than 60
}

// UnusedCopiesGoWhenIdle
//------------------------------------------------------------------------------
void TestDistributed::UnusedCopiesGoWhenIdle() const
{
    Server s( 1 );
    SetPchCacheLimits( s, ( 1ull << 30 ), 0, ( 1ull << 30 ) ); // Room for any number
    StoredPch pch;
    AddStoredPch( s, ( 1 * MEGABYTE ), 8, pch );
    Job * job = MakePchJob( s, pch.m_Id );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.TryResolvePch( *job ); } ) );
    FDELETE job;

    // Kept a while for the next job
    const uint64_t now = Time::GetCurrentFileTime();
    s.TrimRawPchs( now + FileTimeOfSeconds( Server::kRawPchIdleSeconds - 10 ) );
    TEST_ASSERT( FileIO::FileExists( pch.m_RawFile.Get() ) );

    // And removed when it is not used for long: the disk is not held for builds that are over
    s.TrimRawPchs( now + FileTimeOfSeconds( Server::kRawPchIdleSeconds + 10 ) );
    TEST_ASSERT( FileIO::FileExists( pch.m_RawFile.Get() ) == false );
    TEST_ASSERT( HasRawEntry( s, pch.m_Id ) == false );
    TEST_ASSERT( s.IsPchCached( pch.m_Id ) );
}

// LeasedPchIsNotEvicted
//------------------------------------------------------------------------------
void TestDistributed::LeasedPchIsNotEvicted() const
{
    CompressedPch pchs[ 3 ];
    for ( uint32_t i = 0; i < 3; ++i )
    {
        MakeCompressedPch( 100000, ( 51 + i ), pchs[ i ] );
    }

    Server s( 1 );
    SetPchCacheLimits( s, 1, 0 ); // Room for the PCH that has just arrived only
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );
    PchSender sender;
    TEST_ASSERT( sender.ConnectToWorker() );

    // A job needs the first, which is the least recently used. A second arrives, and there is room for one:
    // the first is the only one that could go, and stays, as a job needs it.
    TEST_ASSERT( SendPch( sender, pchs[ 0 ] ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.IsPchCached( pchs[ 0 ].m_Id ); } ) );
    SetPchRecency( s, pchs[ 0 ].m_Id, 1 );
    Job * job = MakePchJob( s, pchs[ 0 ].m_Id );
    TEST_ASSERT( SendPch( sender, pchs[ 1 ] ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.IsPchCached( pchs[ 1 ].m_Id ); } ) );
    TEST_ASSERT( s.IsPchCached( pchs[ 0 ].m_Id ) );
    SetPchRecency( s, pchs[ 1 ].m_Id, 2 );

    // The job is gone. A third arrives, and the other two go, the first being the oldest.
    FDELETE job;
    TEST_ASSERT( SendPch( sender, pchs[ 2 ] ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.IsPchCached( pchs[ 2 ].m_Id ); } ) );
    TEST_ASSERT( s.IsPchCached( pchs[ 0 ].m_Id ) == false );
    TEST_ASSERT( s.IsPchCached( pchs[ 1 ].m_Id ) == false );
}

// PchsAreEvictedByRecency
//------------------------------------------------------------------------------
void TestDistributed::PchsAreEvictedByRecency() const
{
    CompressedPch pchs[ 3 ];
    for ( uint32_t i = 0; i < 3; ++i )
    {
        MakeCompressedPch( 100000, ( 61 + i ), pchs[ i ] );
    }
    // Room for the first and either of the others, counted as they are stored, but not all three
    const uint64_t budget = Math::Max( pchs[ 0 ].m_CompressedSize + pchs[ 1 ].m_CompressedSize,
                                       pchs[ 0 ].m_CompressedSize + pchs[ 2 ].m_CompressedSize );
    TEST_ASSERT( budget < ( pchs[ 0 ].m_CompressedSize + pchs[ 1 ].m_CompressedSize + pchs[ 2 ].m_CompressedSize ) );
    Server s( 1 );
    SetPchCacheLimits( s, budget, 0 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );
    PchSender sender;
    TEST_ASSERT( sender.ConnectToWorker() );
    TEST_ASSERT( SendPch( sender, pchs[ 0 ] ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.IsPchCached( pchs[ 0 ].m_Id ); } ) );
    SetPchRecency( s, pchs[ 0 ].m_Id, 1 );
    TEST_ASSERT( SendPch( sender, pchs[ 1 ] ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.IsPchCached( pchs[ 1 ].m_Id ); } ) );
    TEST_ASSERT( s.IsPchCached( pchs[ 0 ].m_Id ) ); // Both fit
    SetPchRecency( s, pchs[ 1 ].m_Id, 2 );

    // The first is used again, so the second is the one that is not
    AStackString<> path;
    TEST_ASSERT( s.FindCachedPch( pchs[ 0 ].m_Id, path ) );
    TEST_ASSERT( SendPch( sender, pchs[ 2 ] ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.IsPchCached( pchs[ 2 ].m_Id ); } ) );
    TEST_ASSERT( s.IsPchCached( pchs[ 0 ].m_Id ) );
    TEST_ASSERT( s.IsPchCached( pchs[ 1 ].m_Id ) == false );
}

// UnusedCopyGoesWhenTheFreeSpaceIsUnknown
//------------------------------------------------------------------------------
void TestDistributed::UnusedCopyGoesWhenTheFreeSpaceIsUnknown() const
{
    Server s( 1 );
    SetPchCacheLimitsUnknown( s ); // Nothing is kept for later, to be safe
    StoredPch pch;
    AddStoredPch( s, ( 1 * MEGABYTE ), 16, pch );
    Job * job = MakePchJob( s, pch.m_Id );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.TryResolvePch( *job ); } ) );

    // It stays for as long as the job needs it
    s.TrimRawPchs( Time::GetCurrentFileTime() );
    TEST_ASSERT( FileIO::FileExists( pch.m_RawFile.Get() ) );

    // and goes with the last job
    FDELETE job;
    s.TrimRawPchs( Time::GetCurrentFileTime() );
    TEST_ASSERT( FileIO::FileExists( pch.m_RawFile.Get() ) == false );
    TEST_ASSERT( HasRawEntry( s, pch.m_Id ) == false );
    TEST_ASSERT( s.IsPchCached( pch.m_Id ) );
}

// PchStartupScanKeepsTheCompressedStore
//------------------------------------------------------------------------------
void TestDistributed::PchStartupScanKeepsTheCompressedStore() const
{
    AStackString<> root;
    VERIFY( FBuild::GetWorkerCacheDir( root ) );
    auto makeDirNamed = [ & ]( const char * name, AString & outDir )
    {
        outDir.Format( "%s%s%c", root.Get(), name, (char)NATIVE_SLASH );
        Array<AString> old;
        FileIO::GetFiles( outDir, AStackString( "*" ), true, &old );
        for ( const AString & file : old )
        {
            FileIO::FileDelete( file.Get() );
        }
    };
    auto makeDir = [ & ]( uint64_t pchId, AString & outDir )
    {
        AStackString<> name;
        name.Format( "pch.%016" PRIx64, pchId );
        makeDirNamed( name.Get(), outDir );
    };
    auto makeFile = [ & ]( const AString & dir, const char * name, const void * data, size_t size, AString & outFile )
    {
        outFile = dir;
        outFile += name;
        FileStream f;
        TEST_ASSERT( FileIO::EnsurePathExistsForFile( outFile ) );
        TEST_ASSERT( f.Open( outFile.Get(), FileStream::WRITE_ONLY ) );
        TEST_ASSERT( f.Write( data, size ) == size );
    };

    // A PCH with a working copy that was left, one with only the compressed file, one that a version
    // that kept PCHs unpacked left, and one with what a process that crashed (and one that runs) was unpacking
    AStackString<> keptDir, aloneDir, oldDir, tempDir;
    makeDir( 0xB001, keptDir );
    makeDir( 0xB002, aloneDir );
    makeDir( 0xB003, oldDir );
    makeDir( 0xB004, tempDir );
    AStackString<> keptStored, keptRaw, aloneStored, oldRaw, tempStored, stale, running, own;
    TEST_ASSERT( MakeSmallFile( keptDir, PchBlob::kStoredFileName, keptStored ) );
    TEST_ASSERT( MakeSmallFile( keptDir, PchBlob::kRawFileName, keptRaw ) );
    TEST_ASSERT( MakeSmallFile( aloneDir, PchBlob::kStoredFileName, aloneStored ) );
    TEST_ASSERT( MakeSmallFile( oldDir, PchBlob::kRawFileName, oldRaw ) );
    TEST_ASSERT( MakeSmallFile( tempDir, PchBlob::kStoredFileName, tempStored ) );
    AStackString<> name;
    name.Format( "unpack.%u.1.pch", GetIdOfNoProcess() );
    TEST_ASSERT( MakeSmallFile( tempDir, name.Get(), stale ) );
    name.Format( "unpack.%u.2.pch", GetIdOfAnotherProcess() );
    TEST_ASSERT( MakeSmallFile( tempDir, name.Get(), running ) );
    name.Format( "unpack.%u.3.pch", Process::GetCurrentId() );
    TEST_ASSERT( MakeSmallFile( tempDir, name.Get(), own ) );

    // Compressed files that are not a PCH are indexed as the rest are: what a file holds is only found
    // out when a job needs it
    AStackString<> emptyDir, lz4Dir, emptyStored, lz4Stored;
    makeDir( 0xB005, emptyDir );
    makeDir( 0xB006, lz4Dir );
    makeFile( emptyDir, PchBlob::kStoredFileName, nullptr, 0, emptyStored );
    Array<char> content;
    MakeTestContent( 5000, 1, content );
    Compressor lz4;
    lz4.Compress( content.Begin(), content.GetSize(), 1 );
    makeFile( lz4Dir, PchBlob::kStoredFileName, lz4.GetResult(), lz4.GetResultSize(), lz4Stored );

    // Files of those names that are not in the directory of a PCH are not the cache's
    AStackString<> toolchainDir, namedDir, nestedParentDir, toolchainRaw, namedRaw, namedStored, nestedRaw;
    makeDirNamed( "toolchain.00000000000000b7", toolchainDir );
    makeDirNamed( "pch.notanid", namedDir );
    makeDirNamed( "pch.00000000000000b8", nestedParentDir );
    AStackString<> nestedDir( nestedParentDir );
    nestedDir += "sub";
    nestedDir += NATIVE_SLASH;
    makeFile( toolchainDir, PchBlob::kRawFileName, "data", 5, toolchainRaw );
    makeFile( namedDir, PchBlob::kRawFileName, "data", 5, namedRaw );
    makeFile( namedDir, PchBlob::kStoredFileName, "data", 5, namedStored );
    makeFile( nestedDir, PchBlob::kRawFileName, "data", 5, nestedRaw );

    Server s( 1 ); // The scan is done by the time it is constructed

    // The compressed PCHs are in the cache, as their files say. Nothing else is.
    TEST_ASSERT( s.IsPchCached( 0xB001 ) );
    TEST_ASSERT( s.IsPchCached( 0xB002 ) );
    TEST_ASSERT( s.IsPchCached( 0xB003 ) == false );
    TEST_ASSERT( s.IsPchCached( 0xB004 ) );
    TEST_ASSERT( s.IsPchCached( 0xB005 ) );
    TEST_ASSERT( s.IsPchCached( 0xB006 ) );
    TEST_ASSERT( s.IsPchCached( 0xB7 ) == false );
    TEST_ASSERT( s.IsPchCached( 0xB8 ) == false );
    {
        MutexHolder mh( s.m_PchCacheMutex );
        uint32_t numMatches = 0;
        for ( const Server::PchCacheEntry & e : s.m_PchCache )
        {
            if ( ( e.pchId >= 0xB001 ) && ( e.pchId <= 0xB008 ) )
            {
                ++numMatches;
                TEST_ASSERT( e.filePath.EndsWith( PchBlob::kStoredFileName ) );
                const uint64_t expectedSize = ( e.pchId == 0xB005 ) ? 0 : ( e.pchId == 0xB006 ) ? lz4.GetResultSize() : 5;
                TEST_ASSERT( e.size == expectedSize );
            }
        }
        TEST_ASSERT( numMatches == 5 ); // Not the one with only a working copy
        TEST_ASSERT( s.m_PchRaw.IsEmpty() );
    }

    // The working copies are gone, as is the directory of a PCH that was only a copy
    TEST_ASSERT( FileIO::FileExists( keptStored.Get() ) );
    TEST_ASSERT( FileIO::FileExists( keptRaw.Get() ) == false );
    TEST_ASSERT( FileIO::FileExists( aloneStored.Get() ) );
    TEST_ASSERT( FileIO::FileExists( oldRaw.Get() ) == false );
    TEST_ASSERT( FileIO::DirectoryExists( oldDir ) == false );

    // As temporary files are: what a process that is gone left is removed
    TEST_ASSERT( FileIO::FileExists( tempStored.Get() ) );
    TEST_ASSERT( FileIO::FileExists( stale.Get() ) == false );
    TEST_ASSERT( FileIO::FileExists( running.Get() ) );
    TEST_ASSERT( FileIO::FileExists( own.Get() ) );

    // What is not in a directory of a PCH is left alone
    TEST_ASSERT( FileIO::FileExists( toolchainRaw.Get() ) );
    TEST_ASSERT( FileIO::FileExists( namedRaw.Get() ) );
    TEST_ASSERT( FileIO::FileExists( namedStored.Get() ) );
    TEST_ASSERT( FileIO::FileExists( nestedRaw.Get() ) );
    FileIO::FileDelete( toolchainRaw.Get() );
    FileIO::FileDelete( namedRaw.Get() );
    FileIO::FileDelete( namedStored.Get() );
    FileIO::FileDelete( nestedRaw.Get() );
    auto removeDir = []( const AString & dir )
    {
        VERIFY( FileIO::DirectoryDelete( AStackString<>( dir.Get(), dir.Get() + dir.GetLength() - 1 ) ) ); // Without the slash
    };
    removeDir( toolchainDir );
    removeDir( namedDir );
    removeDir( nestedDir );
    removeDir( nestedParentDir );
}

// PchOfAnotherCompressionIsNotStoredAgain
//------------------------------------------------------------------------------
void TestDistributed::PchOfAnotherCompressionIsNotStoredAgain() const
{
    CompressedPch pch;
    MakeCompressedPch( ( 2 * MEGABYTE ) + 7, 17, pch );

    // The same PCH, compressed harder: not the same file, as another coordinator may send it
    Compressor harder;
    harder.CompressZstd( pch.m_Content.Begin(), pch.m_Content.GetSize(), 19 );
    const uint32_t harderSize = (uint32_t)harder.GetResultSize();
    TEST_ASSERT( harderSize != pch.m_CompressedSize );

    // A PCH sent after it on the same connection, which only arrives if the connection is still there
    CompressedPch after;
    MakeCompressedPch( 100000, 18, after );

    Server s( 1 );
    TEST_ASSERT( s.Listen( Protocol::kTestPort ) );
    PchSender first;
    TEST_ASSERT( first.ConnectToWorker() );
    TEST_ASSERT( SendPch( first, pch ) );
    AStackString<> storedFile;
    TEST_ASSERT( WaitUntil( [ & ]() { return s.FindCachedPch( pch.m_Id, storedFile ); } ) );

    // A job is using it
    Job * job = MakePchJob( s, pch.m_Id );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.TryResolvePch( *job ); } ) );

    PchSender second;
    TEST_ASSERT( second.ConnectToWorker() );
    TEST_ASSERT( second.SendChunks( pch.m_Id, (uint32_t)pch.m_Content.GetSize(), harder.GetResult(), harderSize, Protocol::kPchChunkSize, 0, Protocol::MsgPchFile::GetNumChunks( harderSize, Protocol::kPchChunkSize ) ) );
    TEST_ASSERT( SendPch( second, after ) );
    TEST_ASSERT( WaitUntil( [ & ]() { return s.IsPchCached( after.m_Id ); } ) );

    // It is the PCH it was: not replaced, and not counted as the size of the other
    Array<char> onDisk;
    TEST_ASSERT( LoadFile( storedFile, onDisk ) );
    TEST_ASSERT( onDisk.GetSize() == pch.m_CompressedSize );
    TEST_ASSERT( memcmp( onDisk.Begin(), pch.m_Compressed.GetResult(), pch.m_CompressedSize ) == 0 );
    {
        MutexHolder mh( s.m_PchCacheMutex );
        TEST_ASSERT( s.m_PchCache.GetSize() == 2 );
        const Server::PchCacheEntry * stored = s.FindStoredPch( pch.m_Id );
        TEST_ASSERT( stored && ( stored->size == pch.m_CompressedSize ) );
    }

    // and what was sent is not left behind
    AStackString<> dir;
    GetWorkerPchDir( pch.m_Id, dir );
    TEST_ASSERT( CountFilesIn( dir ) == 2 ); // The compressed PCH and its working copy
    FDELETE job;
}

// PchStoreIsUsedThroughTheBuild
//------------------------------------------------------------------------------
void TestDistributed::PchStoreIsUsedThroughTheBuild() const
{
    const char * target( "../tmp/Test/Distributed/distpch.lib" );
    const AStackString<> dbFile( "../tmp/Test/PchStore/PchStore.db" );
    FileIO::FileDelete( dbFile.Get() );
    TEST_ASSERT( FileIO::EnsurePathExistsForFile( dbFile ) );
    const char * objects[] = { "a_pch", "b_pch", "c_pch" };

    auto cleanOutput = []()
    {
        Array<AString> files;
        FileIO::GetFiles( AStackString( "../tmp/Test/Distributed" ), AStackString( "*.*" ), true, &files );
        for ( const AString & file : files )
        {
            FileIO::FileDelete( file.Get() );
        }
    };
    // An object file is the same but for the time it was made, which is in its header
    auto IsSameObject = []( const Array<char> & a, const Array<char> & b )
    {
        if ( a.GetSize() != b.GetSize() )
        {
            return false;
        }
        for ( size_t i = 0; i < a.GetSize(); ++i )
        {
            const bool isTimeStamp = ( ( i >= 4 ) && ( i < 8 ) );
            if ( ( a[ i ] != b[ i ] ) && ( isTimeStamp == false ) )
            {
                return false;
            }
        }
        return true;
    };
    auto readObject = [ & ]( const char * name, Array<char> & outBytes )
    {
        AStackString<> path;
        path.Format( "../tmp/Test/Distributed/%s.obj", name );
        TEST_ASSERT( LoadFile( path, outBytes ) );
    };

    // The jobs are built on a worker that stores the PCH compressed
    Array<char> first[ 3 ];
    Array<char> client_pch;
    AStackString<> storedFile;
    uint64_t pchId = 0;
    {
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true; // The jobs need the PCH on the worker
        options.m_AllowLocalRace = false; // So that every object is built on the worker
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );
        cleanOutput();

        Server worker( 1 );
        SetPchCacheLimits( worker, ( 1ull << 30 ), 0, ( 1ull << 30 ) ); // Copies that nothing uses are kept: the check below is of one
        TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
        TEST_ASSERT( fBuild.Build( target ) );
        TEST_ASSERT( fBuild.SaveDependencyGraph( dbFile.Get() ) );

        TEST_ASSERT( worker.m_NumPchChunksReceived.Load() >= 1 );
        TEST_ASSERT( worker.m_NumPchsUnpacked.Load() >= 1 );
        for ( uint32_t i = 0; i < 3; ++i )
        {
            readObject( objects[ i ], first[ i ] );
            TEST_ASSERT( first[ i ].GetSize() > 0 );
        }

        // The worker holds the compressed PCH, and it is the PCH of the client
        {
            MutexHolder mh( worker.m_PchCacheMutex );
            TEST_ASSERT( worker.m_PchCache.GetSize() >= 1 );
            storedFile = worker.m_PchCache[ worker.m_PchCache.GetSize() - 1 ].filePath;
            pchId = worker.m_PchCache[ worker.m_PchCache.GetSize() - 1 ].pchId;
        }
        TEST_ASSERT( storedFile.EndsWith( PchBlob::kStoredFileName ) );
        TEST_ASSERT( LoadFile( AStackString<>( "../tmp/Test/Distributed/PrecompiledHeader.pch" ), client_pch ) );
        TEST_ASSERT( PchId::Calc( client_pch.Begin(), client_pch.GetSize() ) == pchId );
        Array<char> stored;
        TEST_ASSERT( LoadFile( storedFile, stored ) );
        TEST_ASSERT( BlobDecodesTo( stored, client_pch ) ); // The client may have sent it transformed

        // What the compiler was given is the PCH of the client, byte for byte
        AStackString<> rawFile;
        TEST_ASSERT( Server::GetRawPchPath( storedFile, rawFile ) );
        Array<char> raw;
        TEST_ASSERT( LoadFile( rawFile, raw ) );
        TEST_ASSERT( raw.GetSize() == client_pch.GetSize() );
        TEST_ASSERT( memcmp( raw.Begin(), client_pch.Begin(), raw.GetSize() ) == 0 );
    }

    // A worker that starts later finds the store, and is not sent the PCH again. Only the objects are built again.
    {
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true;
        options.m_AllowLocalRace = false;
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize( dbFile.Get() ) );
        for ( uint32_t i = 0; i < 3; ++i )
        {
            AStackString<> path;
            path.Format( "../tmp/Test/Distributed/%s.obj", objects[ i ] );
            TEST_ASSERT( FileIO::FileDelete( path.Get() ) );
        }
        FileIO::FileDelete( target );

        Server worker( 1 );
        SetPchCacheLimits( worker, ( 1ull << 30 ), 0, ( 1ull << 30 ) );
        TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
        TEST_ASSERT( worker.IsPchCached( pchId ) ); // The scan of the store is done by the time the worker is constructed
        TEST_ASSERT( fBuild.Build( target ) );
        TEST_ASSERT( worker.m_NumPchChunksReceived.Load() == 0 );
        TEST_ASSERT( worker.m_NumPchsUnpacked.Load() >= 1 ); // Made from the store, for the jobs
        for ( uint32_t i = 0; i < 3; ++i )
        {
            Array<char> built;
            readObject( objects[ i ], built );
            TEST_ASSERT( IsSameObject( built, first[ i ] ) );
        }
    }
}

// PchLeasesAreReleasedByEveryEnd
//------------------------------------------------------------------------------
void TestDistributed::PchLeasesAreReleasedByEveryEnd() const
{
    const char * target( "../tmp/Test/Distributed/distpch.lib" );
    auto cleanOutput = []()
    {
        Array<AString> files;
        FileIO::GetFiles( AStackString( "../tmp/Test/Distributed" ), AStackString( "*.*" ), true, &files );
        for ( const AString & file : files )
        {
            FileIO::FileDelete( file.Get() );
        }
    };
    auto noLeasesLeft = []( Server & server ) { return ( TotalLeases( server ) == 0 ); };

    Server worker( 1 );
    SetPchCacheLimits( worker, ( 1ull << 30 ), 0, ( 1ull << 30 ) );
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );

    // A build that runs to its end
    {
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true; // The jobs need the PCH on the worker
        options.m_AllowLocalRace = false;
        options.m_ForceCleanBuild = true;
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );
        cleanOutput();
        TEST_ASSERT( fBuild.Build( target ) );
    }
    TEST_ASSERT( worker.m_NumPchUnpackAttempts.Load() >= 1 ); // The jobs did hold a PCH
    TEST_ASSERT( WaitUntil( [ & ]() { return noLeasesLeft( worker ); } ) );

    // A build whose connection is dropped under a job, which the coordinator builds again on a new one
    {
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true;
        options.m_AllowLocalRace = false;
        options.m_ForceCleanBuild = true;
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );
        cleanOutput();

        ConnectionDrop drop;
        drop.m_Server = &worker;
        Thread dropper;
        dropper.Start( DropConnectionOnFirstJob, "DropConnection", &drop );
        const bool built = fBuild.Build( target );
        drop.m_Stop.Store( true );
        dropper.Join();
        TEST_ASSERT( drop.m_Dropped.Load() );
        TEST_ASSERT( built );
    }
    TEST_ASSERT( WaitUntil( [ & ]() { return noLeasesLeft( worker ); } ) );

    // A build that is stopped while a job is on the worker
    {
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true;
        options.m_AllowLocalRace = false;
        options.m_ForceCleanBuild = true;
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );
        cleanOutput();

        auto reached = [ &worker ]() { return WorkerIsBuildingAJob( worker ); };
        BuildAborter aborter( reached, kWaitTimeoutSeconds );
        fBuild.Build( target );
        TEST_ASSERT( aborter.HasReached() );
    }
    TEST_ASSERT( WaitUntil( [ & ]() { return noLeasesLeft( worker ); } ) );
}

// RegressionTest_RemoteCrashOnErrorFormatting
//------------------------------------------------------------------------------
void TestDistributed::RegressionTest_RemoteCrashOnErrorFormatting()
{
    const char * target( "badcode" );
    TestHelper( target, 1, true ); // compilation should fail
}

//------------------------------------------------------------------------------
void TestDistributed::TestLocalRace()
{
    {
        const char * target( "../tmp/Test/Distributed/dist.lib" );
        TestHelper( target, 1, false, true ); // allow race
    }
    {
        const char * target( "../tmp/Test/Distributed/dist.lib" );
        TestHelper( target, 4, false, true ); // allow race
    }
    {
        const char * target( "../tmp/Test/Distributed/distpch.lib" );
        TestHelper( target, 4, false, true ); // allow race
    }
    {
        const char * target( "badcode" );
        TestHelper( target, 1, true, true ); // compilation should fail, allow race
    }
}

// RemoteRaceWinRemote
//------------------------------------------------------------------------------
void TestDistributed::RemoteRaceWinRemote()
{
    // Check that a remote race that is won remotely is correctly handled
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/RemoteRaceWinRemote/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_ForceCleanBuild = true;
    options.m_NoLocalConsumptionOfRemoteJobs = true;
    FBuildForTest fBuild( options );

    TEST_ASSERT( fBuild.Initialize() );

    // start a client to emulate the other end
    Server s( 1 );
    s.Listen( Protocol::kTestPort );

    TEST_ASSERT( fBuild.Build( "RemoteRaceWinRemote" ) );
}

// RemoteRaceSystemFailure
//------------------------------------------------------------------------------
#if defined( ENABLE_FAKE_SYSTEM_FAILURE )
void TestDistributed::RemoteRaceSystemFailure()
{
    // NOTE: Test only available in DEBUG due to SetFakeSystemFailure

    // Check that a remote race that is won remotely with a failure is
    // correctly handled
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/RemoteRaceSystemFailure/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_ForceCleanBuild = true;
    options.m_NoLocalConsumptionOfRemoteJobs = true;
    options.m_DistVerbose = true;
    FBuildForTest fBuild( options );

    TEST_ASSERT( fBuild.Initialize() );

    // start a client to emulate the other end
    Server s( 1 );
    s.Listen( Protocol::kTestPort );

    // Force a system failure when compiling remotely
    ObjectNode::SetFakeSystemFailureForNextJob();

    // Build
    TEST_ASSERT( fBuild.Build( "RemoteRaceSystemFailure" ) );

    // Ensure the remote failure was induced correctly
    TEST_ASSERT( GetRecordedOutput().Find( "Injecting system failure (sFakeSystemFailure)" ) );
}
#endif

// AnonymousNamespaces
//------------------------------------------------------------------------------
void TestDistributed::AnonymousNamespaces()
{
    // Check that compiling multiple objects with identically named symbols
    // in anonymous namespaces don't cause link errors.  This is because
    // the MS compiler uses the path to the cpp file being compiled to
    // generate the symbol name (it doesn't respect the #line directives)
    const char * target( "../tmp/Test/Distributed/AnonymousNamespaces/AnonymousNamespaces.lib" );
    TestHelper( target, 1 );
}

// TestForceInclude
//------------------------------------------------------------------------------
void TestDistributed::TestForceInclude() const
{
    const char * target( "../tmp/Test/Distributed/ForceInclude/ForceInclude.lib" );
    TestHelper( target, 1 );
}

// ErrorsAreCorrectlyReported_MSVC
//------------------------------------------------------------------------------
void TestDistributed::ErrorsAreCorrectlyReported_MSVC() const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/ErrorsAreCorrectlyReported/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
    options.m_AllowLocalRace = false;
    options.m_ForceCleanBuild = true;

    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    // start a client to emulate the other end
    Server s( 1 );
    s.Listen( Protocol::kTestPort );

    // Check that build fails
    TEST_ASSERT( false == fBuild.Build( "ErrorsAreCorrectlyReported-MSVC" ) );

    // Check that error is returned
    TEST_ASSERT( GetRecordedOutput().Find( "error C2143" ) && GetRecordedOutput().Find( "missing ';' before '}'" ) );
}

// ErrorsAreCorrectlyReported_Clang
//------------------------------------------------------------------------------
void TestDistributed::ErrorsAreCorrectlyReported_Clang() const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/ErrorsAreCorrectlyReported/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
    options.m_AllowLocalRace = false;
    options.m_ForceCleanBuild = true;

    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    // start a client to emulate the other end
    Server s( 1 );
    s.Listen( Protocol::kTestPort );

    // Check that build fails
    TEST_ASSERT( false == fBuild.Build( "ErrorsAreCorrectlyReported-Clang" ) );

    // Check that error is returned
    TEST_ASSERT( GetRecordedOutput().Find( "error: expected ';' at end of declaration" ) );
}

// WarningsAreCorrectlyReported_MSVC
//------------------------------------------------------------------------------
void TestDistributed::WarningsAreCorrectlyReported_MSVC() const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/WarningsAreCorrectlyReported/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
    options.m_AllowLocalRace = false;
    options.m_ForceCleanBuild = true;

    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    // start a client to emulate the other end
    Server s( 1 );
    s.Listen( Protocol::kTestPort );

    // Check that build passes
    TEST_ASSERT( fBuild.Build( "WarningsAreCorrectlyReported-MSVC" ) );

    // Check that error is returned
    TEST_ASSERT( GetRecordedOutput().Find( "warning C4101" ) && GetRecordedOutput().Find( "'x': unreferenced local variable" ) );
}

// WarningsAreCorrectlyReported_Clang
//------------------------------------------------------------------------------
void TestDistributed::WarningsAreCorrectlyReported_Clang() const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/WarningsAreCorrectlyReported/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
    options.m_AllowLocalRace = false;
    options.m_ForceCleanBuild = true;

    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    // start a client to emulate the other end
    Server s( 1 );
    s.Listen( Protocol::kTestPort );

    // Check that build passes
    TEST_ASSERT( fBuild.Build( "WarningsAreCorrectlyReported-Clang" ) );

    // Check that error is returned
    TEST_ASSERT( GetRecordedOutput().Find( "warning: unused variable 'x' [-Wunused-variable]" ) );
}

// ShutdownMemoryLeak
//------------------------------------------------------------------------------
void TestDistributed::ShutdownMemoryLeak() const
{
    // Ensure clean shutdown (no leaks) if the build is aborted and there are
    // available distributable jobs
    //
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/ShutdownMemoryLeak/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
    options.m_AllowLocalRace = false;
    options.m_ForceCleanBuild = true;

    // Init
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    // NOTE: No remote server created so jobs stay in m_DistributableJobs_Available queue

    // Abort the build once some distributed jobs are available, to simulate Ctrl+C or other external stop
#if __has_feature( thread_sanitizer ) || defined( __SANITIZE_THREAD__ )
    // Code under ThreadSanitizer runs several time slower than normal, so we need a larger timeout.
    const float timeout = 30.0f;
#else
    const float timeout = 5.0f;
#endif
    auto hasDistributedJobs = []() { return ( Job::GetTotalLocalDataMemoryUsage() != 0 ); };
    BuildAborter aborter( hasDistributedJobs, timeout );

    // Start build and check it was aborted
    TEST_ASSERT( fBuild.Build( "ShutdownMemoryLeak" ) == false );
    TEST_ASSERT( GetRecordedOutput().Find( "FBuild: Incomplete: ShutdownMemoryLeak" ) );

    TEST_ASSERT( aborter.HasReached() );
}

// TestZiDebugFormat
//------------------------------------------------------------------------------
void TestDistributed::TestZiDebugFormat() const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
    options.m_AllowLocalRace = false;
    options.m_ForceCleanBuild = true;
    FBuildForTest fBuild( options );

    TEST_ASSERT( fBuild.Initialize() );

    // start a client to emulate the other end
    Server s( 1 );
    s.Listen( Protocol::kTestPort );

    TEST_ASSERT( fBuild.Build( "remoteZi" ) );
}

// TestZiDebugFormat_Local
//------------------------------------------------------------------------------
void TestDistributed::TestZiDebugFormat_Local() const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_ForceCleanBuild = true;
    FBuildForTest fBuild( options );

    TEST_ASSERT( fBuild.Initialize() );

    // start a client to emulate the other end
    Server s( 1 );
    s.Listen( Protocol::kTestPort );

    TEST_ASSERT( fBuild.Build( "remoteZi" ) );
}

// D8049_ToolLongDebugRecord
//------------------------------------------------------------------------------
void TestDistributed::D8049_ToolLongDebugRecord() const
{
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/D8049_ToolLongDebugRecord/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
    options.m_AllowLocalRace = false;
    options.m_ForceCleanBuild = true;
    FBuildForTest fBuild( options );

    TEST_ASSERT( fBuild.Initialize() );

    // start a client to emulate the other end
    Server s( 1 );
    s.Listen( Protocol::kTestPort );

    TEST_ASSERT( fBuild.Build( "D8049" ) );
}

// DynamicDeoptimization
//------------------------------------------------------------------------------
void TestDistributed::DynamicDeoptimization() const
{
#if defined( _MSC_VER ) && ( _MSC_VER >= 1944 ) // VS 2022 17.44.x
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/MSVCDynamicDeoptimization/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true; // ensure all jobs happen on the remote worker
    options.m_AllowLocalRace = false;
    options.m_ForceCleanBuild = true;
    FBuildForTest fBuild( options );

    TEST_ASSERT( fBuild.Initialize() );

    // /dynamicdeopt has an additional file which sits next to the normal one
    const char * const extraObjFile = "../tmp/Test/Distributed/MSVCDynamicDeoptimization/file.alt.obj";

    // Ensure file doesn't linker from prior test runs
    EnsureFileDoesNotExist( extraObjFile );

    // Start a server to emulate the other end
    Server s( 1 );
    s.Listen( Protocol::kTestPort );

    TEST_ASSERT( fBuild.Build( "MSVCDynamicDeoptimization" ) );

    // Ensure extra file was returned by worker
    EnsureFileExists( extraObjFile );
#else
    OUTPUT( "[SKIP] DynamicDeoptimization - /dynamicdeopt unavailable (requires VS2022 v17.44.x)\n" );
#endif
}

//------------------------------------------------------------------------------
void TestDistributed::CleanMessageToPreventMSBuildFailure() const
{
    // Error should be identical except for a single remove colon
    {
        const AStackString in( "C:\\Windows\\TEMP\\.fbuild.tmp\\0x00000000\\"
                               "core_1018\\F436D72E\\Module.MergeActors.cpp :"
                               " fatal error C1083: Cannot open compiler intermediate"
                               " file: 'C:\\Windows\\TEMP\\_CL_7a0408f6in':"
                               " No such file or directory" );
        AStackString expectedOut( in );
        TEST_ASSERT( expectedOut.Replace( "fatal error C1083:", "fatal error C1083 " ) == 1 );
        AStackString out;
        Node::CleanMessageToPreventMSBuildFailure( in, out );
        TEST_ASSERT( out == expectedOut );
    }

    // String may contain multiple lines
    {
        const AStackString in( "PROBLEM: C:\\Users\\Franta\\AppData\\Local\\Temp\\.fbuild.tmp\\0x96c448b7\\core_1001\\ErrorWithPercent.obj\n"
                               "ErrorWithPercent.cpp\n"
                               "C:\\p4\\depot\\Code\\Tools\\FBuild\\FBuildTest\\Data\\TestDistributed\\BadCode\\ErrorWithPercent.cpp(6,11): error C3193: '%': requires '/clr' or '/ZW' command line option\n"
                               "    int %s%s%s%s\n"
                               "          ^\n"
                               "C:\\p4\\depot\\Code\\Tools\\FBuild\\FBuildTest\\Data\\TestDistributed\\BadCode\\ErrorWithPercent.cpp(6,11): error C2143: syntax error: missing ';' before '%'\n"
                               "    int %s%s%s%s\n"
                               "          ^\n"
                               "C:\\p4\\depot\\Code\\Tools\\FBuild\\FBuildTest\\Data\\TestDistributed\\BadCode\\ErrorWithPercent.cpp(6,11): error C2530: 's': references must be initialized\n"
                               "    int %s%s%s%s\n"
                               "          ^\n"
                               "C:\\p4\\depot\\Code\\Tools\\FBuild\\FBuildTest\\Data\\TestDistributed\\BadCode\\ErrorWithPercent.cpp(6,13): error C3071: operator '%' can only be applied to an instance of a ref class or a value-type\n"
                               "    int %s%s%s%s\n"
                               "            ^\n"
                               "C:\\p4\\depot\\Code\\Tools\\FBuild\\FBuildTest\\Data\\TestDistributed\\BadCode\\ErrorWithPercent.cpp(7,1): error C2143: syntax error: missing ';' before '}'\n"
                               "}\n"
                               "^\n"
                               "C:\\p4\\depot\\Code\\Tools\\FBuild\\FBuildTest\\Data\\TestDistributed\\BadCode\\ErrorWithPercent.cpp(6,15): warning C4552: '%': result of expression not used\n"
                               "    int %s%s%s%s\n"
                               "              ^" );
        AStackString expectedOut( in );
        TEST_ASSERT( expectedOut.Replace( "error C3193:", "error C3193 " ) == 1 );
        TEST_ASSERT( expectedOut.Replace( "error C2143:", "error C2143 " ) == 2 );
        TEST_ASSERT( expectedOut.Replace( "error C2530:", "error C2530 " ) == 1 );
        TEST_ASSERT( expectedOut.Replace( "error C3071:", "error C3071 " ) == 1 );
        TEST_ASSERT( expectedOut.Replace( "warning C4552:", "warning C4552 " ) == 1 );
        AStackString out;
        Node::CleanMessageToPreventMSBuildFailure( in, out );
        TEST_ASSERT( out == expectedOut );
    }

    // Make sure it doesn't fail on other input (should leave string alone)
    {
        // empty
        AStackString out;
        Node::CleanMessageToPreventMSBuildFailure( AString::GetEmpty(), out );
        TEST_ASSERT( out.IsEmpty() );
    }
    {
        // path
        const AStackString in( "C:\\Windows\\TEMP\\_CL_7a0408f6in" );
        AStackString out;
        Node::CleanMessageToPreventMSBuildFailure( in, out );
        TEST_ASSERT( out == in );
    }
    {
        // error but not in expected format
        const AStackString in( "error C1083 and some other bits" );
        AStackString out;
        Node::CleanMessageToPreventMSBuildFailure( in, out );
        TEST_ASSERT( out == in );
    }
    {
        // keyword only
        const AStackString in( "error" );
        AStackString out;
        Node::CleanMessageToPreventMSBuildFailure( in, out );
        TEST_ASSERT( out == in );
    }
}

// PCH transform helpers
//------------------------------------------------------------------------------
namespace
{
    // The type in the header of a blob of a PCH that the PCH transform was applied to
    constexpr uint32_t kTransformedBlobType = ( Compressor::eZstd | ( (uint32_t)PchTransform::kPtrXfId << 8 ) | ( (uint32_t)PchTransform::kPtrXfVersion << 16 ) );

    // Collects what a blob decodes to
    class BlobCollector : public StreamDecompressor::Sink
    {
    public:
        virtual bool OnPiece( const void * data, size_t size ) override
        {
            const size_t oldSize = m_Data.GetSize();
            if ( ( oldSize + size ) > m_Data.GetCapacity() )
            {
                m_Data.SetCapacity( Math::Max( ( oldSize + size ), ( m_Data.GetCapacity() * 2 ) ) );
            }
            m_Data.SetSize( oldSize + size );
            memcpy( m_Data.Begin() + oldSize, data, size );
            return true;
        }
        Array<char> m_Data;
    };

    // Makes what a PCH transform makes damaged for a while, as one that did not invert would
    class DamagedTransform
    {
    public:
        DamagedTransform() { PchDataCache::Get().SetDamageTransform( true ); }
        ~DamagedTransform() { PchDataCache::Get().SetDamageTransform( false ); }
        explicit DamagedTransform( const DamagedTransform & other ) = delete;
        DamagedTransform & operator=( const DamagedTransform & other ) = delete;
    };
}

/*static*/ bool TestDistributed::WritePchLikeFile( const char * path, uint32_t size, uint32_t seed, Array<char> & outContent )
{
    MakePchLikeBytes( size, seed, outContent );
    if ( FileIO::EnsurePathExistsForFile( AStackString<>( path ) ) == false )
    {
        return false;
    }
    FileStream f;
    return f.Open( path, FileStream::WRITE_ONLY ) && ( f.Write( outContent.Begin(), size ) == size );
}

/*static*/ PchDataCache::CompressedState TestDistributed::WaitForForm( PchDataCache & cache,
                                                                        const PchDataCache::Entry & entry,
                                                                        bool allowTransform,
                                                                        AString & outFilePath,
                                                                        uint64_t & outSize,
                                                                        bool & outTransformed )
{
    PchDataCache::CompressedState state = PchDataCache::COMPRESSING;
    WaitUntil( [ & ]()
    {
        state = cache.GetCompressed( entry, outFilePath, outSize, allowTransform, &outTransformed );
        return ( state != PchDataCache::COMPRESSING );
    } );
    return state;
}

/*static*/ bool TestDistributed::BlobDecodesTo( const Array<char> & blob, const Array<char> & content )
{
    BlobCollector sink;
    uint32_t size = 0;
    return StreamDecompressor::DecompressMemory( blob.Begin(), blob.GetSize(), sink, size ) &&
           ( size == content.GetSize() ) &&
           ( sink.m_Data.GetSize() == content.GetSize() ) &&
           ( memcmp( sink.m_Data.Begin(), content.Begin(), content.GetSize() ) == 0 );
}

/*static*/ uint32_t TestDistributed::GetBlobType( const Array<char> & blob )
{
    uint32_t type = 0xFFFFFFFF;
    if ( blob.GetSize() >= sizeof( type ) )
    {
        memcpy( &type, blob.Begin(), sizeof( type ) );
    }
    return type;
}

/*static*/ uint32_t TestDistributed::CountInStatsLog( const char * text )
{
    AStackString<> logPath;
    VERIFY( FBuild::GetTempDir( logPath ) );
    logPath += "FBuild-distribution-stats.log";
    Array<char> log;
    if ( LoadFile( logPath, log ) == false )
    {
        return 0;
    }
    log.Append( 0 );
    uint32_t count = 0;
    for ( const char * found = strstr( log.Begin(), text ); found; found = strstr( found + 1, text ) )
    {
        ++count;
    }
    return count;
}

/*static*/ void TestDistributed::ClearWorkerPchStore()
{
    AStackString<> cacheDir;
    VERIFY( FBuild::GetWorkerCacheDir( cacheDir ) );
    Array<AString> files;
    FileIO::GetFiles( cacheDir, AStackString( "*" ), true, &files );
    for ( const AString & file : files )
    {
        if ( file.EndsWith( PchBlob::kStoredFileName ) || file.EndsWith( PchBlob::kRawFileName ) )
        {
            FileIO::FileDelete( file.Get() );
        }
    }
}

// PchTransformCapabilityIsNegotiated
//------------------------------------------------------------------------------
void TestDistributed::PchTransformCapabilityIsNegotiated() const
{
    // A capability is used on a connection only if both sides have it
    const uint32_t bit = Protocol::CAP_PCH_TRANSFORM;
    volatile uint32_t all = Protocol::kCapabilities; // Not a constant, so that what is made of it is checked when it is run
    TEST_ASSERT( ( all & bit ) != 0 );
    TEST_ASSERT( ( Protocol::Negotiate( all, all ) & bit ) != 0 );
    TEST_ASSERT( ( Protocol::Negotiate( all & ~bit, all ) & bit ) == 0 );
    TEST_ASSERT( ( Protocol::Negotiate( all, all & ~bit ) & bit ) == 0 );
    TEST_ASSERT( Protocol::Negotiate( all & ~bit, all ) == ( all & ~bit ) ); // The others are as they were

    // The worker tells a client that has it that it can be used, and one that does not that it cannot
    struct Case { uint32_t m_Client; uint32_t m_Ack; };
    const Case cases[] = {
        { bit | Protocol::CAP_PCH_REQUEST, bit | Protocol::CAP_PCH_REQUEST },
        { Protocol::CAP_PCH_REQUEST, Protocol::CAP_PCH_REQUEST },
        { 0, 0 },
        { bit | ( 1u << 31 ), bit },
    };
    for ( const Case & c : cases )
    {
        Server s( 1 );
        TEST_ASSERT( s.Listen( Protocol::kTestPort ) );
        HandshakeProbe probe;
        const ConnectionInfo * ci = probe.Connect( AStackString( "127.0.0.1" ), Protocol::kTestPort, 2000 );
        TEST_ASSERT( ci );
        HandshakeBytes handshake( c.m_Client );
        TEST_ASSERT( probe.Send( ci, handshake.bytes, sizeof( handshake.bytes ) ) );
        TEST_ASSERT( WaitUntil( [ &probe ]() { return ( probe.GetNumMessages() >= 1 ); } ) );
        TEST_ASSERT( probe.GetAckCapabilities() == c.m_Ack );
        probe.ShutdownAllConnections();
    }

    // The client says it has it
    NoCapacityWorker worker;
    TEST_ASSERT( BuildPchLocal( worker ).m_Built );
    TEST_ASSERT( ( worker.GetClientCapabilities() & bit ) != 0 );
}

// PchIsCompressedInTheFormsWorkersRead
//------------------------------------------------------------------------------
void TestDistributed::PchIsCompressedInTheFormsWorkersRead() const
{
    PchDataCache cache;
    Array<char> content;
    const AStackString<> path( "../tmp/Test/Distributed/PchTransformForms/pchlike.pch" );
    const uint32_t fileSize = ( 3 * MEGABYTE ) + 5;
    TEST_ASSERT( WritePchLikeFile( path.Get(), fileSize, 1, content ) );
    PchDataCache::Entry entry;
    entry.pchId = PchId::Calc( content.Begin(), fileSize );
    entry.uncompressedSize = fileSize;
    entry.filePath = path;
    cache.Store( entry );

    // A worker that reads the transform is given the PCH after it
    AStackString<> transformedPath;
    uint64_t transformedSize = 0;
    bool isTransformed = false;
    TEST_ASSERT( WaitForForm( cache, entry, true, transformedPath, transformedSize, isTransformed ) == PchDataCache::READY );
    TEST_ASSERT( isTransformed );
    Array<char> transformedBlob;
    TEST_ASSERT( LoadFile( transformedPath, transformedBlob ) );
    TEST_ASSERT( transformedBlob.GetSize() == transformedSize );
    TEST_ASSERT( GetBlobType( transformedBlob ) == kTransformedBlobType );
    TEST_ASSERT( BlobDecodesTo( transformedBlob, content ) );
    TEST_ASSERT( cache.GetNumCompressions() == 1 );

    // One that does not is given what 24.0 sends: it is made when it is asked for
    AStackString<> plainPath;
    uint64_t plainSize = 0;
    bool plainIsTransformed = true;
    TEST_ASSERT( WaitForForm( cache, entry, false, plainPath, plainSize, plainIsTransformed ) == PchDataCache::READY );
    TEST_ASSERT( plainIsTransformed == false );
    TEST_ASSERT( plainPath != transformedPath );
    TEST_ASSERT( cache.GetNumCompressions() == 2 );
    Array<char> plainBlob;
    TEST_ASSERT( LoadFile( plainPath, plainBlob ) );
    TEST_ASSERT( GetBlobType( plainBlob ) == Compressor::eZstd );
    TEST_ASSERT( DecodesToFile( plainPath, content ) ); // As a worker of 24.0 decodes it
    OUTPUT( "PchIsCompressedInTheFormsWorkersRead: %" PRIu64 " bytes, %" PRIu64 " with the transform (%.1f%%)\n", plainSize, transformedSize, ( 100.0 * (double)transformedSize ) / (double)plainSize );
    TEST_ASSERT( ( transformedSize * 100 ) < ( plainSize * 90 ) );

    // Each is made once, however often it is asked for
    {
        AStackString<> again;
        uint64_t againSize = 0;
        bool againTransformed = false;
        TEST_ASSERT( cache.GetCompressed( entry, again, againSize, true, &againTransformed ) == PchDataCache::READY );
        TEST_ASSERT( ( again == transformedPath ) && ( againSize == transformedSize ) && againTransformed );
        TEST_ASSERT( cache.GetCompressed( entry, again, againSize, false, &againTransformed ) == PchDataCache::READY );
        TEST_ASSERT( ( again == plainPath ) && ( againSize == plainSize ) && ( againTransformed == false ) );
        TEST_ASSERT( cache.GetNumCompressions() == 2 );
    }

    // What a connection takes is the form its worker reads
    for ( uint32_t allow = 0; allow < 2; ++allow )
    {
        Array<uint64_t> pending;
        pending.Append( entry.pchId );
        Array<uint64_t> onWorker;
        Array<PchDataCache::ReadyPch> ready;
        TEST_ASSERT( cache.TakeReadyPchs( pending, onWorker, ready, ( allow != 0 ) ) );
        TEST_ASSERT( pending.IsEmpty() );
        TEST_ASSERT( ready.GetSize() == 1 );
        TEST_ASSERT( ready[ 0 ].transformed == ( allow != 0 ) );
        TEST_ASSERT( ready[ 0 ].filePath == ( allow ? transformedPath : plainPath ) );
        TEST_ASSERT( ready[ 0 ].dataSize == ( allow ? transformedSize : plainSize ) );
        TEST_ASSERT( ready[ 0 ].uncompressedSize == fileSize );
    }

    // A PCH that compresses but has no pointers is not transformed either
    {
        Array<char> notAPch;
        const AStackString<> notAPchPath( "../tmp/Test/Distributed/PchTransformForms/notapch.pch" );
        TEST_ASSERT( WriteTestFile( notAPchPath.Get(), ( 2 * MEGABYTE ) + 3, notAPch ) );
        PchDataCache::Entry notAPchEntry;
        notAPchEntry.pchId = PchId::Calc( notAPch.Begin(), notAPch.GetSize() );
        notAPchEntry.uncompressedSize = (uint32_t)notAPch.GetSize();
        notAPchEntry.filePath = notAPchPath;
        cache.Store( notAPchEntry );
        AStackString<> asked;
        uint64_t askedSize = 0;
        bool askedTransformed = true;
        TEST_ASSERT( WaitForForm( cache, notAPchEntry, true, asked, askedSize, askedTransformed ) == PchDataCache::READY );
        TEST_ASSERT( askedTransformed == false );
        TEST_ASSERT( DecodesToFile( asked, notAPch ) );
        TEST_ASSERT( cache.GetNumCompressions() == 3 );
    }

    // A PCH the transform does not help is sent as it is to a worker that reads the transform, and is made once
    {
        Array<char> random;
        MakeRandomBytes( ( 2 * MEGABYTE ) + 3, 9, random );
        const AStackString<> randomPath( "../tmp/Test/Distributed/PchTransformForms/random.pch" );
        {
            FileStream f;
            TEST_ASSERT( FileIO::EnsurePathExistsForFile( randomPath ) );
            TEST_ASSERT( f.Open( randomPath.Get(), FileStream::WRITE_ONLY ) );
            TEST_ASSERT( f.Write( random.Begin(), random.GetSize() ) == random.GetSize() );
        }
        PchDataCache::Entry randomEntry;
        randomEntry.pchId = PchId::Calc( random.Begin(), random.GetSize() );
        randomEntry.uncompressedSize = (uint32_t)random.GetSize();
        randomEntry.filePath = randomPath;
        cache.Store( randomEntry );

        AStackString<> asked;
        uint64_t askedSize = 0;
        bool askedTransformed = true;
        TEST_ASSERT( WaitForForm( cache, randomEntry, true, asked, askedSize, askedTransformed ) == PchDataCache::READY );
        TEST_ASSERT( askedTransformed == false );
        Array<char> askedBlob;
        TEST_ASSERT( LoadFile( asked, askedBlob ) );
        TEST_ASSERT( ( GetBlobType( askedBlob ) & ~0xFFu ) == 0 ); // Not of a transform
        TEST_ASSERT( DecodesToFile( asked, random ) );
        TEST_ASSERT( cache.GetNumCompressions() == 4 );

        AStackString<> other;
        uint64_t otherSize = 0;
        bool otherTransformed = true;
        TEST_ASSERT( cache.GetCompressed( randomEntry, other, otherSize, false, &otherTransformed ) == PchDataCache::READY );
        TEST_ASSERT( ( other == asked ) && ( otherSize == askedSize ) && ( otherTransformed == false ) );
        TEST_ASSERT( cache.GetNumCompressions() == 4 );
    }
}

// PchTransformThatDoesNotInvertIsNotSent
//------------------------------------------------------------------------------
void TestDistributed::PchTransformThatDoesNotInvertIsNotSent() const
{
    Array<char> content;
    const AStackString<> path( "../tmp/Test/Distributed/PchTransformDamaged/pchlike.pch" );
    const uint32_t fileSize = ( 2 * MEGABYTE ) + 1;
    TEST_ASSERT( WritePchLikeFile( path.Get(), fileSize, 2, content ) );
    PchDataCache::Entry entry;
    entry.pchId = PchId::Calc( content.Begin(), fileSize );
    entry.uncompressedSize = fileSize;
    entry.filePath = path;

    // Nothing is wrong with the PCH: the transform is
    {
        PchDataCache cache;
        cache.SetDamageTransform( true );
        cache.Store( entry );
        AStackString<> filePath;
        uint64_t size = 0;
        bool isTransformed = true;
        TEST_ASSERT( WaitForForm( cache, entry, true, filePath, size, isTransformed ) == PchDataCache::READY );

        // What is sent is the PCH compressed as it was before there was a transform, and nothing of the other is left
        TEST_ASSERT( isTransformed == false );
        Array<char> blob;
        TEST_ASSERT( LoadFile( filePath, blob ) );
        TEST_ASSERT( GetBlobType( blob ) == Compressor::eZstd );
        TEST_ASSERT( DecodesToFile( filePath, content ) );
        TEST_ASSERT( cache.HasCompressionFailed( entry.pchId ) == false );
        TEST_ASSERT( cache.GetFailedAttempts( entry.pchId ) == 0 );
        TEST_ASSERT( cache.GetNumCompressions() == 1 );
        AStackString<> sendDir;
        TEST_ASSERT( cache.GetSendDirectory( sendDir ) );
        Array<AString> files;
        FileIO::GetFiles( sendDir, AStackString( "*" ), true, &files );
        TEST_ASSERT( files.GetSize() == 1 );
    }

    // The same PCH is sent after the transform when it inverts
    {
        PchDataCache cache;
        cache.Store( entry );
        AStackString<> filePath;
        uint64_t size = 0;
        bool isTransformed = false;
        TEST_ASSERT( WaitForForm( cache, entry, true, filePath, size, isTransformed ) == PchDataCache::READY );
        TEST_ASSERT( isTransformed );
    }
}

// PchIsCompressedWithTheWideWindow
//------------------------------------------------------------------------------
void TestDistributed::PchIsCompressedWithTheWideWindow() const
{
    // What a client sends is compressed with the window of Compressor::kZstdPchWindowLog, in both forms. The PCH is
    // larger than the window, as the window of a frame is no larger than the data.
    PchDataCache cache;
    Array<char> content;
    const AStackString<> path( "../tmp/Test/Distributed/PchWideWindow/pchlike.pch" );
    const uint32_t fileSize = ( 70 * MEGABYTE ) + 3;
    TEST_ASSERT( WritePchLikeFile( path.Get(), fileSize, 5, content ) );
    PchDataCache::Entry entry;
    entry.pchId = PchId::Calc( content.Begin(), fileSize );
    entry.uncompressedSize = fileSize;
    entry.filePath = path;
    cache.Store( entry );
    for ( uint32_t allow = 0; allow < 2; ++allow )
    {
        AStackString<> filePath;
        uint64_t size = 0;
        bool isTransformed = false;
        TEST_ASSERT( WaitForForm( cache, entry, ( allow != 0 ), filePath, size, isTransformed ) == PchDataCache::READY );
        TEST_ASSERT( isTransformed == ( allow != 0 ) );
        Array<char> blob;
        TEST_ASSERT( LoadFile( filePath, blob ) );
        TEST_ASSERT( GetBlobWindowLog( blob ) == 26 );
        TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize(), entry.pchId, fileSize ) );
    }
}

// WorkerStoresATransformedPch
//------------------------------------------------------------------------------
void TestDistributed::WorkerStoresATransformedPch() const
{
    Array<char> content;
    MakePchLikeBytes( ( 3 * MEGABYTE ) + 5, 3, content );
    Array<char> blob;
    Array<char> plainBlob;
    bool transformed = false;
    TEST_ASSERT( MakePchBlob( content, kPchLikeBase, blob, &transformed ) );
    TEST_ASSERT( transformed );
    TEST_ASSERT( MakePchBlob( content, 0, plainBlob ) );
    TEST_ASSERT( blob.GetSize() < plainBlob.GetSize() );
    const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
    ClearWorkerPchStore();

    Server worker( 1 );
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
    PchSender sender;
    TEST_ASSERT( sender.ConnectToWorker() );
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)blob.GetSize(), Protocol::kPchChunkSize );
    TEST_ASSERT( sender.SendChunks( pchId, (uint32_t)content.GetSize(), blob.Begin(), (uint32_t)blob.GetSize(), Protocol::kPchChunkSize, 0, numChunks ) );
    AStackString<> storedFile;
    TEST_ASSERT( WaitUntil( [ & ]() { return worker.FindCachedPch( pchId, storedFile ); } ) );

    // What is kept is what was sent: the store is as small as the wire was
    Array<char> onDisk;
    TEST_ASSERT( LoadFile( storedFile, onDisk ) );
    TEST_ASSERT( onDisk.GetSize() == blob.GetSize() );
    TEST_ASSERT( memcmp( onDisk.Begin(), blob.Begin(), blob.GetSize() ) == 0 );
    TEST_ASSERT( GetBlobType( onDisk ) == kTransformedBlobType );
    {
        MutexHolder mh( worker.m_PchCacheMutex );
        TEST_ASSERT( worker.m_PchCache.GetSize() == 1 );
        TEST_ASSERT( worker.m_PchCache[ 0 ].size == blob.GetSize() );
    }

    // A job is given the PCH, with the transform undone
    Job * job = MakePchJob( worker, pchId );
    TEST_ASSERT( WaitUntil( [ & ]() { return worker.TryResolvePch( *job ); } ) );
    Array<char> raw;
    TEST_ASSERT( LoadFile( job->GetPchCachePath(), raw ) );
    TEST_ASSERT( raw.GetSize() == content.GetSize() );
    TEST_ASSERT( memcmp( raw.Begin(), content.Begin(), raw.GetSize() ) == 0 );
    TEST_ASSERT( worker.m_NumPchsUnpacked.Load() == 1 );
    TEST_ASSERT( FileIO::FileExists( storedFile.Get() ) ); // The store is what it was
    FDELETE job;
}

// WorkerAcceptsAPlainPchFromAnOldClient
//------------------------------------------------------------------------------
void TestDistributed::WorkerAcceptsAPlainPchFromAnOldClient() const
{
    // A client of 24.0 sends the PCH compressed, and the worker keeps and unpacks it as before
    Array<char> content;
    MakePchLikeBytes( ( 3 * MEGABYTE ) + 5, 4, content );
    Array<char> blob;
    bool transformed = true;
    TEST_ASSERT( MakePchBlob( content, 0, blob, &transformed ) );
    TEST_ASSERT( transformed == false );
    TEST_ASSERT( GetBlobType( blob ) == Compressor::eZstd );
    const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
    ClearWorkerPchStore();

    Server worker( 1 );
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
    PchSender sender;
    TEST_ASSERT( sender.ConnectToWorker() );
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)blob.GetSize(), Protocol::kPchChunkSize );
    TEST_ASSERT( sender.SendChunks( pchId, (uint32_t)content.GetSize(), blob.Begin(), (uint32_t)blob.GetSize(), Protocol::kPchChunkSize, 0, numChunks ) );
    AStackString<> storedFile;
    TEST_ASSERT( WaitUntil( [ & ]() { return worker.FindCachedPch( pchId, storedFile ); } ) );

    Array<char> onDisk;
    TEST_ASSERT( LoadFile( storedFile, onDisk ) );
    TEST_ASSERT( onDisk.GetSize() == blob.GetSize() );
    TEST_ASSERT( memcmp( onDisk.Begin(), blob.Begin(), blob.GetSize() ) == 0 );
    TEST_ASSERT( DecodesToFile( storedFile, content ) );

    Job * job = MakePchJob( worker, pchId );
    TEST_ASSERT( WaitUntil( [ & ]() { return worker.TryResolvePch( *job ); } ) );
    Array<char> raw;
    TEST_ASSERT( LoadFile( job->GetPchCachePath(), raw ) );
    TEST_ASSERT( raw.GetSize() == content.GetSize() );
    TEST_ASSERT( memcmp( raw.Begin(), content.Begin(), raw.GetSize() ) == 0 );
    FDELETE job;
}

// WorkerRejectsATransformedPchThatIsNotValid
//------------------------------------------------------------------------------
void TestDistributed::WorkerRejectsATransformedPchThatIsNotValid() const
{
    enum Fault { NONE, BODY, TRANSFORM_VERSION, TRANSFORM_ID, WRONG_ID, WRONG_SIZE, NUM_FAULTS };
    for ( uint32_t fault = NONE; fault < NUM_FAULTS; ++fault )
    {
        Array<char> content;
        MakePchLikeBytes( ( 2 * MEGABYTE ) + 3, 10 + fault, content );
        Array<char> blob;
        bool transformed = false;
        TEST_ASSERT( MakePchBlob( content, kPchLikeBase, blob, &transformed ) );
        TEST_ASSERT( transformed );
        uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
        uint32_t size = (uint32_t)content.GetSize();
        uint32_t type = GetBlobType( blob );
        switch ( fault )
        {
            case BODY:              blob[ blob.GetSize() / 2 ] ^= 0x33; break;
            case TRANSFORM_VERSION: type += ( 1u << 16 ); break;
            case TRANSFORM_ID:      type += ( 1u << 8 ); break;
            case WRONG_ID:          pchId++; break;
            case WRONG_SIZE:        size += 8; break;
            default:                break;
        }
        memcpy( blob.Begin(), &type, sizeof( type ) );
        ClearWorkerPchStore();

        Server worker( 1 );
        TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
        auto numClients = [ &worker ]()
        {
            MutexHolder mh( worker.m_ClientListMutex );
            return worker.m_ClientList.GetSize();
        };
        PchSender sender;
        TEST_ASSERT( sender.ConnectToWorker() );
        TEST_ASSERT( WaitUntil( [ & ]() { return numClients() == 1; } ) );
        const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)blob.GetSize(), Protocol::kPchChunkSize );
        TEST_ASSERT( sender.SendChunks( pchId, size, blob.Begin(), (uint32_t)blob.GetSize(), Protocol::kPchChunkSize, 0, numChunks ) );

        AStackString<> storedFile;
        if ( fault == NONE )
        {
            TEST_ASSERT( WaitUntil( [ & ]() { return worker.FindCachedPch( pchId, storedFile ); } ) );
            TEST_ASSERT( numClients() == 1 );
            continue;
        }

        // A PCH that is not valid is rejected as one that is corrupt: the connection is dropped, and nothing is kept
        TEST_ASSERT( WaitUntil( [ & ]() { return numClients() == 0; } ) );
        TEST_ASSERT( worker.FindCachedPch( pchId, storedFile ) == false );
        AStackString<> dir;
        GetWorkerPchDir( pchId, dir );
        Array<AString> files;
        FileIO::GetFiles( dir, AStackString( "*" ), true, &files );
        TEST_ASSERT( files.IsEmpty() );
    }
}

// StoredTransformedPchThatCannotBeInvertedIsDropped
//------------------------------------------------------------------------------
void TestDistributed::StoredTransformedPchThatCannotBeInvertedIsDropped() const
{
    Array<char> content;
    MakePchLikeBytes( ( 2 * MEGABYTE ) + 3, 21, content );
    Array<char> good;
    bool transformed = false;
    TEST_ASSERT( MakePchBlob( content, kPchLikeBase, good, &transformed ) );
    TEST_ASSERT( transformed );
    const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );

    enum Case { GOOD, DAMAGED_BODY, UNKNOWN_VERSION, UNKNOWN_ID, NUM_CASES };
    for ( uint32_t which = GOOD; which < NUM_CASES; ++which )
    {
        Array<char> blob;
        blob = good;
        uint32_t type = GetBlobType( blob );
        if ( which == DAMAGED_BODY )
        {
            for ( size_t i = ( Compressor::kHeaderSize + 100 ); i < blob.GetSize(); i += ( blob.GetSize() / 9 ) )
            {
                blob[ i ] = (char)( blob[ i ] ^ 0x5A );
            }
        }
        else if ( which == UNKNOWN_VERSION )
        {
            type += ( 1u << 16 );
        }
        else if ( which == UNKNOWN_ID )
        {
            type += ( 1u << 8 );
        }
        memcpy( blob.Begin(), &type, sizeof( type ) );
        ClearWorkerPchStore(); // What an earlier case left would be found by the worker

        Server s( 1 );
        AStackString<> storedFile;
        AddStoredBlob( s, pchId, blob.Begin(), blob.GetSize(), true, storedFile );
        AStackString<> rawFile;
        TEST_ASSERT( Server::GetRawPchPath( storedFile, rawFile ) );
        AStackString<> dir( storedFile.Get(), storedFile.FindLast( NATIVE_SLASH ) + 1 );

        Job * job = MakePchJob( s, pchId );
        if ( which == GOOD )
        {
            TEST_ASSERT( WaitUntil( [ & ]() { return s.TryResolvePch( *job ); } ) );
            Array<char> raw;
            TEST_ASSERT( LoadFile( rawFile, raw ) );
            TEST_ASSERT( raw.GetSize() == content.GetSize() );
            TEST_ASSERT( memcmp( raw.Begin(), content.Begin(), raw.GetSize() ) == 0 );
        }
        else
        {
            // As a PCH that is damaged: the job waits, the PCH is not unpacked again, and is not kept, so it can be sent again
            TEST_ASSERT( s.TryResolvePch( *job ) == false );
            TEST_ASSERTM( WaitUntil( [ & ]() { return ( s.IsPchCached( pchId ) == false ); } ), "case %u", which );
            TEST_ASSERT( WaitUntil( [ & ]() { return ( s.m_PchUnpackQueue.IsEmpty() && ( IsPchBeingUnpacked( s, pchId ) == false ) ); } ) );
            TEST_ASSERT( job->GetPchCachePath().IsEmpty() );
            TEST_ASSERT( s.m_NumPchsUnpacked.Load() == 0 );
            TEST_ASSERT( s.m_NumPchUnpackAttempts.Load() == 1 );
            TEST_ASSERT( FileIO::FileExists( storedFile.Get() ) == false );
            TEST_ASSERT( FileIO::FileExists( rawFile.Get() ) == false );
            TEST_ASSERT( FileIO::DirectoryExists( dir ) == false );
        }
        FDELETE job;
    }
}

#if defined( __WINDOWS__ )

// ClientSendsTheTransformedPchOnlyToWorkersThatReadIt
//------------------------------------------------------------------------------
void TestDistributed::ClientSendsTheTransformedPchOnlyToWorkersThatReadIt() const
{
    const uint32_t withoutTransform = ( Protocol::kCapabilities & ~Protocol::CAP_PCH_TRANSFORM );
    const uint32_t numLoggedBefore = CountInStatsLog( "pch-transform:" );

    // A worker of this version, and a client of it: the PCH is sent after the transform
    uint32_t transformedSize = 0;
    uint32_t uncompressedSize = 0;
    {
        NoCapacityWorker worker( Protocol::kCapabilities );
        BuildPchLibraryUntilStopped( worker, BuildStop::PCH_ARRIVES );
        TEST_ASSERT( worker.GetPchType() == kTransformedBlobType );
        transformedSize = worker.GetPchCompressedSize();
        uncompressedSize = worker.GetPchUncompressedSize();
    }
    TEST_ASSERT( CountInStatsLog( "pch-transform:" ) == ( numLoggedBefore + 1 ) ); // The connection says so in its stats

    // A worker of 24.0, which does not advertise the capability: it is sent what it reads
    uint32_t plainSize = 0;
    {
        NoCapacityWorker worker( withoutTransform );
        BuildPchLibraryUntilStopped( worker, BuildStop::PCH_ARRIVES );
        TEST_ASSERT( worker.GetPchType() == Compressor::eZstd );
        TEST_ASSERT( worker.GetPchUncompressedSize() == uncompressedSize );
        plainSize = worker.GetPchCompressedSize();
    }
    OUTPUT( "ClientSendsTheTransformedPchOnlyToWorkersThatReadIt: PCH of %u bytes, %u sent, %u with the transform (%.1f%%)\n",
            uncompressedSize, plainSize, transformedSize, ( 100.0 * transformedSize ) / plainSize );
    TEST_ASSERT( ( (uint64_t)transformedSize * 100 ) < ( (uint64_t)plainSize * 95 ) );

    // A client that does not advertise it is not sent it by a worker that has it
    {
        NoCapacityWorker worker( Protocol::kCapabilities );
        BuildPchLibraryUntilStopped( worker, BuildStop::PCH_ARRIVES, withoutTransform );
        TEST_ASSERT( worker.GetClientCapabilities() == withoutTransform );
        TEST_ASSERT( worker.GetPchType() == Compressor::eZstd );
    }
    TEST_ASSERT( CountInStatsLog( "pch-transform:" ) == ( numLoggedBefore + 1 ) ); // Only the first
}

// Workers of two kinds
//------------------------------------------------------------------------------
namespace
{
    // A worker that is of two kinds, as a fleet is while it is updated: the first connection it is given has
    // the PCH transform, and the next has not. It takes no job, and notes what each connection is sent for a PCH.
    // It tells the connections apart, as FakeWorker, which reads one at a time, does not.
    class TwoKindsOfWorker : public TCPConnectionPool
    {
    public:
        struct Received
        {
            uint32_t m_Capabilities; // Of the connection it came on
            uint32_t m_Type;         // In the header of the data
            uint32_t m_CompressedSize;
            uint32_t m_UncompressedSize;
        };

        TwoKindsOfWorker() = default;
        virtual ~TwoKindsOfWorker() override { ShutdownAllConnections(); }
        explicit TwoKindsOfWorker( const TwoKindsOfWorker & other ) = delete;
        TwoKindsOfWorker & operator=( const TwoKindsOfWorker & other ) = delete;

        size_t GetNumConnections() const
        {
            MutexHolder mh( m_Mutex );
            return m_Connections.GetSize();
        }
        size_t GetNumReceived() const
        {
            MutexHolder mh( m_Mutex );
            return m_Received.GetSize();
        }
        Received GetReceived( size_t index ) const
        {
            MutexHolder mh( m_Mutex );
            return m_Received[ index ];
        }

    private:
        struct Connection
        {
            const ConnectionInfo *      m_Info;
            uint32_t                    m_Capabilities;
            const Protocol::IMessage *  m_Message;  // Its header, until its payload arrives
        };

        virtual void OnReceive( const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory ) override
        {
            keepMemory = true; // freed below

            const Protocol::IMessage * message = nullptr;
            void * payload = nullptr;
            uint32_t capabilities = 0;
            {
                MutexHolder mh( m_Mutex );
                Connection * state = nullptr;
                for ( Connection & c : m_Connections )
                {
                    if ( c.m_Info == connection )
                    {
                        state = &c;
                    }
                }
                if ( state == nullptr )
                {
                    Connection added;
                    added.m_Info = connection;
                    added.m_Capabilities = ( m_Connections.GetSize() == 0 ) ? Protocol::kCapabilities : ( Protocol::kCapabilities & ~Protocol::CAP_PCH_TRANSFORM );
                    added.m_Message = nullptr;
                    m_Connections.Append( added );
                    state = &m_Connections[ m_Connections.GetSize() - 1 ];
                }
                capabilities = state->m_Capabilities;
                if ( state->m_Message == nullptr )
                {
                    state->m_Message = static_cast<const Protocol::IMessage *>( data );
                    if ( state->m_Message->HasPayload() )
                    {
                        return;
                    }
                }
                else
                {
                    payload = data;
                }
                message = state->m_Message;
                state->m_Message = nullptr;
            }

            if ( message->GetType() == Protocol::MSG_CONNECTION )
            {
                const Protocol::MsgConnectionAck ack( 0, capabilities );
                ack.Send( connection );
                const Protocol::MsgPchInventory pchInventory( 0 );
                pchInventory.Send( connection );
                const Protocol::MsgToolchainInventory toolchainInventory( 0 );
                toolchainInventory.Send( connection );
            }
            else if ( message->GetType() == Protocol::MSG_PCH_FILE )
            {
                const Protocol::MsgPchFile & pchFile = static_cast<const Protocol::MsgPchFile &>( *message );
                if ( ( pchFile.GetChunkIndex() == 0 ) && ( size >= Compressor::kHeaderSize ) )
                {
                    Received received;
                    memcpy( &received.m_Type, payload, sizeof( received.m_Type ) );
                    received.m_Capabilities = capabilities;
                    received.m_CompressedSize = pchFile.GetCompressedSize();
                    received.m_UncompressedSize = pchFile.GetUncompressedSize();
                    MutexHolder mh( m_Mutex );
                    m_Received.Append( received );
                }
            }
            (void)size;
            FREE( (void *)message );
            FREE( payload );
        }

        mutable Mutex m_Mutex;
        Array<Connection> m_Connections;
        Array<Received> m_Received;
    };
}

// WorkersOfBothKindsAreSentTheirForms
//------------------------------------------------------------------------------
void TestDistributed::WorkersOfBothKindsAreSentTheirForms() const
{
    // One build with two workers, one that reads the transform and one that does not. The PCH is made in both forms.
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestPchTransform/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true;
    options.m_AllowLocalRace = false;
    options.m_ForceCleanBuild = true;
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    TwoKindsOfWorker worker;
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
    auto bothReceived = [ &worker ]() { return ( worker.GetNumReceived() >= 2 ); };
    BuildAborter aborter( bothReceived, kWaitTimeoutSeconds );
    const bool built = fBuild.Build( "../tmp/Test/PchTransform/TwoWorkers/twoworkers.lib" );
    worker.ShutdownAllConnections();
    TEST_ASSERT( built == false ); // Aborted
    TEST_ASSERT( aborter.HasReached() );
    TEST_ASSERT( worker.GetNumConnections() == 2 );

    // The one that has the transform is sent the PCH after it, and the other is sent what it reads
    uint32_t transformedSize = 0;
    uint32_t plainSize = 0;
    for ( size_t i = 0; i < 2; ++i )
    {
        const TwoKindsOfWorker::Received received = worker.GetReceived( i );
        if ( received.m_Capabilities & Protocol::CAP_PCH_TRANSFORM )
        {
            TEST_ASSERT( received.m_Type == kTransformedBlobType );
            transformedSize = received.m_CompressedSize;
        }
        else
        {
            TEST_ASSERT( received.m_Type == Compressor::eZstd );
            plainSize = received.m_CompressedSize;
        }
        TEST_ASSERT( received.m_UncompressedSize == worker.GetReceived( 0 ).m_UncompressedSize );
    }
    TEST_ASSERT( transformedSize != 0 );
    TEST_ASSERT( plainSize != 0 );
    TEST_ASSERT( ( (uint64_t)transformedSize * 100 ) < ( (uint64_t)plainSize * 95 ) );
}

// PchTransformedThroughTheBuild
//------------------------------------------------------------------------------
void TestDistributed::PchTransformedThroughTheBuild() const
{
    const char * target( "../tmp/Test/Distributed/distpch.lib" );
    const AStackString<> dbFile( "../tmp/Test/PchTransform/Build.db" );
    FileIO::FileDelete( dbFile.Get() );
    TEST_ASSERT( FileIO::EnsurePathExistsForFile( dbFile ) );
    const char * objects[] = { "a_pch", "b_pch", "c_pch" };

    auto cleanOutput = []()
    {
        Array<AString> files;
        FileIO::GetFiles( AStackString( "../tmp/Test/Distributed" ), AStackString( "*.*" ), true, &files );
        for ( const AString & file : files )
        {
            FileIO::FileDelete( file.Get() );
        }
    };
    // An object file is the same but for the time it was made, which is in its header
    auto IsSameObject = []( const Array<char> & a, const Array<char> & b )
    {
        if ( a.GetSize() != b.GetSize() )
        {
            return false;
        }
        for ( size_t i = 0; i < a.GetSize(); ++i )
        {
            const bool isTimeStamp = ( ( i >= 4 ) && ( i < 8 ) );
            if ( ( a[ i ] != b[ i ] ) && ( isTimeStamp == false ) )
            {
                return false;
            }
        }
        return true;
    };
    auto readObject = [ & ]( const char * name, Array<char> & outBytes )
    {
        AStackString<> path;
        path.Format( "../tmp/Test/Distributed/%s.obj", name );
        TEST_ASSERT( LoadFile( path, outBytes ) );
    };
    auto deleteObjects = [ & ]()
    {
        for ( const char * object : objects )
        {
            AStackString<> path;
            path.Format( "../tmp/Test/Distributed/%s.obj", object );
            TEST_ASSERT( FileIO::FileDelete( path.Get() ) );
        }
        FileIO::FileDelete( target );
    };
    auto storedFileOf = []( Server & worker, AString & outStoredFile, uint64_t & outPchId )
    {
        MutexHolder mh( worker.m_PchCacheMutex );
        TEST_ASSERT( worker.m_PchCache.GetSize() >= 1 );
        outStoredFile = worker.m_PchCache[ worker.m_PchCache.GetSize() - 1 ].filePath;
        outPchId = worker.m_PchCache[ worker.m_PchCache.GetSize() - 1 ].pchId;
    };
    ClearWorkerPchStore();

    // A client and a worker of this version: the PCH goes over, and is kept, after the transform
    Array<char> first[ 3 ];
    Array<char> clientPch;
    AStackString<> storedFile;
    uint64_t pchId = 0;
    uint64_t transformedStoredSize = 0;
    {
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true; // The jobs need the PCH on the worker
        options.m_AllowLocalRace = false; // So that every object is built on the worker
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize() );
        cleanOutput();

        Server worker( 1 );
        SetPchCacheLimits( worker, ( 1ull << 30 ), 0, ( 1ull << 30 ) );
        TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
        TEST_ASSERT( fBuild.Build( target ) );
        TEST_ASSERT( fBuild.SaveDependencyGraph( dbFile.Get() ) );
        TEST_ASSERT( worker.m_NumPchChunksReceived.Load() >= 1 );
        for ( uint32_t i = 0; i < 3; ++i )
        {
            readObject( objects[ i ], first[ i ] );
            TEST_ASSERT( first[ i ].GetSize() > 0 );
        }

        storedFileOf( worker, storedFile, pchId );
        TEST_ASSERT( LoadFile( AStackString<>( "../tmp/Test/Distributed/PrecompiledHeader.pch" ), clientPch ) );
        TEST_ASSERT( PchId::Calc( clientPch.Begin(), clientPch.GetSize() ) == pchId );

        // What the worker keeps is the transformed blob, which is smaller, and is the PCH of the client when the transform is undone
        Array<char> stored;
        TEST_ASSERT( LoadFile( storedFile, stored ) );
        TEST_ASSERT( GetBlobType( stored ) == kTransformedBlobType );
        TEST_ASSERT( BlobDecodesTo( stored, clientPch ) );
        transformedStoredSize = stored.GetSize();

        // What the compiler was given is the PCH of the client, byte for byte
        AStackString<> rawFile;
        TEST_ASSERT( Server::GetRawPchPath( storedFile, rawFile ) );
        Array<char> raw;
        TEST_ASSERT( LoadFile( rawFile, raw ) );
        TEST_ASSERT( raw.GetSize() == clientPch.GetSize() );
        TEST_ASSERT( memcmp( raw.Begin(), clientPch.Begin(), raw.GetSize() ) == 0 );
    }

    // A worker that starts later finds it in the store, and is not sent the PCH again. The objects are the same.
    {
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true;
        options.m_AllowLocalRace = false;
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize( dbFile.Get() ) );
        deleteObjects();

        Server worker( 1 );
        SetPchCacheLimits( worker, ( 1ull << 30 ), 0, ( 1ull << 30 ) );
        TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
        TEST_ASSERT( worker.IsPchCached( pchId ) );
        TEST_ASSERT( fBuild.Build( target ) );
        TEST_ASSERT( worker.m_NumPchChunksReceived.Load() == 0 );
        TEST_ASSERT( worker.m_NumPchsUnpacked.Load() >= 1 ); // Made from the transformed store, for the jobs
        for ( uint32_t i = 0; i < 3; ++i )
        {
            Array<char> built;
            readObject( objects[ i ], built );
            TEST_ASSERT( IsSameObject( built, first[ i ] ) );
        }
    }

    // A client that does not advertise the capability sends the PCH as 24.0 does, which is kept as it came and is bigger. The objects are the same.
    ClearWorkerPchStore();
    {
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true;
        options.m_AllowLocalRace = false;
        options.m_DistributionCapabilities = ( Protocol::kCapabilities & ~Protocol::CAP_PCH_TRANSFORM );
        FBuildForTest fBuild( options );
        TEST_ASSERT( fBuild.Initialize( dbFile.Get() ) );
        deleteObjects();

        Server worker( 1 );
        SetPchCacheLimits( worker, ( 1ull << 30 ), 0, ( 1ull << 30 ) );
        TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
        TEST_ASSERT( worker.IsPchCached( pchId ) == false );
        TEST_ASSERT( fBuild.Build( target ) );
        TEST_ASSERT( worker.m_NumPchChunksReceived.Load() >= 1 );
        AStackString<> plainStoredFile;
        uint64_t plainPchId = 0;
        storedFileOf( worker, plainStoredFile, plainPchId );
        TEST_ASSERT( plainPchId == pchId );
        Array<char> stored;
        TEST_ASSERT( LoadFile( plainStoredFile, stored ) );
        TEST_ASSERT( GetBlobType( stored ) == Compressor::eZstd );
        TEST_ASSERT( DecodesToFile( plainStoredFile, clientPch ) );
        OUTPUT( "PchTransformedThroughTheBuild: stored %zu bytes, %" PRIu64 " with the transform (%.1f%%)\n", stored.GetSize(), transformedStoredSize, ( 100.0 * (double)transformedStoredSize ) / (double)stored.GetSize() );
        TEST_ASSERT( ( transformedStoredSize * 100 ) < ( stored.GetSize() * 95 ) );
        for ( uint32_t i = 0; i < 3; ++i )
        {
            Array<char> built;
            readObject( objects[ i ], built );
            TEST_ASSERT( IsSameObject( built, first[ i ] ) );
        }
    }
}

// PchTransformFallsBackThroughTheBuild
//------------------------------------------------------------------------------
void TestDistributed::PchTransformFallsBackThroughTheBuild() const
{
    const char * target( "../tmp/Test/Distributed/distpch.lib" );
    Array<AString> files;
    FileIO::GetFiles( AStackString( "../tmp/Test/Distributed" ), AStackString( "*.*" ), true, &files );
    for ( const AString & file : files )
    {
        FileIO::FileDelete( file.Get() );
    }
    ClearWorkerPchStore();

    // The transform makes what does not give the PCH back, so what is sent is the PCH as it is: the build is not at fault
    DamagedTransform damaged;
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true;
    options.m_AllowLocalRace = false;
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    Server worker( 1 );
    SetPchCacheLimits( worker, ( 1ull << 30 ), 0, ( 1ull << 30 ) );
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
    TEST_ASSERT( fBuild.Build( target ) );
    TEST_ASSERT( FileIO::FileExists( target ) );
    TEST_ASSERT( worker.m_NumPchChunksReceived.Load() >= 1 );
    TEST_ASSERT( worker.m_NumPchsUnpacked.Load() >= 1 );

    AStackString<> storedFile;
    {
        MutexHolder mh( worker.m_PchCacheMutex );
        TEST_ASSERT( worker.m_PchCache.GetSize() >= 1 );
        storedFile = worker.m_PchCache[ worker.m_PchCache.GetSize() - 1 ].filePath;
    }
    Array<char> clientPch;
    TEST_ASSERT( LoadFile( AStackString<>( "../tmp/Test/Distributed/PrecompiledHeader.pch" ), clientPch ) );
    Array<char> stored;
    TEST_ASSERT( LoadFile( storedFile, stored ) );
    TEST_ASSERT( GetBlobType( stored ) == Compressor::eZstd );
    TEST_ASSERT( DecodesToFile( storedFile, clientPch ) );
}

#endif // __WINDOWS__

// Workers that drop the connection during a PCH
//------------------------------------------------------------------------------
namespace
{
    // A worker that ends the connection once it has the chunk before the last of a PCH, as the client is about
    // to send the last. It takes no job.
    class PchDroppingWorker : public FakeWorker
    {
    public:
        PchDroppingWorker() : FakeWorker( 0 ) {}
        explicit PchDroppingWorker( const PchDroppingWorker & other ) = delete;
        PchDroppingWorker & operator=( const PchDroppingWorker & other ) = delete;

        uint32_t GetNumDrops() const { return m_NumDrops.Load(); }

    private:
        virtual void OnMessage( const ConnectionInfo * connection, const Protocol::IMessage & message, void *, uint32_t ) override
        {
            if ( message.GetType() != Protocol::MSG_PCH_FILE )
            {
                return;
            }
            const Protocol::MsgPchFile & pchFile = static_cast<const Protocol::MsgPchFile &>( message );
            const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( pchFile.GetCompressedSize(), pchFile.GetChunkSize() );
            if ( ( numChunks >= 3 ) && ( ( pchFile.GetChunkIndex() + 2 ) == numChunks ) )
            {
                m_NumDrops.Increment();
                Disconnect( connection );
            }
        }

        Atomic<uint32_t> m_NumDrops{ 0 };
    };
}

// ClientIsNotStuckWhenTheWorkerDropsDuringAPchSend
//------------------------------------------------------------------------------
void TestDistributed::ClientIsNotStuckWhenTheWorkerDropsDuringAPchSend() const
{
    // The PCH is sent in several chunks, and the worker ends the connection as the last is being sent. The client
    // connects again, and again. The connection has to be ended however far the send thread has come.
    FBuildTestOptions options;
    options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
    options.m_AllowDistributed = true;
    options.m_NumWorkerThreads = 1;
    options.m_NoLocalConsumptionOfRemoteJobs = true;
    options.m_AllowLocalRace = false;
    options.m_ForceCleanBuild = true;
    options.m_DistributionPchChunkSize = Protocol::kMinPchChunkSize;
    options.m_DistributionReconnectDelay = 0.05f;
    FBuildForTest fBuild( options );
    TEST_ASSERT( fBuild.Initialize() );

    PchDroppingWorker worker;
    TEST_ASSERT( worker.Listen( Protocol::kTestPort ) );
    const uint32_t kNumDrops = 40;
    auto dropped = [ &worker, kNumDrops ]() { return ( worker.GetNumDrops() >= kNumDrops ); };
    BuildAborter aborter( dropped, kWaitTimeoutSeconds );
    const bool built = fBuild.Build( "../tmp/Test/Distributed/distpch.lib" );
    worker.ShutdownAllConnections();
    TEST_ASSERT( built == false ); // Aborted
    TEST_ASSERT( aborter.HasReached() );
}

//------------------------------------------------------------------------------
