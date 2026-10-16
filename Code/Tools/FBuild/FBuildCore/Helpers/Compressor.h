// Compressor
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildCore/Helpers/PchTransform.h"

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

    // The wire format: a header, then the payload. The first word of the header is the type: the codec in
    // its low byte. Zstd data may be of a PCH that a transform (see PchTransform) was applied to before it
    // was compressed, which the next two bytes say: the id of the transform, then its version. Both are 0
    // for any other data, so what was written before there were transforms reads as it did. Data of a
    // transform is not data that older readers know, and they refuse it, as they do any type they do not know.
    // The uncompressed size is that of the data as it was before the transform.
    enum CompressionType : uint32_t
    {
        eUncompressed = 0,
        eLZ4 = 1,
        eZstd = 2,
    };
    static constexpr size_t kHeaderSize = 12;
    struct HeaderInfo
    {
        CompressionType m_Type;
        uint32_t m_UncompressedSize;
        uint32_t m_CompressedSize;  // The payload bytes that follow the header
        uint8_t m_TransformId = 0;      // 0 if there is none
        uint8_t m_TransformVersion = 0;
    };
    // Reads the header at the start of data, if it is a valid one: a known codec and a payload no
    // larger than what it makes, and no transform unless allowTransform (then only on zstd data, and
    // whether the id and version are known is left to the reader). dataSize must be at least kHeaderSize.
    static bool ParseHeader( const void * data, size_t dataSize, HeaderInfo & outHeader, bool allowTransform = false );

    static bool IsValidData( const void * data, size_t dataSize );
    static uint32_t GetUncompressedSize( const void * data, size_t dataSize );

    // Zstd level for data sent between coordinator and workers: toolchain and PCH files, and job results
    inline static const int32_t kZstdTransferLevel = 3;

    // Zstd parameters for a PCH, which is compressed once for many workers and is large and repetitive: a higher level
    // and a window of 64 MiB (the decoders accept up to 128 MiB by default, so a reader needs no setting)
    inline static const int32_t kZstdPchLevel = 9;
    inline static const int32_t kZstdPchWindowLog = 26;

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
    static constexpr uint64_t kMaxFileSize = ( 0xFFFFFFFFull - kHeaderSize );
    // With a transform, what is compressed is what it makes of the file (the observer still sees the file), and
    // the header says so. That is not so if it is not smaller than the file: the file is what is written then
    // and outTransformed is false. The transform is used up by the call. windowLog is that of the frame, or the
    // default of the level if it is 0.
    [[nodiscard]] static bool CompressZstdFile( const char * sourcePath,
                                                uint64_t sourceSize,
                                                const char * destPath,
                                                int32_t compressionLevel,
                                                const Atomic<bool> * abort,
                                                FileObserver & observer,
                                                uint64_t & outSize,
                                                PchTransform::Encoder * transform = nullptr,
                                                bool * outTransformed = nullptr,
                                                int32_t windowLog = 0 );

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
    struct Header
    {
        uint32_t m_CompressionType;
        uint32_t m_UncompressedSize;
        uint32_t m_CompressedSize;
    };
    static void WriteHeader( void * dest, bool zstd, uint32_t uncompressedSize, uint32_t compressedSize, bool transformed = false );
    void * m_Result;
    size_t m_ResultSize;
};

//------------------------------------------------------------------------------
