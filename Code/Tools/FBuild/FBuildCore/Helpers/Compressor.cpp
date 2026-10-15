// Compressor
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Compressor.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/Helpers/FileChunks.h"

// Core
#include "Core/Containers/UniquePtr.h"
#include "Core/Env/Assert.h"
#include "Core/Env/Types.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Math/Conversions.h"
#include "Core/Mem/Mem.h"
#include "Core/Process/Atomic.h"
#include "Core/Profile/Profile.h"

// External
#include "lz4.h"
#include "lz4hc.h"
#include "zstd.h"

#include <memory.h>

//------------------------------------------------------------------------------
Compressor::Compressor()
    : m_Result( nullptr )
    , m_ResultSize( 0 )
{
}

// DESTRUCTOR
//------------------------------------------------------------------------------
Compressor::~Compressor()
{
    FREE( m_Result );
}

// ParseHeader
//------------------------------------------------------------------------------
/*static*/ bool Compressor::ParseHeader( const void * data, size_t dataSize, HeaderInfo & outHeader, bool allowTransform )
{
    ASSERT( data );
    if ( dataSize < kHeaderSize )
    {
        return false;
    }
    const Header * header = (const Header *)data;
    const uint32_t codec = ( header->m_CompressionType & 0xFF );
    const uint8_t transformId = (uint8_t)( ( header->m_CompressionType >> 8 ) & 0xFF );
    const uint8_t transformVersion = (uint8_t)( ( header->m_CompressionType >> 16 ) & 0xFF );
    const bool hasTransform = ( ( transformId | transformVersion ) != 0 );
    if ( ( codec > eZstd ) ||
         ( ( header->m_CompressionType >> 24 ) != 0 ) ||
         ( hasTransform && ( ( allowTransform == false ) || ( codec != eZstd ) || ( transformId == 0 ) ) ) ||
         ( header->m_CompressedSize > header->m_UncompressedSize ) )
    {
        return false;
    }
    outHeader.m_Type = static_cast<CompressionType>( codec );
    outHeader.m_UncompressedSize = header->m_UncompressedSize;
    outHeader.m_CompressedSize = header->m_CompressedSize;
    outHeader.m_TransformId = transformId;
    outHeader.m_TransformVersion = transformVersion;
    return true;
}

// IsValidData
//------------------------------------------------------------------------------
/*static*/ bool Compressor::IsValidData( const void * data, size_t dataSize )
{
    HeaderInfo header;
    return ( ParseHeader( data, dataSize, header ) &&
             ( ( header.m_CompressedSize + kHeaderSize ) == dataSize ) );
}

// WriteHeader
//------------------------------------------------------------------------------
/*static*/ void Compressor::WriteHeader( void * dest, bool zstd, uint32_t uncompressedSize, uint32_t compressedSize, bool transformed )
{
    static_assert( kHeaderSize == sizeof( Header ), "Header size" );
    ASSERT( zstd || ( transformed == false ) );
    Header * header = static_cast<Header *>( dest );
    header->m_CompressionType = zstd ? eZstd : eUncompressed;
    if ( transformed )
    {
        header->m_CompressionType |= ( ( (uint32_t)PchTransform::kPtrXfId << 8 ) | ( (uint32_t)PchTransform::kPtrXfVersion << 16 ) );
    }
    header->m_UncompressedSize = uncompressedSize;
    header->m_CompressedSize = zstd ? compressedSize : uncompressedSize;
}

// CompressZstdFile
//------------------------------------------------------------------------------
/*static*/ bool Compressor::CompressZstdFile( const char * sourcePath,
                                              uint64_t sourceSize,
                                              const char * destPath,
                                              int32_t compressionLevel,
                                              const Atomic<bool> * abort,
                                              FileObserver & observer,
                                              uint64_t & outSize,
                                              PchTransform::Encoder * transform,
                                              bool * outTransformed )
{
    if ( outTransformed )
    {
        *outTransformed = false;
    }
    if ( ( sourceSize == 0 ) || ( sourceSize > kMaxFileSize ) )
    {
        return false;
    }

    // The header says how long the frame turned out to be, so it is written when that is known
    FileStream source;
    FileStream dest;
    char header[ kHeaderSize ] = {};
    if ( ( source.Open( sourcePath, FileStream::READ_ONLY ) == false ) ||
         ( source.GetFileSize() != sourceSize ) ||
         ( dest.Open( destPath, FileStream::WRITE_ONLY ) == false ) ||
         ( dest.Write( header, sizeof( header ) ) != sizeof( header ) ) )
    {
        return false;
    }

    struct ZstdContext
    {
        ZstdContext() : m_Context( ZSTD_createCCtx() ) {}
        ~ZstdContext() { ZSTD_freeCCtx( m_Context ); }
        ZSTD_CCtx * m_Context;
    } zstd;
    if ( ( zstd.m_Context == nullptr ) ||
         ZSTD_isError( ZSTD_CCtx_setParameter( zstd.m_Context, ZSTD_c_compressionLevel, compressionLevel ) ) ||
         ( ( transform == nullptr ) && ZSTD_isError( ZSTD_CCtx_setPledgedSrcSize( zstd.m_Context, sourceSize ) ) ) ) // A transform makes data of another size
    {
        return false;
    }

    const size_t outputSize = ZSTD_CStreamOutSize();
    UniquePtr<char, FreeDeletor> output( static_cast<char *>( ALLOC( outputSize ) ) );
    uint64_t frameSize = 0;
    auto compress = [ & ]( const void * data, size_t size, ZSTD_EndDirective directive )
    {
        ZSTD_inBuffer in = { data, size, 0 };
        size_t remaining;
        do
        {
            ZSTD_outBuffer produced = { output.Get(), outputSize, 0 };
            remaining = ZSTD_compressStream2( zstd.m_Context, &produced, &in, directive );
            if ( ZSTD_isError( remaining ) || ( dest.Write( output.Get(), static_cast<uint32_t>( produced.pos ) ) != produced.pos ) )
            {
                return false;
            }
            frameSize += produced.pos;
        } while ( ( directive == ZSTD_e_end ) ? ( remaining != 0 ) : ( in.pos < in.size ) );
        return true;
    };

    // What the transform makes goes to the frame
    class FrameSink : public PchTransform::Sink
    {
    public:
        explicit FrameSink( const decltype( compress ) & compressFunc ) : m_Compress( compressFunc ) {}
        explicit FrameSink( const FrameSink & other ) = delete;
        FrameSink & operator=( const FrameSink & other ) = delete;
        virtual bool OnPiece( const void * data, size_t size ) override { return m_Compress( data, size, ZSTD_e_continue ); }
    private:
        const decltype( compress ) & m_Compress;
    } frameSink( compress );

    uint64_t consumed = 0;
    observer.OnPass();
    if ( FileChunks::ForEachPiece( source, sourceSize, abort, [ & ]( const void * data, uint32_t size )
    {
        observer.OnPiece( data, size );
        consumed += size;
        const bool last = ( consumed == sourceSize );
        if ( transform )
        {
            return ( transform->Feed( data, size, frameSink ) &&
                     ( ( last == false ) || ( transform->Finish( frameSink ) && compress( nullptr, 0, ZSTD_e_end ) ) ) );
        }
        return compress( data, size, last ? ZSTD_e_end : ZSTD_e_continue );
    } ) == false )
    {
        return false;
    }

    if ( frameSize < sourceSize )
    {
        WriteHeader( header, true, static_cast<uint32_t>( sourceSize ), static_cast<uint32_t>( frameSize ), ( transform != nullptr ) );
        if ( ( dest.Seek( 0 ) == false ) || ( dest.Write( header, sizeof( header ) ) != sizeof( header ) ) )
        {
            return false;
        }
        outSize = ( sizeof( header ) + frameSize );
        if ( outTransformed )
        {
            *outTransformed = ( transform != nullptr );
        }
        return true;
    }

    // Not smaller: the file as it is, in place of the frame
    WriteHeader( header, false, static_cast<uint32_t>( sourceSize ), static_cast<uint32_t>( sourceSize ) );
    observer.OnPass();
    if ( ( source.Seek( 0 ) == false ) ||
         ( dest.Seek( 0 ) == false ) ||
         ( dest.Write( header, sizeof( header ) ) != sizeof( header ) ) ||
         ( FileChunks::ForEachPiece( source, sourceSize, abort, [ & ]( const void * data, uint32_t size )
         {
             observer.OnPiece( data, size );
             return ( dest.Write( data, size ) == size );
         } ) == false ) ||
         ( dest.Truncate() == false ) )
    {
        return false;
    }
    outSize = ( sizeof( header ) + sourceSize );
    return true;
}

// GetUncompressedSize
//------------------------------------------------------------------------------
/*static*/ uint32_t Compressor::GetUncompressedSize( const void * data, size_t dataSize )
{
    // Only valid to call on data that is known to be compressor format
    ASSERT( IsValidData( data, dataSize ) );
    (void)dataSize;

    const Header * header = (const Header *)data;
    return header->m_UncompressedSize;
}

// Compress
//------------------------------------------------------------------------------
bool Compressor::Compress( const void * data, size_t dataSize, int32_t compressionLevel )
{
    PROFILE_FUNCTION;

    ASSERT( data );
    ASSERT( m_Result == nullptr );

    // allocate worst case output size for LZ4
    const int worstCaseSize = LZ4_compressBound( (int)dataSize );
    ASSERT( worstCaseSize > 0 );
    UniquePtr<char, FreeDeletor> output( (char *)ALLOC( (size_t)worstCaseSize ) );

    int32_t compressedSize;

    // do compression
    if ( compressionLevel > 0 )
    {
        // Higher compression, using LZ4HC
        compressedSize = LZ4_compress_HC( (const char *)data, output.Get(), (int)dataSize, worstCaseSize, compressionLevel );
    }
    else if ( compressionLevel < 0 )
    {
        // Lower compression, using regular LZ4
        const int32_t acceleration = ( 0 - compressionLevel );
        compressedSize = LZ4_compress_fast( (const char *)data, output.Get(), (int)dataSize, worstCaseSize, acceleration );
    }
    else
    {
        // Disable compression
        compressedSize = (int32_t)dataSize; // Act as if compression achieved nothing
    }

    ASSERT( compressedSize >= 0 ); // Can be zero for 0 byte files

    // did the compression yield any benefit?
    const bool compressed = ( compressedSize < (int)dataSize );

    if ( compressed )
    {
        // trim memory usage to compressed size
        m_Result = ALLOC( (uint32_t)compressedSize + sizeof( Header ) );
        memcpy( (char *)m_Result + sizeof( Header ), output.Get(), (size_t)compressedSize );
        m_ResultSize = (uint32_t)compressedSize + sizeof( Header );
    }
    else
    {
        // compression failed, so just copy the old data
        m_Result = ALLOC( dataSize + sizeof( Header ) );
        memcpy( (char *)m_Result + sizeof( Header ), data, dataSize );
        m_ResultSize = dataSize + sizeof( Header );
    }

    // fill out header
    Header * header = (Header *)m_Result;
    header->m_CompressionType = compressed ? eLZ4 : eUncompressed;   // compression type
    header->m_UncompressedSize = (uint32_t)dataSize;    // input size
    header->m_CompressedSize = compressed ? (uint32_t)compressedSize : (uint32_t)dataSize;    // output size

    return compressed;
}

// Decompress
//------------------------------------------------------------------------------
bool Compressor::Decompress( const void * data )
{
    PROFILE_FUNCTION;

    ASSERT( data );
    ASSERT( m_Result == nullptr );

    const Header * header = (const Header *)data;

    // Not a codec read here (the type of data a transform was applied to is not)
    if ( header->m_CompressionType > eZstd )
    {
        return false;
    }

    // handle uncompressed case
    if ( header->m_CompressionType == eUncompressed )
    {
        m_Result = ALLOC( header->m_UncompressedSize );
        memcpy( m_Result, (const char *)data + sizeof( Header ), header->m_UncompressedSize );
        m_ResultSize = header->m_UncompressedSize;
        return true;
    }

    // uncompressed size
    const uint32_t uncompressedSize = header->m_UncompressedSize;
    m_Result = ALLOC( uncompressedSize );
    m_ResultSize = uncompressedSize;

    // skip over header to LZ4 data
    const char * compressedData = ( (const char *)data + sizeof( Header ) );

    if ( header->m_CompressionType == eLZ4 )
    {
        // decompress
        const int bytesDecompressed = LZ4_decompress_safe( compressedData,
                                                           (char *)m_Result,
                                                           (int)header->m_CompressedSize,
                                                           (int)uncompressedSize );
        if ( bytesDecompressed == (int)uncompressedSize )
        {
            return true;
        }
    }
    else
    {
        ASSERT( header->m_CompressionType == eZstd );

        // decompress
        const size_t bytesDecompressed = ZSTD_decompress( m_Result,
                                                          uncompressedSize,
                                                          compressedData,
                                                          header->m_CompressedSize );
        if ( bytesDecompressed == uncompressedSize )
        {
            return true;
        }
    }

    // Data is corrupt
    FREE( m_Result );
    m_Result = nullptr;
    m_ResultSize = 0;
    return false;
}

// CompressZstd
//------------------------------------------------------------------------------
bool Compressor::CompressZstd( const void * data,
                               size_t dataSize,
                               int32_t compressionLevel )
{
    PROFILE_FUNCTION;

    ASSERT( data );
    ASSERT( m_Result == nullptr );

    // allocate worst case output size for LZ4
    const size_t worstCaseSize = ZSTD_compressBound( dataSize );
    UniquePtr<char, FreeDeletor> output( (char *)ALLOC( worstCaseSize ) );

    size_t compressedSize;

    // do compression
    if ( compressionLevel > 0 )
    {
        compressedSize = ZSTD_compress( output.Get(),
                                        worstCaseSize,
                                        static_cast<const char *>( data ),
                                        dataSize,
                                        compressionLevel );
    }
    else
    {
        // Disable compression
        compressedSize = dataSize; // Act as if compression achieved nothing
    }

    // did the compression yield any benefit?
    const bool compressed = ( compressedSize < dataSize );

    if ( compressed )
    {
        // trim memory usage to compressed size
        m_Result = ALLOC( compressedSize + sizeof( Header ) );
        memcpy( (char *)m_Result + sizeof( Header ), output.Get(), (size_t)compressedSize );
        m_ResultSize = compressedSize + sizeof( Header );
    }
    else
    {
        // compression failed, so just copy the old data
        m_Result = ALLOC( dataSize + sizeof( Header ) );
        memcpy( (char *)m_Result + sizeof( Header ), data, dataSize );
        m_ResultSize = dataSize + sizeof( Header );
    }

    // fill out header
    Header * header = (Header *)m_Result;
    header->m_CompressionType = compressed ? eZstd : eUncompressed;   // compression type
    header->m_UncompressedSize = (uint32_t)dataSize;    // input size
    header->m_CompressedSize = compressed ? (uint32_t)compressedSize : (uint32_t)dataSize; // output size

    return compressed;
}

//------------------------------------------------------------------------------
