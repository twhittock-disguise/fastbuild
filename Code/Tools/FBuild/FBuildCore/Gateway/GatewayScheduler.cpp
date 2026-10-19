// GatewayScheduler.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "GatewayScheduler.h"

// SetWorkerSlots
//------------------------------------------------------------------------------
void GatewayScheduler::SetWorkerSlots( uint32_t workerId, uint32_t slots )
{
    Worker * worker = FindWorker( workerId );
    if ( worker == nullptr )
    {
        // Jobs still running on it since it was removed hold its slots
        worker = &m_Workers.EmplaceBack();
        worker->m_Id = workerId;
        for ( const Job & job : m_Running )
        {
            worker->m_Used += ( job.m_WorkerId == workerId ) ? 1 : 0;
        }
    }
    worker->m_Slots = slots;
}

// RemoveWorker
//------------------------------------------------------------------------------
void GatewayScheduler::RemoveWorker( uint32_t workerId, Array<uint32_t> & outRunningJobIds )
{
    outRunningJobIds.Clear();
    Worker * worker = FindWorker( workerId );
    if ( worker == nullptr )
    {
        return;
    }
    m_Workers.Erase( worker );
    for ( const Job & job : m_Running )
    {
        if ( job.m_WorkerId == workerId )
        {
            outRunningJobIds.Append( job.m_Id );
        }
    }
}

// ReleaseSlot
//------------------------------------------------------------------------------
void GatewayScheduler::ReleaseSlot( uint32_t workerId )
{
    // A removed worker has no slots to give back
    Worker * worker = FindWorker( workerId );
    if ( worker )
    {
        ASSERT( worker->m_Used > 0 );
        if ( worker->m_Used > 0 )
        {
            --worker->m_Used;
        }
    }
}

// GetTotalSlots
//------------------------------------------------------------------------------
uint32_t GatewayScheduler::GetTotalSlots() const
{
    uint32_t total = 0;
    for ( const Worker & worker : m_Workers )
    {
        total += worker.m_Slots;
    }
    return total;
}

// AddJob
//------------------------------------------------------------------------------
void GatewayScheduler::AddJob( uint32_t jobId, uint32_t clientId, uint64_t pchId, uint64_t toolId )
{
    Client * client = FindClient( clientId );
    if ( client == nullptr )
    {
        client = &m_Clients.EmplaceBack();
        client->m_Id = clientId;
    }
    Job & job = client->m_Waiting.EmplaceBack();
    job.m_Id = jobId;
    job.m_ClientId = clientId;
    job.m_PchId = pchId;
    job.m_ToolId = toolId;
}

// RemoveClient
//------------------------------------------------------------------------------
void GatewayScheduler::RemoveClient( uint32_t clientId, Array<uint32_t> & outWaitingJobIds )
{
    outWaitingJobIds.Clear();
    for ( Job & job : m_Running )
    {
        if ( job.m_ClientId == clientId )
        {
            job.m_ClientGone = true;
        }
    }
    Client * client = FindClient( clientId );
    if ( client == nullptr )
    {
        return;
    }
    for ( const Job & job : client->m_Waiting )
    {
        outWaitingJobIds.Append( job.m_Id );
    }

    // The turn stays with the client it was going to
    const size_t index = (size_t)( client - m_Clients.Begin() );
    m_Clients.Erase( client );
    if ( index < m_NextClient )
    {
        --m_NextClient;
    }
    if ( m_NextClient >= m_Clients.GetSize() )
    {
        m_NextClient = 0;
    }
}

// RemoveWaitingJobs
//------------------------------------------------------------------------------
void GatewayScheduler::RemoveWaitingJobs( Array<uint32_t> & outJobIds )
{
    outJobIds.Clear();
    for ( Client & client : m_Clients )
    {
        for ( const Job & job : client.m_Waiting )
        {
            outJobIds.Append( job.m_Id );
        }
        client.m_Waiting.Clear();
    }
}

// OnCompleted
//------------------------------------------------------------------------------
void GatewayScheduler::OnCompleted( uint32_t jobId )
{
    Job * job = FindRunning( jobId );
    ASSERT( job );
    if ( job )
    {
        m_Running.Erase( job );
    }
}

// OnFailed
//------------------------------------------------------------------------------
GatewayScheduler::Outcome GatewayScheduler::OnFailed( uint32_t jobId, uint32_t workerId )
{
    Job * running = FindRunning( jobId );
    ASSERT( running );
    if ( running == nullptr )
    {
        return OUTCOME_GIVE_UP;
    }
    ASSERT( running->m_WorkerId == workerId );

    Job job( *running );
    m_Running.Erase( running );
    job.m_FailedOn[ job.m_NumFailures++ ] = workerId;
    Client * client = job.m_ClientGone ? nullptr : FindClient( job.m_ClientId );
    if ( ( job.m_NumFailures >= kMaxAttempts ) || ( client == nullptr ) )
    {
        return OUTCOME_GIVE_UP;
    }

    // Ahead of the jobs not yet tried, behind the other retries
    job.m_WorkerId = 0;
    Array<Job> & waiting = client->m_Waiting;
    size_t index = 0;
    while ( ( index < waiting.GetSize() ) && ( waiting[ index ].m_NumFailures > 0 ) )
    {
        ++index;
    }
    waiting.Append( job );
    for ( size_t i = waiting.GetSize() - 1; i > index; --i )
    {
        waiting[ i ] = waiting[ i - 1 ];
    }
    waiting[ index ] = job;
    return OUTCOME_RETRY;
}

// GetNumWaiting
//------------------------------------------------------------------------------
uint32_t GatewayScheduler::GetNumWaiting() const
{
    size_t count = 0;
    for ( const Client & client : m_Clients )
    {
        count += client.m_Waiting.GetSize();
    }
    return (uint32_t)count;
}

// GetNumRunning
//------------------------------------------------------------------------------
uint32_t GatewayScheduler::GetNumRunning() const
{
    return (uint32_t)m_Running.GetSize();
}

// Job::HasFailedOn
//------------------------------------------------------------------------------
bool GatewayScheduler::Job::HasFailedOn( uint32_t workerId ) const
{
    for ( uint32_t i = 0; i < m_NumFailures; ++i )
    {
        if ( m_FailedOn[ i ] == workerId )
        {
            return true;
        }
    }
    return false;
}

// Start
//------------------------------------------------------------------------------
void GatewayScheduler::Start( Client & client, size_t index, uint32_t workerId, Assignment & outAssignment )
{
    Worker * worker = FindWorker( workerId );
    ASSERT( worker && ( worker->GetFree() > 0 ) );
    ++worker->m_Used;

    Job & job = m_Running.EmplaceBack( client.m_Waiting[ index ] );
    job.m_WorkerId = workerId;
    client.m_Waiting.EraseIndex( index );

    outAssignment.m_JobId = job.m_Id;
    outAssignment.m_WorkerId = workerId;
}

// HasFreeSlots
//------------------------------------------------------------------------------
bool GatewayScheduler::HasFreeSlots() const
{
    for ( const Worker & worker : m_Workers )
    {
        if ( worker.GetFree() > 0 )
        {
            return true;
        }
    }
    return false;
}

// FindWorker
//------------------------------------------------------------------------------
GatewayScheduler::Worker * GatewayScheduler::FindWorker( uint32_t workerId )
{
    for ( Worker & worker : m_Workers )
    {
        if ( worker.m_Id == workerId )
        {
            return &worker;
        }
    }
    return nullptr;
}

// FindClient
//------------------------------------------------------------------------------
GatewayScheduler::Client * GatewayScheduler::FindClient( uint32_t clientId )
{
    for ( Client & client : m_Clients )
    {
        if ( client.m_Id == clientId )
        {
            return &client;
        }
    }
    return nullptr;
}

// FindRunning
//------------------------------------------------------------------------------
GatewayScheduler::Job * GatewayScheduler::FindRunning( uint32_t jobId )
{
    for ( Job & job : m_Running )
    {
        if ( job.m_Id == jobId )
        {
            return &job;
        }
    }
    return nullptr;
}

//------------------------------------------------------------------------------
