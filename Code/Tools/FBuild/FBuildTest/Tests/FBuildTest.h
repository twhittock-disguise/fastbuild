// FBuiltTest.h
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "TestFramework/TestGroup.h"

#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/FBuildOptions.h"
#include "Tools/FBuild/FBuildCore/Graph/Node.h"
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"

#include "Core/Containers/Array.h"
#include "Core/Process/Mutex.h"

// Forward Declarations
//------------------------------------------------------------------------------
struct FBuildStats;

// XorShift64
//------------------------------------------------------------------------------
// The same numbers for the same seed
class XorShift64
{
public:
    explicit XorShift64( uint64_t seed ) : m_State( seed | 1 ) {}
    uint64_t Next()
    {
        m_State ^= ( m_State << 13 );
        m_State ^= ( m_State >> 7 );
        m_State ^= ( m_State << 17 );
        return m_State;
    }
    uint32_t Below( uint32_t limit ) { return (uint32_t)( Next() % limit ); }

private:
    uint64_t m_State;
};

// NullFileObserver
//------------------------------------------------------------------------------
// What Compressor::CompressZstdFile is given by a caller that has no use for the data it reads
class NullFileObserver : public Compressor::FileObserver
{
public:
    virtual void OnPass() override {}
    virtual void OnPiece( const void *, size_t ) override {}
};

// FBuildTest
//------------------------------------------------------------------------------
class FBuildTest : public TestGroup
{
public:
    // The files in a directory and below
    static uint32_t CountFilesIn( const AString & dir );

    // Data that does not compress, the same for the same seed
    static void MakeRandomBytes( uint32_t size, uint64_t seed, Array<char> & outBytes );

    // Files as they are written and read by tests: the whole of the content, in a directory that is made if it is not there
    static bool WriteFile( const AString & path, const Array<char> & content );
    static bool ReadFile( const AString & path, Array<char> & outContent );

protected:
    FBuildTest();

    virtual void PreTest() const override;
    virtual void PostTest() const override;

    // helpers to manage generated files
    void EnsureFileDoesNotExist( const char * fileName ) const;
    void EnsureFileDoesNotExist( const AString & fileName ) const { EnsureFileDoesNotExist( fileName.Get() ); }
    void EnsureFileExists( const char * fileName ) const;
    void EnsureFileExists( const AString & fileName ) const { EnsureFileExists( fileName.Get() ); }
    void EnsureDirDoesNotExist( const char * dirPath ) const;
    void EnsureDirDoesNotExist( const AString & dirPath ) const { EnsureDirDoesNotExist( dirPath.Get() ); }
    void EnsureDirExists( const char * dirPath ) const;
    void EnsureDirExists( const AString & dirPath ) const { EnsureDirExists( dirPath.Get() ); }
    void LoadFileContentsAsString( const char * fileName, AString & outString ) const;
    void MakeFile( const char * fileName, const char * fileContents ) const;

    // Helpers to invoke builds or parse bff files
    void Parse( const char * fileName, bool expectFailure = false ) const;
    bool ParseFromString( bool expectResult,
                          const char * bffContents,
                          const char * expectedMessage = nullptr,
                          const char * unexpectedMessage = nullptr ) const;

    // Helper macros
#define TEST_PARSE_OK( ... )        TEST_ASSERT( ParseFromString( true, __VA_ARGS__ ) )
#define TEST_PARSE_FAIL( ... )      TEST_ASSERT( ParseFromString( false, __VA_ARGS__ ) )

    // Helpers to check build results
    void CheckStatsNode( const FBuildStats & stats, size_t numSeen, size_t numBuilt, Node::Type nodeType ) const;
    void CheckStatsTotal( const FBuildStats & stats, size_t numSeen, size_t numBuilt ) const;
    void CheckStatsNode( size_t numSeen, size_t numBuilt, Node::Type nodeType ) const;
    void CheckStatsTotal( size_t numSeen, size_t numBuilt ) const;

    // other helpers
    static void GetCodeDir( AString & codeDir );

    // Data like the heap image of an MSVC PCH, the same for the same seed: 8 byte words, most of them pointers
    // into the image (words whose high half is the base kPchLikeBase), that repeat or are near each other, and
    // the rest numbers. It compresses, and better after PchTransform.
    static constexpr uint32_t kPchLikeBase = 0x6BB;
    static void MakePchLikeBytes( uint32_t size, uint64_t seed, Array<char> & outBytes );

    // The window of the zstd frame of a blob (log 2 of its size), or 0 if it has none because the data fits in one segment
    static uint32_t GetBlobWindowLog( const Array<char> & blob );

    // The blob a client makes of a PCH (Compressor::CompressZstdFile, through a file as it does), with the PCH
    // transform of this base applied, or none if the base is kNoTransform (a base can be 0). outTransformed says
    // whether it was: it is not if the transform does not make the PCH smaller.
    static constexpr uint32_t kNoTransform = 0xFFFFFFFFu;
    static bool MakePchBlob( const Array<char> & pch, uint32_t transformBase, Array<char> & outBlob, bool * outTransformed = nullptr );

    // Ids of processes: one that no process has, and one that is running that is not this process
    static uint32_t GetIdOfNoProcess();
    static uint32_t GetIdOfAnotherProcess();

    const AString & GetRecordedOutput() const { return s_RecordedOutput; }

private:
    mutable AString m_OriginalWorkingDir;
    static bool s_DebuggerAttached;
    static bool LoggingCallback( const char * message );
    static Mutex s_OutputMutex;
    static AString s_RecordedOutput;
};

// FBuildTestOptions
//------------------------------------------------------------------------------
class FBuildTestOptions : public FBuildOptions
{
public:
    FBuildTestOptions();
};

// FBuildForTest
//  - Wrapper for tests to use FBuild while accessing some internals for
//    testing purposes
//------------------------------------------------------------------------------
class FBuildForTest : public FBuild
{
public:
    FBuildForTest( FBuildOptions & options )
        : FBuild( options )
    {
    }

    size_t GetRecursiveDependencyCount( const Node * node ) const;
    size_t GetRecursiveDependencyCount( const char * nodeName ) const;

    void GetNodesOfType( Node::Type type, Array<const Node *> & outNodes ) const;
    const Node * GetNode( const char * nodeName ) const;

    void SerializeDepGraphToText( const char * nodeName, AString & outBuffer ) const;

    const AString & GetDependencyGraphFile() const { return m_DependencyGraphFile; }

    using FBuild::Build;
    virtual bool Build( Node * nodeToBuild ) override;
};

//------------------------------------------------------------------------------
