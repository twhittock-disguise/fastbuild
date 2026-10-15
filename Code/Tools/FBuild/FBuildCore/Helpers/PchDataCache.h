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

    explicit PchDataCache( uint32_t maxCompressAttempts = kMaxCompressAttempts,
                           uint32_t retryDelayMS = kRetryDelayMS );
    ~PchDataCache();

    // Non-copyable
    explicit PchDataCache( const PchDataCache & other ) = delete;
    PchDataCache & operator=( const PchDataCache & other ) = delete;

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
        FAILED, // gave up after the maximum number of attempts
    };

    // Compressed copy of a PCH for sending to workers. The first request starts
    // compression on a background thread, however many connections ask. Reading
    // or compressing is retried after a short delay when it fails. When READY, the
    // data stays valid until Clear().
    CompressedState GetCompressed( const Entry & entry, const void *& outData, size_t & outSize );
    uint32_t GetFailedAttempts( uint64_t pchId ) const;
    bool HasCompressionFailed( uint64_t pchId ) const;
    size_t GetNumCompressions() const; // PCHs for which compression was started

    struct ReadyPch
    {
        uint64_t        pchId;
        uint32_t        uncompressedSize;
        const void *    data;
        size_t          dataSize;
    };

    // Resolves the PCH ids a connection needs to send. Ids the worker already has, and ids
    // with no entry, are dropped from pending. READY ones are dropped and returned in outReady,
    // in the order they were pending. Ones still compressing are skipped and stay pending, so a
    // slow one does not hold up the rest. Returns false if any failed for good (those are dropped).
    bool TakeReadyPchs( Array<uint64_t> & pending,
                        const Array<uint64_t> & onWorker,
                        Array<ReadyPch> & outReady );

    // Clear all cached entries (call at build end)
    void Clear();

    static const uint32_t kMaxCompressAttempts = 3;
    static const uint32_t kRetryDelayMS = 200;

private:
    class CompressedPch;

    // PCHs compress one at a time to bound memory and CPU
    Mutex m_CompressMutex;

    const uint32_t m_MaxCompressAttempts;
    const uint32_t m_RetryDelayMS;

    mutable Mutex m_Mutex;
    Array<Entry> m_Entries;
    Array<CompressedPch *> m_Compressed;
};

//------------------------------------------------------------------------------
