// PchDataCache.h - Coordinator-side metadata cache for PCH distribution
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Array.h"
#include "Core/Env/Types.h"
#include "Core/Process/Atomic.h"
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
        uint64_t    pchId;              // PchId of the PCH content
        uint32_t    uncompressedSize;   // PCH file size
        AString     filePath;           // Absolute path to .pch on coordinator
    };

    // Store PCH metadata (thread-safe, skips if pchId already present)
    void Store( const Entry & entry );

    // Find a cached PCH by pchId (thread-safe, copies entry while mutex held)
    bool Find( uint64_t pchId, Entry & outEntry ) const;

    // Find a cached PCH by file path (thread-safe, copies entry while mutex held)
    bool FindByPath( const AString & filePath, Entry & outEntry ) const;

    // Records a PCH file (thread-safe). The first thread to ask for a path reads and hashes it,
    // in pieces. The others wait for that and use its result, so a file is read once.
    // Returns false if it cannot be read.
    bool Record( const AString & filePath, Entry & outEntry );

    enum CompressedState
    {
        COMPRESSING,
        READY,
        FAILED, // gave up after the maximum number of attempts
    };

    // Compressed copy of a PCH for sending to workers, kept in a temporary file. The first request
    // starts compression on a background thread, however many connections ask. Reading or compressing
    // is retried after a short delay when it fails. When READY, the file stays until Clear().
    CompressedState GetCompressed( const Entry & entry, AString & outFilePath, uint64_t & outFileSize );
    uint32_t GetFailedAttempts( uint64_t pchId ) const;
    bool HasCompressionFailed( uint64_t pchId ) const;
    size_t GetNumCompressions() const; // PCHs for which compression was started

    struct ReadyPch
    {
        uint64_t        pchId;
        uint32_t        uncompressedSize;
        AString         filePath;   // The compressed data, in the format of Compressor
        uint64_t        dataSize;   // Size of that file
    };

    // Resolves the PCH ids a connection needs to send. Ids the worker already has, and ids
    // with no entry, are dropped from pending. READY ones are dropped and returned in outReady,
    // in the order they were pending. Ones still compressing are skipped and stay pending, so a
    // slow one does not hold up the rest. Returns false if any failed for good (those are dropped).
    bool TakeReadyPchs( Array<uint64_t> & pending,
                        const Array<uint64_t> & onWorker,
                        Array<ReadyPch> & outReady );

    // Clear all cached entries and delete their compressed files (call at build end)
    void Clear();

    // Where compressed files are written, by default the temporary directory. Set before any is.
    void SetTempDir( const AString & dir );

    // Compressed files left by a crashed process (not this one) are deleted once they are old
    static void RemoveStaleTempFiles( const AString & dir );

    static const uint32_t kMaxCompressAttempts = 3;
    static const uint32_t kRetryDelayMS = 200;
    static const uint64_t kStaleTempFileSeconds = ( 60 * 60 );

    // Name of a compressed file: <prefix><process id>.<number>.zst
    static const char * const kTempFilePrefix;

private:
    class CompressedPch;
    class HashInFlight;

    bool MakeTempPath( AString & outPath );

    // PCHs compress one at a time to bound memory and CPU
    Mutex m_CompressMutex;

    const uint32_t m_MaxCompressAttempts;
    const uint32_t m_RetryDelayMS;

    mutable Mutex m_Mutex;
    Array<Entry> m_Entries;
    Array<CompressedPch *> m_Compressed;
    Array<HashInFlight *> m_Hashing;
    AString m_TempDir;
    Atomic<bool> m_StaleFilesRemoved{ false };
};

//------------------------------------------------------------------------------
