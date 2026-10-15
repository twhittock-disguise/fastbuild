// PchReceiver.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "PchReceiver.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"

// Core
#include "Core/Containers/UniquePtr.h"
#include "Core/FileIO/FileIO.h"
#include "Core/Math/xxHash.h"
#include "Core/Mem/Mem.h"
#include "Core/Process/Atomic.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Timer.h"

// Static
//------------------------------------------------------------------------------
namespace
{
    // Temporary files are named so that concurrent receivers of the same PCH do not meet
    void MakeTempFileName( AString & outFile, const AString & dir, const char * extension )
    {
        static Atomic<uint32_t> s_Counter{ 0 };
        outFile = dir;
        outFile.AppendFormat( "pch.%08x.%08x.%s", s_Counter.Increment(), (uint32_t)Timer::GetNow(), extension );
    }

    bool IsFileOfSize( const AString & file, uint32_t size )
    {
        FileStream f;
        return ( f.Open( file.Get() ) && ( f.GetFileSize() == size ) );
    }
}

// DESTRUCTOR
//------------------------------------------------------------------------------
PchReceiver::~PchReceiver()
{
    Abort();
}

// GetMaxCompressedSize
//------------------------------------------------------------------------------
/*static*/ uint64_t PchReceiver::GetMaxCompressedSize( uint32_t uncompressedSize )
{
    // Data that does not compress is stored with a small header
    return ( (uint64_t)uncompressedSize + ( uncompressedSize >> 7 ) + ( 64 * 1024 ) );
}

// OnChunk
//------------------------------------------------------------------------------
PchReceiver::Result PchReceiver::OnChunk( const Chunk & chunk,
                                          const AString & pchDir,
                                          AString & outPchFile,
                                          bool & outAlreadyCached )
{
    outAlreadyCached = false;

    if ( chunk.m_Index == 0 )
    {
        if ( m_Active )
        {
            return Fail( "a PCH started before the one before it was complete" );
        }
        if ( ( chunk.m_UncompressedSize == 0 ) ||
             ( chunk.m_CompressedSize == 0 ) ||
             ( chunk.m_CompressedSize > GetMaxCompressedSize( chunk.m_UncompressedSize ) ) ||
             ( chunk.m_ChunkSize < Protocol::kMinPchChunkSize ) ||
             ( chunk.m_ChunkSize > Protocol::kMaxPchChunkSize ) )
        {
            return Fail( "its sizes are not possible" );
        }

        m_Active = true;
        m_PchId = chunk.m_PchId;
        m_UncompressedSize = chunk.m_UncompressedSize;
        m_CompressedSize = chunk.m_CompressedSize;
        m_ChunkSize = chunk.m_ChunkSize;
        m_NextIndex = 0;

        // The id identifies the content, so a file of the right size is the PCH
        AStackString<> pchFile( pchDir );
        pchFile += "pch.pch";
        m_AlreadyCached = IsFileOfSize( pchFile, m_UncompressedSize );
        if ( m_AlreadyCached == false )
        {
            MakeTempFileName( m_ReceivingFile, pchDir, "zst" );
            if ( ( FileIO::EnsurePathExists( pchDir ) == false ) ||
                 ( m_File.Open( m_ReceivingFile.Get(), FileStream::WRITE_ONLY ) == false ) )
            {
                m_ReceivingFile.Clear();
                return Fail( "the cache directory cannot be written" );
            }
        }
    }
    else if ( ( m_Active == false ) ||
              ( chunk.m_PchId != m_PchId ) ||
              ( chunk.m_UncompressedSize != m_UncompressedSize ) ||
              ( chunk.m_CompressedSize != m_CompressedSize ) ||
              ( chunk.m_ChunkSize != m_ChunkSize ) ||
              ( chunk.m_Index != m_NextIndex ) )
    {
        return Fail( "a chunk is not the next one" );
    }

    if ( chunk.m_Size != Protocol::MsgPchFile::GetChunkSize( m_CompressedSize, m_ChunkSize, chunk.m_Index ) )
    {
        return Fail( "a chunk has the wrong size" );
    }

    if ( ( m_AlreadyCached == false ) &&
         ( m_File.Write( chunk.m_Data, chunk.m_Size ) != chunk.m_Size ) )
    {
        return Fail( "a chunk cannot be written" );
    }

    ++m_NextIndex;
    if ( m_NextIndex < Protocol::MsgPchFile::GetNumChunks( m_CompressedSize, m_ChunkSize ) )
    {
        return CONTINUE;
    }
    return Finish( pchDir, outPchFile, outAlreadyCached );
}

// Abort
//------------------------------------------------------------------------------
void PchReceiver::Abort()
{
    m_File.Close();
    if ( m_ReceivingFile.IsEmpty() == false )
    {
        FileIO::FileDelete( m_ReceivingFile.Get() );
        m_ReceivingFile.Clear();
    }
    if ( m_DecodedFile.IsEmpty() == false )
    {
        FileIO::FileDelete( m_DecodedFile.Get() );
        m_DecodedFile.Clear();
    }
    m_Active = false;
    m_AlreadyCached = false;
}

// Fail
//------------------------------------------------------------------------------
PchReceiver::Result PchReceiver::Fail( const char * reason )
{
    FLOG_WARN( "Rejected the PCH 0x%016" PRIx64 ": %s\n", m_PchId, reason );
    Abort();
    return FAILED;
}

// Finish
//------------------------------------------------------------------------------
PchReceiver::Result PchReceiver::Finish( const AString & pchDir, AString & outPchFile, bool & outAlreadyCached )
{
    m_File.Close();

    AStackString<> pchFile( pchDir );
    pchFile += "pch.pch";
    outPchFile = pchFile;

    if ( m_AlreadyCached )
    {
        outAlreadyCached = true;
        Abort();
        return COMPLETE;
    }

    Compressor decompressor;
    {
        UniquePtr<char, FreeDeletor> compressed( (char *)ALLOC( m_CompressedSize ) );
        FileStream f;
        if ( ( f.Open( m_ReceivingFile.Get() ) == false ) ||
             ( f.GetFileSize() != m_CompressedSize ) ||
             ( f.Read( compressed.Get(), m_CompressedSize ) != m_CompressedSize ) )
        {
            return Fail( "the data received cannot be read back" );
        }
        f.Close();
        FileIO::FileDelete( m_ReceivingFile.Get() );
        m_ReceivingFile.Clear();

        if ( ( Compressor::IsValidData( compressed.Get(), m_CompressedSize ) == false ) ||
             ( decompressor.Decompress( compressed.Get() ) == false ) )
        {
            return Fail( "its data cannot be decompressed" );
        }
    }

    if ( decompressor.GetResultSize() != m_UncompressedSize )
    {
        return Fail( "its size is not the one announced" );
    }
    if ( xxHash::Calc64( decompressor.GetResult(), decompressor.GetResultSize() ) != m_PchId )
    {
        return Fail( "its content does not match its id" );
    }

    MakeTempFileName( m_DecodedFile, pchDir, "pch" );
    {
        FileStream f;
        if ( ( f.Open( m_DecodedFile.Get(), FileStream::WRITE_ONLY ) == false ) ||
             ( f.Write( decompressor.GetResult(), decompressor.GetResultSize() ) != decompressor.GetResultSize() ) )
        {
            return Fail( "it cannot be written" );
        }
    }

    // Another connection may have finished the same PCH first, and a compile may be using it
    if ( IsFileOfSize( pchFile, m_UncompressedSize ) )
    {
        outAlreadyCached = true;
    }
    else if ( FileIO::FileMove( m_DecodedFile, pchFile ) == false )
    {
        if ( IsFileOfSize( pchFile, m_UncompressedSize ) == false )
        {
            return Fail( "it cannot be moved into place" );
        }
        outAlreadyCached = true;
    }

    Abort(); // Removes the temporary file if it was not moved
    return COMPLETE;
}

//------------------------------------------------------------------------------
