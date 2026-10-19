// GatewayScheduler - Which waiting job starts on which worker
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Array.h"
#include "Core/Env/Assert.h"
#include "Core/Env/Types.h"

// GatewayScheduler
//------------------------------------------------------------------------------
// Which waiting job starts on which worker. Pure; the gateway calls it under its mutex.
//
// A job is waiting (in its client's queue) or running (on one worker). Slots are
// counted apart from jobs: Next takes a slot and only ReleaseSlot gives it back, so a
// job retried after a timeout keeps the slot of its first attempt taken.
//------------------------------------------------------------------------------
class GatewayScheduler
{
public:
    static constexpr uint32_t kMaxAttempts = 3;  // The first, and two retries
    static constexpr uint32_t kScanDepth = 16;

    enum Affinity : uint8_t { AFFINITY_UNABLE, AFFINITY_NONE, AFFINITY_RECEIVING, AFFINITY_HOLDS };
    struct Assignment { uint32_t m_JobId = 0; uint32_t m_WorkerId = 0; };
    enum Outcome { OUTCOME_RETRY, OUTCOME_GIVE_UP };

    void SetWorkerSlots( uint32_t workerId, uint32_t slots );  // Adds it if new. Fewer slots than running: no new work until below.
    void RemoveWorker( uint32_t workerId, Array<uint32_t> & outRunningJobIds ); // The jobs stay running until OnFailed or OnCompleted
    void ReleaseSlot( uint32_t workerId );
    uint32_t GetTotalSlots() const;

    void AddJob( uint32_t jobId, uint32_t clientId, uint64_t pchId, uint64_t toolId );
    void RemoveClient( uint32_t clientId, Array<uint32_t> & outWaitingJobIds ); // Its running jobs are given up when they fail

    // Round-robin over clients with waiting jobs, one job per turn. Within a client: the first of kScanDepth
    // jobs with a free worker that HOLDS what it needs, else the first job any free, able worker can take.
    // Workers that failed the job are avoided unless every connected able worker has. Ties: more free slots,
    // then the lower id. Takes a slot.
    // AFFINITY: GatewayScheduler::Affinity operator()( uint32_t workerId, uint64_t pchId, uint64_t toolId ) const
    template <class AFFINITY> bool Next( const AFFINITY & affinity, Assignment & outAssignment );
    template <class AFFINITY> uint32_t GetNumWaitingFor( uint32_t workerId, const AFFINITY & affinity ) const;

    void OnCompleted( uint32_t jobId );
    Outcome OnFailed( uint32_t jobId, uint32_t workerId ); // RETRY puts the job ahead of its client's untried jobs
    uint32_t GetNumWaiting() const;
    uint32_t GetNumRunning() const;

private:
    struct Worker
    {
        uint32_t m_Id = 0;
        uint32_t m_Slots = 0;
        uint32_t m_Used = 0;

        uint32_t GetFree() const { return ( m_Slots > m_Used ) ? ( m_Slots - m_Used ) : 0; }
    };

    struct Job
    {
        uint32_t m_Id = 0;
        uint32_t m_ClientId = 0;
        uint64_t m_PchId = 0;
        uint64_t m_ToolId = 0;
        uint32_t m_WorkerId = 0;        // While running
        uint32_t m_NumFailures = 0;
        uint32_t m_FailedOn[ kMaxAttempts ] = {};
        bool m_ClientGone = false;      // While running

        bool HasFailedOn( uint32_t workerId ) const;
    };

    struct Client
    {
        uint32_t m_Id = 0;
        Array<Job> m_Waiting;           // In the order they start
    };

    // A free worker for the job: the best affinity, then the most free slots, then the lowest id
    template <class AFFINITY> const Worker * PickWorker( const Job & job, const AFFINITY & affinity, Affinity & outAffinity ) const;
    // Whether the job avoids the worker: it failed there, and some able worker with slots has not failed it
    template <class AFFINITY> bool IsAvoided( const Job & job, uint32_t workerId, const AFFINITY & affinity ) const;
    template <class AFFINITY> bool NextFromClient( Client & client, const AFFINITY & affinity, Assignment & outAssignment );

    void Start( Client & client, size_t index, uint32_t workerId, Assignment & outAssignment );
    bool HasFreeSlots() const;
    Worker * FindWorker( uint32_t workerId );
    Client * FindClient( uint32_t clientId );
    Job * FindRunning( uint32_t jobId );

    Array<Worker>   m_Workers;
    Array<Client>   m_Clients;          // In the order of their first job
    Array<Job>      m_Running;
    size_t          m_NextClient = 0;   // Index in m_Clients of the client whose turn is next
};

// Next
//------------------------------------------------------------------------------
template <class AFFINITY>
bool GatewayScheduler::Next( const AFFINITY & affinity, Assignment & outAssignment )
{
    if ( HasFreeSlots() == false )
    {
        return false;
    }
    const size_t numClients = m_Clients.GetSize();
    for ( size_t i = 0; i < numClients; ++i )
    {
        const size_t index = ( m_NextClient + i ) % numClients;
        if ( NextFromClient( m_Clients[ index ], affinity, outAssignment ) )
        {
            m_NextClient = ( index + 1 ) % numClients;
            return true;
        }
    }
    return false;
}

// GetNumWaitingFor
//------------------------------------------------------------------------------
template <class AFFINITY>
uint32_t GatewayScheduler::GetNumWaitingFor( uint32_t workerId, const AFFINITY & affinity ) const
{
    uint32_t count = 0;
    for ( const Client & client : m_Clients )
    {
        for ( const Job & job : client.m_Waiting )
        {
            if ( ( affinity( workerId, job.m_PchId, job.m_ToolId ) != AFFINITY_UNABLE ) &&
                 ( IsAvoided( job, workerId, affinity ) == false ) )
            {
                ++count;
            }
        }
    }
    return count;
}

// PickWorker
//------------------------------------------------------------------------------
template <class AFFINITY>
const GatewayScheduler::Worker * GatewayScheduler::PickWorker( const Job & job, const AFFINITY & affinity, Affinity & outAffinity ) const
{
    const Worker * best = nullptr;
    Affinity bestAffinity = AFFINITY_UNABLE;
    for ( const Worker & worker : m_Workers )
    {
        const uint32_t free = worker.GetFree();
        if ( free == 0 )
        {
            continue;
        }
        const Affinity a = affinity( worker.m_Id, job.m_PchId, job.m_ToolId );
        if ( ( a == AFFINITY_UNABLE ) || IsAvoided( job, worker.m_Id, affinity ) )
        {
            continue;
        }
        if ( best )
        {
            const uint32_t bestFree = best->GetFree();
            const bool better = ( a != bestAffinity ) ? ( a > bestAffinity )
                              : ( free != bestFree ) ? ( free > bestFree )
                                                     : ( worker.m_Id < best->m_Id );
            if ( better == false )
            {
                continue;
            }
        }
        best = &worker;
        bestAffinity = a;
    }
    outAffinity = bestAffinity;
    return best;
}

// IsAvoided
//------------------------------------------------------------------------------
template <class AFFINITY>
bool GatewayScheduler::IsAvoided( const Job & job, uint32_t workerId, const AFFINITY & affinity ) const
{
    if ( job.HasFailedOn( workerId ) == false )
    {
        return false;
    }
    for ( const Worker & worker : m_Workers )
    {
        if ( ( worker.m_Slots > 0 ) &&
             ( job.HasFailedOn( worker.m_Id ) == false ) &&
             ( affinity( worker.m_Id, job.m_PchId, job.m_ToolId ) != AFFINITY_UNABLE ) )
        {
            return true;
        }
    }
    return false;
}

// NextFromClient
//------------------------------------------------------------------------------
template <class AFFINITY>
bool GatewayScheduler::NextFromClient( Client & client, const AFFINITY & affinity, Assignment & outAssignment )
{
    // The first job a free worker holds what it needs for, within kScanDepth; else the first job any can take
    const Worker * fallbackWorker = nullptr;
    size_t fallbackIndex = 0;
    const size_t numWaiting = client.m_Waiting.GetSize();
    for ( size_t i = 0; i < numWaiting; ++i )
    {
        if ( ( i >= kScanDepth ) && fallbackWorker )
        {
            break;
        }
        Affinity a;
        const Worker * worker = PickWorker( client.m_Waiting[ i ], affinity, a );
        if ( worker == nullptr )
        {
            continue;
        }
        if ( ( a == AFFINITY_HOLDS ) && ( i < kScanDepth ) )
        {
            Start( client, i, worker->m_Id, outAssignment );
            return true;
        }
        if ( fallbackWorker == nullptr )
        {
            fallbackWorker = worker;
            fallbackIndex = i;
        }
    }
    if ( fallbackWorker )
    {
        Start( client, fallbackIndex, fallbackWorker->m_Id, outAssignment );
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
