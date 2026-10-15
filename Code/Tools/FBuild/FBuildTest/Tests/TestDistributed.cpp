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
#include "Tools/FBuild/FBuildCore/Helpers/PchDataCache.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchId.h"
#include "Tools/FBuild/FBuildCore/Helpers/ToolManifest.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/Protocol/Server.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/Job.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/JobQueueRemote.h"

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
}

class TestDistributed : public FBuildTest
{
private:
    DECLARE_TESTS

    virtual void PreTest() const override;
    virtual void PostTest() const override;

    void TestWith1RemoteWorkerThread() const;
    void TestWith4RemoteWorkerThreads() const;
    void WithPCH() const;
    void WithPCHMacroUndefs() const;
    void PchCacheEvictionPolicy() const;
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
    void ServerSendsOnlyWhatTheClientAdvertises() const;
    void ClientRefusesAWorkerThatRefusesIt() const;
    void ClientRefusesAnAckOfAnotherMajor() const;
    void ClientRejectsAShortAck() const;
    void ClientRejectsAnAckWithAPayload() const;
    void ClientDoesNotWaitForInventoriesTheWorkerLacks() const;
    void ClientIgnoresCapabilitiesItDidNotAdvertise() const;
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
    void BuildPchLibraryUntilStopped( NoCapacityWorker & worker, int when ) const;

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
    REGISTER_TEST( PchCacheEvictionPolicy )
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
    REGISTER_TEST( ServerSendsOnlyWhatTheClientAdvertises )
    REGISTER_TEST( ClientRefusesAWorkerThatRefusesIt )
    REGISTER_TEST( ClientRefusesAnAckOfAnotherMajor )
    REGISTER_TEST( ClientRejectsAShortAck )
    REGISTER_TEST( ClientRejectsAnAckWithAPayload )
    REGISTER_TEST( ClientDoesNotWaitForInventoriesTheWorkerLacks )
    REGISTER_TEST( ClientIgnoresCapabilitiesItDidNotAdvertise )
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

// PchCacheEvictionPolicy
//------------------------------------------------------------------------------
void TestDistributed::PchCacheEvictionPolicy() const
{
    // Entries are deliberately not in age order, so the sort is exercised
    Array<Server::PchCacheEntry> entries;
    entries.Append( Server::PchCacheEntry{ 3, AString( "dummy3" ), 300 } );
    entries.Append( Server::PchCacheEntry{ 1, AString( "dummy1" ), 100 } );
    entries.Append( Server::PchCacheEntry{ 4, AString( "dummy4" ), 400 } );
    entries.Append( Server::PchCacheEntry{ 2, AString( "dummy2" ), 200 } );

    Array<uint64_t> inUse;
    Array<size_t> evictions;

    // Under or at the cap: nothing evicted
    Server::SelectPchEvictions( entries, 4, inUse, evictions );
    TEST_ASSERT( evictions.IsEmpty() );

    // Over the cap by two: the two oldest go, whatever their position
    Server::SelectPchEvictions( entries, 2, inUse, evictions );
    TEST_ASSERT( evictions.GetSize() == 2 );
    TEST_ASSERT( entries[ evictions[ 0 ] ].pchId == 1 );
    TEST_ASSERT( entries[ evictions[ 1 ] ].pchId == 2 );

    // The oldest is in use: it is skipped and the next-oldest go instead
    inUse.Append( 1 );
    Server::SelectPchEvictions( entries, 2, inUse, evictions );
    TEST_ASSERT( evictions.GetSize() == 2 );
    TEST_ASSERT( entries[ evictions[ 0 ] ].pchId == 2 );
    TEST_ASSERT( entries[ evictions[ 1 ] ].pchId == 3 );

    // Everything but the newest is in use: only what is free can go, so the cap is exceeded
    inUse.Append( 2 );
    inUse.Append( 3 );
    Server::SelectPchEvictions( entries, 2, inUse, evictions );
    TEST_ASSERT( evictions.GetSize() == 1 );
    TEST_ASSERT( entries[ evictions[ 0 ] ].pchId == 4 );

    // The newest is the only one free but is itself in use (just received): nothing to evict
    inUse.Append( 4 );
    Server::SelectPchEvictions( entries, 2, inUse, evictions );
    TEST_ASSERT( evictions.IsEmpty() );
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
}

// PchIdStreamsLikeOneShot
//------------------------------------------------------------------------------
void TestDistributed::PchIdStreamsLikeOneShot() const
{
    // The id of a file read in pieces is the id of its content, whatever the sizes of the file and the pieces
    const uint32_t kPiece = PchId::kReadChunkSize;
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
        Atomic<bool> *          m_Go = nullptr;
        PchDataCache::Entry     m_Entry;
        bool                    m_Ok = false;
    };

    uint32_t RecordInThread( void * param )
    {
        RecordRequest * request = static_cast<RecordRequest *>( param );
        while ( request->m_Go->Load() == false )
        {
            Thread::Sleep( 0 );
        }
        request->m_Ok = request->m_Cache->Record( *request->m_Path, request->m_Entry );
        return 0;
    }
}

void TestDistributed::PchRecordReadsTheFileOnce() const
{
    PchDataCache cache;
    Array<char> content;
    const AStackString<> path( "../tmp/Test/Distributed/PchRecordReadsTheFileOnce/test.pch" );
    const uint32_t fileSize = ( 32 * MEGABYTE ); // Takes long enough to hash for the requests to overlap
    TEST_ASSERT( WriteTestFile( path.Get(), fileSize, content ) );
    const uint64_t expectedId = PchId::Calc( content.Begin(), fileSize );

    // Several jobs need the PCH at once
    const uint32_t before = PchId::GetNumFilesHashed();
    const uint32_t kNumRequesters = 8;
    Atomic<bool> go( false );
    RecordRequest requests[ kNumRequesters ];
    Thread threads[ kNumRequesters ];
    for ( uint32_t i = 0; i < kNumRequesters; ++i )
    {
        requests[ i ].m_Cache = &cache;
        requests[ i ].m_Path = &path;
        requests[ i ].m_Go = &go;
        threads[ i ].Start( RecordInThread, "PchRecord", &requests[ i ] );
    }
    go.Store( true );
    for ( uint32_t i = 0; i < kNumRequesters; ++i )
    {
        threads[ i ].Join();
    }

    // One read of the file, and all of them have what it gave
    TEST_ASSERT( PchId::GetNumFilesHashed() == ( before + 1 ) );
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
    TEST_ASSERT( PchId::GetNumFilesHashed() == ( before + 1 ) );

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
    const uint32_t kPiece = PchId::kReadChunkSize;
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
    AStackString<> dir( "../tmp/Test/Distributed/PchCompressedFilesAreCleanedUp/" );
    Array<AString> old;
    FileIO::GetFiles( dir, AStackString( "*" ), false, &old );
    for ( const AString & file : old )
    {
        FileIO::FileDelete( file.Get() );
    }

    const uint32_t thisProcess = Process::GetCurrentId();
    const uint32_t otherProcess = ( thisProcess + 1 );
    auto makeFile = [ & ]( const char * name, AString & outFile )
    {
        outFile = dir;
        outFile += name;
        FileStream f;
        TEST_ASSERT( FileIO::EnsurePathExistsForFile( outFile ) );
        TEST_ASSERT( f.Open( outFile.Get(), FileStream::WRITE_ONLY ) );
        const char data[] = "data";
        TEST_ASSERT( f.Write( data, sizeof( data ) ) == sizeof( data ) );
    };
    AStackString<> staleName;
    staleName.Format( "%s%u.1.zst", PchDataCache::kTempFilePrefix, otherProcess );
    AStackString<> freshName;
    freshName.Format( "%s%u.2.zst", PchDataCache::kTempFilePrefix, otherProcess );
    AStackString<> ownName;
    ownName.Format( "%s%u.3.zst", PchDataCache::kTempFilePrefix, thisProcess );
    AStackString<> stale;
    AStackString<> fresh;
    AStackString<> own;
    AStackString<> unrelated;
    makeFile( staleName.Get(), stale );
    makeFile( freshName.Get(), fresh );
    makeFile( ownName.Get(), own );
    makeFile( "notes.txt", unrelated );

    // Old enough to be left by a process that is gone: the other process's, and our own as well
#if defined( __WINDOWS__ )
    const uint64_t kDay = ( 24ULL * 3600ULL * 10000000ULL );
#else
    const uint64_t kDay = ( 24ULL * 3600ULL * 1000000000ULL );
#endif
    const uint64_t longAgo = ( Time::GetCurrentFileTime() - kDay );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( stale, longAgo ) );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( own, longAgo ) );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( unrelated, longAgo ) );

    // What another process may still be writing, what this one is writing, and what is not ours stay
    PchDataCache::RemoveStaleTempFiles( dir );
    TEST_ASSERT( FileIO::FileExists( stale.Get() ) == false );
    TEST_ASSERT( FileIO::FileExists( fresh.Get() ) );
    TEST_ASSERT( FileIO::FileExists( own.Get() ) );
    TEST_ASSERT( FileIO::FileExists( unrelated.Get() ) );

    // A compressed PCH is a file here until the cache is cleared
    PchDataCache cache;
    cache.SetTempDir( dir );
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
    TEST_ASSERT( compressed.Find( "PchCompressedFilesAreCleanedUp" ) ); // Where it was told to
    TEST_ASSERT( compressed.Find( PchDataCache::kTempFilePrefix ) );
    TEST_ASSERT( FileIO::FileExists( compressed.Get() ) );
    TEST_ASSERT( DecodesToFile( compressed, content ) );
    cache.Clear();
    TEST_ASSERT( FileIO::FileExists( compressed.Get() ) == false );
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
    pchFile.AppendFormat( "pch.%016" PRIx64 "%cpch.pch", kPchId, (char)NATIVE_SLASH );
    AStackString<> dir;
    dir.Assign( pchFile.Get(), pchFile.FindLast( NATIVE_SLASH ) + 1 );
    Array<AString> old;
    FileIO::GetFiles( dir, AStackString( "*" ), true, &old );
    for ( const AString & file : old )
    {
        FileIO::FileDelete( file.Get() );
    }

    const uint32_t thisProcess = Process::GetCurrentId();
    const uint32_t otherProcess = ( thisProcess + 1 );
    auto makeFile = [ & ]( const char * name, AString & outFile )
    {
        outFile = dir;
        outFile += name;
        FileStream f;
        TEST_ASSERT( FileIO::EnsurePathExistsForFile( outFile ) );
        TEST_ASSERT( f.Open( outFile.Get(), FileStream::WRITE_ONLY ) );
        const char data[] = "data";
        TEST_ASSERT( f.Write( data, sizeof( data ) ) == sizeof( data ) );
    };
    AStackString<> staleName;
    staleName.Format( "incoming.%u.1.pch", otherProcess );
    AStackString<> freshName;
    freshName.Format( "incoming.%u.2.pch", otherProcess );
    AStackString<> ownName;
    ownName.Format( "incoming.%u.3.pch", thisProcess );
    AStackString<> pch;
    AStackString<> stale;
    AStackString<> fresh;
    AStackString<> own;
    AStackString<> unrelated;
    makeFile( "pch.pch", pch );
    makeFile( staleName.Get(), stale );
    makeFile( freshName.Get(), fresh );
    makeFile( ownName.Get(), own );
    makeFile( "notes.txt", unrelated );

    // Old enough to be left by a process that is gone: the other process's, and our own as well
#if defined( __WINDOWS__ )
    const uint64_t kDay = ( 24ULL * 3600ULL * 10000000ULL );
#else
    const uint64_t kDay = ( 24ULL * 3600ULL * 1000000000ULL );
#endif
    const uint64_t longAgo = ( Time::GetCurrentFileTime() - kDay );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( stale, longAgo ) );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( own, longAgo ) );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( unrelated, longAgo ) );

    Server s( 1 );

    // The PCH is registered, a leftover of another process is removed, and so is nothing that
    // another process may still be writing, that is being written by this one, or that is not ours
    AStackString<> cachedPath;
    TEST_ASSERT( s.FindCachedPch( kPchId, cachedPath ) );
    TEST_ASSERT( FileIO::FileExists( pch.Get() ) );
    TEST_ASSERT( FileIO::FileExists( stale.Get() ) == false );
    TEST_ASSERT( FileIO::FileExists( fresh.Get() ) );
    TEST_ASSERT( FileIO::FileExists( own.Get() ) );
    TEST_ASSERT( FileIO::FileExists( unrelated.Get() ) );
}

// PchEvictionRemovesTheDirectory
//------------------------------------------------------------------------------
void TestDistributed::PchEvictionRemovesTheDirectory() const
{
    // Two more PCHs than a worker keeps, the two oldest first
    AStackString<> root;
    VERIFY( FBuild::GetWorkerCacheDir( root ) );
    const uint32_t kNumPchs = ( 20 + 2 ); // The most a worker keeps is 20
    Array<Server::PchCacheEntry> entries;
    Array<AString> dirs;
    for ( uint32_t i = 0; i < kNumPchs; ++i )
    {
        AStackString<> dir;
        dir.Format( "%spch.%016" PRIx64 "%c", root.Get(), (uint64_t)( 0x8000 + i ), (char)NATIVE_SLASH );
        AStackString<> file( dir );
        file += "pch.pch";
        TEST_ASSERT( FileIO::EnsurePathExistsForFile( file ) );
        FileStream f;
        TEST_ASSERT( f.Open( file.Get(), FileStream::WRITE_ONLY ) );
        const char data[] = "data";
        TEST_ASSERT( f.Write( data, sizeof( data ) ) == sizeof( data ) );

        Server::PchCacheEntry entry;
        entry.pchId = ( 0x8000 + i );
        entry.filePath = file;
        entry.lastWriteTime = ( 1000 + i );
        entries.Append( entry );
        dirs.Append( dir );
    }

    Server::EvictPchs( entries, Array<uint64_t>() );

    // The oldest are gone, with their directories, and the rest are as they were
    TEST_ASSERT( entries.GetSize() == 20 );
    for ( uint32_t i = 0; i < kNumPchs; ++i )
    {
        const bool evicted = ( i < 2 );
        AStackString<> file( dirs[ i ] );
        file += "pch.pch";
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
    receivingName.Format( "incoming.%u.1.pch", Process::GetCurrentId() + 1 );
    makeDir( 0x9001, nullptr, emptyDir );
    makeDir( 0x9002, "pch.pch", pchDir );
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
    {
        FileStream f;
        TEST_ASSERT( f.Open( cachedPath.Get(), FileStream::READ_ONLY ) );
        TEST_ASSERT( f.GetFileSize() == fileSize );
        Array<char> onWorker;
        onWorker.SetSize( fileSize );
        TEST_ASSERT( f.Read( onWorker.Begin(), fileSize ) == fileSize );
        TEST_ASSERT( memcmp( onWorker.Begin(), content.Begin(), fileSize ) == 0 );
    }
    FileIO::FileDelete( cachedPath.Get() );
    FileIO::DirectoryDelete( AStackString<>( cachedPath.Get(), cachedPath.FindLast( NATIVE_SLASH ) ) );

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
    TEST_ASSERT( f.GetFileSize() == content.GetSize() );
    Array<char> onWorker;
    onWorker.SetSize( content.GetSize() );
    TEST_ASSERT( f.Read( onWorker.Begin(), onWorker.GetSize() ) == onWorker.GetSize() );
    TEST_ASSERT( memcmp( onWorker.Begin(), content.Begin(), content.GetSize() ) == 0 );
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
        TEST_ASSERT( file.EndsWith( ".zst" ) == false );
        if ( file.EndsWith( ".pch" ) )
        {
            TEST_ASSERT( file.EndsWith( "pch.pch" ) );
            ++numPchs;
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
        explicit NoCapacityWorker( const NoCapacityWorker & other ) = delete;
        NoCapacityWorker & operator=( const NoCapacityWorker & other ) = delete;

        uint32_t GetNumManifests() const { return m_NumManifests.Load(); }
        uint32_t GetNumFiles() const { return m_NumFiles.Load(); }
        uint32_t GetNumPchFiles() const { return m_NumPchFiles.Load(); }
        uint32_t GetNumJobs() const { return m_NumJobs.Load(); }
        uint64_t GetLastToolId() const { return m_LastToolId.Load(); }

    private:
        virtual void OnMessage( const ConnectionInfo *, const Protocol::IMessage & message, void *, uint32_t ) override
        {
            switch ( message.GetType() )
            {
                case Protocol::MSG_MANIFEST:
                    m_LastToolId.Store( static_cast<const Protocol::MsgManifest &>( message ).GetToolId() );
                    m_NumManifests.Increment();
                    break;
                case Protocol::MSG_FILE: m_NumFiles.Increment(); break;
                case Protocol::MSG_PCH_FILE: m_NumPchFiles.Increment(); break;
                case Protocol::MSG_JOB: m_NumJobs.Increment(); break;
                default: break;
            }
        }

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
void TestDistributed::BuildPchLibraryUntilStopped( NoCapacityWorker & worker, int when ) const
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

//------------------------------------------------------------------------------
