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
    inline static const uint32_t kVersionMajor = 23; // Changes here make workers incompatible
    inline static const uint8_t kVersionMinor = 6; // Changes must be forwards and backwards compatible

    inline static const uint16_t kTestPort = kPort + 1; // Different port for use by tests

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
        MSG_PCH_FILE = 8,              // Client -> Worker : Push a PCH file
        MSG_PCH_INVENTORY = 9,         // Worker -> Client : Advertise cached PCH IDs
        MSG_REQUEST_MANIFEST = 10,      // Worker -> Client : Request manifest (disconnect recovery)
        MSG_REQUEST_FILE = 11,          // Worker -> Client : Request file (disconnect recovery)

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

    // MsgConnection
    //------------------------------------------------------------------------------
    class MsgConnection : public IMessage
    {
    public:
        MsgConnection();

        uint32_t GetProtocolVersion() const { return m_ProtocolVersion; }
        uint8_t GetPlatform() const { return m_Platform; }
        const char * GetHostName() const { return m_HostName; }
        uint8_t GetProtocolVersionMinor() const { return m_ProtocolVersionMinor; }

    private:
        uint32_t m_ProtocolVersion;
        uint8_t m_Platform;
        uint8_t m_ProtocolVersionMinor;
        uint8_t m_Padding2[ 2 ];
        char m_HostName[ 64 ];
    };
    static_assert( sizeof( MsgConnection ) == sizeof( IMessage ) + 72, "MsgConnection message has incorrect size" );

    // MsgConnectionAck
    //------------------------------------------------------------------------------
    class MsgConnectionAck : public IMessage
    {
    public:
        explicit MsgConnectionAck( uint8_t capacity );

        uint16_t GetWorkerVersion() const { return m_WorkerVersion; }
        uint8_t GetProtocolVersionMajor() const { return m_ProtocolVersionMajor; }
        uint8_t GetProtocolVersionMinor() const { return m_ProtocolVersionMinor; }
        uint8_t GetWorkerCapacity() const { return m_WorkerCapacity; }

    private:
        uint16_t m_WorkerVersion;
        uint8_t m_ProtocolVersionMajor;
        uint8_t m_ProtocolVersionMinor;
        uint8_t m_WorkerCapacity;
        char m_Padding2[ 3 ];
    };
    static_assert( sizeof( MsgConnectionAck ) == sizeof( IMessage ) + 8, "MsgConnectionAck message has incorrect size" );

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
    class MsgPchFile : public IMessage
    {
    public:
        MsgPchFile( uint64_t pchId, uint32_t uncompressedSize );

        uint64_t GetPchId() const { return m_PchId; }
        uint32_t GetUncompressedSize() const { return m_UncompressedSize; }

    private:
        uint64_t m_PchId;
        uint32_t m_UncompressedSize;
    };
    static_assert( sizeof( MsgPchFile ) == sizeof( IMessage ) + 20, "MsgPchFile message has incorrect size" );

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
}

//------------------------------------------------------------------------------
