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
    // Returns false if it cannot be read or is too large to send.
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
    //
    // There are two forms of the copy: the PCH compressed as it is, and compressed after it was transformed
    // (see PchTransform), which is smaller. Only a worker that reads the second (allowTransform) is given it,
    // and it is only made when one asks, if it is worth it, and if it gives the PCH back. When it is not
    // made, the copy is the first form, and outTransformed says which one it is. A form made for one worker
    // is used for every one that wants it.
    CompressedState GetCompressed( const Entry & entry, AString & outFilePath, uint64_t & outFileSize, bool allowTransform = false, bool * outTransformed = nullptr );
    uint32_t GetFailedAttempts( uint64_t pchId ) const;
    bool HasCompressionFailed( uint64_t pchId ) const;
    size_t GetNumCompressions() const; // PCHs for which compression was started

    struct ReadyPch
    {
        uint64_t        pchId;
        uint32_t        uncompressedSize;
        AString         filePath;   // The compressed data, in the format of Compressor
        uint64_t        dataSize;   // Size of that file
        bool            transformed = false; // Its data is of the PCH after a transform (see PchTransform)
    };

    // Resolves the PCH ids a connection needs to send. Ids the worker already has, and ids
    // with no entry, are dropped from pending. READY ones are dropped and returned in outReady,
    // in the order they were pending. Ones still compressing are skipped and stay pending, so a
    // slow one does not hold up the rest. Returns false if any failed for good (those are dropped).
    // allowTransform is whether the worker reads a PCH that was transformed (see GetCompressed).
    bool TakeReadyPchs( Array<uint64_t> & pending,
                        const Array<uint64_t> & onWorker,
                        Array<ReadyPch> & outReady,
                        bool allowTransform = false );

    // Clear all cached entries and delete their compressed files (call at build end)
    void Clear();

    // The directory compressed files are written to: one of this user's, inside the temporary
    // directory. Files of crashed processes are removed from it when it is first used.
    bool GetSendDirectory( AString & outDir );

    // Test knobs: the temporary directory to use, and a function called by the thread that reads a
    // file to record it, after it has the right to. Set before the first compression or record.
    void SetTempDir( const AString & dir );
    typedef void ( *HashHook )( void * userData );
    void SetHashHook( HashHook hook, void * userData );
    void SetDamageTransform( bool damage ) { m_DamageTransform.Store( damage ); } // What a transform makes is damaged, as if it did not invert
    uint32_t GetNumFilesHashed() const { return m_NumFilesHashed.Load(); }

    static const uint32_t kMaxCompressAttempts = 3;
    static const uint32_t kRetryDelayMS = 200;

    // The name of a compressed file is <prefix><process id>.<number><extension>
    static const char * const kTempFilePrefix;
    static const char * const kTempFileExtension;

private:
    class CompressedPch;

    // PCHs compress one at a time to bound memory and CPU
    Mutex m_CompressMutex;

    // Held while a file is read to record it, so a file is read once
    Mutex m_RecordMutex;

    const uint32_t m_MaxCompressAttempts;
    const uint32_t m_RetryDelayMS;

    mutable Mutex m_Mutex;
    Array<Entry> m_Entries;
    Array<CompressedPch *> m_Compressed;
    AString m_TempDir;
    bool m_StaleFilesRemoved = false; // Under m_CompressMutex
    HashHook m_HashHook = nullptr;
    void * m_HashHookData = nullptr;
    Atomic<uint32_t> m_NumFilesHashed{ 0 };
    Atomic<bool> m_DamageTransform{ false };
};

//------------------------------------------------------------------------------
