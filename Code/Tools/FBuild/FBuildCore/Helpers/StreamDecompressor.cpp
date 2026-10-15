// StreamDecompressor.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "StreamDecompressor.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/FileChunks.h"

// Core
#include "Core/FileIO/FileStream.h"
#include "Core/Math/Conversions.h"
#include "Core/Mem/Mem.h"
#include "Core/Process/Atomic.h"

// External
#include "zstd.h"

#include <string.h>

// Static
//------------------------------------------------------------------------------
namespace
{
    // What is decompressed is handed on in pieces of this size
    constexpr size_t kOutputSize = FileChunks::kChunkSize;
}

// CONSTRUCTOR
//------------------------------------------------------------------------------
StreamDecompressor::StreamDecompressor()
{
    memset( m_Header, 0, sizeof( m_Header ) );
}

// DESTRUCTOR
//------------------------------------------------------------------------------
StreamDecompressor::~StreamDecompressor()
{
    ZSTD_freeDCtx( static_cast<ZSTD_DCtx *>( m_Context ) );
    FREE( m_Output );
}

// Feed
//------------------------------------------------------------------------------
bool StreamDecompressor::Feed( const void * data, size_t size, Sink & sink )
{
    if ( m_Failed )
    {
        return false;
    }

    const char * bytes = static_cast<const char *>( data );
    if ( m_HeaderSize < Compressor::kHeaderSize )
    {
        const size_t numHeaderBytes = Math::Min<size_t>( size, ( Compressor::kHeaderSize - m_HeaderSize ) );
        memcpy( m_Header + m_HeaderSize, bytes, numHeaderBytes );
        m_HeaderSize += static_cast<uint32_t>( numHeaderBytes );
        bytes += numHeaderBytes;
        size -= numHeaderBytes;

        if ( m_HeaderSize == Compressor::kHeaderSize )
        {
            // Of the formats Compressor writes, the ones that are read here
            Compressor::HeaderInfo header = {};
            const bool valid = ( Compressor::ParseHeader( m_Header, sizeof( m_Header ), header ) &&
                                 ( ( header.m_Type == Compressor::eZstd ) ||
                                   ( ( header.m_Type == Compressor::eUncompressed ) && ( header.m_CompressedSize == header.m_UncompressedSize ) ) ) );
            m_Zstd = ( header.m_Type == Compressor::eZstd );
            m_UncompressedSize = header.m_UncompressedSize;
            m_CompressedSize = header.m_CompressedSize;
            if ( valid && m_Zstd )
            {
                m_Context = ZSTD_createDCtx();
                m_Output = static_cast<char *>( ALLOC( kOutputSize ) );
            }
            if ( ( valid == false ) || ( m_Zstd && ( m_Context == nullptr ) ) )
            {
                m_Failed = true;
                return false;
            }
        }
    }

    if ( size == 0 )
    {
        return true;
    }
    if ( FeedPayload( bytes, size, sink ) == false )
    {
        m_Failed = true;
        return false;
    }
    return true;
}

// FeedPayload
//------------------------------------------------------------------------------
bool StreamDecompressor::FeedPayload( const char * data, size_t size, Sink & sink )
{
    if ( ( m_HeaderSize < Compressor::kHeaderSize ) || ( size > ( m_CompressedSize - m_PayloadFed ) ) )
    {
        return false; // More than the header says there is
    }
    m_PayloadFed += size;

    if ( m_Zstd == false )
    {
        m_Produced += size;
        return sink.OnPiece( data, size );
    }

    if ( m_FrameDone )
    {
        return false; // Data after the frame
    }
    ZSTD_inBuffer in = { data, size, 0 };
    for ( ;; )
    {
        ZSTD_outBuffer out = { m_Output, kOutputSize, 0 };
        const size_t result = ZSTD_decompressStream( static_cast<ZSTD_DCtx *>( m_Context ), &out, &in );
        if ( ZSTD_isError( result ) )
        {
            return false;
        }
        m_Produced += out.pos;
        if ( ( m_Produced > m_UncompressedSize ) || ( ( out.pos > 0 ) && ( sink.OnPiece( m_Output, out.pos ) == false ) ) )
        {
            return false;
        }
        if ( result == 0 )
        {
            m_FrameDone = true;
            return ( in.pos == in.size ); // Nothing may follow the frame
        }

        // A full output buffer may leave more to come out of what was already taken in
        if ( ( in.pos == in.size ) && ( out.pos < out.size ) )
        {
            return true;
        }
    }
}

// Finish
//------------------------------------------------------------------------------
bool StreamDecompressor::Finish() const
{
    return ( m_Failed == false ) &&
           ( m_HeaderSize == Compressor::kHeaderSize ) &&
           ( m_PayloadFed == m_CompressedSize ) &&
           ( m_Produced == m_UncompressedSize ) &&
           ( ( m_Zstd == false ) || m_FrameDone );
}

// DecompressMemory
//------------------------------------------------------------------------------
/*static*/ bool StreamDecompressor::DecompressMemory( const void * data, size_t size, Sink & sink, uint32_t & outUncompressedSize )
{
    StreamDecompressor decompressor;
    const char * bytes = static_cast<const char *>( data );
    for ( size_t done = 0; done < size; )
    {
        const size_t piece = Math::Min<size_t>( ( size - done ), kOutputSize );
        if ( decompressor.Feed( bytes + done, piece, sink ) == false )
        {
            return false;
        }
        done += piece;
    }
    outUncompressedSize = decompressor.GetUncompressedSize();
    return decompressor.Finish();
}

// DecompressFile
//------------------------------------------------------------------------------
/*static*/ bool StreamDecompressor::DecompressFile( const char * path, const Atomic<bool> * abort, Sink & sink, uint32_t & outUncompressedSize )
{
    FileStream file;
    if ( file.Open( path, FileStream::READ_ONLY ) == false )
    {
        return false;
    }

    StreamDecompressor decompressor;
    if ( ( FileChunks::ForEachPiece( file, file.GetFileSize(), abort, [ & ]( const void * data, uint32_t size )
    {
        return decompressor.Feed( data, size, sink );
    } ) == false ) )
    {
        return false;
    }
    outUncompressedSize = decompressor.GetUncompressedSize();
    return decompressor.Finish();
}

//------------------------------------------------------------------------------
