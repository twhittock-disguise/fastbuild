// GatewayWire.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "GatewayWire.h"

#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Env/Assert.h"
#include "Core/Math/Conversions.h"
#include "Core/Strings/AString.h"

#include <string.h>

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

// PatchResult
//------------------------------------------------------------------------------
void GatewayWire::PatchResult( void * payload, size_t payloadSize, const Protocol::JobResultHeader & header, uint32_t jobId, uint16_t threadIndex )
{
    char * bytes = static_cast<char *>( payload );
    ASSERT( payloadSize >= ( (size_t)header.m_ThreadIndexOffset + sizeof( threadIndex ) ) ); // The caller read the header with Protocol::ReadJobResult
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
    Array<AString> messages;
    messages.EmplaceBack( message );
    Protocol::WriteJobResult( out, jobId, AString::GetEmpty(), false, true, messages, 0, 0, nullptr, 0 );
}

//------------------------------------------------------------------------------
