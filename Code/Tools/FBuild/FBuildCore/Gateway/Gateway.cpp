// Gateway.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Gateway.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Gateway/GatewayWire.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchBlob.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"

// Core
#include "Core/Containers/Move.h"
#include "Core/Env/Assert.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Math/Conversions.h"
#include "Core/Mem/Mem.h"
#include "Core/Profile/Profile.h"
#include "Core/Strings/AStackString.h"

#include <string.h>

// Helpers
//------------------------------------------------------------------------------
namespace
{
    // <cache root>gateway<slash>toolchains<slash>, the cache root being the Server's
    AString GetStashDirectory( const AString & cacheDir )
    {
        AStackString<> directory( cacheDir );
        if ( directory.IsEmpty() && ( FBuild::GetWorkerCacheDir( directory ) == false ) )
        {
            directory.Clear();
        }
        if ( directory.IsEmpty() == false )
        {
            PathUtils::EnsureTrailingSlash( directory );
        }
        directory.AppendFormat( "gateway%ctoolchains%c", (char)NATIVE_SLASH, (char)NATIVE_SLASH );
        return AString( directory );
    }
}

// WorkerAffinity
//------------------------------------------------------------------------------
// The scheduler's view of a worker, for one pass of the scheduler. Called with m_Mutex held.
//
// The local pool holds what a job needs once the job's toolchain is synchronized there, as its PCH is stored
// before the job is scheduled. Which toolchains are synchronized is asked of the Server once per toolchain in
// a pass, from a list it keeps under a lock of its own, so no toolchain being received there holds a pass up.
class Gateway::WorkerAffinity
{
public:
    explicit WorkerAffinity( const Gateway & gateway ) : m_Gateway( gateway ) {}
    WorkerAffinity & operator=( const WorkerAffinity & other ) = delete;

    GatewayScheduler::Affinity operator()( uint32_t workerId, uint64_t pchId, uint64_t toolId ) const
    {
        if ( workerId == kLocalWorkerIndex )
        {
            return ( ( m_Gateway.m_LocalSlots > 0 ) && IsSynchronizedLocally( toolId ) ) ? GatewayScheduler::AFFINITY_HOLDS
                                                                                         : GatewayScheduler::AFFINITY_UNABLE;
        }
        if ( workerId > m_Gateway.m_Workers.GetSize() )
        {
            return GatewayScheduler::AFFINITY_UNABLE;
        }
        const Worker & worker = m_Gateway.m_Workers[ workerId - 1 ];
        return ( worker.m_Ready && worker.m_Connection ) ? worker.m_Connection->GetAffinity( pchId, toolId )
                                                         : GatewayScheduler::AFFINITY_UNABLE;
    }

private:
    struct Toolchain
    {
        uint64_t    m_ToolId;
        bool        m_Synchronized;
    };

    bool IsSynchronizedLocally( uint64_t toolId ) const
    {
        if ( toolId == 0 )
        {
            return false; // The local pool builds a job with the toolchain it names
        }
        for ( const Toolchain & toolchain : m_Toolchains )
        {
            if ( toolchain.m_ToolId == toolId )
            {
                return toolchain.m_Synchronized;
            }
        }
        const bool synchronized = m_Gateway.m_Server->IsToolchainSynchronized( toolId );
        m_Toolchains.Append( Toolchain{ toolId, synchronized } );
        return synchronized;
    }

    const Gateway & m_Gateway;
    mutable Array<Toolchain> m_Toolchains;
};

// CONSTRUCTOR
//------------------------------------------------------------------------------
Gateway::Gateway( const GatewayConfig & config )
    : Gateway( config, 0, 0 )
{
}

// CONSTRUCTOR
//------------------------------------------------------------------------------
Gateway::Gateway( const GatewayConfig & config, uint32_t localThreads, uint32_t prefetchBuffer )
    : m_Config( config )
    , m_Stash( GetStashDirectory( config.m_CacheDir ) )
{
    m_Server = FNEW( Server( *this, config.m_CacheDir, localThreads, prefetchBuffer ) );

    // The local pool's slots are in the pool before a client can connect
    {
        Actions actions;
        {
            MutexHolder mh( m_Mutex );
            UpdateLocalWorker( actions );
        }
        Run( actions );
    }

    // What the store held at start never reaches OnPchStored: its own thread measures it
    Array<uint64_t> pchIds;
    m_Server->GetCachedPchIds( pchIds );
    for ( const uint64_t pchId : pchIds )
    {
        KnownPch & pch = m_Pchs.EmplaceBack();
        pch.m_Id = pchId;
    }
    const bool measure = ( pchIds.IsEmpty() == false );

    // The table is whole before any connection can call back
    size_t numWorkers = config.m_Workers.GetSize();
    if ( numWorkers > GatewayWire::kMaxWorkerIndex )
    {
        FLOG_WARN( "The gateway uses the first %u of the %u workers it was given\n", GatewayWire::kMaxWorkerIndex, (uint32_t)numWorkers );
        numWorkers = GatewayWire::kMaxWorkerIndex;
    }
    Array<DownstreamConnection *> connections;
    connections.SetCapacity( numWorkers );
    m_Workers.SetCapacity( numWorkers );
    for ( size_t i = 0; i < numWorkers; ++i )
    {
        Worker & worker = m_Workers.EmplaceBack();
        worker.m_Connection = FNEW( DownstreamConnection( *this, config.m_Workers[ i ], (uint32_t)( i + 1 ) ) );
        connections.Append( worker.m_Connection );
    }
    for ( DownstreamConnection * connection : connections )
    {
        connection->Start();
    }

    m_HousekeepingThread.Start( HousekeepingThreadFuncStatic, "GatewayHousekeeping", this );
    if ( measure )
    {
        m_MeasureThread.Start( MeasureThreadFuncStatic, "GatewayMeasure", this );
    }
}

// DESTRUCTOR
//------------------------------------------------------------------------------
Gateway::~Gateway()
{
    // New clients are refused (the pool is 0), and nothing more is dispatched
    AString statsLine;
    {
        MutexHolder mh( m_Mutex );
        FormatStats( statsLine );
        m_ShuttingDown = true;
        UpdatePool();
    }
    m_Server->GetLog().Write( "%s", statsLine.Get() );
    m_Quit.Store( true );
    m_HousekeepingSemaphore.Signal();
    m_HousekeepingThread.Join();
    if ( m_MeasureThread.IsRunning() )
    {
        m_MeasureThread.Join(); // A measurement stops at its next piece
    }

    // Outside the mutex: a connection waits for its threads as it goes, and they call back
    Array<DownstreamConnection *> connections;
    {
        MutexHolder mh( m_Mutex );
        for ( Worker & worker : m_Workers )
        {
            connections.Append( worker.m_Connection );
            worker.m_Connection = nullptr;
            worker.m_Ready = false;
        }
    }
    for ( DownstreamConnection * connection : connections )
    {
        FDELETE connection;
    }

    // The jobs give their leases back to the Server before it goes
    Actions actions;
    {
        MutexHolder mh( m_Mutex );
        while ( m_Jobs.IsEmpty() == false )
        {
            RemoveJob( m_Jobs.Top(), actions );
        }
        m_Attempts.Clear();
    }
    Run( actions );

    // Its clients go, and their outboxes are joined
    FDELETE m_Server;
    m_Server = nullptr;
}

// Listen
//------------------------------------------------------------------------------
bool Gateway::Listen( uint16_t port )
{
    return m_Server->Listen( port );
}

// GetServer
//------------------------------------------------------------------------------
Server & Gateway::GetServer()
{
    return *m_Server;
}

// GetStats
//------------------------------------------------------------------------------
void Gateway::GetStats( Stats & outStats ) const
{
    MutexHolder mh( m_Mutex );
    GetStatsLocked( outStats );
}

// GetPoolSlots
//------------------------------------------------------------------------------
/*virtual*/ uint32_t Gateway::GetPoolSlots() const
{
    return m_PoolSlots.Load();
}

// GetToolchainInventory
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::GetToolchainInventory( uint32_t clientId, Array<uint64_t> & outToolIds )
{
    // Pinned for the connection, so what the client is told the gateway holds stays until the client has gone
    Array<uint64_t> toolIds;
    m_Stash.GetComplete( toolIds );
    outToolIds.Clear();
    for ( const uint64_t toolId : toolIds )
    {
        m_Stash.AddUse( toolId );
        if ( m_Stash.IsComplete( toolId ) )
        {
            outToolIds.Append( toolId );
        }
        else
        {
            m_Stash.ReleaseUse( toolId ); // Evicted before it was pinned
        }
    }

    MutexHolder mh( m_Mutex );
    Client & client = FindOrAddClient( clientId );
    for ( const uint64_t toolId : outToolIds )
    {
        client.m_Pins.Append( toolId );
    }
}

// OnClientJob
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnClientJob( uint32_t clientId, const Protocol::MsgJob & msg, void * jobData, size_t jobDataSize )
{
    uint32_t clientJobId = 0;
    uint64_t pchId = 0;
    if ( GatewayWire::PeekJob( jobData, jobDataSize, clientJobId, pchId ) == false )
    {
        FREE( jobData );
        m_Server->DisconnectClient( clientId );
        return;
    }

    GatewayJob * job = FNEW( GatewayJob );
    job->m_ClientId = clientId;
    job->m_ClientJobId = clientJobId;
    job->m_ToolId = msg.GetToolId();
    job->m_PchId = pchId;
    job->m_ResultCompressionLevel = msg.GetResultCompressionLevel();
    job->m_Data = jobData;
    job->m_DataSize = jobDataSize;

    // Held from here: its toolchain is not evicted, nor its PCH once that is stored
    if ( job->m_ToolId != 0 )
    {
        m_Stash.AddUse( job->m_ToolId );
    }
    if ( pchId != 0 )
    {
        job->m_PchLease = m_Server->AcquirePchLease( pchId );
    }

    // Jobs sent before the client learnt of a smaller allowance arrive after it, so the bound is the largest
    const uint32_t maxAllowance = m_Server->GetMaxAllowance( clientId );

    Actions actions;
    {
        MutexHolder mh( m_Mutex );
        Client * client = m_ShuttingDown ? nullptr : &FindOrAddClient( clientId );
        if ( client == nullptr )
        {
            actions.m_FreeJobs.Append( job );
        }
        else if ( client->m_NumJobs >= ( kMaxHeldJobsPerSlot * maxAllowance ) )
        {
            actions.m_FreeJobs.Append( job );
            actions.m_Disconnects.Append( clientId );
            AStackString<> line;
            line.Format( "gateway client-dropped client=%u reason=held-jobs held=%u allowance=%u", clientId, client->m_NumJobs, maxAllowance );
            actions.m_LogLines.EmplaceBack( line.Get() );
        }
        else
        {
            ++client->m_NumJobs;
            job->m_Id = m_NextJobId++;
            m_Jobs.Append( job );
            m_HeldJobBytes += jobDataSize;
            if ( IsReady( *job ) )
            {
                Schedule( *job );
            }
            else
            {
                Park( *job, actions );
            }
            Dispatch( actions );
        }
    }
    Run( actions );
}

// OnClientManifest
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnClientManifest( uint32_t clientId, uint64_t toolId, const void * payload, size_t payloadSize )
{
    BeginUpload( clientId, toolId );
    OnStashed( clientId, toolId, m_Stash.AddManifest( toolId, payload, payloadSize ) );
}

// OnClientFile
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnClientFile( uint32_t clientId, uint64_t toolId, uint32_t fileId, const void * payload, size_t payloadSize )
{
    BeginUpload( clientId, toolId );
    OnStashed( clientId, toolId, m_Stash.AddFile( toolId, fileId, payload, payloadSize ) );
}

// BeginUpload
//------------------------------------------------------------------------------
void Gateway::BeginUpload( uint32_t clientId, uint64_t toolId )
{
    MutexHolder mh( m_Mutex );
    if ( ( m_ShuttingDown == false ) && ( FindUpload( clientId, toolId ) == nullptr ) )
    {
        Upload & upload = m_Uploads.EmplaceBack(); // Sent unasked, as a client sends a toolchain ahead of its jobs
        upload.m_ToolId = toolId;
        upload.m_ClientId = clientId;
    }
}

// OnStashed
//------------------------------------------------------------------------------
void Gateway::OnStashed( uint32_t clientId, uint64_t toolId, ToolchainStash::Result result )
{
    if ( result == ToolchainStash::STASH_INVALID )
    {
        m_Server->DisconnectClient( clientId );
        return;
    }
    Actions actions;
    {
        MutexHolder mh( m_Mutex );
        if ( result == ToolchainStash::STASH_UNKNOWN )
        {
            OnUnknownToolchain( clientId, toolId, actions );
        }
        else if ( result == ToolchainStash::STASH_FAILED )
        {
            OnToolchainNotStored( toolId, actions );
        }
        else
        {
            OnToolchainPayload( clientId, toolId, actions );
        }
    }
    if ( result == ToolchainStash::STASH_FAILED )
    {
        m_Server->LogWarning( "The gateway cannot write toolchain 0x%016" PRIx64 " from client %u to its stash: the jobs waiting for it go back to their clients\n", toolId, clientId );
    }
    Run( actions );
}

// OnPchStored
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnPchStored( uint64_t pchId, uint32_t uncompressedSize, uint32_t compressedSize )
{
    Actions actions;
    {
        MutexHolder mh( m_Mutex );
        KnownPch * pch = FindKnownPch( pchId );
        if ( pch == nullptr )
        {
            pch = &m_Pchs.EmplaceBack();
            pch->m_Id = pchId;
        }
        pch->m_UncompressedSize = uncompressedSize;
        pch->m_CompressedSize = compressedSize;
        pch->m_Measured = true;
        ReleaseParked();
        Dispatch( actions );
    }
    Run( actions );
}

// OnPchsEvicted
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnPchsEvicted( const Array<uint64_t> & pchIds )
{
    MutexHolder mh( m_Mutex );
    for ( const uint64_t pchId : pchIds )
    {
        KnownPch * known = FindKnownPch( pchId );
        if ( known && ( m_Server->IsPchCached( pchId ) == false ) ) // Unless it was stored again meanwhile
        {
            m_Pchs.Erase( known );
        }
    }
}

// OnClientGone
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnClientGone( uint32_t clientId )
{
    Actions actions;
    {
        MutexHolder mh( m_Mutex );

        // Its waiting and parked jobs go. Those running carry on, and their results are dropped.
        Array<uint32_t> waitingJobIds;
        m_Scheduler.RemoveClient( clientId, waitingJobIds );
        for ( const uint32_t jobId : waitingJobIds )
        {
            GatewayJob * job = FindJob( jobId );
            if ( job )
            {
                RemoveJob( job, actions );
            }
        }
        for ( size_t i = m_Jobs.GetSize(); i > 0; --i )
        {
            GatewayJob * job = m_Jobs[ i - 1 ];
            if ( job->m_Parked && ( job->m_ClientId == clientId ) )
            {
                RemoveJob( job, actions );
            }
        }

        // The toolchains it was advertised
        for ( size_t i = 0; i < m_Clients.GetSize(); ++i )
        {
            if ( m_Clients[ i ].m_Id == clientId )
            {
                for ( const uint64_t toolId : m_Clients[ i ].m_Pins )
                {
                    actions.m_ReleaseUses.Append( toolId );
                }
                m_Clients.EraseIndex( i );
                break;
            }
        }

        // What it was sending and no other client is: what it left of a toolchain goes
        for ( size_t i = m_Uploads.GetSize(); i > 0; --i )
        {
            const Upload & upload = m_Uploads[ i - 1 ];
            if ( upload.m_ClientId != clientId )
            {
                continue;
            }
            const uint64_t toolId = upload.m_ToolId;
            m_Uploads.EraseIndex( i - 1 );
            if ( ( IsUploaded( toolId ) == false ) && ( m_Stash.IsComplete( toolId ) == false ) &&
                 ( actions.m_DroppedToolchains.Find( toolId ) == nullptr ) )
            {
                // Under m_Mutex, as a client counts as sending it before it writes any of it
                m_Stash.DropIncomplete( toolId, actions.m_DeleteToolchains );
                actions.m_DroppedToolchains.Append( toolId );
            }
        }

        // A client whose job waits for one of those is asked for it instead. A PCH the gone client was sending
        // no longer counts as being received, so the client of each job waiting for a PCH is asked for it again
        // (the Server asks a client once until the PCH arrives).
        if ( m_ShuttingDown == false )
        {
            for ( const GatewayJob * job : m_Jobs )
            {
                if ( job->m_Parked == false )
                {
                    continue;
                }
                if ( ( job->m_PchId != 0 ) && ( IsPchStored( job->m_PchId ) == false ) )
                {
                    bool asked = false;
                    for ( const Actions::PchAsk & ask : actions.m_PchAsks )
                    {
                        asked |= ( ( ask.m_ClientId == job->m_ClientId ) && ( ask.m_PchId == job->m_PchId ) );
                    }
                    if ( asked == false )
                    {
                        actions.m_PchAsks.Append( Actions::PchAsk{ job->m_ClientId, job->m_PchId } );
                    }
                }
                if ( actions.m_DroppedToolchains.Find( job->m_ToolId ) && ( IsUploaded( job->m_ToolId ) == false ) )
                {
                    RequestToolchain( job->m_ClientId, job->m_ToolId, true, actions );
                }
            }
        }
    }
    Run( actions );
}

// OnLocalJobEnded
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnLocalJobEnded( uint32_t jobId, bool hasResult, bool compressed, MemoryStream & payload )
{
    // The job id is the attempt's (Dispatch)
    uint32_t payloadSize = 0;
    void * data = nullptr;
    GatewayWire::ResultHeader header;
    bool readable = false;
    if ( hasResult )
    {
        payloadSize = (uint32_t)payload.GetSize();
        data = payload.Release();
        readable = data && GatewayWire::ReadResultHeader( data, payloadSize, header ) && ( header.m_JobId == jobId );
    }

    Actions actions;
    ResultRoute route;
    {
        MutexHolder mh( m_Mutex );
        if ( m_ShuttingDown ) // Its jobs have gone, and so may the Server's clients
        {
            FREE( data );
            return;
        }
        if ( readable )
        {
            RouteResult( kLocalWorkerIndex, header, data, payloadSize, route, actions );
        }
        else
        {
            OnLocalAttemptFailed( jobId, actions );
        }
        Dispatch( actions );
    }

    if ( route.m_PassOn )
    {
        MemoryStream result;
        result.Replace( data, payloadSize );
        m_Server->QueueResult( route.m_ClientId, compressed, Move( result ), route.m_Flags ); // Dropped if the client has gone
    }
    else
    {
        FREE( data );
    }
    Run( actions );
}

// OnLocalAttemptFailed
//------------------------------------------------------------------------------
void Gateway::OnLocalAttemptFailed( uint32_t attemptId, Actions & actions )
{
    Attempt * attempt = nullptr;
    for ( Attempt & a : m_Attempts )
    {
        if ( ( a.m_AttemptId == attemptId ) && ( a.m_WorkerIndex == kLocalWorkerIndex ) )
        {
            attempt = &a;
            break;
        }
    }
    if ( attempt == nullptr )
    {
        return;
    }
    m_Scheduler.ReleaseSlot( kLocalWorkerIndex );
    GatewayJob * job = attempt->m_Expired ? nullptr : FindJob( attempt->m_JobId );
    m_Attempts.Erase( attempt );
    if ( job == nullptr )
    {
        return;
    }

    // A fault of the local pool, retried as a downstream worker's is
    const uint64_t pchId = job->m_PchId;
    if ( FailAttempt( *job, kLocalWorkerIndex, "not-run", actions ) )
    {
        GiveUpWithoutResult( *job, actions );
    }

    // A PCH the local pool found damaged was dropped from the store, from which the gateway sends it on too
    if ( ( pchId != 0 ) && ( IsPchStored( pchId ) == false ) )
    {
        ParkJobsWaitingForPch( pchId, actions );
    }
}

// ParkJobsWaitingForPch
//------------------------------------------------------------------------------
void Gateway::ParkJobsWaitingForPch( uint64_t pchId, Actions & actions )
{
    // Those running keep their attempt: the PCH reached their worker, or their attempt fails
    for ( GatewayJob * job : m_Jobs )
    {
        if ( ( job->m_PchId == pchId ) && ( job->m_Parked == false ) && m_Scheduler.RemoveWaitingJob( job->m_Id ) )
        {
            Park( *job, actions ); // Its client is asked for the PCH
        }
    }
}

// OnDownstreamReady
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnDownstreamReady( DownstreamConnection & connection )
{
    Actions actions;
    {
        MutexHolder mh( m_Mutex );
        Worker * worker = FindWorker( connection );
        if ( ( worker == nullptr ) || worker->m_Ready || worker->m_Lost || m_ShuttingDown )
        {
            return;
        }
        UpdateWorker( *worker, connection, actions );
        Dispatch( actions );
    }
    Run( actions );
}

// OnDownstreamCapacity
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnDownstreamCapacity( DownstreamConnection & connection, uint32_t /*allowance*/, uint32_t /*poolSlots*/ )
{
    Actions actions;
    {
        // A connection no longer in the table has been replaced, and one that has ended is out of the scheduler
        MutexHolder mh( m_Mutex );
        Worker * worker = FindWorker( connection );
        if ( ( worker == nullptr ) || worker->m_Lost || connection.HasEnded() || m_ShuttingDown )
        {
            return;
        }
        UpdateWorker( *worker, connection, actions ); // From what the connection holds now
        Dispatch( actions );
    }
    Run( actions );
}

// OnDownstreamResult
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnDownstreamResult( DownstreamConnection & connection, bool compressed, void * payload, uint32_t payloadSize )
{
    GatewayWire::ResultHeader header;
    if ( GatewayWire::ReadResultHeader( payload, payloadSize, header ) == false )
    {
        FREE( payload );
        connection.Close( "protocol" );
        return;
    }

    Actions actions;
    ResultRoute route;
    {
        MutexHolder mh( m_Mutex );
        RouteResult( connection.GetIndex(), header, payload, payloadSize, route, actions );
        Dispatch( actions );
    }

    if ( route.m_CompletedPchId != 0 )
    {
        connection.OnJobCompleted( route.m_CompletedPchId );
    }
    if ( route.m_PassOn )
    {
        MemoryStream result;
        result.Replace( payload, payloadSize );
        m_Server->QueueResult( route.m_ClientId, compressed, Move( result ), route.m_Flags ); // Dropped if the client has gone
    }
    else
    {
        FREE( payload );
    }
    Run( actions );
}

// RouteResult
//------------------------------------------------------------------------------
void Gateway::RouteResult( uint32_t workerIndex, const GatewayWire::ResultHeader & header, void * payload, uint32_t payloadSize, ResultRoute & outRoute, Actions & actions )
{
    Attempt * attempt = nullptr;
    for ( Attempt & a : m_Attempts )
    {
        if ( ( a.m_AttemptId == header.m_JobId ) && ( a.m_WorkerIndex == workerIndex ) )
        {
            attempt = &a;
            break;
        }
    }
    if ( attempt == nullptr )
    {
        return;
    }

    m_Scheduler.ReleaseSlot( workerIndex );
    GatewayJob * job = attempt->m_Expired ? nullptr : FindJob( attempt->m_JobId );
    m_Attempts.Erase( attempt );
    if ( job && header.m_SystemError )
    {
        // The worker's fault: retried, and only the last failure of a job given up goes up
        GatewayWire::PatchResult( payload, payloadSize, header, job->m_ClientJobId, GatewayWire::MakeLane( workerIndex, header.m_ThreadIndex ) );
        outRoute.m_ClientId = job->m_ClientId;
        outRoute.m_PassOn = FailAttempt( *job, workerIndex, "system-error", actions );
        outRoute.m_Flags = Protocol::kResultReturnedByGateway;
    }
    else if ( job )
    {
        // Built, or failed by the code: passed on as it is
        m_Scheduler.OnCompleted( job->m_Id );
        outRoute.m_CompletedPchId = job->m_PchId;
        GatewayWire::PatchResult( payload, payloadSize, header, job->m_ClientJobId, GatewayWire::MakeLane( workerIndex, header.m_ThreadIndex ) );
        outRoute.m_ClientId = job->m_ClientId;
        outRoute.m_PassOn = true;
        RemoveJob( job, actions );
    }
}

// OnDownstreamLost
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnDownstreamLost( DownstreamConnection & connection )
{
    Actions actions;
    {
        // A connection no longer in the table has been replaced
        MutexHolder mh( m_Mutex );
        Worker * worker = FindWorker( connection );
        if ( ( worker == nullptr ) || worker->m_Lost )
        {
            return;
        }
        worker->m_Lost = true; // The housekeeping thread connects again
        worker->m_LostTimer.Restart();

        // One without slots is out of the scheduler, and may still be running jobs
        bool hasAttempts = false;
        for ( const Attempt & attempt : m_Attempts )
        {
            hasAttempts |= ( attempt.m_WorkerIndex == connection.GetIndex() );
        }
        if ( ( worker->m_Ready == false ) && ( hasAttempts == false ) )
        {
            return;
        }

        const char * reason = connection.GetCloseReason();
        RemoveWorker( *worker, connection, reason ? reason : "disconnected", true, actions );
        Dispatch( actions );
    }
    Run( actions );
}

// UpdateWorker
//------------------------------------------------------------------------------
void Gateway::UpdateWorker( Worker & worker, DownstreamConnection & connection, Actions & actions )
{
    // A worker with a pool stays in the scheduler while its allowance is 0, so the jobs waiting for it are
    // its demand. One without leaves it: a job that failed elsewhere would wait for a registered worker.
    const uint32_t poolSlots = connection.GetPoolSlots();
    const uint32_t allowance = connection.GetAllowance();
    if ( poolSlots == 0 )
    {
        if ( worker.m_Ready )
        {
            RemoveWorker( worker, connection, "no-slots", false, actions );
        }
        return;
    }

    m_Scheduler.SetWorkerSlots( connection.GetIndex(), allowance );
    if ( worker.m_Ready == false )
    {
        worker.m_Ready = true;
        AStackString<> line;
        line.Format( "gateway worker-ready worker=%s slots=%u pool=%u", connection.GetAddress().Get(), allowance, poolSlots );
        actions.m_LogLines.EmplaceBack( line.Get() );
    }
    UpdatePool();
}

// RemoveWorker
//------------------------------------------------------------------------------
void Gateway::RemoveWorker( Worker & worker, DownstreamConnection & connection, const char * reason, bool lost, Actions & actions )
{
    // Out of the scheduler and the pool
    const uint32_t index = connection.GetIndex();
    if ( worker.m_Ready )
    {
        worker.m_Ready = false;
        Array<uint32_t> runningJobIds;
        m_Scheduler.RemoveWorker( index, runningJobIds );
        UpdatePool();
    }

    uint32_t numRunning = 0;
    for ( const Attempt & attempt : m_Attempts )
    {
        numRunning += ( ( attempt.m_WorkerIndex == index ) && ( attempt.m_Expired == false ) ) ? 1 : 0;
    }
    AStackString<> line;
    line.Format( "gateway %s worker=%s reason=%s running=%u", lost ? "worker-lost" : "worker-unready", connection.GetAddress().Get(), reason, numRunning );
    actions.m_LogLines.EmplaceBack( line.Get() );

    // A lost worker's jobs are tried elsewhere. One without slots finishes those it runs: they stay running in
    // the scheduler, and fill its slots when it is back. An expired attempt has no slot to give back either way,
    // so it goes, and its late result is dropped.
    for ( size_t i = m_Attempts.GetSize(); i > 0; --i )
    {
        const Attempt attempt = m_Attempts[ i - 1 ];
        if ( attempt.m_WorkerIndex != index )
        {
            continue;
        }
        if ( ( lost == false ) && ( attempt.m_Expired == false ) )
        {
            continue;
        }
        m_Attempts.EraseIndex( i - 1 );
        GatewayJob * job = attempt.m_Expired ? nullptr : FindJob( attempt.m_JobId );
        if ( job && FailAttempt( *job, index, "lost", actions ) )
        {
            GiveUpWithoutResult( *job, actions );
        }
    }
}

// FindPch
//------------------------------------------------------------------------------
/*virtual*/ bool Gateway::FindPch( uint64_t pchId, StoredPch & outPch, PchLease & outLease )
{
    MutexHolder mh( m_Mutex );
    return LookUpPch( pchId, outPch, outLease );
}

// FindToolchainFile
//------------------------------------------------------------------------------
/*virtual*/ bool Gateway::FindToolchainFile( uint64_t toolId, uint32_t fileId, AString & outPath, uint32_t & outSize )
{
    // The stash's index only: SendJob calls this with the gateway's mutex held
    return ( fileId == DownstreamConnection::kManifestFileId ) ? m_Stash.GetManifestFile( toolId, outPath, outSize )
                                                               : m_Stash.GetFile( toolId, fileId, outPath, outSize );
}

// GetToolchainNumFiles
//------------------------------------------------------------------------------
/*virtual*/ uint32_t Gateway::GetToolchainNumFiles( uint64_t toolId )
{
    return m_Stash.GetNumFiles( toolId ); // As FindToolchainFile
}

// Actions::~Actions
//------------------------------------------------------------------------------
Gateway::Actions::~Actions()
{
    for ( const GivenUp & givenUp : m_GivenUp )
    {
        FDELETE givenUp.m_Payload; // Unless Run sent it
    }
    for ( const LocalJob & localJob : m_LocalJobs )
    {
        FREE( localJob.m_Data ); // Unless Run handed it over
    }
}

// Run
//------------------------------------------------------------------------------
void Gateway::Run( Actions & actions )
{
    m_Stash.DeleteRemoved( actions.m_DeleteToolchains );
    for ( const uint64_t toolId : actions.m_ReleaseUses )
    {
        m_Stash.ReleaseUse( toolId );
    }
    for ( GatewayJob * job : actions.m_FreeJobs )
    {
        if ( job->m_ToolId != 0 )
        {
            m_Stash.ReleaseUse( job->m_ToolId );
        }
        FREE( job->m_Data );
        FDELETE job; // And its PCH lease
    }
    for ( Actions::LocalJob & localJob : actions.m_LocalJobs )
    {
        // Takes the Server's toolchain, local job, PCH cache and job queue locks; Forwarder::OnLocalJobEnded follows
        m_Server->RunLocalJob( Protocol::MsgJob( localJob.m_ToolId, localJob.m_ResultCompressionLevel ), localJob.m_Data, localJob.m_DataSize );
        FREE( localJob.m_Data );
        localJob.m_Data = nullptr;
    }
    for ( Actions::GivenUp & givenUp : actions.m_GivenUp )
    {
        m_Server->QueueResult( givenUp.m_ClientId, givenUp.m_Compressed, Move( *givenUp.m_Payload ), Protocol::kResultReturnedByGateway );
        FDELETE givenUp.m_Payload;
        givenUp.m_Payload = nullptr;
    }
    for ( const uint64_t pchId : actions.m_DropPchs )
    {
        m_Server->DropStoredPch( pchId );
    }
    for ( const uint32_t clientId : actions.m_Disconnects )
    {
        m_Server->DisconnectClient( clientId );
    }
    for ( const Actions::PchAsk & ask : actions.m_PchAsks )
    {
        m_Server->AskClientForPch( ask.m_ClientId, ask.m_PchId );
    }
    for ( const Actions::ToolchainRequest & request : actions.m_ToolchainRequests )
    {
        if ( request.m_FileId == DownstreamConnection::kManifestFileId )
        {
            m_Server->SendToClient( request.m_ClientId, Protocol::MsgRequestManifest( request.m_ToolId ) );
        }
        else
        {
            m_Server->SendToClient( request.m_ClientId, Protocol::MsgRequestFile( request.m_ToolId, request.m_FileId ) );
        }
    }
    for ( const AString & line : actions.m_LogLines )
    {
        m_Server->GetLog().Write( "%s", line.Get() );
    }
    for ( const Actions::Demand & demand : actions.m_Demands )
    {
        demand.m_Connection->UpdateDemand( demand.m_Waiting, demand.m_InFlight );
    }
}

// Dispatch
//------------------------------------------------------------------------------
void Gateway::Dispatch( Actions & actions )
{
    if ( m_ShuttingDown )
    {
        return;
    }
    const WorkerAffinity affinity( *this );
    GatewayScheduler::Assignment assignment;
    while ( m_Scheduler.Next( affinity, assignment ) )
    {
        const bool local = ( assignment.m_WorkerId == kLocalWorkerIndex );
        GatewayJob * job = FindJob( assignment.m_JobId );
        DownstreamConnection * connection = local ? nullptr : m_Workers[ assignment.m_WorkerId - 1 ].m_Connection;
        ASSERT( job && ( local || connection ) );

        const uint32_t attemptId = m_NextAttemptId++;
        ++job->m_NumAttempts;
        Attempt & attempt = m_Attempts.EmplaceBack();
        attempt.m_AttemptId = attemptId;
        attempt.m_JobId = job->m_Id;
        attempt.m_WorkerIndex = assignment.m_WorkerId;

        // A copy with the attempt's id, so the job can be sent again
        void * data = ALLOC( job->m_DataSize );
        memcpy( data, job->m_Data, job->m_DataSize );
        GatewayWire::SetJobId( data, job->m_DataSize, attemptId );
        ++m_Forwarded;
        AStackString<> line;
        if ( local )
        {
            // Handed to the Server once m_Mutex is released
            actions.m_LocalJobs.Append( Actions::LocalJob{ job->m_ToolId, job->m_ResultCompressionLevel, data, job->m_DataSize } );
            line.Format( "gateway job-forward client=%u job=%u attempt=%u worker=local pch=0x%016" PRIx64,
                         job->m_ClientId, job->m_ClientJobId, attemptId, job->m_PchId );
            actions.m_LogLines.EmplaceBack( line.Get() );
            continue;
        }
        ConstMemoryStream stream;
        stream.Replace( data, job->m_DataSize, true );

        StoredPch pch;
        PchLease lease;
        const bool hasPch = ( job->m_PchId != 0 ) && LookUpPch( job->m_PchId, pch, lease );
        const bool sendsPch = hasPch && ( connection->GetAffinity( job->m_PchId, 0 ) == GatewayScheduler::AFFINITY_NONE );
        connection->SendJob( Protocol::MsgJob( job->m_ToolId, job->m_ResultCompressionLevel ), Move( stream ), hasPch ? &pch : nullptr, Move( lease ) );

        line.Format( "gateway job-forward client=%u job=%u attempt=%u worker=%s pch=0x%016" PRIx64,
                     job->m_ClientId, job->m_ClientJobId, attemptId, connection->GetAddress().Get(), job->m_PchId );
        actions.m_LogLines.EmplaceBack( line.Get() );
        if ( sendsPch )
        {
            m_PchBytesForwarded += pch.m_CompressedSize;
            line.Format( "gateway pch-forward pch=0x%016" PRIx64 " worker=%s bytes=%u", pch.m_Id, connection->GetAddress().Get(), pch.m_CompressedSize );
            actions.m_LogLines.EmplaceBack( line.Get() );
        }
    }
}

// Schedule
//------------------------------------------------------------------------------
void Gateway::Schedule( GatewayJob & job )
{
    job.m_Parked = false;
    m_Scheduler.AddJob( job.m_Id, job.m_ClientId, job.m_PchId, job.m_ToolId );
}

// Park
//------------------------------------------------------------------------------
void Gateway::Park( GatewayJob & job, Actions & actions )
{
    job.m_Parked = true;
    ++m_NumParked;

    // The Server asks a client once until the PCH arrives, and not while another client is sending it
    if ( ( job.m_PchId != 0 ) && ( IsPchStored( job.m_PchId ) == false ) )
    {
        actions.m_PchAsks.Append( Actions::PchAsk{ job.m_ClientId, job.m_PchId } );

        // Stored but not yet measured: it is measured next
        KnownPch * known = FindKnownPch( job.m_PchId );
        if ( known && ( known->m_AwaitedSince == 0 ) )
        {
            known->m_AwaitedSince = Timer::GetNow();
        }
    }
    if ( ( job.m_ToolId != 0 ) && ( m_Stash.IsComplete( job.m_ToolId ) == false ) && ( IsUploaded( job.m_ToolId ) == false ) )
    {
        RequestToolchain( job.m_ClientId, job.m_ToolId, ( m_Stash.HasManifest( job.m_ToolId ) == false ), actions );
    }
}

// ReleaseParked
//------------------------------------------------------------------------------
void Gateway::ReleaseParked()
{
    for ( GatewayJob * job : m_Jobs )
    {
        if ( job->m_Parked && IsReady( *job ) )
        {
            --m_NumParked;
            Schedule( *job );
        }
    }
}

// IsReady
//------------------------------------------------------------------------------
bool Gateway::IsReady( const GatewayJob & job )
{
    return ( ( job.m_ToolId == 0 ) || m_Stash.IsComplete( job.m_ToolId ) ) &&
           ( ( job.m_PchId == 0 ) || IsPchStored( job.m_PchId ) );
}

// IsPchStored
//------------------------------------------------------------------------------
bool Gateway::IsPchStored( uint64_t pchId )
{
    KnownPch * known = FindKnownPch( pchId );
    if ( ( known == nullptr ) || ( known->m_Measured == false ) )
    {
        return false;
    }
    AStackString<> path;
    if ( m_Server->FindCachedPch( pchId, path ) )
    {
        return true;
    }
    m_Pchs.Erase( known ); // Gone from the store, which reports it with no lock held, so it may not have yet
    return false;
}

// LookUpPch
//------------------------------------------------------------------------------
bool Gateway::LookUpPch( uint64_t pchId, StoredPch & outPch, PchLease & outLease )
{
    KnownPch * known = FindKnownPch( pchId );
    if ( ( known == nullptr ) || ( known->m_Measured == false ) )
    {
        return false;
    }

    // Taken before the store is looked in, so the PCH cannot be evicted after it is found
    PchLease lease = m_Server->AcquirePchLease( pchId );
    AStackString<> path;
    if ( m_Server->FindCachedPch( pchId, path ) == false )
    {
        m_Pchs.Erase( known );
        return false;
    }
    outPch.m_Id = pchId;
    outPch.m_File = path;
    outPch.m_UncompressedSize = known->m_UncompressedSize;
    outPch.m_CompressedSize = known->m_CompressedSize;
    outLease = Move( lease );
    return true;
}

// RequestToolchain
//------------------------------------------------------------------------------
void Gateway::RequestToolchain( uint32_t clientId, uint64_t toolId, bool askForManifest, Actions & actions )
{
    // The client answers from the job it sent (Client: Process( MsgRequestManifest ))
    Upload upload;
    upload.m_ToolId = toolId;
    upload.m_ClientId = clientId;
    upload.m_Requested = true;
    m_Uploads.Append( upload );
    if ( askForManifest )
    {
        actions.m_ToolchainRequests.Append( Actions::ToolchainRequest{ clientId, toolId, DownstreamConnection::kManifestFileId } );
        ++m_ToolchainRequests;
        AStackString<> line;
        line.Format( "gateway toolchain-request client=%u tool=0x%016" PRIx64 " files=0", clientId, toolId );
        actions.m_LogLines.EmplaceBack( line.Get() );
        return;
    }
    RequestMissingFiles( m_Uploads.Top(), actions );
}

// RequestMissingFiles
//------------------------------------------------------------------------------
void Gateway::RequestMissingFiles( Upload & upload, Actions & actions )
{
    upload.m_FilesRequested = true;
    Array<uint32_t> fileIds;
    m_Stash.GetMissingFiles( upload.m_ToolId, fileIds );
    for ( const uint32_t fileId : fileIds )
    {
        actions.m_ToolchainRequests.Append( Actions::ToolchainRequest{ upload.m_ClientId, upload.m_ToolId, fileId } );
    }
    m_ToolchainRequests += (uint32_t)fileIds.GetSize();
    AStackString<> line;
    line.Format( "gateway toolchain-request client=%u tool=0x%016" PRIx64 " files=%u", upload.m_ClientId, upload.m_ToolId, (uint32_t)fileIds.GetSize() );
    actions.m_LogLines.EmplaceBack( line.Get() );
}

// OnToolchainPayload
//------------------------------------------------------------------------------
void Gateway::OnToolchainPayload( uint32_t clientId, uint64_t toolId, Actions & actions )
{
    if ( m_ShuttingDown )
    {
        return;
    }

    // A copy that arrives while another is being written reports the state from before that write, so
    // the stash is asked again here
    if ( m_Stash.IsComplete( toolId ) )
    {
        for ( size_t i = m_Uploads.GetSize(); i > 0; --i )
        {
            if ( m_Uploads[ i - 1 ].m_ToolId == toolId )
            {
                m_Uploads.EraseIndex( i - 1 );
            }
        }
        ReleaseParked();
        Dispatch( actions );
        return;
    }

    // The manifest the gateway asked for: the files are asked for next
    Upload * upload = FindUpload( clientId, toolId );
    if ( upload && upload->m_Requested && ( upload->m_FilesRequested == false ) && m_Stash.HasManifest( toolId ) )
    {
        RequestMissingFiles( *upload, actions );
    }
}

// OnUnknownToolchain
//------------------------------------------------------------------------------
void Gateway::OnUnknownToolchain( uint32_t clientId, uint64_t toolId, Actions & actions )
{
    if ( m_ShuttingDown )
    {
        return;
    }
    Upload * upload = FindUpload( clientId, toolId );
    if ( upload && upload->m_Requested && ( upload->m_FilesRequested == false ) )
    {
        return; // Its manifest was asked for: what it pushed before is asked for again after it
    }
    if ( upload )
    {
        m_Uploads.Erase( upload );
    }
    if ( IsUploaded( toolId ) )
    {
        return; // Another client sends it
    }

    // A client with a job waiting for it is asked for it, this one first
    const GatewayJob * waiting = nullptr;
    for ( const GatewayJob * job : m_Jobs )
    {
        if ( job->m_Parked && ( job->m_ToolId == toolId ) && ( ( waiting == nullptr ) || ( job->m_ClientId == clientId ) ) )
        {
            waiting = job;
        }
    }
    if ( waiting )
    {
        RequestToolchain( waiting->m_ClientId, toolId, ( m_Stash.HasManifest( toolId ) == false ), actions );
    }
}

// OnToolchainNotStored
//------------------------------------------------------------------------------
void Gateway::OnToolchainNotStored( uint64_t toolId, Actions & actions )
{
    if ( m_ShuttingDown )
    {
        return;
    }
    for ( size_t i = m_Uploads.GetSize(); i > 0; --i )
    {
        if ( m_Uploads[ i - 1 ].m_ToolId == toolId )
        {
            m_Uploads.EraseIndex( i - 1 );
        }
    }

    // Each goes back to its client flagged, so the client requeues it and keeps the gateway
    Array<GatewayJob *> givenUp;
    for ( GatewayJob * job : m_Jobs )
    {
        if ( job->m_Parked && ( job->m_ToolId == toolId ) )
        {
            givenUp.Append( job );
        }
    }
    for ( GatewayJob * job : givenUp )
    {
        ++m_GiveUps;
        GiveUpWithoutResult( *job, actions, "its toolchain cannot be stored at the gateway" );
        RemoveJob( job, actions );
    }
    if ( givenUp.IsEmpty() == false )
    {
        AStackString<> line;
        line.Format( "gateway jobs-giveup reason=toolchain n=%u", (uint32_t)givenUp.GetSize() );
        actions.m_LogLines.EmplaceBack( line.Get() );
    }
}

// FindUpload
//------------------------------------------------------------------------------
Gateway::Upload * Gateway::FindUpload( uint32_t clientId, uint64_t toolId )
{
    for ( Upload & upload : m_Uploads )
    {
        if ( ( upload.m_ToolId == toolId ) && ( upload.m_ClientId == clientId ) )
        {
            return &upload;
        }
    }
    return nullptr;
}

// IsUploaded
//------------------------------------------------------------------------------
bool Gateway::IsUploaded( uint64_t toolId ) const
{
    for ( const Upload & upload : m_Uploads )
    {
        if ( upload.m_ToolId == toolId )
        {
            return true;
        }
    }
    return false;
}

// RemoveJob
//------------------------------------------------------------------------------
void Gateway::RemoveJob( GatewayJob * job, Actions & actions )
{
    const bool found = m_Jobs.FindAndErase( job );
    ASSERT( found );
    (void)found;
    m_HeldJobBytes -= job->m_DataSize;
    if ( job->m_Parked )
    {
        --m_NumParked;
    }
    Client * client = FindClient( job->m_ClientId );
    if ( client && ( client->m_NumJobs > 0 ) )
    {
        --client->m_NumJobs;
    }
    actions.m_FreeJobs.Append( job );
}

// FailAttempt
//------------------------------------------------------------------------------
bool Gateway::FailAttempt( GatewayJob & job, uint32_t workerIndex, const char * reason, Actions & actions )
{
    const bool clientIsConnected = ( FindClient( job.m_ClientId ) != nullptr );
    AStackString<> line;
    if ( m_Scheduler.OnFailed( job.m_Id, workerIndex ) == GatewayScheduler::OUTCOME_RETRY )
    {
        ++m_Retries;
        line.Format( "gateway job-retry client=%u job=%u reason=%s attempt=%u", job.m_ClientId, job.m_ClientJobId, reason, job.m_NumAttempts );
        actions.m_LogLines.EmplaceBack( line.Get() );
        return false;
    }

    // Its attempts are used up, or nobody waits for it
    RemoveJob( &job, actions );
    if ( clientIsConnected == false )
    {
        return false;
    }
    ++m_GiveUps;
    line.Format( "gateway job-giveup client=%u job=%u", job.m_ClientId, job.m_ClientJobId );
    actions.m_LogLines.EmplaceBack( line.Get() );
    return true;
}

// GiveUpWithoutResult
//------------------------------------------------------------------------------
void Gateway::GiveUpWithoutResult( GatewayJob & job, Actions & actions, const char * why )
{
    AStackString<> message;
    if ( why )
    {
        message = why;
    }
    else
    {
        message.Format( "no result after %u attempts", job.m_NumAttempts );
    }
    MemoryStream result;
    GatewayWire::WriteLostResult( result, job.m_ClientJobId, message.Get() );
    actions.m_GivenUp.Append( Actions::GivenUp{ job.m_ClientId, false, FNEW( MemoryStream( Move( result ) ) ) } );
}

// ExpireHungAttempts
//------------------------------------------------------------------------------
void Gateway::ExpireHungAttempts( Actions & actions )
{
    // An expired attempt keeps its slot taken until its late result arrives, or its worker is lost. One on a
    // worker out of the scheduler holds none (its slots, once it is back, are filled by its running jobs), so it
    // goes, and its late result is dropped.
    for ( size_t i = 0; i < m_Attempts.GetSize(); )
    {
        Attempt & attempt = m_Attempts[ i ];
        if ( attempt.m_Expired || ( attempt.m_Started.GetElapsed() < m_Config.m_JobTimeoutSeconds ) )
        {
            ++i;
            continue;
        }
        const uint32_t workerIndex = attempt.m_WorkerIndex;
        const uint32_t jobId = attempt.m_JobId;
        const bool inScheduler = ( workerIndex == kLocalWorkerIndex ) ? ( m_LocalSlots > 0 )
                                                                      : ( ( workerIndex <= m_Workers.GetSize() ) && m_Workers[ workerIndex - 1 ].m_Ready );
        if ( inScheduler )
        {
            attempt.m_Expired = true;
            ++i;
        }
        else
        {
            m_Attempts.EraseIndex( i );
        }
        ++m_Timeouts;
        GatewayJob * job = FindJob( jobId );
        if ( job && FailAttempt( *job, workerIndex, "timeout", actions ) )
        {
            GiveUpWithoutResult( *job, actions );
        }
    }
    Dispatch( actions );
}

// GiveUpJobsNoWorkerCanTake
//------------------------------------------------------------------------------
void Gateway::GiveUpJobsNoWorkerCanTake( Actions & actions )
{
    // The workers that can take a job at all: ready, with a slot in their pool that no expired attempt holds.
    // A downstream worker's allowance follows the gateway's demand, so its pool counts.
    Array<uint32_t> ableWorkers;
    if ( m_LocalSlots > CountExpiredAttempts( kLocalWorkerIndex ) )
    {
        ableWorkers.Append( kLocalWorkerIndex );
    }
    for ( size_t i = 0; i < m_Workers.GetSize(); ++i )
    {
        const Worker & worker = m_Workers[ i ];
        const uint32_t index = (uint32_t)( i + 1 );
        if ( worker.m_Ready && worker.m_Connection && worker.m_Connection->IsClosing() )
        {
            return; // Until it is lost, which decides what happens to the jobs it runs, no job counts as without a worker
        }
        if ( worker.m_Ready && worker.m_Connection && ( worker.m_Connection->GetPoolSlots() > CountExpiredAttempts( index ) ) )
        {
            ableWorkers.Append( index );
        }
    }

    // Each waiting job's bound starts when none of them can take it, and starts again once one can. m_Jobs is in
    // the order of the job ids, so it is walked along with the sorted waiting ids.
    Array<uint32_t> waitingJobIds;
    m_Scheduler.GetWaitingJobIds( waitingJobIds );
    waitingJobIds.Sort();
    const WorkerAffinity affinity( *this );
    const int64_t now = Timer::GetNow(); // One time for the tick, so the jobs that lost their workers together go together
    const int64_t bound = (int64_t)( (double)m_Config.m_NoWorkerGiveUpSeconds * (double)Timer::GetFrequency() );
    Array<GatewayJob *> givenUp;
    size_t w = 0;
    for ( GatewayJob * job : m_Jobs )
    {
        while ( ( w < waitingJobIds.GetSize() ) && ( waitingJobIds[ w ] < job->m_Id ) )
        {
            ++w;
        }
        bool noWorker = ( w < waitingJobIds.GetSize() ) && ( waitingJobIds[ w ] == job->m_Id );
        for ( size_t i = 0; noWorker && ( i < ableWorkers.GetSize() ); ++i )
        {
            noWorker = ( affinity( ableWorkers[ i ], job->m_PchId, job->m_ToolId ) == GatewayScheduler::AFFINITY_UNABLE );
        }
        if ( noWorker == false )
        {
            job->m_NoWorker = false;
        }
        else if ( job->m_NoWorker == false )
        {
            job->m_NoWorker = true;
            job->m_NoWorkerSince = now;
        }
        else if ( ( now - job->m_NoWorkerSince ) >= bound )
        {
            givenUp.Append( job );
        }
    }
    if ( givenUp.IsEmpty() )
    {
        return;
    }

    // Each goes back to its client flagged, so the client requeues it and keeps the gateway
    for ( GatewayJob * job : givenUp )
    {
        VERIFY( m_Scheduler.RemoveWaitingJob( job->m_Id ) );
        ++m_GiveUps;
        GiveUpWithoutResult( *job, actions );
        RemoveJob( job, actions );
    }
    AStackString<> line;
    line.Format( "gateway jobs-giveup reason=no-worker n=%u", (uint32_t)givenUp.GetSize() );
    actions.m_LogLines.EmplaceBack( line.Get() );
}

// CountExpiredAttempts
//------------------------------------------------------------------------------
uint32_t Gateway::CountExpiredAttempts( uint32_t workerIndex ) const
{
    uint32_t count = 0;
    for ( const Attempt & attempt : m_Attempts )
    {
        count += ( attempt.m_Expired && ( attempt.m_WorkerIndex == workerIndex ) ) ? 1 : 0;
    }
    return count;
}

// UpdateLocalWorker
//------------------------------------------------------------------------------
void Gateway::UpdateLocalWorker( Actions & actions )
{
    // Its slots are what the worker settings give it now, not an allowance
    const uint32_t slots = m_ShuttingDown ? 0 : m_Server->GetLocalSlots();
    if ( slots == m_LocalSlots )
    {
        return;
    }
    AStackString<> line;
    if ( slots > 0 )
    {
        m_Scheduler.SetWorkerSlots( kLocalWorkerIndex, slots ); // Its running jobs fill its slots
        if ( m_LocalSlots == 0 )
        {
            line.Format( "gateway worker-ready worker=local slots=%u", slots );
            actions.m_LogLines.EmplaceBack( line.Get() );
        }
    }
    else
    {
        // Out of the scheduler, and its jobs run on. An expired attempt holds no slot once it is back, so it goes.
        Array<uint32_t> runningJobIds;
        m_Scheduler.RemoveWorker( kLocalWorkerIndex, runningJobIds );
        for ( size_t i = m_Attempts.GetSize(); i > 0; --i )
        {
            if ( m_Attempts[ i - 1 ].m_Expired && ( m_Attempts[ i - 1 ].m_WorkerIndex == kLocalWorkerIndex ) )
            {
                m_Attempts.EraseIndex( i - 1 );
            }
        }
        line.Format( "gateway worker-unready worker=local reason=no-slots running=%u", (uint32_t)runningJobIds.GetSize() );
        actions.m_LogLines.EmplaceBack( line.Get() );
    }
    m_LocalSlots = slots;
    UpdatePool();
}

// CollectDemands
//------------------------------------------------------------------------------
void Gateway::CollectDemands( Actions & actions )
{
    // A worker's demand is the jobs it could take that wait, and those it runs, expired or not. Every worker
    // is reported: one that is not ready (or has no pool) has nothing waiting for it.
    const WorkerAffinity affinity( *this );
    for ( size_t i = 0; i < m_Workers.GetSize(); ++i )
    {
        DownstreamConnection * connection = m_Workers[ i ].m_Connection;
        if ( connection == nullptr )
        {
            continue;
        }
        const uint32_t index = (uint32_t)( i + 1 );
        uint32_t inFlight = 0;
        for ( const Attempt & attempt : m_Attempts )
        {
            inFlight += ( attempt.m_WorkerIndex == index ) ? 1 : 0;
        }
        actions.m_Demands.Append( Actions::Demand{ connection, m_Scheduler.GetNumWaitingFor( index, affinity ), inFlight } );
    }
}

// UpdatePool
//------------------------------------------------------------------------------
void Gateway::UpdatePool()
{
    uint32_t slots = 0;
    if ( m_ShuttingDown == false )
    {
        slots = m_LocalSlots;
        for ( const Worker & worker : m_Workers )
        {
            if ( worker.m_Ready && worker.m_Connection && ( worker.m_Connection->HasEnded() == false ) )
            {
                slots += worker.m_Connection->GetPoolSlots();
            }
        }
    }
    m_PoolSlots.Store( slots );
}

// FindClient
//------------------------------------------------------------------------------
Gateway::Client * Gateway::FindClient( uint32_t clientId )
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

// FindOrAddClient
//------------------------------------------------------------------------------
Gateway::Client & Gateway::FindOrAddClient( uint32_t clientId )
{
    Client * found = FindClient( clientId );
    if ( found )
    {
        return *found;
    }
    Client & client = m_Clients.EmplaceBack();
    client.m_Id = clientId;
    return client;
}

// FindJob
//------------------------------------------------------------------------------
Gateway::GatewayJob * Gateway::FindJob( uint32_t jobId ) const
{
    for ( GatewayJob * job : m_Jobs )
    {
        if ( job->m_Id == jobId )
        {
            return job;
        }
    }
    return nullptr;
}

// FindWorker
//------------------------------------------------------------------------------
Gateway::Worker * Gateway::FindWorker( const DownstreamConnection & connection )
{
    for ( Worker & worker : m_Workers )
    {
        if ( worker.m_Connection == &connection )
        {
            return &worker;
        }
    }
    return nullptr;
}

// FindKnownPch
//------------------------------------------------------------------------------
Gateway::KnownPch * Gateway::FindKnownPch( uint64_t pchId )
{
    for ( KnownPch & pch : m_Pchs )
    {
        if ( pch.m_Id == pchId )
        {
            return &pch;
        }
    }
    return nullptr;
}

// GetStatsLocked
//------------------------------------------------------------------------------
void Gateway::GetStatsLocked( Stats & outStats ) const
{
    outStats.m_Clients = (uint32_t)m_Clients.GetSize();
    outStats.m_Workers = (uint32_t)m_Workers.GetSize();
    outStats.m_ReadyWorkers = 0;
    for ( const Worker & worker : m_Workers )
    {
        outStats.m_ReadyWorkers += worker.m_Ready ? 1 : 0;
    }
    outStats.m_PoolSlots = m_PoolSlots.Load();
    outStats.m_LocalSlots = m_LocalSlots;
    outStats.m_RunningLocally = 0;
    for ( const Attempt & attempt : m_Attempts )
    {
        outStats.m_RunningLocally += ( attempt.m_WorkerIndex == kLocalWorkerIndex ) ? 1 : 0;
    }
    outStats.m_Waiting = m_Scheduler.GetNumWaiting();
    outStats.m_Parked = m_NumParked;
    outStats.m_Running = m_Scheduler.GetNumRunning();
    outStats.m_HeldJobBytes = m_HeldJobBytes;
    outStats.m_Forwarded = m_Forwarded;
    outStats.m_Retries = m_Retries;
    outStats.m_GiveUps = m_GiveUps;
    outStats.m_Timeouts = m_Timeouts;
    outStats.m_PchBytesForwarded = m_PchBytesForwarded;
    outStats.m_ToolchainRequests = m_ToolchainRequests;
    outStats.m_KnownPchs = (uint32_t)m_Pchs.GetSize();
}

// FormatStats
//------------------------------------------------------------------------------
void Gateway::FormatStats( AString & outLine ) const
{
    Stats stats;
    GetStatsLocked( stats );
    outLine.Format( "gateway stats clients=%u workers=%u/%u pool=%u waiting=%u parked=%u running=%u heldMiB=%u forwarded=%u retries=%u giveups=%u timeouts=%u pchMiB=%u local=%u local-running=%u",
                    stats.m_Clients,
                    stats.m_ReadyWorkers,
                    stats.m_Workers,
                    stats.m_PoolSlots,
                    stats.m_Waiting,
                    stats.m_Parked,
                    stats.m_Running,
                    (uint32_t)( stats.m_HeldJobBytes / MEGABYTE ),
                    stats.m_Forwarded,
                    stats.m_Retries,
                    stats.m_GiveUps,
                    stats.m_Timeouts,
                    (uint32_t)( stats.m_PchBytesForwarded / MEGABYTE ),
                    stats.m_LocalSlots,
                    stats.m_RunningLocally );
}

// HousekeepingThreadFuncStatic
//------------------------------------------------------------------------------
/*static*/ uint32_t Gateway::HousekeepingThreadFuncStatic( void * param )
{
    PROFILE_SET_THREAD_NAME( "GatewayHousekeeping" );
    static_cast<Gateway *>( param )->HousekeepingThreadFunc();
    return 0;
}

// HousekeepingThreadFunc
//------------------------------------------------------------------------------
void Gateway::HousekeepingThreadFunc()
{
    // Often enough for the demand reports, and for the watchdog, the reconnect delay and the no-worker bound to be
    // kept to within a quarter of the shortest
    const float shortest = Math::Min( Math::Min( m_Config.m_JobTimeoutSeconds, m_Config.m_ReconnectSeconds ), m_Config.m_NoWorkerGiveUpSeconds );
    const uint32_t tickMs = Math::Clamp( (uint32_t)( shortest * 1000.0f / 4.0f ), 10u, kDemandTickMs );

    Timer statsTimer;
    while ( m_Quit.Load() == false )
    {
        m_HousekeepingSemaphore.Wait( tickMs );
        if ( m_Quit.Load() )
        {
            break;
        }

        ReconnectLostWorkers();

        Actions actions;
        {
            MutexHolder mh( m_Mutex );
            if ( m_ShuttingDown == false )
            {
                UpdateLocalWorker( actions );
                ExpireHungAttempts( actions ); // And dispatches, so a toolchain the local pool has synchronized is used
                GiveUpJobsNoWorkerCanTake( actions );
                CollectDemands( actions );
            }
            UpdatePool();
            if ( statsTimer.GetElapsed() >= m_Config.m_StatsSeconds )
            {
                AString line;
                FormatStats( line );
                actions.m_LogLines.Append( line );
                statsTimer.Restart();
            }
        }
        Run( actions );
    }
}

// ReconnectLostWorkers
//------------------------------------------------------------------------------
void Gateway::ReconnectLostWorkers()
{
    // A connection is created and destroyed outside the mutex, as its teardown calls back
    Array<uint32_t> indexes;
    Array<DownstreamConnection *> ended;
    {
        MutexHolder mh( m_Mutex );
        if ( m_ShuttingDown )
        {
            return;
        }
        for ( size_t i = 0; i < m_Workers.GetSize(); ++i )
        {
            Worker & worker = m_Workers[ i ];
            if ( worker.m_Lost && ( worker.m_LostTimer.GetElapsed() >= m_Config.m_ReconnectSeconds ) )
            {
                ended.Append( worker.m_Connection ); // Out of the table, so what it still reports is ignored
                worker.m_Connection = nullptr;
                indexes.Append( (uint32_t)i );
            }
        }
    }
    for ( DownstreamConnection * connection : ended )
    {
        FDELETE connection;
    }

    // In the table before it starts, so it is known when it calls back
    for ( const uint32_t i : indexes )
    {
        DownstreamConnection * connection = FNEW( DownstreamConnection( *this, m_Config.m_Workers[ i ], i + 1 ) );
        {
            MutexHolder mh( m_Mutex );
            Worker & worker = m_Workers[ i ];
            worker.m_Connection = connection;
            worker.m_Lost = false;
        }
        connection->Start();
    }
}

// MeasureThreadFuncStatic
//------------------------------------------------------------------------------
/*static*/ uint32_t Gateway::MeasureThreadFuncStatic( void * param )
{
    PROFILE_SET_THREAD_NAME( "GatewayMeasure" );
    static_cast<Gateway *>( param )->MeasureStoredPchs();
    return 0;
}

// MeasureStoredPchs
//------------------------------------------------------------------------------
void Gateway::MeasureStoredPchs()
{
    const Timer timer;
    uint32_t numMeasured = 0;
    uint32_t numFailed = 0;
    while ( m_Quit.Load() == false )
    {
        uint64_t pchId = 0;
        {
            MutexHolder mh( m_Mutex );
            const KnownPch * next = nullptr;
            for ( const KnownPch & known : m_Pchs )
            {
                if ( known.m_Measured )
                {
                    continue;
                }
                if ( next == nullptr )
                {
                    next = &known;
                }
                else if ( ( known.m_AwaitedSince != 0 ) && ( ( next->m_AwaitedSince == 0 ) || ( known.m_AwaitedSince < next->m_AwaitedSince ) ) )
                {
                    next = &known;
                }
            }
            pchId = next ? next->m_Id : 0;
        }
        if ( pchId == 0 )
        {
            m_Server->GetLog().Write( "gateway pch-measured n=%u failed=%u ms=%u", numMeasured, numFailed, (uint32_t)timer.GetElapsedMS() );
            return;
        }

        // The disk is read with no lock held, and the lease keeps the PCH in the store meanwhile
        PchLease lease = m_Server->AcquirePchLease( pchId );
        AStackString<> path;
        FileIO::FileInfo info;
        uint32_t uncompressedSize = 0;
        const bool found = m_Server->FindCachedPch( pchId, path );
        if ( s_MeasureTestHook )
        {
            s_MeasureTestHook( pchId, m_Quit );
        }
        const bool measured = found && FileIO::GetFileInfo( path, info ) &&
                              PchBlob::MeasureFile( path.Get(), pchId, &m_Quit, uncompressedSize );
        if ( m_Quit.Load() )
        {
            return;
        }
        numMeasured += measured ? 1 : 0;
        numFailed += ( found && ( measured == false ) ) ? 1 : 0;

        Actions actions;
        {
            MutexHolder mh( m_Mutex );
            KnownPch * known = FindKnownPch( pchId );
            if ( known && ( known->m_Measured == false ) ) // Unless it arrived again meanwhile
            {
                if ( found == false )
                {
                    m_Pchs.Erase( known );
                }
                else if ( measured == false )
                {
                    m_Pchs.Erase( known ); // Not what its id says
                    actions.m_DropPchs.Append( pchId );
                }
                else
                {
                    known->m_UncompressedSize = uncompressedSize;
                    known->m_CompressedSize = (uint32_t)info.m_Size;
                    known->m_Measured = true;
                    if ( known->m_AwaitedSince != 0 )
                    {
                        const float waitedMs = (float)( Timer::GetNow() - known->m_AwaitedSince ) * Timer::GetFrequencyInvFloatMS();
                        AStackString<> line;
                        line.Format( "gateway pch-awaited pch=0x%016" PRIx64 " waited-ms=%u", pchId, (uint32_t)waitedMs );
                        actions.m_LogLines.EmplaceBack( line.Get() );
                    }
                    ReleaseParked();
                    Dispatch( actions );
                }
            }
        }
        Run( actions );

        // Once it has gone from the store, the clients of the jobs waiting for it can be asked for it
        if ( actions.m_DropPchs.IsEmpty() == false )
        {
            Actions asks;
            {
                MutexHolder mh( m_Mutex );
                for ( const GatewayJob * job : m_Jobs )
                {
                    if ( job->m_Parked && ( job->m_PchId == pchId ) )
                    {
                        asks.m_PchAsks.Append( Actions::PchAsk{ job->m_ClientId, pchId } );
                    }
                }
            }
            Run( asks );
        }
    }
}

//------------------------------------------------------------------------------
