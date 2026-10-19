// PchDispatch.h - Which queued job a remote worker should be given, by the PCH it needs
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Array.h"
#include "Core/Env/Types.h"
#include "Core/Math/Conversions.h"

// PchSendState
//------------------------------------------------------------------------------
// The PCHs of one worker connection. Not thread safe.
class PchSendState
{
public:
    // The worker reports the PCHs it has cached
    void OnInventory( const uint64_t * pchIds, size_t numPchIds )
    {
        for ( size_t i = 0; i < numPchIds; ++i )
        {
            OnDelivered( pchIds[ i ] );
        }
    }

    // A job needs the PCH. Returns false if the worker has it or it is already on its way.
    bool OnRequested( uint64_t pchId )
    {
        if ( IsRequested( pchId ) )
        {
            return false;
        }
        m_Sending.Append( pchId );
        return true;
    }

    // The PCH has reached the worker: it was reported, or was handed to the socket in full.
    // The worker evicts PCHs the coordinator cannot see, so this can stop being true: a worker that
    // asks for the PCH again when a job needs it (OnNeeded).
    void OnDelivered( uint64_t pchId )
    {
        m_Sending.FindAndErase( pchId );
        if ( m_OnWorker.Find( pchId ) == nullptr )
        {
            m_OnWorker.Append( pchId );
        }
    }

    // A PCH is sent again at most this many times per connection, so a worker that keeps losing it
    // (or asking for what is on its way) is given up on rather than fed forever
    static constexpr uint32_t kMaxResends = 8;

    enum NeededResult
    {
        NEEDED_ALREADY_QUEUED,  // On its way: nothing to do
        NEEDED_QUEUED,          // Taken off the worker, to be sent again
        NEEDED_LIMIT,           // Sent again too many times
    };

    // The worker says a job waits for the PCH, so it does not have it
    NeededResult OnNeeded( uint64_t pchId )
    {
        if ( m_Sending.Find( pchId ) != nullptr )
        {
            return NEEDED_ALREADY_QUEUED;
        }

        Resends & resends = GetResends( pchId );
        if ( resends.m_Count >= kMaxResends )
        {
            return NEEDED_LIMIT;
        }
        ++resends.m_Count;

        m_OnWorker.FindAndErase( pchId );
        m_Sending.Append( pchId );
        return NEEDED_QUEUED;
    }

    // A job that uses the PCH has completed on the worker: the PCH was there, so what it lost before
    // does not count against it any more
    void OnJobCompleted( uint64_t pchId )
    {
        GetResends( pchId ).m_Count = 0;
    }

    bool IsOnWorker( uint64_t pchId ) const { return m_OnWorker.Find( pchId ) != nullptr; }
    bool IsRequested( uint64_t pchId ) const { return IsOnWorker( pchId ) || ( m_Sending.Find( pchId ) != nullptr ); }

    const Array<uint64_t> & GetOnWorker() const { return m_OnWorker; }
    const Array<uint64_t> & GetSending() const { return m_Sending; } // In the order requested, which is the order sent

private:
    struct Resends
    {
        uint64_t m_PchId = 0;
        uint32_t m_Count = 0;
    };

    Resends & GetResends( uint64_t pchId )
    {
        for ( Resends & r : m_Resends )
        {
            if ( r.m_PchId == pchId )
            {
                return r;
            }
        }
        Resends newResends;
        newResends.m_PchId = pchId;
        m_Resends.Append( newResends );
        return m_Resends.Top();
    }

    Array<uint64_t> m_OnWorker;
    Array<uint64_t> m_Sending;
    Array<Resends> m_Resends;
};

// PchWorkerState
//------------------------------------------------------------------------------
struct PchWorkerState
{
    Array<uint64_t> m_OnWorker;
    Array<uint64_t> m_Sending;
    uint32_t m_NumHeld = 0; // Jobs the worker already has that wait for a PCH
    uint32_t m_MaxHeld = 0;
};

// PchDispatch
//------------------------------------------------------------------------------
// A worker can only start a job once the PCH it uses has arrived, and over a slow link that
// takes a long time. The uplink is kept busy by queueing the PCH that unblocks the most waiting
// jobs as soon as fewer than kMaxSendingPchs are queued, whether or not a job for it has been
// taken yet (ties go to the lowest id, so workers sharing an uplink choose alike). Jobs that can
// run now go to a worker first. A few more are given to it for the PCHs it is receiving, so it
// does not wait for the next job once one lands.
namespace PchDispatch
{
    // Tuning choices, not derived: the next PCH is queued behind the one being sent so the link
    // stays busy, and at most half of a worker's slots wait for a PCH so jobs that can run are
    // never shut out.
    static constexpr uint32_t kMaxSendingPchs = 2;
    inline uint32_t MaxHeldJobs( uint32_t capacity ) { return Math::Max( 1u, capacity / 2u ); }

    // The PCH to queue for sending next, or 0. "Sending" counts the PCHs queued on the connection
    // and not yet handed to the socket, so the next one is queued while the current is sent.
    // waitingPchIds and numWaiting list the PCHs the queued jobs need, and how many jobs need each.
    inline uint64_t SelectPchToQueue( const Array<uint64_t> & waitingPchIds,
                                      const Array<uint32_t> & numWaiting,
                                      const PchSendState & state )
    {
        if ( state.GetSending().GetSize() >= kMaxSendingPchs )
        {
            return 0;
        }

        uint64_t best = 0;
        uint32_t bestCount = 0;
        for ( size_t i = 0; i < waitingPchIds.GetSize(); ++i )
        {
            const uint64_t pchId = waitingPchIds[ i ];
            if ( ( pchId == 0 ) || state.IsRequested( pchId ) )
            {
                continue;
            }
            if ( ( numWaiting[ i ] > bestCount ) ||
                 ( ( numWaiting[ i ] == bestCount ) && ( bestCount > 0 ) && ( pchId < best ) ) )
            {
                best = pchId;
                bestCount = numWaiting[ i ];
            }
        }
        return best;
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
            if ( ( pchId == 0 ) || ( state.m_OnWorker.Find( pchId ) != nullptr ) )
            {
                return static_cast<int32_t>( i - 1 );
            }
        }

        if ( state.m_NumHeld >= state.m_MaxHeld )
        {
            return -1;
        }

        // Jobs for a PCH on its way, the earliest sent first
        for ( const uint64_t sendingId : state.m_Sending )
        {
            for ( size_t i = numJobs; i > 0; --i )
            {
                if ( getPchId( i - 1 ) == sendingId )
                {
                    return static_cast<int32_t>( i - 1 );
                }
            }
        }
        return -1;
    }
}

//------------------------------------------------------------------------------
