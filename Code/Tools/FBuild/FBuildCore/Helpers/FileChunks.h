// FileChunks.h - Reads a file in pieces
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/UniquePtr.h"
#include "Core/Env/Types.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Math/Conversions.h"
#include "Core/Mem/Mem.h"
#include "Core/Process/Atomic.h"

// FileChunks
//------------------------------------------------------------------------------
class FileChunks
{
public:
    // A file is read in pieces of this size, so a large file is never held whole
    static constexpr uint32_t kChunkSize = ( 1024 * 1024 );

    // Calls onPiece( data, size ) for each piece of a file expected to be fileSize bytes long, in order.
    // Stops and returns false if onPiece returns false, the abort flag is set, the file is not the size
    // it was expected to be, or it cannot be read.
    template <class PIECE_FUNC>
    [[nodiscard]] static bool ForEachPiece( FileStream & file, uint64_t fileSize, const Atomic<bool> * abort, PIECE_FUNC && onPiece )
    {
        UniquePtr<char, FreeDeletor> buffer( static_cast<char *>( ALLOC( kChunkSize ) ) );
        for ( uint64_t done = 0; done < fileSize; )
        {
            if ( abort && abort->Load() )
            {
                return false;
            }

            // FileStream::Read does not stop at the end of a file on Windows: it waits for data that never
            // comes, so a file that shrank is looked for before every read
            if ( file.GetFileSize() != fileSize )
            {
                return false;
            }
            const uint32_t piece = static_cast<uint32_t>( Math::Min<uint64_t>( ( fileSize - done ), kChunkSize ) );
            if ( ( file.Read( buffer.Get(), piece ) != piece ) || ( onPiece( buffer.Get(), piece ) == false ) )
            {
                return false;
            }
            done += piece;
        }
        return true;
    }
};

//------------------------------------------------------------------------------
