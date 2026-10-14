// PchDataCache.h - Coordinator-side metadata cache for PCH distribution
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Array.h"
#include "Core/Env/Types.h"
#include "Core/Process/Mutex.h"
#include "Core/Strings/AString.h"

// PchDataCache
//------------------------------------------------------------------------------
class PchDataCache
{
public:
    static PchDataCache & Get();

    struct Entry
    {
        uint64_t    pchId;              // xxHash64 of PCH content
        uint32_t    uncompressedSize;   // PCH file size
        AString     filePath;           // Absolute path to .pch on coordinator
    };

    // Store PCH metadata (thread-safe, skips if pchId already present)
    void Store( const Entry & entry );

    // Find a cached PCH by pchId (thread-safe, copies entry while mutex held)
    bool Find( uint64_t pchId, Entry & outEntry ) const;

    // Find a cached PCH by file path (thread-safe, copies entry while mutex held)
    bool FindByPath( const AString & filePath, Entry & outEntry ) const;

    enum CompressedState
    {
        COMPRESSING,
        READY,
        FAILED, // reported once, a later call retries
    };

    // Compressed copy of a PCH for sending to workers. Compression runs once on a
    // background thread however many connections ask. When READY, the data stays
    // valid until Clear().
    CompressedState GetCompressed( const Entry & entry, const void *& outData, size_t & outSize );

    // Clear all cached entries (call at build end)
    void Clear();

private:
    class CompressedPch;

    mutable Mutex m_Mutex;
    Array<Entry> m_Entries;
    Array<CompressedPch *> m_Compressed;
};

//------------------------------------------------------------------------------
