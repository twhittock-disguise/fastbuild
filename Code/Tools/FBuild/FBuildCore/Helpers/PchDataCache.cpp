// PchDataCache.cpp - Coordinator-side metadata cache for PCH distribution
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "PchDataCache.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"

// Core
#include "Core/Containers/UniquePtr.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Mem/Mem.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Mutex.h"
#include "Core/Process/Thread.h"

// Static
//------------------------------------------------------------------------------
static PchDataCache s_PchDataCache;

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
        // stack is enough as the file is read and compressed through heap buffers
        m_Thread.Start( ThreadFuncStatic, "PchCompress", this, kStackSize );
    }
    ~CompressedPch()
    {
        RequestCancel();
        m_Thread.Join();
    }

    // Non-copyable
    explicit CompressedPch( const CompressedPch & other ) = delete;
    CompressedPch & operator=( const CompressedPch & other ) = delete;

    void RequestCancel() { m_Cancel.Store( true ); }

    uint64_t GetPchId() const { return m_Entry.pchId; }
    CompressedState GetState() const { return static_cast<CompressedState>( m_State.Load() ); }
    uint32_t GetFailedAttempts() const { return m_FailedAttempts.Load(); }
    const void * GetData() const { return m_Compressor.GetResult(); }
    size_t GetSize() const { return m_Compressor.GetResultSize(); }

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

        FileStream file;
        if ( file.Open( m_Entry.filePath.Get(), FileStream::READ_ONLY ) == false )
        {
            FLOG_WARN( "PCH distribution: failed to open '%s' for sending\n", m_Entry.filePath.Get() );
            return false;
        }

        // Reading past the end of a file that changed since it was recorded does not stop on Windows
        const uint32_t fileSize = m_Entry.uncompressedSize;
        if ( file.GetFileSize() != fileSize )
        {
            FLOG_WARN( "PCH distribution: '%s' is not the recorded size\n", m_Entry.filePath.Get() );
            return false;
        }

        UniquePtr<char, FreeDeletor> data( static_cast<char *>( ALLOC( fileSize ) ) );
        if ( file.Read( data.Get(), fileSize ) != fileSize )
        {
            FLOG_WARN( "PCH distribution: failed to read '%s' for sending\n", m_Entry.filePath.Get() );
            return false;
        }
        file.Close();

        // The result is valid whether or not compression helped
        (void)m_Compressor.CompressZstd( data.Get(), fileSize, Compressor::kZstdTransferLevel );
        return true;
    }

    Entry m_Entry;
    PchDataCache & m_Owner;
    Thread m_Thread;
    Atomic<uint32_t> m_State{ COMPRESSING };
    Atomic<uint32_t> m_FailedAttempts{ 0 };
    Atomic<bool> m_Cancel{ false };
    Compressor m_Compressor;
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

// GetCompressed
//------------------------------------------------------------------------------
PchDataCache::CompressedState PchDataCache::GetCompressed( const Entry & entry,
                                                           const void *& outData,
                                                           size_t & outSize )
{
    MutexHolder mh( m_Mutex );

    for ( const CompressedPch * c : m_Compressed )
    {
        if ( c->GetPchId() == entry.pchId )
        {
            const CompressedState state = c->GetState();
            if ( state == READY )
            {
                outData = c->GetData();
                outSize = c->GetSize();
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
                                  Array<uint64_t> & sent,
                                  Array<ReadyPch> & outReady )
{
    bool allOk = true;

    for ( size_t i = pending.GetSize(); i > 0; --i )
    {
        const uint64_t pchId = pending[ i - 1 ];

        // The worker may have reported it since it was queued
        const bool alreadySent = ( sent.Find( pchId ) != nullptr );

        Entry entry;
        if ( ( alreadySent == false ) && Find( pchId, entry ) )
        {
            const void * data = nullptr;
            size_t dataSize = 0;
            switch ( GetCompressed( entry, data, dataSize ) )
            {
                case COMPRESSING:
                    continue;
                case READY:
                    sent.Append( pchId );
                    outReady.Append( ReadyPch{ pchId, entry.uncompressedSize, data, dataSize } );
                    break;
                case FAILED:
                    allOk = false;
                    break;
            }
        }
        pending.EraseIndex( i - 1 );
    }
    return allOk;
}

// Clear
//------------------------------------------------------------------------------
void PchDataCache::Clear()
{
    MutexHolder mh( m_Mutex );
    m_Entries.Clear();

    // Stop queued compressions before waiting on any of them. One already running
    // finishes first, as compression cannot be interrupted.
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

//------------------------------------------------------------------------------
