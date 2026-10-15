// TestPchTransfer.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchReceiver.h"
#include "Tools/FBuild/FBuildCore/Protocol/PrioritySendQueue.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Math/xxHash.h"
#include "Core/Strings/AStackString.h"

// TestPchTransfer
//------------------------------------------------------------------------------
class TestPchTransfer : public FBuildTest
{
private:
    DECLARE_TESTS

    void ReceiverReassemblesChunks() const;
    void ReceiverRejectsAWrongId() const;
    void ReceiverRejectsMalformedChunks() const;
    void ReceiverDropsAPartialPch() const;
    void ReceiversOfTheSamePchDoNotMeet() const;
    void ReceiverKeepsACachedPch() const;
    void SendQueueSendsBulkDataLast() const;

    static constexpr uint32_t kChunkSize = Protocol::kMinPchChunkSize; // Small, so a PCH has many chunks

    // Data that does not compress, so the size of what is sent is the size asked for
    struct TestPch
    {
        Array<char> m_Compressed;
        uint32_t    m_UncompressedSize = 0;
        uint64_t    m_Id = 0; // As the sender makes it: the hash of the uncompressed PCH
        Array<char> m_Raw;
    };
    static void MakePch( uint32_t compressedSize, uint64_t seed, TestPch & outPch );

    // Cuts one chunk out of the compressed data
    static PchReceiver::Chunk GetChunk( const TestPch & pch, uint32_t chunkSize, uint32_t index );

    // The directory a test keeps a PCH in, emptied of what an earlier run left
    static void GetCleanDir( const char * testName, uint64_t pchId, AString & outDir );
    static uint32_t CountFiles( const AString & dir );
    static bool ReadFile( const AString & file, Array<char> & outContent );
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestPchTransfer )
    REGISTER_TEST( ReceiverReassemblesChunks )
    REGISTER_TEST( ReceiverRejectsAWrongId )
    REGISTER_TEST( ReceiverRejectsMalformedChunks )
    REGISTER_TEST( ReceiverDropsAPartialPch )
    REGISTER_TEST( ReceiversOfTheSamePchDoNotMeet )
    REGISTER_TEST( ReceiverKeepsACachedPch )
    REGISTER_TEST( SendQueueSendsBulkDataLast )
REGISTER_TESTS_END

// MakePch
//------------------------------------------------------------------------------
/*static*/ void TestPchTransfer::MakePch( uint32_t compressedSize, uint64_t seed, TestPch & outPch )
{
    // Data that cannot be compressed is stored with a header
    const uint32_t headerSize = 12;
    TEST_ASSERT( compressedSize > headerSize );
    outPch.m_UncompressedSize = ( compressedSize - headerSize );
    outPch.m_Raw.SetSize( outPch.m_UncompressedSize );
    uint64_t x = ( seed | 1 );
    for ( char & c : outPch.m_Raw )
    {
        x ^= ( x << 13 );
        x ^= ( x >> 7 );
        x ^= ( x << 17 );
        c = (char)( x >> 24 );
    }
    outPch.m_Id = xxHash::Calc64( outPch.m_Raw.Begin(), outPch.m_Raw.GetSize() );

    Compressor c;
    c.CompressZstd( outPch.m_Raw.Begin(), outPch.m_Raw.GetSize(), Compressor::kZstdTransferLevel );
    TEST_ASSERT( c.GetResultSize() == compressedSize );
    outPch.m_Compressed.SetSize( c.GetResultSize() );
    memcpy( outPch.m_Compressed.Begin(), c.GetResult(), c.GetResultSize() );
}

// GetChunk
//------------------------------------------------------------------------------
/*static*/ PchReceiver::Chunk TestPchTransfer::GetChunk( const TestPch & pch, uint32_t chunkSize, uint32_t index )
{
    const uint32_t compressedSize = (uint32_t)pch.m_Compressed.GetSize();
    PchReceiver::Chunk chunk;
    chunk.m_PchId = pch.m_Id;
    chunk.m_UncompressedSize = pch.m_UncompressedSize;
    chunk.m_CompressedSize = compressedSize;
    chunk.m_ChunkSize = chunkSize;
    chunk.m_Index = index;
    chunk.m_Data = pch.m_Compressed.Begin() + ( (size_t)index * chunkSize );
    chunk.m_Size = Protocol::MsgPchFile::GetChunkSize( compressedSize, chunkSize, index );
    return chunk;
}

// GetCleanDir
//------------------------------------------------------------------------------
/*static*/ void TestPchTransfer::GetCleanDir( const char * testName, uint64_t pchId, AString & outDir )
{
    outDir.Format( "../tmp/Test/PchTransfer/%s/pch.%016" PRIx64 "%c", testName, pchId, (char)NATIVE_SLASH );
    Array<AString> old;
    FileIO::GetFiles( outDir, AStackString( "*" ), true, &old );
    for ( const AString & file : old )
    {
        FileIO::FileDelete( file.Get() );
    }
}

// CountFiles
//------------------------------------------------------------------------------
/*static*/ uint32_t TestPchTransfer::CountFiles( const AString & dir )
{
    Array<AString> files;
    FileIO::GetFiles( dir, AStackString( "*" ), true, &files );
    return (uint32_t)files.GetSize();
}

// ReadFile
//------------------------------------------------------------------------------
/*static*/ bool TestPchTransfer::ReadFile( const AString & file, Array<char> & outContent )
{
    FileStream f;
    if ( f.Open( file.Get() ) == false )
    {
        return false;
    }
    outContent.SetSize( (size_t)f.GetFileSize() );
    return ( f.Read( outContent.Begin(), outContent.GetSize() ) == outContent.GetSize() );
}

// ReceiverReassemblesChunks
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiverReassemblesChunks() const
{
    // Around the edges of a chunk, and with many chunks
    const uint32_t sizes[] = {
        100,                    // Less than a chunk
        kChunkSize,             // Exactly one
        kChunkSize + 1,         // One byte over
        ( 2 * kChunkSize ),     // Exactly a multiple
        ( 7 * kChunkSize ) + 13 // Many, the last partial
    };
    uint64_t seed = 1;
    for ( const uint32_t size : sizes )
    {
        TestPch pch;
        MakePch( size, ++seed, pch );

        AStackString<> dir;
        GetCleanDir( "Reassembles", pch.m_Id, dir );

        PchReceiver receiver;
        const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( size, kChunkSize );
        for ( uint32_t i = 0; i < numChunks; ++i )
        {
            AStackString<> file;
            bool alreadyCached = false;
            const PchReceiver::Result result = receiver.OnChunk( GetChunk( pch, kChunkSize, i ), dir, file, alreadyCached );
            TEST_ASSERT( result == ( ( i + 1 ) == numChunks ? PchReceiver::COMPLETE : PchReceiver::CONTINUE ) );
            if ( result == PchReceiver::COMPLETE )
            {
                TEST_ASSERT( alreadyCached == false );
                Array<char> content;
                TEST_ASSERT( ReadFile( file, content ) );
                TEST_ASSERT( content.GetSize() == pch.m_Raw.GetSize() );
                TEST_ASSERT( memcmp( content.Begin(), pch.m_Raw.Begin(), content.GetSize() ) == 0 );
            }
        }

        // Only the PCH is left
        TEST_ASSERT( CountFiles( dir ) == 1 );
    }
}

// ReceiverRejectsAWrongId
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiverRejectsAWrongId() const
{
    // The id is the hash of the PCH, so data that is not what the id says is not kept
    TestPch pch;
    MakePch( ( 3 * kChunkSize ) + 7, 100, pch );
    pch.m_Id++;

    AStackString<> dir;
    GetCleanDir( "WrongId", pch.m_Id, dir );
    PchReceiver receiver;
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)pch.m_Compressed.GetSize(), kChunkSize );
    for ( uint32_t i = 0; i < numChunks; ++i )
    {
        AStackString<> file;
        bool alreadyCached = false;
        const PchReceiver::Result result = receiver.OnChunk( GetChunk( pch, kChunkSize, i ), dir, file, alreadyCached );
        TEST_ASSERT( result == ( ( i + 1 ) == numChunks ? PchReceiver::FAILED : PchReceiver::CONTINUE ) );
    }
    TEST_ASSERT( CountFiles( dir ) == 0 );
}

// ReceiverRejectsMalformedChunks
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiverRejectsMalformedChunks() const
{
    TestPch pch;
    MakePch( ( 4 * kChunkSize ) + 5, 200, pch );
    TestPch other;
    MakePch( ( 2 * kChunkSize ) + 5, 300, other );
    AStackString<> dir;
    GetCleanDir( "Malformed", pch.m_Id, dir );
    AStackString<> otherDir;
    GetCleanDir( "Malformed", other.m_Id, otherDir );

    AStackString<> file;
    bool alreadyCached = false;

    // The first chunk is not the first
    {
        PchReceiver receiver;
        TEST_ASSERT( receiver.OnChunk( GetChunk( pch, kChunkSize, 1 ), dir, file, alreadyCached ) == PchReceiver::FAILED );
        TEST_ASSERT( CountFiles( dir ) == 0 );
    }

    // A chunk is missing
    {
        PchReceiver receiver;
        TEST_ASSERT( receiver.OnChunk( GetChunk( pch, kChunkSize, 0 ), dir, file, alreadyCached ) == PchReceiver::CONTINUE );
        TEST_ASSERT( receiver.OnChunk( GetChunk( pch, kChunkSize, 2 ), dir, file, alreadyCached ) == PchReceiver::FAILED );
        TEST_ASSERT( CountFiles( dir ) == 0 ); // What was received is dropped
    }

    // A chunk of the wrong size
    {
        PchReceiver receiver;
        TEST_ASSERT( receiver.OnChunk( GetChunk( pch, kChunkSize, 0 ), dir, file, alreadyCached ) == PchReceiver::CONTINUE );
        PchReceiver::Chunk chunk = GetChunk( pch, kChunkSize, 1 );
        chunk.m_Size--;
        TEST_ASSERT( receiver.OnChunk( chunk, dir, file, alreadyCached ) == PchReceiver::FAILED );
        TEST_ASSERT( CountFiles( dir ) == 0 );
    }

    // A chunk that says the PCH has other sizes than the first did
    {
        PchReceiver receiver;
        TEST_ASSERT( receiver.OnChunk( GetChunk( pch, kChunkSize, 0 ), dir, file, alreadyCached ) == PchReceiver::CONTINUE );
        PchReceiver::Chunk chunk = GetChunk( pch, kChunkSize, 1 );
        chunk.m_CompressedSize++;
        TEST_ASSERT( receiver.OnChunk( chunk, dir, file, alreadyCached ) == PchReceiver::FAILED );
        TEST_ASSERT( CountFiles( dir ) == 0 );
    }

    // A PCH starts before the one before it is complete
    {
        PchReceiver receiver;
        TEST_ASSERT( receiver.OnChunk( GetChunk( pch, kChunkSize, 0 ), dir, file, alreadyCached ) == PchReceiver::CONTINUE );
        TEST_ASSERT( receiver.OnChunk( GetChunk( other, kChunkSize, 0 ), otherDir, file, alreadyCached ) == PchReceiver::FAILED );
        TEST_ASSERT( CountFiles( dir ) == 0 );
        TEST_ASSERT( CountFiles( otherDir ) == 0 );
    }

    // Sizes that no sender should ask for
    {
        PchReceiver receiver;
        PchReceiver::Chunk chunk = GetChunk( pch, kChunkSize, 0 );
        chunk.m_CompressedSize = ( PchReceiver::GetMaxCompressedSize( chunk.m_UncompressedSize ) < 0xFFFFFFFFULL )
                                     ? (uint32_t)( PchReceiver::GetMaxCompressedSize( chunk.m_UncompressedSize ) + 1 )
                                     : 0xFFFFFFFFu;
        TEST_ASSERT( receiver.OnChunk( chunk, dir, file, alreadyCached ) == PchReceiver::FAILED );

        chunk = GetChunk( pch, kChunkSize, 0 );
        chunk.m_ChunkSize = ( Protocol::kMinPchChunkSize - 1 );
        TEST_ASSERT( receiver.OnChunk( chunk, dir, file, alreadyCached ) == PchReceiver::FAILED );

        chunk = GetChunk( pch, kChunkSize, 0 );
        chunk.m_ChunkSize = ( Protocol::kMaxPchChunkSize + 1 );
        TEST_ASSERT( receiver.OnChunk( chunk, dir, file, alreadyCached ) == PchReceiver::FAILED );

        chunk = GetChunk( pch, kChunkSize, 0 );
        chunk.m_UncompressedSize = 0;
        TEST_ASSERT( receiver.OnChunk( chunk, dir, file, alreadyCached ) == PchReceiver::FAILED );
        TEST_ASSERT( CountFiles( dir ) == 0 );
    }
}

// ReceiverDropsAPartialPch
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiverDropsAPartialPch() const
{
    TestPch pch;
    MakePch( ( 4 * kChunkSize ) + 5, 400, pch );
    AStackString<> dir;
    GetCleanDir( "Partial", pch.m_Id, dir );
    AStackString<> file;
    bool alreadyCached = false;

    // The connection ends with the PCH half received
    {
        PchReceiver receiver;
        TEST_ASSERT( receiver.OnChunk( GetChunk( pch, kChunkSize, 0 ), dir, file, alreadyCached ) == PchReceiver::CONTINUE );
        TEST_ASSERT( receiver.OnChunk( GetChunk( pch, kChunkSize, 1 ), dir, file, alreadyCached ) == PchReceiver::CONTINUE );
        TEST_ASSERT( CountFiles( dir ) == 1 ); // What has arrived is kept somewhere
    } // Destroyed, as the connection's state is
    TEST_ASSERT( CountFiles( dir ) == 0 );

    // The same by asking
    {
        PchReceiver receiver;
        TEST_ASSERT( receiver.OnChunk( GetChunk( pch, kChunkSize, 0 ), dir, file, alreadyCached ) == PchReceiver::CONTINUE );
        receiver.Abort();
        TEST_ASSERT( CountFiles( dir ) == 0 );
    }

    // It can be sent again from the start
    {
        PchReceiver receiver;
        const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)pch.m_Compressed.GetSize(), kChunkSize );
        PchReceiver::Result result = PchReceiver::CONTINUE;
        for ( uint32_t i = 0; i < numChunks; ++i )
        {
            result = receiver.OnChunk( GetChunk( pch, kChunkSize, i ), dir, file, alreadyCached );
        }
        TEST_ASSERT( result == PchReceiver::COMPLETE );
        TEST_ASSERT( CountFiles( dir ) == 1 );
    }
}

// ReceiversOfTheSamePchDoNotMeet
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiversOfTheSamePchDoNotMeet() const
{
    // Two coordinators send the same PCH to a worker, and their chunks arrive alternately
    TestPch pch;
    MakePch( ( 5 * kChunkSize ) + 3, 500, pch );
    AStackString<> dir;
    GetCleanDir( "SamePch", pch.m_Id, dir );

    PchReceiver a;
    PchReceiver b;
    AStackString<> fileA;
    AStackString<> fileB;
    bool cachedA = false;
    bool cachedB = false;
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)pch.m_Compressed.GetSize(), kChunkSize );
    PchReceiver::Result resultA = PchReceiver::CONTINUE;
    PchReceiver::Result resultB = PchReceiver::CONTINUE;
    for ( uint32_t i = 0; i < numChunks; ++i )
    {
        resultA = a.OnChunk( GetChunk( pch, kChunkSize, i ), dir, fileA, cachedA );
        if ( i == 1 )
        {
            TEST_ASSERT( CountFiles( dir ) == 2 ); // Each has its own
        }
        resultB = b.OnChunk( GetChunk( pch, kChunkSize, i ), dir, fileB, cachedB );
    }

    TEST_ASSERT( resultA == PchReceiver::COMPLETE );
    TEST_ASSERT( resultB == PchReceiver::COMPLETE );
    TEST_ASSERT( cachedA == false );
    TEST_ASSERT( cachedB ); // The first one had it in place already
    TEST_ASSERT( fileA == fileB );

    Array<char> content;
    TEST_ASSERT( ReadFile( fileA, content ) );
    TEST_ASSERT( content.GetSize() == pch.m_Raw.GetSize() );
    TEST_ASSERT( memcmp( content.Begin(), pch.m_Raw.Begin(), content.GetSize() ) == 0 );
    TEST_ASSERT( CountFiles( dir ) == 1 ); // No temporary file is left
}

// ReceiverKeepsACachedPch
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiverKeepsACachedPch() const
{
    TestPch pch;
    MakePch( ( 3 * kChunkSize ) + 1, 600, pch );
    AStackString<> dir;
    GetCleanDir( "Cached", pch.m_Id, dir );

    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)pch.m_Compressed.GetSize(), kChunkSize );
    AStackString<> file;
    bool alreadyCached = false;
    {
        PchReceiver receiver;
        for ( uint32_t i = 0; i < numChunks; ++i )
        {
            receiver.OnChunk( GetChunk( pch, kChunkSize, i ), dir, file, alreadyCached );
        }
    }
    TEST_ASSERT( CountFiles( dir ) == 1 );

    // Sent again, it is read and dropped, and nothing is written
    PchReceiver receiver;
    for ( uint32_t i = 0; i < numChunks; ++i )
    {
        const PchReceiver::Result result = receiver.OnChunk( GetChunk( pch, kChunkSize, i ), dir, file, alreadyCached );
        TEST_ASSERT( result == ( ( i + 1 ) == numChunks ? PchReceiver::COMPLETE : PchReceiver::CONTINUE ) );
        TEST_ASSERT( CountFiles( dir ) == 1 );
    }
    TEST_ASSERT( alreadyCached );
}

// SendQueueSendsBulkDataLast
//------------------------------------------------------------------------------
void TestPchTransfer::SendQueueSendsBulkDataLast() const
{
    struct Item
    {
        uint32_t m_Id;
    };
    PrioritySendQueue<Item> queue;
    Array<Item> items;

    TEST_ASSERT( queue.Take( items ) == false );

    queue.Push( Item{ 1 }, false );
    queue.Push( Item{ 2 }, true );
    queue.Push( Item{ 3 }, true );
    queue.Push( Item{ 4 }, false );

    // What is not bulk data goes first, in order
    TEST_ASSERT( queue.Take( items ) );
    TEST_ASSERT( items.GetSize() == 2 );
    TEST_ASSERT( items[ 0 ].m_Id == 1 );
    TEST_ASSERT( items[ 1 ].m_Id == 4 );

    // Then one piece of bulk data at a time
    items.Clear();
    TEST_ASSERT( queue.Take( items ) );
    TEST_ASSERT( items.GetSize() == 1 );
    TEST_ASSERT( items[ 0 ].m_Id == 2 );

    // A message queued while a transfer is under way goes before the rest of it
    queue.Push( Item{ 5 }, false );
    items.Clear();
    TEST_ASSERT( queue.Take( items ) );
    TEST_ASSERT( items.GetSize() == 1 );
    TEST_ASSERT( items[ 0 ].m_Id == 5 );

    items.Clear();
    TEST_ASSERT( queue.Take( items ) );
    TEST_ASSERT( items.GetSize() == 1 );
    TEST_ASSERT( items[ 0 ].m_Id == 3 );

    items.Clear();
    TEST_ASSERT( queue.Take( items ) == false );
}

//------------------------------------------------------------------------------
