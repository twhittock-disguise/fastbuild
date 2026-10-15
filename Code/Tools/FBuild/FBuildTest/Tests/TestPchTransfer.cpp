// TestPchTransfer.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchId.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchReceiver.h"
#include "Tools/FBuild/FBuildCore/Protocol/PrioritySendQueue.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/Protocol/SendKind.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Math/xxHash.h"
#include "Core/Process/Process.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Time.h"

// TestPchTransfer
//------------------------------------------------------------------------------
class TestPchTransfer : public FBuildTest
{
private:
    DECLARE_TESTS

    void ReceiverReassemblesChunks() const;
    void ReceiverRejectsAWrongId() const;
    void ReceiverRejectsAWrongSize() const;
    void ReceiverRejectsMalformedChunks() const;
    void ReceiverAcceptsTheLimits() const;
    void ReceiverDropsAPartialPch() const;
    void ReceiversOfTheSamePchDoNotMeet() const;
    void ReceiverKeepsACachedPch() const;
    void ReceiverKeepsAStoredPchWhateverItsSize() const;
    void ReceiverCleansUpWhenItCannotPublish() const;
    void SendQueueSendsBulkDataLast() const;
    void SendQueueDoesNotStarveBulkData() const;
    void BulkSendKinds() const;

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

    // The message of a chunk of the compressed data, and where its data is
    struct TestChunk
    {
        TestChunk( const TestPch & pch, uint32_t chunkSize, uint32_t index );

        Protocol::MsgPchFile    m_Msg;
        const void *            m_Data;
        size_t                  m_Size;
    };
    static PchReceiver::Result Feed( PchReceiver & receiver, const TestChunk & chunk, const AString & pchFile, bool & outAlreadyCached );
    static PchReceiver::Result FeedAll( PchReceiver & receiver, const TestPch & pch, const AString & pchFile, bool & outAlreadyCached );

    // Where a test keeps a PCH, with what an earlier run left removed
    static void GetCleanPchFile( const char * testName, uint64_t pchId, AString & outPchFile );
    static void GetDirOf( const AString & file, AString & outDir );
    static bool ReadFile( const AString & file, Array<char> & outContent );
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestPchTransfer )
    REGISTER_TEST( ReceiverReassemblesChunks )
    REGISTER_TEST( ReceiverRejectsAWrongId )
    REGISTER_TEST( ReceiverRejectsAWrongSize )
    REGISTER_TEST( ReceiverRejectsMalformedChunks )
    REGISTER_TEST( ReceiverAcceptsTheLimits )
    REGISTER_TEST( ReceiverDropsAPartialPch )
    REGISTER_TEST( ReceiversOfTheSamePchDoNotMeet )
    REGISTER_TEST( ReceiverKeepsACachedPch )
    REGISTER_TEST( ReceiverKeepsAStoredPchWhateverItsSize )
    REGISTER_TEST( ReceiverCleansUpWhenItCannotPublish )
    REGISTER_TEST( SendQueueSendsBulkDataLast )
    REGISTER_TEST( SendQueueDoesNotStarveBulkData )
    REGISTER_TEST( BulkSendKinds )
REGISTER_TESTS_END

// MakePch
//------------------------------------------------------------------------------
/*static*/ void TestPchTransfer::MakePch( uint32_t compressedSize, uint64_t seed, TestPch & outPch )
{
    // Data that cannot be compressed is stored with a header
    const uint32_t headerSize = 12;
    TEST_ASSERT( compressedSize > headerSize );
    outPch.m_UncompressedSize = ( compressedSize - headerSize );
    MakeRandomBytes( outPch.m_UncompressedSize, seed, outPch.m_Raw );
    outPch.m_Id = PchId::Calc( outPch.m_Raw.Begin(), outPch.m_Raw.GetSize() );

    Compressor c;
    c.CompressZstd( outPch.m_Raw.Begin(), outPch.m_Raw.GetSize(), Compressor::kZstdTransferLevel );
    TEST_ASSERT( c.GetResultSize() == compressedSize );
    outPch.m_Compressed.SetSize( c.GetResultSize() );
    memcpy( outPch.m_Compressed.Begin(), c.GetResult(), c.GetResultSize() );
}

// TestChunk
//------------------------------------------------------------------------------
TestPchTransfer::TestChunk::TestChunk( const TestPch & pch, uint32_t chunkSize, uint32_t index )
    : m_Msg( pch.m_Id, pch.m_UncompressedSize, (uint32_t)pch.m_Compressed.GetSize(), chunkSize, index )
    , m_Data( pch.m_Compressed.Begin() + ( (size_t)index * chunkSize ) )
    , m_Size( Protocol::MsgPchFile::GetChunkLength( (uint32_t)pch.m_Compressed.GetSize(), chunkSize, index ) )
{
}

// Feed
//------------------------------------------------------------------------------
/*static*/ PchReceiver::Result TestPchTransfer::Feed( PchReceiver & receiver,
                                                       const TestChunk & chunk,
                                                       const AString & pchFile,
                                                       bool & outAlreadyCached )
{
    return receiver.OnChunk( chunk.m_Msg, chunk.m_Data, chunk.m_Size, pchFile, outAlreadyCached );
}

// FeedAll
//------------------------------------------------------------------------------
/*static*/ PchReceiver::Result TestPchTransfer::FeedAll( PchReceiver & receiver,
                                                         const TestPch & pch,
                                                         const AString & pchFile,
                                                         bool & outAlreadyCached )
{
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)pch.m_Compressed.GetSize(), kChunkSize );
    PchReceiver::Result result = PchReceiver::CONTINUE;
    for ( uint32_t i = 0; i < numChunks; ++i )
    {
        result = Feed( receiver, TestChunk( pch, kChunkSize, i ), pchFile, outAlreadyCached );
        TEST_ASSERT( result == ( ( i + 1 ) == numChunks ? PchReceiver::COMPLETE : PchReceiver::CONTINUE ) );
    }
    return result;
}

// GetCleanPchFile
//------------------------------------------------------------------------------
/*static*/ void TestPchTransfer::GetCleanPchFile( const char * testName, uint64_t pchId, AString & outPchFile )
{
    AStackString<> dir;
    dir.Format( "../tmp/Test/PchTransfer/%s/pch.%016" PRIx64 "%c", testName, pchId, (char)NATIVE_SLASH );
    Array<AString> old;
    FileIO::GetFiles( dir, AStackString( "*" ), true, &old );
    for ( const AString & file : old )
    {
        FileIO::FileDelete( file.Get() );
    }
    outPchFile = dir;
    outPchFile += "pch.zst";
}

// GetDirOf
//------------------------------------------------------------------------------
/*static*/ void TestPchTransfer::GetDirOf( const AString & file, AString & outDir )
{
    const char * lastSlash = file.FindLast( NATIVE_SLASH );
    TEST_ASSERT( lastSlash );
    outDir.Assign( file.Get(), lastSlash + 1 );
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

        AStackString<> file;
        GetCleanPchFile( "Reassembles", pch.m_Id, file );
        AStackString<> dir;
        GetDirOf( file, dir );

        PchReceiver receiver;
        bool alreadyCached = false;
        TEST_ASSERT( FeedAll( receiver, pch, file, alreadyCached ) == PchReceiver::COMPLETE );
        TEST_ASSERT( alreadyCached == false );
        TEST_ASSERT( receiver.GetNumChunksReceived() == Protocol::MsgPchFile::GetNumChunks( size, kChunkSize ) );

        Array<char> content;
        TEST_ASSERT( ReadFile( file, content ) );
        TEST_ASSERT( content.GetSize() == pch.m_Compressed.GetSize() );
        TEST_ASSERT( memcmp( content.Begin(), pch.m_Compressed.Begin(), content.GetSize() ) == 0 );

        // Only the PCH is left
        TEST_ASSERT( CountFilesIn( dir ) == 1 );
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

    AStackString<> file;
    GetCleanPchFile( "WrongId", pch.m_Id, file );
    AStackString<> dir;
    GetDirOf( file, dir );
    PchReceiver receiver;
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)pch.m_Compressed.GetSize(), kChunkSize );
    for ( uint32_t i = 0; i < numChunks; ++i )
    {
        bool alreadyCached = false;
        const PchReceiver::Result result = Feed( receiver, TestChunk( pch, kChunkSize, i ), file, alreadyCached );
        TEST_ASSERT( result == ( ( i + 1 ) == numChunks ? PchReceiver::FAILED : PchReceiver::CONTINUE ) );
    }
    TEST_ASSERT( CountFilesIn( dir ) == 0 );
}

// ReceiverRejectsAWrongSize
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiverRejectsAWrongSize() const
{
    // The data is kept as it came, so the size it is to come to is checked while it is received
    for ( int32_t difference = -1; difference <= 1; difference += 2 )
    {
        TestPch pch;
        MakePch( ( 3 * kChunkSize ) + 9, 110, pch );
        pch.m_UncompressedSize += (uint32_t)difference;

        AStackString<> file;
        GetCleanPchFile( "WrongSize2", pch.m_Id, file );
        AStackString<> dir;
        GetDirOf( file, dir );
        PchReceiver receiver;
        const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)pch.m_Compressed.GetSize(), kChunkSize );
        for ( uint32_t i = 0; i < numChunks; ++i )
        {
            bool alreadyCached = false;
            const PchReceiver::Result result = Feed( receiver, TestChunk( pch, kChunkSize, i ), file, alreadyCached );
            TEST_ASSERT( result == ( ( i + 1 ) == numChunks ? PchReceiver::FAILED : PchReceiver::CONTINUE ) );
        }
        TEST_ASSERT( CountFilesIn( dir ) == 0 );
    }
}

// ReceiverRejectsMalformedChunks
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiverRejectsMalformedChunks() const
{
    TestPch pch;
    MakePch( ( 4 * kChunkSize ) + 5, 200, pch );
    TestPch other;
    MakePch( ( 2 * kChunkSize ) + 5, 300, other );
    AStackString<> file;
    GetCleanPchFile( "Malformed", pch.m_Id, file );
    AStackString<> dir;
    GetDirOf( file, dir );
    AStackString<> otherFile;
    GetCleanPchFile( "Malformed", other.m_Id, otherFile );

    const uint32_t compressedSize = (uint32_t)pch.m_Compressed.GetSize();
    bool alreadyCached = false;

    // The first chunk is not the first
    {
        PchReceiver receiver;
        TEST_ASSERT( Feed( receiver, TestChunk( pch, kChunkSize, 1 ), file, alreadyCached ) == PchReceiver::FAILED );
    }

    // The first chunk twice
    {
        PchReceiver receiver;
        TEST_ASSERT( Feed( receiver, TestChunk( pch, kChunkSize, 0 ), file, alreadyCached ) == PchReceiver::CONTINUE );
        TEST_ASSERT( Feed( receiver, TestChunk( pch, kChunkSize, 0 ), file, alreadyCached ) == PchReceiver::FAILED );
    }

    // A chunk is missing
    {
        PchReceiver receiver;
        TEST_ASSERT( Feed( receiver, TestChunk( pch, kChunkSize, 0 ), file, alreadyCached ) == PchReceiver::CONTINUE );
        TEST_ASSERT( Feed( receiver, TestChunk( pch, kChunkSize, 2 ), file, alreadyCached ) == PchReceiver::FAILED );
    }

    // A chunk of the wrong size
    {
        PchReceiver receiver;
        TEST_ASSERT( Feed( receiver, TestChunk( pch, kChunkSize, 0 ), file, alreadyCached ) == PchReceiver::CONTINUE );
        const TestChunk chunk( pch, kChunkSize, 1 );
        TEST_ASSERT( receiver.OnChunk( chunk.m_Msg, chunk.m_Data, chunk.m_Size - 1, file, alreadyCached ) == PchReceiver::FAILED );
    }

    // A chunk that says the PCH has other sizes than the first did
    {
        PchReceiver receiver;
        TEST_ASSERT( Feed( receiver, TestChunk( pch, kChunkSize, 0 ), file, alreadyCached ) == PchReceiver::CONTINUE );
        const Protocol::MsgPchFile msg( pch.m_Id, pch.m_UncompressedSize, compressedSize + 1, kChunkSize, 1 );
        const TestChunk chunk( pch, kChunkSize, 1 );
        TEST_ASSERT( receiver.OnChunk( msg, chunk.m_Data, chunk.m_Size, file, alreadyCached ) == PchReceiver::FAILED );
    }

    // A PCH starts before the one before it is complete
    {
        PchReceiver receiver;
        TEST_ASSERT( Feed( receiver, TestChunk( pch, kChunkSize, 0 ), file, alreadyCached ) == PchReceiver::CONTINUE );
        TEST_ASSERT( Feed( receiver, TestChunk( other, kChunkSize, 0 ), otherFile, alreadyCached ) == PchReceiver::FAILED );
    }

    // Sizes that no sender should ask for
    {
        const TestChunk chunk( pch, kChunkSize, 0 );
        const Protocol::MsgPchFile tooBig( pch.m_Id, pch.m_UncompressedSize, ( 2 * pch.m_UncompressedSize ) + ( 128 * 1024 ), kChunkSize, 0 );
        const Protocol::MsgPchFile noData( pch.m_Id, pch.m_UncompressedSize, 0, kChunkSize, 0 );
        const Protocol::MsgPchFile noPch( pch.m_Id, 0, compressedSize, kChunkSize, 0 );
        const Protocol::MsgPchFile hugePch( pch.m_Id, PchReceiver::kMaxUncompressedSize + 1, compressedSize, kChunkSize, 0 );
        const Protocol::MsgPchFile tinyChunks( pch.m_Id, pch.m_UncompressedSize, compressedSize, Protocol::kMinPchChunkSize - 1, 0 );
        const Protocol::MsgPchFile hugeChunks( pch.m_Id, pch.m_UncompressedSize, compressedSize, Protocol::kMaxPchChunkSize + 1, 0 );
        const Protocol::MsgPchFile * const msgs[] = { &tooBig, &noData, &noPch, &hugePch, &tinyChunks, &hugeChunks };
        for ( const Protocol::MsgPchFile * msg : msgs )
        {
            PchReceiver receiver;
            TEST_ASSERT( receiver.OnChunk( *msg, chunk.m_Data, chunk.m_Size, file, alreadyCached ) == PchReceiver::FAILED );
        }
    }

    // Nothing was written by any of it
    TEST_ASSERT( CountFilesIn( dir ) == 0 );
}

// ReceiverAcceptsTheLimits
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiverAcceptsTheLimits() const
{
    // The largest chunk size a sender may ask for: a PCH of one chunk
    TestPch pch;
    MakePch( ( 2 * kChunkSize ) + 9, 700, pch );
    AStackString<> file;
    GetCleanPchFile( "Limits", pch.m_Id, file );

    PchReceiver receiver;
    bool alreadyCached = false;
    const Protocol::MsgPchFile msg( pch.m_Id, pch.m_UncompressedSize, (uint32_t)pch.m_Compressed.GetSize(), Protocol::kMaxPchChunkSize, 0 );
    TEST_ASSERT( receiver.OnChunk( msg, pch.m_Compressed.Begin(), pch.m_Compressed.GetSize(), file, alreadyCached ) == PchReceiver::COMPLETE );

    // The smallest is what the other tests use (kChunkSize)
    TEST_ASSERT( kChunkSize == Protocol::kMinPchChunkSize );
}

// ReceiverDropsAPartialPch
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiverDropsAPartialPch() const
{
    TestPch pch;
    MakePch( ( 4 * kChunkSize ) + 5, 400, pch );
    AStackString<> file;
    GetCleanPchFile( "Partial", pch.m_Id, file );
    AStackString<> dir;
    GetDirOf( file, dir );
    bool alreadyCached = false;

    // The connection ends with the PCH half received: nothing is written yet, and nothing registered
    {
        PchReceiver receiver;
        TEST_ASSERT( Feed( receiver, TestChunk( pch, kChunkSize, 0 ), file, alreadyCached ) == PchReceiver::CONTINUE );
        TEST_ASSERT( Feed( receiver, TestChunk( pch, kChunkSize, 1 ), file, alreadyCached ) == PchReceiver::CONTINUE );
    }
    TEST_ASSERT( CountFilesIn( dir ) == 0 );

    // A receiver that is told to drop it can take the PCH from the start
    {
        PchReceiver receiver;
        TEST_ASSERT( Feed( receiver, TestChunk( pch, kChunkSize, 0 ), file, alreadyCached ) == PchReceiver::CONTINUE );
        receiver.Abort();
        TEST_ASSERT( FeedAll( receiver, pch, file, alreadyCached ) == PchReceiver::COMPLETE );
        TEST_ASSERT( CountFilesIn( dir ) == 1 );
    }
}

// ReceiversOfTheSamePchDoNotMeet
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiversOfTheSamePchDoNotMeet() const
{
    // Two coordinators send the same PCH to a worker, and their chunks arrive alternately
    TestPch pch;
    MakePch( ( 5 * kChunkSize ) + 3, 500, pch );
    AStackString<> file;
    GetCleanPchFile( "SamePch", pch.m_Id, file );
    AStackString<> dir;
    GetDirOf( file, dir );

    PchReceiver a;
    PchReceiver b;
    bool cachedA = false;
    bool cachedB = false;
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)pch.m_Compressed.GetSize(), kChunkSize );
    PchReceiver::Result resultA = PchReceiver::CONTINUE;
    PchReceiver::Result resultB = PchReceiver::CONTINUE;
    for ( uint32_t i = 0; i < numChunks; ++i )
    {
        resultA = Feed( a, TestChunk( pch, kChunkSize, i ), file, cachedA );
        resultB = Feed( b, TestChunk( pch, kChunkSize, i ), file, cachedB );
    }

    TEST_ASSERT( resultA == PchReceiver::COMPLETE );
    TEST_ASSERT( resultB == PchReceiver::COMPLETE );
    TEST_ASSERT( cachedA == false );
    TEST_ASSERT( cachedB ); // The first one had it in place already

    Array<char> content;
    TEST_ASSERT( ReadFile( file, content ) );
    TEST_ASSERT( content.GetSize() == pch.m_Compressed.GetSize() );
    TEST_ASSERT( memcmp( content.Begin(), pch.m_Compressed.Begin(), content.GetSize() ) == 0 );
    TEST_ASSERT( CountFilesIn( dir ) == 1 ); // No temporary file is left
}

// ReceiverKeepsACachedPch
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiverKeepsACachedPch() const
{
    TestPch pch;
    MakePch( ( 3 * kChunkSize ) + 1, 600, pch );
    AStackString<> file;
    GetCleanPchFile( "Cached", pch.m_Id, file );
    AStackString<> dir;
    GetDirOf( file, dir );

    bool alreadyCached = false;
    {
        PchReceiver receiver;
        TEST_ASSERT( FeedAll( receiver, pch, file, alreadyCached ) == PchReceiver::COMPLETE );
    }
    TEST_ASSERT( CountFilesIn( dir ) == 1 );

    // Give it a time that nothing writing the file now would give it
#if defined( __WINDOWS__ )
    const uint64_t kHour = ( 3600ULL * 10000000ULL );
#else
    const uint64_t kHour = ( 3600ULL * 1000000000ULL );
#endif
    const uint64_t oldTime = ( Time::GetCurrentFileTime() - kHour );
    TEST_ASSERT( FileIO::SetFileLastWriteTime( file, oldTime ) );
    const uint64_t timeBefore = FileIO::GetFileLastWriteTime( file );
    Array<char> contentBefore;
    TEST_ASSERT( ReadFile( file, contentBefore ) );

    // Sent again, it is dropped as it arrives, and the file is not touched
    PchReceiver receiver;
    TEST_ASSERT( FeedAll( receiver, pch, file, alreadyCached ) == PchReceiver::COMPLETE );
    TEST_ASSERT( alreadyCached );
    TEST_ASSERT( CountFilesIn( dir ) == 1 );
    TEST_ASSERT( FileIO::GetFileLastWriteTime( file ) == timeBefore );
    Array<char> contentAfter;
    TEST_ASSERT( ReadFile( file, contentAfter ) );
    TEST_ASSERT( contentAfter.GetSize() == contentBefore.GetSize() );
    TEST_ASSERT( memcmp( contentAfter.Begin(), contentBefore.Begin(), contentAfter.GetSize() ) == 0 );
}

// ReceiverKeepsAStoredPchWhateverItsSize
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiverKeepsAStoredPchWhateverItsSize() const
{
    // The id identifies the content, not how it is compressed, and a file that is stored is not replaced:
    // a worker may be unpacking it. One that is not a PCH is found out when it is unpacked.
    TestPch pch;
    MakePch( ( 3 * kChunkSize ) + 5, 800, pch );
    AStackString<> file;
    GetCleanPchFile( "OtherSize", pch.m_Id, file );
    AStackString<> dir;
    GetDirOf( file, dir );
    TEST_ASSERT( FileIO::EnsurePathExistsForFile( file ) );
    const char stored[] = "stored in another way";
    {
        FileStream f;
        TEST_ASSERT( f.Open( file.Get(), FileStream::WRITE_ONLY ) );
        TEST_ASSERT( f.Write( stored, sizeof( stored ) ) == sizeof( stored ) );
    }

    PchReceiver receiver;
    bool alreadyCached = false;
    TEST_ASSERT( FeedAll( receiver, pch, file, alreadyCached ) == PchReceiver::COMPLETE );
    TEST_ASSERT( alreadyCached );
    Array<char> content;
    TEST_ASSERT( ReadFile( file, content ) );
    TEST_ASSERT( content.GetSize() == sizeof( stored ) );
    TEST_ASSERT( memcmp( content.Begin(), stored, sizeof( stored ) ) == 0 );
    TEST_ASSERT( CountFilesIn( dir ) == 1 );
}

// ReceiverCleansUpWhenItCannotPublish
//------------------------------------------------------------------------------
void TestPchTransfer::ReceiverCleansUpWhenItCannotPublish() const
{
    // The PCH is written in full before it is moved into place. When that fails the file has to be
    // removed, which needs it closed first: an open file cannot be deleted on Windows.
    TestPch pch;
    MakePch( ( 3 * kChunkSize ) + 5, 900, pch );
    AStackString<> file;
    GetCleanPchFile( "CannotPublish", pch.m_Id, file );
    AStackString<> dir;
    GetDirOf( file, dir );
    TEST_ASSERT( FileIO::EnsurePathExists( file ) ); // A directory where the PCH goes

    PchReceiver receiver;
    bool alreadyCached = false;
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( (uint32_t)pch.m_Compressed.GetSize(), kChunkSize );
    PchReceiver::Result result = PchReceiver::CONTINUE;
    for ( uint32_t i = 0; i < numChunks; ++i )
    {
        result = Feed( receiver, TestChunk( pch, kChunkSize, i ), file, alreadyCached );
    }
    TEST_ASSERT( result == PchReceiver::FAILED );

    Array<AString> left;
    FileIO::GetFiles( dir, AStackString( "incoming.*" ), true, &left );
    TEST_ASSERT( left.IsEmpty() );
}

// SendQueueSendsBulkDataLast
//------------------------------------------------------------------------------
namespace
{
    struct QueueItem
    {
        bool IsBulk() const { return m_Bulk; }

        uint32_t    m_Id;
        bool        m_Bulk;
    };
}

void TestPchTransfer::SendQueueSendsBulkDataLast() const
{
    PrioritySendQueue<QueueItem> queue;
    Array<QueueItem> items;

    queue.Take( items );
    TEST_ASSERT( items.IsEmpty() );

    queue.Push( QueueItem{ 1, false } );
    queue.Push( QueueItem{ 2, true } );
    queue.Push( QueueItem{ 3, true } );
    queue.Push( QueueItem{ 4, false } );

    // What is not bulk data goes first, in order, followed by one piece of bulk data
    queue.Take( items );
    TEST_ASSERT( items.GetSize() == 3 );
    TEST_ASSERT( items[ 0 ].m_Id == 1 );
    TEST_ASSERT( items[ 1 ].m_Id == 4 );
    TEST_ASSERT( items[ 2 ].m_Id == 2 );

    // A message queued while a transfer is under way goes before the rest of it
    queue.Push( QueueItem{ 5, false } );
    items.Clear();
    queue.Take( items );
    TEST_ASSERT( items.GetSize() == 2 );
    TEST_ASSERT( items[ 0 ].m_Id == 5 );
    TEST_ASSERT( items[ 1 ].m_Id == 3 );

    items.Clear();
    queue.Take( items );
    TEST_ASSERT( items.IsEmpty() );
}

// SendQueueDoesNotStarveBulkData
//------------------------------------------------------------------------------
void TestPchTransfer::SendQueueDoesNotStarveBulkData() const
{
    // Messages keep coming, as jobs do while a PCH is queued: the PCH still goes, a piece at a time
    PrioritySendQueue<QueueItem> queue;
    const uint32_t kPieces = 5;
    for ( uint32_t i = 0; i < kPieces; ++i )
    {
        queue.Push( QueueItem{ 100 + i, true } );
    }

    uint32_t nextPiece = 100;
    for ( uint32_t round = 0; round < kPieces; ++round )
    {
        queue.Push( QueueItem{ round, false } );
        Array<QueueItem> items;
        queue.Take( items );
        TEST_ASSERT( items.GetSize() == 2 );
        TEST_ASSERT( items[ 0 ].m_Id == round );
        TEST_ASSERT( items[ 1 ].m_Id == nextPiece++ );
    }

    // All of it went, in order
    TEST_ASSERT( nextPiece == ( 100 + kPieces ) );
    Array<QueueItem> items;
    queue.Take( items );
    TEST_ASSERT( items.IsEmpty() );
}

// BulkSendKinds
//------------------------------------------------------------------------------
void TestPchTransfer::BulkSendKinds() const
{
    // Only a PCH waits behind jobs. Jobs, and the messages the worker needs to run them, do not.
    TEST_ASSERT( IsBulkSendKind( SendKind::Pch ) );
    TEST_ASSERT( IsBulkSendKind( SendKind::Job ) == false );
    TEST_ASSERT( IsBulkSendKind( SendKind::Toolchain ) == false );
    TEST_ASSERT( IsBulkSendKind( SendKind::Control ) == false );
}

//------------------------------------------------------------------------------
