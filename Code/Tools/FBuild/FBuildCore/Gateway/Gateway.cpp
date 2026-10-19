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
// The scheduler's view of a downstream worker. Called with m_Mutex held.
class Gateway::WorkerAffinity
{
public:
    explicit WorkerAffinity( const Gateway & gateway ) : m_Gateway( gateway ) {}
    WorkerAffinity & operator=( const WorkerAffinity & other ) = delete;

    GatewayScheduler::Affinity operator()( uint32_t workerId, uint64_t pchId, uint64_t toolId ) const
    {
        if ( ( workerId == kLocalWorkerIndex ) || ( workerId > m_Gateway.m_Workers.GetSize() ) )
        {
            return GatewayScheduler::AFFINITY_UNABLE;
        }
        const Worker & worker = m_Gateway.m_Workers[ workerId - 1 ];
        return ( worker.m_Ready && worker.m_Connection ) ? worker.m_Connection->GetAffinity( pchId, toolId )
                                                         : GatewayScheduler::AFFINITY_UNABLE;
    }

private:
    const Gateway & m_Gateway;
};

// CONSTRUCTOR
//------------------------------------------------------------------------------
Gateway::Gateway( const GatewayConfig & config )
    : m_Config( config )
    , m_Stash( GetStashDirectory( config.m_CacheDir ) )
{
    m_Server = FNEW( Server( *this, config.m_CacheDir ) );

    // What the store held at start never reaches OnPchStored: the housekeeping thread measures it
    Array<uint64_t> pchIds;
    m_Server->GetCachedPchIds( pchIds );
    for ( const uint64_t pchId : pchIds )
    {
        KnownPch & pch = m_Pchs.EmplaceBack();
        pch.m_Id = pchId;
    }

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
    client.m_OfferedSlots = m_PoolSlots.Load(); // As the ack just sent it (Server::Process( MsgConnection ))
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

    Actions actions;
    {
        MutexHolder mh( m_Mutex );
        Client * client = m_ShuttingDown ? nullptr : &FindOrAddClient( clientId );
        if ( client == nullptr )
        {
            actions.m_FreeJobs.Append( job );
        }
        else if ( client->m_NumJobs >= ( kMaxHeldJobsPerSlot * client->m_OfferedSlots ) )
        {
            actions.m_FreeJobs.Append( job );
            actions.m_Disconnects.Append( clientId );
            AStackString<> line;
            line.Format( "gateway client-dropped client=%u reason=held-jobs held=%u", clientId, client->m_NumJobs );
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
    if ( m_Stash.AddManifest( toolId, payload, payloadSize ) == ToolchainStash::STASH_INVALID )
    {
        m_Server->DisconnectClient( clientId );
        return;
    }
    Actions actions;
    {
        MutexHolder mh( m_Mutex );
        OnToolchainPayload( clientId, toolId, actions );
    }
    Run( actions );
}

// OnClientFile
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnClientFile( uint32_t clientId, uint64_t toolId, uint32_t fileId, const void * payload, size_t payloadSize )
{
    if ( m_Stash.AddFile( toolId, fileId, payload, payloadSize ) == ToolchainStash::STASH_INVALID )
    {
        m_Server->DisconnectClient( clientId );
        return;
    }
    Actions actions;
    {
        MutexHolder mh( m_Mutex );
        OnToolchainPayload( clientId, toolId, actions );
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
                 ( actions.m_DropIncomplete.Find( toolId ) == nullptr ) )
            {
                actions.m_DropIncomplete.Append( toolId );
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
                if ( actions.m_DropIncomplete.Find( job->m_ToolId ) && ( IsUploaded( job->m_ToolId ) == false ) )
                {
                    RequestToolchain( job->m_ClientId, job->m_ToolId, true, actions );
                }
            }
        }
    }
    Run( actions );
}

// OnDownstreamReady
//------------------------------------------------------------------------------
/*virtual*/ void Gateway::OnDownstreamReady( DownstreamConnection & connection )
{
    Actions actions;
    {
        MutexHolder mh( m_Mutex );
        Worker * worker = FindWorker( connection );
        const uint32_t slots = connection.GetCapacity();
        if ( ( worker == nullptr ) || worker->m_Ready || worker->m_Lost || ( slots == 0 ) || m_ShuttingDown )
        {
            return;
        }

        // Only while it has slots: a job that failed elsewhere would wait for a registered worker
        worker->m_Ready = true;
        m_Scheduler.SetWorkerSlots( connection.GetIndex(), slots );
        UpdatePool();

        AStackString<> line;
        line.Format( "gateway worker-ready worker=%s slots=%u", connection.GetAddress().Get(), slots );
        actions.m_LogLines.EmplaceBack( line.Get() );
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
    uint32_t clientId = 0;
    uint64_t completedPchId = 0;
    bool passOn = false;
    uint8_t flags = 0;
    {
        MutexHolder mh( m_Mutex );
        const uint32_t workerIndex = connection.GetIndex();
        Attempt * attempt = nullptr;
        for ( Attempt & a : m_Attempts )
        {
            if ( ( a.m_AttemptId == header.m_JobId ) && ( a.m_WorkerIndex == workerIndex ) )
            {
                attempt = &a;
                break;
            }
        }

        // A result for an attempt the gateway no longer waits for only frees its slot
        if ( attempt )
        {
            m_Scheduler.ReleaseSlot( workerIndex );
            GatewayJob * job = attempt->m_Expired ? nullptr : FindJob( attempt->m_JobId );
            m_Attempts.Erase( attempt );
            if ( job && header.m_SystemError )
            {
                // The worker's fault: retried, and only the last failure of a job given up goes up
                GatewayWire::PatchResult( payload, payloadSize, header, job->m_ClientJobId, GatewayWire::MakeLane( workerIndex, header.m_ThreadIndex ) );
                clientId = job->m_ClientId;
                passOn = FailAttempt( *job, workerIndex, "system-error", actions );
                flags = Protocol::kResultReturnedByGateway;
            }
            else if ( job )
            {
                // Built, or failed by the code: passed on as it is
                m_Scheduler.OnCompleted( job->m_Id );
                completedPchId = job->m_PchId;
                GatewayWire::PatchResult( payload, payloadSize, header, job->m_ClientJobId, GatewayWire::MakeLane( workerIndex, header.m_ThreadIndex ) );
                clientId = job->m_ClientId;
                passOn = true;
                RemoveJob( job, actions );
            }
            Dispatch( actions );
        }
    }

    if ( completedPchId != 0 )
    {
        connection.OnJobCompleted( completedPchId );
    }
    if ( passOn )
    {
        MemoryStream result;
        result.Replace( payload, payloadSize );
        m_Server->QueueResult( clientId, compressed, Move( result ), flags ); // Dropped if the client has gone
    }
    else
    {
        FREE( payload );
    }
    Run( actions );
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
        if ( worker->m_Ready == false )
        {
            return;
        }

        // Out of the scheduler and the pool, then what it was building is tried elsewhere
        const uint32_t index = connection.GetIndex();
        worker->m_Ready = false;
        Array<uint32_t> runningJobIds;
        m_Scheduler.RemoveWorker( index, runningJobIds );
        UpdatePool();

        const char * reason = connection.GetCloseReason();
        AStackString<> line;
        line.Format( "gateway worker-lost worker=%s reason=%s running=%u", connection.GetAddress().Get(), reason ? reason : "disconnected", (uint32_t)runningJobIds.GetSize() );
        actions.m_LogLines.EmplaceBack( line.Get() );

        for ( size_t i = m_Attempts.GetSize(); i > 0; --i )
        {
            const Attempt attempt = m_Attempts[ i - 1 ];
            if ( attempt.m_WorkerIndex != index )
            {
                continue;
            }
            m_Attempts.EraseIndex( i - 1 ); // An expired one has no slot to give back now
            GatewayJob * job = attempt.m_Expired ? nullptr : FindJob( attempt.m_JobId );
            if ( job && FailAttempt( *job, index, "lost", actions ) )
            {
                GiveUpWithoutResult( *job, actions );
            }
        }
        Dispatch( actions );
    }
    Run( actions );
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
}

// Run
//------------------------------------------------------------------------------
void Gateway::Run( Actions & actions )
{
    for ( const uint64_t toolId : actions.m_DropIncomplete )
    {
        m_Stash.DropIncomplete( toolId );
    }
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
        GatewayJob * job = FindJob( assignment.m_JobId );
        DownstreamConnection * connection = m_Workers[ assignment.m_WorkerId - 1 ].m_Connection;
        ASSERT( job && connection );

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
        ConstMemoryStream stream;
        stream.Replace( data, job->m_DataSize, true );

        StoredPch pch;
        PchLease lease;
        const bool hasPch = ( job->m_PchId != 0 ) && LookUpPch( job->m_PchId, pch, lease );
        const bool sendsPch = hasPch && ( connection->GetAffinity( job->m_PchId, 0 ) == GatewayScheduler::AFFINITY_NONE );
        connection->SendJob( Protocol::MsgJob( job->m_ToolId, job->m_ResultCompressionLevel ), Move( stream ), hasPch ? &pch : nullptr, Move( lease ) );

        ++m_Forwarded;
        AStackString<> line;
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
    m_Pchs.Erase( known ); // Evicted, which the store does not report
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

    for ( Upload & upload : m_Uploads )
    {
        if ( ( upload.m_ToolId == toolId ) && ( upload.m_ClientId == clientId ) )
        {
            // The manifest the gateway asked for: the files are asked for next
            if ( upload.m_Requested && ( upload.m_FilesRequested == false ) && m_Stash.HasManifest( toolId ) )
            {
                RequestMissingFiles( upload, actions );
            }
            return;
        }
    }
    Upload upload; // Sent unasked, as a client sends a toolchain ahead of its jobs
    upload.m_ToolId = toolId;
    upload.m_ClientId = clientId;
    m_Uploads.Append( upload );
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
void Gateway::GiveUpWithoutResult( GatewayJob & job, Actions & actions )
{
    AStackString<> message;
    message.Format( "no result after %u attempts", job.m_NumAttempts );
    MemoryStream result;
    GatewayWire::WriteLostResult( result, job.m_ClientJobId, message.Get() );
    actions.m_GivenUp.Append( Actions::GivenUp{ job.m_ClientId, false, FNEW( MemoryStream( Move( result ) ) ) } );
}

// ExpireHungAttempts
//------------------------------------------------------------------------------
void Gateway::ExpireHungAttempts( Actions & actions )
{
    // An expired attempt keeps its slot taken until its late result arrives, or its worker is lost
    for ( Attempt & attempt : m_Attempts )
    {
        if ( attempt.m_Expired || ( attempt.m_Started.GetElapsed() < m_Config.m_JobTimeoutSeconds ) )
        {
            continue;
        }
        attempt.m_Expired = true;
        ++m_Timeouts;
        GatewayJob * job = FindJob( attempt.m_JobId );
        if ( job && FailAttempt( *job, attempt.m_WorkerIndex, "timeout", actions ) )
        {
            GiveUpWithoutResult( *job, actions );
        }
    }
    Dispatch( actions );
}

// UpdatePool
//------------------------------------------------------------------------------
void Gateway::UpdatePool()
{
    uint32_t slots = 0;
    if ( m_ShuttingDown == false )
    {
        for ( const Worker & worker : m_Workers )
        {
            if ( worker.m_Ready && worker.m_Connection && ( worker.m_Connection->HasEnded() == false ) )
            {
                slots += worker.m_Connection->GetCapacity();
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
}

// FormatStats
//------------------------------------------------------------------------------
void Gateway::FormatStats( AString & outLine ) const
{
    Stats stats;
    GetStatsLocked( stats );
    outLine.Format( "gateway stats clients=%u workers=%u/%u pool=%u waiting=%u parked=%u running=%u heldMiB=%u forwarded=%u retries=%u giveups=%u timeouts=%u pchMiB=%u",
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
                    (uint32_t)( stats.m_PchBytesForwarded / MEGABYTE ) );
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
    // Often enough for the watchdog and the reconnect delay to be kept to within a quarter of the shorter
    const float shortest = Math::Min( m_Config.m_JobTimeoutSeconds, m_Config.m_ReconnectSeconds );
    const uint32_t tickMs = Math::Clamp( (uint32_t)( shortest * 1000.0f / 4.0f ), 10u, 1000u );

    Timer statsTimer;
    while ( m_Quit.Load() == false )
    {
        m_HousekeepingSemaphore.Wait( tickMs );
        if ( m_Quit.Load() )
        {
            break;
        }

        MeasureStoredPchs();
        ReconnectLostWorkers();

        Actions actions;
        {
            MutexHolder mh( m_Mutex );
            if ( m_ShuttingDown == false )
            {
                ExpireHungAttempts( actions );
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

// MeasureStoredPchs
//------------------------------------------------------------------------------
void Gateway::MeasureStoredPchs()
{
    while ( m_Quit.Load() == false )
    {
        uint64_t pchId = 0;
        {
            MutexHolder mh( m_Mutex );
            for ( const KnownPch & known : m_Pchs )
            {
                if ( known.m_Measured == false )
                {
                    pchId = known.m_Id;
                    break;
                }
            }
        }
        if ( pchId == 0 )
        {
            return;
        }

        // The disk is read with no lock held, and the lease keeps the PCH in the store meanwhile
        PchLease lease = m_Server->AcquirePchLease( pchId );
        AStackString<> path;
        FileIO::FileInfo info;
        uint32_t uncompressedSize = 0;
        const bool found = m_Server->FindCachedPch( pchId, path );
        const bool measured = found && FileIO::GetFileInfo( path, info ) &&
                              PchBlob::MeasureFile( path.Get(), pchId, &m_Quit, uncompressedSize );
        if ( m_Quit.Load() )
        {
            return;
        }

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
