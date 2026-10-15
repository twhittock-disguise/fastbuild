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

// Core
#include "Core/Containers/UniquePtr.h"
#include "Core/Env/CPUInfo.h"
#include "Core/Env/Env.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Process.h"
#include "Core/Profile/Profile.h"
#include "Core/Mem/Mem.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Time.h"
#include "Core/Time/Timer.h"
#include "Core/Tracing/Tracing.h"

#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"

// Defines
//------------------------------------------------------------------------------
#if defined( __OSX__ ) || defined( __LINUX__ )
    // Touch files every 4 hours
    #define SERVER_TOOLCHAIN_TIMESTAMP_REFRESH_INTERVAL_SECS ( 60.0f * 60.0f * 4.0f )
#endif

namespace
{
    constexpr uint32_t kMaxCachedPchEntries = 20;

    const char * const kPchFileName = "pch.pch";

    // A temporary PCH file another process left is only removed once it has not been written to
    // for this long, as that process may be receiving a PCH
    constexpr uint64_t kStaleTempPchFileSeconds = ( 60 * 60 );

    // Collects the directories found while listing files
    class DirectoryCollector : public GetFilesHelper
    {
    public:
        virtual bool OnDirectory( const AString & dirPath ) override
        {
            m_Directories.Append( dirPath );
            return GetFilesHelper::OnDirectory( dirPath );
        }

        Array<AString> m_Directories;
    };

    // Picks entries to evict, least recently used first, until at most maxEntries remain.
    // Protected entries are never picked, so more than maxEntries may remain.
    template <class AGE, class PROTECTED>
    void SelectLeastRecentlyUsed( size_t numEntries,
                                  uint32_t maxEntries,
                                  const AGE & ageOf,
                                  const PROTECTED & isProtected,
                                  Array<size_t> & outIndicesToEvict )
    {
        outIndicesToEvict.Clear();

        if ( numEntries <= maxEntries )
        {
            return;
        }

        // Visit indices oldest (lowest age value) first
        Array<size_t> indicesOldestFirst;
        indicesOldestFirst.SetCapacity( numEntries );
        for ( size_t i = 0; i < numEntries; ++i )
        {
            indicesOldestFirst.Append( i );
        }
        indicesOldestFirst.Sort( [ &ageOf ]( size_t a, size_t b ) -> bool
        {
            return ( ageOf( a ) < ageOf( b ) );
        } );

        size_t remaining = numEntries;
        for ( const size_t idx : indicesOldestFirst )
        {
            if ( remaining <= maxEntries )
            {
                break;
            }
            if ( isProtected( idx ) )
            {
                continue;
            }
            outIndicesToEvict.Append( idx );
            --remaining;
        }
    }

    // Persistent on-disk cache layout (toolchains and PCHs), under FBuild::GetWorkerCacheDir:
    //   toolchain.<toolId hex>/...
    //   pch.<pchId hex>/pch.pch
    //   pch.<pchId hex>/incoming.<process id>.<number>.pch (while a PCH is being received)
    bool BuildPchFilePath( uint64_t pchId, AStackString<> & outFile )
    {
        if ( FBuild::GetWorkerCacheDir( outFile ) == false ) { return false; }
        outFile.AppendFormat( "pch.%016" PRIx64 "%c%s", pchId, (char)NATIVE_SLASH, kPchFileName );
        return true;
    }

    // Removes the toolchain directory that holds the manifest file, and everything in it
    void DeleteToolchainDirectory( const AString & manifestPath )
    {
        const char * lastSlash = manifestPath.FindLast( NATIVE_SLASH );
        if ( lastSlash == nullptr )
        {
            return;
        }
        AStackString<> root( manifestPath.Get(), lastSlash );
        const char * name = root.FindLast( NATIVE_SLASH );
        name = ( name ? ( name + 1 ) : root.Get() );
        if ( AString::StrNCmp( name, "toolchain.", 10 ) != 0 )
        {
            return;
        }

        Array<AString> files;
        FileIO::GetFiles( root, AStackString<>( "*" ), true, &files );
        Array<AString> directories;
        directories.Append( root );
        for ( const AString & file : files )
        {
            FileIO::FileDelete( file.Get() );

            AStackString<> directory( file.Get(), file.FindLast( NATIVE_SLASH ) );
            while ( ( directory.GetLength() > root.GetLength() ) && ( directories.Find( directory ) == nullptr ) )
            {
                directories.Append( directory );
                directory.SetLength( (uint32_t)( directory.FindLast( NATIVE_SLASH ) - directory.Get() ) );
            }
        }

        // Deepest first, so a directory is empty by the time it is removed
        directories.Sort( []( const AString & a, const AString & b ) { return a.GetLength() > b.GetLength(); } );
        for ( const AString & directory : directories )
        {
            FileIO::DirectoryDelete( directory );
        }
    }
}

// CONSTRUCTOR
//------------------------------------------------------------------------------
Server::Server( uint32_t numThreadsInJobQueue, uint32_t prefetchBuffer )
    : m_PrefetchBuffer( prefetchBuffer )
    , m_ShouldExit( false )
{
    m_ClientList.SetCapacity( 32 );

    const uint32_t numCores = numThreadsInJobQueue ? numThreadsInJobQueue
                                                   : CPUInfo::Get().GetNumUsefulCores();
    m_JobQueueRemote = FNEW( JobQueueRemote( numCores ) );

    m_Thread.Start( ThreadFuncStatic, "Server", this );

    // Pre-scan caches so they're ready before any connections arrive
    ScanPchCache();
    ScanToolchainCache();
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

    // This client no longer needs its toolchains to stay available
    for ( ToolManifest * tm : cs->m_KnownTools )
    {
        tm->ReleaseUse();
    }
    cs->m_KnownTools.Clear();

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
                    RequestMissingFiles( *otherCS, jMan );
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

    // Toolchains released above may now be evictable
    EvictToolchains( nullptr );
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
        case Protocol::MSG_PCH_FILE:
        {
            const Protocol::MsgPchFile * msg = static_cast<const Protocol::MsgPchFile *>( imsg );
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

// SendTo
//------------------------------------------------------------------------------
/*static*/ bool Server::SendTo( ClientState & cs, const Protocol::IMessage & msg )
{
    MutexHolder sendMH( cs.m_SendMutex );
    return msg.Send( cs.m_Connection );
}

// SendTo
//------------------------------------------------------------------------------
/*static*/ bool Server::SendTo( ClientState & cs, const Protocol::IMessage & msg, const MemoryStream & payload )
{
    MutexHolder sendMH( cs.m_SendMutex );
    return msg.Send( cs.m_Connection, payload );
}

// SendIdInventory
//------------------------------------------------------------------------------
// Sends the ids a worker has cached, so the coordinator does not send them again
template <class MSG>
/*static*/ void Server::SendIdInventory( ClientState & cs, const Array<uint64_t> & ids )
{
    const MSG msg( (uint32_t)ids.GetSize() );
    if ( ids.IsEmpty() )
    {
        SendTo( cs, msg );
        return;
    }
    MemoryStream ms;
    for ( const uint64_t id : ids )
    {
        ms.Write( id );
    }
    SendTo( cs, msg, ms );
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
        FLOG_WARN( "Disconnecting '%s' due to bad protocol version (client v%u, worker v%u)\n",
                   remoteAddr.Get(), msg->GetProtocolVersion(), Protocol::kVersionMajor );
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
        cs->m_HostName = msg->GetHostName();
    }

    // Recalculate capacity across all connected coordinators
    // (takes m_ClientListMutex, so cs->m_Mutex must not be held)
    RecalculateCapacity();

    // Send ack with allocated capacity
    const Protocol::MsgConnectionAck ack( (uint8_t)cs->m_AllocatedCapacity );
    SendTo( *cs, ack );

    // Send PCH inventory so coordinator knows what's already cached
    Array<uint64_t> pchIds;
    {
        MutexHolder mh( m_PchCacheMutex );
        for ( const PchCacheEntry & e : m_PchCache )
        {
            pchIds.Append( e.pchId );
        }
    }
    SendIdInventory<Protocol::MsgPchInventory>( *cs, pchIds );

    // Send toolchain inventory so coordinator can skip sending toolchains we already have.
    // It follows the PCH inventory, so the coordinator knows both have arrived.
    SendToolchainInventory( *cs );
}

// Process( MsgJob )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgJob * msg, const void * payload, size_t payloadSize )
{
    ClientState * cs = (ClientState *)connection->GetUserData();

    // Decode in arrival order: the stream is compressed against the jobs before it
    void * jobData = nullptr;
    size_t jobDataSize = 0;
    if ( cs->m_JobDecoder.Decode( payload, payloadSize, jobData, jobDataSize ) == false )
    {
        AStackString remoteAddr;
        TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
        FLOG_WARN( "Disconnecting '%s' (%s) as its job data could not be decoded (corrupt, out of order or window above %u MiB)\n",
                   remoteAddr.Get(), cs->m_HostName.Get(), ( 1u << JobStream::kWindowLog ) / (uint32_t)MEGABYTE );
        Disconnect( connection );
        return;
    }
    cs->m_NumJobsActive.Increment();

    MutexHolder mh( cs->m_Mutex );

    // deserialize job
    ConstMemoryStream ms( jobData, jobDataSize );
    Job * job = FNEW( Job( ms ) );
    FREE( jobData );
    job->SetUserData( cs );
    job->SetResultCompressionLevel( msg->GetResultCompressionLevel(), true /*allowZstdUse*/ );

    // Resolve PCH cache path if job was bundled with PCH distribution data
    const uint64_t pchId = job->GetPchId();
    bool pchReady = true;
    if ( pchId != 0 )
    {
        AString pchPath;
        if ( FindCachedPch( pchId, pchPath ) )
        {
            job->SetPchCachePath( pchPath );
            FLOG_VERBOSE( "MsgJob: PCH resolved pchId=0x%016" PRIx64 " -> '%s' for '%s'\n",
                          pchId, pchPath.Get(), job->GetRemoteName().Get() );
        }
        else
        {
            FLOG_VERBOSE( "MsgJob: PCH not yet available pchId=0x%016" PRIx64 " for '%s' — waiting\n",
                          pchId, job->GetRemoteName().Get() );
            pchReady = false;
        }
    }

    // Find or create the manifest
    const uint64_t toolId = msg->GetToolId();
    ASSERT( toolId );

    MutexHolder manifestMH( m_ToolManifestsMutex );

    ToolManifest ** found = m_Tools.FindDeref( toolId );
    ToolManifest * manifest = found ? *found : nullptr;
    if ( manifest )
    {
        job->SetToolManifest( manifest );

        // Tool fully synchronized and PCH ready — queue immediately
        if ( manifest->IsSynchronized() && pchReady )
        {
            JobQueueRemote::Get().QueueJob( job );
            return;
        }

        // Wait for synchronization to complete
        if ( manifest->GetUserData() == nullptr )
        {
            manifest->SetUserData( (void *)connection );
        }
    }
    else
    {
        // First time seeing this tool — coordinator will send MsgManifest + MsgFile
        manifest = FNEW( ToolManifest( toolId ) );
        manifest->SetUserData( (void *)connection );
        job->SetToolManifest( manifest );
        m_Tools.Append( manifest );
    }

    // Can't start job yet - put it on hold (waiting for toolchain and/or PCH)
    cs->m_WaitingJobs.Append( job );
}

// Process( MsgManifest )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgManifest * msg, const void * payload, size_t payloadSize )
{
    ToolManifest * manifest = nullptr;
    const uint64_t toolId = msg->GetToolId();
    ConstMemoryStream ms( payload, payloadSize );
    ClientState * cs = (ClientState *)connection->GetUserData();
    m_NumManifestsReceived.Increment();

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

            AStackString remoteAddr;
            TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
            FLOG_WARN( "Disconnecting '%s' (%s) due to corrupt MsgManifest\n",
                       remoteAddr.Get(),
                       cs->m_HostName.Get() );
            Disconnect( connection );
            return;
        }

        // This client will send jobs for the toolchain, so it must not be evicted
        manifest->AcquireUse();
    }
    {
        MutexHolder mh( cs->m_Mutex );
        cs->m_KnownTools.Append( manifest );
    }

    // manifest has checked local files, from previous sessions and may
    // be synchronized
    if ( manifest->IsSynchronized() )
    {
        OnToolchainSynchronized( manifest );
        ReleaseReadyWaitingJobs();
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
    m_NumToolFilesReceived.Increment();

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

    // ToolChain is now synchronized — allow any jobs waiting on it to start
    OnToolchainSynchronized( manifest );
    ReleaseReadyWaitingJobs();
    JobQueueRemote::Get().WakeMainThread();
}

// ReleaseReadyWaitingJobs
//------------------------------------------------------------------------------
void Server::ReleaseReadyWaitingJobs()
{
    // Queue any jobs whose toolchain has finished syncing AND whose PCH (if
    // any) is now in the cache. Called whenever a dependency lands —
    // typically the final MsgFile of a toolchain or a MsgPchFile.
    MutexHolder mhC( m_ClientListMutex );
    for ( ClientState * cs : m_ClientList )
    {
        MutexHolder mh2( cs->m_Mutex );

        const int32_t numJobs = (int32_t)cs->m_WaitingJobs.GetSize();
        for ( int32_t i = ( numJobs - 1 ); i >= 0; --i )
        {
            Job * job = cs->m_WaitingJobs[ (size_t)i ];

            const ToolManifest * manifest = job->GetToolManifest();
            ASSERT( manifest );
            if ( !manifest->IsSynchronized() ) { continue; }

            const uint64_t pchId = job->GetPchId();
            if ( pchId != 0 && job->GetPchCachePath().IsEmpty() )
            {
                AString pchPath;
                if ( FindCachedPch( pchId, pchPath ) == false )
                {
                    continue;
                }
                job->SetPchCachePath( pchPath );
            }

            cs->m_WaitingJobs.EraseIndex( (size_t)i );
            JobQueueRemote::Get().QueueJob( job );
            PROTOCOL_DEBUG( "Server: Job %x can now be started\n", job );
        }
    }
}

// Process( MsgPchFile )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgPchFile * msg, const void * payload, size_t payloadSize )
{
    ClientState * cs = (ClientState *)connection->GetUserData();
    ASSERT( cs );

    const uint64_t pchId = msg->GetPchId();

    AStackString pchFilePath;
    if ( BuildPchFilePath( pchId, pchFilePath ) == false )
    {
        FLOG_WARN( "Failed to get temp dir for PCH cache\n" );
        Disconnect( connection );
        return;
    }

    bool alreadyCached = false;
    const PchReceiver::Result result = cs->m_PchReceiver.OnChunk( *msg, payload, payloadSize, pchFilePath, alreadyCached );
    if ( result == PchReceiver::FAILED )
    {
        Disconnect( connection );
        return;
    }
    if ( result == PchReceiver::CONTINUE )
    {
        return;
    }

    // Touch timestamp to prevent periodic cleanup
    FileIO::SetFileLastWriteTimeToNow( pchFilePath );

    // Snapshot the PCHs still needed before taking m_PchCacheMutex: the waiting-job scan
    // takes m_ClientListMutex, which ReleaseReadyWaitingJobs holds while calling FindCachedPch.
    // The PCH just received is protected too, as the jobs waiting for it are not queued yet.
    Array<uint64_t> pchIdsInUse;
    CollectPchIdsInUse( pchIdsInUse );
    pchIdsInUse.Append( pchId );

    // Add to in-memory cache
    {
        MutexHolder mh( m_PchCacheMutex );

        const uint64_t now = Time::GetCurrentFileTime();

        // Check not already in cache (race with another connection)
        for ( PchCacheEntry & e : m_PchCache )
        {
            if ( e.pchId == pchId )
            {
                e.lastWriteTime = now;
                return;
            }
        }

        PchCacheEntry entry;
        entry.pchId = pchId;
        entry.filePath = pchFilePath;
        entry.lastWriteTime = now;
        m_PchCache.Append( Move( entry ) );

        EvictPchs( m_PchCache, pchIdsInUse );
    }

    FLOG_OUTPUT( "PCH cached: 0x%016" PRIx64 " -> %s (cached=%s)\n",
                 pchId, pchFilePath.Get(), alreadyCached ? "disk" : "new" );

    // Release any jobs that were waiting for this PCH
    ReleaseReadyWaitingJobs();
    JobQueueRemote::Get().WakeMainThread();
}

// ScanPchCache
//------------------------------------------------------------------------------
void Server::ScanPchCache()
{
    PROFILE_FUNCTION;

    // Only scan once
    if ( m_PchCacheScanned )
    {
        return;
    }
    m_PchCacheScanned = true;

    AStackString basePath;
    if ( FBuild::GetWorkerCacheDir( basePath ) == false )
    {
        return;
    }

    if ( FileIO::DirectoryExists( basePath ) == false )
    {
        return;
    }

    // Use GetFilesEx to get timestamps for LRU eviction
    Array<PchCacheEntry> scannedEntries;

    // Scan for pch.pch files under pch.<hex> subdirs.
    // Parse pchId from the directory name rather than reading/hashing each file.
    Array<FileIO::FileInfo> pchFiles;
    AStackString pchWildcard( kPchFileName );
    Array<AString> pchPatterns;
    pchPatterns.Append( pchWildcard );
    if ( FileIO::GetFilesEx( basePath, &pchPatterns, true /*recurse*/, &pchFiles ) )
    {
        for ( const FileIO::FileInfo & fi : pchFiles )
        {
            // Path is: <basePath>/pch.<hex>/pch.pch — extract hex from parent dir
            const char * pchDot = fi.m_Name.Find( "pch." );
            if ( pchDot == nullptr ) { continue; }
            const char * hexStart = pchDot + 4; // skip "pch."
            uint64_t pchId = 0;
            for ( const char * p = hexStart; *p && *p != NATIVE_SLASH && *p != '/'; ++p )
            {
                char c = *p;
                uint8_t nibble;
                if ( c >= '0' && c <= '9' )      { nibble = (uint8_t)( c - '0' ); }
                else if ( c >= 'a' && c <= 'f' ) { nibble = (uint8_t)( c - 'a' + 10 ); }
                else if ( c >= 'A' && c <= 'F' ) { nibble = (uint8_t)( c - 'A' + 10 ); }
                else { pchId = 0; break; } // Invalid hex
                pchId = ( pchId << 4 ) | nibble;
            }
            if ( pchId == 0 ) { continue; }

            PchCacheEntry entry;
            entry.pchId = pchId;
            entry.filePath = fi.m_Name;
            entry.lastWriteTime = fi.m_LastWriteTime;
            scannedEntries.Append( Move( entry ) );
        }
    }

    RemoveStaleTempPchFiles( basePath );
    RemoveEmptyPchDirectories( basePath );

    if ( scannedEntries.IsEmpty() )
    {
        return;
    }

    // No jobs exist yet at startup, so nothing is in use
    EvictPchs( scannedEntries, Array<uint64_t>() );

    // Populate in-memory cache
    {
        MutexHolder mh( m_PchCacheMutex );
        for ( PchCacheEntry & se : scannedEntries )
        {
            m_PchCache.Append( Move( se ) );
        }
    }

    FLOG_OUTPUT( "PCH inventory: %u entries scanned from disk cache\n",
                 (uint32_t)m_PchCache.GetSize() );
}

// RemoveStaleTempPchFiles
//------------------------------------------------------------------------------
/*static*/ void Server::RemoveStaleTempPchFiles( const AString & basePath )
{
    Array<FileIO::FileInfo> files;
    Array<AString> patterns;
    patterns.Append( AStackString<>( "incoming.*" ) );
    if ( FileIO::GetFilesEx( basePath, &patterns, true /*recurse*/, &files ) == false )
    {
        return;
    }

    // Those of this process are being written to, and those of another may be as well
    const uint64_t now = Time::FileTimeToSeconds( Time::GetCurrentFileTime() );
    const uint32_t thisProcess = Process::GetCurrentId();
    for ( const FileIO::FileInfo & file : files )
    {
        uint32_t processId = 0;
        if ( ( PchReceiver::IsTempFileName( file.m_Name.Get(), processId ) == false ) ||
             ( processId == thisProcess ) )
        {
            continue;
        }
        const uint64_t lastWritten = Time::FileTimeToSeconds( file.m_LastWriteTime );
        if ( ( now > lastWritten ) && ( ( now - lastWritten ) >= kStaleTempPchFileSeconds ) )
        {
            FileIO::FileDelete( file.m_Name.Get() );
        }
    }
}

// RemoveEmptyPchDirectories
//------------------------------------------------------------------------------
/*static*/ void Server::RemoveEmptyPchDirectories( const AString & basePath )
{
    DirectoryCollector collector;
    FileIO::GetFiles( basePath, collector );

    // A PCH directory is removed when the PCH in it was evicted. Deleting a directory only works
    // when it is empty, so one with a PCH or a PCH being received is left.
    for ( const AString & directory : collector.m_Directories )
    {
        AStackString<> path( directory );
        if ( path.EndsWith( NATIVE_SLASH ) )
        {
            path.SetLength( path.GetLength() - 1 );
        }
        const char * lastSlash = path.FindLast( NATIVE_SLASH );
        if ( lastSlash == nullptr )
        {
            continue;
        }
        const AStackString<> parent( path.Get(), lastSlash + 1 );
        if ( ( parent == basePath ) && ( AString::StrNCmp( lastSlash + 1, "pch.", 4 ) == 0 ) )
        {
            FileIO::DirectoryDelete( path );
        }
    }
}

// CollectPchIdsInUse
//------------------------------------------------------------------------------
void Server::CollectPchIdsInUse( Array<uint64_t> & outPchIds ) const
{
    JobQueueRemote::Get().GetPchIdsInUse( outPchIds );

    // Jobs waiting for their toolchain or PCH are not in the job queue yet
    MutexHolder mhC( m_ClientListMutex );
    for ( ClientState * cs : m_ClientList )
    {
        MutexHolder mh( cs->m_Mutex );
        for ( const Job * job : cs->m_WaitingJobs )
        {
            if ( job->GetPchId() != 0 )
            {
                outPchIds.Append( job->GetPchId() );
            }
        }
    }
}

// EvictPchs
//------------------------------------------------------------------------------
/*static*/ void Server::EvictPchs( Array<PchCacheEntry> & entries,
                                    const Array<uint64_t> & pchIdsInUse )
{
    Array<size_t> evictions;
    SelectPchEvictions( entries, kMaxCachedPchEntries, pchIdsInUse, evictions );
    evictions.Sort();
    for ( size_t i = evictions.GetSize(); i > 0; --i )
    {
        const size_t idx = evictions[ i - 1 ];
        const AString & file = entries[ idx ].filePath;
        FileIO::FileDelete( file.Get() );

        // The directory goes with it, unless something else is in it
        const char * lastSlash = file.FindLast( NATIVE_SLASH );
        if ( lastSlash )
        {
            FileIO::DirectoryDelete( AStackString<>( file.Get(), lastSlash ) );
        }
        entries.EraseIndex( idx );
    }
}

// SelectPchEvictions
//------------------------------------------------------------------------------
/*static*/ void Server::SelectPchEvictions( const Array<PchCacheEntry> & entries,
                                             uint32_t maxEntries,
                                             const Array<uint64_t> & pchIdsInUse,
                                             Array<size_t> & outIndicesToEvict )
{
    SelectLeastRecentlyUsed( entries.GetSize(),
                             maxEntries,
                             [ &entries ]( size_t i ) { return entries[ i ].lastWriteTime; },
                             [ &entries, &pchIdsInUse ]( size_t i ) { return ( pchIdsInUse.Find( entries[ i ].pchId ) != nullptr ); },
                             outIndicesToEvict );
}

// ScanToolchainCache
//------------------------------------------------------------------------------
void Server::ScanToolchainCache()
{
    PROFILE_FUNCTION;

    AStackString basePath;
    if ( ( FBuild::GetWorkerCacheDir( basePath ) == false ) || ( FileIO::DirectoryExists( basePath ) == false ) )
    {
        return;
    }

    Array<AString> patterns;
    patterns.Append( AString( ToolManifest::kPersistedFileName ) );
    Array<FileIO::FileInfo> manifestFiles;
    if ( FileIO::GetFilesEx( basePath, &patterns, true /*recurse*/, &manifestFiles ) == false )
    {
        return;
    }

    // Most recently used first, so the toolchains that will not be kept are dropped
    // before any of their files are read
    Array<size_t> newestFirst;
    for ( size_t i = 0; i < manifestFiles.GetSize(); ++i )
    {
        newestFirst.Append( i );
    }
    newestFirst.Sort( [ &manifestFiles ]( size_t a, size_t b ) { return manifestFiles[ a ].m_LastWriteTime > manifestFiles[ b ].m_LastWriteTime; } );

    // A toolchain is only registered if every file in it still matches its manifest,
    // so everything advertised later can be used as is.
    uint32_t numLoaded = 0;
    for ( const size_t index : newestFirst )
    {
        const AString & manifestPath = manifestFiles[ index ].m_Name;
        ToolManifest * manifest = ( numLoaded < kMaxCachedToolchains ) ? ToolManifest::LoadFromDisk( manifestPath ) : nullptr;
        if ( manifest == nullptr )
        {
            // Over the cap, incomplete or modified: only the manifest file marks the directory as ours
            FLOG_VERBOSE( "Toolchain cache: removing '%s'\n", manifestPath.Get() );
            DeleteToolchainDirectory( manifestPath );
            continue;
        }

        MutexHolder mh( m_ToolManifestsMutex );
        if ( m_Tools.FindDeref( manifest->GetToolId() ) )
        {
            FDELETE manifest;
            continue;
        }
        m_Tools.Append( manifest );
        ++numLoaded;
    }

    FLOG_OUTPUT( "Toolchain inventory: %u toolchains loaded from disk cache\n", numLoaded );
}

// SendToolchainInventory
//------------------------------------------------------------------------------
void Server::SendToolchainInventory( ClientState & cs )
{
    // The coordinator will not send the toolchains listed here, so they must stay available
    // while it is connected. Only the most recently used ones are offered, which bounds how
    // many a connection can hold on to.
    Array<ToolManifest *> advertised;
    {
        MutexHolder manifestMH( m_ToolManifestsMutex );
        for ( ToolManifest * tm : m_Tools )
        {
            if ( tm->IsSynchronized() )
            {
                advertised.Append( tm );
            }
        }
        advertised.Sort( []( const ToolManifest * a, const ToolManifest * b ) { return a->GetLastUsed() > b->GetLastUsed(); } );
        if ( advertised.GetSize() > kMaxCachedToolchains )
        {
            advertised.SetSize( kMaxCachedToolchains );
        }
        for ( ToolManifest * tm : advertised )
        {
            tm->AcquireUse();
        }
    }

    Array<uint64_t> toolIds;
    for ( const ToolManifest * tm : advertised )
    {
        toolIds.Append( tm->GetToolId() );
    }
    {
        MutexHolder mh( cs.m_Mutex );
        for ( ToolManifest * tm : advertised )
        {
            cs.m_KnownTools.Append( tm );
        }
    }

    SendIdInventory<Protocol::MsgToolchainInventory>( cs, toolIds );
}

// OnToolchainSynchronized
//------------------------------------------------------------------------------
void Server::OnToolchainSynchronized( ToolManifest * manifest )
{
    {
        MutexHolder manifestMH( m_ToolManifestsMutex );
        manifest->MarkUsed(); // Just arrived, so the most recent
    }

    // The connection that sent the toolchain holds a use of it, so it stays while it is written
    if ( manifest->SaveToDisk() == false )
    {
        FLOG_VERBOSE( "Toolchain cache: 0x%016" PRIx64 " will not be reused after the worker restarts\n", manifest->GetToolId() );
    }
    EvictToolchains( manifest );
}

// EvictToolchains
//------------------------------------------------------------------------------
void Server::EvictToolchains( const ToolManifest * keep )
{
    // Taking a toolchain out of m_Tools while holding the mutex is safe: jobs and
    // connections take their use of it under the same mutex (or already hold one).
    Array<ToolManifest *> evicted;
    {
        MutexHolder manifestMH( m_ToolManifestsMutex );

        Array<ToolManifest *> candidates;
        for ( ToolManifest * tm : m_Tools )
        {
            if ( tm->IsSynchronized() ) // The others are still being received
            {
                candidates.Append( tm );
            }
        }

        Array<size_t> evictions;
        SelectLeastRecentlyUsed( candidates.GetSize(),
                                 kMaxCachedToolchains,
                                 [ &candidates ]( size_t i ) { return candidates[ i ]->GetLastUsed(); },
                                 [ &candidates, keep ]( size_t i ) { return ( candidates[ i ] == keep ) || candidates[ i ]->IsInUse(); },
                                 evictions );
        for ( const size_t idx : evictions )
        {
            m_Tools.FindAndErase( candidates[ idx ] );
            evicted.Append( candidates[ idx ] );
        }
    }

    for ( ToolManifest * tm : evicted )
    {
        FLOG_VERBOSE( "Toolchain cache: evicting 0x%016" PRIx64 "\n", tm->GetToolId() );
        tm->DeleteFromDisk();
        FDELETE tm;
    }
}

// FindCachedPch
//------------------------------------------------------------------------------
bool Server::FindCachedPch( uint64_t pchId, AString & outPath )
{
    MutexHolder mh( m_PchCacheMutex );
    for ( PchCacheEntry & e : m_PchCache )
    {
        if ( e.pchId == pchId )
        {
            e.lastWriteTime = Time::GetCurrentFileTime();
            outPath = e.filePath;
            return true;
        }
    }
    return false;
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

            // The connection might have been lost before we completed. Check before copying
            // the result into a message; it is checked again below, under the lock that matters.
            bool connectionStillActive;
            {
                MutexHolder mh( m_ClientListMutex );
                connectionStillActive = ( m_ClientList.Find( cs ) != nullptr );
            }

            if ( connectionStillActive )
            {
                // Build the message before taking any lock, as the result can be large
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

                // Holding the client list keeps the connection alive while we send. Only
                // cs->m_Mutex is free during the send, so jobs can still arrive on this connection.
                MutexHolder mh( m_ClientListMutex );
                if ( m_ClientList.Find( cs ) != nullptr )
                {
                    ASSERT( cs->m_NumJobsActive.Load() > 0 );
                    cs->m_NumJobsActive.Decrement();

                    const uint32_t active = cs->m_NumJobsActive.Load();
                    const uint32_t allocated = cs->m_AllocatedCapacity;
                    const uint8_t remaining = (uint8_t)( allocated > active ? allocated - active : 0 );

                    if ( job->GetResultCompressionLevel() == 0 )
                    {
                        // Uncompressed
                        const Protocol::MsgJobResult msg( remaining );
                        SendTo( *cs, msg, ms );
                    }
                    else
                    {
                        // Compressed
                        const Protocol::MsgJobResultCompressed msg( remaining );
                        SendTo( *cs, msg, ms );
                    }
                }
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
void Server::RequestMissingFiles( ClientState & cs, ToolManifest * manifest ) const
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
            SendTo( cs, reqFileMsg );

            // prevent it being requested again
            manifest->MarkFileAsSynchronizing( i );

            // either this is the first file being synchronized, or we
            // are synchronizing multiple files from the same connection
            // (it should not be possible to have files requested from different connections)
            ASSERT( ( manifest->GetUserData() == nullptr ) || ( manifest->GetUserData() == cs.m_Connection ) );
            manifest->SetUserData( (void *)cs.m_Connection );
        }
    }
}

//------------------------------------------------------------------------------
