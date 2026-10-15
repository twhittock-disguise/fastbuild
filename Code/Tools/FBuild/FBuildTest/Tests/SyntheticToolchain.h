// SyntheticToolchain - One file toolchains kept where a worker keeps them
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildCore/Helpers/ToolManifest.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Containers/UniquePtr.h"
#include "Core/Env/Env.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/Math/xxHash.h"
#include "Core/Strings/AStackString.h"

// SyntheticToolchain
//------------------------------------------------------------------------------
namespace SyntheticToolchain
{
    // Keeps the worker cache of a test clear of any real worker on the machine
    // (a worker removes toolchains and PCHs beyond what it keeps)
    class ScratchWorkerCache
    {
    public:
        // codeDir is the Code directory, with a trailing slash
        void Enter( const AString & codeDir, const char * testGroup )
        {
            m_HadOriginal = Env::GetEnvVariable( "FASTBUILD_TEMP_PATH", m_Original );

            AStackString<> scratch( codeDir );
            scratch += "../tmp/Test/";
            scratch += testGroup;
            scratch += "/worker_tmp/";

            // Every test starts with nothing cached
            Array<AString> files;
            FileIO::GetFiles( scratch, AStackString<>( "*" ), true, &files );
            for ( const AString & file : files )
            {
                FileIO::FileDelete( file.Get() );
            }
            Env::SetEnvVariable( "FASTBUILD_TEMP_PATH", scratch );
        }

        void Leave()
        {
            AStackString<> restored( m_Original );
            if ( m_HadOriginal == false )
            {
                FileIO::GetTempDir( restored ); // What not setting the variable means
            }
            Env::SetEnvVariable( "FASTBUILD_TEMP_PATH", restored );
        }

    private:
        AString m_Original;
        bool m_HadOriginal = false;
    };

    // Where the coordinator says the toolchain lives. Only the layout below it matters.
    inline void GetRootPath( AString & outRoot )
    {
        outRoot.Clear();
        outRoot += NATIVE_SLASH;
        outRoot += "synthetic";
        outRoot += NATIVE_SLASH;
    }

    // The manifest a coordinator sends for a toolchain of one file
    inline void WriteManifest( MemoryStream & ms,
                               uint64_t toolId,
                               const AString & rootPath,
                               const AString & fileName,
                               const char * content )
    {
        ms.Write( toolId );
        ms.Write( rootPath );
        ms.Write( (uint32_t)1 ); // files
        ms.Write( fileName );
        ms.Write( (uint64_t)1 ); // time stamp
        ms.Write( xxHash3::Calc64Big( content, AString::StrLen( content ) ) );
        ms.Write( (uint32_t)AString::StrLen( content ) );
        ms.Write( (uint32_t)0 ); // environment variables
    }

    // Writes the toolchain's file where the worker keeps it, so a manifest built from it is synchronized
    inline bool WriteFile( uint64_t toolId, const char * content )
    {
        const ToolManifest where( toolId );
        AStackString<> directory;
        where.GetRemotePath( directory );
        if ( FileIO::EnsurePathExists( directory ) == false )
        {
            return false;
        }
        AStackString<> file( directory );
        file += "tool.exe";
        FileStream f;
        return f.Open( file.Get(), FileStream::WRITE_ONLY ) &&
               ( f.Write( content, AString::StrLen( content ) ) == AString::StrLen( content ) );
    }

    // A synchronized toolchain, as a worker holds it after receiving it. The caller owns it.
    inline ToolManifest * Create( uint64_t toolId, const char * content )
    {
        if ( WriteFile( toolId, content ) == false )
        {
            return nullptr;
        }

        AStackString<> root;
        GetRootPath( root );
        AStackString<> fileName( root );
        fileName += "tool.exe";

        MemoryStream ms;
        WriteManifest( ms, toolId, root, fileName, content );
        ConstMemoryStream stream( ms.GetData(), ms.GetSize() );

        UniquePtr<ToolManifest> manifest( FNEW( ToolManifest( toolId ) ) );
        if ( ( manifest->DeserializeFromRemote( stream ) == false ) || ( manifest->IsSynchronized() == false ) )
        {
            return nullptr;
        }
        return manifest.ReleaseOwnership();
    }
}

//------------------------------------------------------------------------------
