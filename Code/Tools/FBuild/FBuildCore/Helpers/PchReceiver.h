// PchReceiver.h - Reassembles a PCH sent by a coordinator in chunks
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Strings/AString.h"

// PchReceiver
//------------------------------------------------------------------------------
// One per coordinator connection. The chunks of a PCH are written to a temporary file as they
// arrive. The last one decompresses it, checks the size and the id (the xxHash64 of the file)
// and moves the result into place, so a PCH file is only ever seen complete. The chunks of one
// PCH arrive in order and without another PCH in between.
class PchReceiver
{
public:
    PchReceiver() = default;
    ~PchReceiver();

    // Non-copyable
    explicit PchReceiver( const PchReceiver & other ) = delete;
    PchReceiver & operator=( const PchReceiver & other ) = delete;

    struct Chunk
    {
        uint64_t        m_PchId = 0;
        uint32_t        m_UncompressedSize = 0;
        uint32_t        m_CompressedSize = 0;
        uint32_t        m_ChunkSize = 0;
        uint32_t        m_Index = 0;
        const void *    m_Data = nullptr;
        size_t          m_Size = 0;
    };

    enum Result
    {
        CONTINUE,   // More chunks are expected
        COMPLETE,   // The PCH is in place
        FAILED,     // Not valid, and nothing is left on disk. The sender should be disconnected.
    };

    // pchDir is the directory the PCH is kept in, ending in a slash
    Result OnChunk( const Chunk & chunk,
                    const AString & pchDir,
                    AString & outPchFile,
                    bool & outAlreadyCached );

    // Drops a PCH that was only partly received
    void Abort();

    // The most a compressed PCH can be larger than the PCH, so a sender cannot make us store more
    static uint64_t GetMaxCompressedSize( uint32_t uncompressedSize );

private:
    Result Fail( const char * reason );
    Result Finish( const AString & pchDir, AString & outPchFile, bool & outAlreadyCached );

    bool        m_Active = false;
    bool        m_AlreadyCached = false; // The PCH was already on disk: the chunks are read and dropped
    uint64_t    m_PchId = 0;
    uint32_t    m_UncompressedSize = 0;
    uint32_t    m_CompressedSize = 0;
    uint32_t    m_ChunkSize = 0;
    uint32_t    m_NextIndex = 0;
    AString     m_ReceivingFile; // Compressed data, as received
    AString     m_DecodedFile;   // Decompressed data, until it is moved into place
    FileStream  m_File;
};

//------------------------------------------------------------------------------
