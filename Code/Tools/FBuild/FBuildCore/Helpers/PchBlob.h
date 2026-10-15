// PchBlob.h - A PCH as a worker stores it: compressed, and unpacked when a job needs it
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"
#include "Core/Strings/AString.h"

// Forward Declarations
//------------------------------------------------------------------------------
template <class T> class Atomic;

// PchBlob
//------------------------------------------------------------------------------
// The compressed form is the data that came over the wire (see Compressor), kept as it is. The
// extension names the codec, so another way of storing a PCH can be told from this one by its file.
class PchBlob
{
public:
    // The files of a PCH, in its directory
    inline static const char * const kStoredFileName = "pch.zst";   // Compressed: what the cache holds
    inline static const char * const kRawFileName = "pch.pch";      // A working copy for the compiler

    // Is the blob (data in the format of Compressor) the PCH with this id and size?
    [[nodiscard]] static bool Verify( const void * blob, size_t blobSize, uint64_t pchId, uint32_t uncompressedSize );

    enum UnpackResult
    {
        UNPACK_OK,
        UNPACK_CORRUPT,         // The stored file is missing, not valid, or not the PCH with this id
        UNPACK_CANNOT_WRITE,    // The raw copy could not be written (no room, no access)
        UNPACK_ABORTED,         // The abort flag was set
    };

    // Decompresses the stored file to rawFile, a piece at a time, and checks it is the PCH with this id. It
    // is written to tempFile first, so rawFile is only ever seen whole. Nothing is left behind on failure.
    [[nodiscard]] static UnpackResult Unpack( const AString & storedFile,
                                              uint64_t pchId,
                                              const AString & tempFile,
                                              const AString & rawFile,
                                              const Atomic<bool> * abort,
                                              uint64_t & outSize );
};

//------------------------------------------------------------------------------
