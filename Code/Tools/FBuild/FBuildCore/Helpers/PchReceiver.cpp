// PchReceiver.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "PchReceiver.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchId.h"
#include "Tools/FBuild/FBuildCore/Helpers/TempFiles.h"

// Core
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Math/Conversions.h"
#include "Core/Math/xxHash.h"
#include "Core/Process/Atomic.h"
#include "Core/Strings/AStackString.h"

// Static
//------------------------------------------------------------------------------
namespace
{
    bool IsFileOfSize( const AString & file, uint32_t size )
    {
        FileStream f;
        return ( f.Open( file.Get() ) && ( f.GetFileSize() == size ) );
    }

    // The most a compressed PCH can be larger than the PCH, so a sender cannot make us store more
    uint64_t GetMaxCompressedSize( uint32_t uncompressedSize )
    {
        // Data that does not compress is stored with a small header
        return ( (uint64_t)uncompressedSize + ( uncompressedSize >> 7 ) + ( 64 * 1024 ) );
    }
}

// DESTRUCTOR
//------------------------------------------------------------------------------
PchReceiver::~PchReceiver()
{
    Abort();
}

// OnChunk
//------------------------------------------------------------------------------
PchReceiver::Result PchReceiver::OnChunk( const Protocol::MsgPchFile & msg,
                                          const void * data,
                                          size_t size,
                                          const AString & pchFile,
                                          bool & outAlreadyCached )
{
    outAlreadyCached = false;

    const uint64_t pchId = msg.GetPchId();
    const uint32_t index = msg.GetChunkIndex();

    if ( index == 0 )
    {
        if ( m_Active )
        {
            return Fail( pchId, "a PCH started before the one before it was complete" );
        }
        if ( ( msg.GetUncompressedSize() == 0 ) ||
             ( msg.GetUncompressedSize() > kMaxUncompressedSize ) ||
             ( msg.GetCompressedSize() == 0 ) ||
             ( msg.GetCompressedSize() > GetMaxCompressedSize( msg.GetUncompressedSize() ) ) ||
             ( msg.GetChunkSize() < Protocol::kMinPchChunkSize ) ||
             ( msg.GetChunkSize() > Protocol::kMaxPchChunkSize ) )
        {
            return Fail( pchId, "its sizes are not possible" );
        }

        m_Active = true;
        m_PchId = pchId;
        m_ReceivingPchId.Store( pchId );
        m_UncompressedSize = msg.GetUncompressedSize();
        m_CompressedSize = msg.GetCompressedSize();
        m_ChunkSize = msg.GetChunkSize();
        m_NextIndex = 0;

        // The id identifies the content, so a file of the right size is the PCH
        m_AlreadyCached = IsFileOfSize( pchFile, m_UncompressedSize );
    }
    else if ( ( m_Active == false ) ||
              ( pchId != m_PchId ) ||
              ( msg.GetUncompressedSize() != m_UncompressedSize ) ||
              ( msg.GetCompressedSize() != m_CompressedSize ) ||
              ( msg.GetChunkSize() != m_ChunkSize ) ||
              ( index != m_NextIndex ) )
    {
        return Fail( pchId, "a chunk is not the next one" );
    }

    if ( size != Protocol::MsgPchFile::GetChunkLength( m_CompressedSize, m_ChunkSize, index ) )
    {
        return Fail( pchId, "a chunk has the wrong size" );
    }

    if ( m_AlreadyCached == false )
    {
        // The buffer grows with what has arrived, never to what the sender says is coming
        const size_t oldSize = m_Compressed.GetSize();
        const size_t newSize = ( oldSize + size );
        if ( newSize > m_Compressed.GetCapacity() )
        {
            m_Compressed.SetCapacity( Math::Min<size_t>( Math::Max( newSize, m_Compressed.GetCapacity() * 2 ), m_CompressedSize ) );
        }
        m_Compressed.SetSize( newSize );
        memcpy( m_Compressed.Begin() + oldSize, data, size );
    }

    m_NumChunksReceived.Increment();
    ++m_NextIndex;
    if ( m_NextIndex < Protocol::MsgPchFile::GetNumChunks( m_CompressedSize, m_ChunkSize ) )
    {
        return CONTINUE;
    }
    return Finish( pchFile, outAlreadyCached );
}

// Abort
//------------------------------------------------------------------------------
void PchReceiver::Abort()
{
    Reset();
    EndReceiving();
}

// Reset
//------------------------------------------------------------------------------
void PchReceiver::Reset()
{
    Array<char> empty;
    m_Compressed.Swap( empty );

    // The file is never open here: it is closed before anything can fail
    if ( m_TempFile.IsEmpty() == false )
    {
        FileIO::FileDelete( m_TempFile.Get() );
        m_TempFile.Clear();
    }
    m_Active = false;
    m_AlreadyCached = false;
}

// Fail
//------------------------------------------------------------------------------
PchReceiver::Result PchReceiver::Fail( uint64_t pchId, const char * reason )
{
    FLOG_WARN( "Rejected the PCH 0x%016" PRIx64 ": %s\n", pchId, reason );
    Abort();
    return FAILED;
}

// Finish
//------------------------------------------------------------------------------
PchReceiver::Result PchReceiver::Finish( const AString & pchFile, bool & outAlreadyCached )
{
    if ( m_AlreadyCached )
    {
        outAlreadyCached = true;
        Reset();
        return COMPLETE;
    }

    Compressor decompressor;
    if ( ( Compressor::IsValidData( m_Compressed.Begin(), m_Compressed.GetSize() ) == false ) ||
         ( decompressor.Decompress( m_Compressed.Begin() ) == false ) )
    {
        return Fail( m_PchId, "its data cannot be decompressed" );
    }
    {
        Array<char> empty;
        m_Compressed.Swap( empty );
    }

    if ( decompressor.GetResultSize() != m_UncompressedSize )
    {
        return Fail( m_PchId, "its size is not the one announced" );
    }
    if ( PchId::Calc( decompressor.GetResult(), decompressor.GetResultSize() ) != m_PchId )
    {
        return Fail( m_PchId, "its content does not match its id" );
    }

    // Write it next to where it goes, and close it before anything can fail: a file that is open
    // cannot be deleted on Windows
    const char * lastSlash = pchFile.FindLast( NATIVE_SLASH );
    AStackString<> dir;
    if ( lastSlash )
    {
        dir.Assign( pchFile.Get(), lastSlash + 1 );
    }
    TempFiles::MakeName( dir, kTempFilePrefix, kTempFileExtension, m_TempFile );
    bool written = false;
    if ( FileIO::EnsurePathExistsForFile( pchFile ) )
    {
        FileStream f;
        written = ( f.Open( m_TempFile.Get(), FileStream::WRITE_ONLY ) &&
                    ( f.Write( decompressor.GetResult(), decompressor.GetResultSize() ) == decompressor.GetResultSize() ) );
    }
    if ( written == false )
    {
        return Fail( m_PchId, "it cannot be written" );
    }

    // Another connection may have finished the same PCH first, and a compile may be using it
    if ( IsFileOfSize( pchFile, m_UncompressedSize ) )
    {
        outAlreadyCached = true;
    }
    else if ( FileIO::FileMove( m_TempFile, pchFile ) == false )
    {
        if ( IsFileOfSize( pchFile, m_UncompressedSize ) == false )
        {
            return Fail( m_PchId, "it cannot be moved into place" );
        }
        outAlreadyCached = true;
    }

    Reset(); // Removes the temporary file if it was not moved
    return COMPLETE;
}

//------------------------------------------------------------------------------
