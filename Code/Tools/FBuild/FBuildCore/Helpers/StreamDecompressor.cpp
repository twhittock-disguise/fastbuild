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

// RawSink
//------------------------------------------------------------------------------
// Hands on what the inverse of a transform makes, which is the data, no more of it than the header says
class StreamDecompressor::RawSink : public PchTransform::Sink
{
public:
    RawSink( StreamDecompressor & owner, StreamDecompressor::Sink & sink )
        : m_Owner( owner )
        , m_Sink( sink )
    {
    }
    explicit RawSink( const RawSink & other ) = delete;
    RawSink & operator=( const RawSink & other ) = delete;
    virtual bool OnPiece( const void * data, size_t size ) override
    {
        m_Owner.m_Produced += size;
        return ( ( m_Owner.m_Produced <= m_Owner.m_UncompressedSize ) && m_Sink.OnPiece( data, size ) );
    }

private:
    StreamDecompressor & m_Owner;
    StreamDecompressor::Sink & m_Sink;
};

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
    FDELETE m_Decoder;
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
            const bool parsed = Compressor::ParseHeader( m_Header, sizeof( m_Header ), header, true );
            const bool transformed = ( ( header.m_TransformId | header.m_TransformVersion ) != 0 );
            const bool valid = ( parsed &&
                                 ( ( header.m_Type == Compressor::eZstd ) ||
                                   ( ( header.m_Type == Compressor::eUncompressed ) && ( header.m_CompressedSize == header.m_UncompressedSize ) ) ) &&
                                 ( ( transformed == false ) ||
                                   ( ( header.m_TransformId == PchTransform::kPtrXfId ) && ( header.m_TransformVersion == PchTransform::kPtrXfVersion ) ) ) );
            m_Zstd = ( header.m_Type == Compressor::eZstd );
            m_UncompressedSize = header.m_UncompressedSize;
            m_CompressedSize = header.m_CompressedSize;
            if ( valid && m_Zstd )
            {
                m_Context = ZSTD_createDCtx();
                m_Output = static_cast<char *>( ALLOC( kOutputSize ) );
                if ( transformed )
                {
                    m_Decoder = FNEW( PchTransform::Decoder );
                }
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
        return OnFrameData( data, size, sink );
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
        if ( ( out.pos > 0 ) && ( OnFrameData( m_Output, out.pos, sink ) == false ) )
        {
            return false;
        }
        if ( result == 0 )
        {
            m_FrameDone = true;
            if ( m_Decoder )
            {
                RawSink rawSink( *this, sink );
                if ( m_Decoder->Finish( rawSink ) == false )
                {
                    return false;
                }
            }
            return ( in.pos == in.size ); // Nothing may follow the frame
        }

        // A full output buffer may leave more to come out of what was already taken in
        if ( ( in.pos == in.size ) && ( out.pos < out.size ) )
        {
            return true;
        }
    }
}

// OnFrameData
//------------------------------------------------------------------------------
bool StreamDecompressor::OnFrameData( const void * data, size_t size, Sink & sink )
{
    if ( m_Decoder == nullptr )
    {
        m_Produced += size;
        return ( ( m_Produced <= m_UncompressedSize ) && sink.OnPiece( data, size ) );
    }

    // What the inverse makes is bounded by the size in the header, and it makes at least half as many bytes as it is given
    RawSink rawSink( *this, sink );
    return m_Decoder->Feed( data, size, rawSink );
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
