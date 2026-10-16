// TestBuildAndLinkLibrary.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

#include "Tools/FBuild/FBuildCore/BFF/BFFParser.h"
#include "Tools/FBuild/FBuildCore/BFF/Functions/Function.h"
#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Helpers/BuildProfiler.h"
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchTransform.h"

// Core
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Process/Process.h"
#include "Core/Strings/AStackString.h"
#include "Core/Tracing/Tracing.h"

#include <string.h>

// Static Data
//------------------------------------------------------------------------------
/*static*/ bool FBuildTest::s_DebuggerAttached( false );
/*static*/ Mutex FBuildTest::s_OutputMutex;
/*static*/ AString FBuildTest::s_RecordedOutput( 1024 * 1024 );

// CONSTRUCTOR (FBuildTest)
//------------------------------------------------------------------------------
FBuildTest::FBuildTest()
{
    s_DebuggerAttached = IsDebuggerAttached();
    m_OriginalWorkingDir.SetReserved( 512 );
}

// PreTest
//------------------------------------------------------------------------------
/*virtual*/ void FBuildTest::PreTest() const
{
    Tracing::AddCallbackOutput( LoggingCallback );
    s_RecordedOutput.Clear();

    FBuildStats::SetIgnoreCompilerNodeDeps( true );

    // Store current working
    VERIFY( FileIO::GetCurrentDir( m_OriginalWorkingDir ) );

    // Set the WorkingDir to be the source code "Code" dir
    AStackString codeDir;
    GetCodeDir( codeDir );
    VERIFY( FileIO::SetCurrentDir( codeDir ) );
}

// PostTest
//------------------------------------------------------------------------------
/*virtual*/ void FBuildTest::PostTest() const
{
    VERIFY( FileIO::SetCurrentDir( m_OriginalWorkingDir ) );

    FBuildStats::SetIgnoreCompilerNodeDeps( false );

    Tracing::RemoveCallbackOutput( LoggingCallback );
}

// EnsureFileDoesNotExist
//------------------------------------------------------------------------------
void FBuildTest::EnsureFileDoesNotExist( const char * fileName ) const
{
    FileIO::FileDelete( fileName );
    TEST_ASSERT( FileIO::FileExists( fileName ) == false );
}

// EnsureFileExists
//------------------------------------------------------------------------------
void FBuildTest::EnsureFileExists( const char * fileName ) const
{
    TEST_ASSERT( FileIO::FileExists( fileName ) );
}

// EnsureDirDoesNotExist
//------------------------------------------------------------------------------
void FBuildTest::EnsureDirDoesNotExist( const char * dirPath ) const
{
    FileIO::DirectoryDelete( AStackString( dirPath ) );
    TEST_ASSERT( FileIO::DirectoryExists( AStackString( dirPath ) ) == false );
}

// EnsureDirExists
//------------------------------------------------------------------------------
void FBuildTest::EnsureDirExists( const char * dirPath ) const
{
    TEST_ASSERT( FileIO::EnsurePathExists( AStackString( dirPath ) ) );
}

// LoadFileContentsAsString
//------------------------------------------------------------------------------
void FBuildTest::LoadFileContentsAsString( const char * fileName, AString & outString ) const
{
    FileStream f;
    TEST_ASSERT( f.Open( fileName ) );
    const uint32_t fileSize = (uint32_t)f.GetFileSize();
    outString.SetLength( fileSize );
    TEST_ASSERT( f.ReadBuffer( outString.Get(), fileSize ) );
}

// MakeFile
//------------------------------------------------------------------------------
void FBuildTest::MakeFile( const char * fileName, const char * fileContents ) const
{
    FileStream f;
    TEST_ASSERT( f.Open( fileName, FileStream::WRITE_ONLY ) );
    const size_t len = AString::StrLen( fileContents );
    TEST_ASSERT( f.WriteBuffer( fileContents, len ) == len );
}

// Parse
//------------------------------------------------------------------------------
void FBuildTest::Parse( const char * fileName, bool expectFailure ) const
{
    FBuild fBuild;
    NodeGraph ng;
    BFFParser p( ng );
    const bool parseResult = p.ParseFromFile( fileName );
    if ( expectFailure )
    {
        TEST_ASSERT( parseResult == false ); // Make sure it failed as expected
    }
    else
    {
        TEST_ASSERT( parseResult == true );
    }
}

// ParseFromString
//------------------------------------------------------------------------------
bool FBuildTest::ParseFromString( bool expectedResult,
                                  const char * bffContents,
                                  const char * expectedMessage,
                                  const char * unexpectedMessage ) const
{
    // Note size of output so we can check if message was part of this invocation
    const size_t outputSizeBefore = GetRecordedOutput().GetLength();
    const char * searchStart = GetRecordedOutput().Get() + outputSizeBefore;

    // Parse
    bool result;
    {
        FBuild fBuild;
        NodeGraph ng;
        BFFParser p( ng );
        result = p.ParseFromString( "test.bff", bffContents );
    }

    // Check result is as expected
    if ( result != expectedResult )
    {
        // Emit message about mismatch
        OUTPUT( "Test %s but %s was expected\n",
                result ? "succeeded" : "failed",
                expectedResult ? "success" : "failure" );
        return false; // break in calling code
    }

    // Check message was present if needed
    if ( expectedMessage )
    {
        // Search for expected message in output
        const bool foundExpectedMessage = ( GetRecordedOutput().Find( expectedMessage, searchStart ) != nullptr );
        if ( foundExpectedMessage == false )
        {
            OUTPUT( "Expected %s was not found: %s\n", expectedResult ? "message" : "error", expectedMessage );
            return false;
        }
    }

    // Check message was present if unexpected
    if ( unexpectedMessage )
    {
        // Search for unexpected message in output
        const bool foundUnexpectedMessage = ( GetRecordedOutput().Find( unexpectedMessage, searchStart ) != nullptr );
        if ( foundUnexpectedMessage )
        {
            OUTPUT( "Unexpected %s was found: %s\n", expectedResult ? "message" : "error", unexpectedMessage );
            return false;
        }
    }

    return true;
}

// CheckStatsNode
//------------------------------------------------------------------------------
void FBuildTest::CheckStatsNode( const FBuildStats & stats, size_t numSeen, size_t numBuilt, Node::Type nodeType ) const
{
    const FBuildStats::Stats & nodeStats = stats.GetStatsFor( nodeType );

    const uint32_t actualNumSeen = nodeStats.m_NumProcessed;
    const uint32_t actualNumBuilt = nodeStats.m_NumBuilt;

    const bool nodeStatsOk = ( actualNumSeen == numSeen ) && ( actualNumBuilt == numBuilt );
    if ( !nodeStatsOk )
    {
        if ( actualNumSeen != numSeen )
        {
            OUTPUT( "PROBLEM for %s: Saw %u instead of %u\n", Node::GetTypeName( nodeType ), actualNumSeen, (uint32_t)numSeen );
        }
        if ( actualNumBuilt != numBuilt )
        {
            OUTPUT( "PROBLEM for %s: Built %u instead of %u\n", Node::GetTypeName( nodeType ), actualNumBuilt, (uint32_t)numBuilt );
        }
        TEST_ASSERT( false );
    }
}

// CheckStatsTotal
//------------------------------------------------------------------------------
void FBuildTest::CheckStatsTotal( const FBuildStats & stats, size_t numSeen, size_t numBuilt ) const
{
    const size_t actualNumSeen = stats.GetNodesProcessed();
    TEST_ASSERT( actualNumSeen == numSeen );

    const size_t actualNumBuilt = stats.GetNodesBuilt();
    TEST_ASSERT( actualNumBuilt == numBuilt );
}

// CheckStatsNode
//------------------------------------------------------------------------------
void FBuildTest::CheckStatsNode( size_t numSeen, size_t numBuilt, Node::Type nodeType ) const
{
    const FBuildStats & stats = FBuild::Get().GetStats();
    CheckStatsNode( stats, numSeen, numBuilt, nodeType );
}

// CheckStatsTotal
//------------------------------------------------------------------------------
void FBuildTest::CheckStatsTotal( size_t numSeen, size_t numBuilt ) const
{
    const FBuildStats & stats = FBuild::Get().GetStats();
    CheckStatsTotal( stats, numSeen, numBuilt );
}

// CountFilesIn
//------------------------------------------------------------------------------
/*static*/ uint32_t FBuildTest::CountFilesIn( const AString & dir )
{
    Array<AString> files;
    FileIO::GetFiles( dir, AStackString( "*" ), true, &files );
    return (uint32_t)files.GetSize();
}

// MakeRandomBytes
//------------------------------------------------------------------------------
/*static*/ void FBuildTest::MakeRandomBytes( uint32_t size, uint64_t seed, Array<char> & outBytes )
{
    outBytes.SetSize( size );
    uint64_t x = ( seed | 1 );
    for ( char & c : outBytes )
    {
        x ^= ( x << 13 );
        x ^= ( x >> 7 );
        x ^= ( x << 17 );
        c = (char)( x >> 24 );
    }
}

// MakePchLikeBytes
//------------------------------------------------------------------------------
/*static*/ void FBuildTest::MakePchLikeBytes( uint32_t size, uint64_t seed, Array<char> & outBytes )
{
    outBytes.SetSize( size );
    uint64_t x = ( seed | 1 );
    auto next = [ &x ]()
    {
        x ^= ( x << 13 );
        x ^= ( x >> 7 );
        x ^= ( x << 17 );
        return x;
    };
    auto geometric = [ &next ]( uint32_t limit ) // 0 half of the time, 1 a quarter...
    {
        uint32_t k = 0;
        while ( ( k + 1 ) < limit && ( ( next() & 1 ) == 0 ) )
        {
            ++k;
        }
        return k;
    };

    // Records of words: a number, or a pointer to an object allocated now, a recent one, a hot one, or one near a recent one
    struct Slot { bool m_IsPointer; uint32_t m_Value; };
    static const Slot template0[] = { { false, 1 }, { true, 0 }, { false, 8 }, { true, 1 }, { true, 2 }, { false, 0 }, { false, 24 }, { true, 0 } };
    static const Slot template1[] = { { false, 3 }, { true, 2 }, { true, 1 }, { false, 0 } };
    static const Slot template2[] = { { false, 16 }, { true, 3 }, { false, 1 }, { true, 0 }, { false, 0 }, { false, 2 } };
    static const Slot * const templates[] = { template0, template1, template2 };
    static const uint32_t templateSizes[] = { 8, 4, 6 };

    uint32_t hot[ 8 ];
    uint32_t recent[ 16 ];
    for ( uint32_t i = 0; i < 8; ++i )
    {
        hot[ i ] = ( 0x10000 + ( 0x100 * i ) );
    }
    for ( uint32_t i = 0; i < 16; ++i )
    {
        recent[ i ] = ( 0x20000 + ( 8 * i ) );
    }
    uint32_t cursor = 0x100000;

    uint32_t numWords = ( size / 8 );
    uint32_t word = 0;
    while ( word < numWords )
    {
        const uint32_t t = geometric( 3 );
        for ( uint32_t s = 0; ( s < templateSizes[ t ] ) && ( word < numWords ); ++s, ++word )
        {
            const Slot & slot = templates[ t ][ s ];
            uint64_t value = slot.m_Value;
            if ( slot.m_IsPointer )
            {
                uint32_t offset;
                switch ( slot.m_Value )
                {
                    case 0:     cursor += ( 8 * ( 1 + (uint32_t)( next() % 8 ) ) ); offset = cursor; break;
                    case 1:     offset = recent[ geometric( 16 ) ]; break;
                    case 2:     offset = hot[ next() % 8 ]; break;
                    default:    offset = ( recent[ geometric( 16 ) ] + ( 8 * (uint32_t)( next() % 16 ) ) ); break;
                }
                memmove( recent + 1, recent, 15 * sizeof( uint32_t ) );
                recent[ 0 ] = offset;
                value = ( ( (uint64_t)kPchLikeBase << 32 ) | offset );
            }
            for ( uint32_t b = 0; b < 8; ++b )
            {
                outBytes[ ( word * 8 ) + b ] = (char)( value >> ( 8 * b ) );
            }
        }
    }
    for ( uint32_t i = ( numWords * 8 ); i < size; ++i )
    {
        outBytes[ i ] = (char)next();
    }
}

// GetBlobWindowLog
//------------------------------------------------------------------------------
/*static*/ uint32_t FBuildTest::GetBlobWindowLog( const Array<char> & blob )
{
    // After the header of the blob: the magic number of a frame, then its descriptor and the byte of the window
    const size_t frame = Compressor::kHeaderSize;
    TEST_ASSERT( blob.GetSize() > ( frame + 6 ) );
    const uint8_t * bytes = reinterpret_cast<const uint8_t *>( blob.Begin() + frame );
    TEST_ASSERT( ( bytes[ 0 ] == 0x28 ) && ( bytes[ 1 ] == 0xB5 ) && ( bytes[ 2 ] == 0x2F ) && ( bytes[ 3 ] == 0xFD ) );
    const bool singleSegment = ( ( bytes[ 4 ] & 0x20 ) != 0 );
    return singleSegment ? 0 : ( (uint32_t)( bytes[ 5 ] >> 3 ) + 10 );
}

// MakePchBlob
//------------------------------------------------------------------------------
/*static*/ bool FBuildTest::MakePchBlob( const Array<char> & pch, uint32_t transformBase, Array<char> & outBlob, bool * outTransformed )
{
    static uint32_t s_Count = 0;
    AStackString<> source;
    source.Format( "../tmp/Test/PchBlobs/pch-%u.bin", ++s_Count );
    AStackString<> blob( source );
    blob += ".zst";
    {
        FileStream f;
        if ( ( FileIO::EnsurePathExistsForFile( source ) == false ) ||
             ( f.Open( source.Get(), FileStream::WRITE_ONLY ) == false ) ||
             ( f.Write( pch.Begin(), pch.GetSize() ) != pch.GetSize() ) )
        {
            return false;
        }
    }

    struct NoObserver : public Compressor::FileObserver
    {
        virtual void OnPass() override {}
        virtual void OnPiece( const void *, size_t ) override {}
    } observer;
    uint64_t blobSize = 0;
    bool transformed = false;
    PchTransform::Encoder encoder( transformBase );
    bool ok = Compressor::CompressZstdFile( source.Get(), pch.GetSize(), blob.Get(), Compressor::kZstdPchLevel, nullptr, observer, blobSize,
                                            ( transformBase != 0 ) ? &encoder : nullptr, &transformed, Compressor::kZstdPchWindowLog );
    FileStream f;
    ok = ok && f.Open( blob.Get(), FileStream::READ_ONLY ) && ( f.GetFileSize() == blobSize );
    if ( ok )
    {
        outBlob.SetSize( (size_t)blobSize );
        ok = ( f.Read( outBlob.Begin(), outBlob.GetSize() ) == outBlob.GetSize() );
    }
    f.Close();
    FileIO::FileDelete( source.Get() );
    FileIO::FileDelete( blob.Get() );
    if ( outTransformed )
    {
        *outTransformed = transformed;
    }
    return ok;
}

// GetIdOfNoProcess
//------------------------------------------------------------------------------
/*static*/ uint32_t FBuildTest::GetIdOfNoProcess()
{
    // Far above what ids come to
    for ( uint32_t id = 0x7FFFFFF0; id > 0x7F000000; id -= 4 )
    {
        if ( Process::IsProcessRunning( id ) == false )
        {
            return id;
        }
    }
    return 0;
}

// GetIdOfAnotherProcess
//------------------------------------------------------------------------------
/*static*/ uint32_t FBuildTest::GetIdOfAnotherProcess()
{
    // The System process, and init: always there, and not ours to look at
#if defined( __WINDOWS__ )
    return 4;
#else
    return 1;
#endif
}

// GetCodeDir
//------------------------------------------------------------------------------
/*static*/ void FBuildTest::GetCodeDir( AString & codeDir )
{
    // we want the working dir to be the 'Code' directory
    TEST_ASSERT( FileIO::GetCurrentDir( codeDir ) );
    if ( !codeDir.EndsWith( NATIVE_SLASH ) )
    {
        codeDir += NATIVE_SLASH;
    }
#if defined( __WINDOWS__ )
    const char * codePos = codeDir.FindLastI( "\\code\\" );
#else
    const char * codePos = codeDir.FindLastI( "/code/" );
#endif
    if ( codePos )
    {
        codeDir.SetLength( (uint16_t)( codePos - codeDir.Get() + 6 ) );
        return;
    }

    // If not already set to code dir, try to discover it automatically
    // relative to executable compilation location
#if defined( __WINDOWS__ )
    const char * const tmpPos = codeDir.FindLastI( "\\tmp\\" );
#else
    const char * const tmpPos = codeDir.FindLastI( "/tmp/" );
#endif
    if ( tmpPos )
    {
        // Truncate before "tmp", but include slash
        codeDir.SetLength( (uint16_t)( tmpPos - codeDir.Get() + 1 ) );
        codeDir.AppendFormat( "Code%c", NATIVE_SLASH );
        return;
    }
    TEST_ASSERT( false && "Failed to determine 'Code' folder location" );
}

// LoggingCallback
//------------------------------------------------------------------------------
bool FBuildTest::LoggingCallback( const char * message )
{
    MutexHolder mh( s_OutputMutex );
    s_RecordedOutput.Append( message, AString::StrLen( message ) );

    return true; // Caller should also print to stdout
}

// CONSTRUCTOR - FBuildTestOptions
//------------------------------------------------------------------------------
FBuildTestOptions::FBuildTestOptions()
{
    // Override defaults
    m_ShowSummary = true; // required to generate stats for node count checks
    m_Profile = true; // Ensure "-profile" option is exercised
    m_EnableMonitor = true; // Make sure monitor code paths are tested

    // Ensure any distributed compilation tests use the test port
    m_DistributionPort = Protocol::kTestPort;
}

// GetRecursiveDependencyCount
//------------------------------------------------------------------------------
size_t FBuildForTest::GetRecursiveDependencyCount( const Node * node ) const
{
    size_t count = 0;
    const Dependencies * depLists[ 3 ] = { &node->GetPreBuildDependencies(),
                                           &node->GetStaticDependencies(),
                                           &node->GetDynamicDependencies() };
    for ( const Dependencies * depList : depLists )
    {
        for ( const Dependency & dep : *depList )
        {
            count += GetRecursiveDependencyCount( dep.GetNode() );
        }
        count += depList->GetSize();
    }
    return count;
}

// GetRecursiveDependencyCount
//------------------------------------------------------------------------------
size_t FBuildForTest::GetRecursiveDependencyCount( const char * nodeName ) const
{
    const Node * node = m_DependencyGraph->FindNode( AStackString( nodeName ) );
    TEST_ASSERT( node );
    return GetRecursiveDependencyCount( node );
}

// GetNodesOfType
//------------------------------------------------------------------------------
void FBuildForTest::GetNodesOfType( Node::Type type, Array<const Node *> & outNodes ) const
{
    const size_t numNodes = m_DependencyGraph->GetNodeCount();
    for ( size_t i = 0; i < numNodes; ++i )
    {
        const Node * node = m_DependencyGraph->GetNodeByIndex( i );
        if ( node->GetType() == type )
        {
            outNodes.Append( node );
        }
    }
}

// GetNode
//------------------------------------------------------------------------------
const Node * FBuildForTest::GetNode( const char * nodeName ) const
{
    return m_DependencyGraph->FindNode( AStackString( nodeName ) );
}

// SerializeDepGraphToText
//------------------------------------------------------------------------------
void FBuildForTest::SerializeDepGraphToText( const char * nodeName, AString & outBuffer ) const
{
    Node * node = m_DependencyGraph->FindNode( AStackString( nodeName ) );
    Dependencies deps( 1 );
    deps.Add( node );
    m_DependencyGraph->SerializeToText( deps, outBuffer );
}

// Build
//------------------------------------------------------------------------------
/*virtual*/ bool FBuildForTest::Build( Node * nodeToBuild )
{
    // Perform build as normal
    const bool result = FBuild::Build( nodeToBuild );

    // Output -profile info if enabled
    if ( m_Options.m_Profile )
    {
        BuildProfiler::Get().Capture( *this );
        VERIFY( BuildProfiler::Get().SaveJSON( "fbuild_profile.json" ) );
    }

    return result;
}

//------------------------------------------------------------------------------
