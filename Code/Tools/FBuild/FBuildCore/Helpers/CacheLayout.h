// CacheLayout - Names in a worker's or a gateway's cache directory, and their removal
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"

// Forward Declarations
//------------------------------------------------------------------------------
template <class T> class Array;
class AString;

// CacheLayout
//------------------------------------------------------------------------------
namespace CacheLayout
{
    // A toolchain's directory: the prefix, then its id as 16 hex digits
    inline constexpr char kToolchainDirPrefix[] = "toolchain.";
    inline constexpr size_t kToolchainDirPrefixLength = sizeof( kToolchainDirPrefix ) - 1;

    // Exactly 16 hex digits, of either case
    [[nodiscard]] bool ParseHex64( const char * begin, const char * end, uint64_t & outValue );

    // Deletes the files, which are under root (given without a trailing slash), then the directories
    // between them and root, deepest first, and root
    void DeleteFilesAndDirectories( const AString & root, const Array<AString> & files );

    // Deletes everything under root (given without a trailing slash), and root
    void DeleteDirectoryTree( const AString & root );
}

//------------------------------------------------------------------------------
