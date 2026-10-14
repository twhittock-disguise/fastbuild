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
    explicit CompressedPch( const Entry & entry )
        : m_Entry( entry )
    {
        m_Thread.Start( ThreadFuncStatic, "PchCompress", this, kStackSize );
    }
    ~CompressedPch() { m_Thread.Join(); }

    uint64_t GetPchId() const { return m_Entry.pchId; }
    CompressedState GetState() const { return static_cast<CompressedState>( m_State.Load() ); }
    const void * GetData() const { return m_Compressor.GetResult(); }
    size_t GetSize() const { return m_Compressor.GetResultSize(); }

private:
    inline static const uint32_t kStackSize = ( 512 * 1024 );

    static uint32_t ThreadFuncStatic( void * param )
    {
        CompressedPch * c = static_cast<CompressedPch *>( param );
        c->m_State.Store( c->Compress() ? READY : FAILED );
        return 0;
    }

    bool Compress()
    {
        FileStream file;
        if ( file.Open( m_Entry.filePath.Get(), FileStream::READ_ONLY ) == false )
        {
            FLOG_WARN( "PCH distribution: failed to open '%s' for sending\n", m_Entry.filePath.Get() );
            return false;
        }

        const uint32_t fileSize = m_Entry.uncompressedSize;
        UniquePtr<char, FreeDeletor> data( static_cast<char *>( ALLOC( fileSize ) ) );
        if ( file.Read( data.Get(), fileSize ) != fileSize )
        {
            FLOG_WARN( "PCH distribution: failed to read '%s' for sending\n", m_Entry.filePath.Get() );
            return false;
        }
        file.Close();

        m_Compressor.CompressZstd( data.Get(), fileSize, Compressor::kZstdTransferLevel );
        return true;
    }

    Entry m_Entry;
    Thread m_Thread;
    Atomic<uint32_t> m_State{ COMPRESSING };
    Compressor m_Compressor;
};

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

    for ( size_t i = 0; i < m_Compressed.GetSize(); ++i )
    {
        CompressedPch * c = m_Compressed[ i ];
        if ( c->GetPchId() != entry.pchId )
        {
            continue;
        }

        const CompressedState state = c->GetState();
        if ( state == READY )
        {
            outData = c->GetData();
            outSize = c->GetSize();
        }
        else if ( state == FAILED )
        {
            FDELETE c;
            m_Compressed.EraseIndex( i );
        }
        return state;
    }

    m_Compressed.Append( FNEW( CompressedPch( entry ) ) );
    return COMPRESSING;
}

// Clear
//------------------------------------------------------------------------------
void PchDataCache::Clear()
{
    MutexHolder mh( m_Mutex );
    m_Entries.Clear();
    for ( CompressedPch * c : m_Compressed )
    {
        FDELETE c;
    }
    m_Compressed.Clear();
}

//------------------------------------------------------------------------------
