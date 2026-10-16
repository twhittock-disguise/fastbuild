// TestPchBlob.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchBlob.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchId.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchTransform.h"
#include "Tools/FBuild/FBuildCore/Helpers/StreamDecompressor.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Process/Atomic.h"
#include "Core/Strings/AStackString.h"

// External
#include "zstd.h"

#include <string.h>

// TestPchBlob
//------------------------------------------------------------------------------
class TestPchBlob : public FBuildTest
{
private:
    DECLARE_TESTS

    void DecompressesWhatCompressorWrote() const;
    void DecompressesInPiecesOfAnySize() const;
    void RejectsDataThatIsNotValid() const;
    void UnpackWritesTheCheckedPch() const;
    void UnpackRejectsAWrongIdOrAFileThatIsNotThere() const;
    void UnpackLeavesNothingWhenItCannotWrite() const;
    void UnpackKeepsTheStoredFileWhenAWriteFails() const;
    void UnpackStopsWhenAborted() const;
    void VerifyChecksTheSizeAndTheId() const;
    void TransformedBlobIsReadLikeAPlainOne() const;
    void TransformedBlobIsVerifiedAndUnpacked() const;
    void RejectsTransformedBlobsThatAreNotValid() const;
    void TransformFallsBackToThePlainBlob() const;
    void TransformedFrameIsBounded() const;
    void PchBlobsUseTheWideWindow() const;
    void PchBlobWindowsFollowTheirSize() const;
    void HostileTransformedFramesAreRejected() const;

    // The header of a blob, as words
    static uint32_t GetHeaderWord( const Array<char> & blob, uint32_t index );
    static void SetHeaderWord( Array<char> & blob, uint32_t index, uint32_t value );
    static constexpr uint32_t kTransformedType = ( Compressor::eZstd | ( (uint32_t)PchTransform::kPtrXfId << 8 ) | ( (uint32_t)PchTransform::kPtrXfVersion << 16 ) );
    static_assert( kTransformedType == 0x00010102, "The type word of a blob of the transform (zstd, id 1, version 1) is on the wire" );

    // Compressible content with some variation
    static void MakeContent( uint32_t size, uint32_t seed, Array<char> & outContent );
    static void Compress( const Array<char> & content, Array<char> & outBlob );
    static void GetTestDir( const char * testName, AString & outDir );
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestPchBlob )
    REGISTER_TEST( DecompressesWhatCompressorWrote )
    REGISTER_TEST( DecompressesInPiecesOfAnySize )
    REGISTER_TEST( RejectsDataThatIsNotValid )
    REGISTER_TEST( UnpackWritesTheCheckedPch )
    REGISTER_TEST( UnpackRejectsAWrongIdOrAFileThatIsNotThere )
    REGISTER_TEST( UnpackLeavesNothingWhenItCannotWrite )
    REGISTER_TEST( UnpackKeepsTheStoredFileWhenAWriteFails )
    REGISTER_TEST( UnpackStopsWhenAborted )
    REGISTER_TEST( VerifyChecksTheSizeAndTheId )
    REGISTER_TEST( TransformedBlobIsReadLikeAPlainOne )
    REGISTER_TEST( TransformedBlobIsVerifiedAndUnpacked )
    REGISTER_TEST( RejectsTransformedBlobsThatAreNotValid )
    REGISTER_TEST( TransformFallsBackToThePlainBlob )
    REGISTER_TEST( TransformedFrameIsBounded )
    REGISTER_TEST( PchBlobsUseTheWideWindow )
    REGISTER_TEST( PchBlobWindowsFollowTheirSize )
    REGISTER_TEST( HostileTransformedFramesAreRejected )
REGISTER_TESTS_END

// Helpers
//------------------------------------------------------------------------------
namespace
{
    // Collects what is decompressed
    class CollectingSink : public StreamDecompressor::Sink
    {
    public:
        virtual bool OnPiece( const void * data, size_t size ) override
        {
            const size_t oldSize = m_Data.GetSize();
            m_Data.SetSize( oldSize + size );
            memcpy( m_Data.Begin() + oldSize, data, size );
            return true;
        }
        Array<char> m_Data;
    };

    // Refuses what it is given after a number of pieces
    class RefusingSink : public StreamDecompressor::Sink
    {
    public:
        virtual bool OnPiece( const void *, size_t ) override { return ( ++m_NumPieces <= 1 ); }
        uint32_t m_NumPieces = 0;
    };

    // A file that is written to the same, up to a number of writes, and then does something else
    class FaultyFileStream : public FileStream
    {
    public:
        FaultyFileStream( uint32_t numGoodWrites, Atomic<bool> * abortAfterThem )
            : m_NumGoodWrites( numGoodWrites )
            , m_Abort( abortAfterThem )
        {
        }
        virtual uint64_t WriteBuffer( const void * buffer, uint64_t bytesToWrite ) override
        {
            ++m_NumWrites;
            if ( m_NumWrites > m_NumGoodWrites )
            {
                return 0; // As when there is no room
            }
            const uint64_t written = FileStream::WriteBuffer( buffer, bytesToWrite );
            if ( ( m_NumWrites == m_NumGoodWrites ) && m_Abort )
            {
                m_Abort->Store( true );
            }
            return written;
        }
        uint32_t m_NumWrites = 0;

    private:
        uint32_t m_NumGoodWrites;
        Atomic<bool> * m_Abort;
    };
}

// MakeContent
//------------------------------------------------------------------------------
/*static*/ void TestPchBlob::MakeContent( uint32_t size, uint32_t seed, Array<char> & outContent )
{
    outContent.SetSize( size );
    uint32_t state = ( seed * 2654435761u ) + 12345u;
    for ( uint32_t i = 0; i < size; ++i )
    {
        if ( ( i % 64 ) == 0 )
        {
            state = ( state * 1664525u ) + 1013904223u;
        }
        outContent[ i ] = (char)( 'a' + ( ( state >> ( ( i % 64 ) / 8 ) ) & 15 ) );
    }
}

// Compress
//------------------------------------------------------------------------------
/*static*/ void TestPchBlob::Compress( const Array<char> & content, Array<char> & outBlob )
{
    Compressor c;
    c.CompressZstd( content.Begin(), content.GetSize(), Compressor::kZstdTransferLevel );
    outBlob.SetSize( c.GetResultSize() );
    memcpy( outBlob.Begin(), c.GetResult(), c.GetResultSize() );
}

// GetTestDir
//------------------------------------------------------------------------------
/*static*/ void TestPchBlob::GetTestDir( const char * testName, AString & outDir )
{
    outDir.Format( "../tmp/Test/PchBlob/%s/", testName );
    Array<AString> old;
    FileIO::GetFiles( outDir, AStackString( "*" ), true, &old );
    for ( const AString & file : old )
    {
        FileIO::FileDelete( file.Get() );
    }
    TEST_ASSERT( FileIO::EnsurePathExists( outDir ) );
}

// DecompressesWhatCompressorWrote
//------------------------------------------------------------------------------
void TestPchBlob::DecompressesWhatCompressorWrote() const
{
    // Around the size of the pieces what is decompressed goes on in, and what does not compress
    const uint32_t sizes[] = { 1, 100, ( 1024 * 1024 ) - 1, ( 1024 * 1024 ), ( 1024 * 1024 ) + 1, ( 3 * 1024 * 1024 ) + 17 };
    for ( const uint32_t size : sizes )
    {
        for ( uint32_t kind = 0; kind < 2; ++kind )
        {
            Array<char> content;
            if ( kind == 0 )
            {
                MakeContent( size, size, content );
            }
            else
            {
                MakeRandomBytes( size, size, content );
            }
            Array<char> blob;
            Compress( content, blob );

            CollectingSink sink;
            uint32_t uncompressedSize = 0;
            TEST_ASSERT( StreamDecompressor::DecompressMemory( blob.Begin(), blob.GetSize(), sink, uncompressedSize ) );
            TEST_ASSERT( uncompressedSize == size );
            TEST_ASSERT( sink.m_Data.GetSize() == content.GetSize() );
            TEST_ASSERT( memcmp( sink.m_Data.Begin(), content.Begin(), content.GetSize() ) == 0 );
        }
    }

    // What Compressor writes to a file, as a client does
    {
        AStackString<> dir;
        GetTestDir( "WroteToAFile", dir );
        Array<char> content;
        MakeContent( ( 2 * 1024 * 1024 ) + 5, 3, content );
        AStackString<> source( dir );
        source += "source.bin";
        AStackString<> compressed( dir );
        compressed += "source.zst";
        TEST_ASSERT( WriteFile( source, content ) );

        NullFileObserver observer;
        uint64_t compressedSize = 0;
        TEST_ASSERT( Compressor::CompressZstdFile( source.Get(), content.GetSize(), compressed.Get(), Compressor::kZstdTransferLevel, nullptr, observer, compressedSize ) );

        CollectingSink sink;
        uint32_t uncompressedSize = 0;
        TEST_ASSERT( StreamDecompressor::DecompressFile( compressed.Get(), nullptr, sink, uncompressedSize ) );
        TEST_ASSERT( sink.m_Data.GetSize() == content.GetSize() );
        TEST_ASSERT( memcmp( sink.m_Data.Begin(), content.Begin(), content.GetSize() ) == 0 );
    }
}

// DecompressesInPiecesOfAnySize
//------------------------------------------------------------------------------
void TestPchBlob::DecompressesInPiecesOfAnySize() const
{
    Array<char> content;
    MakeContent( ( 2 * 1024 * 1024 ) + 99, 7, content );
    Array<char> blob;
    Compress( content, blob );

    // Even a piece that ends inside the header, or that is only a byte
    const size_t pieceSizes[] = { 1, 5, 11, 12, 13, 4096, ( 1024 * 1024 ), 1000000000 };
    for ( const size_t pieceSize : pieceSizes )
    {
        StreamDecompressor decompressor;
        CollectingSink sink;
        size_t done = 0;
        if ( pieceSize == 1 )
        {
            // Only the start: a byte at a time throughout would be slow
            for ( ; done < 64; ++done )
            {
                TEST_ASSERT( decompressor.Feed( blob.Begin() + done, 1, sink ) );
            }
        }
        while ( done < blob.GetSize() )
        {
            const size_t piece = Math::Min( pieceSize, blob.GetSize() - done );
            TEST_ASSERT( decompressor.Feed( blob.Begin() + done, piece, sink ) );
            done += piece;
        }
        TEST_ASSERT( decompressor.Finish() );
        TEST_ASSERT( decompressor.GetUncompressedSize() == content.GetSize() );
        TEST_ASSERT( sink.m_Data.GetSize() == content.GetSize() );
        TEST_ASSERT( memcmp( sink.m_Data.Begin(), content.Begin(), content.GetSize() ) == 0 );
    }
}

// RejectsDataThatIsNotValid
//------------------------------------------------------------------------------
void TestPchBlob::RejectsDataThatIsNotValid() const
{
    Array<char> content;
    MakeContent( 300000, 9, content );
    Array<char> blob;
    Compress( content, blob );
    uint32_t size = 0;
    {
        CollectingSink sink;
        TEST_ASSERT( StreamDecompressor::DecompressMemory( blob.Begin(), blob.GetSize(), sink, size ) );
    }

    // Cut short: more is expected
    {
        CollectingSink sink;
        TEST_ASSERT( StreamDecompressor::DecompressMemory( blob.Begin(), blob.GetSize() - 1, sink, size ) == false );
    }
    {
        CollectingSink sink;
        TEST_ASSERT( StreamDecompressor::DecompressMemory( blob.Begin(), 5, sink, size ) == false );
    }

    // More than the header says there is
    {
        Array<char> longer( blob );
        longer.Append( 'x' );
        CollectingSink sink;
        TEST_ASSERT( StreamDecompressor::DecompressMemory( longer.Begin(), longer.GetSize(), sink, size ) == false );
    }

    // Damaged, wherever it is: what the frame holds is checked by the frame, or by the size and the id
    {
        const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
        TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize(), pchId, (uint32_t)content.GetSize() ) );
        const size_t step = Math::Max<size_t>( blob.GetSize() / 61, 1 );
        for ( size_t position = 0; position < blob.GetSize(); position += ( ( position < 40 ) ? 1 : step ) )
        {
            Array<char> damaged( blob );
            damaged[ position ] = (char)( damaged[ position ] ^ 0x55 );
            TEST_ASSERT( PchBlob::Verify( damaged.Begin(), damaged.GetSize(), pchId, (uint32_t)content.GetSize() ) == false );
        }
        Array<char> lastDamaged( blob );
        lastDamaged[ lastDamaged.GetSize() - 1 ] = (char)( lastDamaged[ lastDamaged.GetSize() - 1 ] ^ 0x01 );
        TEST_ASSERT( PchBlob::Verify( lastDamaged.Begin(), lastDamaged.GetSize(), pchId, (uint32_t)content.GetSize() ) == false );
    }

    // A header that says it is another size
    {
        Array<char> wrong( blob );
        uint32_t uncompressedSize;
        memcpy( &uncompressedSize, wrong.Begin() + 4, 4 );
        uncompressedSize += 1;
        memcpy( wrong.Begin() + 4, &uncompressedSize, 4 );
        CollectingSink sink;
        TEST_ASSERT( StreamDecompressor::DecompressMemory( wrong.Begin(), wrong.GetSize(), sink, size ) == false );
        uncompressedSize -= 2;
        memcpy( wrong.Begin() + 4, &uncompressedSize, 4 );
        CollectingSink sink2;
        TEST_ASSERT( StreamDecompressor::DecompressMemory( wrong.Begin(), wrong.GetSize(), sink2, size ) == false );
    }

    // A header of a kind that is not read: LZ4 is not what a PCH is sent in
    {
        Compressor lz4;
        lz4.Compress( content.Begin(), content.GetSize(), 1 );
        CollectingSink sink;
        TEST_ASSERT( StreamDecompressor::DecompressMemory( lz4.GetResult(), lz4.GetResultSize(), sink, size ) == false );
    }
    {
        Array<char> unknown( blob );
        const uint32_t type = 7;
        memcpy( unknown.Begin(), &type, 4 );
        CollectingSink sink;
        TEST_ASSERT( StreamDecompressor::DecompressMemory( unknown.Begin(), unknown.GetSize(), sink, size ) == false );
    }

    // A sink that does not want it
    {
        RefusingSink sink;
        Array<char> big;
        MakeContent( ( 3 * 1024 * 1024 ), 4, big );
        Array<char> bigBlob;
        Compress( big, bigBlob );
        TEST_ASSERT( StreamDecompressor::DecompressMemory( bigBlob.Begin(), bigBlob.GetSize(), sink, size ) == false );
    }
}

// UnpackWritesTheCheckedPch
//------------------------------------------------------------------------------
void TestPchBlob::UnpackWritesTheCheckedPch() const
{
    AStackString<> dir;
    GetTestDir( "Unpack", dir );
    Array<char> content;
    MakeContent( ( 3 * 1024 * 1024 ) + 321, 11, content );
    Array<char> blob;
    Compress( content, blob );
    const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );

    AStackString<> stored( dir );
    stored += PchBlob::kStoredFileName;
    AStackString<> raw( dir );
    raw += PchBlob::kRawFileName;
    AStackString<> temp( dir );
    temp += "unpack.1.1.pch";
    TEST_ASSERT( WriteFile( stored, blob ) );

    uint64_t size = 0;
    TEST_ASSERT( PchBlob::Unpack( stored, pchId, temp, raw, nullptr, size ) == PchBlob::UNPACK_OK );
    TEST_ASSERT( size == content.GetSize() );
    Array<char> unpacked;
    TEST_ASSERT( ReadFile( raw, unpacked ) );
    TEST_ASSERT( unpacked.GetSize() == content.GetSize() );
    TEST_ASSERT( memcmp( unpacked.Begin(), content.Begin(), content.GetSize() ) == 0 );

    // The stored file is as it was, and nothing else is left
    Array<char> storedAgain;
    TEST_ASSERT( ReadFile( stored, storedAgain ) );
    TEST_ASSERT( storedAgain.GetSize() == blob.GetSize() );
    TEST_ASSERT( CountFilesIn( dir ) == 2 );

    // A copy that is there is replaced
    {
        Array<char> junk;
        MakeContent( 100, 1, junk );
        TEST_ASSERT( WriteFile( raw, junk ) );
    }
    TEST_ASSERT( PchBlob::Unpack( stored, pchId, temp, raw, nullptr, size ) == PchBlob::UNPACK_OK );
    TEST_ASSERT( ReadFile( raw, unpacked ) );
    TEST_ASSERT( unpacked.GetSize() == content.GetSize() );
}

// UnpackRejectsAWrongIdOrAFileThatIsNotThere
//------------------------------------------------------------------------------
void TestPchBlob::UnpackRejectsAWrongIdOrAFileThatIsNotThere() const
{
    AStackString<> dir;
    GetTestDir( "UnpackRejects", dir );
    Array<char> content;
    MakeContent( 500000, 13, content );
    Array<char> blob;
    Compress( content, blob );
    const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
    AStackString<> stored( dir );
    stored += PchBlob::kStoredFileName;
    AStackString<> raw( dir );
    raw += PchBlob::kRawFileName;
    AStackString<> temp( dir );
    temp += "unpack.1.1.pch";
    uint64_t size = 0;

    // Not there
    TEST_ASSERT( PchBlob::Unpack( stored, pchId, temp, raw, nullptr, size ) == PchBlob::UNPACK_CORRUPT );
    TEST_ASSERT( CountFilesIn( dir ) == 0 );

    // The data is a PCH, but not the one with this id
    TEST_ASSERT( WriteFile( stored, blob ) );
    TEST_ASSERT( PchBlob::Unpack( stored, pchId + 1, temp, raw, nullptr, size ) == PchBlob::UNPACK_CORRUPT );
    TEST_ASSERT( FileIO::FileExists( raw.Get() ) == false );
    TEST_ASSERT( FileIO::FileExists( temp.Get() ) == false );

    // Damaged on disk: the data of a PCH that was changed has another id, or is not decodable
    {
        Array<char> damaged( blob );
        for ( size_t i = Compressor::kHeaderSize + 50; i < damaged.GetSize(); i += ( damaged.GetSize() / 5 ) )
        {
            damaged[ i ] = (char)( damaged[ i ] ^ 0x21 );
        }
        TEST_ASSERT( WriteFile( stored, damaged ) );
        TEST_ASSERT( PchBlob::Unpack( stored, pchId, temp, raw, nullptr, size ) == PchBlob::UNPACK_CORRUPT );
        TEST_ASSERT( FileIO::FileExists( raw.Get() ) == false );
        TEST_ASSERT( FileIO::FileExists( temp.Get() ) == false );
    }

    // Cut short
    {
        Array<char> cut( blob );
        cut.SetSize( blob.GetSize() / 2 );
        TEST_ASSERT( WriteFile( stored, cut ) );
        TEST_ASSERT( PchBlob::Unpack( stored, pchId, temp, raw, nullptr, size ) == PchBlob::UNPACK_CORRUPT );
        TEST_ASSERT( FileIO::FileExists( raw.Get() ) == false );
        TEST_ASSERT( FileIO::FileExists( temp.Get() ) == false );
    }
}

// UnpackLeavesNothingWhenItCannotWrite
//------------------------------------------------------------------------------
void TestPchBlob::UnpackLeavesNothingWhenItCannotWrite() const
{
    AStackString<> dir;
    GetTestDir( "UnpackCannotWrite", dir );
    Array<char> content;
    MakeContent( 500000, 15, content );
    Array<char> blob;
    Compress( content, blob );
    const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
    AStackString<> stored( dir );
    stored += PchBlob::kStoredFileName;
    TEST_ASSERT( WriteFile( stored, blob ) );
    uint64_t size = 0;

    // A directory where the copy is to go: the data is written, and cannot be put in place
    AStackString<> raw( dir );
    raw += PchBlob::kRawFileName;
    TEST_ASSERT( FileIO::EnsurePathExists( raw ) );
    AStackString<> temp( dir );
    temp += "unpack.1.1.pch";
    TEST_ASSERT( PchBlob::Unpack( stored, pchId, temp, raw, nullptr, size ) == PchBlob::UNPACK_CANNOT_WRITE );
    TEST_ASSERT( FileIO::FileExists( temp.Get() ) == false );

    // A temporary file that cannot be made
    AStackString<> noTemp( dir );
    noTemp += "missing-dir/unpack.1.2.pch";
    AStackString<> raw2( dir );
    raw2 += "other.pch";
    TEST_ASSERT( PchBlob::Unpack( stored, pchId, noTemp, raw2, nullptr, size ) == PchBlob::UNPACK_CANNOT_WRITE );
    TEST_ASSERT( FileIO::FileExists( raw2.Get() ) == false );
}

// UnpackKeepsTheStoredFileWhenAWriteFails
//------------------------------------------------------------------------------
void TestPchBlob::UnpackKeepsTheStoredFileWhenAWriteFails() const
{
    AStackString<> dir;
    GetTestDir( "UnpackWriteFails", dir );
    Array<char> content;
    MakeContent( ( 3 * 1024 * 1024 ) + 77, 21, content ); // Written in several pieces
    Array<char> blob;
    Compress( content, blob );
    const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
    AStackString<> stored( dir );
    stored += PchBlob::kStoredFileName;
    AStackString<> raw( dir );
    raw += PchBlob::kRawFileName;
    AStackString<> temp( dir );
    temp += "unpack.1.1.pch";
    TEST_ASSERT( WriteFile( stored, blob ) );

    // The second piece cannot be written: the first was, so there is a file to remove
    FaultyFileStream writer( 1, nullptr );
    uint64_t size = 0;
    TEST_ASSERT( PchBlob::Unpack( stored, pchId, temp, raw, nullptr, size, &writer ) == PchBlob::UNPACK_CANNOT_WRITE );
    TEST_ASSERT( writer.m_NumWrites == 2 );
    TEST_ASSERT( FileIO::FileExists( temp.Get() ) == false );
    TEST_ASSERT( FileIO::FileExists( raw.Get() ) == false );

    // The PCH is not at fault, so it is as it was
    Array<char> storedAgain;
    TEST_ASSERT( ReadFile( stored, storedAgain ) );
    TEST_ASSERT( storedAgain.GetSize() == blob.GetSize() );
    TEST_ASSERT( memcmp( storedAgain.Begin(), blob.Begin(), blob.GetSize() ) == 0 );
    TEST_ASSERT( CountFilesIn( dir ) == 1 );
}

// UnpackStopsWhenAborted
//------------------------------------------------------------------------------
void TestPchBlob::UnpackStopsWhenAborted() const
{
    AStackString<> dir;
    GetTestDir( "UnpackAborted", dir );

    // Data that does not compress is in several pieces as it is stored, and the flag is looked at between them
    Array<char> content;
    MakeRandomBytes( ( 3 * 1024 * 1024 ) + 321, 17, content );
    Array<char> blob;
    Compress( content, blob );
    const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
    AStackString<> stored( dir );
    stored += PchBlob::kStoredFileName;
    AStackString<> raw( dir );
    raw += PchBlob::kRawFileName;
    AStackString<> temp( dir );
    temp += "unpack.1.1.pch";
    TEST_ASSERT( WriteFile( stored, blob ) );

    // Aborted after the first piece was written
    Atomic<bool> abort{ false };
    FaultyFileStream writer( 1, &abort );
    uint64_t size = 0;
    TEST_ASSERT( PchBlob::Unpack( stored, pchId, temp, raw, &abort, size, &writer ) == PchBlob::UNPACK_ABORTED );
    TEST_ASSERT( writer.m_NumWrites == 1 );
    TEST_ASSERT( FileIO::FileExists( temp.Get() ) == false );
    TEST_ASSERT( FileIO::FileExists( raw.Get() ) == false );
    TEST_ASSERT( CountFilesIn( dir ) == 1 ); // The stored file stays: the PCH is not at fault

    // Before the first
    const Atomic<bool> aborted{ true };
    TEST_ASSERT( PchBlob::Unpack( stored, pchId, temp, raw, &aborted, size ) == PchBlob::UNPACK_ABORTED );
    TEST_ASSERT( CountFilesIn( dir ) == 1 );
}

// VerifyChecksTheSizeAndTheId
//------------------------------------------------------------------------------
void TestPchBlob::VerifyChecksTheSizeAndTheId() const
{
    Array<char> content;
    MakeContent( 400000, 19, content );
    Array<char> blob;
    Compress( content, blob );
    const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
    const uint32_t size = (uint32_t)content.GetSize();

    TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize(), pchId, size ) );
    TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize(), pchId + 1, size ) == false );
    TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize(), pchId, size + 1 ) == false );
    TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize() - 1, pchId, size ) == false );
}

// GetHeaderWord
//------------------------------------------------------------------------------
/*static*/ uint32_t TestPchBlob::GetHeaderWord( const Array<char> & blob, uint32_t index )
{
    uint32_t value;
    memcpy( &value, blob.Begin() + ( index * sizeof( uint32_t ) ), sizeof( value ) );
    return value;
}

// SetHeaderWord
//------------------------------------------------------------------------------
/*static*/ void TestPchBlob::SetHeaderWord( Array<char> & blob, uint32_t index, uint32_t value )
{
    memcpy( blob.Begin() + ( index * sizeof( uint32_t ) ), &value, sizeof( value ) );
}

// TransformedBlobIsReadLikeAPlainOne
//------------------------------------------------------------------------------
void TestPchBlob::TransformedBlobIsReadLikeAPlainOne() const
{
    Array<char> content;
    MakePchLikeBytes( ( 2 * 1024 * 1024 ) + 5, 3, content );
    Array<char> plain;
    Array<char> blob;
    bool transformed = true;
    TEST_ASSERT( MakePchBlob( content, kNoTransform, plain, &transformed ) );
    TEST_ASSERT( transformed == false );
    TEST_ASSERT( MakePchBlob( content, kPchLikeBase, blob, &transformed ) );
    TEST_ASSERT( transformed );

    // It is smaller than the plain blob, and says what it is
    OUTPUT( "TransformedBlobIsReadLikeAPlainOne: %zu bytes, %zu with the transform (%.1f%%)\n", plain.GetSize(), blob.GetSize(), ( 100.0 * (double)blob.GetSize() ) / (double)plain.GetSize() );
    TEST_ASSERT( ( blob.GetSize() * 100 ) < ( plain.GetSize() * 90 ) );
    TEST_ASSERT( GetHeaderWord( blob, 0 ) == kTransformedType );
    TEST_ASSERT( GetHeaderWord( blob, 1 ) == content.GetSize() ); // That of the PCH
    TEST_ASSERT( GetHeaderWord( plain, 0 ) == Compressor::eZstd );

    // A reader that does not read transforms does not take it for data, whether or not it checks the header first
    Compressor::HeaderInfo header = {};
    TEST_ASSERT( Compressor::ParseHeader( blob.Begin(), blob.GetSize(), header ) == false );
    TEST_ASSERT( Compressor::IsValidData( blob.Begin(), blob.GetSize() ) == false );
    {
        Compressor decompressor;
        TEST_ASSERT( decompressor.Decompress( blob.Begin() ) == false );
    }
    {
        // Not even when its frame is the size the header says: what it decodes to is not the PCH
        Array<char> relabeled;
        relabeled = plain;
        SetHeaderWord( relabeled, 0, kTransformedType );
        Compressor decompressor;
        TEST_ASSERT( Compressor::IsValidData( relabeled.Begin(), relabeled.GetSize() ) == false );
        TEST_ASSERT( decompressor.Decompress( relabeled.Begin() ) == false );
    }
    TEST_ASSERT( Compressor::ParseHeader( blob.Begin(), blob.GetSize(), header, true ) );
    TEST_ASSERT( header.m_Type == Compressor::eZstd );
    TEST_ASSERT( header.m_TransformId == PchTransform::kPtrXfId );
    TEST_ASSERT( header.m_TransformVersion == PchTransform::kPtrXfVersion );
    TEST_ASSERT( header.m_UncompressedSize == content.GetSize() );
    TEST_ASSERT( header.m_CompressedSize == ( blob.GetSize() - Compressor::kHeaderSize ) );
    TEST_ASSERT( Compressor::ParseHeader( plain.Begin(), plain.GetSize(), header, true ) );
    TEST_ASSERT( ( header.m_TransformId == 0 ) && ( header.m_TransformVersion == 0 ) );

    // It decompresses to the PCH
    {
        CollectingSink sink;
        uint32_t size = 0;
        TEST_ASSERT( StreamDecompressor::DecompressMemory( blob.Begin(), blob.GetSize(), sink, size ) );
        TEST_ASSERT( size == content.GetSize() );
        TEST_ASSERT( sink.m_Data.GetSize() == content.GetSize() );
        TEST_ASSERT( memcmp( sink.m_Data.Begin(), content.Begin(), content.GetSize() ) == 0 );
    }

    // In pieces of any size
    const size_t pieceSizes[] = { 1, 5, 11, 12, 13, 4093, ( 1024 * 1024 ), 1000000000 };
    for ( const size_t pieceSize : pieceSizes )
    {
        StreamDecompressor decompressor;
        CollectingSink sink;
        size_t done = 0;
        if ( pieceSize == 1 )
        {
            for ( ; done < 200; ++done ) // The start: a byte at a time throughout would be slow
            {
                TEST_ASSERT( decompressor.Feed( blob.Begin() + done, 1, sink ) );
            }
        }
        while ( done < blob.GetSize() )
        {
            const size_t piece = Math::Min( pieceSize, blob.GetSize() - done );
            TEST_ASSERT( decompressor.Feed( blob.Begin() + done, piece, sink ) );
            done += piece;
        }
        TEST_ASSERT( decompressor.Finish() );
        TEST_ASSERT( sink.m_Data.GetSize() == content.GetSize() );
        TEST_ASSERT( memcmp( sink.m_Data.Begin(), content.Begin(), content.GetSize() ) == 0 );
    }
}

// TransformedBlobIsVerifiedAndUnpacked
//------------------------------------------------------------------------------
void TestPchBlob::TransformedBlobIsVerifiedAndUnpacked() const
{
    // Sizes that are not a multiple of a word, or of a piece
    const uint32_t sizes[] = { 1000003, ( 1024 * 1024 ) + 1, ( 3 * 1024 * 1024 ) + 7 };
    for ( const uint32_t size : sizes )
    {
        Array<char> content;
        MakePchLikeBytes( size, size, content );
        Array<char> blob;
        bool transformed = false;
        TEST_ASSERT( MakePchBlob( content, kPchLikeBase, blob, &transformed ) );
        TEST_ASSERT( transformed );
        const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );

        // It is the PCH of the id and size, and no other
        TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize(), pchId, size ) );
        TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize(), pchId + 1, size ) == false );
        TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize(), pchId, size + 1 ) == false );
        TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize() - 1, pchId, size ) == false );

        AStackString<> dir;
        GetTestDir( "TransformedUnpack", dir );
        AStackString<> stored( dir );
        stored += "pch.zst";
        TEST_ASSERT( WriteFile( stored, blob ) );
        TEST_ASSERT( PchBlob::VerifyFile( stored.Get(), pchId, size, nullptr ) );
        TEST_ASSERT( PchBlob::VerifyFile( stored.Get(), pchId + 1, size, nullptr ) == false );
        TEST_ASSERT( PchBlob::VerifyFile( stored.Get(), pchId, size - 1, nullptr ) == false );
        const Atomic<bool> aborted{ true };
        TEST_ASSERT( PchBlob::VerifyFile( stored.Get(), pchId, size, &aborted ) == false );

        // Unpacking inverts the transform: the raw file is the PCH
        AStackString<> temp( dir );
        temp += "pch.tmp";
        AStackString<> raw( dir );
        raw += "pch.pch";
        uint64_t rawSize = 0;
        TEST_ASSERT( PchBlob::Unpack( stored, pchId, temp, raw, nullptr, rawSize ) == PchBlob::UNPACK_OK );
        TEST_ASSERT( rawSize == size );
        Array<char> unpacked;
        TEST_ASSERT( ReadFile( raw, unpacked ) );
        TEST_ASSERT( unpacked.GetSize() == content.GetSize() );
        TEST_ASSERT( memcmp( unpacked.Begin(), content.Begin(), content.GetSize() ) == 0 );
        TEST_ASSERT( FileIO::FileExists( stored.Get() ) );
        TEST_ASSERT( FileIO::FileExists( temp.Get() ) == false );

        // And it is the id that is checked: of the PCH, not of what was stored
        TEST_ASSERT( FileIO::FileDelete( raw.Get() ) );
        TEST_ASSERT( PchBlob::Unpack( stored, pchId + 1, temp, raw, nullptr, rawSize ) == PchBlob::UNPACK_CORRUPT );
        TEST_ASSERT( FileIO::FileExists( raw.Get() ) == false );
        TEST_ASSERT( FileIO::FileExists( temp.Get() ) == false );
    }
}

// RejectsTransformedBlobsThatAreNotValid
//------------------------------------------------------------------------------
void TestPchBlob::RejectsTransformedBlobsThatAreNotValid() const
{
    Array<char> content;
    MakePchLikeBytes( 700001, 21, content );
    Array<char> blob;
    bool transformed = false;
    TEST_ASSERT( MakePchBlob( content, kPchLikeBase, blob, &transformed ) );
    TEST_ASSERT( transformed );
    const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
    const uint32_t size = (uint32_t)content.GetSize();
    TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize(), pchId, size ) );

    auto isRejected = [ & ]( const Array<char> & bad )
    {
        CollectingSink sink;
        uint32_t decompressedSize = 0;
        return ( PchBlob::Verify( bad.Begin(), bad.GetSize(), pchId, size ) == false ) &&
               ( StreamDecompressor::DecompressMemory( bad.Begin(), bad.GetSize(), sink, decompressedSize ) == false );
    };

    // A damaged body: anywhere in it
    const size_t positions[] = { 13, 100, blob.GetSize() / 3, blob.GetSize() / 2, blob.GetSize() - 5, blob.GetSize() - 1 };
    for ( const size_t position : positions )
    {
        Array<char> bad;
        bad = blob;
        bad[ position ] ^= 0x5A;
        TEST_ASSERT( PchBlob::Verify( bad.Begin(), bad.GetSize(), pchId, size ) == false ); // It may decompress, to what is not the PCH

        // The worker's copy of it is not unpacked
        AStackString<> dir;
        GetTestDir( "TransformedCorrupt", dir );
        AStackString<> stored( dir );
        stored += "pch.zst";
        TEST_ASSERT( WriteFile( stored, bad ) );
        AStackString<> temp( dir );
        temp += "pch.tmp";
        AStackString<> raw( dir );
        raw += "pch.pch";
        uint64_t rawSize = 0;
        TEST_ASSERT( PchBlob::Unpack( stored, pchId, temp, raw, nullptr, rawSize ) == PchBlob::UNPACK_CORRUPT );
        TEST_ASSERT( CountFilesIn( dir ) == 1 );
    }

    // A header that names a transform that is not known: another id, another version, bits that mean nothing
    const uint32_t wrongTypes[] = {
        ( Compressor::eZstd | ( 2u << 8 ) | ( (uint32_t)PchTransform::kPtrXfVersion << 16 ) ),                         // Another id
        ( Compressor::eZstd | ( 255u << 8 ) | ( (uint32_t)PchTransform::kPtrXfVersion << 16 ) ),
        ( Compressor::eZstd | ( (uint32_t)PchTransform::kPtrXfId << 8 ) | ( ( PchTransform::kPtrXfVersion + 1u ) << 16 ) ), // Another version
        ( Compressor::eZstd | ( (uint32_t)PchTransform::kPtrXfId << 8 ) | ( 255u << 16 ) ),
        ( Compressor::eZstd | ( (uint32_t)PchTransform::kPtrXfId << 8 ) ),                                              // No version
        ( Compressor::eZstd | ( (uint32_t)PchTransform::kPtrXfVersion << 16 ) ),                                        // A version of no transform
        ( kTransformedType | ( 1u << 24 ) ),                                                                            // Bits that are not used
        ( Compressor::eUncompressed | ( (uint32_t)PchTransform::kPtrXfId << 8 ) | ( (uint32_t)PchTransform::kPtrXfVersion << 16 ) ), // Of data that is not compressed
        ( Compressor::eLZ4 | ( (uint32_t)PchTransform::kPtrXfId << 8 ) | ( (uint32_t)PchTransform::kPtrXfVersion << 16 ) ),
        ( 3u | ( (uint32_t)PchTransform::kPtrXfId << 8 ) | ( (uint32_t)PchTransform::kPtrXfVersion << 16 ) ),           // A codec that is not known
    };
    for ( const uint32_t type : wrongTypes )
    {
        Array<char> bad;
        bad = blob;
        SetHeaderWord( bad, 0, type );
        TEST_ASSERT( isRejected( bad ) );
    }

    // Data that is not compressed is not data of a transform, though it is the size of the PCH
    {
        Array<char> random;
        MakeRandomBytes( 100000, 22, random );
        Array<char> copy;
        TEST_ASSERT( MakePchBlob( random, kNoTransform, copy ) );
        TEST_ASSERT( GetHeaderWord( copy, 0 ) == Compressor::eUncompressed );
        const uint64_t randomId = PchId::Calc( random.Begin(), random.GetSize() );
        TEST_ASSERT( PchBlob::Verify( copy.Begin(), copy.GetSize(), randomId, (uint32_t)random.GetSize() ) );
        SetHeaderWord( copy, 0, Compressor::eUncompressed | ( (uint32_t)PchTransform::kPtrXfId << 8 ) | ( (uint32_t)PchTransform::kPtrXfVersion << 16 ) );
        TEST_ASSERT( PchBlob::Verify( copy.Begin(), copy.GetSize(), randomId, (uint32_t)random.GetSize() ) == false );
    }

    // A size that is not that of the PCH
    const int32_t differences[] = { -1, 1, -8, 8 };
    for ( const int32_t difference : differences )
    {
        Array<char> bad;
        bad = blob;
        SetHeaderWord( bad, 1, size + (uint32_t)difference );
        CollectingSink sink;
        uint32_t decompressedSize = 0;
        TEST_ASSERT( StreamDecompressor::DecompressMemory( bad.Begin(), bad.GetSize(), sink, decompressedSize ) == false );
        TEST_ASSERT( PchBlob::Verify( bad.Begin(), bad.GetSize(), pchId, size + (uint32_t)difference ) == false );
    }

    // Data cut short, or with more after it
    {
        Array<char> bad;
        bad = blob;
        bad.SetSize( bad.GetSize() - 1 );
        TEST_ASSERT( isRejected( bad ) );
        bad = blob;
        bad.Append( 'x' );
        TEST_ASSERT( isRejected( bad ) );
    }
}

// TransformFallsBackToThePlainBlob
//------------------------------------------------------------------------------
void TestPchBlob::TransformFallsBackToThePlainBlob() const
{
    // Data that does not get smaller is written as it is, with the header of data that is not compressed: the
    // transform makes it no smaller
    Array<char> content;
    MakeRandomBytes( 500000, 8, content );
    Array<char> plain;
    Array<char> blob;
    bool transformed = true;
    TEST_ASSERT( MakePchBlob( content, kPchLikeBase, blob, &transformed ) );
    TEST_ASSERT( transformed == false );
    TEST_ASSERT( MakePchBlob( content, kNoTransform, plain, nullptr ) );
    TEST_ASSERT( blob.GetSize() == plain.GetSize() );
    TEST_ASSERT( memcmp( blob.Begin(), plain.Begin(), blob.GetSize() ) == 0 );
    TEST_ASSERT( GetHeaderWord( blob, 0 ) == Compressor::eUncompressed );
    TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize(), PchId::Calc( content.Begin(), content.GetSize() ), (uint32_t)content.GetSize() ) );
}

// TransformedFrameIsBounded
//------------------------------------------------------------------------------
void TestPchBlob::TransformedFrameIsBounded() const
{
    // A frame that makes more than the transform can make of a PCH of the size the header says is not
    // decompressed to the end, however well it compresses
    Array<char> stream;
    for ( uint32_t i = 0; i < 4; ++i )
    {
        stream.Append( (char)( kPchLikeBase >> ( 8 * i ) ) );
    }
    const uint32_t numZeroWords = 5000;
    for ( uint32_t i = 0; i < ( numZeroWords * 8 ); ++i )
    {
        stream.Append( 0 );
    }
    Array<char> blob;
    Compress( stream, blob );
    TEST_ASSERT( GetHeaderWord( blob, 0 ) == Compressor::eZstd );

    // What it makes is a PCH of numZeroWords words, and is what it is when the header says so
    {
        Array<char> good;
        good = blob;
        SetHeaderWord( good, 0, kTransformedType );
        SetHeaderWord( good, 1, numZeroWords * 8 );
        CollectingSink sink;
        uint32_t size = 0;
        TEST_ASSERT( StreamDecompressor::DecompressMemory( good.Begin(), good.GetSize(), sink, size ) );
        TEST_ASSERT( sink.m_Data.GetSize() == ( numZeroWords * 8 ) );
    }

    // A header that says it is smaller
    const uint32_t claims[] = { 1000, 8, 39992, ( ( numZeroWords * 8 ) - 1 ), ( ( numZeroWords * 8 ) + 1 ) };
    for ( const uint32_t claim : claims )
    {
        Array<char> bad;
        bad = blob;
        SetHeaderWord( bad, 0, kTransformedType );
        SetHeaderWord( bad, 1, claim );
        CollectingSink sink;
        uint32_t size = 0;
        const bool isRead = StreamDecompressor::DecompressMemory( bad.Begin(), bad.GetSize(), sink, size );
        TEST_ASSERT( isRead == false );
        TEST_ASSERT( sink.m_Data.GetSize() <= claim ); // Nothing more than it said was handed on
    }
}

// PchBlobsUseTheWideWindow
//------------------------------------------------------------------------------
void TestPchBlob::PchBlobsUseTheWideWindow() const
{
    // The window of a PCH that is larger than it is: the one of the PCH parameters (see Compressor::kZstdPchWindowLog),
    // which a decoder takes with no setting up. The transformed form is told no size, so it says the window it was given,
    // as long as that holds the PCH. The plain form is told its size: it is a single segment, and so says no window,
    // until the PCH is larger than the window. That is why the plain one is of more than 64 MiB, and the other is not.
    struct Case { uint32_t m_Size; bool m_Transform; };
    const Case cases[] = {
        { ( 65 * 1024 * 1024 ) + 3, false },
        { ( 33 * 1024 * 1024 ) + 3, true },
    };
    for ( const Case & c : cases )
    {
        Array<char> content;
        MakePchLikeBytes( c.m_Size, 31, content );
        const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
        Array<char> blob;
        bool transformed = false;
        TEST_ASSERT( MakePchBlob( content, c.m_Transform ? kPchLikeBase : kNoTransform, blob, &transformed ) );
        TEST_ASSERT( transformed == c.m_Transform );
        TEST_ASSERT( GetBlobWindowLog( blob ) == Compressor::kZstdPchWindowLog );

        // It is read as any other: verified, and unpacked
        TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize(), pchId, c.m_Size ) );
        AStackString<> dir;
        GetTestDir( "WideWindow", dir );
        AStackString<> stored( dir );
        stored += "pch.zst";
        TEST_ASSERT( WriteFile( stored, blob ) );
        AStackString<> temp( dir );
        temp += "pch.tmp";
        AStackString<> raw( dir );
        raw += "pch.pch";
        uint64_t rawSize = 0;
        TEST_ASSERT( PchBlob::Unpack( stored, pchId, temp, raw, nullptr, rawSize ) == PchBlob::UNPACK_OK );
        TEST_ASSERT( rawSize == c.m_Size );
        TEST_ASSERT( FileIO::FileDelete( raw.Get() ) );

        // A worker of 24.0 reads the plain one with the decoder it has
        if ( c.m_Transform == false )
        {
            Compressor decompressor;
            TEST_ASSERT( Compressor::IsValidData( blob.Begin(), blob.GetSize() ) );
            TEST_ASSERT( decompressor.Decompress( blob.Begin() ) );
            TEST_ASSERT( decompressor.GetResultSize() == content.GetSize() );
            TEST_ASSERT( memcmp( decompressor.GetResult(), content.Begin(), content.GetSize() ) == 0 );
        }
        FileIO::FileDelete( stored.Get() );
    }
}

// PchBlobWindowsFollowTheirSize
//------------------------------------------------------------------------------
void TestPchBlob::PchBlobWindowsFollowTheirSize() const
{
    // The window of a frame is what a worker allocates to read it: that of a PCH of a few MiB is not that of a large one,
    // whichever the form. (A frame that is a single segment has the size of its data for a window.)
    const uint32_t sizes[] = { 100000, ( 1024 * 1024 ) + 5, ( 5 * 1024 * 1024 ) + 7 };
    for ( const uint32_t size : sizes )
    {
        uint32_t expectedLog = 10; // The smallest window there is
        while ( ( 1ull << expectedLog ) < size )
        {
            ++expectedLog;
        }
        TEST_ASSERT( expectedLog < Compressor::kZstdPchWindowLog );

        Array<char> content;
        MakePchLikeBytes( size, size, content );
        Array<char> plain;
        Array<char> blob;
        bool transformed = false;
        TEST_ASSERT( MakePchBlob( content, kNoTransform, plain ) );
        TEST_ASSERT( MakePchBlob( content, kPchLikeBase, blob, &transformed ) );
        TEST_ASSERT( transformed );
        const uint32_t plainLog = GetBlobWindowLog( plain );
        const uint32_t transformedLog = GetBlobWindowLog( blob );
        OUTPUT( "PchBlobWindowsFollowTheirSize: %u bytes: window log %u plain, %u transformed\n", size, plainLog, transformedLog );
        TEST_ASSERTM( ( plainLog == 0 ) || ( plainLog <= expectedLog ), "%u bytes", size );
        TEST_ASSERTM( transformedLog == expectedLog, "%u bytes", size );
        const uint64_t pchId = PchId::Calc( content.Begin(), content.GetSize() );
        TEST_ASSERT( PchBlob::Verify( blob.Begin(), blob.GetSize(), pchId, size ) );
    }
}

// HostileTransformedFramesAreRejected
//------------------------------------------------------------------------------
void TestPchBlob::HostileTransformedFramesAreRejected() const
{
    // Frames of transformed data that a client of ours does not make, whatever they hold. Each is a valid zstd frame, so it
    // is the transform that fails, and none makes more than the size its header claims.
    const uint32_t base = kPchLikeBase;
    const uint32_t f1 = ( base | 0x80000000u );
    const uint32_t f2 = ( base | 0x40000000u );
    const uint32_t f3 = ( base | 0x20000000u );
    const uint32_t kNumPadWords = 300; // Words that are not pointers, so that what follows the base compresses to a frame
    auto putWord = [ & ]( Array<char> & stream, uint64_t word )
    {
        for ( uint32_t b = 0; b < 8; ++b )
        {
            stream.Append( (char)( word >> ( 8 * b ) ) );
        }
    };
    auto makeStream = [ & ]( Array<char> & stream )
    {
        for ( uint32_t b = 0; b < 4; ++b )
        {
            stream.Append( (char)( base >> ( 8 * b ) ) );
        }
        for ( uint32_t i = 0; i < kNumPadWords; ++i )
        {
            putWord( stream, 0 );
        }
    };
    auto makeBlob = [ & ]( const Array<char> & stream, uint32_t numWords, Array<char> & outBlob )
    {
        Compress( stream, outBlob );
        TEST_ASSERT( GetHeaderWord( outBlob, 0 ) == Compressor::eZstd ); // It is a frame
        SetHeaderWord( outBlob, 0, kTransformedType );
        SetHeaderWord( outBlob, 1, ( kNumPadWords + numWords ) * 8 ); // What the data would be if it were valid
    };
    auto isRead = [ & ]( const Array<char> & blob )
    {
        const uint32_t claimedSize = GetHeaderWord( blob, 1 );

        // Whole, and in pieces
        CollectingSink sink;
        uint32_t size = 0;
        const bool result = StreamDecompressor::DecompressMemory( blob.Begin(), blob.GetSize(), sink, size );
        TEST_ASSERT( sink.m_Data.GetSize() <= claimedSize );
        StreamDecompressor decompressor;
        CollectingSink pieces;
        bool fed = true;
        for ( size_t done = 0; ( done < blob.GetSize() ) && fed; done += 7 )
        {
            fed = decompressor.Feed( blob.Begin() + done, Math::Min<size_t>( 7, blob.GetSize() - done ), pieces );
        }
        TEST_ASSERT( pieces.m_Data.GetSize() <= claimedSize );
        TEST_ASSERT( ( fed && decompressor.Finish() ) == result );
        return result;
    };

    // The control: a stream that is valid is read
    {
        Array<char> stream;
        makeStream( stream );
        putWord( stream, ( (uint64_t)base << 32 ) | 0x1000u );
        putWord( stream, ( (uint64_t)f1 << 32 ) );
        Array<char> blob;
        makeBlob( stream, 2, blob );
        TEST_ASSERT( isRead( blob ) );
    }

    // An escape at the end of the data, that is not followed by the word it is for. The header says the size of what
    // comes before it, so that it is the escape that is at fault and not the size.
    {
        Array<char> stream;
        makeStream( stream );
        putWord( stream, ( (uint64_t)base << 32 ) | 0x1000u );
        putWord( stream, ( (uint64_t)f3 << 32 ) );
        Array<char> blob;
        makeBlob( stream, 1, blob );
        TEST_ASSERT( isRead( blob ) == false );
    }

    // References to entries of the cache, which holds as many as there were pointers before them: those it holds are read,
    // and those it does not, when it is empty, past what is in it and past what it can hold, are not
    struct Reference { uint64_t m_Word; uint32_t m_NumEntriesNeeded; };
    const Reference references[] = {
        { ( (uint64_t)f1 << 32 ),                                   1 },
        { ( (uint64_t)f1 << 32 ) | 1,                               2 },
        { ( (uint64_t)f1 << 32 ) | 32,                              33 },
        { ( (uint64_t)f1 << 32 ) | 0xFFFFFFFFu,                     0xFFFFFFFFu },
        { ( (uint64_t)f2 << 32 ) | ( 1u << 16 ) | 2048,             2 },
        { ( (uint64_t)f2 << 32 ) | ( 40u << 16 ) | 2048,            41 },
        { ( (uint64_t)f2 << 32 ) | ( 0xFFFFu << 16 ) | 0xFFFFu,     0x10000 },
    };
    uint32_t numRejected = 0;
    for ( uint32_t i = 0; i < ( sizeof( references ) / sizeof( references[ 0 ] ) ); ++i )
    {
        for ( uint32_t numPointers = 0; numPointers < 3; ++numPointers )
        {
            Array<char> stream;
            makeStream( stream );
            for ( uint32_t p = 0; p < numPointers; ++p )
            {
                putWord( stream, ( (uint64_t)base << 32 ) | ( 0x1000u + p ) );
            }
            putWord( stream, references[ i ].m_Word );
            putWord( stream, 0 );
            Array<char> blob;
            makeBlob( stream, numPointers + 2, blob );
            const bool isValid = ( references[ i ].m_NumEntriesNeeded <= numPointers );
            TEST_ASSERTM( isRead( blob ) == isValid, "reference %u after %u pointers", i, numPointers );
            numRejected += ( isValid ? 0 : 1 );
        }
    }
    TEST_ASSERT( numRejected >= 15 );

    // A frame that holds less than the base the transform begins with: 1 to 3 bytes, which is not what Compress makes a frame of
    for ( uint32_t numBytes = 1; numBytes < 4; ++numBytes )
    {
        const char bytes[ 3 ] = { 0x40, 0x41, 0x42 };
        Array<char> blob;
        blob.SetSize( Compressor::kHeaderSize + ZSTD_compressBound( numBytes ) );
        const size_t frameSize = ZSTD_compress( blob.Begin() + Compressor::kHeaderSize, blob.GetSize() - Compressor::kHeaderSize, bytes, numBytes, 3 );
        TEST_ASSERT( ZSTD_isError( frameSize ) == false );
        blob.SetSize( Compressor::kHeaderSize + frameSize );
        SetHeaderWord( blob, 0, kTransformedType );
        SetHeaderWord( blob, 1, 64 ); // At least the size of the frame, so that the header is one
        SetHeaderWord( blob, 2, (uint32_t)frameSize );
        TEST_ASSERTM( isRead( blob ) == false, "%u bytes", numBytes );
    }

    // A frame that is followed by more
    {
        Array<char> stream;
        makeStream( stream );
        putWord( stream, ( (uint64_t)base << 32 ) | 0x1000u );
        putWord( stream, ( (uint64_t)f1 << 32 ) );
        Array<char> blob;
        makeBlob( stream, 2, blob );
        TEST_ASSERT( isRead( blob ) );
        const uint32_t numTrailings[] = { 1, 4, 12, 100 };
        for ( const uint32_t numTrailing : numTrailings )
        {
            Array<char> longer;
            longer = blob;
            for ( uint32_t i = 0; i < numTrailing; ++i )
            {
                longer.Append( (char)( 0x28 + i ) ); // Not the start of another frame either
            }
            TEST_ASSERTM( isRead( longer ) == false, "%u bytes after the frame", numTrailing );
        }
    }
}

//------------------------------------------------------------------------------
