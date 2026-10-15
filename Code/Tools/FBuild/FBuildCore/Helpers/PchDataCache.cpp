// PchDataCache.cpp - Coordinator-side metadata cache for PCH distribution
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "PchDataCache.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchId.h"
#include "Tools/FBuild/FBuildCore/Helpers/TempFiles.h"

// Core
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Mem/Mem.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Mutex.h"
#include "Core/Process/Thread.h"
#include "Core/Strings/AStackString.h"

// Static
//------------------------------------------------------------------------------
static PchDataCache s_PchDataCache;

/*static*/ const char * const PchDataCache::kTempFilePrefix = "fbuild-pch-send.";
/*static*/ const char * const PchDataCache::kTempFileExtension = ".zst";

namespace
{
    // The id the data read so far hashes to
    PRAGMA_DISABLE_PUSH_MSVC( 4324 ) // structure was padded due to alignment specifier
    class HashObserver : public Compressor::FileObserver
    {
    public:
        virtual void OnPass() override { m_Hash = PchId::Accumulator(); }
        virtual void OnPiece( const void * data, size_t size ) override { PchId::AddData( m_Hash, data, size ); }
        uint64_t GetId() { return PchId::Finalize( m_Hash ); }

    private:
        PchId::Accumulator m_Hash;
    };
    PRAGMA_DISABLE_POP_MSVC // 4324
}

// CompressedPch
//------------------------------------------------------------------------------
class PchDataCache::CompressedPch
{
public:
    CompressedPch( const Entry & entry, PchDataCache & owner )
        : m_Entry( entry )
        , m_Owner( owner )
    {
        // A dedicated thread: build worker threads are busy compiling, and a small
        // stack is enough as the file is read and compressed in pieces
        m_Thread.Start( ThreadFuncStatic, "PchCompress", this, kStackSize );
    }
    ~CompressedPch()
    {
        RequestCancel();
        m_Thread.Join();
        if ( m_FilePath.IsEmpty() == false )
        {
            FileIO::FileDelete( m_FilePath.Get() );
        }
    }

    // Non-copyable
    explicit CompressedPch( const CompressedPch & other ) = delete;
    CompressedPch & operator=( const CompressedPch & other ) = delete;

    void RequestCancel() { m_Cancel.Store( true ); }

    uint64_t GetPchId() const { return m_Entry.pchId; }
    CompressedState GetState() const { return static_cast<CompressedState>( m_State.Load() ); }
    uint32_t GetFailedAttempts() const { return m_FailedAttempts.Load(); }
    const AString & GetFilePath() const { return m_FilePath; }
    uint64_t GetFileSize() const { return m_FileSize; }

private:
    inline static const uint32_t kStackSize = ( 512 * 1024 );

    static uint32_t ThreadFuncStatic( void * param )
    {
        static_cast<CompressedPch *>( param )->ThreadFunc();
        return 0;
    }

    void ThreadFunc()
    {
        for ( uint32_t attempt = 1; attempt <= m_Owner.m_MaxCompressAttempts; ++attempt )
        {
            if ( Compress() )
            {
                m_State.Store( READY );
                return;
            }
            if ( m_Cancel.Load() )
            {
                return;
            }
            m_FailedAttempts.Increment();

            if ( attempt < m_Owner.m_MaxCompressAttempts )
            {
                for ( uint32_t waited = 0; ( waited < m_Owner.m_RetryDelayMS ) && !m_Cancel.Load(); waited += 10 )
                {
                    Thread::Sleep( 10 );
                }
            }
        }

        FLOG_ERROR( "PCH distribution: giving up on '%s' after %u attempts\n",
                    m_Entry.filePath.Get(), m_Owner.m_MaxCompressAttempts );
        m_State.Store( FAILED );
    }

    bool Compress()
    {
        MutexHolder mh( m_Owner.m_CompressMutex );
        if ( m_Cancel.Load() )
        {
            return false;
        }

        AStackString<> dir;
        if ( m_Owner.GetSendDirectory( dir ) == false )
        {
            FLOG_WARN( "PCH distribution: no place to write the compressed '%s'\n", m_Entry.filePath.Get() );
            return false;
        }
        if ( m_Owner.m_StaleFilesRemoved == false )
        {
            m_Owner.m_StaleFilesRemoved = true;
            TempFiles::RemoveStale( dir, kTempFilePrefix, kTempFileExtension, false );
        }
        AStackString<> tempPath;
        TempFiles::MakeName( dir, kTempFilePrefix, kTempFileExtension, tempPath );

        // The file is read in pieces and must hash to the id it was recorded with: a worker rejects
        // data that does not, and would be disconnected and sent the same data again
        HashObserver observer;
        uint64_t compressedFileSize = 0;
        if ( Compressor::CompressZstdFile( m_Entry.filePath.Get(), m_Entry.uncompressedSize, tempPath.Get(),
                                           Compressor::kZstdTransferLevel, &m_Cancel, observer, compressedFileSize ) == false )
        {
            FLOG_WARN( "PCH distribution: failed to read and compress '%s' to '%s'\n", m_Entry.filePath.Get(), tempPath.Get() );
            FileIO::FileDelete( tempPath.Get() );
            return false;
        }
        if ( observer.GetId() != m_Entry.pchId )
        {
            FLOG_WARN( "PCH distribution: '%s' changed since it was recorded\n", m_Entry.filePath.Get() );
            FileIO::FileDelete( tempPath.Get() );
            return false;
        }
        m_FilePath = tempPath;
        m_FileSize = compressedFileSize;
        return true;
    }

    Entry m_Entry;
    PchDataCache & m_Owner;
    Thread m_Thread;
    Atomic<uint32_t> m_State{ COMPRESSING };
    Atomic<uint32_t> m_FailedAttempts{ 0 };
    Atomic<bool> m_Cancel{ false };
    AString m_FilePath;     // Set before the state is READY
    uint64_t m_FileSize = 0;
};

// CONSTRUCTOR
//------------------------------------------------------------------------------
PchDataCache::PchDataCache( uint32_t maxCompressAttempts, uint32_t retryDelayMS )
    : m_MaxCompressAttempts( maxCompressAttempts )
    , m_RetryDelayMS( retryDelayMS )
{
}

// DESTRUCTOR
//------------------------------------------------------------------------------
PchDataCache::~PchDataCache()
{
    Clear();
}

// Get
//------------------------------------------------------------------------------
/*static*/ PchDataCache & PchDataCache::Get()
{
    return s_PchDataCache;
}

// Store
//------------------------------------------------------------------------------
void PchDataCache::Store( const Entry & entry )
{
    MutexHolder mh( m_Mutex );

    // Skip if already stored
    for ( const Entry & e : m_Entries )
    {
        if ( e.pchId == entry.pchId )
        {
            return;
        }
    }

    m_Entries.Append( entry );
}

// Find
//------------------------------------------------------------------------------
bool PchDataCache::Find( uint64_t pchId, Entry & outEntry ) const
{
    MutexHolder mh( m_Mutex );

    for ( const Entry & e : m_Entries )
    {
        if ( e.pchId == pchId )
        {
            outEntry = e;
            return true;
        }
    }
    return false;
}

// FindByPath
//------------------------------------------------------------------------------
bool PchDataCache::FindByPath( const AString & filePath, Entry & outEntry ) const
{
    MutexHolder mh( m_Mutex );

    for ( const Entry & e : m_Entries )
    {
        if ( e.filePath == filePath )
        {
            outEntry = e;
            return true;
        }
    }
    return false;
}

// Record
//------------------------------------------------------------------------------
bool PchDataCache::Record( const AString & filePath, Entry & outEntry )
{
    // Only one thread reads a file. The others wait here, then find what it recorded.
    MutexHolder recordHolder( m_RecordMutex );
    if ( FindByPath( filePath, outEntry ) )
    {
        return true;
    }

    if ( m_HashHook )
    {
        m_HashHook( m_HashHookData );
    }
    m_NumFilesHashed.Increment();

    uint64_t pchId = 0;
    uint64_t fileSize = 0;
    if ( PchId::CalcFile( filePath, pchId, fileSize ) == false )
    {
        FLOG_WARN( "PCH distribution: failed to read '%s'\n", filePath.Get() );
        return false;
    }
    if ( ( fileSize == 0 ) || ( fileSize > Compressor::kMaxFileSize ) )
    {
        FLOG_WARN( "PCH distribution: '%s' cannot be sent: it is %" PRIu64 " bytes\n", filePath.Get(), fileSize );
        return false;
    }

    outEntry.pchId = pchId;
    outEntry.uncompressedSize = static_cast<uint32_t>( fileSize );
    outEntry.filePath = filePath;
    Store( outEntry );
    return true;
}

// GetCompressed
//------------------------------------------------------------------------------
PchDataCache::CompressedState PchDataCache::GetCompressed( const Entry & entry,
                                                           AString & outFilePath,
                                                           uint64_t & outFileSize )
{
    MutexHolder mh( m_Mutex );

    for ( const CompressedPch * c : m_Compressed )
    {
        if ( c->GetPchId() == entry.pchId )
        {
            const CompressedState state = c->GetState();
            if ( state == READY )
            {
                outFilePath = c->GetFilePath();
                outFileSize = c->GetFileSize();
            }
            return state;
        }
    }

    m_Compressed.Append( FNEW( CompressedPch( entry, *this ) ) );
    return COMPRESSING;
}

// GetFailedAttempts
//------------------------------------------------------------------------------
uint32_t PchDataCache::GetFailedAttempts( uint64_t pchId ) const
{
    MutexHolder mh( m_Mutex );

    for ( const CompressedPch * c : m_Compressed )
    {
        if ( c->GetPchId() == pchId )
        {
            return c->GetFailedAttempts();
        }
    }
    return 0;
}

// HasCompressionFailed
//------------------------------------------------------------------------------
bool PchDataCache::HasCompressionFailed( uint64_t pchId ) const
{
    MutexHolder mh( m_Mutex );

    for ( const CompressedPch * c : m_Compressed )
    {
        if ( c->GetPchId() == pchId )
        {
            return ( c->GetState() == FAILED );
        }
    }
    return false;
}

// GetNumCompressions
//------------------------------------------------------------------------------
size_t PchDataCache::GetNumCompressions() const
{
    MutexHolder mh( m_Mutex );
    return m_Compressed.GetSize();
}

// TakeReadyPchs
//------------------------------------------------------------------------------
bool PchDataCache::TakeReadyPchs( Array<uint64_t> & pending,
                                  const Array<uint64_t> & onWorker,
                                  Array<ReadyPch> & outReady )
{
    bool allOk = true;

    Array<uint64_t> stillPending;
    for ( const uint64_t pchId : pending )
    {
        // The worker may have reported it since it was requested
        const bool workerHasIt = ( onWorker.Find( pchId ) != nullptr );

        Entry entry;
        if ( ( workerHasIt == false ) && Find( pchId, entry ) )
        {
            AStackString<> filePath;
            uint64_t fileSize = 0;
            switch ( GetCompressed( entry, filePath, fileSize ) )
            {
                case COMPRESSING:
                    stillPending.Append( pchId );
                    break;
                case READY:
                {
                    ReadyPch ready;
                    ready.pchId = pchId;
                    ready.uncompressedSize = entry.uncompressedSize;
                    ready.filePath = filePath;
                    ready.dataSize = fileSize;
                    outReady.Append( Move( ready ) );
                    break;
                }
                case FAILED:
                    allOk = false;
                    break;
            }
        }
    }
    pending.Swap( stillPending );
    return allOk;
}

// Clear
//------------------------------------------------------------------------------
void PchDataCache::Clear()
{
    MutexHolder mh( m_Mutex );
    m_Entries.Clear();

    // Stop queued compressions before waiting on any of them. One already running
    // finishes the piece it is on, then stops. Each deletes its file.
    for ( CompressedPch * c : m_Compressed )
    {
        c->RequestCancel();
    }
    for ( CompressedPch * c : m_Compressed )
    {
        FDELETE c;
    }
    m_Compressed.Clear();
}

// GetSendDirectory
//------------------------------------------------------------------------------
bool PchDataCache::GetSendDirectory( AString & outDir )
{
    AStackString<> parent;
    {
        MutexHolder mh( m_Mutex );
        parent = m_TempDir;
    }
    if ( ( parent.IsEmpty() ) && ( FBuild::GetTempDir( parent ) == false ) )
    {
        return false;
    }
    PathUtils::EnsureTrailingSlash( parent );
    return TempFiles::GetPrivateDirectory( parent, "fbuild-pch-send", outDir );
}

// SetTempDir
//------------------------------------------------------------------------------
void PchDataCache::SetTempDir( const AString & dir )
{
    MutexHolder mh( m_Mutex );
    m_TempDir = dir;
}

// SetHashHook
//------------------------------------------------------------------------------
void PchDataCache::SetHashHook( HashHook hook, void * userData )
{
    m_HashHook = hook;
    m_HashHookData = userData;
}

//------------------------------------------------------------------------------
