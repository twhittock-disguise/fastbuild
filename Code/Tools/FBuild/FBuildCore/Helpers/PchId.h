// PchId.h - The identity of a PCH: a hash of the whole .pch file
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"
#include "Core/Math/xxHash.h"
#include "Core/Strings/AString.h"

// PchId
//------------------------------------------------------------------------------
// The client computes an id as it reads the file, and a worker checks the one it received against
// the data it was sent, so both must use these functions.
class PchId
{
public:
    // A file is read in pieces of this size, so hashing it never holds the whole file
    static constexpr uint32_t kReadChunkSize = ( 1024 * 1024 );

    // Hash in pieces
    using Accumulator = xxHash3Accumulator;
    static void AddData( Accumulator & accumulator, const void * data, size_t dataSize ) { accumulator.AddDataBig( data, dataSize ); }
    static uint64_t Finalize( Accumulator & accumulator ) { return accumulator.Finalize64(); }

    // Hash in one go
    static uint64_t Calc( const void * data, size_t dataSize ) { return xxHash3::Calc64Big( data, dataSize ); }

    // Hash a file without reading it all at once
    static bool CalcFile( const AString & path, uint64_t & outId, uint64_t & outSize );

    static uint32_t GetNumFilesHashed(); // For tests: how many files CalcFile was asked to read
};

//------------------------------------------------------------------------------
