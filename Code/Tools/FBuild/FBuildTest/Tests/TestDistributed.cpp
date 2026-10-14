// TestDistributed.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildTest/Tests/FBuildTest.h"

#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/Graph/CompilerNode.h"
#include "Tools/FBuild/FBuildCore/Graph/ObjectNode.h"
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/DistributionStats.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchDataCache.h"
#include "Tools/FBuild/FBuildCore/Helpers/ToolManifest.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/Protocol/Server.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/Job.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/JobQueueRemote.h"

// Core
#include "Core/FileIO/FileIO.h"
#include "Core/Containers/UniquePtr.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Mem/Mem.h"
#include "Core/Network/TCPConnectionPool.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Thread.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Time.h"
#include "Core/Time/Timer.h"
#include "Core/Tracing/Tracing.h"

// Defines
//------------------------------------------------------------------------------
#if !defined( __has_feature )
    #define __has_feature( ... ) 0
#endif

// TestDistributed
//------------------------------------------------------------------------------
class TestDistributed : public FBuildTest
{
private:
    DECLARE_TESTS

    void TestWith1RemoteWorkerThread() const;
    void TestWith4RemoteWorkerThreads() const;
    void WithPCH() const;
    void WithPCHMacroUndefs() const;
    void PchCacheEvictionPolicy() const;
    void PchCompressedCache() const;
    void PchCompressRetry() const;
    void PchTakeReadyPchs() const;
    void DistributionStatsLog() const;
    void PayloadsAcceptedByWorker() const;
    void ToolchainCacheEvictionPolicy() const;
    void ToolchainCachedAcrossWorkerRestart() const;
    void ToolchainCorruptedFileIsResent() const;
    void ToolchainInventoryFollowsAck() const;
    void JobStreamDistributed() const;
    void MismatchedProtocolVersionIsRefused() const;
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

    // PchDataCache helpers
    static constexpr float kWaitTimeoutSeconds = 30.0f;
    struct CompressionRequest
    {
        PchDataCache *                  m_Cache = nullptr;
        const PchDataCache::Entry *     m_Entry = nullptr;
        Atomic<bool> *                  m_Go = nullptr;
        PchDataCache::CompressedState   m_State = PchDataCache::COMPRESSING;
        const void *                    m_Data = nullptr;
        size_t                          m_Size = 0;
    };
    static bool WriteTestFile( const char * path, uint32_t size, Array<char> & outContent );
    static PchDataCache::CompressedState WaitForCompression( PchDataCache & cache,
                                                             const PchDataCache::Entry & entry,
                                                             const void *& outData,
                                                             size_t & outSize );
    static bool DecodesTo( const void * data, size_t dataSize, const Array<char> & content );
    static uint32_t RequestCompression( void * param );
    void BuildOnNewWorker( uint32_t & outManifestsReceived,
                           uint32_t & outToolFilesReceived,
                           AString & outWorkerToolchainFile ) const;
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
    REGISTER_TEST( DistributionStatsLog )
    REGISTER_TEST( PayloadsAcceptedByWorker )
    REGISTER_TEST( ToolchainCacheEvictionPolicy )
    REGISTER_TEST( ToolchainCachedAcrossWorkerRestart )
    REGISTER_TEST( ToolchainCorruptedFileIsResent )
    REGISTER_TEST( ToolchainInventoryFollowsAck )
    REGISTER_TEST( JobStreamDistributed )
    REGISTER_TEST( MismatchedProtocolVersionIsRefused )
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
/*static*/ bool TestDistributed::WriteTestFile( const char * path, uint32_t size, Array<char> & outContent )
{
    // Compressible, with some variation
    outContent.SetSize( size );
    for ( uint32_t i = 0; i < size; ++i )
    {
        outContent[ i ] = (char)( ( i % 251 ) ^ ( ( i / 4096 ) & 0xFF ) );
    }

    if ( FileIO::EnsurePathExistsForFile( AStackString<>( path ) ) == false )
    {
        return false;
    }
    FileStream f;
    return f.Open( path, FileStream::WRITE_ONLY ) && ( f.Write( outContent.Begin(), size ) == size );
}

/*static*/ PchDataCache::CompressedState TestDistributed::WaitForCompression( PchDataCache & cache,
                                                                              const PchDataCache::Entry & entry,
                                                                              const void *& outData,
                                                                              size_t & outSize )
{
    const Timer timer;
    PchDataCache::CompressedState state = cache.GetCompressed( entry, outData, outSize );
    while ( ( state == PchDataCache::COMPRESSING ) && ( timer.GetElapsed() < kWaitTimeoutSeconds ) )
    {
        Thread::Sleep( 10 );
        state = cache.GetCompressed( entry, outData, outSize );
    }
    return state;
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

/*static*/ uint32_t TestDistributed::RequestCompression( void * param )
{
    CompressionRequest * request = static_cast<CompressionRequest *>( param );
    while ( request->m_Go->Load() == false )
    {
        Thread::Sleep( 0 );
    }
    request->m_State = WaitForCompression( *request->m_Cache, *request->m_Entry, request->m_Data, request->m_Size );
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
    entry.pchId = 0x1234;
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

    // One compression, one buffer shared by all of them
    TEST_ASSERT( cache.GetNumCompressions() == 1 );
    for ( const CompressionRequest & request : requests )
    {
        TEST_ASSERT( request.m_State == PchDataCache::READY );
        TEST_ASSERT( request.m_Data == requests[ 0 ].m_Data );
        TEST_ASSERT( request.m_Size == requests[ 0 ].m_Size );
    }
    TEST_ASSERT( requests[ 0 ].m_Size < fileSize );
    TEST_ASSERT( DecodesTo( requests[ 0 ].m_Data, requests[ 0 ].m_Size, content ) );
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

        PchDataCache::Entry entry;
        entry.pchId = 0x1;
        entry.uncompressedSize = fileSize;
        entry.filePath = path;
        cache.Store( entry );

        const void * data = nullptr;
        size_t size = 0;
        TEST_ASSERT( cache.GetCompressed( entry, data, size ) == PchDataCache::COMPRESSING );

        const Timer timer;
        while ( ( cache.GetFailedAttempts( entry.pchId ) == 0 ) && ( timer.GetElapsed() < kWaitTimeoutSeconds ) )
        {
            Thread::Sleep( 1 );
        }
        TEST_ASSERT( cache.GetFailedAttempts( entry.pchId ) == 1 );
        TEST_ASSERT( WriteTestFile( path.Get(), fileSize, content ) );

        TEST_ASSERT( WaitForCompression( cache, entry, data, size ) == PchDataCache::READY );
        TEST_ASSERT( DecodesTo( data, size, content ) );
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
        const void * data = nullptr;
        size_t size = 0;
        TEST_ASSERT( WaitForCompression( cache, entry, data, size ) == PchDataCache::FAILED );
        TEST_ASSERT( timer.GetElapsed() >= ( ( kMaxAttempts - 1 ) * kRetryDelayMS * 0.001f * 0.9f ) );
        TEST_ASSERT( cache.GetFailedAttempts( entry.pchId ) == kMaxAttempts );
        TEST_ASSERT( cache.HasCompressionFailed( entry.pchId ) );

        // Asking again neither retries nor starts another compression
        TEST_ASSERT( cache.GetCompressed( entry, data, size ) == PchDataCache::FAILED );
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
    const AStackString<> shortPath( "../tmp/Test/Distributed/PchTakeReadyPchs/short.pch" );
    const uint32_t fileSize = ( 1 * MEGABYTE );
    TEST_ASSERT( WriteTestFile( goodPath.Get(), fileSize, content ) );
    Array<char> shortContent;
    TEST_ASSERT( WriteTestFile( shortPath.Get(), 100, shortContent ) );

    const uint64_t kGood = 1;
    const uint64_t kUnreadable = 2;
    const uint64_t kUnknown = 3;
    const uint64_t kWorkerHasIt = 4;
    PchDataCache::Entry entry;
    entry.pchId = kGood;
    entry.uncompressedSize = fileSize;
    entry.filePath = goodPath;
    cache.Store( entry );
    entry.pchId = kUnreadable;
    entry.uncompressedSize = 1024; // The file is shorter
    entry.filePath = shortPath;
    cache.Store( entry );
    entry.pchId = kWorkerHasIt;
    entry.uncompressedSize = fileSize;
    entry.filePath = goodPath;
    cache.Store( entry );

    Array<uint64_t> pending;
    pending.Append( kGood );
    pending.Append( kUnreadable );
    pending.Append( kUnknown );
    pending.Append( kWorkerHasIt );
    Array<uint64_t> sent;
    sent.Append( kWorkerHasIt ); // Reported in the worker's inventory

    Array<PchDataCache::ReadyPch> ready;
    bool allOk = true;
    const Timer timer;
    while ( pending.IsEmpty() == false )
    {
        TEST_ASSERT( timer.GetElapsed() < kWaitTimeoutSeconds );
        if ( cache.TakeReadyPchs( pending, sent, ready ) == false )
        {
            allOk = false;
        }
        Thread::Sleep( 10 );
    }

    // The unreadable PCH is reported once, then dropped without being marked as sent
    TEST_ASSERT( allOk == false );
    TEST_ASSERT( sent.Find( kUnreadable ) == nullptr );

    // Unknown ids and ones the worker has are dropped without being sent
    TEST_ASSERT( sent.Find( kUnknown ) == nullptr );
    TEST_ASSERT( ready.GetSize() == 1 );

    // The readable one is ready once, with the right payload, and marked as sent
    TEST_ASSERT( ready[ 0 ].pchId == kGood );
    TEST_ASSERT( ready[ 0 ].uncompressedSize == fileSize );
    TEST_ASSERT( DecodesTo( ready[ 0 ].data, ready[ 0 ].dataSize, content ) );
    TEST_ASSERT( sent.Find( kGood ) != nullptr );
    TEST_ASSERT( sent.GetSize() == 2 );

    // Needed again by the same connection: not sent twice
    pending.Append( kGood );
    TEST_ASSERT( cache.TakeReadyPchs( pending, sent, ready ) );
    TEST_ASSERT( pending.IsEmpty() );
    TEST_ASSERT( ready.GetSize() == 1 );
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
        stats.m_ResultsReceived = 3;
        stats.m_ResultBytesReceived = ( 1 * (uint64_t)MEGABYTE );

        AStackString<> text;
        DistributionStats::Format( stats, "2026-01-02 03:04:05", text );
        TEST_ASSERT( text.Find( "[2026-01-02 03:04:05] worker 10.0.0.7, connected 12.5s" ) );
        TEST_ASSERT( text.Find( "toolchain: 18 msgs, 40.0 MB raw, 2.0 MB sent" ) );
        TEST_ASSERT( text.Find( "jobs:      3 msgs, 24.0 MB raw, 3.0 MB sent" ) );
        TEST_ASSERT( text.Find( "results:   3 received, 1.0 MB" ) );
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

    // The sender's payload for a PCH goes through the worker's receive path. The id is new
    // each run, so the worker has to decode it rather than find it cached from a previous run.
    PchDataCache cache;
    PchDataCache::Entry entry;
    entry.pchId = ( 0x7E57ULL << 48 ) | ( Time::GetCurrentFileTime() & 0xFFFFFFFFFFFFULL );
    entry.uncompressedSize = fileSize;
    entry.filePath = path;
    cache.Store( entry );
    const void * data = nullptr;
    size_t dataSize = 0;
    TEST_ASSERT( WaitForCompression( cache, entry, data, dataSize ) == PchDataCache::READY );

    Server worker( 1 );
    const Protocol::MsgPchFile msg( entry.pchId, fileSize );
    worker.Process( nullptr, &msg, data, dataSize );

    AStackString<> cachedPath;
    TEST_ASSERT( worker.FindCachedPch( entry.pchId, cachedPath ) );
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

// ToolchainCacheEvictionPolicy
//------------------------------------------------------------------------------
void TestDistributed::ToolchainCacheEvictionPolicy() const
{
    // Entries are deliberately not in age order, so the sort is exercised
    Array<Server::ToolchainEvictionEntry> entries;
    entries.Append( Server::ToolchainEvictionEntry{ 300, false } );
    entries.Append( Server::ToolchainEvictionEntry{ 100, false } );
    entries.Append( Server::ToolchainEvictionEntry{ 400, false } );
    entries.Append( Server::ToolchainEvictionEntry{ 200, false } );

    Array<size_t> evictions;

    // At the cap: nothing evicted
    Server::SelectToolchainEvictions( entries, 4, evictions );
    TEST_ASSERT( evictions.IsEmpty() );

    // Over the cap by two: the two oldest go, whatever their position
    Server::SelectToolchainEvictions( entries, 2, evictions );
    TEST_ASSERT( evictions.GetSize() == 2 );
    TEST_ASSERT( entries[ evictions[ 0 ] ].lastUsed == 100 );
    TEST_ASSERT( entries[ evictions[ 1 ] ].lastUsed == 200 );

    // The oldest is protected (in use, or just added): the next-oldest go instead
    entries[ 1 ].isProtected = true;
    Server::SelectToolchainEvictions( entries, 2, evictions );
    TEST_ASSERT( evictions.GetSize() == 2 );
    TEST_ASSERT( entries[ evictions[ 0 ] ].lastUsed == 200 );
    TEST_ASSERT( entries[ evictions[ 1 ] ].lastUsed == 300 );

    // Everything but one entry is protected: only that one can go, so the cap is exceeded
    entries[ 0 ].isProtected = true;
    entries[ 3 ].isProtected = true;
    Server::SelectToolchainEvictions( entries, 2, evictions );
    TEST_ASSERT( evictions.GetSize() == 1 );
    TEST_ASSERT( entries[ evictions[ 0 ] ].lastUsed == 400 );

    // Everything is protected: nothing can go
    entries[ 2 ].isProtected = true;
    Server::SelectToolchainEvictions( entries, 2, evictions );
    TEST_ASSERT( evictions.IsEmpty() );
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

    // Whether or not an earlier run left the toolchain behind, it is there afterwards
    BuildOnNewWorker( manifests, files, toolchainFile );

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

// JobStreamDistributed
//------------------------------------------------------------------------------
void TestDistributed::JobStreamDistributed() const
{
    const char * target( "../tmp/Test/Distributed/dist.lib" );

    // Each build has its own worker, so each connection starts a new stream
    for ( uint32_t build = 0; build < 2; ++build )
    {
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

        // The jobs were built by the worker, from data it decoded
        const uint64_t received = s.m_JobBytesReceived.Load();
        const uint64_t decoded = s.m_JobBytesDecoded.Load();
        TEST_ASSERT( decoded > 0 );
        OUTPUT( "JobStreamDistributed: build %u, jobs %u bytes received for %u bytes decoded\n", build, (uint32_t)received, (uint32_t)decoded );

        // and they were compressed against each other, not one by one
        TEST_ASSERT( received < ( decoded / 3 ) );
    }
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

        // Waits for a condition that depends on messages from the worker
        template <class FUNC>
        static bool WaitFor( const FUNC & condition )
        {
            const Timer timer;
            while ( condition() == false )
            {
                if ( timer.GetElapsed() > 5.0f )
                {
                    return false;
                }
                Thread::Sleep( 10 );
            }
            return true;
        }

    private:
        virtual void OnReceive( const ConnectionInfo *, void * data, uint32_t, bool & keepMemory ) override
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
            }

            FREE( (void *)m_CurrentMessage );
            FREE( payload );
            m_CurrentMessage = nullptr;
        }
        virtual void OnDisconnected( const ConnectionInfo * ) override { m_Disconnected.Store( true ); }

        mutable Mutex m_Mutex;
        Array<uint8_t> m_MessageTypes;
        Atomic<bool> m_Disconnected{ false };
        const Protocol::IMessage * m_CurrentMessage = nullptr;
    };
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

    TEST_ASSERT( HandshakeProbe::WaitFor( [ &probe ]() { return ( probe.GetNumMessages() >= 3 ); } ) );

    // The coordinator holds back its jobs until the last of these arrives
    TEST_ASSERT( probe.GetNumMessages() == 3 );
    TEST_ASSERT( probe.GetMessageType( 0 ) == Protocol::MSG_CONNECTION_ACK );
    TEST_ASSERT( probe.GetMessageType( 1 ) == Protocol::MSG_PCH_INVENTORY );
    TEST_ASSERT( probe.GetMessageType( 2 ) == Protocol::MSG_TOOLCHAIN_INVENTORY );

    probe.ShutdownAllConnections();
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
    const Protocol::MsgConnection msg;
    uint8_t bytes[ sizeof( msg ) ];
    memcpy( bytes, &msg, sizeof( msg ) );
    const size_t versionOffset = sizeof( Protocol::IMessage );
    TEST_ASSERT( bytes[ versionOffset ] == Protocol::kVersionMajor ); // Found the right byte
    bytes[ versionOffset ] = (uint8_t)( Protocol::kVersionMajor - 1 );
    TEST_ASSERT( probe.Send( ci, bytes, sizeof( bytes ) ) );

    // The worker drops the connection without acknowledging it
    TEST_ASSERT( HandshakeProbe::WaitFor( [ &probe ]() { return probe.IsDisconnected(); } ) );
    TEST_ASSERT( probe.GetNumMessages() == 0 );

    probe.ShutdownAllConnections();
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

    // Create thread that will abort build to simulate Ctrl+C or other external stop
    class Helper
    {
    public:
        static uint32_t AbortBuild( void * data )
        {
            // Wait until some distributed jobs are available
            const Timer t;
#if __has_feature( thread_sanitizer ) || defined( __SANITIZE_THREAD__ )
            // Code under ThreadSanitizer runs several time slower than normal, so we need a larger timeout.
            const float timeout = 30.0f;
#else
            const float timeout = 5.0f;
#endif
            while ( t.GetElapsed() < timeout )
            {
                if ( Job::GetTotalLocalDataMemoryUsage() != 0 )
                {
                    *static_cast<bool *>( data ) = true;
                    break;
                }
                Thread::Sleep( 1 );
            }

            // Abort the build
            FBuild::Get().AbortBuild();
            return 0;
        }
    };
    bool detectedDistributedJobs = false;
    Thread thread;
    thread.Start( Helper::AbortBuild, "TestDistributed", &detectedDistributedJobs );

    // Start build and check it was aborted
    TEST_ASSERT( fBuild.Build( "ShutdownMemoryLeak" ) == false );
    TEST_ASSERT( GetRecordedOutput().Find( "FBuild: Incomplete: ShutdownMemoryLeak" ) );

    thread.Join();

    TEST_ASSERT( detectedDistributedJobs );
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
