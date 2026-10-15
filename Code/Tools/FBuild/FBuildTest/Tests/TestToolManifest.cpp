// TestToolManifest.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"
#include "SyntheticToolchain.h"

#include "Tools/FBuild/FBuildCore/Helpers/ToolManifest.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/Job.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Containers/UniquePtr.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/Strings/AStackString.h"

// TestToolManifest
//------------------------------------------------------------------------------
class TestToolManifest : public FBuildTest
{
private:
    DECLARE_TESTS

    virtual void PreTest() const override;
    virtual void PostTest() const override;

    void MigrateKeepsFileSize() const;
    void PersistedToolchainRoundTrip() const;
    void PersistedToolchainIsRefusedWhenNotIntact() const;
    void ManifestCannotLeaveToolchainDirectory() const;
    void SecondManifestIsIgnored() const;
    void UseCounts() const;
    void RecencyIsPersistedWhenLastUserLeaves() const;

    static bool ReadBytes( const AString & path, Array<char> & outBytes );
    static bool WriteBytes( const AString & path, const Array<char> & bytes );

    mutable SyntheticToolchain::ScratchWorkerCache m_WorkerCache;
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestToolManifest )
    REGISTER_TEST( MigrateKeepsFileSize )
    REGISTER_TEST( PersistedToolchainRoundTrip )
    REGISTER_TEST( PersistedToolchainIsRefusedWhenNotIntact )
    REGISTER_TEST( ManifestCannotLeaveToolchainDirectory )
    REGISTER_TEST( SecondManifestIsIgnored )
    REGISTER_TEST( UseCounts )
    REGISTER_TEST( RecencyIsPersistedWhenLastUserLeaves )
REGISTER_TESTS_END

// PreTest
//------------------------------------------------------------------------------
/*virtual*/ void TestToolManifest::PreTest() const
{
    FBuildTest::PreTest();

    AStackString<> codeDir;
    GetCodeDir( codeDir );
    m_WorkerCache.Enter( codeDir, "TestToolManifest" );
}

// PostTest
//------------------------------------------------------------------------------
/*virtual*/ void TestToolManifest::PostTest() const
{
    m_WorkerCache.Leave();

    FBuildTest::PostTest();
}

// ReadBytes / WriteBytes
//------------------------------------------------------------------------------
/*static*/ bool TestToolManifest::ReadBytes( const AString & path, Array<char> & outBytes )
{
    FileStream f;
    if ( f.Open( path.Get(), FileStream::READ_ONLY ) == false )
    {
        return false;
    }
    outBytes.SetSize( (size_t)f.GetFileSize() );
    return ( f.Read( outBytes.Begin(), outBytes.GetSize() ) == outBytes.GetSize() );
}

/*static*/ bool TestToolManifest::WriteBytes( const AString & path, const Array<char> & bytes )
{
    FileStream f;
    if ( f.Open( path.Get(), FileStream::WRITE_ONLY ) == false )
    {
        return false;
    }
    return ( bytes.IsEmpty() || ( f.Write( bytes.Begin(), bytes.GetSize() ) == bytes.GetSize() ) );
}

// MigrateKeepsFileSize
//------------------------------------------------------------------------------
void TestToolManifest::MigrateKeepsFileSize() const
{
    // A manifest rebuilt from the bff has not seen the files yet. When the compiler is
    // unchanged, what the old one learned (including the size, which is part of what
    // is sent to workers and of the toolId) has to carry over.
    const ToolManifestFile old( AStackString<>( "tool.exe" ), 5, 0xABCD, 1234 );
    ToolManifestFile migrated( AStackString<>( "tool.exe" ), 0, 0, 0 );
    migrated.Migrate( old );

    TEST_ASSERT( migrated.GetTimeStamp() == 5 );
    TEST_ASSERT( migrated.GetHash() == 0xABCD );
    TEST_ASSERT( migrated.GetUncompressedContentSize() == 1234 );
}

// PersistedToolchainRoundTrip
//------------------------------------------------------------------------------
void TestToolManifest::PersistedToolchainRoundTrip() const
{
    UniquePtr<ToolManifest> saved( SyntheticToolchain::Create( 0x11, "compiler" ) );
    TEST_ASSERT( saved.Get() );
    TEST_ASSERT( saved->SaveToDisk() );

    AStackString<> path;
    saved->GetPersistedPath( path );
    TEST_ASSERT( FileIO::FileExists( path.Get() ) );

    UniquePtr<ToolManifest> loaded( ToolManifest::LoadFromDisk( path ) );
    TEST_ASSERT( loaded.Get() );
    TEST_ASSERT( loaded->GetToolId() == 0x11 );
    TEST_ASSERT( loaded->IsSynchronized() );
    TEST_ASSERT( loaded->GetFiles().GetSize() == 1 );

    // Recency survives a restart through the manifest's time stamp
    TEST_ASSERT( loaded->GetLastUsed() == FileIO::GetFileLastWriteTime( path ) );
}

// PersistedToolchainIsRefusedWhenNotIntact
//------------------------------------------------------------------------------
void TestToolManifest::PersistedToolchainIsRefusedWhenNotIntact() const
{
    // Damage to the manifest file. The undamaged file loads, so each of these is refused for the damage.
    {
        UniquePtr<ToolManifest> saved( SyntheticToolchain::Create( 0x21, "compiler" ) );
        TEST_ASSERT( saved.Get() && saved->SaveToDisk() );
        AStackString<> path;
        saved->GetPersistedPath( path );

        Array<char> good;
        TEST_ASSERT( ReadBytes( path, good ) );
        TEST_ASSERT( good.GetSize() > 16 );

        auto loadsAs = [ &path ]( const Array<char> & bytes )
        {
            WriteBytes( path, bytes );
            UniquePtr<ToolManifest> manifest( ToolManifest::LoadFromDisk( path ) );
            return ( manifest.Get() != nullptr );
        };

        TEST_ASSERT( loadsAs( good ) );

        Array<char> bad( good );
        bad.SetSize( good.GetSize() - 10 );
        TEST_ASSERT( loadsAs( bad ) == false ); // Cut short

        bad = good;
        bad[ 0 ] ^= 0xFF;
        TEST_ASSERT( loadsAs( bad ) == false ); // Not a manifest

        bad = good;
        memset( bad.Begin() + sizeof( uint32_t ), 0, sizeof( uint64_t ) );
        TEST_ASSERT( loadsAs( bad ) == false ); // Without a toolId

        bad.Clear();
        TEST_ASSERT( loadsAs( bad ) == false ); // Empty

        bad = good;
        bad.SetSize( ( 64 * MEGABYTE ) + 1 );
        TEST_ASSERT( loadsAs( bad ) == false ); // Larger than any manifest

        TEST_ASSERT( loadsAs( good ) ); // Nothing is left behind from the failures

        AStackString<> missing( path );
        missing += ".missing";
        TEST_ASSERT( ToolManifest::LoadFromDisk( missing ) == nullptr );
    }

    // The toolchain's own files were changed, keeping their size
    {
        AStackString<> path;
        AStackString<> toolFile;
        {
            UniquePtr<ToolManifest> saved( SyntheticToolchain::Create( 0x22, "compiler" ) );
            TEST_ASSERT( saved.Get() && saved->SaveToDisk() );
            saved->GetPersistedPath( path );
            saved->GetRemoteFilePath( 0, toolFile );
        } // Releases the lock on the file

        Array<char> content;
        TEST_ASSERT( ReadBytes( toolFile, content ) );
        content[ 3 ] ^= 0x55;
        TEST_ASSERT( WriteBytes( toolFile, content ) );
        TEST_ASSERT( ToolManifest::LoadFromDisk( path ) == nullptr );
    }

    // A manifest that is not in the directory of its toolId could not be removed again
    {
        UniquePtr<ToolManifest> saved( SyntheticToolchain::Create( 0x23, "compiler" ) );
        TEST_ASSERT( saved.Get() && saved->SaveToDisk() );
        AStackString<> path;
        saved->GetPersistedPath( path );
        Array<char> bytes;
        TEST_ASSERT( ReadBytes( path, bytes ) );

        const ToolManifest elsewhere( 0x24 );
        AStackString<> otherPath;
        elsewhere.GetPersistedPath( otherPath );
        TEST_ASSERT( FileIO::EnsurePathExistsForFile( otherPath ) );
        TEST_ASSERT( WriteBytes( otherPath, bytes ) );

        TEST_ASSERT( ToolManifest::LoadFromDisk( otherPath ) == nullptr );
        UniquePtr<ToolManifest> inPlace( ToolManifest::LoadFromDisk( path ) );
        TEST_ASSERT( inPlace.Get() );
    }
}

// ManifestCannotLeaveToolchainDirectory
//------------------------------------------------------------------------------
void TestToolManifest::ManifestCannotLeaveToolchainDirectory() const
{
    AStackString<> root;
    SyntheticToolchain::GetRootPath( root );

    auto accepted = [ &root ]( const AString & fileName )
    {
        MemoryStream ms;
        SyntheticToolchain::WriteManifest( ms, 0x31, root, fileName, "compiler" );
        ConstMemoryStream stream( ms.GetData(), ms.GetSize() );
        ToolManifest manifest( 0x31 );
        return manifest.DeserializeFromRemote( stream );
    };

    // Below the root is fine, in a sub directory too
    AStackString<> inRoot( root );
    inRoot += "tool.exe";
    TEST_ASSERT( accepted( inRoot ) );
    AStackString<> inSubDirectory( root );
    inSubDirectory += "sub";
    inSubDirectory += NATIVE_SLASH;
    inSubDirectory += "tool.exe";
    TEST_ASSERT( accepted( inSubDirectory ) );

    // A name that would reach out of the toolchain directory is refused, whichever slash it uses
    AStackString<> up( root );
    up += "..";
    up += NATIVE_SLASH;
    up += "evil.exe";
    TEST_ASSERT( accepted( up ) == false );
    AStackString<> upWithSlash( root );
    upWithSlash += "sub/../../evil.exe";
    TEST_ASSERT( accepted( upWithSlash ) == false );
    AStackString<> upAsFileName( root );
    upAsFileName += "..";
    TEST_ASSERT( accepted( upAsFileName ) == false );
}

// SecondManifestIsIgnored
//------------------------------------------------------------------------------
void TestToolManifest::SecondManifestIsIgnored() const
{
    UniquePtr<ToolManifest> manifest( SyntheticToolchain::Create( 0x41, "compiler" ) );
    TEST_ASSERT( manifest.Get() );
    TEST_ASSERT( manifest->GetFiles().GetSize() == 1 );

    // Another coordinator sends its manifest for the same toolchain (listing files this one does not).
    // The first manifest wins: its files are already in use.
    AStackString<> root;
    SyntheticToolchain::GetRootPath( root );
    AStackString<> toolFile( root );
    toolFile += "tool.exe";
    AStackString<> otherFile( root );
    otherFile += "other.dll";

    MemoryStream ms;
    ms.Write( (uint64_t)0x41 );
    ms.Write( root );
    ms.Write( (uint32_t)2 );
    ms.Write( toolFile );
    ms.Write( (uint64_t)1 );
    ms.Write( manifest->GetFiles()[ 0 ].GetHash() );
    ms.Write( manifest->GetFiles()[ 0 ].GetUncompressedContentSize() );
    ms.Write( otherFile );
    ms.Write( (uint64_t)1 );
    ms.Write( (uint64_t)0x1234 );
    ms.Write( (uint32_t)10 );
    ms.Write( (uint32_t)0 );
    ConstMemoryStream stream( ms.GetData(), ms.GetSize() );

    TEST_ASSERT( manifest->DeserializeFromRemote( stream ) );
    TEST_ASSERT( manifest->GetFiles().GetSize() == 1 );
    TEST_ASSERT( manifest->IsSynchronized() );
    TEST_ASSERT( manifest->GetFiles()[ 0 ].GetSyncState() == ToolManifestFile::SYNCHRONIZED );
}

// UseCounts
//------------------------------------------------------------------------------
void TestToolManifest::UseCounts() const
{
    ToolManifest manifest( 0x51 );
    TEST_ASSERT( manifest.IsInUse() == false );
    TEST_ASSERT( manifest.GetLastUsed() == 0 );

    // A job uses its toolchain until it is gone, however it ends
    Job * first = FNEW( Job( (Node *)nullptr ) );
    Job * second = FNEW( Job( (Node *)nullptr ) );
    first->SetToolManifest( &manifest );
    second->SetToolManifest( &manifest );
    TEST_ASSERT( manifest.IsInUse() );
    TEST_ASSERT( manifest.GetLastUsed() > 0 ); // Starting a job is what makes a toolchain recent

    FDELETE first;
    TEST_ASSERT( manifest.IsInUse() );
    FDELETE second;
    TEST_ASSERT( manifest.IsInUse() == false );
}

// RecencyIsPersistedWhenLastUserLeaves
//------------------------------------------------------------------------------
void TestToolManifest::RecencyIsPersistedWhenLastUserLeaves() const
{
    UniquePtr<ToolManifest> manifest( SyntheticToolchain::Create( 0x61, "compiler" ) );
    TEST_ASSERT( manifest.Get() && manifest->SaveToDisk() );
    AStackString<> path;
    manifest->GetPersistedPath( path );

    // Far in the past, in whichever unit the platform uses for file times
    const uint64_t kLongAgo = 100000000000ULL;
    const uint64_t stale = ( FileIO::GetFileLastWriteTime( path ) - kLongAgo );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( path, stale ) );

    manifest->AcquireUse();
    manifest->AcquireUse();
    manifest->ReleaseUse();
    TEST_ASSERT( FileIO::GetFileLastWriteTime( path ) < ( stale + ( kLongAgo / 2 ) ) ); // Someone is still using it

    manifest->ReleaseUse();
    TEST_ASSERT( FileIO::GetFileLastWriteTime( path ) > ( stale + ( kLongAgo / 2 ) ) ); // The last user left: now
}

//------------------------------------------------------------------------------
