// Server.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Server.h"
#include "Protocol.h"

#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchBlob.h"
#include "Tools/FBuild/FBuildCore/Helpers/ToolManifest.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/Job.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/JobQueueRemote.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/WorkerThreadRemote.h"

// Core
#include "Core/Containers/Move.h"
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
#include "Tools/FBuild/FBuildCore/Helpers/TempFiles.h"

#include <stdarg.h>

// Defines
//------------------------------------------------------------------------------
#if defined( __OSX__ ) || defined( __LINUX__ )
    // Touch files every 4 hours
    #define SERVER_TOOLCHAIN_TIMESTAMP_REFRESH_INTERVAL_SECS ( 60.0f * 60.0f * 4.0f )
#endif

namespace
{
    // The temporary file a PCH is unpacked to, next to the working copy
    const char * const kUnpackTempFilePrefix = "unpack.";
    const char * const kUnpackTempFileExtension = ".pch";

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

    constexpr uint64_t kNoLimit = ~(uint64_t)0;

    // Where the PCH store and the worker log are kept: cacheDir, or else the worker cache.
    // Empty (so no store and no log) if there is no such place.
    AString GetCacheRoot( const AString & cacheDir )
    {
        AStackString<> directory( cacheDir );
        if ( directory.IsEmpty() )
        {
            if ( FBuild::GetWorkerCacheDir( directory ) == false )
            {
                directory.Clear();
            }
        }
        else
        {
            PathUtils::EnsureTrailingSlash( directory );
        }
        return AString( directory );
    }

    struct LruLimits
    {
        size_t      m_MaxEntries;
        size_t      m_MinEntries;   // Not evicted for the sake of m_MaxBytes
        uint64_t    m_MaxBytes;
    };

    // Picks entries to evict, least recently used first, until at most m_MaxEntries remain and they
    // come to at most m_MaxBytes (which never takes the count below m_MinEntries).
    // Protected entries are never picked, so more may remain.
    template <class AGE, class SIZE, class PROTECTED>
    void SelectLeastRecentlyUsed( size_t numEntries,
                                  const LruLimits & limits,
                                  const AGE & ageOf,
                                  const SIZE & sizeOf,
                                  const PROTECTED & isProtected,
                                  Array<size_t> & outIndicesToEvict )
    {
        outIndicesToEvict.Clear();

        uint64_t remainingBytes = 0;
        for ( size_t i = 0; i < numEntries; ++i )
        {
            remainingBytes += sizeOf( i );
        }
        const auto isOverLimits = [ &limits ]( size_t remaining, uint64_t bytes )
        {
            return ( remaining > limits.m_MaxEntries ) ||
                   ( ( bytes > limits.m_MaxBytes ) && ( remaining > limits.m_MinEntries ) );
        };
        if ( isOverLimits( numEntries, remainingBytes ) == false )
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
            if ( isOverLimits( remaining, remainingBytes ) == false )
            {
                break;
            }
            if ( isProtected( idx ) )
            {
                continue;
            }
            outIndicesToEvict.Append( idx );
            --remaining;
            remainingBytes -= sizeOf( idx );
        }
    }

    // Persistent on-disk cache layout. Toolchains are under FBuild::GetWorkerCacheDir, PCHs under the
    // Server's cache root (the same directory unless a gateway is given another):
    //   toolchain.<toolId hex>/...
    //   pch.<pchId hex>/pch.zst (the PCH, compressed)
    //   pch.<pchId hex>/pch.pch (a working copy for the compiler, while jobs need it)
    //   pch.<pchId hex>/incoming.<process id>.<number>.pch (while a PCH is being received)
    //   pch.<pchId hex>/unpack.<process id>.<number>.pch (while a working copy is being made)
    bool BuildPchFilePath( const AString & cacheRoot, uint64_t pchId, AStackString<> & outFile )
    {
        if ( cacheRoot.IsEmpty() ) { return false; }
        outFile = cacheRoot;
        outFile.AppendFormat( "pch.%016" PRIx64 "%c%s", pchId, (char)NATIVE_SLASH, PchBlob::kStoredFileName );
        return true;
    }

    // The directory of a file, without the trailing slash
    bool GetDirectoryOf( const AString & file, AString & outDirectory )
    {
        const char * lastSlash = file.FindLast( NATIVE_SLASH );
        if ( lastSlash == nullptr )
        {
            return false;
        }
        outDirectory.Assign( file.Get(), lastSlash );
        return true;
    }

    // The id of the PCH whose directory (<basePath>pch.<16 hex digits>) holds the file, or 0 if the file is in none
    uint64_t GetPchIdOfFile( const AString & file, const AString & basePath )
    {
        const char * dirEnd = file.FindLast( NATIVE_SLASH );
        if ( dirEnd == nullptr )
        {
            return 0;
        }
        const char * dirStart = dirEnd;
        while ( ( dirStart > file.Get() ) && ( dirStart[ -1 ] != NATIVE_SLASH ) )
        {
            --dirStart;
        }
        if ( ( (size_t)( dirStart - file.Get() ) != basePath.GetLength() ) ||
             ( AString::StrNCmp( file.Get(), basePath.Get(), basePath.GetLength() ) != 0 ) ||
             ( ( dirEnd - dirStart ) != ( 4 + 16 ) ) ||
             ( AString::StrNCmp( dirStart, "pch.", 4 ) != 0 ) )
        {
            return 0;
        }
        uint64_t pchId = 0;
        for ( const char * p = ( dirStart + 4 ); p < dirEnd; ++p )
        {
            const char c = *p;
            uint8_t nibble;
            if ( ( c >= '0' ) && ( c <= '9' ) )      { nibble = (uint8_t)( c - '0' ); }
            else if ( ( c >= 'a' ) && ( c <= 'f' ) ) { nibble = (uint8_t)( c - 'a' + 10 ); }
            else if ( ( c >= 'A' ) && ( c <= 'F' ) ) { nibble = (uint8_t)( c - 'A' + 10 ); }
            else { return 0; }
            pchId = ( ( pchId << 4 ) | nibble );
        }
        return pchId;
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

    // The payload of a MSG_JOB_RESULT(_COMPRESSED) for a job built here
    void WriteJobResult( const Job & job, bool ok, MemoryStream & ms )
    {
        const Node * node = job.GetNode();
        ms.Write( job.GetJobId() );
        ms.Write( node->GetName() );
        ms.Write( ok );
        ms.Write( job.GetSystemErrorCount() > 0 );
        ms.Write( job.GetMessages() );
        ms.Write( node->GetLastBuildTime() );
        ms.Write( job.GetRemoteThreadIndex() ); // The thread used to build the job to assist with visualization

        // write the data - build result for success, or output+errors for failure
        ms.Write( (uint32_t)job.GetDataSize() );
        ms.WriteBuffer( job.GetData(), job.GetDataSize() );
    }
}

// Static Data
//------------------------------------------------------------------------------
/*static*/ uint8_t Server::s_LocalJobTag = 0;

// CONSTRUCTOR
//------------------------------------------------------------------------------
Server::Server( uint32_t numThreadsInJobQueue, uint32_t prefetchBuffer )
    : m_CacheDir( GetCacheRoot( AString::GetEmpty() ) )
    , m_Log( m_CacheDir )
    , m_PrefetchBuffer( prefetchBuffer )
    , m_ShouldExit( false )
{
    m_ClientList.SetCapacity( 32 );

    const uint32_t numCores = numThreadsInJobQueue ? numThreadsInJobQueue
                                                   : CPUInfo::Get().GetNumUsefulCores();
    m_JobQueueRemote = FNEW( JobQueueRemote( numCores ) );

    m_Thread.Start( ThreadFuncStatic, "Server", this );
    m_PchUnpackThread.Start( UnpackThreadFuncStatic, "PchUnpack", this, 256 * 1024 );

    // Pre-scan caches so they're ready before any connections arrive
    ScanPchCache();
    ScanToolchainCache();
}

// CONSTRUCTOR (gateway)
//------------------------------------------------------------------------------
Server::Server( Forwarder & forwarder, const AString & cacheDir, uint32_t localThreads, uint32_t prefetchBuffer )
    : m_Forwarder( &forwarder )
    , m_CacheDir( GetCacheRoot( cacheDir ) )
    , m_Log( m_CacheDir )
    , m_PrefetchBuffer( prefetchBuffer )
    , m_ShouldExit( false )
{
    m_ClientList.SetCapacity( 32 );

    // A pure gateway keeps PCHs as they arrive, for the gateway to send on: none is unpacked.
    // ToolManifest keeps toolchains in the worker cache dir, so a local pool needs its PCHs there too.
    if ( ( localThreads > 0 ) && ( PathUtils::ArePathsEqual( m_CacheDir, GetCacheRoot( AString::GetEmpty() ) ) == false ) )
    {
        LogWarning( "The gateway has no local pool: its cache dir '%s' is not the worker cache dir, where toolchains are kept\n", m_CacheDir.Get() );
        localThreads = 0;
    }
    if ( localThreads > 0 )
    {
        m_JobQueueRemote = FNEW( JobQueueRemote( localThreads ) );
    }
    m_Thread.Start( ThreadFuncStatic, "Server", this );
    if ( m_JobQueueRemote )
    {
        m_PchUnpackThread.Start( UnpackThreadFuncStatic, "PchUnpack", this, 256 * 1024 );
    }

    ScanPchCache();
    if ( m_JobQueueRemote )
    {
        ScanToolchainCache();
    }
}

// DESTRUCTOR
//------------------------------------------------------------------------------
Server::~Server()
{
    m_ShouldExit.Store( true );
    if ( m_JobQueueRemote )
    {
        m_JobQueueRemote->WakeMainThread();
    }
    m_Thread.Join();
    if ( m_PchUnpackThread.IsRunning() )
    {
        m_PchUnpackSemaphore.Signal();
        m_PchUnpackThread.Join();
    }

    ShutdownAllConnections();

    FDELETE m_JobQueueRemote;

    // They hold PCH leases and toolchain uses, so they go first
    for ( Job * job : m_LocalWaitingJobs )
    {
        FDELETE job;
    }
    for ( Job * job : m_LocalEndedJobs )
    {
        FDELETE job;
    }

    for ( ToolManifest * tool : m_Tools )
    {
        FDELETE tool;
    }
}

// GetHostForJob
//------------------------------------------------------------------------------
/*static*/ void Server::GetHostForJob( const Job * job, AString & hostName )
{
    if ( job->GetUserData() == &s_LocalJobTag )
    {
        hostName = "gateway";
        return;
    }
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

// LogWarning
//------------------------------------------------------------------------------
void Server::LogWarning( const char * fmtString, ... )
{
    AStackString<> message;
    va_list args;
    va_start( args, fmtString );
    message.VFormat( fmtString, args );
    va_end( args );

    FLOG_WARN( "%s", message.Get() );
    while ( message.EndsWith( '\n' ) )
    {
        message.Trim( 0, 1 );
    }
    m_Log.Write( "error %s", message.Get() );
}

// OnConnected
//------------------------------------------------------------------------------
/*virtual*/ void Server::OnConnected( const ConnectionInfo * connection )
{
    ClientState * cs = FNEW( ClientState( connection ) );
    connection->SetUserData( cs );

    AStackString<> remoteAddr;
    TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
    m_Log.Write( "connect addr=%s", remoteAddr.Get() );

    ClientOutbox * outbox = FNEW( ClientOutbox( *this, *cs ) );

    MutexHolder mh( m_ClientListMutex );
    cs->m_Id = m_NextClientId++;
    cs->m_Outbox = outbox;
    m_ClientList.Append( cs );
}

//------------------------------------------------------------------------------
/*virtual*/ void Server::OnDisconnected( const ConnectionInfo * connection )
{
    ASSERT( connection );
    ClientState * cs = (ClientState *)connection->GetUserData();
    ASSERT( cs );

    AStackString<> remoteAddr;
    TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
    m_Log.Write( "disconnect addr=%s host=%s jobs=%u", remoteAddr.Get(), cs->m_HostName.Get(), cs->m_NumJobsActive.Load() );

    // No result is queued for this client from here on. The connection is marked as closing first,
    // so a send that waits for the client to read gives up, and the outbox thread can be joined.
    ClientOutbox * outbox;
    {
        MutexHolder mh( m_ClientListMutex );
        outbox = cs->m_Outbox;
        cs->m_Outbox = nullptr;
    }
    Disconnect( connection );
    FDELETE outbox;
    const uint32_t clientId = cs->m_Id;

    // Unhook any jobs which are queued or in progress for this client
    // - deletes the queued jobs
    // - unhooks the UserData for in-progress jobs so the result is discarded on completion
    if ( m_JobQueueRemote )
    {
        m_JobQueueRemote->CancelJobsWithUserData( cs );
    }

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
    const uint32_t poolSlots = GetPoolSlots();
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

        // Its slots go to the clients that remain
        m_Allocator.RemoveClient( clientId );
        ShareSlotsLocked( poolSlots );
    }

    // Once the client has left the list, with no lock held
    if ( m_Forwarder )
    {
        m_Forwarder->OnClientGone( clientId );
    }

    // Toolchains released above may now be evictable
    EvictToolchains( nullptr );

    // A job that waited for a PCH this client was receiving has to ask for it
    ReleaseReadyWaitingJobs();
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
        if ( Protocol::IsValidHeader( data, size ) == false )
        {
            AStackString remoteAddr;
            TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
            LogWarning( "Disconnecting '%s' as it sent an invalid message (%u bytes)\n", remoteAddr.Get(), size );
            FREE( data );
            Disconnect( connection );
            return;
        }
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
            Process( connection, msg, size );
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
        case Protocol::MSG_DEMAND:
        {
            const Protocol::MsgDemand * msg = static_cast<const Protocol::MsgDemand *>( imsg );
            Process( connection, msg );
            break;
        }
        default:
        {
            // unknown message type
            ASSERT( false ); // this indicates a protocol bug
            AStackString remoteAddr;
            TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
            LogWarning( "Disconnecting '%s' as it sent an unknown message (type %u)\n", remoteAddr.Get(), (uint32_t)messageType );
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

// QueueResult
//------------------------------------------------------------------------------
bool Server::QueueResult( uint32_t clientId, bool compressed, MemoryStream && payload, uint8_t flags )
{
    MutexHolder mh( m_ClientListMutex );
    for ( ClientState * cs : m_ClientList )
    {
        if ( cs->m_Id == clientId )
        {
            if ( cs->m_Outbox == nullptr )
            {
                return false; // It is going
            }
            cs->m_Outbox->PushResult( compressed, Move( payload ), flags );
            return true;
        }
    }
    return false;
}

// SendToClient
//------------------------------------------------------------------------------
bool Server::SendToClient( uint32_t clientId, const Protocol::IMessage & msg )
{
    MutexHolder mh( m_ClientListMutex );
    for ( ClientState * cs : m_ClientList )
    {
        if ( cs->m_Id == clientId )
        {
            if ( cs->m_Outbox == nullptr )
            {
                return false; // It is going
            }
            cs->m_Outbox->PushMessage( msg );
            return true;
        }
    }
    return false;
}

// AskClientForPch
//------------------------------------------------------------------------------
void Server::AskClientForPch( uint32_t clientId, uint64_t pchId )
{
    MutexHolder mh( m_ClientListMutex );
    for ( ClientState * cs : m_ClientList )
    {
        if ( cs->m_Id == clientId )
        {
            MutexHolder mh2( cs->m_Mutex );
            RequestPchIfMissing( *cs, pchId );
            return;
        }
    }
}

// DisconnectClient
//------------------------------------------------------------------------------
void Server::DisconnectClient( uint32_t clientId )
{
    // Under the lock, as a connection outlives its client's place in the list
    MutexHolder mh( m_ClientListMutex );
    for ( const ClientState * cs : m_ClientList )
    {
        if ( cs->m_Id == clientId )
        {
            Disconnect( cs->m_Connection );
            return;
        }
    }
}

// GetMaxAllowance
//------------------------------------------------------------------------------
uint32_t Server::GetMaxAllowance( uint32_t clientId ) const
{
    MutexHolder mh( m_ClientListMutex );
    for ( const ClientState * cs : m_ClientList )
    {
        if ( cs->m_Id == clientId )
        {
            return cs->m_MaxAllocatedCapacity;
        }
    }
    return 0;
}

// ClientOutbox
//------------------------------------------------------------------------------
Server::ClientOutbox::ClientOutbox( Server & server, ClientState & cs )
    : m_Server( server )
    , m_ClientState( cs )
    , m_Quit( false )
{
    m_Thread.Start( ThreadFuncStatic, "ClientOutbox", this );
}

// ~ClientOutbox
//------------------------------------------------------------------------------
Server::ClientOutbox::~ClientOutbox()
{
    m_Quit.Store( true );
    m_WakeSemaphore.Signal();
    m_Thread.Join();

    for ( const QueuedResult & result : m_Results )
    {
        FDELETE result.m_Payload;
    }
}

// PushResult
//------------------------------------------------------------------------------
void Server::ClientOutbox::PushResult( bool compressed, MemoryStream && payload, uint8_t flags )
{
    QueuedResult result;
    result.m_Payload = FNEW( MemoryStream( Move( payload ) ) );
    result.m_Flags = flags;
    result.m_Compressed = compressed;
    result.m_IsMessage = false;
    {
        MutexHolder mh( m_Mutex );
        m_Results.Append( result );
    }
    m_WakeSemaphore.Signal();
}

// PushMessage
//------------------------------------------------------------------------------
void Server::ClientOutbox::PushMessage( const Protocol::IMessage & msg )
{
    ASSERT( msg.HasPayload() == false );
    QueuedResult request;
    request.m_Payload = FNEW( MemoryStream( msg.GetSize() ) );
    request.m_Payload->WriteBuffer( &msg, msg.GetSize() );
    request.m_Flags = 0;
    request.m_Compressed = false;
    request.m_IsMessage = true;
    {
        MutexHolder mh( m_Mutex );
        m_Results.Append( request );
    }
    m_WakeSemaphore.Signal();
}

// Wake
//------------------------------------------------------------------------------
void Server::ClientOutbox::Wake()
{
    m_WakeSemaphore.Signal();
}

// ThreadFuncStatic
//------------------------------------------------------------------------------
/*static*/ uint32_t Server::ClientOutbox::ThreadFuncStatic( void * param )
{
    PROFILE_SET_THREAD_NAME( "ClientOutbox" );

    static_cast<ClientOutbox *>( param )->ThreadFunc();
    return 0;
}

// ThreadFunc
//------------------------------------------------------------------------------
void Server::ClientOutbox::ThreadFunc()
{
    Array<QueuedResult> results;
    while ( m_Quit.Load() == false )
    {
        m_WakeSemaphore.Wait();
        if ( m_Quit.Load() == false )
        {
            SendCapacityIfChanged();
        }

        {
            MutexHolder mh( m_Mutex );
            results.Swap( m_Results );
        }
        for ( const QueuedResult & result : results )
        {
            if ( m_Quit.Load() == false )
            {
                Send( result );
            }
            FDELETE result.m_Payload;
        }
        results.Clear();
    }
}

// Send
//------------------------------------------------------------------------------
void Server::ClientOutbox::Send( const QueuedResult & result )
{
    ClientState & cs = m_ClientState;
    if ( result.m_IsMessage )
    {
        SendTo( cs, *static_cast<const Protocol::IMessage *>( result.m_Payload->GetData() ) );
        return;
    }

    // Before the send mutex is taken, so the result is no longer counted while it waits to be sent
    ASSERT( cs.m_NumJobsActive.Load() > 0 );
    cs.m_NumJobsActive.Decrement();

    if ( result.m_Compressed )
    {
        const Protocol::MsgJobResultCompressed msg( result.m_Flags );
        SendTo( cs, msg, *result.m_Payload );
    }
    else
    {
        const Protocol::MsgJobResult msg( result.m_Flags );
        SendTo( cs, msg, *result.m_Payload );
    }
}

// SendCapacityIfChanged
//------------------------------------------------------------------------------
void Server::ClientOutbox::SendCapacityIfChanged()
{
    // Not ahead of the ack and the inventories: the outbox is woken once they have gone
    ClientState & cs = m_ClientState;
    if ( ( cs.m_HandshakeDone.Load() == false ) || ( cs.m_CapacityChanged.Load() == false ) )
    {
        return;
    }

    // Cleared before the allowance is read, so a change after the read is sent too
    cs.m_CapacityChanged.Store( false );
    uint32_t allowance = 0;
    uint32_t poolSlots = 0;
    {
        MutexHolder mh( m_Server.m_ClientListMutex );
        allowance = cs.m_AllocatedCapacity;
        poolSlots = m_Server.m_Allocator.GetPoolSlots();
    }
    SendTo( cs, Protocol::MsgCapacity( allowance, poolSlots ) );
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
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgConnection * msg, uint32_t size )
{
    ClientState * cs = (ClientState *)connection->GetUserData();
    AStackString remoteAddr;
    TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );

    // A handshake has no payload (see IsValidHeader), so size is that of the message itself. A client of
    // another major only has its version fields to be relied on, and the host name as far as it goes.
    AStackString<> hostName;
    msg->GetHostName( size, hostName );
    if ( msg->GetProtocolVersion() != Protocol::kVersionMajor )
    {
        AStackString<> ourVersion;
        Protocol::GetVersionString( ourVersion );
        FLOG_WARN( "Refusing '%s' (%s): it runs protocol v%u, this worker runs %s. FBuild and FBuildWorker must be updated together.\n",
                   remoteAddr.Get(), hostName.Get(), msg->GetProtocolVersion(), ourVersion.Get() );
        m_Log.Write( "refused addr=%s host=%s reason=protocol-version", remoteAddr.Get(), hostName.Get() );
        const Protocol::MsgRefused refused;
        SendTo( *cs, refused );
        Disconnect( connection );
        return;
    }
    if ( size < sizeof( Protocol::MsgConnection ) )
    {
        FLOG_WARN( "Disconnecting '%s' as its handshake is too short (%u bytes)\n", remoteAddr.Get(), size );
        m_Log.Write( "refused addr=%s host=%s reason=handshake-too-short", remoteAddr.Get(), hostName.Get() );
        Disconnect( connection );
        return;
    }

    // Check for matching platform
    if ( msg->GetPlatform() != Env::GetPlatform() )
    {
        FLOG_WARN( "Disconnecting '%s' (%s) due to mismatched platform\n", remoteAddr.Get(), hostName.Get() );
        m_Log.Write( "refused addr=%s host=%s reason=platform-mismatch", remoteAddr.Get(), hostName.Get() );
        Disconnect( connection );
        return;
    }

    // take note of initial status of client
    {
        MutexHolder mh( cs->m_Mutex );
        cs->m_HostName = hostName;
    }

    // A gateway without workers takes no clients
    const uint32_t poolSlots = GetPoolSlots();
    if ( m_Forwarder && ( poolSlots == 0 ) )
    {
        FLOG_WARN( "Disconnecting '%s' (%s): the gateway has no workers\n", remoteAddr.Get(), hostName.Get() );
        m_Log.Write( "refused addr=%s host=%s reason=no-capacity", remoteAddr.Get(), hostName.Get() );
        Disconnect( connection );
        return;
    }

    m_Log.Write( "handshake addr=%s host=%s", remoteAddr.Get(), hostName.Get() );

    // The new client's share of the demand it is assumed to have. The ack carries it, so it is not sent again.
    uint32_t capacity = 0;
    {
        MutexHolder mh( m_ClientListMutex );
        m_Allocator.AddClient( cs->m_Id );
        ShareSlotsLocked( poolSlots, cs->m_Id );
        capacity = cs->m_AllocatedCapacity;
    }

    // Send ack with allocated capacity
    const Protocol::MsgConnectionAck ack( capacity );
    SendTo( *cs, ack );

    // Send PCH inventory so coordinator knows what's already cached
    Array<uint64_t> pchIds;
    GetCachedPchIds( pchIds );
    SendIdInventory<Protocol::MsgPchInventory>( *cs, pchIds );

    // Send toolchain inventory so coordinator can skip sending toolchains we already have.
    // It follows the PCH inventory, so the coordinator knows both have arrived.
    if ( m_Forwarder )
    {
        Array<uint64_t> toolIds;
        m_Forwarder->GetToolchainInventory( cs->m_Id, toolIds );
        SendIdInventory<Protocol::MsgToolchainInventory>( *cs, toolIds );
    }
    else
    {
        SendToolchainInventory( *cs );
    }

    // An allowance that changed meanwhile follows
    cs->m_HandshakeDone.Store( true );
    MutexHolder mh( m_ClientListMutex );
    if ( cs->m_CapacityChanged.Load() && cs->m_Outbox )
    {
        cs->m_Outbox->Wake();
    }
}

// Process( MsgDemand )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgDemand * msg )
{
    ClientState * cs = (ClientState *)connection->GetUserData();
    if ( cs->m_HandshakeDone.Load() == false )
    {
        return;
    }

    const uint32_t poolSlots = GetPoolSlots();
    uint32_t allowance = 0;
    {
        MutexHolder mh( m_ClientListMutex );
        cs->m_Demand = msg->GetDemand();
        m_Allocator.SetDemand( cs->m_Id, cs->m_Demand );
        ShareSlotsLocked( poolSlots );
        MarkCapacityChanged( *cs ); // Whether or not its allowance changed, so that it learns the pool
        allowance = cs->m_AllocatedCapacity;
    }
    if ( m_Forwarder )
    {
        m_Log.Write( "gateway allowance client=%u demand=%u allowance=%u", cs->m_Id, msg->GetDemand(), allowance );
    }
    else
    {
        m_Log.Write( "demand host=%s demand=%u allowance=%u pool=%u", cs->m_HostName.Get(), msg->GetDemand(), allowance, poolSlots );
    }
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
        LogWarning( "Disconnecting '%s' (%s) as its job data could not be decoded (corrupt, out of order or window above %u MiB)\n",
                   remoteAddr.Get(), cs->m_HostName.Get(), ( 1u << JobStream::kWindowLog ) / (uint32_t)MEGABYTE );
        Disconnect( connection );
        return;
    }
    cs->m_NumJobsActive.Increment();

    if ( m_Forwarder )
    {
        m_Forwarder->OnClientJob( cs->m_Id, *msg, jobData, jobDataSize );
        return;
    }

    // Lock order: m_ClientListMutex before cs->m_Mutex, so asking for a PCH comes after this scope
    uint64_t missingPchId = 0;
    AStackString<> jobName;
    uint64_t pchId = 0;
    bool isJobWaiting = true;
    {
        MutexHolder mh( cs->m_Mutex );

        // deserialize job
        ConstMemoryStream ms( jobData, jobDataSize );
        Job * job = FNEW( Job( ms ) );
        FREE( jobData );
        job->SetUserData( cs );
        job->SetResultCompressionLevel( msg->GetResultCompressionLevel(), true /*allowZstdUse*/ );
        jobName = job->GetRemoteName(); // The job may be finished and gone as soon as it is queued

        // Resolve PCH cache path if job was bundled with PCH distribution data. The job holds the PCH
        // from here to its end, so it is not evicted or unpacked and deleted while the job waits.
        pchId = job->GetPchId();
        bool pchReady = true;
        if ( pchId != 0 )
        {
            job->SetPchLease( AcquirePchLease( pchId ) );
            if ( TryResolvePch( *job ) )
            {
                cs->m_PchNeededSent.FindAndErase( pchId );
                FLOG_VERBOSE( "MsgJob: PCH resolved pchId=0x%016" PRIx64 " -> '%s' for '%s'\n",
                              pchId, job->GetPchCachePath().Get(), job->GetRemoteName().Get() );
            }
            else
            {
                FLOG_VERBOSE( "MsgJob: PCH not yet available pchId=0x%016" PRIx64 " for '%s' — waiting\n",
                              pchId, job->GetRemoteName().Get() );
                pchReady = false;
                missingPchId = pchId;
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
                m_JobQueueRemote->QueueJob( job );
                isJobWaiting = false;
            }
            else if ( manifest->GetUserData() == nullptr )
            {
                // Wait for synchronization to complete
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
        if ( isJobWaiting )
        {
            cs->m_WaitingJobs.Append( job );
        }
    }

    m_Log.Write( "job received name=%s pch=0x%016" PRIx64 " waiting=%u", jobName.Get(), pchId, isJobWaiting ? 1u : 0u );

    if ( missingPchId != 0 )
    {
        MutexHolder mhC( m_ClientListMutex );
        MutexHolder mh2( cs->m_Mutex );
        RequestPchIfMissing( *cs, missingPchId );
    }
}

// Process( MsgManifest )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgManifest * msg, const void * payload, size_t payloadSize )
{
    m_NumManifestsReceived.Increment();

    // A gateway's local pool synchronizes the toolchain as a worker does, before it is forwarded
    if ( m_JobQueueRemote )
    {
        ReceiveManifest( connection, msg->GetToolId(), payload, payloadSize );
    }
    if ( m_Forwarder )
    {
        const ClientState * cs = (const ClientState *)connection->GetUserData();
        m_Forwarder->OnClientManifest( cs->m_Id, msg->GetToolId(), payload, payloadSize );
    }
}

// ReceiveManifest
//------------------------------------------------------------------------------
void Server::ReceiveManifest( const ConnectionInfo * connection, uint64_t toolId, const void * payload, size_t payloadSize )
{
    ToolManifest * manifest = nullptr;
    ConstMemoryStream ms( payload, payloadSize );
    ClientState * cs = (ClientState *)connection->GetUserData();

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
            OnUnusableToolchainData( connection, "corrupt MsgManifest" );
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
    }

    // In push mode, files arrive proactively from the coordinator.
    // No need to request missing files.
}

// Process( MsgFile )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgFile * msg, const void * payload, size_t payloadSize )
{
    m_NumToolFilesReceived.Increment();

    // A gateway's local pool synchronizes the toolchain as a worker does, before it is forwarded
    if ( m_JobQueueRemote )
    {
        ReceiveFile( connection, msg->GetToolId(), msg->GetFileId(), payload, payloadSize );
    }
    if ( m_Forwarder )
    {
        const ClientState * cs = (const ClientState *)connection->GetUserData();
        m_Forwarder->OnClientFile( cs->m_Id, msg->GetToolId(), msg->GetFileId(), payload, payloadSize );
    }
}

// ReceiveFile
//------------------------------------------------------------------------------
void Server::ReceiveFile( const ConnectionInfo * connection, uint64_t toolId, uint32_t fileId, const void * payload, size_t payloadSize )
{
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
                OnUnusableToolchainData( connection, "corrupt MsgFile" );
            }
            else
            {
                // something went wrong storing the file
                AStackString fileName;
                manifest->GetRemoteFilePath( fileId, fileName );
                LogWarning( "Failed to store fileId %u for manifest 0x%" PRIx64 "\n"
                           " - %s\n",
                           fileId,
                           toolId,
                           fileName.Get() );
                if ( m_Forwarder == nullptr )
                {
                    Disconnect( connection );
                }
            }
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
    m_JobQueueRemote->WakeMainThread();
}

// OnUnusableToolchainData
//------------------------------------------------------------------------------
void Server::OnUnusableToolchainData( const ConnectionInfo * connection, const char * what )
{
    const ClientState * cs = (const ClientState *)connection->GetUserData();
    AStackString remoteAddr;
    TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
    if ( m_Forwarder )
    {
        // The forwarder has the same data, and decides for itself whether the client stays
        LogWarning( "The gateway's local pool cannot use the toolchain '%s' (%s) sent: %s\n", remoteAddr.Get(), cs->m_HostName.Get(), what );
        return;
    }

    ASSERT( false && "Toolchain data corrupt" );
    LogWarning( "Disconnecting '%s' (%s) due to %s\n", remoteAddr.Get(), cs->m_HostName.Get(), what );
    Disconnect( connection );
}

// ReleaseReadyWaitingJobs
//------------------------------------------------------------------------------
void Server::ReleaseReadyWaitingJobs()
{
    // Queue any jobs whose toolchain has finished syncing AND whose PCH (if
    // any) is now in the cache. Called whenever a dependency lands —
    // typically the final MsgFile of a toolchain or a MsgPchFile.
    if ( m_JobQueueRemote == nullptr )
    {
        return; // A pure gateway holds no jobs
    }
    ReleaseReadyLocalJobs();

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

            const uint64_t pchId = job->GetPchId();
            bool pchReady = true;
            if ( pchId != 0 && job->GetPchCachePath().IsEmpty() )
            {
                if ( TryResolvePch( *job ) )
                {
                    cs->m_PchNeededSent.FindAndErase( pchId );
                }
                else
                {
                    RequestPchIfMissing( *cs, pchId );
                    pchReady = false;
                }
            }
            if ( !manifest->IsSynchronized() || !pchReady ) { continue; }

            cs->m_WaitingJobs.EraseIndex( (size_t)i );
            m_JobQueueRemote->QueueJob( job );
            PROTOCOL_DEBUG( "Server: Job %x can now be started\n", job );
        }
    }
}

// GetLocalSlots
//------------------------------------------------------------------------------
uint32_t Server::GetLocalSlots() const
{
    if ( m_Forwarder == nullptr || m_JobQueueRemote == nullptr )
    {
        return 0;
    }
    // None when the worker gives no CPUs: disabled, idle on a busy machine, or short of disk or memory
    const uint32_t numCPUs = WorkerThreadRemote::GetNumCPUsToUse();
    return ( numCPUs > 0 ) ? ( numCPUs + m_PrefetchBuffer ) : 0;
}

// IsToolchainSynchronized
//------------------------------------------------------------------------------
bool Server::IsToolchainSynchronized( uint64_t toolId ) const
{
    MutexHolder manifestMH( m_ToolManifestsMutex );
    ToolManifest * const * found = m_Tools.FindDeref( toolId );
    return found && ( *found )->IsSynchronized();
}

// RunLocalJob
//------------------------------------------------------------------------------
void Server::RunLocalJob( const Protocol::MsgJob & msg, const void * jobData, size_t jobDataSize )
{
    ASSERT( m_Forwarder );

    ConstMemoryStream ms( jobData, jobDataSize );
    Job * job = FNEW( Job( ms ) );
    job->SetUserData( &s_LocalJobTag );
    job->SetResultCompressionLevel( msg.GetResultCompressionLevel(), true /*allowZstdUse*/ );
    const AStackString<> jobName( job->GetRemoteName() ); // The job may be finished and gone as soon as it is queued
    const uint64_t pchId = job->GetPchId();

    LocalJobState state = LOCAL_JOB_CANNOT_RUN; // In a pure gateway, which has no local pool
    if ( m_JobQueueRemote )
    {
        // Held from here to its end, as a worker's job holds them, so neither is evicted while it waits
        if ( pchId != 0 )
        {
            job->SetPchLease( AcquirePchLease( pchId ) );
        }
        {
            MutexHolder manifestMH( m_ToolManifestsMutex );
            ToolManifest ** found = m_Tools.FindDeref( msg.GetToolId() );
            if ( found )
            {
                job->SetToolManifest( *found );
            }
        }

        // Checked and parked under one lock, so a toolchain or PCH that lands meanwhile releases it
        MutexHolder mh( m_LocalJobsMutex );
        state = GetLocalJobState( *job );
        if ( state == LOCAL_JOB_READY )
        {
            m_JobQueueRemote->QueueJob( job );
        }
        else if ( state == LOCAL_JOB_WAITING )
        {
            m_LocalWaitingJobs.Append( job );
        }
    }
    if ( state == LOCAL_JOB_CANNOT_RUN )
    {
        MutexHolder mh( m_LocalJobsMutex );
        m_LocalEndedJobs.Append( job );
    }

    m_Log.Write( "job received name=%s pch=0x%016" PRIx64 " waiting=%u local=1", jobName.Get(), pchId, ( state == LOCAL_JOB_WAITING ) ? 1u : 0u );
    if ( ( state == LOCAL_JOB_CANNOT_RUN ) && m_JobQueueRemote )
    {
        m_JobQueueRemote->WakeMainThread();
    }
}

// GetLocalJobState
//------------------------------------------------------------------------------
Server::LocalJobState Server::GetLocalJobState( Job & job )
{
    // The gateway gives the local pool a job once its toolchain is synchronized and its PCH stored here,
    // so a job that cannot have them is not waited for
    const ToolManifest * manifest = job.GetToolManifest();
    if ( manifest == nullptr )
    {
        return LOCAL_JOB_CANNOT_RUN;
    }
    bool pchReady = true;
    const uint64_t pchId = job.GetPchId();
    if ( ( pchId != 0 ) && job.GetPchCachePath().IsEmpty() && ( TryResolvePch( job ) == false ) )
    {
        if ( IsPchCached( pchId ) == false )
        {
            return LOCAL_JOB_CANNOT_RUN; // Dropped as damaged, or never stored
        }
        pchReady = false; // Being unpacked
    }
    return ( manifest->IsSynchronized() && pchReady ) ? LOCAL_JOB_READY : LOCAL_JOB_WAITING;
}

// ReleaseReadyLocalJobs
//------------------------------------------------------------------------------
void Server::ReleaseReadyLocalJobs()
{
    bool anyEnded = false;
    {
        MutexHolder mh( m_LocalJobsMutex );
        for ( size_t i = m_LocalWaitingJobs.GetSize(); i > 0; --i )
        {
            Job * job = m_LocalWaitingJobs[ i - 1 ];
            const LocalJobState state = GetLocalJobState( *job );
            if ( state == LOCAL_JOB_WAITING )
            {
                continue;
            }
            m_LocalWaitingJobs.EraseIndex( i - 1 );
            if ( state == LOCAL_JOB_READY )
            {
                m_JobQueueRemote->QueueJob( job );
            }
            else
            {
                m_LocalEndedJobs.Append( job );
                anyEnded = true;
            }
        }
    }
    if ( anyEnded )
    {
        m_JobQueueRemote->WakeMainThread();
    }
}

// EndLocalJobsWaitingForPch
//------------------------------------------------------------------------------
void Server::EndLocalJobsWaitingForPch( uint64_t pchId )
{
    bool anyEnded = false;
    {
        MutexHolder mh( m_LocalJobsMutex );
        for ( size_t i = m_LocalWaitingJobs.GetSize(); i > 0; --i )
        {
            Job * job = m_LocalWaitingJobs[ i - 1 ];
            if ( ( job->GetPchId() == pchId ) && job->GetPchCachePath().IsEmpty() )
            {
                m_LocalWaitingJobs.EraseIndex( i - 1 );
                m_LocalEndedJobs.Append( job );
                anyEnded = true;
            }
        }
    }
    if ( anyEnded && m_JobQueueRemote )
    {
        m_JobQueueRemote->WakeMainThread();
    }
}

// RequestPchIfMissing
//------------------------------------------------------------------------------
void Server::RequestPchIfMissing( ClientState & cs, uint64_t pchId )
{
    if ( cs.m_Outbox && MarkPchNeeded( cs, pchId ) )
    {
        cs.m_Outbox->PushMessage( Protocol::MsgPchNeeded( pchId ) );
    }
}

// MarkPchNeeded
//------------------------------------------------------------------------------
bool Server::MarkPchNeeded( ClientState & cs, uint64_t pchId )
{
    if ( cs.m_PchNeededSent.Find( pchId ) || IsPchCached( pchId ) || IsPchBeingReceived( pchId ) )
    {
        return false;
    }

    // Once until it arrives: the client sends it again when asked, and the chunks take a while
    cs.m_PchNeededSent.Append( pchId );
    FLOG_VERBOSE( "PCH 0x%016" PRIx64 " is needed by a job from '%s' and is not cached: asking for it\n",
                  pchId, cs.m_HostName.Get() );
    m_Log.Write( "pch needed id=0x%016" PRIx64 " host=%s", pchId, cs.m_HostName.Get() );
    return true;
}

// IsPchBeingReceived
//------------------------------------------------------------------------------
bool Server::IsPchBeingReceived( uint64_t pchId ) const
{
    for ( const ClientState * cs : m_ClientList )
    {
        if ( cs->m_PchReceiver.IsReceiving( pchId ) )
        {
            return true;
        }
    }
    return false;
}

// IsPchCached
//------------------------------------------------------------------------------
bool Server::IsPchCached( uint64_t pchId )
{
    MutexHolder mh( m_PchCacheMutex );
    return ( FindStoredPch( pchId ) != nullptr );
}

// Process( MsgPchFile )
//------------------------------------------------------------------------------
void Server::Process( const ConnectionInfo * connection, const Protocol::MsgPchFile * msg, const void * payload, size_t payloadSize )
{
    ClientState * cs = (ClientState *)connection->GetUserData();
    ASSERT( cs );

    const uint64_t pchId = msg->GetPchId();
    m_NumPchChunksReceived.Increment();

    AStackString pchFilePath;
    if ( BuildPchFilePath( m_CacheDir, pchId, pchFilePath ) == false )
    {
        LogWarning( "Failed to get temp dir for PCH cache\n" );
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

    // The disk is read before m_PchCacheMutex is taken
    FileIO::FileInfo storedInfo;
    const uint64_t storedSize = FileIO::GetFileInfo( pchFilePath, storedInfo ) ? storedInfo.m_Size : msg->GetCompressedSize();
    uint64_t cacheBytes = storedSize;
    {
        MutexHolder mh( m_PchCacheMutex );
        cacheBytes += ( CountPchBytes( m_PchCache ) + CountRawPchBytes() );
    }
    const PchCacheLimits limits = GetPchCacheLimits( cacheBytes );

    // Add to in-memory cache
    bool newlyCached = true;
    Array<uint64_t> evictedIds;
    {
        MutexHolder mh( m_PchCacheMutex );

        const uint64_t now = Time::GetCurrentFileTime();

        // Check not already in cache (race with another connection)
        if ( PchCacheEntry * existing = FindStoredPch( pchId ) )
        {
            existing->lastWriteTime = now;
            newlyCached = false;
        }

        if ( newlyCached )
        {
            PchCacheEntry entry;
            entry.pchId = pchId;
            entry.filePath = pchFilePath;
            entry.lastWriteTime = now;
            entry.size = storedSize; // What the file on disk comes to
            m_PchCache.Append( Move( entry ) );

            // Every job holds a lease on its PCH from when it arrives, so what leases or an unpack
            // hold stays, and so does the PCH just received (the jobs waiting for it have leases too)
            Array<uint64_t> pchIdsInUse;
            pchIdsInUse.Append( pchId );
            for ( const PchRawEntry & raw : m_PchRaw )
            {
                if ( ( raw.numLeases > 0 ) || ( raw.state == PchRawEntry::UNPACKING ) )
                {
                    pchIdsInUse.Append( raw.pchId );
                }
            }
            EvictPchs( m_PchCache, limits.m_Bytes, limits.m_MinEntries, pchIdsInUse, &evictedIds );
            for ( const uint64_t evictedId : evictedIds )
            {
                const PchRawEntry * raw = FindRawPch( evictedId );
                if ( raw )
                {
                    ASSERT( raw->numLeases == 0 );
                    m_PchRaw.EraseIndex( (size_t)( raw - m_PchRaw.Begin() ) );
                }
            }
        }
    }
    cs->m_PchReceiver.EndReceiving(); // Jobs find it in the cache from here on

    m_Log.Write( "pch received id=0x%016" PRIx64 " bytes=%u seconds=%.1f", pchId, msg->GetCompressedSize(), (double)cs->m_PchReceiver.GetSecondsReceiving() );
    for ( const uint64_t evictedId : evictedIds )
    {
        m_Log.Write( "pch evicted id=0x%016" PRIx64, evictedId );
    }

    // It is here, so no connection needs to ask for it again if it is evicted later
    {
        MutexHolder mhC( m_ClientListMutex );
        for ( ClientState * otherCS : m_ClientList )
        {
            MutexHolder mh2( otherCS->m_Mutex );
            otherCS->m_PchNeededSent.FindAndErase( pchId );
        }
    }

    if ( newlyCached )
    {
        FLOG_OUTPUT( "PCH cached: 0x%016" PRIx64 " -> %s (cached=%s)\n",
                     pchId, pchFilePath.Get(), alreadyCached ? "disk" : "new" );
    }

    if ( m_Forwarder )
    {
        m_Forwarder->OnPchStored( pchId, msg->GetUncompressedSize(), msg->GetCompressedSize() );
    }

    // Release any jobs that were waiting for this PCH
    ReleaseReadyWaitingJobs();
    if ( m_JobQueueRemote )
    {
        m_JobQueueRemote->WakeMainThread();
    }
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

    const AString & basePath = m_CacheDir;
    if ( basePath.IsEmpty() || ( FileIO::DirectoryExists( basePath ) == false ) )
    {
        return;
    }

    // Use GetFilesEx to get timestamps for LRU eviction
    Array<PchCacheEntry> scannedEntries;

    // Scan for the compressed PCHs (pch.zst) in the pch.<hex> directories.
    // Parse pchId from the directory name rather than reading/hashing each file.
    // A working copy (pch.pch) is not the cache's: no job exists yet, so it is deleted. That also
    // removes the directories that hold nothing else, which is all that PCHs stored as they were
    // before they were compressed leave. There is one worker per machine (see FBuildWorker's Main), so
    // no other process has a working copy in use. Files in other directories are not touched.
    Array<FileIO::FileInfo> pchFiles;
    Array<AString> pchPatterns;
    pchPatterns.Append( AString( PchBlob::kStoredFileName ) );
    pchPatterns.Append( AString( PchBlob::kRawFileName ) );
    if ( FileIO::GetFilesEx( basePath, &pchPatterns, true /*recurse*/, &pchFiles ) )
    {
        for ( const FileIO::FileInfo & fi : pchFiles )
        {
            const uint64_t pchId = GetPchIdOfFile( fi.m_Name, basePath );
            if ( pchId == 0 ) { continue; }

            if ( fi.m_Name.EndsWith( PchBlob::kRawFileName ) )
            {
                FileIO::FileDelete( fi.m_Name.Get() );
                continue;
            }

            PchCacheEntry entry;
            entry.pchId = pchId;
            entry.filePath = fi.m_Name;
            entry.lastWriteTime = fi.m_LastWriteTime;
            entry.size = fi.m_Size;
            scannedEntries.Append( Move( entry ) );
        }
    }

    TempFiles::RemoveStale( basePath, PchReceiver::kTempFilePrefix, PchReceiver::kTempFileExtension, true );
    TempFiles::RemoveStale( basePath, kUnpackTempFilePrefix, kUnpackTempFileExtension, true );
    RemoveEmptyPchDirectories( basePath );

    if ( scannedEntries.IsEmpty() )
    {
        return;
    }

    // No jobs exist yet at startup, so nothing is in use
    const PchCacheLimits limits = GetPchCacheLimits( CountPchBytes( scannedEntries ) );
    Array<uint64_t> evictedIds;
    EvictPchs( scannedEntries, limits.m_Bytes, limits.m_MinEntries, Array<uint64_t>(), &evictedIds );
    for ( const uint64_t evictedId : evictedIds )
    {
        m_Log.Write( "pch evicted id=0x%016" PRIx64, evictedId );
    }

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

// EvictPchs
//------------------------------------------------------------------------------
/*static*/ void Server::EvictPchs( Array<PchCacheEntry> & entries,
                                    uint64_t budgetBytes,
                                    uint32_t minEntries,
                                    const Array<uint64_t> & pchIdsInUse,
                                    Array<uint64_t> * outEvictedIds )
{
    Array<size_t> evictions;
    SelectPchEvictions( entries, budgetBytes, minEntries, pchIdsInUse, evictions );
    evictions.Sort();
    for ( size_t i = evictions.GetSize(); i > 0; --i )
    {
        const size_t idx = evictions[ i - 1 ];
        RemovePchFiles( entries[ idx ] );
        FLOG_VERBOSE( "PCH cache: evicting 0x%016" PRIx64 " (%u MiB compressed)\n", entries[ idx ].pchId, (uint32_t)( entries[ idx ].size / MEGABYTE ) );
        if ( outEvictedIds )
        {
            outEvictedIds->Append( entries[ idx ].pchId );
        }
        entries.EraseIndex( idx );
    }
}

// CalculatePchCacheBudget
//------------------------------------------------------------------------------
/*static*/ uint64_t Server::CalculatePchCacheBudget( uint64_t freeBytes, uint64_t cacheBytes )
{
    // The space the cache could have: what is free, plus what it already holds
    const uint64_t available = ( freeBytes > ( kNoLimit - cacheBytes ) ) ? kNoLimit : ( freeBytes + cacheBytes );
    const uint64_t share = ( available / 100 ) * kPchCacheSharePercent;
    return Math::Max( kMinPchCacheBytes, Math::Min( kMaxPchCacheBytes, share ) );
}

// CalculateRawPchBudget
//------------------------------------------------------------------------------
/*static*/ uint64_t Server::CalculateRawPchBudget( uint64_t freeBytes, uint64_t cacheBytes )
{
    const uint64_t available = ( freeBytes > ( kNoLimit - cacheBytes ) ) ? kNoLimit : ( freeBytes + cacheBytes );
    const uint64_t share = ( available / 100 ) * kRawPchSharePercent;
    return Math::Min( kMaxRawPchBytes, share ); // No minimum: on a full disk nothing is kept for later
}

// QueryPchCacheLimits
//------------------------------------------------------------------------------
/*static*/ bool Server::QueryPchCacheLimits( const AString & cacheDir, uint64_t cacheBytes, PchCacheLimits & outLimits )
{
    uint64_t freeBytes = 0;
    if ( cacheDir.IsEmpty() ||
         ( FileIO::EnsurePathExists( cacheDir ) == false ) ||
         ( FileIO::GetFreeDiskSpace( cacheDir, freeBytes ) == false ) )
    {
        return false;
    }
    outLimits.m_Bytes = CalculatePchCacheBudget( freeBytes, cacheBytes );
    outLimits.m_MinEntries = kMinCachedPchEntries;
    outLimits.m_RawBytes = CalculateRawPchBudget( freeBytes, cacheBytes );
    return true;
}

// GetPchCacheLimits
//------------------------------------------------------------------------------
Server::PchCacheLimits Server::GetPchCacheLimits( uint64_t cacheBytes ) const
{
    PchCacheLimits limits;
    if ( m_PchCacheLimitsFunc( m_CacheDir, cacheBytes, limits ) == false )
    {
        // The free space is not known: keep the cache small
        static bool logged = false;
        if ( logged == false )
        {
            logged = true;
            FLOG_WARN( "Could not get the free disk space of the PCH cache: limiting it to %u MiB\n", (uint32_t)( kMinPchCacheBytes / MEGABYTE ) );
        }
        limits.m_Bytes = kMinPchCacheBytes;
        limits.m_MinEntries = kMinCachedPchEntries;
        limits.m_RawBytes = 0;
    }
    return limits;
}

// CountPchBytes
//------------------------------------------------------------------------------
/*static*/ uint64_t Server::CountPchBytes( const Array<PchCacheEntry> & entries )
{
    uint64_t bytes = 0;
    for ( const PchCacheEntry & e : entries )
    {
        bytes += e.size;
    }
    return bytes;
}

// SelectPchEvictions
//------------------------------------------------------------------------------
/*static*/ void Server::SelectPchEvictions( const Array<PchCacheEntry> & entries,
                                             uint64_t budgetBytes,
                                             uint32_t minEntries,
                                             const Array<uint64_t> & pchIdsInUse,
                                             Array<size_t> & outIndicesToEvict )
{
    const LruLimits limits = { kMaxCachedPchEntries, minEntries, budgetBytes };
    SelectLeastRecentlyUsed( entries.GetSize(),
                             limits,
                             [ &entries ]( size_t i ) { return entries[ i ].lastWriteTime; },
                             [ &entries ]( size_t i ) { return entries[ i ].size; },
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
        const LruLimits limits = { kMaxCachedToolchains, 0, kNoLimit };
        SelectLeastRecentlyUsed( candidates.GetSize(),
                                 limits,
                                 [ &candidates ]( size_t i ) { return candidates[ i ]->GetLastUsed(); },
                                 []( size_t ) { return (uint64_t)0; },
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

// FindStoredPch
//------------------------------------------------------------------------------
Server::PchCacheEntry * Server::FindStoredPch( uint64_t pchId )
{
    for ( PchCacheEntry & e : m_PchCache )
    {
        if ( e.pchId == pchId )
        {
            return &e;
        }
    }
    return nullptr;
}

// FindCachedPch
//------------------------------------------------------------------------------
bool Server::FindCachedPch( uint64_t pchId, AString & outPath )
{
    MutexHolder mh( m_PchCacheMutex );
    PchCacheEntry * stored = FindStoredPch( pchId );
    if ( stored == nullptr )
    {
        return false;
    }
    stored->lastWriteTime = Time::GetCurrentFileTime();
    outPath = stored->filePath;
    return true;
}

// DropStoredPch
//------------------------------------------------------------------------------
void Server::DropStoredPch( uint64_t pchId )
{
    PchCacheEntry dropped;
    {
        MutexHolder mh( m_PchCacheMutex );
        PchCacheEntry * stored = FindStoredPch( pchId );
        if ( stored == nullptr )
        {
            return;
        }
        dropped = Move( *stored );
        m_PchCache.Erase( stored );

        // Its working copy goes with it, so the next job has one made again
        PchRawEntry * raw = FindRawPch( pchId );
        if ( raw && ( raw->state == PchRawEntry::READY ) )
        {
            raw->state = PchRawEntry::ABSENT;
            if ( raw->numLeases == 0 )
            {
                m_PchRaw.EraseIndex( (size_t)( raw - m_PchRaw.Begin() ) );
            }
        }
    }
    RemovePchFiles( dropped ); // The disk is written with no lock held
    m_Log.Write( "pch dropped id=0x%016" PRIx64, pchId );
}

// GetCachedPchIds
//------------------------------------------------------------------------------
void Server::GetCachedPchIds( Array<uint64_t> & outPchIds ) const
{
    MutexHolder mh( m_PchCacheMutex );
    outPchIds.Clear();
    for ( const PchCacheEntry & e : m_PchCache )
    {
        outPchIds.Append( e.pchId );
    }
}

// FindRawPch
//------------------------------------------------------------------------------
Server::PchRawEntry * Server::FindRawPch( uint64_t pchId )
{
    for ( PchRawEntry & raw : m_PchRaw )
    {
        if ( raw.pchId == pchId )
        {
            return &raw;
        }
    }
    return nullptr;
}

// CountRawPchBytes
//------------------------------------------------------------------------------
uint64_t Server::CountRawPchBytes() const
{
    uint64_t bytes = 0;
    for ( const PchRawEntry & raw : m_PchRaw )
    {
        if ( raw.state == PchRawEntry::READY )
        {
            bytes += raw.size;
        }
    }
    return bytes;
}

// GetRawPchPath
//------------------------------------------------------------------------------
/*static*/ bool Server::GetRawPchPath( const AString & storedFile, AString & outRawFile )
{
    AStackString<> directory;
    if ( GetDirectoryOf( storedFile, directory ) == false )
    {
        return false;
    }
    outRawFile.Format( "%s%c%s", directory.Get(), (char)NATIVE_SLASH, PchBlob::kRawFileName );
    return true;
}

// RemovePchFiles
//------------------------------------------------------------------------------
/*static*/ void Server::RemovePchFiles( const PchCacheEntry & entry )
{
    AStackString<> rawFile;
    if ( GetRawPchPath( entry.filePath, rawFile ) )
    {
        FileIO::FileDelete( rawFile.Get() );
    }
    FileIO::FileDelete( entry.filePath.Get() );

    // The directory goes with it, unless something else is in it
    AStackString<> directory;
    if ( GetDirectoryOf( entry.filePath, directory ) )
    {
        FileIO::DirectoryDelete( directory );
    }
}

// AcquirePchLease
//------------------------------------------------------------------------------
PchLease Server::AcquirePchLease( uint64_t pchId )
{
    MutexHolder mh( m_PchCacheMutex );
    PchRawEntry * raw = FindRawPch( pchId );
    if ( raw == nullptr )
    {
        PchRawEntry entry;
        entry.pchId = pchId;
        m_PchRaw.Append( entry );
        raw = &m_PchRaw[ m_PchRaw.GetSize() - 1 ];
    }
    ++raw->numLeases;
    raw->lastUsed = Time::GetCurrentFileTime();
    return PchLease( this, pchId );
}

// ReleasePchLease
//------------------------------------------------------------------------------
/*virtual*/ void Server::ReleasePchLease( uint64_t pchId )
{
    MutexHolder mh( m_PchCacheMutex );
    PchRawEntry * raw = FindRawPch( pchId );
    ASSERT( raw && ( raw->numLeases > 0 ) );
    if ( raw == nullptr )
    {
        return;
    }
    --raw->numLeases;
    raw->lastUsed = Time::GetCurrentFileTime();
    if ( ( raw->numLeases == 0 ) && ( raw->state == PchRawEntry::ABSENT ) )
    {
        m_PchRaw.EraseIndex( (size_t)( raw - m_PchRaw.Begin() ) );
    }
}

// TryResolvePch
//------------------------------------------------------------------------------
bool Server::TryResolvePch( Job & job )
{
    const uint64_t pchId = job.GetPchId();
    MutexHolder mh( m_PchCacheMutex );

    PchCacheEntry * stored = FindStoredPch( pchId );
    if ( stored == nullptr )
    {
        return false; // Not here: the job waits for it to be sent
    }
    const uint64_t now = Time::GetCurrentFileTime();
    stored->lastWriteTime = now;

    AStackString<> rawFile;
    PchRawEntry * raw = FindRawPch( pchId );
    ASSERT( raw ); // The job has a lease
    if ( ( raw == nullptr ) || ( GetRawPchPath( stored->filePath, rawFile ) == false ) )
    {
        return false;
    }

    if ( raw->state == PchRawEntry::READY ) // Only removed under this mutex, with its entry
    {
        raw->lastUsed = now;
        job.SetPchCachePath( rawFile );
        return true;
    }
    if ( raw->state == PchRawEntry::ABSENT )
    {
        raw->state = PchRawEntry::UNPACKING;
        m_PchUnpackQueue.Append( pchId );
        m_PchUnpackSemaphore.Signal();
    }
    return false; // Until the thread that unpacks it is done
}

// UnpackThreadFuncStatic
//------------------------------------------------------------------------------
/*static*/ uint32_t Server::UnpackThreadFuncStatic( void * param )
{
    PROFILE_SET_THREAD_NAME( "PchUnpackThread" );

    Server * s = (Server *)param;
    s->UnpackThreadFunc();
    return 0;
}

// UnpackThreadFunc
//------------------------------------------------------------------------------
void Server::UnpackThreadFunc()
{
    while ( m_ShouldExit.Load() == false )
    {
        m_PchUnpackSemaphore.Wait();

        // One at a time: the disk and a core are what a build on this worker needs as well
        while ( m_ShouldExit.Load() == false )
        {
            uint64_t pchId = 0;
            {
                MutexHolder mh( m_PchCacheMutex );
                if ( m_PchUnpackQueue.IsEmpty() )
                {
                    break;
                }
                pchId = m_PchUnpackQueue[ 0 ];
                m_PchUnpackQueue.PopFront();
            }
            UnpackPch( pchId );
        }
    }
}

// UnpackPch
//------------------------------------------------------------------------------
void Server::UnpackPch( uint64_t pchId )
{
    PROFILE_FUNCTION;

    AStackString<> storedFile;
    AStackString<> rawFile;
    bool haveStored = false;
    {
        MutexHolder mh( m_PchCacheMutex );
        PchRawEntry * raw = FindRawPch( pchId );
        if ( raw == nullptr )
        {
            return;
        }
        if ( raw->numLeases == 0 )
        {
            // The jobs that needed it are gone
            m_PchRaw.EraseIndex( (size_t)( raw - m_PchRaw.Begin() ) );
            return;
        }
        const PchCacheEntry * stored = FindStoredPch( pchId );
        if ( stored )
        {
            storedFile = stored->filePath;
            haveStored = GetRawPchPath( storedFile, rawFile );
        }
    }

    PchBlob::UnpackResult result = PchBlob::UNPACK_CORRUPT; // A PCH that is not stored is as good as a damaged one
    uint64_t rawSize = 0;
    const Timer timer;
    if ( haveStored )
    {
        const char * lastSlash = rawFile.FindLast( NATIVE_SLASH );
        const AStackString<> dir( rawFile.Get(), lastSlash + 1 );
        for ( uint32_t attempt = 0; attempt < 2; ++attempt )
        {
            AStackString<> tempFile;
            TempFiles::MakeName( dir, kUnpackTempFilePrefix, kUnpackTempFileExtension, tempFile );
            m_NumPchUnpackAttempts.Increment();
            result = PchBlob::Unpack( storedFile, pchId, tempFile, rawFile, &m_ShouldExit, rawSize );
            if ( result != PchBlob::UNPACK_CANNOT_WRITE )
            {
                break;
            }
            if ( attempt == 0 )
            {
                LogWarning( "PCH 0x%016" PRIx64 ": the working copy cannot be written, dropping those nothing uses and trying again\n", pchId );
                DropUnusedRawPchs();
            }
        }
        if ( result == PchBlob::UNPACK_OK )
        {
            m_NumPchsUnpacked.Increment();
        }
    }

    {
        MutexHolder mh( m_PchCacheMutex );
        PchRawEntry * raw = FindRawPch( pchId );
        if ( result == PchBlob::UNPACK_OK )
        {
            if ( raw )
            {
                raw->state = PchRawEntry::READY;
                raw->size = rawSize;
                raw->lastUsed = Time::GetCurrentFileTime();
            }
        }
        else
        {
            if ( raw )
            {
                raw->state = PchRawEntry::ABSENT;
                if ( raw->numLeases == 0 )
                {
                    m_PchRaw.EraseIndex( (size_t)( raw - m_PchRaw.Begin() ) );
                }
            }

            // Only a damaged file is dropped. One that cannot be unpacked for want of room is fine:
            // sending it again would not make room.
            PchCacheEntry * stored = ( result == PchBlob::UNPACK_CORRUPT ) ? FindStoredPch( pchId ) : nullptr;
            if ( stored )
            {
                RemovePchFiles( *stored );
                m_PchCache.EraseIndex( (size_t)( stored - m_PchCache.Begin() ) );
            }
        }
    }

    if ( result == PchBlob::UNPACK_ABORTED )
    {
        return;
    }
    if ( result == PchBlob::UNPACK_OK )
    {
        FLOG_VERBOSE( "PCH unpacked: 0x%016" PRIx64 " %u MiB in %u ms\n", pchId, (uint32_t)( rawSize / MEGABYTE ), (uint32_t)timer.GetElapsedMS() );
    }
    else if ( result == PchBlob::UNPACK_CORRUPT )
    {
        // Clients are asked for it again when a job needs it
        LogWarning( "PCH 0x%016" PRIx64 " is damaged: dropping it\n", pchId );
    }
    else
    {
        LogWarning( "PCH 0x%016" PRIx64 " cannot be unpacked, as its working copy cannot be written: returning the jobs that need it to their clients\n", pchId );
        DisconnectClientsWaitingForPch( pchId );
        EndLocalJobsWaitingForPch( pchId );
        return; // Nothing changed for the jobs of other clients: they would only ask for it to be unpacked again
    }

    ReleaseReadyWaitingJobs();
    if ( m_JobQueueRemote )
    {
        m_JobQueueRemote->WakeMainThread();
    }
}

// DisconnectClientsWaitingForPch
//------------------------------------------------------------------------------
void Server::DisconnectClientsWaitingForPch( uint64_t pchId )
{
    // Their jobs are cancelled and so go back to the clients, to be built elsewhere. Under the lock, as a
    // connection outlives its client's place in the list.
    MutexHolder mhC( m_ClientListMutex );
    for ( ClientState * cs : m_ClientList )
    {
        MutexHolder mh( cs->m_Mutex );
        for ( const Job * job : cs->m_WaitingJobs )
        {
            if ( ( job->GetPchId() == pchId ) && job->GetPchCachePath().IsEmpty() )
            {
                Disconnect( cs->m_Connection );
                break;
            }
        }
    }
}

// SelectRawPchTrims
//------------------------------------------------------------------------------
/*static*/ void Server::SelectRawPchTrims( const Array<PchRawEntry> & entries,
                                            uint64_t budgetBytes,
                                            uint64_t now,
                                            uint32_t idleSeconds,
                                            Array<size_t> & outIndicesToTrim )
{
    outIndicesToTrim.Clear();

    // The working copies nothing uses, the one used longest ago first
    Array<size_t> unused;
    uint64_t unusedBytes = 0;
    for ( size_t i = 0; i < entries.GetSize(); ++i )
    {
        if ( ( entries[ i ].state == PchRawEntry::READY ) && ( entries[ i ].numLeases == 0 ) )
        {
            unused.Append( i );
            unusedBytes += entries[ i ].size;
        }
    }
    unused.Sort( [ &entries ]( size_t a, size_t b ) { return ( entries[ a ].lastUsed < entries[ b ].lastUsed ); } );

    for ( const size_t index : unused )
    {
        const uint64_t lastUsed = entries[ index ].lastUsed;
        const bool idle = ( now > lastUsed ) && ( Time::FileTimeToSeconds( now - lastUsed ) > idleSeconds );
        if ( idle || ( unusedBytes > budgetBytes ) )
        {
            outIndicesToTrim.Append( index );
            unusedBytes -= entries[ index ].size;
        }
    }
}

// TrimRawPchs
//------------------------------------------------------------------------------
void Server::TrimRawPchs( uint64_t now )
{
    uint64_t cacheBytes = 0;
    {
        MutexHolder mh( m_PchCacheMutex );
        cacheBytes = ( CountPchBytes( m_PchCache ) + CountRawPchBytes() );
    }
    RemoveUnusedRawPchs( now, GetPchCacheLimits( cacheBytes ).m_RawBytes );
}

// DropUnusedRawPchs
//------------------------------------------------------------------------------
void Server::DropUnusedRawPchs()
{
    RemoveUnusedRawPchs( Time::GetCurrentFileTime(), 0 );
}

// RemoveUnusedRawPchs
//------------------------------------------------------------------------------
void Server::RemoveUnusedRawPchs( uint64_t now, uint64_t budgetBytes )
{
    // Deleted under the lock, so that a job that needs the PCH sees it is not there and has it made again
    MutexHolder mh( m_PchCacheMutex );
    Array<size_t> trims;
    SelectRawPchTrims( m_PchRaw, budgetBytes, now, kRawPchIdleSeconds, trims );
    trims.Sort();
    for ( size_t i = trims.GetSize(); i > 0; --i )
    {
        const size_t index = trims[ i - 1 ];
        const uint64_t pchId = m_PchRaw[ index ].pchId;
        AStackString<> rawFile;
        const PchCacheEntry * stored = FindStoredPch( pchId );
        if ( stored && GetRawPchPath( stored->filePath, rawFile ) )
        {
            FileIO::FileDelete( rawFile.Get() );
        }
        if ( rawFile.IsEmpty() || ( FileIO::FileExists( rawFile.Get() ) == false ) )
        {
            FLOG_VERBOSE( "PCH cache: dropping the working copy of 0x%016" PRIx64 "\n", pchId );
            m_PchRaw.EraseIndex( index );
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

        ShareSlotsOnTick();

        TouchToolchains();

        // Working copies nothing uses are dropped when they are too many or have not been used lately
        if ( m_PchTrimTimer.GetElapsed() > 10.0f )
        {
            m_PchTrimTimer.Restart();
            TrimRawPchs( Time::GetCurrentFileTime() );
        }

        if ( m_JobQueueRemote )
        {
            m_JobQueueRemote->MainThreadWait( 100 );
        }
        else
        {
            Thread::Sleep( 100 );
        }
    }
}

// FinalizeCompletedJobs
//------------------------------------------------------------------------------
void Server::FinalizeCompletedJobs()
{
    PROFILE_FUNCTION;

    // Local jobs that could not be run here end without a result, so the gateway can send them elsewhere
    Array<Job *> notRun;
    {
        MutexHolder mh( m_LocalJobsMutex );
        notRun.Swap( m_LocalEndedJobs );
    }
    for ( Job * job : notRun )
    {
        m_Log.Write( "job finished name=%s ms=0 result=not-run", job->GetRemoteName().Get() );
        MemoryStream none;
        m_Forwarder->OnLocalJobEnded( job->GetJobId(), false, false, none );
        FDELETE job;
    }

    if ( m_JobQueueRemote == nullptr )
    {
        return;
    }
    JobQueueRemote & jcr = *m_JobQueueRemote;
    Node::BuildResult result;
    while ( Job * job = jcr.GetCompletedJob( result ) )
    {
        const char * outcome = "aborted";
        if ( result == Node::BuildResult::eOk )
        {
            outcome = "ok";
        }
        else if ( result == Node::BuildResult::eFailed )
        {
            outcome = ( job->GetSystemErrorCount() > 0 ) ? "system-error" : "failed";
        }
        const Node * node = job->GetNode();
        m_Log.Write( "job finished name=%s ms=%u result=%s", job->GetRemoteName().Get(), node->GetLastBuildTime(), outcome );

        const bool hasResult = ( result == Node::BuildResult::eOk ) || ( result == Node::BuildResult::eFailed );
        if ( job->GetUserData() == &s_LocalJobTag )
        {
            // The gateway's: it is never cancelled with a client, and an aborted one ends without a result
            MemoryStream ms;
            if ( hasResult )
            {
                WriteJobResult( *job, result == Node::BuildResult::eOk, ms );
            }
            m_Forwarder->OnLocalJobEnded( job->GetJobId(), hasResult, hasResult && ( job->GetResultCompressionLevel() != 0 ), ms );
            FDELETE job;
            continue;
        }

        // Jobs that ended in a useful state are reported to the Client
        // Other jobs (like those that were cancelled) are not
        if ( hasResult )
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
                WriteJobResult( *job, result == Node::BuildResult::eOk, ms );

                // The client's outbox sends it, so no lock is held while the client reads
                MutexHolder mh( m_ClientListMutex );
                if ( ( m_ClientList.Find( cs ) != nullptr ) && cs->m_Outbox )
                {
                    cs->m_Outbox->PushResult( job->GetResultCompressionLevel() != 0, Move( ms ), 0 ); // A worker's result has no flags
                }
            }
        }

        FDELETE job;
    }
}

// GetPoolSlots
//------------------------------------------------------------------------------
uint32_t Server::GetPoolSlots() const
{
    return m_Forwarder ? m_Forwarder->GetPoolSlots() : ( WorkerThreadRemote::GetNumCPUsToUse() + m_PrefetchBuffer );
}

// ShareSlotsLocked
//------------------------------------------------------------------------------
void Server::ShareSlotsLocked( uint32_t poolSlots, uint32_t unmarkedClientId )
{
    // Every client learns a new pool, so one with no allowance (a gateway with nothing waiting) knows it has slots
    const bool poolChanged = ( poolSlots != m_Allocator.GetPoolSlots() );
    m_Allocator.SetPoolSlots( poolSlots );
    Array<uint32_t> changed;
    m_Allocator.Recompute( changed );
    m_ShareTimer.Restart();

    for ( ClientState * cs : m_ClientList )
    {
        const bool allowanceChanged = ( changed.Find( cs->m_Id ) != nullptr );
        if ( allowanceChanged )
        {
            cs->m_AllocatedCapacity = m_Allocator.GetAllowance( cs->m_Id );
            cs->m_MaxAllocatedCapacity = Math::Max( cs->m_MaxAllocatedCapacity, cs->m_AllocatedCapacity );
        }
        if ( ( allowanceChanged || poolChanged ) && ( cs->m_Id != unmarkedClientId ) )
        {
            MarkCapacityChanged( *cs );
        }
    }
}

// ShareSlotsOnTick
//------------------------------------------------------------------------------
void Server::ShareSlotsOnTick()
{
    const uint32_t poolSlots = GetPoolSlots();
    MutexHolder mh( m_ClientListMutex );
    if ( ( poolSlots != m_Allocator.GetPoolSlots() ) ||
         ( m_Allocator.IsScarce() && ( m_ShareTimer.GetElapsed() >= kRotateSeconds ) ) )
    {
        ShareSlotsLocked( poolSlots );
    }
}

// MarkCapacityChanged
//------------------------------------------------------------------------------
/*static*/ void Server::MarkCapacityChanged( ClientState & cs )
{
    cs.m_CapacityChanged.Store( true );
    if ( cs.m_Outbox )
    {
        cs.m_Outbox->Wake();
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
    if ( cs.m_Outbox == nullptr )
    {
        return; // It is going
    }
    MutexHolder manifestMH( m_ToolManifestsMutex );

    const Array<ToolManifestFile> & files = manifest->GetFiles();
    const size_t numFiles = files.GetSize();
    for ( size_t i = 0; i < numFiles; ++i )
    {
        const ToolManifestFile & f = files[ i ];
        if ( f.GetSyncState() == ToolManifestFile::NOT_SYNCHRONIZED )
        {
            // request this file
            cs.m_Outbox->PushMessage( Protocol::MsgRequestFile( manifest->GetToolId(), (uint32_t)i ) );

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
