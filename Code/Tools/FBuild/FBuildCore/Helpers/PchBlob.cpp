// PchBlob.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "PchBlob.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/Helpers/PchId.h"
#include "Tools/FBuild/FBuildCore/Helpers/StreamDecompressor.h"

// Core
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Process/Atomic.h"

// Static
//------------------------------------------------------------------------------
namespace
{
    // Hashes what is decompressed, and writes it if it has somewhere to go
    PRAGMA_DISABLE_PUSH_MSVC( 4324 ) // structure was padded due to alignment specifier
    class PchSink : public StreamDecompressor::Sink
    {
    public:
        explicit PchSink( FileStream * file )
            : m_File( file )
        {
        }

        virtual bool OnPiece( const void * data, size_t size ) override
        {
            PchId::AddData( m_Hash, data, size );
            if ( m_File && ( m_File->Write( data, size ) != size ) )
            {
                m_WriteFailed = true;
                return false;
            }
            return true;
        }

        uint64_t GetId() { return PchId::Finalize( m_Hash ); }
        bool HasWriteFailed() const { return m_WriteFailed; }

    private:
        FileStream *        m_File;
        bool                m_WriteFailed = false;
        PchId::Accumulator  m_Hash;
    };
    PRAGMA_DISABLE_POP_MSVC // 4324
}

// Verify
//------------------------------------------------------------------------------
/*static*/ bool PchBlob::Verify( const void * blob, size_t blobSize, uint64_t pchId, uint32_t uncompressedSize )
{
    PchSink sink( nullptr );
    uint32_t size = 0;
    return ( StreamDecompressor::DecompressMemory( blob, blobSize, sink, size ) &&
             ( size == uncompressedSize ) &&
             ( sink.GetId() == pchId ) );
}

// Unpack
//------------------------------------------------------------------------------
/*static*/ PchBlob::UnpackResult PchBlob::Unpack( const AString & storedFile,
                                                  uint64_t pchId,
                                                  const AString & tempFile,
                                                  const AString & rawFile,
                                                  const Atomic<bool> * abort,
                                                  uint64_t & outSize,
                                                  FileStream * writer )
{
    if ( FileIO::EnsurePathExistsForFile( rawFile ) == false )
    {
        return UNPACK_CANNOT_WRITE;
    }

    // The file is closed before anything can fail: an open file cannot be deleted on Windows
    UnpackResult result = UNPACK_OK;
    uint32_t size = 0;
    {
        FileStream plainFile;
        FileStream & file = writer ? *writer : plainFile;
        if ( file.Open( tempFile.Get(), FileStream::WRITE_ONLY ) == false )
        {
            return UNPACK_CANNOT_WRITE;
        }
        PchSink sink( &file );
        if ( StreamDecompressor::DecompressFile( storedFile.Get(), abort, sink, size ) == false )
        {
            if ( abort && abort->Load() )
            {
                result = UNPACK_ABORTED;
            }
            else
            {
                result = sink.HasWriteFailed() ? UNPACK_CANNOT_WRITE : UNPACK_CORRUPT;
            }
        }
        else if ( sink.GetId() != pchId )
        {
            result = UNPACK_CORRUPT;
        }
        file.Close(); // A writer from outside outlives this scope
    }

    if ( ( result == UNPACK_OK ) && ( FileIO::FileMove( tempFile, rawFile ) == false ) )
    {
        result = UNPACK_CANNOT_WRITE;
    }
    if ( result != UNPACK_OK )
    {
        FileIO::FileDelete( tempFile.Get() );
        return result;
    }
    outSize = size;
    return UNPACK_OK;
}

//------------------------------------------------------------------------------
