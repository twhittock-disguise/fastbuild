// JobStream
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "JobStream.h"

// Core
#include "Core/Env/Assert.h"
#include "Core/Mem/Mem.h"

// External
#include "zstd.h"

#include <string.h>

// CONSTRUCTOR
//------------------------------------------------------------------------------
JobStreamEncoder::JobStreamEncoder( int compressionLevel, int windowLog )
    : m_CompressionLevel( compressionLevel )
    , m_WindowLog( windowLog )
{
}

// DESTRUCTOR
//------------------------------------------------------------------------------
JobStreamEncoder::~JobStreamEncoder()
{
    ZSTD_freeCCtx( m_Context );
    FREE( m_Buffer );
}

// Encode
//------------------------------------------------------------------------------
bool JobStreamEncoder::Encode( const void * data, size_t dataSize )
{
    m_ResultSize = 0;
    if ( ( dataSize == 0 ) || ( dataSize > JobStream::kMaxChunkSize ) )
    {
        return false;
    }

    if ( m_Context == nullptr )
    {
        m_Context = ZSTD_createCCtx();
        if ( m_Context == nullptr )
        {
            return false;
        }
        const bool paramsOk =
            !ZSTD_isError( ZSTD_CCtx_setParameter( m_Context, ZSTD_c_compressionLevel, m_CompressionLevel ) ) &&
            !ZSTD_isError( ZSTD_CCtx_setParameter( m_Context, ZSTD_c_windowLog, m_WindowLog ) ) &&
            !ZSTD_isError( ZSTD_CCtx_setParameter( m_Context, ZSTD_c_enableLongDistanceMatching, 1 ) ) &&
            !ZSTD_isError( ZSTD_CCtx_setParameter( m_Context, ZSTD_c_ldmHashLog, m_WindowLog - 10 ) );
        if ( paramsOk == false )
        {
            ZSTD_freeCCtx( m_Context );
            m_Context = nullptr;
            return false;
        }
    }

    // A flush emits no more than the bound for the input plus a block header
    const size_t headerSize = sizeof( uint32_t );
    const size_t neededSize = ( headerSize + ZSTD_compressBound( dataSize ) + 64 );
    if ( m_BufferSize < neededSize )
    {
        FREE( m_Buffer );
        m_Buffer = static_cast<char *>( ALLOC( neededSize ) );
        m_BufferSize = neededSize;
    }

    const uint32_t uncompressedSize = static_cast<uint32_t>( dataSize );
    memcpy( m_Buffer, &uncompressedSize, headerSize );

    ZSTD_inBuffer in = { data, dataSize, 0 };
    ZSTD_outBuffer out = { m_Buffer + headerSize, m_BufferSize - headerSize, 0 };
    size_t remaining;
    do
    {
        remaining = ZSTD_compressStream2( m_Context, &out, &in, ZSTD_e_flush );
        if ( ZSTD_isError( remaining ) || ( ( out.pos == out.size ) && ( remaining != 0 ) ) )
        {
            // The stream state is unknown, so the stream can not continue
            ZSTD_freeCCtx( m_Context );
            m_Context = nullptr;
            return false;
        }
    } while ( ( remaining != 0 ) || ( in.pos != in.size ) );

    m_ResultSize = ( headerSize + out.pos );
    return true;
}

// JobStreamDecoder
//------------------------------------------------------------------------------
JobStreamDecoder::~JobStreamDecoder()
{
    ZSTD_freeDCtx( m_Context );
}

// Decode
//------------------------------------------------------------------------------
bool JobStreamDecoder::Decode( const void * chunk, size_t chunkSize, void * & outData, size_t & outSize )
{
    outData = nullptr;
    outSize = 0;

    const size_t headerSize = sizeof( uint32_t );
    if ( chunkSize <= headerSize )
    {
        return false;
    }
    uint32_t uncompressedSize;
    memcpy( &uncompressedSize, chunk, headerSize );
    if ( ( uncompressedSize == 0 ) || ( uncompressedSize > JobStream::kMaxChunkSize ) )
    {
        return false;
    }

    if ( m_Context == nullptr )
    {
        m_Context = ZSTD_createDCtx();
        if ( m_Context == nullptr )
        {
            return false;
        }
        if ( ZSTD_isError( ZSTD_DCtx_setParameter( m_Context, ZSTD_d_windowLogMax, JobStream::kWindowLog ) ) )
        {
            ZSTD_freeDCtx( m_Context );
            m_Context = nullptr;
            return false;
        }
    }

    void * output = ALLOC( uncompressedSize );
    ZSTD_inBuffer in = { static_cast<const char *>( chunk ) + headerSize, chunkSize - headerSize, 0 };
    ZSTD_outBuffer out = { output, uncompressedSize, 0 };
    bool ok = true;
    while ( in.pos < in.size )
    {
        const size_t inBefore = in.pos;
        const size_t outBefore = out.pos;
        const size_t result = ZSTD_decompressStream( m_Context, &out, &in );

        // An error, the end of a frame (the stream never ends one), more output than
        // declared, or no progress all mean the chunk does not belong to this stream
        if ( ZSTD_isError( result ) ||
             ( result == 0 ) ||
             ( ( in.pos == inBefore ) && ( out.pos == outBefore ) ) ||
             ( ( out.pos == out.size ) && ( in.pos < in.size ) ) )
        {
            ok = false;
            break;
        }
    }
    ok = ok && ( out.pos == out.size );

    // The decoder can take all of the input and still hold output it had no room for,
    // which means the chunk declared less than it contains
    if ( ok )
    {
        char extra;
        ZSTD_inBuffer noInput = { nullptr, 0, 0 };
        ZSTD_outBuffer extraOut = { &extra, 1, 0 };
        ok = ( ZSTD_isError( ZSTD_decompressStream( m_Context, &extraOut, &noInput ) ) == false ) && ( extraOut.pos == 0 );
    }

    if ( ok == false )
    {
        // The decoder state is unknown, so the stream can not continue
        FREE( output );
        ZSTD_freeDCtx( m_Context );
        m_Context = nullptr;
        return false;
    }

    outData = output;
    outSize = uncompressedSize;
    return true;
}

//------------------------------------------------------------------------------
