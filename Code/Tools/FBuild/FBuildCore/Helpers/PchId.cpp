// PchId.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "PchId.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/Helpers/FileChunks.h"

// Core
#include "Core/FileIO/FileStream.h"

// CalcFile
//------------------------------------------------------------------------------
/*static*/ bool PchId::CalcFile( const AString & path, uint64_t & outId, uint64_t & outSize )
{
    FileStream file;
    if ( file.Open( path.Get(), FileStream::READ_ONLY ) == false )
    {
        return false;
    }
    const uint64_t fileSize = file.GetFileSize();

    Accumulator accumulator;
    if ( FileChunks::ForEachPiece( file, fileSize, nullptr, [ & ]( const void * data, uint32_t size )
    {
        AddData( accumulator, data, size );
        return true;
    } ) == false )
    {
        return false;
    }

    outId = Finalize( accumulator );
    outSize = fileSize;
    return true;
}

//------------------------------------------------------------------------------
