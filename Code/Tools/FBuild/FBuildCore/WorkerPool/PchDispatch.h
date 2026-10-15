// PchDispatch.h - Which queued job a remote worker should be given, by the PCH it needs
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Array.h"
#include "Core/Env/Types.h"

// PchWorkerState
//------------------------------------------------------------------------------
struct PchWorkerState
{
    const Array<uint64_t> * m_OnWorker = nullptr; // Received by the worker, or reported in its inventory
    const Array<uint64_t> * m_Sending = nullptr;  // Queued for or being sent to the worker, in send order
    uint32_t m_NumHeld = 0; // Jobs the worker already has that wait for a PCH
    uint32_t m_MaxHeld = 0;
};

// PchDispatch
//------------------------------------------------------------------------------
// A worker can only start a job once the PCH it uses has arrived, and over a slow link that
// takes a long time. Jobs that can run now go first. A few more are given to the worker for the
// PCH it is receiving, so it does not wait for the next job once the PCH lands, and the PCH to
// send next is the one that unblocks the most queued jobs (ties go to the lowest id, so workers
// sharing an uplink choose alike).
namespace PchDispatch
{
    static constexpr uint32_t kMaxSendingPchs = 2; // The next PCH is queued behind the current one so the link stays busy

    inline bool Contains( const Array<uint64_t> * ids, uint64_t id )
    {
        return ( ids != nullptr ) && ( ids->Find( id ) != nullptr );
    }

    // Returns the index of the job to give the worker, or -1. Jobs at the end of the queue are
    // the most expensive, so they are taken first.
    template <class GET_PCH_ID>
    int32_t SelectJob( size_t numJobs, const GET_PCH_ID & getPchId, const PchWorkerState & state )
    {
        // Jobs the worker can start now
        for ( size_t i = numJobs; i > 0; --i )
        {
            const uint64_t pchId = getPchId( i - 1 );
            if ( ( pchId == 0 ) || Contains( state.m_OnWorker, pchId ) )
            {
                return static_cast<int32_t>( i - 1 );
            }
        }

        if ( state.m_NumHeld >= state.m_MaxHeld )
        {
            return -1;
        }

        // Jobs for a PCH on its way, the earliest sent first
        if ( state.m_Sending )
        {
            for ( const uint64_t sendingId : *state.m_Sending )
            {
                for ( size_t i = numJobs; i > 0; --i )
                {
                    if ( getPchId( i - 1 ) == sendingId )
                    {
                        return static_cast<int32_t>( i - 1 );
                    }
                }
            }
            if ( state.m_Sending->GetSize() >= kMaxSendingPchs )
            {
                return -1;
            }
        }

        // Start sending the PCH that has the most jobs waiting
        Array<uint64_t> ids;
        Array<uint32_t> counts;
        for ( size_t i = 0; i < numJobs; ++i )
        {
            const uint64_t pchId = getPchId( i );
            bool found = false;
            for ( size_t j = 0; j < ids.GetSize(); ++j )
            {
                if ( ids[ j ] == pchId )
                {
                    ++counts[ j ];
                    found = true;
                    break;
                }
            }
            if ( found == false )
            {
                ids.Append( pchId );
                counts.Append( 1 );
            }
        }
        size_t best = 0;
        for ( size_t j = 1; j < ids.GetSize(); ++j )
        {
            if ( ( counts[ j ] > counts[ best ] ) ||
                 ( ( counts[ j ] == counts[ best ] ) && ( ids[ j ] < ids[ best ] ) ) )
            {
                best = j;
            }
        }
        if ( ids.IsEmpty() )
        {
            return -1;
        }
        for ( size_t i = numJobs; i > 0; --i )
        {
            if ( getPchId( i - 1 ) == ids[ best ] )
            {
                return static_cast<int32_t>( i - 1 );
            }
        }
        return -1;
    }
}

//------------------------------------------------------------------------------
