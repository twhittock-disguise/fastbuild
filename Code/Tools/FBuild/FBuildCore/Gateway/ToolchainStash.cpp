// ToolchainStash.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "ToolchainStash.h"

// Core
#include "Core/Env/Assert.h"
#include "Core/Process/Thread.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Strings/AStackString.h"

#include <inttypes.h>

// Defines
//------------------------------------------------------------------------------
namespace
{
    const char * const kToolchainDirPrefix = "toolchain.";
    const size_t kToolchainDirPrefixLength = 10;
    const char * const kManifestFileName = "manifest.bin";
    const char * const kTempExtension = ".tmp";

    // A manifest claiming more files than this is not one a client has
    constexpr uint32_t kMaxFilesPerToolchain = 65536;

    // How long a manifest waits for the directory of the same toolchain to finish being deleted
    constexpr uint32_t kMaxDeleteWaitMs = 30000;

    // The part of a manifest the stash reads (ToolManifest::DeserializeFromRemote)
    bool ReadManifestHeader( const void * payload, size_t payloadSize, uint64_t toolId, uint32_t & outNumFiles )
    {
        ConstMemoryStream ms( payload, payloadSize );
        uint64_t id;
        AStackString mainExecutablePath;
        uint32_t numFiles( 0 );
        if ( !ms.Read( id ) ||
             !ms.Read( mainExecutablePath ) ||
             !ms.Read( numFiles ) ||
             ( AString::StrLen( mainExecutablePath.Get() ) != mainExecutablePath.GetLength() ) ||
             ( numFiles == 0 ) ||
             ( numFiles > kMaxFilesPerToolchain ) ||
             ( id != toolId ) )
        {
            return false;
        }
        outNumFiles = numFiles;
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

    bool ParseHex( const char * begin, const char * end, uint64_t & outValue )
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
    if ( ReadManifestHeader( payload, payloadSize, toolId, numFiles ) == false )
    {
        return STASH_INVALID;
    }
    return Write( toolId, kManifestSlot, numFiles, payload, payloadSize );
}

// AddFile
//------------------------------------------------------------------------------
ToolchainStash::Result ToolchainStash::AddFile( uint64_t toolId, uint32_t fileId, const void * payload, size_t payloadSize )
{
    if ( fileId == kManifestSlot )
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
    if ( slot == kManifestSlot )
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
    else if ( ( entry == nullptr ) || ( slot >= entry->m_NumFiles ) )
    {
        return STASH_INVALID; // A file with no manifest to say it belongs
    }
    entry->m_LastUse = ++m_Clock;

    SlotState & state = ( slot == kManifestSlot ) ? entry->m_ManifestState : entry->m_FileStates[ slot ];
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
    for ( uint32_t attempt = 0;; ++attempt )
    {
        bool claimed = false;
        bool mustWait = false;
        const Result early = Claim( toolId, slot, numFiles, toDelete, claimed, mustWait );
        DeleteToolchains( toDelete );
        toDelete.Clear();
        if ( mustWait )
        {
            // The toolchain was just evicted and its directory is still being deleted
            if ( attempt >= kMaxDeleteWaitMs )
            {
                return STASH_INVALID;
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

    Result result = STASH_INVALID;
    {
        MutexHolder mh( m_Mutex );
        Entry * entry = Find( toolId );
        ASSERT( entry && ( entry->m_NumWrites > 0 ) );
        --entry->m_NumWrites;
        SlotState & state = ( slot == kManifestSlot ) ? entry->m_ManifestState : entry->m_FileStates[ slot ];
        if ( written )
        {
            state = SLOT_HELD;
            if ( slot == kManifestSlot )
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
            result = written ? STASH_INCOMPLETE : STASH_INVALID;
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
    GetSlotPath( toolId, kManifestSlot, outPath );
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
    for ( size_t i = 0; i < m_PinIds.GetSize(); ++i )
    {
        if ( m_PinIds[ i ] == toolId )
        {
            ++m_PinCounts[ i ];
            return;
        }
    }
    m_PinIds.Append( toolId );
    m_PinCounts.Append( 1 );
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
        for ( size_t i = 0; i < m_PinIds.GetSize(); ++i )
        {
            if ( m_PinIds[ i ] == toolId )
            {
                if ( --m_PinCounts[ i ] == 0 )
                {
                    m_PinIds.EraseIndex( i );
                    m_PinCounts.EraseIndex( i );
                }
                Trim( toDelete );
                break;
            }
        }
    }
    DeleteToolchains( toDelete );
}

// DropIncomplete
//------------------------------------------------------------------------------
void ToolchainStash::DropIncomplete( uint64_t toolId )
{
    Array<uint64_t> toDelete;
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
            Remove( entry, toDelete );
        }
    }
    DeleteToolchains( toDelete );
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
    for ( size_t i = 0; i < m_PinIds.GetSize(); ++i )
    {
        if ( m_PinIds[ i ] == toolId )
        {
            return m_PinCounts[ i ];
        }
    }
    return 0;
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
    while ( m_Entries.GetSize() > kMaxToolchains )
    {
        Entry * oldest = nullptr;
        for ( Entry & entry : m_Entries )
        {
            // One being received is kept: its client is still sending it
            if ( entry.IsComplete() && ( GetNumPins( entry.m_ToolId ) == 0 ) && ( ( oldest == nullptr ) || ( entry.m_LastUse < oldest->m_LastUse ) ) )
            {
                oldest = &entry;
            }
        }
        if ( oldest == nullptr )
        {
            return;
        }
        Remove( oldest, outToolIds );
    }
}

// GetToolchainDirectory
//------------------------------------------------------------------------------
void ToolchainStash::GetToolchainDirectory( uint64_t toolId, AString & outDirectory ) const
{
    outDirectory = m_Directory;
    outDirectory.AppendFormat( "%s%016" PRIx64 "%c", kToolchainDirPrefix, toolId, (char)NATIVE_SLASH );
}

// GetSlotPath
//------------------------------------------------------------------------------
void ToolchainStash::GetSlotPath( uint64_t toolId, uint32_t slot, AString & outPath ) const
{
    GetToolchainDirectory( toolId, outPath );
    if ( slot == kManifestSlot )
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
        Array<AString> files;
        FileIO::GetFiles( directory, AStackString<>( "*" ), false, &files );
        for ( const AString & file : files )
        {
            FileIO::FileDelete( file.Get() );
        }
        directory.Trim( 0, 1 ); // The slash
        FileIO::DirectoryDelete( directory );

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
             ( AString::StrNCmp( dirBegin, kToolchainDirPrefix, kToolchainDirPrefixLength ) != 0 ) ||
             ( ParseHex( dirBegin + kToolchainDirPrefixLength, dirEnd, toolId ) == false ) )
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
            GetSlotPath( toolchain.m_ToolId, kManifestSlot, path );
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
                       ReadManifestHeader( bytes.Begin(), bytes.GetSize(), toolchain.m_ToolId, numFiles );
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
