// GatewayWire.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "GatewayWire.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Env/Assert.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/Math/Conversions.h"
#include "Core/Strings/AStackString.h"

#include <string.h>

// Helpers
//------------------------------------------------------------------------------
namespace
{
    // Reads from a buffer that came from the network, so every read is bounds checked
    class WireReader
    {
    public:
        WireReader( const void * data, size_t size )
            : m_Data( static_cast<const char *>( data ) )
            , m_Size( size )
        {
        }

        size_t Tell() const { return m_Pos; }

        template <class T>
        bool Read( T & out )
        {
            if ( ( m_Size - m_Pos ) < sizeof( T ) )
            {
                return false;
            }
            memcpy( &out, m_Data + m_Pos, sizeof( T ) );
            m_Pos += sizeof( T );
            return true;
        }

        bool ReadString( AString & out )
        {
            uint32_t length;
            if ( ( Read( length ) == false ) || ( ( m_Size - m_Pos ) < length ) )
            {
                return false;
            }
            out.Assign( m_Data + m_Pos, m_Data + m_Pos + length );
            m_Pos += length;
            return true;
        }

        bool Skip( size_t bytes )
        {
            if ( ( m_Size - m_Pos ) < bytes )
            {
                return false;
            }
            m_Pos += bytes;
            return true;
        }

    private:
        const char * m_Data;
        size_t m_Size;
        size_t m_Pos = 0;
    };
}

// PeekJob
//------------------------------------------------------------------------------
bool GatewayWire::PeekJob( const void * job, size_t jobSize, uint32_t & outJobId, uint64_t & outPchId )
{
    if ( jobSize < kMinJobSize )
    {
        return false;
    }
    const char * bytes = static_cast<const char *>( job );
    memcpy( &outJobId, bytes, sizeof( outJobId ) );
    memcpy( &outPchId, bytes + jobSize - sizeof( outPchId ), sizeof( outPchId ) );
    return true;
}

// SetJobId
//------------------------------------------------------------------------------
void GatewayWire::SetJobId( void * job, size_t jobSize, uint32_t jobId )
{
    ASSERT( jobSize >= kMinJobSize ); // The caller checked it with PeekJob
    if ( jobSize >= sizeof( jobId ) )
    {
        memcpy( job, &jobId, sizeof( jobId ) );
    }
}

// ReadResultHeader
//------------------------------------------------------------------------------
bool GatewayWire::ReadResultHeader( const void * payload, size_t payloadSize, ResultHeader & outHeader )
{
    WireReader reader( payload, payloadSize );

    uint32_t numMessages;
    if ( ( reader.Read( outHeader.m_JobId ) == false ) ||
         ( reader.ReadString( outHeader.m_Name ) == false ) ||
         ( reader.Read( outHeader.m_Ok ) == false ) ||
         ( reader.Read( outHeader.m_SystemError ) == false ) ||
         ( reader.Read( numMessages ) == false ) )
    {
        return false;
    }
    for ( uint32_t i = 0; i < numMessages; ++i )
    {
        AStackString message;
        if ( reader.ReadString( message ) == false )
        {
            return false;
        }
    }

    uint32_t buildTime;
    if ( reader.Read( buildTime ) == false )
    {
        return false;
    }
    outHeader.m_ThreadIndexOffset = static_cast<uint32_t>( reader.Tell() );
    if ( reader.Read( outHeader.m_ThreadIndex ) == false )
    {
        return false;
    }

    // The data that follows is as long as it says
    uint32_t dataSize;
    return reader.Read( dataSize ) && reader.Skip( dataSize );
}

// PatchResult
//------------------------------------------------------------------------------
void GatewayWire::PatchResult( void * payload, size_t payloadSize, const ResultHeader & header, uint32_t jobId, uint16_t threadIndex )
{
    char * bytes = static_cast<char *>( payload );
    ASSERT( payloadSize >= ( (size_t)header.m_ThreadIndexOffset + sizeof( threadIndex ) ) ); // The caller read the header with ReadResultHeader
    if ( ( payloadSize >= sizeof( jobId ) ) && ( payloadSize >= ( (size_t)header.m_ThreadIndexOffset + sizeof( threadIndex ) ) ) )
    {
        memcpy( bytes, &jobId, sizeof( jobId ) );
        memcpy( bytes + header.m_ThreadIndexOffset, &threadIndex, sizeof( threadIndex ) );
    }
}

// MakeLane
//------------------------------------------------------------------------------
uint16_t GatewayWire::MakeLane( uint32_t workerIndex, uint16_t threadIndex )
{
    const int32_t lane = Math::Clamp( (int32_t)threadIndex - (int32_t)kFirstThreadIndex, 1, (int32_t)kLaneStride - 1 );
    return static_cast<uint16_t>( kFirstThreadIndex + ( workerIndex * kLaneStride ) + (uint32_t)lane );
}

// WriteLostResult
//------------------------------------------------------------------------------
void GatewayWire::WriteLostResult( MemoryStream & out, uint32_t jobId, const char * message )
{
    // The layout Server::FinalizeCompletedJobs writes
    out.Write( jobId );
    out.Write( AString::GetEmpty() );
    out.Write( false );
    out.Write( true );
    Array<AString> messages;
    messages.EmplaceBack( message );
    out.Write( messages );
    out.Write( (uint32_t)0 ); // Build time
    out.Write( (uint16_t)0 ); // Thread index
    out.Write( (uint32_t)0 ); // Data size
}

//------------------------------------------------------------------------------
