// Protocol.cpp - Defines network communication protocol
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Protocol.h"

// Tools/FBuild
#include "Tools/FBuild/FBuildCore/FBuildVersion.h"

// Core
#include "Core/Env/Env.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/Network/TCPConnectionPool.h"
#include "Core/Strings/AString.h"

// system
#include <memory.h> // for memset
#include <stddef.h> // for offsetof
#if defined( __APPLE__ ) || defined( __LINUX__ )
    #include <unistd.h> // for ::gethostname
#endif

// Message Debug
//------------------------------------------------------------------------------

#ifdef PROTOCOL_DEBUG_ENABLED
const char * GetProtocolMessageDebugName( Protocol::MessageType msgType )
{
    // clang-format off
    const char * const msgNames[] =
    {
        "",
        "Connection",
        "Job",
        "JobResult",
        "JobResultCompressed",
        "Manifest",
        "File",
        "ConnectionAck",
        "PchFile",
        "PchInventory",
        "RequestManifest",
        "RequestFile",
        "ToolchainInventory",
        "Refused",
        "PchNeeded",
        "Demand",
        "Capacity",
    };
    // clang-format on
    static_assert( ( sizeof( msgNames ) / sizeof( const char * ) ) == Protocol::NUM_MESSAGES, "msgNames item count doesn't match NUM_MESSAGES" );

    return msgNames[ msgType ];
}
#endif

// IMessage
//------------------------------------------------------------------------------
Protocol::IMessage::IMessage( Protocol::MessageType msgType, uint8_t msgSize, bool hasPayload )
    : m_MsgType( msgType )
    , m_MsgSize( msgSize )
    , m_HasPayload( hasPayload )
{
    memset( m_Padding1, 0, sizeof( m_Padding1 ) );
}

// IMessage::Send
//------------------------------------------------------------------------------
bool Protocol::IMessage::Send( const ConnectionInfo * connection ) const
{
    ASSERT( connection );
    ASSERT( m_HasPayload == false ); // must use Send with payload

    // the packet is the contents of the derived class
    TCPConnectionPool & pool = connection->GetTCPConnectionPool();
    return pool.Send( connection, this, m_MsgSize );
}

// IMessage::Send (with payload)
//------------------------------------------------------------------------------
bool Protocol::IMessage::Send( const ConnectionInfo * connection, const MemoryStream & payload ) const
{
    ASSERT( connection );
    ASSERT( m_HasPayload == true ); // must NOT use Send with payload

    TCPConnectionPool & pool = connection->GetTCPConnectionPool();
    return pool.Send( connection, this, m_MsgSize, payload.GetData(), payload.GetSize() );
}

// IMessage::Send (with payload)
//------------------------------------------------------------------------------
bool Protocol::IMessage::Send( const ConnectionInfo * connection, const ConstMemoryStream & payload ) const
{
    ASSERT( connection );
    ASSERT( m_HasPayload == true ); // must NOT use Send with payload

    TCPConnectionPool & pool = connection->GetTCPConnectionPool();
    return pool.Send( connection, this, m_MsgSize, payload.GetData(), payload.GetSize() );
}

// IMessage::Broadcast
//------------------------------------------------------------------------------
bool Protocol::IMessage::Broadcast( TCPConnectionPool * pool ) const
{
    ASSERT( pool );
    ASSERT( m_HasPayload == false ); // must implement custom function

    // the packet is the contents of the derived class
    return pool->Broadcast( this, m_MsgSize );
}

// MsgConnection
//------------------------------------------------------------------------------
// The version fields of the handshake messages are where every major puts them, so a peer of another
// major can tell the versions apart. A change to the offsets is a change to every later major.
PRAGMA_DISABLE_PUSH_CLANG( "-Winvalid-offsetof" )
Protocol::MsgConnection::MsgConnection()
    : Protocol::IMessage( Protocol::MSG_CONNECTION, sizeof( MsgConnection ), false )
    , m_ProtocolVersion( kVersionMajor )
    , m_Platform( Env::GetPlatform() )
    , m_ProtocolVersionMinor( kVersionMinor )
{
    static_assert( offsetof( MsgConnection, m_ProtocolVersion ) == 4, "MsgConnection major moved" );
    static_assert( offsetof( MsgConnection, m_HostName ) == 12, "MsgConnection host name moved" );
    memset( m_Padding2, 0, sizeof( m_Padding2 ) );
    memset( m_HostName, 0, sizeof( m_HostName ) );
    if ( ::gethostname( m_HostName, 64 ) != 0 )
    {
        AString::Copy( "Unavailable", m_HostName, 12 ); // inc terminator in copy
    }
}

//------------------------------------------------------------------------------
void Protocol::MsgConnection::GetHostName( uint32_t frameSize, AString & outName ) const
{
    // A client does not have to terminate it, and its frame may stop short of it
    const uint32_t offset = (uint32_t)offsetof( MsgConnection, m_HostName );
    const uint32_t available = ( frameSize > offset ) ? Math::Min<uint32_t>( frameSize - offset, sizeof( m_HostName ) ) : 0;
    const char * end = static_cast<const char *>( memchr( m_HostName, 0, available ) );
    outName.Assign( m_HostName, ( end ? end : ( m_HostName + available ) ) );
}

// MsgConnectionAck
//------------------------------------------------------------------------------
Protocol::MsgConnectionAck::MsgConnectionAck( uint32_t capacity )
    : Protocol::IMessage( Protocol::MSG_CONNECTION_ACK, sizeof( MsgConnectionAck ), false )
    , m_WorkerVersion( static_cast<uint16_t>( GetVersionIdentifier() ) )
    , m_ProtocolVersionMajor( kVersionMajor )
    , m_ProtocolVersionMinor( kVersionMinor )
    , m_WorkerCapacity( capacity )
{
    static_assert( offsetof( MsgConnectionAck, m_WorkerVersion ) == 4, "MsgConnectionAck worker version moved" );
    static_assert( offsetof( MsgConnectionAck, m_ProtocolVersionMajor ) == 6, "MsgConnectionAck major moved" );
    static_assert( offsetof( MsgConnectionAck, m_ProtocolVersionMinor ) == 7, "MsgConnectionAck minor moved" );
    static_assert( offsetof( MsgConnectionAck, m_WorkerCapacity ) == 8, "MsgConnectionAck capacity moved" );
}

// MsgRefused
//------------------------------------------------------------------------------
Protocol::MsgRefused::MsgRefused()
    : Protocol::IMessage( Protocol::MSG_REFUSED, sizeof( MsgRefused ), false )
    , m_WorkerVersion( static_cast<uint16_t>( GetVersionIdentifier() ) )
    , m_ProtocolVersionMajor( kVersionMajor )
    , m_ProtocolVersionMinor( kVersionMinor )
{
    static_assert( offsetof( MsgRefused, m_WorkerVersion ) == 4, "MsgRefused worker version moved" );
    static_assert( offsetof( MsgRefused, m_ProtocolVersionMajor ) == 6, "MsgRefused major moved" );
    static_assert( offsetof( MsgRefused, m_ProtocolVersionMinor ) == 7, "MsgRefused minor moved" );
}
PRAGMA_DISABLE_POP_CLANG

// MsgJob
//------------------------------------------------------------------------------
Protocol::MsgJob::MsgJob( uint64_t toolId, int16_t resultCompressionLevel )
    : Protocol::IMessage( Protocol::MSG_JOB, sizeof( MsgJob ), true )
    , m_ResultCompressionLevel( resultCompressionLevel )
    , m_ToolId( toolId )
{
    memset( m_Padding2, 0, sizeof( m_Padding2 ) );
    ASSERT( toolId );
}

// MsgJobResult
//------------------------------------------------------------------------------
Protocol::MsgJobResult::MsgJobResult( uint8_t flags )
    : Protocol::IMessage( Protocol::MSG_JOB_RESULT, sizeof( MsgJobResult ), true )
    , m_Flags( flags )
{
}

// MsgJobResultCompressed
//------------------------------------------------------------------------------
Protocol::MsgJobResultCompressed::MsgJobResultCompressed( uint8_t flags )
    : Protocol::IMessage( Protocol::MSG_JOB_RESULT_COMPRESSED, sizeof( MsgJobResultCompressed ), true )
    , m_Flags( flags )
{
}

// MsgRequestManifest
//------------------------------------------------------------------------------
Protocol::MsgRequestManifest::MsgRequestManifest( uint64_t toolId )
    : Protocol::IMessage( Protocol::MSG_REQUEST_MANIFEST, sizeof( MsgRequestManifest ), false )
    , m_ToolId( toolId )
{
    memset( m_Padding2, 0, sizeof( m_Padding2 ) );
}

// MsgManifest
//------------------------------------------------------------------------------
Protocol::MsgManifest::MsgManifest( uint64_t toolId )
    : Protocol::IMessage( Protocol::MSG_MANIFEST, sizeof( MsgManifest ), true )
    , m_ToolId( toolId )
{
    memset( m_Padding2, 0, sizeof( m_Padding2 ) );
}

// MsgRequestFile
//------------------------------------------------------------------------------
Protocol::MsgRequestFile::MsgRequestFile( uint64_t toolId, uint32_t fileId )
    : Protocol::IMessage( Protocol::MSG_REQUEST_FILE, sizeof( MsgRequestFile ), false )
    , m_FileId( fileId )
    , m_ToolId( toolId )
{
}

// MsgFile
//------------------------------------------------------------------------------
Protocol::MsgFile::MsgFile( uint64_t toolId, uint32_t fileId )
    : Protocol::IMessage( Protocol::MSG_FILE, sizeof( MsgFile ), true )
    , m_FileId( fileId )
    , m_ToolId( toolId )
{
}

// MsgPchFile
//------------------------------------------------------------------------------
Protocol::MsgPchFile::MsgPchFile( uint64_t pchId, uint32_t uncompressedSize, uint32_t compressedSize, uint32_t chunkSize, uint32_t chunkIndex )
    : Protocol::IMessage( Protocol::MSG_PCH_FILE, sizeof( MsgPchFile ), true )
    , m_PchId( pchId )
    , m_UncompressedSize( uncompressedSize )
    , m_CompressedSize( compressedSize )
    , m_ChunkSize( chunkSize )
    , m_ChunkIndex( chunkIndex )
{
}

//------------------------------------------------------------------------------
/*static*/ uint32_t Protocol::MsgPchFile::GetNumChunks( uint32_t compressedSize, uint32_t chunkSize )
{
    ASSERT( chunkSize > 0 );
    return (uint32_t)( ( (uint64_t)compressedSize + chunkSize - 1 ) / chunkSize );
}

//------------------------------------------------------------------------------
/*static*/ uint32_t Protocol::MsgPchFile::GetChunkLength( uint32_t compressedSize, uint32_t chunkSize, uint32_t chunkIndex )
{
    const uint64_t offset = ( (uint64_t)chunkIndex * chunkSize );
    if ( offset >= compressedSize )
    {
        return 0;
    }
    return (uint32_t)Math::Min<uint64_t>( chunkSize, compressedSize - offset );
}

// MsgPchInventory
//------------------------------------------------------------------------------
Protocol::MsgPchInventory::MsgPchInventory( uint32_t numEntries )
    : Protocol::IMessage( Protocol::MSG_PCH_INVENTORY, sizeof( MsgPchInventory ), numEntries > 0 )
    , m_NumEntries( numEntries )
{
}

// MsgPchNeeded
//------------------------------------------------------------------------------
Protocol::MsgPchNeeded::MsgPchNeeded( uint64_t pchId )
    : Protocol::IMessage( Protocol::MSG_PCH_NEEDED, sizeof( MsgPchNeeded ), false )
    , m_PchId( pchId )
{
    memset( m_Padding2, 0, sizeof( m_Padding2 ) );
}

// MsgToolchainInventory
//------------------------------------------------------------------------------
Protocol::MsgToolchainInventory::MsgToolchainInventory( uint32_t numEntries )
    : Protocol::IMessage( Protocol::MSG_TOOLCHAIN_INVENTORY, sizeof( MsgToolchainInventory ), numEntries > 0 )
    , m_NumEntries( numEntries )
{
}

// MsgDemand
//------------------------------------------------------------------------------
Protocol::MsgDemand::MsgDemand( uint32_t demand )
    : Protocol::IMessage( Protocol::MSG_DEMAND, sizeof( MsgDemand ), false )
    , m_Demand( demand )
{
}

// MsgCapacity
//------------------------------------------------------------------------------
Protocol::MsgCapacity::MsgCapacity( uint32_t allowance, uint32_t poolSlots )
    : Protocol::IMessage( Protocol::MSG_CAPACITY, sizeof( MsgCapacity ), false )
    , m_Allowance( allowance )
    , m_PoolSlots( poolSlots )
{
}

// GetMinMessageSize
//------------------------------------------------------------------------------
uint32_t Protocol::GetMinMessageSize( MessageType type )
{
    switch ( type )
    {
        case MSG_CONNECTION:            return kHandshakePrefixSize;
        case MSG_CONNECTION_ACK:        return kHandshakePrefixSize;
        case MSG_REFUSED:               return sizeof( MsgRefused );
        case MSG_JOB:                   return sizeof( MsgJob );
        case MSG_JOB_RESULT:            return sizeof( MsgJobResult );
        case MSG_JOB_RESULT_COMPRESSED: return sizeof( MsgJobResultCompressed );
        case MSG_MANIFEST:              return sizeof( MsgManifest );
        case MSG_FILE:                  return sizeof( MsgFile );
        case MSG_PCH_FILE:              return sizeof( MsgPchFile );
        case MSG_PCH_INVENTORY:         return sizeof( MsgPchInventory );
        case MSG_REQUEST_MANIFEST:      return sizeof( MsgRequestManifest );
        case MSG_REQUEST_FILE:          return sizeof( MsgRequestFile );
        case MSG_TOOLCHAIN_INVENTORY:   return sizeof( MsgToolchainInventory );
        case MSG_PCH_NEEDED:            return sizeof( MsgPchNeeded );
        case MSG_DEMAND:                return sizeof( MsgDemand );
        case MSG_CAPACITY:              return sizeof( MsgCapacity );
        case NUM_MESSAGES:              break;
    }
    return 0;
}

// GetPayloadRule
//------------------------------------------------------------------------------
Protocol::PayloadRule Protocol::GetPayloadRule( MessageType type )
{
    switch ( type )
    {
        case MSG_CONNECTION:            return PAYLOAD_NEVER;
        case MSG_CONNECTION_ACK:        return PAYLOAD_NEVER;
        case MSG_REFUSED:               return PAYLOAD_NEVER;
        case MSG_JOB:                   return PAYLOAD_ALWAYS;
        case MSG_JOB_RESULT:            return PAYLOAD_ALWAYS;
        case MSG_JOB_RESULT_COMPRESSED: return PAYLOAD_ALWAYS;
        case MSG_MANIFEST:              return PAYLOAD_ALWAYS;
        case MSG_FILE:                  return PAYLOAD_ALWAYS;
        case MSG_PCH_FILE:              return PAYLOAD_ALWAYS;
        case MSG_PCH_INVENTORY:         return PAYLOAD_OPTIONAL;
        case MSG_REQUEST_MANIFEST:      return PAYLOAD_NEVER;
        case MSG_REQUEST_FILE:          return PAYLOAD_NEVER;
        case MSG_TOOLCHAIN_INVENTORY:   return PAYLOAD_OPTIONAL;
        case MSG_PCH_NEEDED:            return PAYLOAD_NEVER;
        case MSG_DEMAND:                return PAYLOAD_NEVER;
        case MSG_CAPACITY:              return PAYLOAD_NEVER;
        case NUM_MESSAGES:              break;
    }
    return PAYLOAD_NEVER;
}

// IsValidHeader
//------------------------------------------------------------------------------
bool Protocol::IsValidHeader( const void * frame, uint32_t size )
{
    if ( size < sizeof( IMessage ) )
    {
        return false; // Not even the type can be read
    }
    const IMessage * msg = static_cast<const IMessage *>( frame );
    const uint32_t minSize = GetMinMessageSize( msg->GetType() );
    if ( ( minSize == 0 ) || ( size < minSize ) )
    {
        return false; // An unknown type, or too short for its type
    }
    const PayloadRule rule = GetPayloadRule( msg->GetType() );
    return ( ( rule == PAYLOAD_OPTIONAL ) || ( ( rule == PAYLOAD_ALWAYS ) == msg->HasPayload() ) );
}

// FormatVersion
//------------------------------------------------------------------------------
void Protocol::FormatVersion( AString & outText, uint32_t major, uint32_t minor )
{
    outText.Format( "v%u.%u", major, minor );
}

// GetVersionString
//------------------------------------------------------------------------------
void Protocol::GetVersionString( AString & outText )
{
    FormatVersion( outText, kVersionMajor, kVersionMinor );
}

//------------------------------------------------------------------------------
