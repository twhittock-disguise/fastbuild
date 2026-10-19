// ToolchainStash - The toolchains clients sent a gateway, kept as they arrived
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Array.h"
#include "Core/Process/Mutex.h"
#include "Core/Strings/AString.h"

// ToolchainStash
//------------------------------------------------------------------------------
// The payloads of a toolchain's messages, under <directory>toolchain.<id hex>/
// (manifest.bin, file.<n>.bin; each written through a temporary file and renamed).
// A toolchain is complete once its manifest and all its files are held. Nothing is
// unpacked or decompressed. Thread safe; disk access happens outside the index lock.
//------------------------------------------------------------------------------
class ToolchainStash
{
public:
    static constexpr uint32_t kMaxToolchains = 16;   // Unpinned ones beyond this are evicted, least recently used first

    // Takes up what an earlier run left: complete toolchains are held, partial ones removed
    explicit ToolchainStash( const AString & directory );
    ~ToolchainStash();
    explicit ToolchainStash( const ToolchainStash & other ) = delete;
    ToolchainStash & operator=( const ToolchainStash & other ) = delete;

    enum Result { STASH_INCOMPLETE, STASH_COMPLETE, STASH_INVALID };
    Result AddManifest( uint64_t toolId, const void * payload, size_t payloadSize ); // A second manifest for a held toolchain is accepted and ignored
    Result AddFile( uint64_t toolId, uint32_t fileId, const void * payload, size_t payloadSize ); // A second copy of a held file is accepted and ignored

    bool IsComplete( uint64_t toolId ) const;
    bool HasManifest( uint64_t toolId ) const;
    void GetComplete( Array<uint64_t> & outToolIds ) const;              // Most recently used first
    void GetMissingFiles( uint64_t toolId, Array<uint32_t> & outFileIds ) const;
    bool GetManifestFile( uint64_t toolId, AString & outPath, uint32_t & outSize ) const;
    bool GetFile( uint64_t toolId, uint32_t fileId, AString & outPath, uint32_t & outSize ) const;
    uint32_t GetNumFiles( uint64_t toolId ) const;

    void AddUse( uint64_t toolId );      // Pinned: an advertised toolchain per client connection, or a held job's
    void ReleaseUse( uint64_t toolId );
    void DropIncomplete( uint64_t toolId );

    // Called, with no lock held, before the directories of removed toolchains are deleted (tests stall it)
    void SetBeforeDeleteHook( void ( *hook )( void * ), void * userData ) { m_BeforeDelete = hook; m_BeforeDeleteUserData = userData; }

private:
    enum SlotState : uint8_t { SLOT_EMPTY, SLOT_WRITING, SLOT_HELD };

    struct Entry
    {
        uint64_t            m_ToolId = 0;
        uint32_t            m_NumFiles = 0;
        SlotState           m_ManifestState = SLOT_EMPTY;
        uint32_t            m_ManifestSize = 0;
        Array<SlotState>    m_FileStates;
        Array<uint32_t>     m_FileSizes;
        uint32_t            m_NumFilesHeld = 0;
        uint32_t            m_NumWrites = 0;        // In progress, outside the lock
        uint32_t            m_NumUses = 0;
        uint64_t            m_LastUse = 0;
        bool                m_DropWhenIdle = false;

        bool IsComplete() const { return ( m_ManifestState == SLOT_HELD ) && ( m_NumFilesHeld == m_NumFiles ); }
    };

    static constexpr uint32_t kManifestSlot = 0xFFFFFFFF;

    Result Write( uint64_t toolId, uint32_t slot, uint32_t numFiles, const void * payload, size_t payloadSize );
    Result Claim( uint64_t toolId, uint32_t slot, uint32_t numFiles, Array<uint64_t> & outToDelete, bool & outClaimed, bool & outMustWait );
    uint32_t GetNumPins( uint64_t toolId ) const;
    Entry * Find( uint64_t toolId ) const;
    void Remove( Entry * entry, Array<uint64_t> & outToolIds );
    void Trim( Array<uint64_t> & outToolIds );
    void GetToolchainDirectory( uint64_t toolId, AString & outDirectory ) const;
    void GetSlotPath( uint64_t toolId, uint32_t slot, AString & outPath ) const;
    void DeleteToolchains( const Array<uint64_t> & toolIds );
    void Scan();

    AString             m_Directory;    // With a trailing slash
    mutable Mutex       m_Mutex;
    Array<Entry>        m_Entries;
    Array<uint64_t>     m_PinIds;       // Pins are per toolchain id, and outlive the entry
    Array<uint32_t>     m_PinCounts;
    Array<uint64_t>     m_Deleting;     // Removed toolchains whose directory is not yet deleted
    mutable uint64_t    m_Clock = 0;
    void ( *m_BeforeDelete )( void * ) = nullptr;
    void *              m_BeforeDeleteUserData = nullptr;
};

//------------------------------------------------------------------------------
