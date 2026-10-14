// TestJobStream.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

#include "Tools/FBuild/FBuildCore/Helpers/JobStream.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Mem/Mem.h"
#include "Core/Tracing/Tracing.h"

#include <memory.h>

// TestJobStream
//------------------------------------------------------------------------------
class TestJobStream : public FBuildTest
{
private:
    DECLARE_TESTS

    void SharedHistory() const;
    void FreshStreamAfterReset() const;
    void RejectsBadChunks() const;
    void RejectsLargerWindow() const;
    void MemoryPerStream() const;

    static constexpr uint32_t kSharedSize = ( 1024 * 1024 );
    static constexpr uint32_t kUniqueSize = ( 20 * 1024 );
    static constexpr uint32_t kNumJobs = 8;

    // Shaped like preprocessed jobs: a large part shared by every job, and a part unique to each
    static void MakeJob( uint32_t index, Array<char> & outJob );
    static void Fill( char * buffer, uint32_t size, uint32_t seed );

    // The chunks a stream produces for the jobs, in order
    static void EncodeAll( JobStreamEncoder & encoder, const Array<Array<char>> & jobs, Array<Array<char>> & outChunks );
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestJobStream )
    REGISTER_TEST( SharedHistory )
    REGISTER_TEST( FreshStreamAfterReset )
    REGISTER_TEST( RejectsBadChunks )
    REGISTER_TEST( RejectsLargerWindow )
    REGISTER_TEST( MemoryPerStream )
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

// MakeJob
//------------------------------------------------------------------------------
/*static*/ void TestJobStream::MakeJob( uint32_t index, Array<char> & outJob )
{
    outJob.SetSize( kSharedSize + kUniqueSize );
    Fill( outJob.Begin(), kSharedSize, 0 );
    Fill( outJob.Begin() + kSharedSize, kUniqueSize, index + 1 );
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

// SharedHistory
//------------------------------------------------------------------------------
void TestJobStream::SharedHistory() const
{
    Array<Array<char>> jobs;
    jobs.SetSize( kNumJobs );
    for ( uint32_t i = 0; i < kNumJobs; ++i )
    {
        MakeJob( i, jobs[ i ] );
    }

    // One stream for all of the jobs
    JobStreamEncoder encoder;
    Array<Array<char>> chunks;
    EncodeAll( encoder, jobs, chunks );

    // Every job comes out as it went in, in order
    JobStreamDecoder decoder;
    size_t streamedSize = 0;
    for ( size_t i = 0; i < kNumJobs; ++i )
    {
        void * data = nullptr;
        size_t size = 0;
        TEST_ASSERT( decoder.Decode( chunks[ i ].Begin(), chunks[ i ].GetSize(), data, size ) );
        TEST_ASSERT( size == jobs[ i ].GetSize() );
        TEST_ASSERT( memcmp( data, jobs[ i ].Begin(), size ) == 0 );
        FREE( data );
        streamedSize += chunks[ i ].GetSize();
    }

    // The same jobs, each compressed with no history
    size_t independentSize = 0;
    for ( size_t i = 0; i < kNumJobs; ++i )
    {
        JobStreamEncoder single;
        TEST_ASSERT( single.Encode( jobs[ i ].Begin(), jobs[ i ].GetSize() ) );
        independentSize += single.GetResultSize();
    }

    // Only the first job pays for the shared part
    TEST_ASSERT( streamedSize < ( independentSize / 3 ) );
    TEST_ASSERT( chunks[ 1 ].GetSize() < ( chunks[ 0 ].GetSize() / 10 ) );
}

// FreshStreamAfterReset
//------------------------------------------------------------------------------
void TestJobStream::FreshStreamAfterReset() const
{
    Array<Array<char>> jobs;
    jobs.SetSize( 3 );
    for ( uint32_t i = 0; i < 3; ++i )
    {
        MakeJob( i, jobs[ i ] );
    }

    JobStreamEncoder first;
    Array<Array<char>> firstChunks;
    EncodeAll( first, jobs, firstChunks );
    JobStreamDecoder firstDecoder;
    for ( size_t i = 0; i < 3; ++i )
    {
        void * data = nullptr;
        size_t size = 0;
        TEST_ASSERT( firstDecoder.Decode( firstChunks[ i ].Begin(), firstChunks[ i ].GetSize(), data, size ) );
        FREE( data );
    }

    // A new connection: neither end remembers the old one
    JobStreamEncoder second;
    Array<Array<char>> secondChunks;
    EncodeAll( second, jobs, secondChunks );
    TEST_ASSERT( secondChunks[ 0 ].GetSize() > ( 10 * firstChunks[ 2 ].GetSize() ) ); // Carries the shared part again
    JobStreamDecoder secondDecoder;
    for ( size_t i = 0; i < 3; ++i )
    {
        void * data = nullptr;
        size_t size = 0;
        TEST_ASSERT( secondDecoder.Decode( secondChunks[ i ].Begin(), secondChunks[ i ].GetSize(), data, size ) );
        TEST_ASSERT( memcmp( data, jobs[ i ].Begin(), size ) == 0 );
        FREE( data );
    }
}

// RejectsBadChunks
//------------------------------------------------------------------------------
void TestJobStream::RejectsBadChunks() const
{
    Array<Array<char>> jobs;
    jobs.SetSize( 3 );
    for ( uint32_t i = 0; i < 3; ++i )
    {
        MakeJob( i, jobs[ i ] );
    }
    JobStreamEncoder encoder;
    Array<Array<char>> chunks;
    EncodeAll( encoder, jobs, chunks );

    // Each of these is refused without a crash or a result
    const size_t headerSize = sizeof( uint32_t );
    const Array<char> & good = chunks[ 0 ];
    auto refused = [ & ]( const void * chunk, size_t chunkSize )
    {
        JobStreamDecoder decoder;
        void * data = reinterpret_cast<void *>( 1 );
        size_t size = 1;
        const bool ok = decoder.Decode( chunk, chunkSize, data, size );
        return ( ok == false ) && ( data == nullptr ) && ( size == 0 );
    };

    // Out of order: the second job of a stream is not the start of one
    TEST_ASSERT( refused( chunks[ 1 ].Begin(), chunks[ 1 ].GetSize() ) );

    // Too short to hold anything, or declaring nothing
    TEST_ASSERT( refused( good.Begin(), headerSize ) );
    {
        Array<char> empty( good );
        const uint32_t zero = 0;
        memcpy( empty.Begin(), &zero, headerSize );
        TEST_ASSERT( refused( empty.Begin(), empty.GetSize() ) );
    }

    // Cut short
    TEST_ASSERT( refused( good.Begin(), good.GetSize() - 100 ) );

    // Declared size does not match the data, in either direction
    {
        Array<char> bad( good );
        uint32_t size;
        memcpy( &size, bad.Begin(), headerSize );
        const uint32_t larger = ( size + 1 );
        memcpy( bad.Begin(), &larger, headerSize );
        TEST_ASSERT( refused( bad.Begin(), bad.GetSize() ) );
        const uint32_t smaller = ( size - 1 );
        memcpy( bad.Begin(), &smaller, headerSize );
        TEST_ASSERT( refused( bad.Begin(), bad.GetSize() ) );
    }

    // Not zstd data
    {
        Array<char> bad( good );
        bad[ headerSize ] ^= 0xFF;
        TEST_ASSERT( refused( bad.Begin(), bad.GetSize() ) );
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

// RejectsLargerWindow
//------------------------------------------------------------------------------
void TestJobStream::RejectsLargerWindow() const
{
    Array<char> job;
    MakeJob( 0, job );

    // A window the decoder accepts
    {
        JobStreamEncoder encoder( JobStream::kWindowLog );
        TEST_ASSERT( encoder.Encode( job.Begin(), job.GetSize() ) );
        JobStreamDecoder decoder;
        void * data = nullptr;
        size_t size = 0;
        TEST_ASSERT( decoder.Decode( encoder.GetResult(), encoder.GetResultSize(), data, size ) );
        FREE( data );
    }

    // The next size up would make the decoder hold twice the history, so it is refused
    {
        JobStreamEncoder encoder( JobStream::kWindowLog + 1 );
        TEST_ASSERT( encoder.Encode( job.Begin(), job.GetSize() ) );
        JobStreamDecoder decoder;
        void * data = nullptr;
        size_t size = 0;
        TEST_ASSERT( decoder.Decode( encoder.GetResult(), encoder.GetResultSize(), data, size ) == false );
        TEST_ASSERT( data == nullptr );
    }
}

// MemoryPerStream
//------------------------------------------------------------------------------
void TestJobStream::MemoryPerStream() const
{
    Array<char> job;

    JobStreamEncoder encoder;
    JobStreamDecoder decoder;
    for ( uint32_t i = 0; i < 4; ++i )
    {
        MakeJob( i, job );
        TEST_ASSERT( encoder.Encode( job.Begin(), job.GetSize() ) );
        void * data = nullptr;
        size_t size = 0;
        TEST_ASSERT( decoder.Decode( encoder.GetResult(), encoder.GetResultSize(), data, size ) );
        FREE( data );
    }

    // What one connection holds on each end, as reserved by zstd
    const size_t encoderMemory = encoder.GetMemoryUsage();
    const size_t decoderMemory = decoder.GetMemoryUsage();
    OUTPUT( "JobStream memory per connection: encoder %u MiB, decoder %u MiB (%u connections: %u MiB, %u MiB)\n",
            (uint32_t)( encoderMemory / MEGABYTE ), (uint32_t)( decoderMemory / MEGABYTE ), 15u,
            (uint32_t)( 15 * encoderMemory / MEGABYTE ), (uint32_t)( 15 * decoderMemory / MEGABYTE ) );

    // History for the window, and not much more
    const size_t window = ( (size_t)1 << JobStream::kWindowLog );
    TEST_ASSERT( encoderMemory > 0 );
    TEST_ASSERT( decoderMemory >= window );
    TEST_ASSERT( decoderMemory < ( window + ( 8 * MEGABYTE ) ) );
    TEST_ASSERT( encoderMemory < ( window + ( 24 * MEGABYTE ) ) );
}

//------------------------------------------------------------------------------
