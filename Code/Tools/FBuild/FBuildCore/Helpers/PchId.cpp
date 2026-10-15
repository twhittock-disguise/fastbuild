// PchId.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "PchId.h"

// Core
#include "Core/Containers/UniquePtr.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Math/Conversions.h"
#include "Core/Mem/Mem.h"
#include "Core/Process/Atomic.h"

// Static
//------------------------------------------------------------------------------
namespace
{
    Atomic<uint32_t> s_NumFilesHashed{ 0 };
}

// CalcFile
//------------------------------------------------------------------------------
/*static*/ bool PchId::CalcFile( const AString & path, uint64_t & outId, uint64_t & outSize )
{
    s_NumFilesHashed.Increment();

    FileStream file;
    if ( file.Open( path.Get(), FileStream::READ_ONLY ) == false )
    {
        return false;
    }
    const uint64_t fileSize = file.GetFileSize();

    UniquePtr<char, FreeDeletor> buffer( static_cast<char *>( ALLOC( kReadChunkSize ) ) );
    Accumulator accumulator;
    for ( uint64_t remaining = fileSize; remaining > 0; )
    {
        // Reading past the end of a file that shrank does not stop on Windows
        if ( file.GetFileSize() < fileSize )
        {
            return false;
        }
        const uint32_t chunk = static_cast<uint32_t>( Math::Min<uint64_t>( remaining, kReadChunkSize ) );
        if ( file.Read( buffer.Get(), chunk ) != chunk )
        {
            return false;
        }
        AddData( accumulator, buffer.Get(), chunk );
        remaining -= chunk;
    }

    outId = Finalize( accumulator );
    outSize = fileSize;
    return true;
}

// GetNumFilesHashed
//------------------------------------------------------------------------------
/*static*/ uint32_t PchId::GetNumFilesHashed()
{
    return s_NumFilesHashed.Load();
}

//------------------------------------------------------------------------------
