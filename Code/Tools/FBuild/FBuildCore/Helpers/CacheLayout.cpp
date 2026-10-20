// CacheLayout.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "CacheLayout.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/FileIO/FileIO.h"
#include "Core/Strings/AStackString.h"

// ParseHex64
//------------------------------------------------------------------------------
bool CacheLayout::ParseHex64( const char * begin, const char * end, uint64_t & outValue )
{
    if ( ( end - begin ) != 16 )
    {
        return false;
    }
    uint64_t value = 0;
    for ( const char * p = begin; p < end; ++p )
    {
        const char c = *p;
        uint8_t nibble;
        if ( ( c >= '0' ) && ( c <= '9' ) )      { nibble = (uint8_t)( c - '0' ); }
        else if ( ( c >= 'a' ) && ( c <= 'f' ) ) { nibble = (uint8_t)( c - 'a' + 10 ); }
        else if ( ( c >= 'A' ) && ( c <= 'F' ) ) { nibble = (uint8_t)( c - 'A' + 10 ); }
        else { return false; }
        value = ( ( value << 4 ) | nibble );
    }
    outValue = value;
    return true;
}

// DeleteFilesAndDirectories
//------------------------------------------------------------------------------
void CacheLayout::DeleteFilesAndDirectories( const AString & root, const Array<AString> & files )
{
    Array<AString> directories;
    directories.Append( root );
    for ( const AString & file : files )
    {
        FileIO::FileDelete( file.Get() );

        const char * lastSlash = file.FindLast( NATIVE_SLASH );
        if ( lastSlash == nullptr )
        {
            continue;
        }
        AStackString<> directory( file.Get(), lastSlash );
        while ( ( directory.GetLength() > root.GetLength() ) && ( directories.Find( directory ) == nullptr ) )
        {
            directories.Append( directory );
            directory.SetLength( (uint32_t)( directory.FindLast( NATIVE_SLASH ) - directory.Get() ) );
        }
    }

    // Deepest first, so a directory is empty by the time it is removed
    directories.Sort( []( const AString & a, const AString & b ) { return a.GetLength() > b.GetLength(); } );
    for ( const AString & directory : directories )
    {
        FileIO::DirectoryDelete( directory );
    }
}

// DeleteDirectoryTree
//------------------------------------------------------------------------------
void CacheLayout::DeleteDirectoryTree( const AString & root )
{
    Array<AString> files;
    FileIO::GetFiles( root, AStackString<>( "*" ), true, &files );
    DeleteFilesAndDirectories( root, files );
}

//------------------------------------------------------------------------------
