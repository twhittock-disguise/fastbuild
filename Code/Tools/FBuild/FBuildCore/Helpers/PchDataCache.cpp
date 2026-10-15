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

// Core
#include "Core/Containers/UniquePtr.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Math/Conversions.h"
#include "Core/Mem/Mem.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Mutex.h"
#include "Core/Process/Process.h"
#include "Core/Process/Thread.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Time.h"

// External
#include "zstd.h"

#include <stdlib.h>
#include <string.h>

// Static
//------------------------------------------------------------------------------
static PchDataCache s_PchDataCache;

/*static*/ const char * const PchDataCache::kTempFilePrefix = "fbuild-pch-send.";

namespace
{
    const char * const kSendTempExtension = ".zst";

    // <prefix><process id>.<number>.zst
    bool ParseTempFileName( const char * fileName, uint32_t & outProcessId )
    {
        const AStackString<> path( fileName );
        const char * lastSlash = path.FindLast( NATIVE_SLASH );
        if ( lastSlash == nullptr )
        {
            lastSlash = path.FindLast( '/' );
        }
        const char * name = lastSlash ? ( lastSlash + 1 ) : path.Get();

        const size_t prefixLength = strlen( PchDataCache::kTempFilePrefix );
        if ( strncmp( name, PchDataCache::kTempFilePrefix, prefixLength ) != 0 )
        {
            return false;
        }
        const char * p = ( name + prefixLength );
        char * end = nullptr;
        const unsigned long processId = strtoul( p, &end, 10 );
        if ( ( end == p ) || ( *end != '.' ) )
        {
            return false;
        }
        p = ( end + 1 );
        strtoul( p, &end, 10 );
        if ( ( end == p ) || ( strcmp( end, kSendTempExtension ) != 0 ) )
        {
            return false;
        }
        outProcessId = static_cast<uint32_t>( processId );
        return true;
    }

    struct ZstdContext
    {
        ZstdContext() : m_Context( ZSTD_createCCtx() ) {}
        ~ZstdContext() { ZSTD_freeCCtx( m_Context ); }
        ZSTD_CCtx * m_Context;
    };
}

// HashInFlight
//------------------------------------------------------------------------------
class PchDataCache::HashInFlight
{
public:
    explicit HashInFlight( const AString & path ) : m_Path( path ) {}

    AString m_Path;
    Mutex   m_Mutex;    // Held while the file is read
    uint32_t m_Users = 0; // Under the cache's mutex
};

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

        FileStream source;
        if ( source.Open( m_Entry.filePath.Get(), FileStream::READ_ONLY ) == false )
        {
            FLOG_WARN( "PCH distribution: failed to open '%s' for sending\n", m_Entry.filePath.Get() );
            return false;
        }

        // Reading past the end of a file that changed since it was recorded does not stop on Windows
        const uint64_t fileSize = m_Entry.uncompressedSize;
        if ( ( fileSize == 0 ) || ( source.GetFileSize() != fileSize ) )
        {
            FLOG_WARN( "PCH distribution: '%s' is not the recorded size\n", m_Entry.filePath.Get() );
            return false;
        }

        AStackString<> tempPath;
        if ( ( m_Owner.MakeTempPath( tempPath ) == false ) || ( FileIO::EnsurePathExistsForFile( tempPath ) == false ) )
        {
            FLOG_WARN( "PCH distribution: no place to write the compressed '%s'\n", m_Entry.filePath.Get() );
            return false;
        }

        uint64_t compressedFileSize = 0;
        if ( CompressToFile( source, fileSize, tempPath, compressedFileSize ) == false )
        {
            FileIO::FileDelete( tempPath.Get() ); // All the files are closed by now
            return false;
        }
        m_FilePath = tempPath;
        m_FileSize = compressedFileSize;
        return true;
    }

    // Writes a header and one zstd frame, or the data itself if that is not smaller. The file is
    // read and written in pieces, and its content must hash to the id it was recorded with: a
    // worker rejects data that does not, and would be disconnected and sent the same data again.
    bool CompressToFile( FileStream & source, uint64_t fileSize, const AString & tempPath, uint64_t & outFileSize )
    {
        if ( fileSize > UINT32_MAX )
        {
            FLOG_WARN( "PCH distribution: '%s' is too large to send\n", m_Entry.filePath.Get() );
            return false;
        }

        uint64_t frameSize = 0;
        {
            FileStream out;
            char header[ Compressor::kHeaderSize ] = {};
            if ( ( out.Open( tempPath.Get(), FileStream::WRITE_ONLY ) == false ) ||
                 ( out.Write( header, sizeof( header ) ) != sizeof( header ) ) )
            {
                FLOG_WARN( "PCH distribution: failed to write '%s'\n", tempPath.Get() );
                return false;
            }

            ZstdContext zstd;
            if ( ( zstd.m_Context == nullptr ) ||
                 ZSTD_isError( ZSTD_CCtx_setParameter( zstd.m_Context, ZSTD_c_compressionLevel, Compressor::kZstdTransferLevel ) ) ||
                 ZSTD_isError( ZSTD_CCtx_setPledgedSrcSize( zstd.m_Context, fileSize ) ) )
            {
                return false;
            }

            UniquePtr<char, FreeDeletor> input( static_cast<char *>( ALLOC( PchId::kReadChunkSize ) ) );
            const size_t outputSize = ZSTD_CStreamOutSize();
            UniquePtr<char, FreeDeletor> output( static_cast<char *>( ALLOC( outputSize ) ) );

            PchId::Accumulator hash;
            for ( uint64_t read = 0; read < fileSize; )
            {
                if ( m_Cancel.Load() )
                {
                    return false;
                }
                const uint32_t chunk = static_cast<uint32_t>( Math::Min<uint64_t>( fileSize - read, PchId::kReadChunkSize ) );
                if ( ( source.GetFileSize() != fileSize ) || ( source.Read( input.Get(), chunk ) != chunk ) )
                {
                    FLOG_WARN( "PCH distribution: failed to read '%s' for sending\n", m_Entry.filePath.Get() );
                    return false;
                }
                PchId::AddData( hash, input.Get(), chunk );
                read += chunk;

                const bool last = ( read == fileSize );
                ZSTD_inBuffer in = { input.Get(), chunk, 0 };
                size_t remaining;
                do
                {
                    ZSTD_outBuffer produced = { output.Get(), outputSize, 0 };
                    remaining = ZSTD_compressStream2( zstd.m_Context, &produced, &in, last ? ZSTD_e_end : ZSTD_e_continue );
                    if ( ZSTD_isError( remaining ) ||
                         ( out.Write( output.Get(), (uint32_t)produced.pos ) != produced.pos ) )
                    {
                        FLOG_WARN( "PCH distribution: failed to compress '%s'\n", m_Entry.filePath.Get() );
                        return false;
                    }
                    frameSize += produced.pos;
                } while ( last ? ( remaining != 0 ) : ( in.pos < in.size ) );
            }

            if ( PchId::Finalize( hash ) != m_Entry.pchId )
            {
                FLOG_WARN( "PCH distribution: '%s' changed since it was recorded\n", m_Entry.filePath.Get() );
                return false;
            }

            if ( frameSize < fileSize )
            {
                // The header says how long the frame turned out to be
                Compressor::WriteHeader( header, true, static_cast<uint32_t>( fileSize ), static_cast<uint32_t>( frameSize ) );
                if ( ( out.Seek( 0 ) == false ) || ( out.Write( header, sizeof( header ) ) != sizeof( header ) ) )
                {
                    FLOG_WARN( "PCH distribution: failed to write '%s'\n", tempPath.Get() );
                    return false;
                }
                outFileSize = ( sizeof( header ) + frameSize );
                return true;
            }
        }

        return WriteUncompressed( fileSize, tempPath, outFileSize );
    }

    // Data that does not compress is sent as it is, with a header
    bool WriteUncompressed( uint64_t fileSize, const AString & tempPath, uint64_t & outFileSize )
    {
        FileStream source;
        FileStream out;
        char header[ Compressor::kHeaderSize ];
        Compressor::WriteHeader( header, false, static_cast<uint32_t>( fileSize ), static_cast<uint32_t>( fileSize ) );
        if ( ( source.Open( m_Entry.filePath.Get(), FileStream::READ_ONLY ) == false ) ||
             ( out.Open( tempPath.Get(), FileStream::WRITE_ONLY ) == false ) ||
             ( out.Write( header, sizeof( header ) ) != sizeof( header ) ) )
        {
            FLOG_WARN( "PCH distribution: failed to write '%s'\n", tempPath.Get() );
            return false;
        }

        UniquePtr<char, FreeDeletor> buffer( static_cast<char *>( ALLOC( PchId::kReadChunkSize ) ) );
        PchId::Accumulator hash;
        for ( uint64_t copied = 0; copied < fileSize; )
        {
            const uint32_t chunk = static_cast<uint32_t>( Math::Min<uint64_t>( fileSize - copied, PchId::kReadChunkSize ) );
            if ( ( source.GetFileSize() != fileSize ) ||
                 ( source.Read( buffer.Get(), chunk ) != chunk ) ||
                 ( out.Write( buffer.Get(), chunk ) != chunk ) )
            {
                FLOG_WARN( "PCH distribution: failed to copy '%s'\n", m_Entry.filePath.Get() );
                return false;
            }
            PchId::AddData( hash, buffer.Get(), chunk );
            copied += chunk;
        }
        if ( PchId::Finalize( hash ) != m_Entry.pchId )
        {
            FLOG_WARN( "PCH distribution: '%s' changed since it was recorded\n", m_Entry.filePath.Get() );
            return false;
        }
        outFileSize = ( sizeof( header ) + fileSize );
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
    if ( FindByPath( filePath, outEntry ) )
    {
        return true;
    }

    HashInFlight * hashing = nullptr;
    {
        MutexHolder mh( m_Mutex );
        for ( HashInFlight * h : m_Hashing )
        {
            if ( h->m_Path == filePath )
            {
                hashing = h;
                break;
            }
        }
        if ( hashing == nullptr )
        {
            hashing = FNEW( HashInFlight( filePath ) );
            m_Hashing.Append( hashing );
        }
        ++hashing->m_Users;
    }

    bool ok;
    {
        // Only one thread reads the file. The others wait here, then find what it recorded.
        MutexHolder fileHolder( hashing->m_Mutex );
        ok = FindByPath( filePath, outEntry );
        if ( ok == false )
        {
            uint64_t pchId = 0;
            uint64_t fileSize = 0;
            if ( PchId::CalcFile( filePath, pchId, fileSize ) && ( fileSize > 0 ) && ( fileSize <= UINT32_MAX ) )
            {
                outEntry.pchId = pchId;
                outEntry.uncompressedSize = static_cast<uint32_t>( fileSize );
                outEntry.filePath = filePath;
                Store( outEntry );
                ok = true;
            }
        }
    }

    {
        MutexHolder mh( m_Mutex );
        if ( --hashing->m_Users == 0 )
        {
            m_Hashing.FindAndErase( hashing );
            FDELETE hashing;
        }
    }
    return ok;
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

// SetTempDir
//------------------------------------------------------------------------------
void PchDataCache::SetTempDir( const AString & dir )
{
    MutexHolder mh( m_Mutex );
    m_TempDir = dir;
}

// MakeTempPath
//------------------------------------------------------------------------------
bool PchDataCache::MakeTempPath( AString & outPath )
{
    AStackString<> dir;
    {
        MutexHolder mh( m_Mutex );
        dir = m_TempDir;
    }
    if ( ( dir.IsEmpty() ) && ( FBuild::GetTempDir( dir ) == false ) )
    {
        return false;
    }
    PathUtils::EnsureTrailingSlash( dir );

    // Compressions run one at a time, so this is not raced
    if ( m_StaleFilesRemoved.Load() == false )
    {
        m_StaleFilesRemoved.Store( true );
        RemoveStaleTempFiles( dir );
    }

    static Atomic<uint32_t> s_Counter{ 0 };
    outPath = dir;
    outPath.AppendFormat( "%s%u.%u%s", kTempFilePrefix, Process::GetCurrentId(), s_Counter.Increment(), kSendTempExtension );
    return true;
}

// RemoveStaleTempFiles
//------------------------------------------------------------------------------
/*static*/ void PchDataCache::RemoveStaleTempFiles( const AString & dir )
{
    Array<FileIO::FileInfo> files;
    Array<AString> patterns;
    AStackString<> pattern( kTempFilePrefix );
    pattern += '*';
    patterns.Append( pattern );
    if ( FileIO::GetFilesEx( dir, &patterns, false /*recurse*/, &files ) == false )
    {
        return;
    }

    // Those of this process are in use, and those of another may be
    const uint64_t now = Time::FileTimeToSeconds( Time::GetCurrentFileTime() );
    const uint32_t thisProcess = Process::GetCurrentId();
    for ( const FileIO::FileInfo & file : files )
    {
        uint32_t processId = 0;
        if ( ( ParseTempFileName( file.m_Name.Get(), processId ) == false ) || ( processId == thisProcess ) )
        {
            continue;
        }
        const uint64_t lastWritten = Time::FileTimeToSeconds( file.m_LastWriteTime );
        if ( ( now > lastWritten ) && ( ( now - lastWritten ) >= kStaleTempFileSeconds ) )
        {
            FileIO::FileDelete( file.m_Name.Get() );
        }
    }
}

//------------------------------------------------------------------------------
