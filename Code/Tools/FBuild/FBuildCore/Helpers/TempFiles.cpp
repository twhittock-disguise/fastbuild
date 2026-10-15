// TempFiles.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "TempFiles.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/FileIO/FileIO.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Process.h"
#include "Core/Strings/AStackString.h"

#include <stdlib.h>
#include <string.h>
#if defined( __LINUX__ ) || defined( __APPLE__ )
    #include <sys/stat.h>
    #include <unistd.h>
#endif

// GetPrivateDirectory
//------------------------------------------------------------------------------
/*static*/ bool TempFiles::GetPrivateDirectory( const AString & parent, const char * name, AString & outDir )
{
    outDir = parent;
#if defined( __LINUX__ ) || defined( __APPLE__ )
    outDir.AppendFormat( "%s.%u", name, static_cast<uint32_t>( ::getuid() ) );
#else
    outDir += name;
#endif
    outDir += NATIVE_SLASH;
    if ( FileIO::EnsurePathExists( outDir ) == false )
    {
        return false;
    }

#if defined( __LINUX__ ) || defined( __APPLE__ )
    AStackString<> path( outDir );
    path.SetLength( path.GetLength() - 1 ); // stat does not want the slash
    struct stat info;
    if ( ( ::stat( path.Get(), &info ) != 0 ) || ( S_ISDIR( info.st_mode ) == false ) || ( info.st_uid != ::getuid() ) )
    {
        return false;
    }
    return ( ::chmod( path.Get(), S_IRWXU ) == 0 );
#else
    return true;
#endif
}

// MakeName
//------------------------------------------------------------------------------
/*static*/ void TempFiles::MakeName( const AString & dir, const char * prefix, const char * extension, AString & outPath )
{
    static Atomic<uint32_t> s_Counter{ 0 };
    outPath = dir;
    outPath.AppendFormat( "%s%u.%u%s", prefix, Process::GetCurrentId(), s_Counter.Increment(), extension );
}

// ParseName
//------------------------------------------------------------------------------
/*static*/ bool TempFiles::ParseName( const char * path, const char * prefix, const char * extension, uint32_t & outProcessId )
{
    const char * name = path;
    for ( const char * c = path; *c; ++c )
    {
        if ( ( *c == '/' ) || ( *c == '\\' ) )
        {
            name = ( c + 1 );
        }
    }

    const size_t prefixLength = strlen( prefix );
    if ( strncmp( name, prefix, prefixLength ) != 0 )
    {
        return false;
    }

    // <process id>.<number><extension>
    const char * p = ( name + prefixLength );
    char * end = nullptr;
    const unsigned long processId = strtoul( p, &end, 10 );
    if ( ( end == p ) || ( *end != '.' ) )
    {
        return false;
    }
    p = ( end + 1 );
    strtoul( p, &end, 10 );
    if ( ( end == p ) || ( strcmp( end, extension ) != 0 ) )
    {
        return false;
    }
    outProcessId = static_cast<uint32_t>( processId );
    return true;
}

// RemoveStale
//------------------------------------------------------------------------------
/*static*/ void TempFiles::RemoveStale( const AString & dir, const char * prefix, const char * extension, bool recurse )
{
    Array<FileIO::FileInfo> files;
    Array<AString> patterns;
    AStackString<> pattern( prefix );
    pattern += '*';
    patterns.Append( pattern );
    if ( FileIO::GetFilesEx( dir, &patterns, recurse, &files ) == false )
    {
        return;
    }

    const uint32_t thisProcess = Process::GetCurrentId();
    for ( const FileIO::FileInfo & file : files )
    {
        uint32_t processId = 0;
        if ( ( ParseName( file.m_Name.Get(), prefix, extension, processId ) == false ) ||
             ( processId == thisProcess ) ||
             Process::IsProcessRunning( processId ) )
        {
            continue;
        }
        FileIO::FileDelete( file.m_Name.Get() );
    }
}

//------------------------------------------------------------------------------
