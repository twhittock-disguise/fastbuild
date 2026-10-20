// ToolchainStash.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "ToolchainStash.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/Helpers/CacheLayout.h"
#include "Tools/FBuild/FBuildCore/Helpers/LruSelect.h"
#include "Tools/FBuild/FBuildCore/Helpers/ToolManifest.h"

// Core
#include "Core/Env/Assert.h"
#include "Core/Process/Thread.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Timer.h"

#include <inttypes.h>

// Defines
//------------------------------------------------------------------------------
namespace
{
    const char * const kManifestFileName = "manifest.bin";
    const char * const kTempExtension = ".tmp";

    // A manifest claiming more files than this is not one a client has
    constexpr uint32_t kMaxFilesPerToolchain = 65536;

    // How long a manifest waits for the directory of the same toolchain to finish being deleted
    constexpr uint32_t kMaxDeleteWaitMs = 30000;

    // The whole manifest, as a worker reads it (ToolManifest::DeserializeFromRemote), so a worker is never sent one it refuses
    bool ReadManifest( const void * payload, size_t payloadSize, uint64_t toolId, uint32_t & outNumFiles )
    {
        ConstMemoryStream ms( payload, payloadSize );
        AStackString mainExecutablePath;
        Array<ToolManifestFile> files;
        Array<AString> customEnvironmentVariables;
        if ( ( ToolManifest::ReadRemote( ms, toolId, mainExecutablePath, files, customEnvironmentVariables ) == false ) ||
             ( files.GetSize() > kMaxFilesPerToolchain ) )
        {
            return false;
        }
        outNumFiles = (uint32_t)files.GetSize();
        return true;
    }

    bool WriteThroughTemporaryFile( const AString & path, const void * payload, size_t payloadSize )
    {
        AStackString<> temp( path );
        temp += kTempExtension;
        {
            FileStream f;
            if ( ( f.Open( temp.Get(), FileStream::WRITE_ONLY ) == false ) ||
                 ( f.WriteBuffer( payload, payloadSize ) != payloadSize ) )
            {
                f.Close();
                FileIO::FileDelete( temp.Get() );
                return false;
            }
        }
        if ( FileIO::FileMove( temp, path ) == false )
        {
            FileIO::FileDelete( temp.Get() );
            return false;
        }
        return true;
    }

    // file.<n>.bin
    bool ParseFileId( const AString & fileName, uint32_t & outFileId )
    {
        if ( fileName.BeginsWith( "file." ) == false || fileName.EndsWith( ".bin" ) == false )
        {
            return false;
        }
        const char * digits = ( fileName.Get() + 5 );
        const char * end = ( fileName.Get() + fileName.GetLength() - 4 );
        if ( ( digits == end ) || ( ( end - digits ) > 9 ) )
        {
            return false;
        }
        uint32_t value = 0;
        for ( const char * p = digits; p < end; ++p )
        {
            if ( ( *p < '0' ) || ( *p > '9' ) )
            {
                return false;
            }
            value = ( value * 10 ) + (uint32_t)( *p - '0' );
        }
        outFileId = value;
        return true;
    }

    // What an earlier run left in one toolchain directory
    struct ScannedToolchain
    {
        uint64_t        m_ToolId = 0;
        bool            m_HasManifest = false;
        uint32_t        m_ManifestSize = 0;
        uint64_t        m_ManifestTime = 0;
        Array<uint32_t> m_FileIds;
        Array<uint32_t> m_FileSizes;
    };
}

// CONSTRUCTOR
//------------------------------------------------------------------------------
ToolchainStash::ToolchainStash( const AString & directory )
    : m_Directory( directory )
    , m_MaxDeleteWaitMs( kMaxDeleteWaitMs )
{
    PathUtils::EnsureTrailingSlash( m_Directory );
    Scan();
}

// DESTRUCTOR
//------------------------------------------------------------------------------
ToolchainStash::~ToolchainStash() = default;

// AddManifest
//------------------------------------------------------------------------------
ToolchainStash::Result ToolchainStash::AddManifest( uint64_t toolId, const void * payload, size_t payloadSize )
{
    uint32_t numFiles;
    if ( ReadManifest( payload, payloadSize, toolId, numFiles ) == false )
    {
        return STASH_INVALID;
    }
    return Write( toolId, kManifestFileId, numFiles, payload, payloadSize );
}

// AddFile
//------------------------------------------------------------------------------
ToolchainStash::Result ToolchainStash::AddFile( uint64_t toolId, uint32_t fileId, const void * payload, size_t payloadSize )
{
    if ( fileId == kManifestFileId )
    {
        return STASH_INVALID;
    }
    return Write( toolId, fileId, 0, payload, payloadSize );
}

// Claim
//------------------------------------------------------------------------------
// Takes the slot for a write, creating the toolchain for a manifest. Not claimed: the
// result is the answer (a duplicate, or invalid), or mustWait says to try again.
ToolchainStash::Result ToolchainStash::Claim( uint64_t toolId, uint32_t slot, uint32_t numFiles, Array<uint64_t> & outToDelete, bool & outClaimed, bool & outMustWait )
{
    MutexHolder mh( m_Mutex );
    Entry * entry = Find( toolId );
    if ( slot == kManifestFileId )
    {
        if ( entry == nullptr )
        {
            if ( m_Deleting.Find( toolId ) )
            {
                outMustWait = true;
                return STASH_INVALID;
            }
            Entry & added = m_Entries.EmplaceBack();
            added.m_ToolId = toolId;
            added.m_NumFiles = numFiles;
            added.m_FileStates.SetSize( numFiles );
            added.m_FileSizes.SetSize( numFiles );
            for ( SlotState & state : added.m_FileStates )
            {
                state = SLOT_EMPTY;
            }
            for ( uint32_t & size : added.m_FileSizes )
            {
                size = 0;
            }
            Trim( outToDelete );
            entry = Find( toolId ); // Trim does not remove it, but it may have moved the array
        }
        else if ( entry->m_NumFiles != numFiles )
        {
            return STASH_INVALID;
        }
    }
    else if ( entry == nullptr )
    {
        return STASH_UNKNOWN; // A file with no manifest to say it belongs
    }
    else if ( slot >= entry->m_NumFiles )
    {
        return STASH_INVALID;
    }
    entry->m_LastUse = ++m_Clock;

    SlotState & state = ( slot == kManifestFileId ) ? entry->m_ManifestState : entry->m_FileStates[ slot ];
    if ( state != SLOT_EMPTY )
    {
        return entry->IsComplete() ? STASH_COMPLETE : STASH_INCOMPLETE; // Already held, or being written
    }
    state = SLOT_WRITING;
    ++entry->m_NumWrites;
    outClaimed = true;
    return STASH_INCOMPLETE;
}

// Write
//------------------------------------------------------------------------------
// A slot is claimed under the lock, written outside it, then marked held. A toolchain
// with a write in progress is not removed until the write ends.
ToolchainStash::Result ToolchainStash::Write( uint64_t toolId, uint32_t slot, uint32_t numFiles, const void * payload, size_t payloadSize )
{
    if ( payloadSize > 0xFFFFFFFF )
    {
        return STASH_INVALID;
    }

    Array<uint64_t> toDelete;
    const Timer waitTimer;
    for ( ;; )
    {
        bool claimed = false;
        bool mustWait = false;
        const Result early = Claim( toolId, slot, numFiles, toDelete, claimed, mustWait );
        DeleteToolchains( toDelete );
        toDelete.Clear();
        if ( mustWait )
        {
            // The toolchain was just evicted and its directory is still being deleted
            if ( waitTimer.GetElapsedMS() >= (float)m_MaxDeleteWaitMs )
            {
                return STASH_FAILED;
            }
            Thread::Sleep( 1 );
            continue;
        }
        if ( claimed == false )
        {
            return early;
        }
        break;
    }

    AStackString<> directory;
    AStackString<> path;
    GetToolchainDirectory( toolId, directory );
    GetSlotPath( toolId, slot, path );
    const bool written = FileIO::EnsurePathExists( directory ) &&
                         WriteThroughTemporaryFile( path, payload, payloadSize );

    Result result = STASH_FAILED;
    {
        MutexHolder mh( m_Mutex );
        Entry * entry = Find( toolId );
        ASSERT( entry && ( entry->m_NumWrites > 0 ) );
        --entry->m_NumWrites;
        SlotState & state = ( slot == kManifestFileId ) ? entry->m_ManifestState : entry->m_FileStates[ slot ];
        if ( written )
        {
            state = SLOT_HELD;
            if ( slot == kManifestFileId )
            {
                entry->m_ManifestSize = (uint32_t)payloadSize;
            }
            else
            {
                entry->m_FileSizes[ slot ] = (uint32_t)payloadSize;
                ++entry->m_NumFilesHeld;
            }
            result = entry->IsComplete() ? STASH_COMPLETE : STASH_INCOMPLETE;
        }
        else
        {
            state = SLOT_EMPTY;
            entry->m_DropWhenIdle = true; // Whatever else arrived for it cannot be completed
        }

        if ( entry->m_DropWhenIdle && ( entry->m_NumWrites == 0 ) && ( entry->IsComplete() == false ) )
        {
            Remove( entry, toDelete );
            result = written ? STASH_INCOMPLETE : STASH_FAILED;
        }
        else if ( result == STASH_COMPLETE )
        {
            Trim( toDelete );
        }
    }
    DeleteToolchains( toDelete );
    return result;
}

// IsComplete
//------------------------------------------------------------------------------
bool ToolchainStash::IsComplete( uint64_t toolId ) const
{
    MutexHolder mh( m_Mutex );
    const Entry * entry = Find( toolId );
    return entry && entry->IsComplete();
}

// HasManifest
//------------------------------------------------------------------------------
bool ToolchainStash::HasManifest( uint64_t toolId ) const
{
    MutexHolder mh( m_Mutex );
    const Entry * entry = Find( toolId );
    return entry && ( entry->m_ManifestState == SLOT_HELD );
}

// GetComplete
//------------------------------------------------------------------------------
void ToolchainStash::GetComplete( Array<uint64_t> & outToolIds ) const
{
    outToolIds.Clear();
    MutexHolder mh( m_Mutex );
    Array<const Entry *> complete;
    for ( const Entry & entry : m_Entries )
    {
        if ( entry.IsComplete() )
        {
            complete.Append( &entry );
        }
    }
    complete.Sort( []( const Entry * a, const Entry * b ) { return a->m_LastUse > b->m_LastUse; } );
    for ( const Entry * entry : complete )
    {
        outToolIds.Append( entry->m_ToolId );
    }
}

// GetMissingFiles
//------------------------------------------------------------------------------
void ToolchainStash::GetMissingFiles( uint64_t toolId, Array<uint32_t> & outFileIds ) const
{
    outFileIds.Clear();
    MutexHolder mh( m_Mutex );
    const Entry * entry = Find( toolId );
    if ( entry )
    {
        for ( uint32_t i = 0; i < entry->m_NumFiles; ++i )
        {
            if ( entry->m_FileStates[ i ] != SLOT_HELD )
            {
                outFileIds.Append( i );
            }
        }
    }
}

// GetManifestFile
//------------------------------------------------------------------------------
bool ToolchainStash::GetManifestFile( uint64_t toolId, AString & outPath, uint32_t & outSize ) const
{
    MutexHolder mh( m_Mutex );
    Entry * entry = Find( toolId );
    if ( ( entry == nullptr ) || ( entry->m_ManifestState != SLOT_HELD ) )
    {
        return false;
    }
    entry->m_LastUse = ++m_Clock;
    GetSlotPath( toolId, kManifestFileId, outPath );
    outSize = entry->m_ManifestSize;
    return true;
}

// GetFile
//------------------------------------------------------------------------------
bool ToolchainStash::GetFile( uint64_t toolId, uint32_t fileId, AString & outPath, uint32_t & outSize ) const
{
    MutexHolder mh( m_Mutex );
    Entry * entry = Find( toolId );
    if ( ( entry == nullptr ) || ( fileId >= entry->m_NumFiles ) || ( entry->m_FileStates[ fileId ] != SLOT_HELD ) )
    {
        return false;
    }
    entry->m_LastUse = ++m_Clock;
    GetSlotPath( toolId, fileId, outPath );
    outSize = entry->m_FileSizes[ fileId ];
    return true;
}

// GetNumFiles
//------------------------------------------------------------------------------
uint32_t ToolchainStash::GetNumFiles( uint64_t toolId ) const
{
    MutexHolder mh( m_Mutex );
    const Entry * entry = Find( toolId );
    return entry ? entry->m_NumFiles : 0;
}

// AddUse
//------------------------------------------------------------------------------
void ToolchainStash::AddUse( uint64_t toolId )
{
    MutexHolder mh( m_Mutex );
    Pin * pin = m_Pins.Find( toolId );
    if ( pin )
    {
        ++pin->m_Count;
        return;
    }
    m_Pins.Append( Pin{ toolId, 1 } );
    Entry * entry = Find( toolId );
    if ( entry )
    {
        entry->m_LastUse = ++m_Clock;
    }
}

// ReleaseUse
//------------------------------------------------------------------------------
void ToolchainStash::ReleaseUse( uint64_t toolId )
{
    Array<uint64_t> toDelete;
    {
        MutexHolder mh( m_Mutex );
        Pin * pin = m_Pins.Find( toolId );
        if ( pin )
        {
            if ( --pin->m_Count == 0 )
            {
                m_Pins.Erase( pin );
            }
            Trim( toDelete );
        }
    }
    DeleteToolchains( toDelete );
}

// DropIncomplete
//------------------------------------------------------------------------------
void ToolchainStash::DropIncomplete( uint64_t toolId )
{
    Array<uint64_t> toDelete;
    DropIncomplete( toolId, toDelete );
    DeleteRemoved( toDelete );
}

// DropIncomplete
//------------------------------------------------------------------------------
void ToolchainStash::DropIncomplete( uint64_t toolId, Array<uint64_t> & outRemoved )
{
    MutexHolder mh( m_Mutex );
    Entry * entry = Find( toolId );
    if ( ( entry == nullptr ) || entry->IsComplete() )
    {
        return;
    }
    if ( entry->m_NumWrites > 0 )
    {
        entry->m_DropWhenIdle = true; // The last write removes it
    }
    else
    {
        Remove( entry, outRemoved );
    }
}

// DeleteRemoved
//------------------------------------------------------------------------------
void ToolchainStash::DeleteRemoved( const Array<uint64_t> & toolIds )
{
    DeleteToolchains( toolIds );
}

// Find
//------------------------------------------------------------------------------
ToolchainStash::Entry * ToolchainStash::Find( uint64_t toolId ) const
{
    for ( const Entry & entry : m_Entries )
    {
        if ( entry.m_ToolId == toolId )
        {
            return const_cast<Entry *>( &entry );
        }
    }
    return nullptr;
}

// GetNumPins
//------------------------------------------------------------------------------
uint32_t ToolchainStash::GetNumPins( uint64_t toolId ) const
{
    const Pin * pin = m_Pins.Find( toolId );
    return pin ? pin->m_Count : 0;
}

// Remove
//------------------------------------------------------------------------------
void ToolchainStash::Remove( Entry * entry, Array<uint64_t> & outToolIds )
{
    outToolIds.Append( entry->m_ToolId );
    m_Deleting.Append( entry->m_ToolId ); // Until its directory is gone, the id is not taken again
    m_Entries.Erase( entry );
}

// Trim
//------------------------------------------------------------------------------
void ToolchainStash::Trim( Array<uint64_t> & outToolIds )
{
    // One being received is kept: its client is still sending it
    const LruLimits limits = { kMaxToolchains, 0, ~(uint64_t)0 };
    Array<size_t> indices;
    SelectLeastRecentlyUsed( m_Entries.GetSize(),
                             limits,
                             [ this ]( size_t i ) { return m_Entries[ i ].m_LastUse; },
                             []( size_t ) { return (uint64_t)0; },
                             [ this ]( size_t i ) { return ( m_Entries[ i ].IsComplete() == false ) || ( GetNumPins( m_Entries[ i ].m_ToolId ) > 0 ); },
                             indices );
    Array<uint64_t> toolIds;
    toolIds.SetCapacity( indices.GetSize() );
    for ( const size_t i : indices )
    {
        toolIds.Append( m_Entries[ i ].m_ToolId );
    }
    for ( const uint64_t toolId : toolIds )
    {
        Remove( Find( toolId ), outToolIds );
    }
}

// GetToolchainDirectory
//------------------------------------------------------------------------------
void ToolchainStash::GetToolchainDirectory( uint64_t toolId, AString & outDirectory ) const
{
    outDirectory = m_Directory;
    outDirectory.AppendFormat( "%s%016" PRIx64 "%c", CacheLayout::kToolchainDirPrefix, toolId, (char)NATIVE_SLASH );
}

// GetSlotPath
//------------------------------------------------------------------------------
void ToolchainStash::GetSlotPath( uint64_t toolId, uint32_t slot, AString & outPath ) const
{
    GetToolchainDirectory( toolId, outPath );
    if ( slot == kManifestFileId )
    {
        outPath += kManifestFileName;
    }
    else
    {
        outPath.AppendFormat( "file.%u.bin", slot );
    }
}

// DeleteToolchains
//------------------------------------------------------------------------------
void ToolchainStash::DeleteToolchains( const Array<uint64_t> & toolIds )
{
    if ( m_BeforeDelete && ( toolIds.IsEmpty() == false ) )
    {
        m_BeforeDelete( m_BeforeDeleteUserData );
    }
    for ( const uint64_t toolId : toolIds )
    {
        AStackString<> directory;
        GetToolchainDirectory( toolId, directory );
        directory.Trim( 0, 1 ); // The slash
        CacheLayout::DeleteDirectoryTree( directory );

        MutexHolder mh( m_Mutex );
        m_Deleting.FindAndErase( toolId );
    }
}

// Scan
//------------------------------------------------------------------------------
void ToolchainStash::Scan()
{
    Array<AString> patterns;
    patterns.EmplaceBack( "*" );
    Array<FileIO::FileInfo> files;
    if ( FileIO::GetFilesEx( m_Directory, &patterns, true, &files ) == false )
    {
        return;
    }

    // Group what is there by toolchain directory (<directory>toolchain.<id hex>/<file>)
    Array<ScannedToolchain> found;
    for ( const FileIO::FileInfo & info : files )
    {
        const AString & name = info.m_Name;
        if ( name.BeginsWith( m_Directory ) == false )
        {
            continue;
        }
        const char * dirBegin = ( name.Get() + m_Directory.GetLength() );
        const char * dirEnd = dirBegin;
        while ( ( *dirEnd != '\0' ) && ( *dirEnd != NATIVE_SLASH ) )
        {
            ++dirEnd;
        }
        uint64_t toolId;
        if ( ( *dirEnd == '\0' ) ||
             ( AString::StrNCmp( dirBegin, CacheLayout::kToolchainDirPrefix, CacheLayout::kToolchainDirPrefixLength ) != 0 ) ||
             ( CacheLayout::ParseHex64( dirBegin + CacheLayout::kToolchainDirPrefixLength, dirEnd, toolId ) == false ) )
        {
            continue;
        }
        const AStackString<> fileName( dirEnd + 1 );

        ScannedToolchain * toolchain = nullptr;
        for ( ScannedToolchain & t : found )
        {
            if ( t.m_ToolId == toolId )
            {
                toolchain = &t;
            }
        }
        if ( toolchain == nullptr )
        {
            toolchain = &found.EmplaceBack();
            toolchain->m_ToolId = toolId;
        }

        uint32_t fileId = 0;
        if ( fileName.EndsWith( kTempExtension ) )
        {
            FileIO::FileDelete( name.Get() );
        }
        else if ( fileName == kManifestFileName )
        {
            toolchain->m_HasManifest = true;
            toolchain->m_ManifestSize = (uint32_t)info.m_Size;
            toolchain->m_ManifestTime = info.m_LastWriteTime;
        }
        else if ( ParseFileId( fileName, fileId ) )
        {
            toolchain->m_FileIds.Append( fileId );
            toolchain->m_FileSizes.Append( (uint32_t)info.m_Size );
        }
    }

    Array<uint64_t> times; // Of each held toolchain's manifest, parallel to m_Entries
    Array<uint64_t> unusable;
    for ( const ScannedToolchain & toolchain : found )
    {
        Entry entry;
        bool complete = false;
        uint32_t numFiles = 0;
        if ( toolchain.m_HasManifest )
        {
            AStackString<> path;
            GetSlotPath( toolchain.m_ToolId, kManifestFileId, path );
            Array<char> bytes;
            FileStream f;
            if ( f.Open( path.Get() ) )
            {
                bytes.SetSize( (size_t)f.GetFileSize() );
                if ( f.ReadBuffer( bytes.Begin(), bytes.GetSize() ) != bytes.GetSize() )
                {
                    bytes.Clear();
                }
            }
            complete = ( bytes.IsEmpty() == false ) &&
                       ReadManifest( bytes.Begin(), bytes.GetSize(), toolchain.m_ToolId, numFiles );
        }
        if ( complete )
        {
            entry.m_ToolId = toolchain.m_ToolId;
            entry.m_NumFiles = numFiles;
            entry.m_ManifestState = SLOT_HELD;
            entry.m_ManifestSize = toolchain.m_ManifestSize;
            entry.m_FileStates.SetSize( numFiles );
            entry.m_FileSizes.SetSize( numFiles );
            for ( uint32_t i = 0; i < numFiles; ++i )
            {
                entry.m_FileStates[ i ] = SLOT_EMPTY;
                entry.m_FileSizes[ i ] = 0;
            }
            for ( size_t i = 0; i < toolchain.m_FileIds.GetSize(); ++i )
            {
                const uint32_t fileId = toolchain.m_FileIds[ i ];
                if ( ( fileId < numFiles ) && ( entry.m_FileStates[ fileId ] == SLOT_EMPTY ) )
                {
                    entry.m_FileStates[ fileId ] = SLOT_HELD;
                    entry.m_FileSizes[ fileId ] = toolchain.m_FileSizes[ i ];
                    ++entry.m_NumFilesHeld;
                }
            }
            complete = entry.IsComplete();
        }

        if ( complete )
        {
            m_Entries.Append( entry );
            times.Append( toolchain.m_ManifestTime );
        }
        else
        {
            unusable.Append( toolchain.m_ToolId );
        }
    }
    DeleteToolchains( unusable );

    // The one written longest ago counts as the least recently used
    Array<size_t> order;
    for ( size_t i = 0; i < m_Entries.GetSize(); ++i )
    {
        order.Append( i );
    }
    order.Sort( [ &times ]( size_t a, size_t b ) { return times[ a ] < times[ b ]; } );
    for ( const size_t i : order )
    {
        m_Entries[ i ].m_LastUse = ++m_Clock;
    }

    Array<uint64_t> toDelete;
    Trim( toDelete );
    DeleteToolchains( toDelete );
}

//------------------------------------------------------------------------------
