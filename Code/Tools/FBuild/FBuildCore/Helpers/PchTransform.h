// PchTransform.h - A reversible rewrite of a PCH that makes it compress better
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Assert.h"
#include "Core/Env/Types.h"

// Forward Declarations
//------------------------------------------------------------------------------
template <class T> class Atomic;

// PchTransform
//------------------------------------------------------------------------------
// The heap image in an MSVC PCH is a sequence of 8 byte words, many of them pointers into the image: words
// whose high half is the same value (the base) and whose low half is an offset. Pointers repeat, or point near
// one that came shortly before, so a word that is a pointer is replaced with a reference to a recent one.
// What zstd makes of that is about a third smaller than of the PCH itself.
//
// The transform is exact for any bytes, whatever their length and whether or not they are a PCH: only the
// size of the result and how well it compresses depend on the input. It is a stream: it takes its input in
// pieces of any size, passes its result on in pieces, and holds a bounded amount (a small cache of pointers
// that the inverse rebuilds from the stream it has read so far).
//
// The format, which version kPtrXfVersion of the transform names:
//   the base, a uint32_t, then the words and last the tail (the bytes that do not make a whole word). A word
//   whose high half is the base is a pointer, and is written as one of
//     - (F1, i): the offset equals entry i of the cache, which then moves to the front
//     - (F2, i << 16 | (d + 2048)): the offset is entry i plus d, which is within 2048 of it
//     - the word itself, if neither holds
//   Every pointer enters the cache at the front unless it was in it, the oldest entry leaving when it is full.
//   F1, F2 and F3 are the base with bit 31, bit 30 and bit 29 of the high half set.
//   A word whose high half is F1, F2 or F3 would be taken for a reference, so it is written as (F3, 0) and then
//   the word itself, which the inverse copies. So the base must have none of those three bits.
class PchTransform
{
public:
    // The transform of a PCH is named in the header of its blob (see Compressor) by an id and a version.
    // Another way of rewriting a PCH takes another id; a change to this one takes another version.
    static constexpr uint8_t kPtrXfId = 1;
    static constexpr uint8_t kPtrXfVersion = 1;

    // The base has none of the bits that mark a reference
    static constexpr uint32_t kMaxBase = 0x1FFFFFFF;
    static bool IsValidBase( uint32_t base ) { return ( base <= kMaxBase ); }

    // The most the transform makes of data of a size, in bytes: when every word is escaped
    static constexpr uint64_t GetMaxTransformedSize( uint64_t size ) { return ( sizeof( uint32_t ) + ( size * 2 ) ); }

    // The recent pointers the references of a word name
    static constexpr uint32_t kCacheSize = 32;
    static constexpr int32_t kNearRange = 2048;

    // Where the result of a transform goes
    class Sink
    {
    public:
        virtual ~Sink() = default;
        virtual bool OnPiece( const void * data, size_t size ) = 0; // false stops the transform
    };

    // The recent pointers, in the order they were last used
    class PointerCache
    {
    public:
        // The index of the entry that is offset, or -1
        int32_t Find( uint32_t offset ) const;

        // The index of the entry nearest to offset that is within kNearRange of it (the first of two at the
        // same distance), or -1. outDelta is offset less that entry.
        int32_t FindNear( uint32_t offset, int32_t & outDelta ) const;

        uint32_t GetCount() const { return m_Count; }
        uint32_t Get( uint32_t index ) const { ASSERT( index < m_Count ); return m_Offsets[ index ]; }

        void MoveToFront( uint32_t index );
        void InsertAtFront( uint32_t offset ); // The oldest entry leaves when it is full

    private:
        uint32_t m_Offsets[ kCacheSize ];
        uint32_t m_Count = 0;
    };

    // Encoder: the data in, the transformed data to the sink
    class Encoder
    {
    public:
        explicit Encoder( uint32_t base );
        ~Encoder();

        // Non-copyable
        explicit Encoder( const Encoder & other ) = delete;
        Encoder & operator=( const Encoder & other ) = delete;

        // The next bytes of the data. False if the sink refused what they made.
        [[nodiscard]] bool Feed( const void * data, size_t size, Sink & sink );

        // After the last byte: the tail, and what is held back. False if the sink refused it.
        [[nodiscard]] bool Finish( Sink & sink );

        // Test seam: a fault made on purpose, to reach what a transform that is wrong leads to
        enum Fault : uint32_t
        {
            FAULT_NONE,
            FAULT_DAMAGE,   // The last byte of the first piece of more than 16 bytes is flipped, as if the transform did not invert.
                            // A piece of the base and a word or so is left alone so that it is the data that is wrong and not the base.
            FAULT_REFUSE,   // The sink is not given what is made, and the encoder reports that it was refused
        };
        void SetFault( Fault fault ) { m_Fault = fault; }

    private:
        void EncodeWord( uint64_t word );
        bool Flush( Sink & sink );

        const uint32_t  m_Base;
        PointerCache    m_Cache;
        uint8_t         m_Carry[ 8 ];       // The start of a word that the next piece completes
        uint32_t        m_CarrySize = 0;
        uint8_t *       m_Out = nullptr;
        size_t          m_OutSize = 0;
        Fault           m_Fault = FAULT_NONE;
    };

    // Decoder: the transformed data in, the data to the sink
    class Decoder
    {
    public:
        Decoder();
        ~Decoder();

        // Non-copyable
        explicit Decoder( const Decoder & other ) = delete;
        Decoder & operator=( const Decoder & other ) = delete;

        // The next bytes of the transformed data. False if they are not valid, or the sink refused what they made.
        [[nodiscard]] bool Feed( const void * data, size_t size, Sink & sink );

        // After the last byte: the tail. False if the data was not the whole of a valid transform, or the sink refused it.
        [[nodiscard]] bool Finish( Sink & sink );

    private:
        bool DecodeWord( uint64_t word );
        bool Flush( Sink & sink );

        uint32_t        m_Base = 0;
        uint8_t         m_Header[ 4 ];
        uint32_t        m_HeaderSize = 0;
        PointerCache    m_Cache;
        uint8_t         m_Carry[ 8 ];
        uint32_t        m_CarrySize = 0;
        bool            m_LiteralNext = false;  // The word after an escape is data, and is copied
        bool            m_Failed = false;
        uint8_t *       m_Out = nullptr;
        size_t          m_OutSize = 0;
    };

    // What zstd makes of the PCH in the file with and without the transform, and the base the transform has. The base
    // is the high half most words have. The file is judged by compressing pieces of it, at compressionLevel (the one
    // the file is sent with, which finds part of what the transform removes): all of it up to 8 MiB, else eight pieces
    // of a MiB along it, the first and last included. The time this takes is that of compressing 8 MiB twice, whatever
    // the size of the file. False if there is no base, or the file cannot be read.
    [[nodiscard]] static bool Estimate( const char * path, uint64_t size, int32_t compressionLevel, const Atomic<bool> * abort,
                                        uint32_t & outBase, uint64_t & outPlainSize, uint64_t & outTransformedSize );

    // Whether the transform is worth it: at most 95% of the size without it. A smaller gain does not make up for the
    // time the transform takes, and a PCH of another layout, or another compiler, is sent as it is.
    static constexpr bool IsWorthIt( uint64_t plainSize, uint64_t transformedSize ) { return ( ( transformedSize * 100 ) <= ( plainSize * 95 ) ); }

    // Whether to transform the PCH in the file, and with which base: Estimate, and IsWorthIt of what it finds
    [[nodiscard]] static bool Choose( const char * path, uint64_t size, int32_t compressionLevel, const Atomic<bool> * abort, uint32_t & outBase );

    // The base most words of a piece of data have, if enough have one for the transform to help
    [[nodiscard]] static bool FindBase( const void * data, size_t size, uint32_t & outBase );
};

//------------------------------------------------------------------------------
