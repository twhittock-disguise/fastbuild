// PchReceiver.h - Reassembles a PCH sent by a coordinator in chunks
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"

#include "Core/Containers/Array.h"
#include "Core/Env/Types.h"
#include "Core/Process/Atomic.h"
#include "Core/Strings/AString.h"

// PchReceiver
//------------------------------------------------------------------------------
// One per coordinator connection. The chunks of a PCH are kept in memory as they arrive. The last
// one decompresses them, checks the size and the id (the PchId of the file) and moves the
// result into place, so a PCH file is only ever seen complete. The chunks of one PCH arrive in
// order and without another PCH in between.
class PchReceiver
{
public:
    PchReceiver() = default;
    ~PchReceiver();

    // Non-copyable
    explicit PchReceiver( const PchReceiver & other ) = delete;
    PchReceiver & operator=( const PchReceiver & other ) = delete;

    enum Result
    {
        CONTINUE,   // More chunks are expected
        COMPLETE,   // The PCH is in place
        FAILED,     // Not valid, and nothing is left on disk. The sender should be disconnected.
    };

    // pchFile is the path the PCH is kept at
    Result OnChunk( const Protocol::MsgPchFile & msg,
                    const void * data,
                    size_t size,
                    const AString & pchFile,
                    bool & outAlreadyCached );

    // Drops a PCH that was only partly received
    void Abort();

    uint32_t GetNumChunksReceived() const { return m_NumChunksReceived.Load(); }

    // Whether this PCH is arriving, or has completed and not yet been handed on (EndReceiving).
    // Safe to ask from another connection's thread.
    bool IsReceiving( uint64_t pchId ) const { return ( m_ReceivingPchId.Load() == pchId ); }

    // A COMPLETE PCH is where the jobs look for it now
    void EndReceiving() { m_ReceivingPchId.Store( 0 ); }

    // The temporary file a PCH is written to before it is moved into place, next to pchFile, is named
    // as TempFiles does
    inline static const char * const kTempFilePrefix = "incoming.";
    inline static const char * const kTempFileExtension = ".pch";

    // The largest PCH a sender may announce
    inline static const uint32_t kMaxUncompressedSize = ( 2u * 1024 * 1024 * 1024 );

private:
    Result Fail( uint64_t pchId, const char * reason );
    Result Finish( const AString & pchFile, bool & outAlreadyCached );
    void Reset(); // Abort, except that a completed PCH is still being received until EndReceiving

    bool            m_Active = false;
    bool            m_AlreadyCached = false; // The PCH was already on disk: the chunks are dropped
    uint64_t        m_PchId = 0;
    uint32_t        m_UncompressedSize = 0;
    uint32_t        m_CompressedSize = 0;
    uint32_t        m_ChunkSize = 0;
    uint32_t        m_NextIndex = 0;
    Array<char>     m_Compressed;   // As received, so far
    AString         m_TempFile;     // The decompressed PCH, until it is moved into place
    Atomic<uint32_t> m_NumChunksReceived{ 0 };
    Atomic<uint64_t> m_ReceivingPchId{ 0 }; // The PCH being received, or 0
};

//------------------------------------------------------------------------------
