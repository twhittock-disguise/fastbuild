// JobStream - Job payloads compressed as one continuing zstd stream per connection
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"

// Forward Declarations
//------------------------------------------------------------------------------
struct ZSTD_CCtx_s;
struct ZSTD_DCtx_s;

// JobStream
//------------------------------------------------------------------------------
// Consecutive jobs from one coordinator share most of their content (the same
// headers, flags and sources), so each chunk is compressed against the jobs
// sent before it. Chunks must be decoded in the order they were encoded, and
// both ends start a fresh stream for every connection.
//
// Chunk layout: <uint32_t uncompressed size><zstd data flushed to a block boundary>
//------------------------------------------------------------------------------
namespace JobStream
{
    constexpr int kCompressionLevel = 6;
    constexpr int kWindowLog = 26;      // 64 MiB of history, which the decoder has to hold too
    constexpr int kLdmHashLog = 16;     // Long distance matching table (windowLog - 10)
    constexpr size_t kMaxChunkSize = ( 1024u * 1024u * 1024u );
}

// JobStreamEncoder
//------------------------------------------------------------------------------
class JobStreamEncoder
{
public:
    explicit JobStreamEncoder( int windowLog = JobStream::kWindowLog );
    ~JobStreamEncoder();

    // Non-copyable
    explicit JobStreamEncoder( const JobStreamEncoder & other ) = delete;
    JobStreamEncoder & operator=( const JobStreamEncoder & other ) = delete;

    // Compress data as the next chunk. The result is valid until the next call.
    // On failure the stream is unusable and the connection should be closed.
    [[nodiscard]] bool Encode( const void * data, size_t dataSize );

    const void * GetResult() const { return m_Buffer; }
    size_t GetResultSize() const { return m_ResultSize; }

    size_t GetMemoryUsage() const; // As reserved by zstd

private:
    const int m_WindowLog;
    ZSTD_CCtx_s * m_Context = nullptr;
    char * m_Buffer = nullptr;
    size_t m_BufferSize = 0;
    size_t m_ResultSize = 0;
};

// JobStreamDecoder
//------------------------------------------------------------------------------
class JobStreamDecoder
{
public:
    JobStreamDecoder() = default;
    ~JobStreamDecoder();

    // Non-copyable
    explicit JobStreamDecoder( const JobStreamDecoder & other ) = delete;
    JobStreamDecoder & operator=( const JobStreamDecoder & other ) = delete;

    // Decode the next chunk into a newly allocated buffer owned by the caller (FREE).
    // Fails on corrupt, out of order or oversized (window) data, after which the
    // stream is unusable and the connection should be closed.
    [[nodiscard]] bool Decode( const void * chunk, size_t chunkSize, void * & outData, size_t & outSize );

    size_t GetMemoryUsage() const; // As reserved by zstd

private:
    ZSTD_DCtx_s * m_Context = nullptr;
};

//------------------------------------------------------------------------------
