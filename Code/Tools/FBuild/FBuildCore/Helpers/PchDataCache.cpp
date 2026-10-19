// PchDataCache.cpp - Coordinator-side metadata cache for PCH distribution
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "PchDataCache.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchBlob.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchId.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchTransform.h"
#include "Tools/FBuild/FBuildCore/Helpers/TempFiles.h"

// Core
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Math/Conversions.h"
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
    CompressedPch( const Entry & entry, PchDataCache & owner, bool wantTransform )
        : m_Entry( entry )
        , m_Owner( owner )
        , m_WantTransform( wantTransform )
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
    bool IsTransformed() const { return m_Transformed; } // Known when READY

    // Whether this copy is what a worker that does (allowTransform) or does not read a transformed PCH is given:
    // one made for the same kind of worker, or one made for a worker that reads it and that is the plain form
    // because the transform was not worth it or did not give the PCH back
    bool IsFormFor( bool allowTransform ) const
    {
        return ( ( m_WantTransform == allowTransform ) ||
                 ( ( allowTransform == false ) && ( GetState() == READY ) && ( m_Transformed == false ) ) );
    }
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

        // The transformed form if it is wanted and works out, else the plain one. Only a failure of the plain
        // one is a failure of the PCH: a worker that cannot read the transformed one is sent the plain one.
        uint64_t compressedFileSize = 0;
        bool transformed = ( m_WantTransform && WriteTransformed( tempPath, compressedFileSize ) );
        if ( m_Cancel.Load() )
        {
            return false;
        }
        if ( ( transformed == false ) && ( WriteBlob( tempPath, nullptr, compressedFileSize, transformed ) == false ) )
        {
            return false;
        }
        m_FilePath = tempPath;
        m_FileSize = compressedFileSize;
        m_Transformed = transformed;
        return true;
    }

    // The transformed form, which is checked to give the PCH back before it is used. False if it is not worth it, was
    // not made or is not right, with no file left that is not the plain form.
    bool WriteTransformed( const AString & tempPath, uint64_t & outSize )
    {
        uint32_t base = 0;
        if ( PchTransform::Choose( m_Entry.filePath.Get(), m_Entry.uncompressedSize, Compressor::kZstdPchLevel, &m_Cancel, base ) == false )
        {
            return false;
        }
        PchTransform::Encoder encoder( base );
        encoder.SetFault( static_cast<PchTransform::Encoder::Fault>( m_Owner.m_TransformFault.Load() ) );
        bool transformed = false;
        if ( ( WriteBlob( tempPath, &encoder, outSize, transformed ) == false ) || ( transformed == false ) )
        {
            return false;
        }
        if ( PchBlob::VerifyFile( tempPath.Get(), m_Entry.pchId, m_Entry.uncompressedSize, &m_Cancel ) == false )
        {
            if ( m_Cancel.Load() == false ) // A check that was stopped found nothing
            {
                FLOG_WARN( "PCH distribution: the transformed '%s' is not the PCH, so it is sent as it is\n", m_Entry.filePath.Get() );
            }
            FileIO::FileDelete( tempPath.Get() );
            return false;
        }
        return true;
    }

    // The file is read in pieces and must hash to the id it was recorded with: a worker rejects
    // data that does not, and would be disconnected and sent the same data again
    bool WriteBlob( const AString & tempPath, PchTransform::Encoder * transform, uint64_t & outSize, bool & outTransformed )
    {
        HashObserver observer;
        if ( Compressor::CompressZstdFile( m_Entry.filePath.Get(), m_Entry.uncompressedSize, tempPath.Get(),
                                           Compressor::kZstdPchLevel, &m_Cancel, observer, outSize, transform, &outTransformed, Compressor::kZstdPchWindowLog ) == false )
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
        return true;
    }

    Entry m_Entry;
    PchDataCache & m_Owner;
    const bool m_WantTransform;
    Thread m_Thread;
    Atomic<uint32_t> m_State{ COMPRESSING };
    Atomic<uint32_t> m_FailedAttempts{ 0 };
    Atomic<bool> m_Cancel{ false };
    AString m_FilePath;     // Set before the state is READY
    uint64_t m_FileSize = 0;
    bool m_Transformed = false; // Set before the state is READY
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
                                                           uint64_t & outFileSize,
                                                           bool allowTransform,
                                                           bool * outTransformed )
{
    MutexHolder mh( m_Mutex );

    for ( const CompressedPch * c : m_Compressed )
    {
        // There is a copy for each kind of worker. One that asks while the copy of the other kind is being made
        // is not made to wait for it: that is likely to be transformed, which it cannot read, so its own starts at once.
        if ( ( c->GetPchId() == entry.pchId ) && c->IsFormFor( allowTransform ) )
        {
            const CompressedState state = c->GetState();
            if ( state == READY )
            {
                outFilePath = c->GetFilePath();
                outFileSize = c->GetFileSize();
                if ( outTransformed )
                {
                    *outTransformed = c->IsTransformed();
                }
            }
            return state;
        }
    }

    m_Compressed.Append( FNEW( CompressedPch( entry, *this, allowTransform ) ) );
    return COMPRESSING;
}

// GetFailedAttempts
//------------------------------------------------------------------------------
uint32_t PchDataCache::GetFailedAttempts( uint64_t pchId ) const
{
    MutexHolder mh( m_Mutex );

    uint32_t numFailed = 0;
    for ( const CompressedPch * c : m_Compressed )
    {
        if ( c->GetPchId() == pchId )
        {
            numFailed = Math::Max( numFailed, c->GetFailedAttempts() );
        }
    }
    return numFailed;
}

// HasCompressionFailed
//------------------------------------------------------------------------------
bool PchDataCache::HasCompressionFailed( uint64_t pchId ) const
{
    MutexHolder mh( m_Mutex );

    for ( const CompressedPch * c : m_Compressed )
    {
        if ( ( c->GetPchId() == pchId ) && ( c->GetState() == FAILED ) )
        {
            return true;
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
            bool transformed = false;
            switch ( GetCompressed( entry, filePath, fileSize, true /*allowTransform*/, &transformed ) )
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
                    ready.transformed = transformed;
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
    Array<CompressedPch *> compressed;
    {
        MutexHolder mh( m_Mutex );
        m_Entries.Clear();
        compressed.Swap( m_Compressed );
    }

    // Stop queued compressions before waiting on any of them. One already running finishes the piece it is
    // on, then stops. Each deletes its file. The lock is not held while waiting: a compression takes it to
    // find where to write.
    for ( CompressedPch * c : compressed )
    {
        c->RequestCancel();
    }
    for ( CompressedPch * c : compressed )
    {
        FDELETE c;
    }
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
