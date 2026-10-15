// TestTempFiles.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

#include "Tools/FBuild/FBuildCore/Helpers/TempFiles.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Process/Process.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Time.h"

// TestTempFiles
//------------------------------------------------------------------------------
class TestTempFiles : public FBuildTest
{
private:
    DECLARE_TESTS

    void ProcessLiveness() const;
    void NamesNameTheProcess() const;
    void StaleFilesOfProcessesThatAreGoneAreRemoved() const;
    void PrivateDirectoryIsMade() const;

    // An empty directory for a test
    static void GetCleanDir( const char * testName, AString & outDir );
    static bool MakeFile( const AString & dir, const char * name, AString & outFile );
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestTempFiles )
    REGISTER_TEST( ProcessLiveness )
    REGISTER_TEST( NamesNameTheProcess )
    REGISTER_TEST( StaleFilesOfProcessesThatAreGoneAreRemoved )
    REGISTER_TEST( PrivateDirectoryIsMade )
REGISTER_TESTS_END

// GetCleanDir
//------------------------------------------------------------------------------
/*static*/ void TestTempFiles::GetCleanDir( const char * testName, AString & outDir )
{
    outDir.Format( "../tmp/Test/TempFiles/%s/", testName );
    Array<AString> old;
    FileIO::GetFiles( outDir, AStackString( "*" ), true, &old );
    for ( const AString & file : old )
    {
        FileIO::FileDelete( file.Get() );
    }
}

// MakeFile
//------------------------------------------------------------------------------
/*static*/ bool TestTempFiles::MakeFile( const AString & dir, const char * name, AString & outFile )
{
    outFile = dir;
    outFile += name;
    FileStream f;
    const char data[] = "data";
    return FileIO::EnsurePathExistsForFile( outFile ) &&
           f.Open( outFile.Get(), FileStream::WRITE_ONLY ) &&
           ( f.Write( data, sizeof( data ) ) == sizeof( data ) );
}

// ProcessLiveness
//------------------------------------------------------------------------------
void TestTempFiles::ProcessLiveness() const
{
    TEST_ASSERT( Process::IsProcessRunning( Process::GetCurrentId() ) );
    TEST_ASSERT( Process::IsProcessRunning( GetIdOfAnotherProcess() ) );

    const uint32_t none = GetIdOfNoProcess();
    TEST_ASSERT( none != 0 );
    TEST_ASSERT( Process::IsProcessRunning( none ) == false );
}

// NamesNameTheProcess
//------------------------------------------------------------------------------
void TestTempFiles::NamesNameTheProcess() const
{
    AStackString<> dir;
    GetCleanDir( "NamesNameTheProcess", dir );
    AStackString<> a;
    AStackString<> b;
    TempFiles::MakeName( dir, "incoming.", ".pch", a );
    TempFiles::MakeName( dir, "incoming.", ".pch", b );

    // Never the same, in the directory, and the process that made them can be told
    TEST_ASSERT( a != b );
    TEST_ASSERT( a.BeginsWith( dir ) );
    uint32_t processId = 0;
    TEST_ASSERT( TempFiles::ParseName( a.Get(), "incoming.", ".pch", processId ) );
    TEST_ASSERT( processId == Process::GetCurrentId() );
    TEST_ASSERT( TempFiles::ParseName( "incoming.1234.5.pch", "incoming.", ".pch", processId ) );
    TEST_ASSERT( processId == 1234 );
    TEST_ASSERT( TempFiles::ParseName( "dir\\sub/incoming.77.0.pch", "incoming.", ".pch", processId ) );
    TEST_ASSERT( processId == 77 );

    // Other files are not
    TEST_ASSERT( TempFiles::ParseName( "pch.pch", "incoming.", ".pch", processId ) == false );
    TEST_ASSERT( TempFiles::ParseName( "incoming.pch", "incoming.", ".pch", processId ) == false );
    TEST_ASSERT( TempFiles::ParseName( "incoming.12.pch", "incoming.", ".pch", processId ) == false );
    TEST_ASSERT( TempFiles::ParseName( "incoming.12.3.pch.zst", "incoming.", ".pch", processId ) == false );
    TEST_ASSERT( TempFiles::ParseName( "xincoming.12.3.pch", "incoming.", ".pch", processId ) == false );
    TEST_ASSERT( TempFiles::ParseName( "incoming.12.3.zst", "incoming.", ".pch", processId ) == false );
}

// StaleFilesOfProcessesThatAreGoneAreRemoved
//------------------------------------------------------------------------------
void TestTempFiles::StaleFilesOfProcessesThatAreGoneAreRemoved() const
{
    AStackString<> dir;
    GetCleanDir( "StaleFilesOfProcessesThatAreGoneAreRemoved", dir );

    auto nameOf = [ & ]( AString & outName, uint32_t processId, uint32_t number, const char * extension )
    {
        outName.Format( "incoming.%u.%u%s", processId, number, extension );
    };
    AStackString<> name;
    AStackString<> gone;
    AStackString<> goneInSubdir;
    AStackString<> running;
    AStackString<> own;
    AStackString<> otherExtension;
    AStackString<> unrelated;
    nameOf( name, GetIdOfNoProcess(), 1, ".pch" );
    TEST_ASSERT( MakeFile( dir, name.Get(), gone ) );
    nameOf( name, GetIdOfNoProcess(), 2, ".pch" );
    AStackString<> subdir( "sub/" );
    subdir.Append( name );
    TEST_ASSERT( MakeFile( dir, subdir.Get(), goneInSubdir ) );
    nameOf( name, GetIdOfAnotherProcess(), 3, ".pch" );
    TEST_ASSERT( MakeFile( dir, name.Get(), running ) );
    nameOf( name, Process::GetCurrentId(), 4, ".pch" );
    TEST_ASSERT( MakeFile( dir, name.Get(), own ) );
    nameOf( name, GetIdOfNoProcess(), 5, ".zst" );
    TEST_ASSERT( MakeFile( dir, name.Get(), otherExtension ) );
    TEST_ASSERT( MakeFile( dir, "notes.txt", unrelated ) );

    // However old the file of a process that is running is, it is in use. One that is gone left what it
    // had, however new. Recent or not, it is the process that matters.
#if defined( __WINDOWS__ )
    const uint64_t kDay = ( 24ULL * 3600ULL * 10000000ULL );
#else
    const uint64_t kDay = ( 24ULL * 3600ULL * 1000000000ULL );
#endif
    const uint64_t longAgo = ( Time::GetCurrentFileTime() - kDay );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( running, longAgo ) );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( own, longAgo ) );

    // Not looking in directories inside it
    TempFiles::RemoveStale( dir, "incoming.", ".pch", false );
    TEST_ASSERT( FileIO::FileExists( gone.Get() ) == false );
    TEST_ASSERT( FileIO::FileExists( goneInSubdir.Get() ) );
    TEST_ASSERT( FileIO::FileExists( running.Get() ) );
    TEST_ASSERT( FileIO::FileExists( own.Get() ) );
    TEST_ASSERT( FileIO::FileExists( otherExtension.Get() ) );
    TEST_ASSERT( FileIO::FileExists( unrelated.Get() ) );

    TempFiles::RemoveStale( dir, "incoming.", ".pch", true );
    TEST_ASSERT( FileIO::FileExists( goneInSubdir.Get() ) == false );
    TEST_ASSERT( FileIO::FileExists( running.Get() ) );
    TEST_ASSERT( FileIO::FileExists( own.Get() ) );
    TEST_ASSERT( FileIO::FileExists( otherExtension.Get() ) );
    TEST_ASSERT( FileIO::FileExists( unrelated.Get() ) );
}

// PrivateDirectoryIsMade
//------------------------------------------------------------------------------
void TestTempFiles::PrivateDirectoryIsMade() const
{
    AStackString<> parent;
    GetCleanDir( "PrivateDirectoryIsMade", parent );

    AStackString<> dir;
    TEST_ASSERT( TempFiles::GetPrivateDirectory( parent, "private", dir ) );
    TEST_ASSERT( dir.BeginsWith( parent ) );
    TEST_ASSERT( dir.EndsWith( NATIVE_SLASH ) );
    TEST_ASSERT( dir.Find( "private" ) );
    TEST_ASSERT( FileIO::DirectoryExists( dir ) );

    // The same one each time, and a file can be made in it
    AStackString<> again;
    TEST_ASSERT( TempFiles::GetPrivateDirectory( parent, "private", again ) );
    TEST_ASSERT( again == dir );
    AStackString<> file;
    TEST_ASSERT( MakeFile( dir, "file.txt", file ) );

    // Not where something else is
    AStackString<> blocker;
    TEST_ASSERT( MakeFile( parent, "blocked", blocker ) );
    AStackString<> blockedParent( blocker );
    blockedParent += NATIVE_SLASH;
    AStackString<> blockedDir;
    TEST_ASSERT( TempFiles::GetPrivateDirectory( blockedParent, "private", blockedDir ) == false );
}

//------------------------------------------------------------------------------
