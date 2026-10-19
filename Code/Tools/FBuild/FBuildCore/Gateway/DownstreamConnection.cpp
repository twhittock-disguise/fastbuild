// DownstreamConnection.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "DownstreamConnection.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/Gateway/GatewayWire.h"
#include "Tools/FBuild/FBuildCore/Helpers/JobStream.h"
#include "Tools/FBuild/FBuildCore/Protocol/SendKind.h"

// Core
#include "Core/Containers/Move.h"
#include "Core/Env/Assert.h"
#include "Core/Env/Types.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/Mem/Mem.h"
#include "Core/Profile/Profile.h"
#include "Core/Strings/AStackString.h"

// CONSTRUCTOR
//------------------------------------------------------------------------------
PRAGMA_DISABLE_PUSH_MSVC( 4355 ) // 'this' used in base member initializer list: the queue only calls its listener from Run
DownstreamConnection::DownstreamConnection( Owner & owner, const GatewayConfig::Worker & worker, uint32_t index )
    : m_Owner( owner )
    , m_Host( worker.m_Host )
    , m_Port( worker.m_Port )
    , m_Index( index )
    , m_Address( worker.m_Host )
    , m_SendQueue( *this, worker.m_Host, JobStream::kLanCompressionLevel, JobStream::kLanWindowLog )
{
    ASSERT( ( index >= 1 ) && ( index <= GatewayWire::kMaxWorkerIndex ) );
    if ( m_Port != Protocol::kPort )
    {
        m_Address.AppendFormat( ":%u", (uint32_t)m_Port );
    }
    SetConnectSendBufferSize( Protocol::kClientSendBufferSize );
}
PRAGMA_DISABLE_POP_MSVC

// DESTRUCTOR
//------------------------------------------------------------------------------
DownstreamConnection::~DownstreamConnection()
{
    Close( "shutdown" );
    SetShuttingDown(); // A connect in progress gives up
    if ( m_SendThread.IsRunning() )
    {
        m_SendThread.Join(); // The thread shuts the pool down as it ends
    }
    else
    {
        ShutdownAllConnections();
    }
    End();
}

// Start
//------------------------------------------------------------------------------
void DownstreamConnection::Start()
{
    m_SendThread.Start( SendThreadFuncStatic, "GatewayDownstream", this );
}

// Close
//------------------------------------------------------------------------------
void DownstreamConnection::Close( const char * reason )
{
    const ConnectionInfo * connection = nullptr;
    {
        MutexHolder mh( m_Mutex );
        if ( m_CloseReason == nullptr )
        {
            m_CloseReason = reason;
        }
        connection = m_Connection;
    }

    // The send thread stops, and a send to a worker that does not read gives up
    m_SendQueue.Quit();
    if ( connection )
    {
        Disconnect( connection );
    }
}

// GetCloseReason
//------------------------------------------------------------------------------
const char * DownstreamConnection::GetCloseReason() const
{
    MutexHolder mh( m_Mutex );
    return m_CloseReason;
}

// HasEnded
//------------------------------------------------------------------------------
bool DownstreamConnection::HasEnded() const
{
    return m_Ended.Load();
}

// IsClosing
//------------------------------------------------------------------------------
bool DownstreamConnection::IsClosing() const
{
    return m_Ended.Load() || m_SendQueue.IsQuitting();
}

// GetAddress
//------------------------------------------------------------------------------
const AString & DownstreamConnection::GetAddress() const
{
    return m_Address;
}

// GetIndex
//------------------------------------------------------------------------------
uint32_t DownstreamConnection::GetIndex() const
{
    return m_Index;
}

// GetAllowance
//------------------------------------------------------------------------------
uint32_t DownstreamConnection::GetAllowance() const
{
    return m_Allowance.Load();
}

// GetPoolSlots
//------------------------------------------------------------------------------
uint32_t DownstreamConnection::GetPoolSlots() const
{
    return m_PoolSlots.Load();
}

// UpdateDemand
//------------------------------------------------------------------------------
void DownstreamConnection::UpdateDemand( uint32_t waiting, uint32_t inFlight )
{
    // The first demand follows the inventories, as a client's does
    if ( ( m_Ready.Load() == false ) || IsClosing() )
    {
        return;
    }

    const float now = m_DemandTimer.GetElapsed();
    const uint32_t demand = m_DemandRamp.Update( waiting, inFlight, now );
    if ( m_DemandRamp.ShouldSend( demand, now ) )
    {
        m_SendQueue.Push( SendQueueItem( Protocol::MsgDemand( demand ) ) );
        m_DemandRamp.OnSent( demand, now );
    }
}

// GetAffinity
//------------------------------------------------------------------------------
GatewayScheduler::Affinity DownstreamConnection::GetAffinity( uint64_t pchId, uint64_t toolId ) const
{
    if ( ( m_Ready.Load() == false ) || IsClosing() )
    {
        return GatewayScheduler::AFFINITY_UNABLE;
    }

    // A toolchain is held once it is queued: it goes out ahead of the jobs queued after it. The jobs
    // queued while a PCH is on its way wait on the worker for it.
    MutexHolder mh( m_Mutex );
    const GatewayScheduler::Affinity tool = ( ( toolId == 0 ) || m_Toolchains.Find( toolId ) ) ? GatewayScheduler::AFFINITY_HOLDS
                                                                                                : GatewayScheduler::AFFINITY_NONE;
    GatewayScheduler::Affinity pch = GatewayScheduler::AFFINITY_NONE;
    if ( ( pchId == 0 ) || m_PchState.IsOnWorker( pchId ) )
    {
        pch = GatewayScheduler::AFFINITY_HOLDS;
    }
    else if ( m_PchState.IsRequested( pchId ) )
    {
        pch = GatewayScheduler::AFFINITY_RECEIVING;
    }
    return ( tool < pch ) ? tool : pch;
}

// SendJob
//------------------------------------------------------------------------------
void DownstreamConnection::SendJob( const Protocol::MsgJob & msg, ConstMemoryStream && job, const StoredPch * pch, PchLease && pchLease )
{
    PchLease lease( Move( pchLease ) ); // Released on return unless the PCH's chunks are queued

    MutexHolder sendJobLock( m_SendJobMutex );

    const uint64_t toolId = msg.GetToolId();
    bool queueToolchain = false;
    {
        MutexHolder mh( m_Mutex );
        if ( ( toolId != 0 ) && ( m_Toolchains.Find( toolId ) == nullptr ) )
        {
            m_Toolchains.Append( toolId );
            queueToolchain = true;
        }
    }
    if ( queueToolchain )
    {
        const uint32_t numFiles = m_Owner.GetToolchainNumFiles( toolId );
        bool queued = ( numFiles > 0 ) && QueueToolchainFile( toolId, kManifestFileId );
        for ( uint32_t fileId = 0; queued && ( fileId < numFiles ); ++fileId )
        {
            queued = QueueToolchainFile( toolId, fileId );
        }
        if ( queued == false )
        {
            Close( "toolchain" ); // The job could not run there
            return;
        }
    }

    const uint64_t rawBytes = job.GetSize();
    SendQueueItem item( msg, Move( job ), SendKind::Job, rawBytes, true );
    if ( ( pch == nullptr ) || ( pch->m_Id == 0 ) )
    {
        m_SendQueue.Push( Move( item ) );
        return;
    }

    MutexHolder mh( m_Mutex );
    if ( m_PchState.OnRequested( pch->m_Id ) )
    {
        QueuePch( *pch, Move( lease ) );
    }
    if ( m_PchState.IsOnWorker( pch->m_Id ) )
    {
        m_SendQueue.Push( Move( item ) );
    }
    else
    {
        m_JobsWaitingForPch.EmplaceBack( pch->m_Id, Move( item ) ); // OnPchDelivered queues it
    }
}

// OnJobCompleted
//------------------------------------------------------------------------------
void DownstreamConnection::OnJobCompleted( uint64_t pchId )
{
    MutexHolder mh( m_Mutex );
    m_PchState.OnJobCompleted( pchId );
}

// OnDisconnected
//------------------------------------------------------------------------------
/*virtual*/ void DownstreamConnection::OnDisconnected( const ConnectionInfo * /*connection*/ )
{
    m_SendQueue.Quit();

    // Freed if the connection dropped between a message and its payload
    FREE( (void *)m_CurrentMessage );
    m_CurrentMessage = nullptr;

    // The pool frees the ConnectionInfo when this returns, so the send thread must be done with it
    while ( m_SendThreadDone.Load() == false )
    {
        Thread::Sleep( 1 );
    }

    Array<QueuedPchLease> leases;
    {
        MutexHolder mh( m_Mutex );
        m_Connection = nullptr;
        leases.Swap( m_PchLeases );
    }
    leases.Clear(); // Released with no lock held

    End();
}

// OnReceive
//------------------------------------------------------------------------------
/*virtual*/ void DownstreamConnection::OnReceive( const ConnectionInfo * /*connection*/, void * data, uint32_t size, bool & keepMemory )
{
    keepMemory = true; // Freed below, or handed to the owner

    void * payload = nullptr;
    size_t payloadSize = 0;
    if ( m_CurrentMessage == nullptr )
    {
        if ( Protocol::IsValidHeader( data, size ) == false )
        {
            FREE( data );
            Close( "protocol" );
            return;
        }
        m_CurrentMessage = static_cast<const Protocol::IMessage *>( data );
        if ( m_CurrentMessage->HasPayload() )
        {
            return;
        }
    }
    else
    {
        payload = data;
        payloadSize = size;
    }

    const Protocol::IMessage * const imsg = m_CurrentMessage;
    m_CurrentMessage = nullptr;
    const Protocol::MessageType messageType = imsg->GetType();

    if ( ( m_AckReceived == false ) && ( messageType != Protocol::MSG_CONNECTION_ACK ) )
    {
        Close( "handshake" ); // A refusal, or something before the ack
    }
    else
    {
        switch ( messageType )
        {
            case Protocol::MSG_CONNECTION_ACK:
            {
                Process( static_cast<const Protocol::MsgConnectionAck *>( imsg ), size ); // An ack has no payload
                break;
            }
            case Protocol::MSG_PCH_INVENTORY:
            {
                const uint32_t numEntries = static_cast<const Protocol::MsgPchInventory *>( imsg )->GetNumEntries();
                if ( ProcessInventory( payload, payloadSize, numEntries, true ) == false )
                {
                    Close( "protocol" );
                }
                break;
            }
            case Protocol::MSG_TOOLCHAIN_INVENTORY:
            {
                const uint32_t numEntries = static_cast<const Protocol::MsgToolchainInventory *>( imsg )->GetNumEntries();
                if ( ProcessInventory( payload, payloadSize, numEntries, false ) == false )
                {
                    Close( "protocol" );
                }
                break;
            }
            case Protocol::MSG_PCH_NEEDED:
            {
                Process( static_cast<const Protocol::MsgPchNeeded *>( imsg ) );
                break;
            }
            case Protocol::MSG_REQUEST_MANIFEST:
            {
                ProcessToolchainRequest( static_cast<const Protocol::MsgRequestManifest *>( imsg )->GetToolId(), kManifestFileId );
                break;
            }
            case Protocol::MSG_REQUEST_FILE:
            {
                const Protocol::MsgRequestFile * msg = static_cast<const Protocol::MsgRequestFile *>( imsg );
                ProcessToolchainRequest( msg->GetToolId(), msg->GetFileId() );
                break;
            }
            case Protocol::MSG_JOB_RESULT:
            case Protocol::MSG_JOB_RESULT_COMPRESSED:
            {
                if ( ( m_Ready.Load() == false ) || ( payload == nullptr ) )
                {
                    Close( "protocol" );
                    break;
                }
                m_Owner.OnDownstreamResult( *this, ( messageType == Protocol::MSG_JOB_RESULT_COMPRESSED ), payload, (uint32_t)payloadSize );
                payload = nullptr; // The owner's
                break;
            }
            case Protocol::MSG_CAPACITY:
            {
                const Protocol::MsgCapacity * msg = static_cast<const Protocol::MsgCapacity *>( imsg );
                m_Allowance.Store( msg->GetAllowance() );
                m_PoolSlots.Store( msg->GetPoolSlots() );
                if ( m_Ready.Load() ) // Before that, the owner reads them when told the connection is ready
                {
                    m_Owner.OnDownstreamCapacity( *this, msg->GetAllowance(), msg->GetPoolSlots() );
                }
                break;
            }
            default:
            {
                Close( "protocol" ); // Not something a worker sends
                break;
            }
        }
    }

    FREE( (void *)imsg );
    FREE( payload );
}

// Process( MsgConnectionAck )
//------------------------------------------------------------------------------
void DownstreamConnection::Process( const Protocol::MsgConnectionAck * msg, uint32_t size )
{
    // Only the version fields are the same in every major version
    if ( m_AckReceived ||
         ( msg->GetProtocolVersionMajor() != Protocol::kVersionMajor ) ||
         ( size < sizeof( Protocol::MsgConnectionAck ) ) )
    {
        Close( "handshake" );
        return;
    }
    m_AckReceived = true;
    m_Allowance.Store( msg->GetWorkerCapacity() ); // Both, until the worker sends a MSG_CAPACITY
    m_PoolSlots.Store( msg->GetWorkerCapacity() );
}

// ProcessInventory
//------------------------------------------------------------------------------
bool DownstreamConnection::ProcessInventory( const void * payload, size_t payloadSize, uint32_t numEntries, bool isPch )
{
    bool & received = isPch ? m_PchInventoryReceived : m_ToolchainInventoryReceived;
    if ( received || ( payloadSize < ( (size_t)numEntries * sizeof( uint64_t ) ) ) )
    {
        return false;
    }
    received = true;

    const uint64_t * ids = static_cast<const uint64_t *>( payload );
    {
        MutexHolder mh( m_Mutex );
        if ( isPch )
        {
            m_PchState.OnInventory( ids, numEntries );
        }
        else
        {
            for ( uint32_t i = 0; i < numEntries; ++i )
            {
                if ( m_Toolchains.Find( ids[ i ] ) == nullptr )
                {
                    m_Toolchains.Append( ids[ i ] );
                }
            }
        }
    }

    if ( m_PchInventoryReceived && m_ToolchainInventoryReceived )
    {
        m_Ready.Store( true );
        m_Owner.OnDownstreamReady( *this );
    }
    return true;
}

// Process( MsgPchNeeded )
//------------------------------------------------------------------------------
void DownstreamConnection::Process( const Protocol::MsgPchNeeded * msg )
{
    // Answered from the gateway's store, and never passed to the client
    const uint64_t pchId = msg->GetPchId();
    StoredPch pch;
    PchLease lease; // Released on return unless the chunks are queued
    if ( ( pchId == 0 ) || ( m_Owner.FindPch( pchId, pch, lease ) == false ) )
    {
        Close( "pch" );
        return;
    }

    bool overLimit = false;
    {
        MutexHolder mh( m_Mutex );
        switch ( m_PchState.OnNeeded( pchId ) )
        {
            case PchSendState::NEEDED_QUEUED:           QueuePch( pch, Move( lease ) ); break;
            case PchSendState::NEEDED_ALREADY_QUEUED:   break;
            case PchSendState::NEEDED_LIMIT:            overLimit = true; break;
        }
    }
    if ( overLimit )
    {
        Close( "pch" ); // It keeps losing the PCH
    }
}

// ProcessToolchainRequest
//------------------------------------------------------------------------------
void DownstreamConnection::ProcessToolchainRequest( uint64_t toolId, uint32_t fileId )
{
    if ( toolId == 0 )
    {
        Close( "protocol" );
        return;
    }

    // A toolchain the owner no longer holds (evicted once its jobs ended) is not answered: a job
    // still waiting for it on the worker ends by the owner's watchdog
    QueueToolchainFile( toolId, fileId );
}

// QueueToolchainFile
//------------------------------------------------------------------------------
bool DownstreamConnection::QueueToolchainFile( uint64_t toolId, uint32_t fileId )
{
    // The payload as the client sent it, read from the stash when it is sent
    AString path;
    uint32_t size = 0;
    if ( m_Owner.FindToolchainFile( toolId, fileId, path, size ) == false )
    {
        return false;
    }
    if ( fileId == kManifestFileId )
    {
        m_SendQueue.Push( SendQueueItem( Protocol::MsgManifest( toolId ), path, 0, size, SendKind::Toolchain, size, 0, false ) );
    }
    else
    {
        m_SendQueue.Push( SendQueueItem( Protocol::MsgFile( toolId, fileId ), path, 0, size, SendKind::Toolchain, size, 0, false ) );
    }
    return true;
}

// QueuePch
//------------------------------------------------------------------------------
void DownstreamConnection::QueuePch( const StoredPch & pch, PchLease && lease )
{
    // Caller holds m_Mutex. The lease is kept until the last chunk is handed to the socket.
    QueuedPchLease & queued = m_PchLeases.EmplaceBack();
    queued.m_PchId = pch.m_Id;
    queued.m_Lease = Move( lease );

    // Each chunk is read from the stored file as it is sent
    const uint32_t chunkSize = Protocol::kPchChunkSize;
    const uint32_t numChunks = Protocol::MsgPchFile::GetNumChunks( pch.m_CompressedSize, chunkSize );
    for ( uint32_t i = 0; i < numChunks; ++i )
    {
        const bool isLast = ( ( i + 1 ) == numChunks );
        m_SendQueue.Push( SendQueueItem( Protocol::MsgPchFile( pch.m_Id, pch.m_UncompressedSize, pch.m_CompressedSize, chunkSize, i ),
                                         pch.m_File,
                                         (uint64_t)i * chunkSize,
                                         Protocol::MsgPchFile::GetChunkLength( pch.m_CompressedSize, chunkSize, i ),
                                         SendKind::Pch,
                                         ( i == 0 ) ? pch.m_UncompressedSize : 0,
                                         isLast ? pch.m_Id : 0,
                                         false ) );
    }
}

// OnPchDelivered
//------------------------------------------------------------------------------
/*virtual*/ void DownstreamConnection::OnPchDelivered( uint64_t pchId )
{
    PchLease lease; // Released with no lock held
    MutexHolder mh( m_Mutex );
    m_PchState.OnDelivered( pchId );
    for ( size_t i = 0; i < m_PchLeases.GetSize(); ++i )
    {
        if ( m_PchLeases[ i ].m_PchId == pchId )
        {
            lease = Move( m_PchLeases[ i ].m_Lease );
            m_PchLeases.EraseIndex( i );
            break;
        }
    }

    // The last chunk is sent before anything pushed now, so these jobs reach the worker behind it
    for ( size_t i = 0; i < m_JobsWaitingForPch.GetSize(); )
    {
        if ( m_JobsWaitingForPch[ i ].m_PchId == pchId )
        {
            m_SendQueue.Push( Move( m_JobsWaitingForPch[ i ].m_Item ) );
            m_JobsWaitingForPch.EraseIndex( i );
        }
        else
        {
            ++i;
        }
    }
}

// End
//------------------------------------------------------------------------------
void DownstreamConnection::End()
{
    {
        MutexHolder mh( m_Mutex );
        if ( m_Ended.Load() )
        {
            return;
        }
        m_Ended.Store( true );
    }
    m_Owner.OnDownstreamLost( *this );
}

// SendThreadFuncStatic
//------------------------------------------------------------------------------
/*static*/ uint32_t DownstreamConnection::SendThreadFuncStatic( void * param )
{
    static_cast<DownstreamConnection *>( param )->SendThreadFunc();
    return 0;
}

// SendThreadFunc
//------------------------------------------------------------------------------
void DownstreamConnection::SendThreadFunc()
{
    PROFILE_SET_THREAD_NAME( AStackString().Format( "G2W_%s", m_Address.Get() ).Get() );

    const ConnectionInfo * ci = m_SendQueue.IsQuitting() ? nullptr : Connect( m_Host, m_Port, 2000, this );
    if ( ci )
    {
        {
            MutexHolder mh( m_Mutex );
            m_Connection = ci;
        }
        const Protocol::MsgConnection msg;
        if ( ( m_SendQueue.IsQuitting() == false ) && msg.Send( ci ) )
        {
            m_SendQueue.Run( *this, ci );
        }
    }
    m_SendThreadDone.Store( true );

    // A connection ends in OnDisconnected, on the receive thread, which tells the owner
    ShutdownAllConnections();
    if ( ci == nullptr )
    {
        End();
    }
}

//------------------------------------------------------------------------------
