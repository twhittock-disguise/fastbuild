// TempFiles.h - Names of temporary files that say which process made them
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"
#include "Core/Strings/AString.h"

// TempFiles
//------------------------------------------------------------------------------
// A name is <dir><prefix><process id>.<number><extension>: no two are the same, and what a process
// that crashed left can be told from what a running one is using.
class TempFiles
{
public:
    // A directory for the files of one user, made if it is not there: parent/name, with the id of the
    // user added where the parent is shared between users (Linux, macOS). There it must belong to the
    // user and is closed to everyone else, or this returns false. The names of files are easy to guess
    // and files are not created exclusively, so this is what keeps another user from planting one.
    static bool GetPrivateDirectory( const AString & parent, const char * name, AString & outDir );

    static void MakeName( const AString & dir, const char * prefix, const char * extension, AString & outPath );

    // Is this (the name of) a file made by MakeName with this prefix and extension? Gives the process id.
    static bool ParseName( const char * path, const char * prefix, const char * extension, uint32_t & outProcessId );

    // Deletes the files of processes that are not running. Those of this process and of running ones
    // stay, however old: one may be writing or reading them. A process id can be reused, in which
    // case what a crashed process left stays until that one exits as well.
    static void RemoveStale( const AString & dir, const char * prefix, const char * extension, bool recurse );
};

//------------------------------------------------------------------------------
