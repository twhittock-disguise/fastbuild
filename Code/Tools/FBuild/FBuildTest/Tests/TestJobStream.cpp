// TestJobStream.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

#include "Tools/FBuild/FBuildCore/Helpers/JobStream.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Mem/Mem.h"

// External
#include "zstd.h"

#include <memory.h>

// TestJobStream
//------------------------------------------------------------------------------
class TestJobStream : public FBuildTest
{
private:
    DECLARE_TESTS

    void SharedHistory() const;
    void RejectsBadChunks() const;
    void RejectsHostileSizes() const;
    void RejectsLargerWindow() const;
    void LanSettingIsReadByTheWorkerDecoder() const;

    static constexpr uint32_t kSharedSize = ( 1024 * 1024 );
    static constexpr uint32_t kUniqueSize = ( 20 * 1024 );
    static constexpr size_t kHeaderSize = sizeof( uint32_t );

    // Shaped like preprocessed jobs: a large part shared by every job, and a part unique to each
    static void MakeJobs( uint32_t numJobs, Array<Array<char>> & outJobs );
    static void Fill( char * buffer, uint32_t size, uint32_t seed );

    // The chunks a stream produces for the jobs, in order
    static void EncodeAll( JobStreamEncoder & encoder, const Array<Array<char>> & jobs, Array<Array<char>> & outChunks );

    // Every chunk decodes to its job, in order
    static void DecodeAll( JobStreamDecoder & decoder, const Array<Array<char>> & chunks, const Array<Array<char>> & jobs );

    static bool IsRefused( const void * chunk, size_t chunkSize );
    static bool IsRefusedWhenDeclaring( uint32_t declaredSize, size_t bodySize ); // For a body that is not a stream

    // A chunk from a stream with the given window, as a sender that does not follow JobStream::kWindowLog would make
    static void EncodeWithWindow( int windowLog, const Array<char> & job, Array<char> & outChunk );
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestJobStream )
    REGISTER_TEST( SharedHistory )
    REGISTER_TEST( RejectsBadChunks )
    REGISTER_TEST( RejectsHostileSizes )
    REGISTER_TEST( RejectsLargerWindow )
    REGISTER_TEST( LanSettingIsReadByTheWorkerDecoder )
REGISTER_TESTS_END

// Fill
//------------------------------------------------------------------------------
/*static*/ void TestJobStream::Fill( char * buffer, uint32_t size, uint32_t seed )
{
    // Text like content from 16 letters
    uint32_t state = ( seed * 2654435761u ) | 1u;
    for ( uint32_t i = 0; i < size; ++i )
    {
        state ^= ( state << 13 );
        state ^= ( state >> 17 );
        state ^= ( state << 5 );
        buffer[ i ] = (char)( 'a' + ( ( state >> 8 ) & 15 ) );
    }
}

// MakeJobs
//------------------------------------------------------------------------------
/*static*/ void TestJobStream::MakeJobs( uint32_t numJobs, Array<Array<char>> & outJobs )
{
    outJobs.SetSize( numJobs );
    for ( uint32_t i = 0; i < numJobs; ++i )
    {
        outJobs[ i ].SetSize( kSharedSize + kUniqueSize );
        Fill( outJobs[ i ].Begin(), kSharedSize, 0 );
        Fill( outJobs[ i ].Begin() + kSharedSize, kUniqueSize, i + 1 );
    }
}

// EncodeAll
//------------------------------------------------------------------------------
/*static*/ void TestJobStream::EncodeAll( JobStreamEncoder & encoder, const Array<Array<char>> & jobs, Array<Array<char>> & outChunks )
{
    outChunks.SetSize( jobs.GetSize() );
    for ( size_t i = 0; i < jobs.GetSize(); ++i )
    {
        TEST_ASSERT( encoder.Encode( jobs[ i ].Begin(), jobs[ i ].GetSize() ) );
        outChunks[ i ].SetSize( encoder.GetResultSize() );
        memcpy( outChunks[ i ].Begin(), encoder.GetResult(), encoder.GetResultSize() );
    }
}

// DecodeAll
//------------------------------------------------------------------------------
/*static*/ void TestJobStream::DecodeAll( JobStreamDecoder & decoder, const Array<Array<char>> & chunks, const Array<Array<char>> & jobs )
{
    for ( size_t i = 0; i < chunks.GetSize(); ++i )
    {
        void * data = nullptr;
        size_t size = 0;
        TEST_ASSERT( decoder.Decode( chunks[ i ].Begin(), chunks[ i ].GetSize(), data, size ) );
        TEST_ASSERT( size == jobs[ i ].GetSize() );
        TEST_ASSERT( memcmp( data, jobs[ i ].Begin(), size ) == 0 );
        FREE( data );
    }
}

// IsRefused
//------------------------------------------------------------------------------
// Refused by a decoder that starts a stream, without a crash or a result
/*static*/ bool TestJobStream::IsRefused( const void * chunk, size_t chunkSize )
{
    JobStreamDecoder decoder;
    void * data = reinterpret_cast<void *>( 1 );
    size_t size = 1;
    const bool ok = decoder.Decode( chunk, chunkSize, data, size );
    return ( ok == false ) && ( data == nullptr ) && ( size == 0 );
}

// IsRefusedWhenDeclaring
//------------------------------------------------------------------------------
/*static*/ bool TestJobStream::IsRefusedWhenDeclaring( uint32_t declaredSize, size_t bodySize )
{
    Array<char> chunk;
    chunk.SetSize( kHeaderSize + bodySize );
    memcpy( chunk.Begin(), &declaredSize, kHeaderSize );
    Fill( chunk.Begin() + kHeaderSize, (uint32_t)bodySize, 7 );
    return IsRefused( chunk.Begin(), chunk.GetSize() );
}

// EncodeWithWindow
//------------------------------------------------------------------------------
/*static*/ void TestJobStream::EncodeWithWindow( int windowLog, const Array<char> & job, Array<char> & outChunk )
{
    ZSTD_CCtx * context = ZSTD_createCCtx();
    TEST_ASSERT( context );
    TEST_ASSERT( ZSTD_isError( ZSTD_CCtx_setParameter( context, ZSTD_c_compressionLevel, JobStream::kCompressionLevel ) ) == false );
    TEST_ASSERT( ZSTD_isError( ZSTD_CCtx_setParameter( context, ZSTD_c_windowLog, windowLog ) ) == false );

    outChunk.SetSize( kHeaderSize + ZSTD_compressBound( job.GetSize() ) );
    const uint32_t declaredSize = (uint32_t)job.GetSize();
    memcpy( outChunk.Begin(), &declaredSize, kHeaderSize );

    ZSTD_inBuffer in = { job.Begin(), job.GetSize(), 0 };
    ZSTD_outBuffer out = { outChunk.Begin() + kHeaderSize, outChunk.GetSize() - kHeaderSize, 0 };
    size_t remaining;
    do
    {
        remaining = ZSTD_compressStream2( context, &out, &in, ZSTD_e_flush );
        TEST_ASSERT( ZSTD_isError( remaining ) == false );
    } while ( ( remaining != 0 ) || ( in.pos != in.size ) );
    outChunk.SetSize( kHeaderSize + out.pos );
    ZSTD_freeCCtx( context );
}

// SharedHistory
//------------------------------------------------------------------------------
void TestJobStream::SharedHistory() const
{
    const uint32_t kNumJobs = 8;
    Array<Array<char>> jobs;
    MakeJobs( kNumJobs, jobs );

    // One stream for all of the jobs
    JobStreamEncoder encoder;
    Array<Array<char>> chunks;
    EncodeAll( encoder, jobs, chunks );

    // Every job comes out as it went in, in order
    JobStreamDecoder decoder;
    DecodeAll( decoder, chunks, jobs );

    // The same jobs, each compressed with no history
    size_t streamedSize = 0;
    size_t independentSize = 0;
    for ( size_t i = 0; i < kNumJobs; ++i )
    {
        streamedSize += chunks[ i ].GetSize();

        JobStreamEncoder single;
        TEST_ASSERT( single.Encode( jobs[ i ].Begin(), jobs[ i ].GetSize() ) );
        independentSize += single.GetResultSize();
    }

    // Only the first job pays for the shared part
    TEST_ASSERT( streamedSize < ( independentSize / 3 ) );
    TEST_ASSERT( chunks[ 1 ].GetSize() < ( chunks[ 0 ].GetSize() / 10 ) );
}

// RejectsBadChunks
//------------------------------------------------------------------------------
void TestJobStream::RejectsBadChunks() const
{
    Array<Array<char>> jobs;
    MakeJobs( 3, jobs );
    JobStreamEncoder encoder;
    Array<Array<char>> chunks;
    EncodeAll( encoder, jobs, chunks );
    const Array<char> & good = chunks[ 0 ];

    // Out of order: the second job of a stream is not the start of one
    TEST_ASSERT( IsRefused( chunks[ 1 ].Begin(), chunks[ 1 ].GetSize() ) );

    // Too short to hold anything, or declaring nothing
    TEST_ASSERT( IsRefused( good.Begin(), kHeaderSize ) );
    {
        Array<char> empty( good );
        const uint32_t zero = 0;
        memcpy( empty.Begin(), &zero, kHeaderSize );
        TEST_ASSERT( IsRefused( empty.Begin(), empty.GetSize() ) );
    }

    // Cut short
    TEST_ASSERT( IsRefused( good.Begin(), good.GetSize() - 100 ) );

    // Declared size does not match the data, in either direction
    {
        Array<char> bad( good );
        uint32_t size;
        memcpy( &size, bad.Begin(), kHeaderSize );
        const uint32_t larger = ( size + 1 );
        memcpy( bad.Begin(), &larger, kHeaderSize );
        TEST_ASSERT( IsRefused( bad.Begin(), bad.GetSize() ) );
        const uint32_t smaller = ( size - 1 );
        memcpy( bad.Begin(), &smaller, kHeaderSize );
        TEST_ASSERT( IsRefused( bad.Begin(), bad.GetSize() ) );
    }

    // Not zstd data
    {
        Array<char> bad( good );
        bad[ kHeaderSize ] ^= 0xFF;
        TEST_ASSERT( IsRefused( bad.Begin(), bad.GetSize() ) );
    }

    // After a failure the decoder starts over, so a good stream still decodes
    {
        JobStreamDecoder decoder;
        void * data = nullptr;
        size_t size = 0;
        TEST_ASSERT( decoder.Decode( chunks[ 1 ].Begin(), chunks[ 1 ].GetSize(), data, size ) == false );
        TEST_ASSERT( decoder.Decode( good.Begin(), good.GetSize(), data, size ) );
        TEST_ASSERT( memcmp( data, jobs[ 0 ].Begin(), size ) == 0 );
        FREE( data );
    }
}

// RejectsHostileSizes
//------------------------------------------------------------------------------
void TestJobStream::RejectsHostileSizes() const
{
    // The size in the header comes from the sender and decides what the worker allocates
    const uint32_t kTooLarge = (uint32_t)( JobStream::kMaxChunkSize + 1 );
    TEST_ASSERT( IsRefusedWhenDeclaring( kTooLarge, 100 ) );
    TEST_ASSERT( IsRefusedWhenDeclaring( 0xFFFFFFFF, 100 ) );

    // The largest allowed size with nothing like that much data behind it
    TEST_ASSERT( IsRefusedWhenDeclaring( (uint32_t)JobStream::kMaxChunkSize, 100 ) );

    // The same limit applies to what a coordinator can send
    {
        JobStreamEncoder encoder;
        Array<char> tooLarge;
        tooLarge.SetSize( JobStream::kMaxChunkSize + 1 );
        TEST_ASSERT( encoder.Encode( tooLarge.Begin(), tooLarge.GetSize() ) == false );
    }
}

// RejectsLargerWindow
//------------------------------------------------------------------------------
void TestJobStream::RejectsLargerWindow() const
{
    Array<Array<char>> jobs;
    MakeJobs( 1, jobs );

    // A window the decoder accepts
    {
        Array<char> chunk;
        EncodeWithWindow( JobStream::kWindowLog, jobs[ 0 ], chunk );
        JobStreamDecoder decoder;
        void * data = nullptr;
        size_t size = 0;
        TEST_ASSERT( decoder.Decode( chunk.Begin(), chunk.GetSize(), data, size ) );
        FREE( data );
    }

    // The next size up would make the decoder hold twice the history, so it is refused
    {
        Array<char> chunk;
        EncodeWithWindow( JobStream::kWindowLog + 1, jobs[ 0 ], chunk );
        TEST_ASSERT( IsRefused( chunk.Begin(), chunk.GetSize() ) );
    }
}

// LanSettingIsReadByTheWorkerDecoder
//------------------------------------------------------------------------------
void TestJobStream::LanSettingIsReadByTheWorkerDecoder() const
{
    Array<Array<char>> jobs;
    MakeJobs( 4, jobs );

    JobStreamEncoder encoder( JobStream::kLanCompressionLevel, JobStream::kLanWindowLog );
    Array<Array<char>> chunks;
    EncodeAll( encoder, jobs, chunks );

    // The decoder is the one every worker has, and it keeps the history across the chunks
    JobStreamDecoder decoder;
    DecodeAll( decoder, chunks, jobs );
    TEST_ASSERT( chunks[ 1 ].GetSize() < ( chunks[ 0 ].GetSize() / 10 ) );

    // The setting changes the stream: the default encoder makes a different first chunk
    JobStreamEncoder normal;
    TEST_ASSERT( normal.Encode( jobs[ 0 ].Begin(), jobs[ 0 ].GetSize() ) );
    TEST_ASSERT( normal.GetResultSize() != chunks[ 0 ].GetSize() );
}

//------------------------------------------------------------------------------
