// Compressor
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"

// Forward Declarations
//------------------------------------------------------------------------------
template <class T> class Atomic;

// Compressor
//------------------------------------------------------------------------------
class Compressor
{
public:
    explicit Compressor();
    ~Compressor();

    static bool IsValidData( const void * data, size_t dataSize );
    static uint32_t GetUncompressedSize( const void * data, size_t dataSize );

    // Zstd level for data sent between coordinator and workers: toolchain and PCH files, and job results
    inline static const int32_t kZstdTransferLevel = 3;

    // compressionLevel:
    //   < 0 : use LZ4, with values directly mapping to "acceleration level"
    //  == 0 : disable compression
    //   > 0 : use LZ4HC, with values directly mapping to "compression level"
    bool Compress( const void * data, size_t dataSize, int32_t compressionLevel = -1 ); // -1 = default LZ4 compression level

    // Zstd
    bool CompressZstd( const void * data, size_t dataSize, int32_t compressionLevel = -1 ); // -1 = default Zstd compression level

    // Decompress (handled all formats including uncompressed)
    bool Decompress( const void * data );

    // Sees the content of a file as CompressZstdFile reads it
    class FileObserver
    {
    public:
        virtual ~FileObserver() = default;
        virtual void OnPass() = 0; // The file is about to be read again from its start
        virtual void OnPiece( const void * data, size_t size ) = 0;
    };

    // Compresses a file into another without holding either in memory. The result is in the format
    // Decompress reads: a header and one zstd frame, or the file itself behind a header when the frame is
    // not smaller. The file is read once, and twice when it does not compress; the observer sees every
    // pass. The source must be sourceSize bytes: the size is part of the format, and of at most
    // kMaxFileSize. outSize is the size of the file written.
    static constexpr size_t kHeaderSize = 12;
    static constexpr uint64_t kMaxFileSize = ( 0xFFFFFFFFull - kHeaderSize );
    [[nodiscard]] static bool CompressZstdFile( const char * sourcePath,
                                                uint64_t sourceSize,
                                                const char * destPath,
                                                int32_t compressionLevel,
                                                const Atomic<bool> * abort,
                                                FileObserver & observer,
                                                uint64_t & outSize );

    const void * GetResult() const { return m_Result; }
    size_t GetResultSize() const { return m_ResultSize; }

    void * ReleaseResult()
    {
        void * r = m_Result;
        m_Result = nullptr;
        m_ResultSize = 0;
        return r;
    }

private:
    enum CompressionType : uint32_t
    {
        eUncompressed = 0,
        eLZ4 = 1,
        eZstd = 2,
    };
    struct Header
    {
        uint32_t m_CompressionType;
        uint32_t m_UncompressedSize;
        uint32_t m_CompressedSize;
    };
    static void WriteHeader( void * dest, bool zstd, uint32_t uncompressedSize, uint32_t compressedSize );
    void * m_Result;
    size_t m_ResultSize;
};

//------------------------------------------------------------------------------
