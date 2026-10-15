// TestPchTransform.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

#include "Tools/FBuild/FBuildCore/Helpers/PchTransform.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Math/Conversions.h"
#include "Core/Process/Atomic.h"
#include "Core/Strings/AStackString.h"

// External
#include "zstd.h"

#include <string.h>

// TestPchTransform
//------------------------------------------------------------------------------
class TestPchTransform : public FBuildTest
{
private:
    DECLARE_TESTS

    void RoundTripsAnyBytes() const;
    void RoundTripsLargerData() const;
    void WordsThatLookLikeReferencesAreEscaped() const;
    void ShrinksWhatHasPointers() const;
    void GrowthIsBounded() const;
    void DecoderRejectsInvalidStreams() const;
    void DecoderSurvivesGarbage() const;
    void SinksThatRefuseStopThePieces() const;
    void FindsTheBase() const;
    void ChoosesOnlyWhatItHelps() const;
    void CacheOrdersByUse() const;

    // The words of a kind of data, with the high halves a transform with this base gives a meaning to
    static constexpr uint32_t kNumKinds = 9;
    static void MakeData( uint32_t kind, uint32_t size, uint64_t seed, uint32_t base, Array<char> & outData );
    static void RoundTrip( const Array<char> & data, uint32_t base, uint64_t seed, uint64_t * outTransformedSize = nullptr );
    static bool WriteFile( const AString & path, const Array<char> & content );
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestPchTransform )
    REGISTER_TEST( RoundTripsAnyBytes )
    REGISTER_TEST( RoundTripsLargerData )
    REGISTER_TEST( WordsThatLookLikeReferencesAreEscaped )
    REGISTER_TEST( ShrinksWhatHasPointers )
    REGISTER_TEST( GrowthIsBounded )
    REGISTER_TEST( DecoderRejectsInvalidStreams )
    REGISTER_TEST( DecoderSurvivesGarbage )
    REGISTER_TEST( SinksThatRefuseStopThePieces )
    REGISTER_TEST( FindsTheBase )
    REGISTER_TEST( ChoosesOnlyWhatItHelps )
    REGISTER_TEST( CacheOrdersByUse )
REGISTER_TESTS_END

// Helpers
//------------------------------------------------------------------------------
namespace
{
    // The same numbers for the same seed
    class XfRandom
    {
    public:
        explicit XfRandom( uint64_t seed ) : m_State( seed | 1 ) {}
        uint64_t Next()
        {
            m_State ^= ( m_State << 13 );
            m_State ^= ( m_State >> 7 );
            m_State ^= ( m_State << 17 );
            return m_State;
        }
        uint32_t Below( uint32_t limit ) { return (uint32_t)( Next() % limit ); }

    private:
        uint64_t m_State;
    };

    // Collects what a transform makes
    class XfCollectSink : public PchTransform::Sink
    {
    public:
        virtual bool OnPiece( const void * data, size_t size ) override
        {
            const size_t oldSize = m_Data.GetSize();
            if ( ( oldSize + size ) > m_Data.GetCapacity() )
            {
                m_Data.SetCapacity( Math::Max( ( oldSize + size ), ( m_Data.GetCapacity() * 2 ) ) );
            }
            m_Data.SetSize( oldSize + size );
            memcpy( m_Data.Begin() + oldSize, data, size );
            ++m_NumPieces;
            return true;
        }
        Array<char> m_Data;
        uint32_t m_NumPieces = 0;
    };

    // Refuses everything
    class XfRefusingSink : public PchTransform::Sink
    {
    public:
        virtual bool OnPiece( const void *, size_t ) override { return false; }
    };

    void XfPutWord( Array<char> & data, uint64_t word )
    {
        for ( uint32_t b = 0; b < 8; ++b )
        {
            data.Append( (char)( word >> ( 8 * b ) ) );
        }
    }

    // Feeds in pieces of random sizes, up to maxPiece (which can be 1)
    template <class TRANSFORM>
    bool XfFeedInPieces( TRANSFORM & transform, const void * data, size_t size, uint32_t maxPiece, XfRandom & random, PchTransform::Sink & sink )
    {
        const char * bytes = static_cast<const char *>( data );
        for ( size_t done = 0; done < size; )
        {
            const size_t piece = Math::Min<size_t>( size - done, 1 + random.Below( maxPiece ) );
            if ( transform.Feed( bytes + done, piece, sink ) == false )
            {
                return false;
            }
            done += piece;
        }
        return true;
    }

    uint64_t XfZstdSize( const Array<char> & data )
    {
        Array<char> out;
        out.SetSize( ZSTD_compressBound( data.GetSize() ) );
        return ZSTD_compress( out.Begin(), out.GetSize(), data.Begin(), data.GetSize(), 3 );
    }
}

// MakeData
//------------------------------------------------------------------------------
/*static*/ void TestPchTransform::MakeData( uint32_t kind, uint32_t size, uint64_t seed, uint32_t base, Array<char> & outData )
{
    XfRandom random( seed );
    const uint32_t f1 = ( base | 0x80000000u );
    const uint32_t f2 = ( base | 0x40000000u );
    const uint32_t f3 = ( base | 0x20000000u );
    outData.Clear();
    outData.SetCapacity( size + 8 );

    const uint32_t numWords = ( size / 8 );
    uint32_t recent[ 8 ] = { 0x1000, 0x1008, 0x2000, 0x2010, 0x9000, 0x9040, 0xFFFFFFF0, 5 };
    for ( uint32_t i = 0; i < numWords; ++i )
    {
        uint64_t word = 0;
        switch ( kind )
        {
            case 0: word = random.Next(); break;                                        // XfRandom
            case 1: word = 0; break;                                                    // Zeros
            case 2: word = ( ( (uint64_t)base << 32 ) | recent[ random.Below( 8 ) ] ); break; // Pointers that repeat
            case 3:                                                                     // Pointers nearest each other, and far
            {
                recent[ random.Below( 8 ) ] = random.Below( 4 ) ? ( recent[ random.Below( 8 ) ] + random.Below( 6000 ) - 3000 ) : (uint32_t)random.Next();
                word = ( ( (uint64_t)base << 32 ) | recent[ random.Below( 8 ) ] );
                break;
            }
            case 4:                                                                     // The words a transform gives a meaning to
            {
                const uint32_t highs[] = { base, f1, f2, f3, 0, 0xFFFFFFFFu, base ^ 1u, ( f1 ^ f2 ), ( f3 | 1u ) };
                const uint32_t lows[] = { 0, 1, 31, 32, 33, 0xFFFF, 0x10000, 0x1F0000, 0x200000, 0xFFFFFFFFu, 0x7FF, 0x800, 0x801, 0x1000 };
                word = ( ( (uint64_t)highs[ random.Below( 9 ) ] << 32 ) | lows[ random.Below( 14 ) ] );
                break;
            }
            case 5:                                                                     // A mixture
            {
                const uint32_t pick = random.Below( 5 );
                if ( pick == 0 ) { word = random.Next(); }
                else if ( pick == 1 ) { word = ( ( (uint64_t)base << 32 ) | recent[ random.Below( 8 ) ] ); }
                else if ( pick == 2 ) { word = ( ( (uint64_t)( random.Below( 2 ) ? f1 : f2 ) << 32 ) | random.Below( 40 ) ); }
                else if ( pick == 3 ) { word = ( ( (uint64_t)f3 << 32 ) | random.Below( 3 ) ); }
                else { word = ( ( (uint64_t)base << 32 ) | ( recent[ random.Below( 8 ) ] + random.Below( 4096 ) - 2048 ) ); }
                break;
            }
            case 6:                                                                     // Escapes
                word = ( (uint64_t)f3 << 32 );
                break;
            case 7:                                                                     // Pointers each of which is nearest the one before
                recent[ 0 ] += random.Below( 1500 ) - 700;
                word = ( ( (uint64_t)base << 32 ) | recent[ 0 ] );
                break;
            default: break;
        }
        XfPutWord( outData, word );
    }

    // The tail, and for kind 8 random bytes in which the bytes of the base are put at any offset
    for ( uint32_t i = ( numWords * 8 ); i < size; ++i )
    {
        outData.Append( (char)random.Next() );
    }
    if ( kind == 8 )
    {
        outData.SetSize( size );
        for ( char & c : outData )
        {
            c = (char)random.Next();
        }
        for ( uint32_t i = 0; ( i + 8 ) <= size; i += 1 + random.Below( 11 ) )
        {
            for ( uint32_t b = 0; b < 4; ++b )
            {
                outData[ i + 4 + b ] = (char)( base >> ( 8 * b ) );
            }
        }
    }
    ASSERT( outData.GetSize() == size );
}

// RoundTrip
//------------------------------------------------------------------------------
// The inverse of the transform gives the data back, whatever the sizes of the pieces either is fed
/*static*/ void TestPchTransform::RoundTrip( const Array<char> & data, uint32_t base, uint64_t seed, uint64_t * outTransformedSize )
{
    XfRandom random( seed );
    const uint32_t maxPieces[] = { 1, 3, 8, 13, 4096, 1000000 };
    const bool isLarge = ( data.GetSize() > 70000 ); // A piece of a byte or so would take long
    const uint32_t encodePiece = maxPieces[ ( isLarge ? 4 : 0 ) + random.Below( isLarge ? 2 : 6 ) ];
    const uint32_t decodePiece = maxPieces[ ( isLarge ? 4 : 0 ) + random.Below( isLarge ? 2 : 6 ) ];

    XfCollectSink transformed;
    {
        PchTransform::Encoder encoder( base );
        TEST_ASSERT( XfFeedInPieces( encoder, data.Begin(), data.GetSize(), encodePiece, random, transformed ) );
        TEST_ASSERT( encoder.Finish( transformed ) );
    }
    TEST_ASSERT( transformed.m_Data.GetSize() >= 4 );
    TEST_ASSERT( transformed.m_Data.GetSize() <= PchTransform::GetMaxTransformedSize( data.GetSize() ) );

    XfCollectSink restored;
    {
        PchTransform::Decoder decoder;
        TEST_ASSERT( XfFeedInPieces( decoder, transformed.m_Data.Begin(), transformed.m_Data.GetSize(), decodePiece, random, restored ) );
        TEST_ASSERT( decoder.Finish( restored ) );
    }
    TEST_ASSERT( restored.m_Data.GetSize() == data.GetSize() );
    TEST_ASSERT( ( data.GetSize() == 0 ) || ( memcmp( restored.m_Data.Begin(), data.Begin(), data.GetSize() ) == 0 ) );
    if ( outTransformedSize )
    {
        *outTransformedSize = transformed.m_Data.GetSize();
    }
}

// WriteFile
//------------------------------------------------------------------------------
/*static*/ bool TestPchTransform::WriteFile( const AString & path, const Array<char> & content )
{
    FileStream f;
    return ( FileIO::EnsurePathExistsForFile( path ) &&
             f.Open( path.Get(), FileStream::WRITE_ONLY ) &&
             ( f.Write( content.Begin(), content.GetSize() ) == content.GetSize() ) );
}

// RoundTripsAnyBytes
//------------------------------------------------------------------------------
void TestPchTransform::RoundTripsAnyBytes() const
{
    // Every size up to a few words past the cache, every kind of data, and bases that are the extremes
    const uint32_t bases[] = { 0x6BB, 0, 1, PchTransform::kMaxBase, 0x1000000 };
    uint64_t seed = 1;
    for ( uint32_t size = 0; size <= 300; ++size )
    {
        for ( uint32_t kind = 0; kind < kNumKinds; ++kind )
        {
            for ( const uint32_t base : bases )
            {
                Array<char> data;
                MakeData( kind, size, ++seed, base, data );
                RoundTrip( data, base, seed * 7 );
            }
        }
    }

    // Sizes around the sizes of the pieces of a transform's result, and of a file
    const uint32_t sizes[] = { 4095, 4096, 4097, 8191, 8192, 8193, 65535, 65536, 65537, 65536 + 7, 131072 + 1, 1000003 };
    for ( const uint32_t size : sizes )
    {
        for ( uint32_t kind = 0; kind < kNumKinds; ++kind )
        {
            Array<char> data;
            MakeData( kind, size, ++seed, 0x6BB, data );
            RoundTrip( data, 0x6BB, seed * 11 );
        }
    }
}

// RoundTripsLargerData
//------------------------------------------------------------------------------
void TestPchTransform::RoundTripsLargerData() const
{
    // What the test PCHs are like, in the sizes of pieces a file is read in
    const uint32_t sizes[] = { ( 1024 * 1024 ) - 3, ( 1024 * 1024 ), ( 5 * 1024 * 1024 ) + 5 };
    for ( const uint32_t size : sizes )
    {
        Array<char> data;
        MakePchLikeBytes( size, size, data );
        RoundTrip( data, kPchLikeBase, size );
    }
}

// WordsThatLookLikeReferencesAreEscaped
//------------------------------------------------------------------------------
void TestPchTransform::WordsThatLookLikeReferencesAreEscaped() const
{
    const uint32_t base = 0x6BB;
    const uint32_t highs[] = { ( base | 0x80000000u ), ( base | 0x40000000u ), ( base | 0x20000000u ) };
    for ( const uint32_t high : highs )
    {
        Array<char> data;
        XfPutWord( data, ( (uint64_t)high << 32 ) | 0x12345678u );
        XfCollectSink transformed;
        PchTransform::Encoder encoder( base );
        TEST_ASSERT( encoder.Feed( data.Begin(), data.GetSize(), transformed ) );
        TEST_ASSERT( encoder.Finish( transformed ) );

        // The base, an escape and the word
        TEST_ASSERT( transformed.m_Data.GetSize() == ( 4 + 8 + 8 ) );
        TEST_ASSERT( memcmp( transformed.m_Data.Begin() + 12, data.Begin(), 8 ) == 0 );
        RoundTrip( data, base, high );
    }

    // A pointer is not an escape, and what is not one is the same
    {
        Array<char> data;
        XfPutWord( data, ( (uint64_t)base << 32 ) | 0x1000u );        // Not in the cache: as it is
        XfPutWord( data, ( (uint64_t)base << 32 ) | 0x1000u );        // The same: a reference
        XfPutWord( data, ( (uint64_t)base << 32 ) | 0x1010u );        // Near: a reference
        XfPutWord( data, 0x123456789ABCDEF0ull );                     // Not a pointer
        XfCollectSink transformed;
        PchTransform::Encoder encoder( base );
        TEST_ASSERT( encoder.Feed( data.Begin(), data.GetSize(), transformed ) );
        TEST_ASSERT( encoder.Finish( transformed ) );
        TEST_ASSERT( transformed.m_Data.GetSize() == ( 4 + 32 ) );
        TEST_ASSERT( memcmp( transformed.m_Data.Begin() + 4, data.Begin(), 8 ) == 0 );
        TEST_ASSERT( memcmp( transformed.m_Data.Begin() + 28, data.Begin() + 24, 8 ) == 0 );
        const uint8_t * second = reinterpret_cast<const uint8_t *>( transformed.m_Data.Begin() + 12 );
        TEST_ASSERT( ( second[ 4 ] == 0xBB ) && ( second[ 5 ] == 0x06 ) && ( second[ 6 ] == 0x00 ) && ( second[ 7 ] == 0x80 ) ); // F1
        TEST_ASSERT( second[ 0 ] == 0 ); // Entry 0 of the cache
        const uint8_t * third = reinterpret_cast<const uint8_t *>( transformed.m_Data.Begin() + 20 );
        TEST_ASSERT( ( third[ 7 ] == 0x40 ) ); // F2
        TEST_ASSERT( ( third[ 0 ] | ( third[ 1 ] << 8 ) ) == ( 0x10 + 2048 ) ); // 0x10 past entry 0
        RoundTrip( data, base, 99 );
    }
}

// ShrinksWhatHasPointers
//------------------------------------------------------------------------------
void TestPchTransform::ShrinksWhatHasPointers() const
{
    Array<char> data;
    MakePchLikeBytes( ( 4 * 1024 * 1024 ), 5, data );
    uint64_t transformedSize = 0;
    RoundTrip( data, kPchLikeBase, 5, &transformedSize );

    Array<char> transformed;
    {
        XfCollectSink sink;
        PchTransform::Encoder encoder( kPchLikeBase );
        TEST_ASSERT( encoder.Feed( data.Begin(), data.GetSize(), sink ) );
        TEST_ASSERT( encoder.Finish( sink ) );
        transformed = sink.m_Data;
    }
    const uint64_t plain = XfZstdSize( data );
    const uint64_t after = XfZstdSize( transformed );
    OUTPUT( "ShrinksWhatHasPointers: zstd %" PRIu64 " -> %" PRIu64 " bytes (%.1f%%)\n", plain, after, ( 100.0 * (double)after ) / (double)plain );
    TEST_ASSERT( ( after * 100 ) < ( plain * 85 ) ); // At least a seventh smaller
}

// GrowthIsBounded
//------------------------------------------------------------------------------
void TestPchTransform::GrowthIsBounded() const
{
    // XfRandom bytes have no pointers: the base and nothing else is added
    {
        Array<char> data;
        MakeRandomBytes( 100000, 3, data );
        uint64_t size = 0;
        RoundTrip( data, 0x6BB, 1, &size );
        TEST_ASSERT( size <= ( data.GetSize() + 4 + 64 ) );
    }

    // Data of nothing but escapes is the worst there is
    {
        Array<char> data;
        MakeData( 6, 80000, 1, 0x6BB, data );
        uint64_t size = 0;
        RoundTrip( data, 0x6BB, 2, &size );
        TEST_ASSERT( size == PchTransform::GetMaxTransformedSize( data.GetSize() ) );
    }

    // Data of references does not grow
    {
        Array<char> data;
        MakeData( 2, 80000, 1, 0x6BB, data );
        uint64_t size = 0;
        RoundTrip( data, 0x6BB, 3, &size );
        TEST_ASSERT( size <= ( data.GetSize() + 4 ) );
    }
}

// DecoderRejectsInvalidStreams
//------------------------------------------------------------------------------
void TestPchTransform::DecoderRejectsInvalidStreams() const
{
    const uint32_t base = 0x6BB;
    auto header = [ & ]( Array<char> & stream, uint32_t b )
    {
        for ( uint32_t i = 0; i < 4; ++i )
        {
            stream.Append( (char)( b >> ( 8 * i ) ) );
        }
    };

    // A base that has the bits of a reference
    {
        Array<char> stream;
        header( stream, 0x80000000u );
        XfCollectSink sink;
        PchTransform::Decoder decoder;
        TEST_ASSERT( decoder.Feed( stream.Begin(), stream.GetSize(), sink ) == false );
        TEST_ASSERT( decoder.Feed( stream.Begin(), 1, sink ) == false ); // It stays failed
        TEST_ASSERT( decoder.Finish( sink ) == false );
    }
    {
        Array<char> stream;
        header( stream, PchTransform::kMaxBase + 1 );
        XfCollectSink sink;
        PchTransform::Decoder decoder;
        TEST_ASSERT( decoder.Feed( stream.Begin(), stream.GetSize(), sink ) == false );
    }

    // A reference to an entry that is not in the cache
    const uint32_t f1 = ( base | 0x80000000u );
    const uint32_t f2 = ( base | 0x40000000u );
    const uint32_t f3 = ( base | 0x20000000u );
    const uint64_t references[] = { ( (uint64_t)f1 << 32 ), ( (uint64_t)f1 << 32 ) | 3, ( (uint64_t)f2 << 32 ) | ( 1u << 16 ) | 2048 };
    for ( const uint64_t reference : references )
    {
        Array<char> stream;
        header( stream, base );
        XfPutWord( stream, ( (uint64_t)base << 32 ) | 0x1000 ); // One entry
        XfPutWord( stream, reference );
        XfCollectSink sink;
        PchTransform::Decoder decoder;
        const bool fedOk = decoder.Feed( stream.Begin(), stream.GetSize(), sink );
        const bool isValid = ( reference == ( (uint64_t)f1 << 32 ) ); // Entry 0 is there
        TEST_ASSERT( fedOk == isValid );
    }

    // An escape that is not followed by a word, and a stream that ends in its base
    {
        Array<char> stream;
        header( stream, base );
        XfPutWord( stream, ( (uint64_t)f3 << 32 ) );
        XfCollectSink sink;
        PchTransform::Decoder decoder;
        TEST_ASSERT( decoder.Feed( stream.Begin(), stream.GetSize(), sink ) );
        TEST_ASSERT( decoder.Finish( sink ) == false );
    }
    {
        Array<char> stream;
        header( stream, base );
        XfPutWord( stream, ( (uint64_t)f3 << 32 ) );
        stream.Append( 'x' ); // Part of the word the escape is for
        XfCollectSink sink;
        PchTransform::Decoder decoder;
        TEST_ASSERT( decoder.Feed( stream.Begin(), stream.GetSize(), sink ) );
        TEST_ASSERT( decoder.Finish( sink ) == false );
    }
    {
        Array<char> stream;
        header( stream, base );
        XfCollectSink sink;
        PchTransform::Decoder decoder;
        TEST_ASSERT( decoder.Feed( stream.Begin(), 3, sink ) ); // Not all of it
        TEST_ASSERT( decoder.Finish( sink ) == false );
    }

    // A stream of nothing at all is the transform of nothing
    {
        Array<char> stream;
        header( stream, base );
        XfCollectSink sink;
        PchTransform::Decoder decoder;
        TEST_ASSERT( decoder.Feed( stream.Begin(), stream.GetSize(), sink ) );
        TEST_ASSERT( decoder.Finish( sink ) );
        TEST_ASSERT( sink.m_Data.GetSize() == 0 );
    }
}

// DecoderSurvivesGarbage
//------------------------------------------------------------------------------
void TestPchTransform::DecoderSurvivesGarbage() const
{
    // Whatever is fed is a failure or data, and never more than a stream of its size can make
    XfRandom random( 77 );
    for ( uint32_t i = 0; i < 400; ++i )
    {
        Array<char> stream;
        stream.SetSize( 4 + random.Below( 600 ) );
        for ( char & c : stream )
        {
            c = (char)random.Next();
        }
        const uint32_t base = ( random.Below( 3 ) == 0 ) ? 0x6BBu : ( (uint32_t)random.Next() & PchTransform::kMaxBase );
        for ( uint32_t b = 0; b < 4; ++b )
        {
            stream[ b ] = (char)( base >> ( 8 * b ) );
        }

        // Words that name entries of the cache, so that some of them are valid
        for ( size_t w = 4; ( w + 8 ) <= stream.GetSize(); w += 8 )
        {
            if ( random.Below( 2 ) )
            {
                const uint32_t high = base | ( random.Below( 2 ) ? 0x80000000u : 0x40000000u ) | ( random.Below( 4 ) ? 0 : 0x20000000u );
                const uint32_t low = random.Below( 36 ) | ( random.Below( 2 ) ? ( random.Below( 5 ) << 16 ) : 0 );
                for ( uint32_t b = 0; b < 4; ++b )
                {
                    stream[ w + b ] = (char)( low >> ( 8 * b ) );
                    stream[ w + 4 + b ] = (char)( high >> ( 8 * b ) );
                }
            }
        }

        XfCollectSink sink;
        PchTransform::Decoder decoder;
        XfFeedInPieces( decoder, stream.Begin(), stream.GetSize(), 1 + random.Below( 50 ), random, sink ); // Either way
        TEST_ASSERT( sink.m_Data.GetSize() <= stream.GetSize() );
    }
}

// SinksThatRefuseStopThePieces
//------------------------------------------------------------------------------
void TestPchTransform::SinksThatRefuseStopThePieces() const
{
    Array<char> data;
    MakePchLikeBytes( 300000, 1, data );

    // The encoder
    {
        XfRefusingSink sink;
        PchTransform::Encoder encoder( kPchLikeBase );
        TEST_ASSERT( encoder.Feed( data.Begin(), data.GetSize(), sink ) == false );
    }
    {
        XfRefusingSink sink;
        PchTransform::Encoder encoder( kPchLikeBase );
        TEST_ASSERT( encoder.Feed( data.Begin(), 3, sink ) == false ); // What it made of three bytes is its base
    }
    {
        XfRefusingSink sink;
        PchTransform::Encoder encoder( kPchLikeBase );
        TEST_ASSERT( encoder.Finish( sink ) == false );
    }

    // The decoder
    XfCollectSink transformed;
    {
        PchTransform::Encoder encoder( kPchLikeBase );
        TEST_ASSERT( encoder.Feed( data.Begin(), data.GetSize(), transformed ) );
        TEST_ASSERT( encoder.Finish( transformed ) );
    }
    {
        XfRefusingSink sink;
        PchTransform::Decoder decoder;
        TEST_ASSERT( decoder.Feed( transformed.m_Data.Begin(), transformed.m_Data.GetSize(), sink ) == false );
        TEST_ASSERT( decoder.Finish( sink ) == false );
    }
}

// FindsTheBase
//------------------------------------------------------------------------------
void TestPchTransform::FindsTheBase() const
{
    uint32_t base = 0;

    // What most words have
    {
        Array<char> data;
        MakePchLikeBytes( 100000, 2, data );
        TEST_ASSERT( PchTransform::FindBase( data.Begin(), data.GetSize(), base ) );
        TEST_ASSERT( base == kPchLikeBase );
    }
    {
        Array<char> data;
        MakeData( 2, 80000, 1, 0x12345, data );
        TEST_ASSERT( PchTransform::FindBase( data.Begin(), data.GetSize(), base ) );
        TEST_ASSERT( base == 0x12345 );
    }

    // No base: nothing in common, nothing but zeros, no words, or too few
    {
        Array<char> data;
        MakeRandomBytes( 100000, 4, data );
        TEST_ASSERT( PchTransform::FindBase( data.Begin(), data.GetSize(), base ) == false );
        MakeData( 1, 100000, 1, 0x6BB, data );
        TEST_ASSERT( PchTransform::FindBase( data.Begin(), data.GetSize(), base ) == false );
        TEST_ASSERT( PchTransform::FindBase( data.Begin(), 7, base ) == false );
        TEST_ASSERT( PchTransform::FindBase( data.Begin(), 0, base ) == false );
    }
    {
        // One word in twenty has the high half
        Array<char> data;
        XfRandom random( 3 );
        for ( uint32_t i = 0; i < 20000; ++i )
        {
            XfPutWord( data, ( ( i % 20 ) == 0 ) ? ( ( (uint64_t)0x6BB << 32 ) | random.Below( 1000 ) ) : random.Next() );
        }
        TEST_ASSERT( PchTransform::FindBase( data.Begin(), data.GetSize(), base ) == false );
    }

    // A base the transform cannot use: it has a bit that marks a reference
    {
        Array<char> data;
        for ( uint32_t i = 0; i < 1000; ++i )
        {
            XfPutWord( data, ( (uint64_t)0x80000123u << 32 ) | i );
        }
        TEST_ASSERT( PchTransform::FindBase( data.Begin(), data.GetSize(), base ) == false );
    }
}

// ChoosesOnlyWhatItHelps
//------------------------------------------------------------------------------
void TestPchTransform::ChoosesOnlyWhatItHelps() const
{
    AStackString<> dir( "../tmp/Test/PchTransform/Choose/" );
    TEST_ASSERT( FileIO::EnsurePathExists( dir ) );
    AStackString<> path( dir );
    path += "data.bin";
    uint32_t base = 0;

    // A file of pointers that repeat and are nearest each other: in a megabyte and in more
    const uint32_t sizes[] = { ( 100 * 1024 ), ( 3 * 1024 * 1024 ) + 1, ( 10 * 1024 * 1024 ) + 3 };
    for ( const uint32_t size : sizes )
    {
        Array<char> data;
        MakePchLikeBytes( size, size, data );
        TEST_ASSERT( WriteFile( path, data ) );
        base = 0;
        TEST_ASSERT( PchTransform::Choose( path.Get(), size, 3, nullptr, base ) );
        TEST_ASSERT( base == kPchLikeBase );
    }

    // Not a file that is what it was said to be, or that is asked to stop
    {
        Array<char> data;
        MakePchLikeBytes( 500000, 1, data );
        TEST_ASSERT( WriteFile( path, data ) );
        TEST_ASSERT( PchTransform::Choose( path.Get(), 499999, 3, nullptr, base ) == false );
        const Atomic<bool> aborted{ true };
        TEST_ASSERT( PchTransform::Choose( path.Get(), 500000, 3, &aborted, base ) == false );
        TEST_ASSERT( PchTransform::Choose( "../tmp/Test/PchTransform/Choose/not-there", 500000, 3, nullptr, base ) == false );
    }

    // Data it does not help: there is no base, or there is one and pointers do not repeat or come nearest each other
    struct Case { const char * m_Name; uint32_t m_Kind; uint32_t m_Size; };
    const Case cases[] = {
        { "random",             0, 2000000 },
        { "zeros",              1, 2000000 },
        { "random pointers",    10, 2000000 },
        { "tiny",               2, 5 },
        { "empty",              2, 0 },
    };
    for ( const Case & c : cases )
    {
        Array<char> data;
        if ( c.m_Kind == 10 )
        {
            // The same high half and a low half that is anything
            XfRandom random( 6 );
            for ( uint32_t i = 0; i < ( c.m_Size / 8 ); ++i )
            {
                XfPutWord( data, ( (uint64_t)0x6BB << 32 ) | (uint32_t)random.Next() );
            }
        }
        else
        {
            MakeData( c.m_Kind, c.m_Size, 1, 0x6BB, data );
        }
        TEST_ASSERT( WriteFile( path, data ) );
        TEST_ASSERTM( PchTransform::Choose( path.Get(), data.GetSize(), 3, nullptr, base ) == false, "%s", c.m_Name );
    }
}

// CacheOrdersByUse
//------------------------------------------------------------------------------
void TestPchTransform::CacheOrdersByUse() const
{
    PchTransform::PointerCache cache;
    TEST_ASSERT( cache.GetCount() == 0 );
    TEST_ASSERT( cache.Find( 5 ) == -1 );
    int32_t delta = 0;
    TEST_ASSERT( cache.FindNear( 5, delta ) == -1 );

    // The newest first, and it is only as large as it is
    for ( uint32_t i = 0; i < ( PchTransform::kCacheSize + 5 ); ++i )
    {
        cache.InsertAtFront( 10000 + ( i * 100000 ) );
    }
    TEST_ASSERT( cache.GetCount() == PchTransform::kCacheSize );
    TEST_ASSERT( cache.Get( 0 ) == ( 10000 + ( ( PchTransform::kCacheSize + 4 ) * 100000 ) ) );
    TEST_ASSERT( cache.Find( 10000 ) == -1 );                           // The oldest ones left
    TEST_ASSERT( cache.Find( 10000 + ( 5 * 100000 ) ) == (int32_t)( PchTransform::kCacheSize - 1 ) );

    // Moved to the front, the others keep their order
    uint32_t before[ PchTransform::kCacheSize ];
    for ( uint32_t i = 0; i < PchTransform::kCacheSize; ++i )
    {
        before[ i ] = cache.Get( i );
    }
    cache.MoveToFront( 7 );
    TEST_ASSERT( cache.Get( 0 ) == before[ 7 ] );
    TEST_ASSERT( cache.Find( before[ 7 ] ) == 0 );
    TEST_ASSERT( cache.GetCount() == PchTransform::kCacheSize );
    for ( uint32_t i = 1; i < PchTransform::kCacheSize; ++i )
    {
        TEST_ASSERT( cache.Get( i ) == before[ ( i <= 7 ) ? ( i - 1 ) : i ] );
    }

    // The nearest within the range, the first of two that are as nearest, and none beyond it
    PchTransform::PointerCache nearest;
    nearest.InsertAtFront( 5000 );     // 3
    nearest.InsertAtFront( 1000 );     // 2
    nearest.InsertAtFront( 3000 );     // 1
    nearest.InsertAtFront( 1500 );     // 0
    TEST_ASSERT( nearest.FindNear( 1700, delta ) == 0 );
    TEST_ASSERT( delta == 200 );
    TEST_ASSERT( nearest.FindNear( 1250, delta ) == 0 );                   // 250 from entry 0 and from entry 2
    TEST_ASSERT( delta == -250 );
    TEST_ASSERT( nearest.FindNear( 9000, delta ) == -1 );

    // The ends of the range
    PchTransform::PointerCache one;
    one.InsertAtFront( 10000 );
    TEST_ASSERT( one.FindNear( 10000 + 2047, delta ) == 0 );
    TEST_ASSERT( delta == 2047 );
    TEST_ASSERT( one.FindNear( 10000 - 2048, delta ) == 0 );
    TEST_ASSERT( delta == -2048 );
    TEST_ASSERT( one.FindNear( 10000 + 2048, delta ) == -1 );
    TEST_ASSERT( one.FindNear( 10000 - 2049, delta ) == -1 );
}

//------------------------------------------------------------------------------
