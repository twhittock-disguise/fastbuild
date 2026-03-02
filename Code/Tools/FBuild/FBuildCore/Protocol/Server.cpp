// Server.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Server.h"
#include "Protocol.h"

#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Helpers/ToolManifest.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/Job.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/JobQueueRemote.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/WorkerThreadRemote.h"

#include "Core/Env/Env.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/Process/Atomic.h"
#include "Core/Profile/Profile.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Timer.h"
#include "Core/Tracing/Tracing.h"

// Defines
//------------------------------------------------------------------------------
#if defined( __OSX__ ) || defined( __LINUX__ )
    // Touch files every 4 hours
    #define SERVER_TOOLCHAIN_TIMESTAMP_REFRESH_INTERVAL_SECS ( 60.0f * 60.0f * 4.0f )
#endif

// CONSTRUCTOR
//------------------------------------------------------------------------------
Server::Server( uint32_t numThreadsInJobQueue, uint32_t prefetchBuffer )
    : m_PrefetchBuffer( prefetchBuffer )
    , m_ShouldExit( false )
{
    m_ClientList.SetCapacity( 32 );

    m_JobQueueRemote = FNEW( JobQueueRemote( numThreadsInJobQueue ? numThreadsInJobQueue : Env::GetNumProcessors() ) );

    m_Thread.Start( ThreadFuncStatic, "Server", this );
}

// DESTRUCTOR
//------------------------------------------------------------------------------
Server::~Server()
{
    m_ShouldExit.Store( true );
    JobQueueRemote::Get().WakeMainThread();
    m_Thread.Join();

    ShutdownAllConnections();

    FDELETE m_JobQueueRemote;

    for ( ToolManifest * tool : m_Tools )
    {
        FDELETE tool;
    }
}

// GetHostForJob
//------------------------------------------------------------------------------
/*static*/ void Server::GetHostForJob( const Job * job, AString & hostName )
{
    const ClientState * cs = (const ClientState *)job->GetUserData();
    if ( cs )
    {
        hostName = cs->m_HostName;
    }
    else
    {
        hostName.Clear();
    }
}

// IsSynchingTool
//------------------------------------------------------------------------------
bool Server::IsSynchingTool( AString & statusStr ) const
{
    MutexHolder manifestMH( m_ToolManifestsMutex ); // ensure we don't make redundant requests

    for ( const ToolManifest * tool : m_Tools )
    {
        if ( tool->IsSynchronized() == false )
        {
            uint32_t synchDone;
            uint32_t synchTotal;
            const bool synching = tool->GetSynchronizationStatus( synchDone, synchTotal );
            if ( synching )
            {
                statusStr.Format( "Synchronizing Compiler %2.1f / %2.1f MiB\n",
                                  (double)( (float)synchDone / (float)MEGABYTE ),
                                  (double)( (float)synchTotal / (float)MEGABYTE ) );
                return true;
            }
        }
    }

    return false; // no toolchain is currently synching
}

// OnConnected
//------------------------------------------------------------------------------
/*virtual*/ void Server::OnConnected( const ConnectionInfo * connection )
{
    ClientState * cs = FNEW( ClientState( connection ) );
    connection->SetUserData( cs );

    MutexHolder mh( m_ClientListMutex );
    m_ClientList.Append( cs );
}

//------------------------------------------------------------------------------
/*virtual*/ void Server::OnDisconnected( const ConnectionInfo * connection )
{
    ASSERT( connection );
    ClientState * cs = (ClientState *)connection->GetUserData();
    ASSERT( cs );

    // Unhook any jobs which are queued or in progress for this client
    // - deletes the queued jobs
    // - unhooks the UserData for in-progress jobs so the result is discarded on completion
    JobQueueRemote & jqr = JobQueueRemote::Get();
    jqr.CancelJobsWithUserData( cs );

    // check if any tool chain was being sync'd from this Client
    Array<ToolManifest *> cancelledManifests;
    {
        MutexHolder manifestMH( m_ToolManifestsMutex );
        for ( ToolManifest * tm : m_Tools )
        {
            // if synchronizing from connection that was just disconnected...
            if ( ( tm->IsSynchronized() == false ) &&
                 ( tm->GetUserData() == connection ) )
            {
                // ...flag any expected files as not synching
                tm->CancelSynchronizingFiles();
                tm->SetUserData( nullptr );
                cancelledManifests.Append( tm );
            }
        }
    }

    // free the serverstate structure
    {
        // Remove from ClientList
        MutexHolder mh( m_ClientListMutex );
        const bool found = m_ClientList.FindAndErase( cs );
        ASSERT( found );
        (void)found;

        // because we cancelled manifest synchronization, we need to check if other
        // connections are waiting for the same manifest
        for ( ClientState * otherCS : m_ClientList )
        {
            MutexHolder mh2( otherCS->m_Mutex );
            for ( const Job * j : otherCS->m_WaitingJobs )
            {
                ToolManifest * jMan = j->GetToolManifest();
                if ( cancelledManifests.Find( jMan ) )
                {
                    RequestMissingFiles( otherCS->m_Connection, jMan );
                }
            }
        }

        // This is usually null here, but might need to be freed if
        // we had the connection drop between message and payload
        FREE( (void *)( cs->m_CurrentMessage ) );

        // delete any jobs where we were waiting on Tool synchronization
        for ( Job * job : cs->m_WaitingJobs )
        {
            delete job;
        }

        FDELETE cs;

        // Redistribute capacity among remaining coordinators
        RecalculateCapacityLocked();
    }
}

// OnReceive
//------------------------------------------------------------------------------
/*virtual*/ void Server::OnReceive( const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory )
{
    keepMemory = true; // we'll take care of freeing the memory

    ClientState * cs = (ClientState *)connection->GetUserData();
    ASSERT( cs );

    // are we expecting a msg, or the payload for a msg?
    void * payload = nullptr;
    size_t payloadSize = 0;
    if ( cs->m_CurrentMessage == nullptr )
    {
        // message
        cs->m_CurrentMessage = static_cast<const Protocol::IMessage *>( data );
        if ( cs->m_CurrentMessage->HasPayload() )
        {
            return;
        }
    }
    else
    {
        // payload
        ASSERT( cs->m_CurrentMessage->HasPayload() );
        payload = data;
        payloadSize = size;
    }

    // determine message type
    const Protocol::IMessage * imsg = cs->m_CurrentMessage;
    const Protocol::MessageType messageType = imsg->GetType();

    PROTOCOL_DEBUG( "Client -> Server : %u (%s)\n", messageType, GetProtocolMessageDebugName( messageType ) );

    switch ( messageType )
    {
        case Protocol::MSG_CONNECTION:
        {
            const Protocol::MsgConnection * msg = static_cast<const Protocol::MsgConnection *>( imsg );
            Process( connection, msg );
            break;
        }
        case Protocol::MSG_STATUS:
        {
            const Protocol::MsgStatus * msg = static_cast<const Protocol::MsgStatus *>( imsg );
            Process( connection, msg );
            break;
        }
        case Protocol::MSG_NO_JOB_AVAILABLE:
        {
            const Protocol::MsgNoJobAvailable * msg = static_cast<const Protocol::MsgNoJobAvailable *>( imsg );
            Process( connection, msg );
            break;
        }
        case Protocol::MSG_JOB:
        {
            const Protocol::MsgJob * msg = static_cast<const Protocol::MsgJob *>( imsg );
            Process( connection, msg, payload, payloadSize );
            break;
        }
        case Protocol::MSG_MANIFEST:
        {
            const Protocol::MsgManifest * msg = static_cast<const Protocol::MsgManifest *>( imsg );
            Process( connection, msg, payload, payloadSize );
            break;
        }
        case Protocol::MSG_FILE:
        {
            const Protocol::MsgFile * msg = static_cast<const Protocol::MsgFile *>( imsg );
            Process( connection, msg, payload, payloadSize );
            break;
        }
        default:
        {
            // unknown message type
            ASSERT( false ); // this indicates a protocol bug
            Disconnect( connection );
            break;
        }
    }

    // free everything
    FREE( (void *)( cs->m_CurrentMessage ) );
    FREE( payload );
    cs->m_CurrentMessage = nullptr;
}

// Process( MsgConnection )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgConnection * msg )
{
    // check for valid/supported protocol version
    if ( msg->GetProtocolVersion() != Protocol::kVersionMajor )
    {
        AStackString remoteAddr;
        TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
        FLOG_WARN( "Disconnecting '%s' due to bad protocol version\n", remoteAddr.Get() );
        Disconnect( connection );
        return;
    }

    // Check for matching platform
    if ( msg->GetPlatform() != Env::GetPlatform() )
    {
        AStackString remoteAddr;
        TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
        FLOG_WARN( "Disconnecting '%s' (%s) due to mismatched platform\n", remoteAddr.Get(), msg->GetHostName() );
        Disconnect( connection );
        return;
    }

    // take note of initial status of client
    ClientState * cs = (ClientState *)connection->GetUserData();
    {
        MutexHolder mh( cs->m_Mutex );
        cs->m_NumJobsAvailable.Store( msg->GetNumJobsAvailable() );
        cs->m_ProtocolVersionMinor = msg->GetProtocolVersionMinor();
        cs->m_HostName = msg->GetHostName();
    }

    // Recalculate capacity across all connected coordinators
    // (must NOT hold cs->m_Mutex here to avoid lock ordering with FinalizeCompletedJobs)
    RecalculateCapacity();

    // If Client is new enough, send an ack message with capacity
    if ( msg->GetProtocolVersionMinor() >= 3 )
    {
        // Send Ack to client with allocated capacity
        const Protocol::MsgConnectionAck ack( (uint8_t)cs->m_AllocatedCapacity );
        ack.Send( connection );
    }
}

// Process( MsgStatus )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgStatus * msg )
{
    // take note of latest status of client
    ClientState * cs = (ClientState *)connection->GetUserData();
    cs->m_NumJobsAvailable.Store( msg->GetNumJobsAvailable() );

    // Wake main thread to request jobs
    JobQueueRemote::Get().WakeMainThread();
}

// Process( MsgNoJobAvailable )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgNoJobAvailable * )
{
    // In push mode, jobs are pushed directly by the coordinator, so this
    // message should not normally be received. Ignore it.
    (void)connection;
}

// Process( MsgJob )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgJob * msg, const void * payload, size_t payloadSize )
{
    ClientState * cs = (ClientState *)connection->GetUserData();
    {
        // In push mode, jobs are pushed by the coordinator — no request tracking needed
        cs->m_NumJobsActive.Increment();

        MutexHolder mh( cs->m_Mutex );

        // deserialize job
        ConstMemoryStream ms( payload, payloadSize );

        Job * job = FNEW( Job( ms ) );
        job->SetUserData( cs );

        // Zstd is supported by all v23+ clients
        job->SetResultCompressionLevel( msg->GetResultCompressionLevel(), true /*allowZstdUse*/ );

        // Get ToolId
        const uint64_t toolId = msg->GetToolId();
        ASSERT( toolId );

        {
            // Find or create the manifest
            MutexHolder manifestMH( m_ToolManifestsMutex );

            ToolManifest ** found = m_Tools.FindDeref( toolId );
            ToolManifest * manifest = found ? *found : nullptr;
            if ( manifest )
            {
                job->SetToolManifest( manifest );

                // Is tool fully synchronized?
                if ( manifest->IsSynchronized() )
                {
                    // we have all the files - we can do the job
                    JobQueueRemote::Get().QueueJob( job );
                    return;
                }

                // In push mode, manifest and files are sent proactively by the coordinator.
                // Just wait for synchronization to complete.
                if ( manifest->GetUserData() == nullptr )
                {
                    manifest->SetUserData( (void *)connection );
                }
            }
            else
            {
                // first time seeing this tool — create manifest object
                // In push mode, the coordinator will send MsgManifest + MsgFile proactively
                manifest = FNEW( ToolManifest( toolId ) );
                manifest->SetUserData( (void *)connection );
                job->SetToolManifest( manifest );
                m_Tools.Append( manifest );
            }

            // can't start job yet - put it on hold
            cs->m_WaitingJobs.Append( job );
        }
    }
}

// Process( MsgManifest )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgManifest * msg, const void * payload, size_t payloadSize )
{
    ToolManifest * manifest = nullptr;
    const uint64_t toolId = msg->GetToolId();
    ConstMemoryStream ms( payload, payloadSize );

    {
        MutexHolder manifestMH( m_ToolManifestsMutex ); // ensure we don't make redundant requests

        // Find or create the manifest (in push mode, MsgManifest arrives before MsgJob)
        ToolManifest ** found = m_Tools.FindDeref( toolId );
        if ( found )
        {
            manifest = *found;
        }
        else
        {
            manifest = FNEW( ToolManifest( toolId ) );
            manifest->SetUserData( (void *)connection );
            m_Tools.Append( manifest );
        }
        if ( manifest->DeserializeFromRemote( ms ) == false )
        {
            ASSERT( false && "MsgManifest corrupt" );

            ClientState * cs = (ClientState *)connection->GetUserData();
            AStackString remoteAddr;
            TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
            FLOG_WARN( "Disconnecting '%s' (%s) due to corrupt MsgManifest\n",
                       remoteAddr.Get(),
                       cs->m_HostName.Get() );
            Disconnect( connection );
            return;
        }
    }

    // manifest has checked local files, from previous sessions and may
    // be synchronized
    if ( manifest->IsSynchronized() )
    {
        CheckWaitingJobs( manifest );
        return;
    }

    // In push mode, files arrive proactively from the coordinator.
    // No need to request missing files.
}

// Process( MsgFile )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgFile * msg, const void * payload, size_t payloadSize )
{
    const uint64_t toolId = msg->GetToolId();
    const uint32_t fileId = msg->GetFileId();

    // Update the Manifest
    ToolManifest * manifest = nullptr;
    {
        MutexHolder manifestMH( m_ToolManifestsMutex );

        // fill out the received manifest
        ToolManifest ** found = m_Tools.FindDeref( toolId );
        if ( found == nullptr )
        {
            // In push mode, MsgFile should always arrive after MsgManifest
            // (TCP ordering guarantees this). If not found, disconnect.
            return;
        }
        manifest = *found;
        // In push mode, files arrive unsolicited - accept from any connection
        // and track the connection for synchronization
        if ( manifest->GetUserData() == nullptr )
        {
            manifest->SetUserData( (void *)connection );
        }
        ASSERT( manifest->GetUserData() == connection );
        (void)connection;

        bool corruptData = false;
        if ( manifest->ReceiveFileData( fileId, payload, payloadSize, corruptData ) == false )
        {
            if ( corruptData )
            {
                ASSERT( false && "MsgFile corrupt" );

                ClientState * cs = (ClientState *)connection->GetUserData();
                AStackString remoteAddr;
                TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
                FLOG_WARN( "Disconnecting '%s' (%s) due to corrupt MsgFile\n",
                           remoteAddr.Get(),
                           cs->m_HostName.Get() );
            }
            else
            {
                // something went wrong storing the file
                AStackString fileName;
                manifest->GetRemoteFilePath( fileId, fileName );
                FLOG_WARN( "Failed to store fileId %u for manifest 0x%" PRIx64 "\n"
                           " - %s\n",
                           fileId,
                           toolId,
                           fileName.Get() );
            }

            Disconnect( connection );
            return;
        }

        if ( manifest->IsSynchronized() == false )
        {
            // wait for more files
            return;
        }
        manifest->SetUserData( nullptr );
    }

    // ToolChain is now synchronized
    // Allow any jobs that were waiting on it to start
    CheckWaitingJobs( manifest );
    JobQueueRemote::Get().WakeMainThread();
}

// CheckWaitingJobs
//------------------------------------------------------------------------------
void Server::CheckWaitingJobs( const ToolManifest * manifest )
{
    // Queue for start any jobs that may now be ready.
    // NOTE: In push mode, the coordinator sends the toolchain proactively
    // before/alongside jobs, so it's valid for the toolchain to sync before
    // any jobs arrive (m_WaitingJobs may be empty).
    MutexHolder mhC( m_ClientListMutex );
    for ( ClientState * cs : m_ClientList )
    {
        // For each connected client...
        MutexHolder mh2( cs->m_Mutex );

        // .. check all jobs waiting for ToolManifests
        const int32_t numJobs = (int32_t)cs->m_WaitingJobs.GetSize();
        for ( int32_t i = ( numJobs - 1 ); i >= 0; --i )
        {
            Job * job = cs->m_WaitingJobs[ (size_t)i ];
            const ToolManifest * manifestForThisJob = job->GetToolManifest();
            ASSERT( manifestForThisJob );
            if ( manifestForThisJob == manifest )
            {
                cs->m_WaitingJobs.EraseIndex( (size_t)i );
                JobQueueRemote::Get().QueueJob( job );
                PROTOCOL_DEBUG( "Server: Job %x can now be started\n", job );
            }
        }
    }
}

// ThreadFuncStatic
//------------------------------------------------------------------------------
/*static*/ uint32_t Server::ThreadFuncStatic( void * param )
{
    PROFILE_SET_THREAD_NAME( "ServerThread" );

    Server * s = (Server *)param;
    s->ThreadFunc();
    return 0;
}

// ThreadFunc
//------------------------------------------------------------------------------
void Server::ThreadFunc()
{
    while ( m_ShouldExit.Load() == false )
    {
        FinalizeCompletedJobs();

        TouchToolchains();

        JobQueueRemote::Get().MainThreadWait( 100 );
    }
}

// FinalizeCompletedJobs
//------------------------------------------------------------------------------
void Server::FinalizeCompletedJobs()
{
    PROFILE_FUNCTION;

    JobQueueRemote & jcr = JobQueueRemote::Get();
    Node::BuildResult result;
    while ( Job * job = jcr.GetCompletedJob( result ) )
    {
        // Jobs that ended in a useful state are reported to the Client
        // Other jobs (like those that were cancelled) are not
        if ( ( result == Node::BuildResult::eOk ) || ( result == Node::BuildResult::eFailed ) )
        {
            // get associated connection
            ClientState * cs = (ClientState *)job->GetUserData();

            MutexHolder mh( m_ClientListMutex );

            const bool connectionStillActive = ( m_ClientList.Find( cs ) != nullptr );
            if ( connectionStillActive )
            {
                MemoryStream ms;
                ms.Write( job->GetJobId() );
                ms.Write( job->GetNode()->GetName() );
                ms.Write( result == Node::BuildResult::eOk );
                ms.Write( job->GetSystemErrorCount() > 0 );
                ms.Write( job->GetMessages() );
                ms.Write( job->GetNode()->GetLastBuildTime() );
                ms.Write( job->GetRemoteThreadIndex() ); // The thread used to build the job to assist with visualization

                // write the data - build result for success, or output+errors for failure
                ms.Write( (uint32_t)job->GetDataSize() );
                ms.WriteBuffer( job->GetData(), job->GetDataSize() );

                {
                    ASSERT( cs->m_NumJobsActive.Load() > 0 );
                    cs->m_NumJobsActive.Decrement();

                    const uint32_t active = cs->m_NumJobsActive.Load();
                    const uint32_t allocated = cs->m_AllocatedCapacity;
                    const uint8_t remaining = (uint8_t)( allocated > active ? allocated - active : 0 );

                    MutexHolder mh2( cs->m_Mutex );

                    if ( job->GetResultCompressionLevel() == 0 )
                    {
                        // Uncompressed
                        const Protocol::MsgJobResult msg( remaining );
                        msg.Send( cs->m_Connection, ms );
                    }
                    else
                    {
                        // Compressed
                        const Protocol::MsgJobResultCompressed msg( remaining );
                        msg.Send( cs->m_Connection, ms );
                    }
                }
            }
            else
            {
                // we might get here without finding the connection
                // (if the connection was lost before we completed)
            }
        }

        FDELETE job;
    }
}

// RecalculateCapacity
//------------------------------------------------------------------------------
void Server::RecalculateCapacity()
{
    MutexHolder mh( m_ClientListMutex );
    RecalculateCapacityLocked();
}

// RecalculateCapacityLocked
//------------------------------------------------------------------------------
void Server::RecalculateCapacityLocked()
{
    // Caller must hold m_ClientListMutex
    const uint32_t numClients = (uint32_t)m_ClientList.GetSize();
    if ( numClients == 0 )
    {
        return;
    }
    const uint32_t totalCores = WorkerThreadRemote::GetNumCPUsToUse();
    const uint32_t totalCapacity = totalCores + m_PrefetchBuffer;
    const uint32_t perClient = totalCapacity / numClients;
    const uint32_t remainder = totalCapacity % numClients;
    uint32_t i = 0;
    for ( ClientState * cs : m_ClientList )
    {
        cs->m_AllocatedCapacity = perClient + ( i < remainder ? 1 : 0 );
        i++;
    }
}

// TouchToolchains
//------------------------------------------------------------------------------
void Server::TouchToolchains()
{
#if defined( __OSX__ ) || defined( __LINUX__ )
    if ( m_TouchToolchainTimer.GetElapsed() < SERVER_TOOLCHAIN_TIMESTAMP_REFRESH_INTERVAL_SECS )
    {
        return;
    }
    m_TouchToolchainTimer.Restart();

    MutexHolder manifestMH( m_ToolManifestsMutex );
    for ( const ToolManifest * toolManifest : m_Tools )
    {
        toolManifest->TouchFiles();
    }
#else
    // TODO:C we could update Windows timestamps too
#endif
}

// RequestMissingFiles
//------------------------------------------------------------------------------
void Server::RequestMissingFiles( const ConnectionInfo * connection, ToolManifest * manifest ) const
{
    MutexHolder manifestMH( m_ToolManifestsMutex );

    const Array<ToolManifestFile> & files = manifest->GetFiles();
    const size_t numFiles = files.GetSize();
    for ( size_t i = 0; i < numFiles; ++i )
    {
        const ToolManifestFile & f = files[ i ];
        if ( f.GetSyncState() == ToolManifestFile::NOT_SYNCHRONIZED )
        {
            // request this file
            const Protocol::MsgRequestFile reqFileMsg( manifest->GetToolId(), (uint32_t)i );
            reqFileMsg.Send( connection );

            // prevent it being requested again
            manifest->MarkFileAsSynchronizing( i );

            // either this is the first file being synchronized, or we
            // are synchronizing multiple files from the same connection
            // (it should not be possible to have files requested from different connections)
            ASSERT( ( manifest->GetUserData() == nullptr ) || ( manifest->GetUserData() == connection ) );
            manifest->SetUserData( (void *)connection );
        }
    }
}

//------------------------------------------------------------------------------
