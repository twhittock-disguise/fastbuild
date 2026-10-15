// StreamDecompressor.h - Decompresses what Compressor wrote, a piece at a time
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"

// Forward Declarations
//------------------------------------------------------------------------------
template <class T> class Atomic;

// StreamDecompressor
//------------------------------------------------------------------------------
// Reads data in the format of Compressor (a header, then one zstd frame, or the data itself when it
// did not compress) without holding the result: it goes to a Sink as it is produced. The input may
// be fed in pieces of any size. LZ4 is not read.
class StreamDecompressor
{
public:
    StreamDecompressor();
    ~StreamDecompressor();

    // Non-copyable
    explicit StreamDecompressor( const StreamDecompressor & other ) = delete;
    StreamDecompressor & operator=( const StreamDecompressor & other ) = delete;

    class Sink
    {
    public:
        virtual ~Sink() = default;
        virtual bool OnPiece( const void * data, size_t size ) = 0; // false stops the decompression
    };

    // The next bytes of the input. False if they are not valid, or the sink refused what they made.
    [[nodiscard]] bool Feed( const void * data, size_t size, Sink & sink );

    // After the last byte: whether the input was one whole, valid piece of data
    [[nodiscard]] bool Finish() const;

    // Known once the header has been fed
    bool HasHeader() const { return m_HeaderSize == kHeaderSize; }
    uint32_t GetUncompressedSize() const { return m_UncompressedSize; }

    // The input in one go, or the file at path (which must be fileSize bytes)
    [[nodiscard]] static bool DecompressMemory( const void * data, size_t size, Sink & sink, uint32_t & outUncompressedSize );
    [[nodiscard]] static bool DecompressFile( const char * path, const Atomic<bool> * abort, Sink & sink, uint32_t & outUncompressedSize );

private:
    static constexpr uint32_t kHeaderSize = 12;

    bool FeedPayload( const char * data, size_t size, Sink & sink );

    char        m_Header[ kHeaderSize ];
    uint32_t    m_HeaderSize = 0;       // Bytes of the header received
    bool        m_Zstd = false;
    bool        m_Failed = false;
    bool        m_FrameDone = false;
    uint32_t    m_UncompressedSize = 0;
    uint32_t    m_CompressedSize = 0;
    uint64_t    m_PayloadFed = 0;
    uint64_t    m_Produced = 0;
    void *      m_Context = nullptr;    // ZSTD_DCtx
    char *      m_Output = nullptr;
};

//------------------------------------------------------------------------------
