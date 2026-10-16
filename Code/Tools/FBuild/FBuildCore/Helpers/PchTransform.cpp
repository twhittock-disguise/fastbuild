// PchTransform.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "PchTransform.h"

// Core
#include "Core/Containers/UniquePtr.h"
#include "Core/Env/Assert.h"
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
    // A piece of the result is this large. Writing a word makes at most two words of result.
    constexpr size_t kOutSize = ( 64 * 1024 );
    constexpr size_t kMaxWordResult = 16;

    // The bits that tell a reference from a pointer: set in the high half of a word, they make F1, F2 and F3
    constexpr uint32_t kF1Bit = 0x80000000u;
    constexpr uint32_t kF2Bit = 0x40000000u;
    constexpr uint32_t kF3Bit = 0x20000000u;

    // Pieces of a file that are tried to judge the transform: this long, and no more than this many
    constexpr size_t kProbeLength = ( 1024 * 1024 );
    constexpr uint32_t kProbeCount = 8;

    // The fewest words with the base, as a share of the words, for the transform to be worth trying (1/8)
    constexpr uint32_t kMinBaseShare = 8;

    uint64_t Load64( const uint8_t * p )
    {
        uint64_t word = 0;
        for ( uint32_t i = 0; i < 8; ++i )
        {
            word |= ( (uint64_t)p[ i ] << ( 8 * i ) );
        }
        return word;
    }

    void Store64( uint8_t * p, uint64_t word )
    {
        for ( uint32_t i = 0; i < 8; ++i )
        {
            p[ i ] = (uint8_t)( word >> ( 8 * i ) );
        }
    }

    // Collects what a transform makes of a piece of data, into a buffer of the size it can be at most
    class CollectSink : public PchTransform::Sink
    {
    public:
        explicit CollectSink( size_t capacity )
            : m_Data( static_cast<char *>( ALLOC( capacity ) ) )
            , m_Capacity( capacity )
        {
        }
        virtual bool OnPiece( const void * data, size_t size ) override
        {
            if ( size > ( m_Capacity - m_Size ) )
            {
                return false;
            }
            memcpy( m_Data.Get() + m_Size, data, size );
            m_Size += size;
            return true;
        }
        const char * GetData() const { return m_Data.Get(); }
        size_t GetSize() const { return m_Size; }

    private:
        explicit CollectSink( const CollectSink & other ) = delete;
        CollectSink & operator=( const CollectSink & other ) = delete;

        UniquePtr<char, FreeDeletor> m_Data;
        size_t m_Capacity;
        size_t m_Size = 0;
    };

    // Compresses pieces of data with one context, at the level it was made for
    class SizeCompressor
    {
    public:
        explicit SizeCompressor( int32_t level, size_t maxInput )
            : m_Context( ZSTD_createCCtx() )
            , m_ScratchSize( ZSTD_compressBound( maxInput ) )
            , m_Scratch( static_cast<char *>( ALLOC( m_ScratchSize ) ) )
        {
            m_IsValid = ( m_Context != nullptr ) &&
                        ( ZSTD_isError( ZSTD_CCtx_setParameter( m_Context, ZSTD_c_compressionLevel, level ) ) == false );
        }
        ~SizeCompressor() { ZSTD_freeCCtx( m_Context ); }

        // What zstd makes of data, in bytes
        bool GetSize( const void * data, size_t size, uint64_t & outSize )
        {
            if ( m_IsValid == false )
            {
                return false;
            }
            const size_t result = ZSTD_compress2( m_Context, m_Scratch.Get(), m_ScratchSize, data, size );
            outSize = result;
            return ( ZSTD_isError( result ) == false );
        }

    private:
        explicit SizeCompressor( const SizeCompressor & other ) = delete;
        SizeCompressor & operator=( const SizeCompressor & other ) = delete;

        ZSTD_CCtx * m_Context;
        size_t m_ScratchSize;
        UniquePtr<char, FreeDeletor> m_Scratch;
        bool m_IsValid = false;
    };
}

// PointerCache
//------------------------------------------------------------------------------
int32_t PchTransform::PointerCache::Find( uint32_t offset ) const
{
    for ( uint32_t i = 0; i < m_Count; ++i )
    {
        if ( m_Offsets[ i ] == offset )
        {
            return (int32_t)i;
        }
    }
    return -1;
}

// FindNear
//------------------------------------------------------------------------------
int32_t PchTransform::PointerCache::FindNear( uint32_t offset, int32_t & outDelta ) const
{
    int32_t best = -1;
    int32_t bestDistance = 0;
    for ( uint32_t i = 0; i < m_Count; ++i )
    {
        const int32_t delta = (int32_t)( offset - m_Offsets[ i ] );
        if ( ( delta >= -kNearRange ) && ( delta < kNearRange ) )
        {
            const int32_t distance = ( delta < 0 ) ? -delta : delta;
            if ( ( best < 0 ) || ( distance < bestDistance ) )
            {
                best = (int32_t)i;
                bestDistance = distance;
                outDelta = delta;
            }
        }
    }
    return best;
}

// MoveToFront
//------------------------------------------------------------------------------
void PchTransform::PointerCache::MoveToFront( uint32_t index )
{
    ASSERT( index < m_Count );
    const uint32_t offset = m_Offsets[ index ];
    memmove( m_Offsets + 1, m_Offsets, index * sizeof( uint32_t ) );
    m_Offsets[ 0 ] = offset;
}

// InsertAtFront
//------------------------------------------------------------------------------
void PchTransform::PointerCache::InsertAtFront( uint32_t offset )
{
    const uint32_t numMoved = ( m_Count < kCacheSize ) ? m_Count : ( kCacheSize - 1 );
    memmove( m_Offsets + 1, m_Offsets, numMoved * sizeof( uint32_t ) );
    m_Offsets[ 0 ] = offset;
    if ( m_Count < kCacheSize )
    {
        ++m_Count;
    }
}

// Encoder CONSTRUCTOR
//------------------------------------------------------------------------------
PchTransform::Encoder::Encoder( uint32_t base )
    : m_Base( base )
    , m_Out( static_cast<uint8_t *>( ALLOC( kOutSize ) ) )
{
    ASSERT( IsValidBase( base ) );
    for ( uint32_t i = 0; i < 4; ++i )
    {
        m_Out[ m_OutSize++ ] = (uint8_t)( base >> ( 8 * i ) );
    }
}

// Encoder DESTRUCTOR
//------------------------------------------------------------------------------
PchTransform::Encoder::~Encoder()
{
    FREE( m_Out );
}

// Encoder::Feed
//------------------------------------------------------------------------------
bool PchTransform::Encoder::Feed( const void * data, size_t size, Sink & sink )
{
    const uint8_t * bytes = static_cast<const uint8_t *>( data );

    // A word that was started by the last piece
    if ( m_CarrySize > 0 )
    {
        while ( ( size > 0 ) && ( m_CarrySize < 8 ) )
        {
            m_Carry[ m_CarrySize++ ] = *bytes++;
            --size;
        }
        if ( m_CarrySize < 8 )
        {
            return true;
        }
        EncodeWord( Load64( m_Carry ) );
        m_CarrySize = 0;
    }

    for ( ; size >= 8; bytes += 8, size -= 8 )
    {
        if ( ( ( kOutSize - m_OutSize ) < kMaxWordResult ) && ( Flush( sink ) == false ) )
        {
            return false;
        }
        EncodeWord( Load64( bytes ) );
    }

    memcpy( m_Carry, bytes, size );
    m_CarrySize = (uint32_t)size;
    return Flush( sink );
}

// Encoder::Finish
//------------------------------------------------------------------------------
bool PchTransform::Encoder::Finish( Sink & sink )
{
    // The bytes that are not a whole word are the tail
    if ( ( kOutSize - m_OutSize ) < m_CarrySize )
    {
        if ( Flush( sink ) == false )
        {
            return false;
        }
    }
    memcpy( m_Out + m_OutSize, m_Carry, m_CarrySize );
    m_OutSize += m_CarrySize;
    m_CarrySize = 0;
    return Flush( sink );
}

// Encoder::EncodeWord
//------------------------------------------------------------------------------
void PchTransform::Encoder::EncodeWord( uint64_t word )
{
    ASSERT( ( kOutSize - m_OutSize ) >= kMaxWordResult );
    const uint32_t high = (uint32_t)( word >> 32 );
    const uint32_t f1 = ( m_Base | kF1Bit );
    const uint32_t f2 = ( m_Base | kF2Bit );
    const uint32_t f3 = ( m_Base | kF3Bit );

    if ( high == m_Base )
    {
        const uint32_t offset = (uint32_t)word;
        const int32_t same = m_Cache.Find( offset );
        if ( same >= 0 )
        {
            word = ( ( (uint64_t)f1 << 32 ) | (uint32_t)same );
            m_Cache.MoveToFront( (uint32_t)same );
        }
        else
        {
            int32_t delta = 0;
            const int32_t nearest = m_Cache.FindNear( offset, delta );
            if ( nearest >= 0 )
            {
                word = ( ( (uint64_t)f2 << 32 ) | ( (uint32_t)nearest << 16 ) | (uint32_t)( delta + kNearRange ) );
            }
            m_Cache.InsertAtFront( offset );
        }
    }
    else if ( ( high == f1 ) || ( high == f2 ) || ( high == f3 ) )
    {
        // Not to be taken for a reference
        Store64( m_Out + m_OutSize, ( (uint64_t)f3 << 32 ) );
        m_OutSize += 8;
    }
    Store64( m_Out + m_OutSize, word );
    m_OutSize += 8;
}

// Encoder::Flush
//------------------------------------------------------------------------------
bool PchTransform::Encoder::Flush( Sink & sink )
{
    if ( m_OutSize == 0 )
    {
        return true;
    }
    if ( m_Fault == FAULT_REFUSE )
    {
        return false;
    }
    if ( ( m_Fault == FAULT_DAMAGE ) && ( m_OutSize > 16 ) )
    {
        m_Out[ m_OutSize - 1 ] ^= 1;
        m_Fault = FAULT_NONE;
    }
    const size_t size = m_OutSize;
    m_OutSize = 0;
    return sink.OnPiece( m_Out, size );
}

// Decoder CONSTRUCTOR
//------------------------------------------------------------------------------
PchTransform::Decoder::Decoder()
    : m_Out( static_cast<uint8_t *>( ALLOC( kOutSize ) ) )
{
    memset( m_Header, 0, sizeof( m_Header ) );
    memset( m_Carry, 0, sizeof( m_Carry ) );
}

// Decoder DESTRUCTOR
//------------------------------------------------------------------------------
PchTransform::Decoder::~Decoder()
{
    FREE( m_Out );
}

// Decoder::Feed
//------------------------------------------------------------------------------
bool PchTransform::Decoder::Feed( const void * data, size_t size, Sink & sink )
{
    if ( m_Failed )
    {
        return false;
    }
    const uint8_t * bytes = static_cast<const uint8_t *>( data );

    // The base
    while ( ( m_HeaderSize < sizeof( m_Header ) ) && ( size > 0 ) )
    {
        m_Header[ m_HeaderSize++ ] = *bytes++;
        --size;
        if ( m_HeaderSize == sizeof( m_Header ) )
        {
            m_Base = ( (uint32_t)m_Header[ 0 ] | ( (uint32_t)m_Header[ 1 ] << 8 ) | ( (uint32_t)m_Header[ 2 ] << 16 ) | ( (uint32_t)m_Header[ 3 ] << 24 ) );
            if ( IsValidBase( m_Base ) == false )
            {
                m_Failed = true;
                return false;
            }
        }
    }
    if ( m_HeaderSize < sizeof( m_Header ) )
    {
        return true;
    }

    // A word that was started by the last piece
    if ( m_CarrySize > 0 )
    {
        while ( ( size > 0 ) && ( m_CarrySize < 8 ) )
        {
            m_Carry[ m_CarrySize++ ] = *bytes++;
            --size;
        }
        if ( m_CarrySize < 8 )
        {
            return true;
        }
        if ( DecodeWord( Load64( m_Carry ) ) == false )
        {
            m_Failed = true;
            return false;
        }
        m_CarrySize = 0;
    }

    for ( ; size >= 8; bytes += 8, size -= 8 )
    {
        if ( ( ( kOutSize - m_OutSize ) < 8 ) && ( Flush( sink ) == false ) )
        {
            m_Failed = true;
            return false;
        }
        if ( DecodeWord( Load64( bytes ) ) == false )
        {
            m_Failed = true;
            return false;
        }
    }

    memcpy( m_Carry, bytes, size );
    m_CarrySize = (uint32_t)size;
    if ( Flush( sink ) == false )
    {
        m_Failed = true;
        return false;
    }
    return true;
}

// Decoder::Finish
//------------------------------------------------------------------------------
bool PchTransform::Decoder::Finish( Sink & sink )
{
    // An escape has to be followed by the word it is for
    if ( m_Failed || ( m_HeaderSize < sizeof( m_Header ) ) || m_LiteralNext )
    {
        return false;
    }

    // The bytes that are not a whole word are the tail
    if ( ( ( kOutSize - m_OutSize ) < m_CarrySize ) && ( Flush( sink ) == false ) )
    {
        return false;
    }
    memcpy( m_Out + m_OutSize, m_Carry, m_CarrySize );
    m_OutSize += m_CarrySize;
    m_CarrySize = 0;
    return Flush( sink );
}

// Decoder::DecodeWord
//------------------------------------------------------------------------------
bool PchTransform::Decoder::DecodeWord( uint64_t word )
{
    ASSERT( ( kOutSize - m_OutSize ) >= 8 );
    if ( m_LiteralNext )
    {
        m_LiteralNext = false;
    }
    else
    {
        const uint32_t high = (uint32_t)( word >> 32 );
        const uint32_t low = (uint32_t)word;
        if ( high == ( m_Base | kF3Bit ) )
        {
            m_LiteralNext = true;
            return true;
        }
        if ( high == ( m_Base | kF1Bit ) )
        {
            if ( low >= m_Cache.GetCount() )
            {
                return false;
            }
            const uint32_t offset = m_Cache.Get( low );
            m_Cache.MoveToFront( low );
            word = ( ( (uint64_t)m_Base << 32 ) | offset );
        }
        else if ( high == ( m_Base | kF2Bit ) )
        {
            const uint32_t index = ( low >> 16 );
            if ( index >= m_Cache.GetCount() )
            {
                return false;
            }
            const uint32_t offset = ( m_Cache.Get( index ) + ( low & 0xFFFF ) - (uint32_t)kNearRange );
            m_Cache.InsertAtFront( offset );
            word = ( ( (uint64_t)m_Base << 32 ) | offset );
        }
        else if ( high == m_Base )
        {
            m_Cache.InsertAtFront( low );
        }
    }
    Store64( m_Out + m_OutSize, word );
    m_OutSize += 8;
    return true;
}

// Decoder::Flush
//------------------------------------------------------------------------------
bool PchTransform::Decoder::Flush( Sink & sink )
{
    if ( m_OutSize == 0 )
    {
        return true;
    }
    const size_t size = m_OutSize;
    m_OutSize = 0;
    return sink.OnPiece( m_Out, size );
}

// FindBase
//------------------------------------------------------------------------------
/*static*/ bool PchTransform::FindBase( const void * data, size_t size, uint32_t & outBase )
{
    // The values of the high half, with how many words have each. A word with none is not a pointer.
    constexpr uint32_t kNumSlots = 1024;
    struct Slot
    {
        uint32_t m_Value;
        uint32_t m_Count;
    };
    Slot slots[ kNumSlots ];
    memset( slots, 0, sizeof( slots ) );

    const uint8_t * bytes = static_cast<const uint8_t *>( data );
    const size_t numWords = ( size / 8 );
    for ( size_t i = 0; i < numWords; ++i )
    {
        const uint32_t high = (uint32_t)( Load64( bytes + ( i * 8 ) ) >> 32 );
        if ( high == 0 )
        {
            continue;
        }

        // A value that does not find a place is not counted: the one most words have does
        uint32_t slot = ( ( high * 2654435761u ) >> 22 );
        for ( uint32_t probe = 0; probe < 8; ++probe, slot = ( ( slot + 1 ) % kNumSlots ) )
        {
            if ( ( slots[ slot ].m_Count == 0 ) || ( slots[ slot ].m_Value == high ) )
            {
                slots[ slot ].m_Value = high;
                ++slots[ slot ].m_Count;
                break;
            }
        }
    }

    const Slot * best = &slots[ 0 ];
    for ( const Slot & slot : slots )
    {
        if ( slot.m_Count > best->m_Count )
        {
            best = &slot;
        }
    }
    if ( ( best->m_Count == 0 ) || ( ( (uint64_t)best->m_Count * kMinBaseShare ) < numWords ) || ( IsValidBase( best->m_Value ) == false ) )
    {
        return false;
    }
    outBase = best->m_Value;
    return true;
}

// Choose
//------------------------------------------------------------------------------
/*static*/ bool PchTransform::Choose( const char * path, uint64_t size, int32_t compressionLevel, const Atomic<bool> * abort, uint32_t & outBase )
{
    uint32_t base = 0;
    uint64_t plainSize = 0;
    uint64_t transformedSize = 0;
    if ( ( Estimate( path, size, compressionLevel, abort, base, plainSize, transformedSize ) == false ) || ( IsWorthIt( plainSize, transformedSize ) == false ) )
    {
        return false;
    }
    outBase = base;
    return true;
}

// Estimate
//------------------------------------------------------------------------------
/*static*/ bool PchTransform::Estimate( const char * path, uint64_t size, int32_t compressionLevel, const Atomic<bool> * abort,
                                        uint32_t & outBase, uint64_t & outPlainSize, uint64_t & outTransformedSize )
{
    // The pieces to try: the file if it is short, else some from along it, the first and the last among them
    uint64_t offsets[ kProbeCount ];
    size_t lengths[ kProbeCount ];
    uint32_t numProbes = 0;
    if ( size <= ( (uint64_t)kProbeLength * kProbeCount ) )
    {
        for ( uint64_t offset = 0; offset < size; offset += kProbeLength )
        {
            offsets[ numProbes ] = offset;
            lengths[ numProbes ] = (size_t)Math::Min<uint64_t>( kProbeLength, ( size - offset ) );
            ++numProbes;
        }
    }
    else
    {
        for ( uint32_t i = 0; i < kProbeCount; ++i )
        {
            offsets[ numProbes ] = ( ( ( ( size - kProbeLength ) / ( kProbeCount - 1 ) ) * i ) & ~(uint64_t)7 );
            lengths[ numProbes ] = kProbeLength;
            ++numProbes;
        }
    }
    if ( numProbes == 0 )
    {
        return false;
    }

    FileStream file;
    if ( ( file.Open( path, FileStream::READ_ONLY ) == false ) || ( file.GetFileSize() != size ) )
    {
        return false;
    }
    UniquePtr<char, FreeDeletor> data( static_cast<char *>( ALLOC( kProbeLength ) ) );
    SizeCompressor compressor( compressionLevel, (size_t)GetMaxTransformedSize( kProbeLength ) );
    auto readProbe = [ & ]( uint32_t index )
    {
        return ( abort == nullptr || ( abort->Load() == false ) ) &&
               file.Seek( offsets[ index ] ) &&
               ( file.Read( data.Get(), lengths[ index ] ) == lengths[ index ] );
    };

    // The base is what the first of them that has one has, from the middle on
    uint32_t base = 0;
    bool hasBase = false;
    for ( uint32_t i = 0; ( i < numProbes ) && ( hasBase == false ); ++i )
    {
        const uint32_t index = ( ( numProbes / 2 ) + i ) % numProbes;
        if ( readProbe( index ) == false )
        {
            return false;
        }
        hasBase = FindBase( data.Get(), lengths[ index ], base );
    }
    if ( hasBase == false )
    {
        return false;
    }

    // Each is compressed on its own. The pointers the transform has seen go on from one to the next.
    uint64_t plainSize = 0;
    uint64_t transformedSize = 0;
    Encoder encoder( base );
    for ( uint32_t i = 0; i < numProbes; ++i )
    {
        uint64_t size1 = 0;
        uint64_t size2 = 0;
        CollectSink transformed( (size_t)GetMaxTransformedSize( kProbeLength ) );
        if ( ( readProbe( i ) == false ) ||
             ( compressor.GetSize( data.Get(), lengths[ i ], size1 ) == false ) ||
             ( encoder.Feed( data.Get(), lengths[ i ], transformed ) == false ) ||
             ( ( ( i + 1 ) == numProbes ) && ( encoder.Finish( transformed ) == false ) ) ||
             ( compressor.GetSize( transformed.GetData(), transformed.GetSize(), size2 ) == false ) )
        {
            return false;
        }
        plainSize += size1;
        transformedSize += size2;
    }

    outBase = base;
    outPlainSize = plainSize;
    outTransformedSize = transformedSize;
    return true;
}

//------------------------------------------------------------------------------
