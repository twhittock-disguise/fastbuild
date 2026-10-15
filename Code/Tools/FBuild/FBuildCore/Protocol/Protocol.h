// Protocol.h - Defines network communication protocol
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/MSVCStaticAnalysis.h"
#include "Core/Env/Types.h"

// Forward Declarations
//------------------------------------------------------------------------------
class ConnectionInfo;
class AString;
class ConstMemoryStream;
class MemoryStream;
class TCPConnectionPool;

// Defines
//------------------------------------------------------------------------------
//#define PROTOCOL_DEBUG_ENABLED // uncomment this for protocol spam
#ifdef PROTOCOL_DEBUG_ENABLED
    #include "Core/Tracing/Tracing.h"
    #define PROTOCOL_DEBUG( ... ) DEBUGSPAM( __VA_ARGS__ )
#else
    #define PROTOCOL_DEBUG( ... ) (void)0
#endif

// Protocol
//------------------------------------------------------------------------------
namespace Protocol
{
    inline static const uint16_t kPort = 31264; // Arbitrarily chosen port

    // Protocol Version
    //
    // Major: any change to the wire format or to what a message means. A worker and a client of
    //   different majors refuse each other at the handshake. The header of every message and the
    //   version fields of MsgConnection, MsgConnectionAck and MsgRefused keep their offsets in every
    //   major, so a mismatch can always be told apart and explained (see kHandshakePrefixSize).
    // Minor: additive changes that an older peer can safely not know about.
    // Capabilities: optional features negotiated for each connection. Each side advertises its
    //   capabilities in the handshake and uses a feature only if the other advertised it, and never
    //   sends a message type the other did not advertise. A receiver ignores the trailing bytes of a
    //   message it knows and the capability bits it does not know.
    // The build database version (NodeGraph kCurrentVersion) is bumped for every change to a reflected
    //   type. After merging upstream it is set to max(upstream, ours) + 1 so versions can not collide.
    inline static const uint32_t kVersionMajor = 24;
    inline static const uint8_t kVersionMinor = 0;

    // Optional features of a connection. A feature is in use when both sides advertise its bit. Every
    // 24.0 peer has all of them: the bits exist so that a later version can drop or skip one without a
    // new major. Without the bit the feature's messages are not sent, and not waited for.
    enum Capability : uint32_t
    {
        CAP_PCH_INVENTORY = ( 1u << 0 ),        // The worker sends MSG_PCH_INVENTORY after the ack
        CAP_TOOLCHAIN_INVENTORY = ( 1u << 1 ),  // The worker sends MSG_TOOLCHAIN_INVENTORY after the PCH inventory
        CAP_PCH_CHUNKS = ( 1u << 2 ),           // The client sends PCHs as MSG_PCH_FILE chunks
        CAP_JOB_STREAM = ( 1u << 3 ),           // Jobs are compressed as one stream for the connection
        CAP_RESULT_COMPRESSION = ( 1u << 4 ),   // Results come back as MSG_JOB_RESULT_COMPRESSED
    };
    inline static const uint32_t kCapabilities = ( CAP_PCH_INVENTORY | CAP_TOOLCHAIN_INVENTORY | CAP_PCH_CHUNKS | CAP_JOB_STREAM | CAP_RESULT_COMPRESSION );

    inline static const uint16_t kTestPort = kPort + 1; // Different port for use by tests

    // Send buffer of a client's connection to a worker: sized to cover 300 Mbit/s at 50 ms. What
    // is in it goes out ahead of a message sent later. (Linux doubles the value and clamps it to wmem_max.)
    inline static const uint32_t kClientSendBufferSize = ( 2 * 1024 * 1024 );

    // Identifiers for all unique messages
    //------------------------------------------------------------------------------
    enum MessageType : uint8_t
    {
        MSG_CONNECTION = 1,             // Client -> Worker : Initial handshake
        MSG_JOB = 2,                    // Client -> Worker : Push a job to execute
        MSG_JOB_RESULT = 3,             // Worker -> Client : Return completed job (uncompressed)
        MSG_JOB_RESULT_COMPRESSED = 4,  // Worker -> Client : Return completed job (compressed)
        MSG_MANIFEST = 5,               // Client -> Worker : Push tool manifest
        MSG_FILE = 6,                   // Client -> Worker : Push a tool file
        MSG_CONNECTION_ACK = 7,         // Worker -> Client : Handshake ack with capacity
        MSG_PCH_FILE = 8,              // Client -> Worker : Push a chunk of a PCH file
        MSG_PCH_INVENTORY = 9,         // Worker -> Client : Advertise cached PCH IDs
        MSG_REQUEST_MANIFEST = 10,      // Worker -> Client : Request manifest (disconnect recovery)
        MSG_REQUEST_FILE = 11,          // Worker -> Client : Request file (disconnect recovery)
        MSG_TOOLCHAIN_INVENTORY = 12,   // Worker -> Client : Advertise cached toolchain IDs
        MSG_REFUSED = 13,               // Worker -> Client : Handshake refused, with the version of the worker

        NUM_MESSAGES            // leave last
    };
}

#ifdef PROTOCOL_DEBUG_ENABLED
const char * GetProtocolMessageDebugName( Protocol::MessageType msgType );
#endif

namespace Protocol
{
    // base class for all messages
    //------------------------------------------------------------------------------
    class IMessage
    {
    public:
        bool Send( const ConnectionInfo * connection ) const;
        bool Send( const ConnectionInfo * connection, const MemoryStream & payload ) const;
        bool Send( const ConnectionInfo * connection, const ConstMemoryStream & payload ) const;
        bool Broadcast( TCPConnectionPool * pool ) const;

        MessageType GetType() const { return m_MsgType; }
        uint32_t GetSize() const { return m_MsgSize; }
        bool HasPayload() const { return m_HasPayload; }

    protected:
        IMessage( MessageType msgType, uint8_t msgSize, bool hasPayload );

        // properties common to all messages
        MessageType m_MsgType;
        uint8_t m_MsgSize;
        bool m_HasPayload;
        char m_Padding1[ 1 ];
    };
    static_assert( sizeof( IMessage ) == 3 + 1 /*padding*/, "Message base class has incorrect size" );

    // Handshake messages. Everything up to this many bytes is the header and the version fields, which
    // keep their layout in every major version. What follows belongs to the major, so a frame of
    // another major is only read this far.
    inline static const uint32_t kHandshakePrefixSize = 8;
    inline static const uint32_t kConnectionVersionOffset = 4;      // MsgConnection : uint32_t major
    inline static const uint32_t kAckWorkerVersionOffset = 4;       // MsgConnectionAck, MsgRefused : uint16_t
    inline static const uint32_t kAckProtocolMajorOffset = 6;       // MsgConnectionAck, MsgRefused : uint8_t
    inline static const uint32_t kAckProtocolMinorOffset = 7;       // MsgConnectionAck, MsgRefused : uint8_t

    // MsgConnection
    //------------------------------------------------------------------------------
    class MsgConnection : public IMessage
    {
    public:
        MsgConnection();

        uint32_t GetProtocolVersion() const { return m_ProtocolVersion; }
        uint8_t GetPlatform() const { return m_Platform; }
        uint8_t GetProtocolVersionMinor() const { return m_ProtocolVersionMinor; }
        const char * GetHostName() const { return m_HostName; }
        uint32_t GetCapabilities() const { return m_Capabilities; }

    private:
        uint32_t m_ProtocolVersion;
        uint8_t m_Platform;
        uint8_t m_ProtocolVersionMinor;
        uint8_t m_Padding2[ 2 ];
        char m_HostName[ 64 ];
        uint32_t m_Capabilities;
    };
    static_assert( sizeof( MsgConnection ) == sizeof( IMessage ) + 76, "MsgConnection message has incorrect size" );

    // MsgConnectionAck
    //------------------------------------------------------------------------------
    class MsgConnectionAck : public IMessage
    {
    public:
        explicit MsgConnectionAck( uint8_t capacity, uint32_t capabilities = kCapabilities );

        uint16_t GetWorkerVersion() const { return m_WorkerVersion; }
        uint8_t GetProtocolVersionMajor() const { return m_ProtocolVersionMajor; }
        uint8_t GetProtocolVersionMinor() const { return m_ProtocolVersionMinor; }
        uint8_t GetWorkerCapacity() const { return m_WorkerCapacity; }
        uint32_t GetCapabilities() const { return m_Capabilities; }

    private:
        uint16_t m_WorkerVersion;
        uint8_t m_ProtocolVersionMajor;
        uint8_t m_ProtocolVersionMinor;
        uint8_t m_WorkerCapacity;
        char m_Padding2[ 3 ];
        uint32_t m_Capabilities;
    };
    static_assert( sizeof( MsgConnectionAck ) == sizeof( IMessage ) + 12, "MsgConnectionAck message has incorrect size" );

    // MsgRefused
    //------------------------------------------------------------------------------
    // Sent instead of the ack to a client of another major version, with the version of the worker
    class MsgRefused : public IMessage
    {
    public:
        MsgRefused();

        uint16_t GetWorkerVersion() const { return m_WorkerVersion; }
        uint8_t GetProtocolVersionMajor() const { return m_ProtocolVersionMajor; }
        uint8_t GetProtocolVersionMinor() const { return m_ProtocolVersionMinor; }

    private:
        uint16_t m_WorkerVersion;
        uint8_t m_ProtocolVersionMajor;
        uint8_t m_ProtocolVersionMinor;
    };
    static_assert( sizeof( MsgRefused ) == sizeof( IMessage ) + 4, "MsgRefused message has incorrect size" );

    // The smallest frame a message of this type can arrive in. Handshake frames need only their version
    // fields. 0 for a type that is not known.
    uint32_t GetMinMessageSize( MessageType type );

    // True if the frame is long enough for its type, so its handler can read it
    bool IsFrameLongEnough( const void * frame, uint32_t size );

    // "protocol v24.0"
    void GetVersionDescription( AString & outText );

    // MsgJob
    //------------------------------------------------------------------------------
    class MsgJob : public IMessage
    {
    public:
        explicit MsgJob( uint64_t toolId, int16_t resultCompressionLevel );

        uint64_t GetToolId() const { return m_ToolId; }
        int16_t GetResultCompressionLevel() const { return m_ResultCompressionLevel; }

    private:
        int16_t m_ResultCompressionLevel;
        char m_Padding2[ 2 ];
        uint64_t m_ToolId;
    };
    static_assert( sizeof( MsgJob ) == sizeof( IMessage ) + 4 /*alignment*/ + 8, "MsgJob message has incorrect size" );

    // MsgJobResult
    //------------------------------------------------------------------------------
    class MsgJobResult : public IMessage
    {
    public:
        explicit MsgJobResult( uint8_t remainingCapacity );

        uint8_t GetRemainingCapacity() const { return m_RemainingCapacity; }

    private:
        uint8_t m_RemainingCapacity;
    };
    static_assert( sizeof( MsgJobResult ) == sizeof( IMessage ) + 1, "MsgJobResult message has incorrect size" );

    // MsgJobResultCompressed
    //------------------------------------------------------------------------------
    class MsgJobResultCompressed : public IMessage
    {
    public:
        explicit MsgJobResultCompressed( uint8_t remainingCapacity );

        uint8_t GetRemainingCapacity() const { return m_RemainingCapacity; }

    private:
        uint8_t m_RemainingCapacity;
    };
    static_assert( sizeof( MsgJobResultCompressed ) == sizeof( IMessage ) + 1, "MsgJobResultCompressed message has incorrect size" );

    // MsgRequestManifest
    //------------------------------------------------------------------------------
    class MsgRequestManifest : public IMessage
    {
    public:
        explicit MsgRequestManifest( uint64_t toolId );

        uint64_t GetToolId() const { return m_ToolId; }

    private:
        char m_Padding2[ 4 ];
        uint64_t m_ToolId;
    };
    static_assert( sizeof( MsgRequestManifest ) == sizeof( IMessage ) + 4 /*alignment*/ + 8, "MsgRequestManifest message has incorrect size" );

    // MsgManifest
    //------------------------------------------------------------------------------
    class MsgManifest : public IMessage
    {
    public:
        explicit MsgManifest( uint64_t toolId );

        uint64_t GetToolId() const { return m_ToolId; }

    private:
        char m_Padding2[ 4 ];
        uint64_t m_ToolId;
    };
    static_assert( sizeof( MsgManifest ) == sizeof( IMessage ) + 4 /*alignment*/ + 8, "MsgManifest message has incorrect size" );

    // MsgRequestFile
    //------------------------------------------------------------------------------
    class MsgRequestFile : public IMessage
    {
    public:
        MsgRequestFile( uint64_t toolId, uint32_t fileId );

        uint64_t GetToolId() const { return m_ToolId; }
        uint32_t GetFileId() const { return m_FileId; }

    private:
        uint32_t m_FileId;
        uint64_t m_ToolId;
    };
    static_assert( sizeof( MsgRequestFile ) == sizeof( IMessage ) + 12, "MsgRequestFile message has incorrect size" );

    // MsgFile
    //------------------------------------------------------------------------------
    class MsgFile : public IMessage
    {
    public:
        MsgFile( uint64_t toolId, uint32_t fileId );

        uint64_t GetToolId() const { return m_ToolId; }
        uint32_t GetFileId() const { return m_FileId; }

    private:
        uint32_t m_FileId;
        uint64_t m_ToolId;
    };
    static_assert( sizeof( MsgFile ) == sizeof( IMessage ) + 12, "MsgFile message has incorrect size" );

    // MsgPchFile
    //------------------------------------------------------------------------------
    // A PCH is sent as a zstd compressed blob cut into chunks (the last holds the rest), each in a
    // message of its own so jobs can be sent between them. Every chunk of a PCH carries the same
    // sizes and chunk size.
    inline static const uint32_t kPchChunkSize = ( 1024 * 1024 );
    inline static const uint32_t kMinPchChunkSize = ( 16 * 1024 ); // Bounds what a sender can ask a worker to handle
    inline static const uint32_t kMaxPchChunkSize = ( 16 * 1024 * 1024 );

    class MsgPchFile : public IMessage
    {
    public:
        MsgPchFile( uint64_t pchId, uint32_t uncompressedSize, uint32_t compressedSize, uint32_t chunkSize, uint32_t chunkIndex );

        uint64_t GetPchId() const { return m_PchId; }
        uint32_t GetUncompressedSize() const { return m_UncompressedSize; }
        uint32_t GetCompressedSize() const { return m_CompressedSize; }
        uint32_t GetChunkSize() const { return m_ChunkSize; }
        uint32_t GetChunkIndex() const { return m_ChunkIndex; }

        static uint32_t GetNumChunks( uint32_t compressedSize, uint32_t chunkSize );
        static uint32_t GetChunkLength( uint32_t compressedSize, uint32_t chunkSize, uint32_t chunkIndex ); // 0 if past the end

    private:
        uint64_t m_PchId;
        uint32_t m_UncompressedSize;
        uint32_t m_CompressedSize;
        uint32_t m_ChunkSize;
        uint32_t m_ChunkIndex;
    };
    static_assert( sizeof( MsgPchFile ) == sizeof( IMessage ) + 28, "MsgPchFile message has incorrect size" );

    // MsgPchInventory
    //------------------------------------------------------------------------------
    class MsgPchInventory : public IMessage
    {
    public:
        explicit MsgPchInventory( uint32_t numEntries );

        uint32_t GetNumEntries() const { return m_NumEntries; }

    private:
        uint32_t m_NumEntries;
    };
    static_assert( sizeof( MsgPchInventory ) == sizeof( IMessage ) + 4, "MsgPchInventory message has incorrect size" );

    // MsgToolchainInventory
    //------------------------------------------------------------------------------
    class MsgToolchainInventory : public IMessage
    {
    public:
        explicit MsgToolchainInventory( uint32_t numEntries );

        uint32_t GetNumEntries() const { return m_NumEntries; }

    private:
        uint32_t m_NumEntries;
    };
    static_assert( sizeof( MsgToolchainInventory ) == sizeof( IMessage ) + 4, "MsgToolchainInventory message has incorrect size" );
}

//------------------------------------------------------------------------------
