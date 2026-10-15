// TestPchBlob.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchBlob.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchId.h"
#include "Tools/FBuild/FBuildCore/Helpers/StreamDecompressor.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Process/Atomic.h"
#include "Core/Strings/AStackString.h"

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

    // Compressible content with some variation
    static void MakeContent( uint32_t size, uint32_t seed, Array<char> & outContent );
    static void Compress( const Array<char> & content, Array<char> & outBlob );
    static void GetTestDir( const char * testName, AString & outDir );
    static bool WriteFile( const AString & path, const Array<char> & content );
    static bool ReadFile( const AString & path, Array<char> & outContent );
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

// WriteFile
//------------------------------------------------------------------------------
/*static*/ bool TestPchBlob::WriteFile( const AString & path, const Array<char> & content )
{
    FileStream f;
    return ( FileIO::EnsurePathExistsForFile( path ) &&
             f.Open( path.Get(), FileStream::WRITE_ONLY ) &&
             ( f.Write( content.Begin(), content.GetSize() ) == content.GetSize() ) );
}

// ReadFile
//------------------------------------------------------------------------------
/*static*/ bool TestPchBlob::ReadFile( const AString & path, Array<char> & outContent )
{
    FileStream f;
    if ( f.Open( path.Get() ) == false )
    {
        return false;
    }
    outContent.SetSize( (size_t)f.GetFileSize() );
    return ( f.Read( outContent.Begin(), outContent.GetSize() ) == outContent.GetSize() );
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

        struct NoObserver : public Compressor::FileObserver
        {
            virtual void OnPass() override {}
            virtual void OnPiece( const void *, size_t ) override {}
        } observer;
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

//------------------------------------------------------------------------------
