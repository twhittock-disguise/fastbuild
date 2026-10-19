// GatewayWire - Reading and patching job and result messages without deserializing them
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"
#include "Core/Strings/AString.h"

// Forward Declarations
//------------------------------------------------------------------------------
class MemoryStream;

// GatewayWire
//------------------------------------------------------------------------------
// The gateway forwards what clients and workers send it. It needs a few fields
// of those messages (ids, thread index) and leaves the rest as it is.
//------------------------------------------------------------------------------
namespace GatewayWire
{
    // A serialized job starts with its job id and ends with its PCH id (Job::Serialize)
    static constexpr size_t kMinJobSize = sizeof( uint32_t ) + sizeof( uint64_t );

    // Remote thread indexes start at 1001 (JobQueueRemote), and profiler lanes are named from 1000
    static constexpr uint16_t kFirstThreadIndex = 1000;
    static constexpr uint32_t kLaneStride = 256; // Lanes per worker; worker 0 is the gateway's own pool
    static constexpr uint32_t kMaxWorkerIndex = ( 0xFFFF - kFirstThreadIndex ) / kLaneStride - 1;

    [[nodiscard]] bool PeekJob( const void * job, size_t jobSize, uint32_t & outJobId, uint64_t & outPchId );
    void SetJobId( void * job, size_t jobSize, uint32_t jobId );

    // The fields of a result (Server::FinalizeCompletedJobs) before its data
    struct ResultHeader
    {
        uint32_t m_JobId = 0;
        AString m_Name;
        bool m_Ok = false;
        bool m_SystemError = false;
        uint16_t m_ThreadIndex = 0;
        uint32_t m_ThreadIndexOffset = 0; // Where the thread index is in the payload
    };
    [[nodiscard]] bool ReadResultHeader( const void * payload, size_t payloadSize, ResultHeader & outHeader );
    void PatchResult( void * payload, size_t payloadSize, const ResultHeader & header, uint32_t jobId, uint16_t threadIndex );

    // kFirstThreadIndex + workerIndex * kLaneStride + Clamp( threadIndex - kFirstThreadIndex, 1, kLaneStride - 1 )
    uint16_t MakeLane( uint32_t workerIndex, uint16_t threadIndex );

    // A result (systemError set) for a job whose last attempt ended without one
    void WriteLostResult( MemoryStream & out, uint32_t jobId, const char * message );
}

//------------------------------------------------------------------------------
