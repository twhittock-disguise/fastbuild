// WorkerSendQueue.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "WorkerSendQueue.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FLog.h"

// Core
#include "Core/Containers/UniquePtr.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Math/Conversions.h"
#include "Core/Mem/Mem.h"
#include "Core/Network/TCPConnectionPool.h"
#include "Core/Profile/Profile.h"

// CONSTRUCTOR
//------------------------------------------------------------------------------
WorkerSendQueue::WorkerSendQueue( Listener & listener, const AString & peerName, int jobCompressionLevel, int jobWindowLog )
    : m_Listener( listener )
    , m_PeerName( peerName )
    , m_JobEncoder( jobCompressionLevel, jobWindowLog )
{
}

// DESTRUCTOR
//------------------------------------------------------------------------------
WorkerSendQueue::~WorkerSendQueue() = default;

// Push
//------------------------------------------------------------------------------
void WorkerSendQueue::Push( SendQueueItem && item )
{
    if ( item.m_Kind == SendKind::Job )
    {
        m_UnsentJobs.Increment();
        m_UnsentJobBytes.Add( item.m_RawBytes );
    }
    {
        MutexHolder lock( m_SendQueueMutex );
        m_SendQueue.Push( Move( item ) );
    }
    m_WakeSemaphore.Signal();
}

// Quit
//------------------------------------------------------------------------------
void WorkerSendQueue::Quit()
{
    m_Quit.Store( true );
    m_WakeSemaphore.Signal();
}

// Run
//------------------------------------------------------------------------------
void WorkerSendQueue::Run( TCPConnectionPool & pool, const ConnectionInfo * ci )
{
    // Send messages
    while ( true )
    {
        // Take the queued messages. Drain fully before blocking on semaphore.
        Array<SendQueueItem> sendQueue;
        {
            MutexHolder lock( m_SendQueueMutex );
            m_SendQueue.Take( sendQueue );
        }

        // If nothing to send, block until signaled
        if ( sendQueue.IsEmpty() )
        {
            m_WakeSemaphore.Wait();

            // When client has been signaled to exit, we should exit as well
            if ( m_Quit.Load() == true )
            {
                break;
            }
            continue; // Re-check queue after waking
        }

        for ( const SendQueueItem & item : sendQueue )
        {
            if ( SendItem( pool, ci, item ) == false )
            {
                return;
            }
        }
    }
}

// SendItem
//------------------------------------------------------------------------------
bool WorkerSendQueue::SendItem( TCPConnectionPool & pool, const ConnectionInfo * ci, const SendQueueItem & item )
{
    PROFILE_SECTION( "SendMsg" );

    const double queueDelay = (double)item.m_Enqueued.GetElapsed();

    // Compress here, in the order jobs go out, as each is compressed against the ones before it
    const void * payload = item.m_Payload.GetData();
    size_t payloadSize = item.m_Payload.GetSize();
    UniquePtr<char, FreeDeletor> fileSlice;
    if ( item.m_PayloadFile.IsEmpty() == false )
    {
        // Only this slice of a large file is in memory
        FileStream file;
        fileSlice.Replace( static_cast<char *>( ALLOC( item.m_PayloadLength ) ) );
        if ( ( file.Open( item.m_PayloadFile.Get(), FileStream::READ_ONLY ) == false ) ||
             ( file.Seek( item.m_PayloadOffset ) == false ) ||
             ( file.Read( fileSlice.Get(), item.m_PayloadLength ) != item.m_PayloadLength ) )
        {
            FLOG_WARN( "Failed to read '%s' to send it to %s. Closing the connection.\n",
                       item.m_PayloadFile.Get(), m_PeerName.Get() );
            m_Failed.Store( true );
            m_Quit.Store( true );
            return false;
        }
        payload = fileSlice.Get();
        payloadSize = item.m_PayloadLength;
    }
    double encodeTime = 0.0;
    if ( item.m_StreamCompress )
    {
        PROFILE_SECTION( "CompressJob" );
        const Timer encodeTimer;
        if ( m_JobEncoder.Encode( payload, payloadSize ) == false )
        {
            FLOG_WARN( "Failed to compress a job for %s. Closing the connection.\n", m_PeerName.Get() );
            m_Failed.Store( true );
            m_Quit.Store( true );
            return false;
        }
        payload = m_JobEncoder.GetResult();
        payloadSize = m_JobEncoder.GetResultSize();
        encodeTime = encodeTimer.GetElapsed();
    }

    // The last chunk of a PCH counts as delivered before it is sent: a worker that has lost the PCH asks
    // again as soon as it has it, which can be before this thread returns from the send
    if ( item.m_PchId != 0 )
    {
        m_Listener.OnPchDelivered( item.m_PchId );
    }

    // Send message
    const Timer sendTimer;
    const bool sendOk = item.m_HasPayload ? pool.Send( ci,
                                                       item.m_Message.GetData(),
                                                       item.m_Message.GetSize(),
                                                       payload,
                                                       payloadSize )
                                          : pool.Send( ci,
                                                       item.m_Message.GetData(),
                                                       item.m_Message.GetSize() );
    if ( item.m_Kind == SendKind::Job )
    {
        m_UnsentJobs.Decrement();
        m_UnsentJobBytes.Sub( item.m_RawBytes );
        if ( m_ClientWake && m_WakeWhenJobsSent.Load() )
        {
            m_WakeWhenJobsSent.Store( false );
            m_ClientWake->Signal();
        }
    }

    // If the send fails, we should be disconnected
    ASSERT( sendOk || ( m_Quit.Load() == true ) );

    if ( sendOk )
    {
        if ( item.m_PchTransformed && ( item.m_PchId != 0 ) )
        {
            m_NumTransformedPchs.Increment(); // Delivered
        }
        DistributionSendStats & stats = m_SendStats[ (size_t)item.m_Kind ];
        stats.m_Messages++;
        stats.m_RawBytes += item.m_RawBytes;
        stats.m_SentBytes += ( item.m_Message.GetSize() + ( item.m_HasPayload ? payloadSize : 0 ) );
        stats.m_SendTime += sendTimer.GetElapsed();
        stats.m_EncodeTime += encodeTime;
        stats.m_LastSentAt = m_ConnectionTimer.GetElapsed();
        if ( item.m_Kind != SendKind::Pch ) // Bulk data is meant to wait
        {
            stats.m_MaxQueueDelay = Math::Max( stats.m_MaxQueueDelay, queueDelay );
        }
    }

    // If signaled to exit for any reason, don't process additional items
    // (exit signal can come from a send failure above, or a receive failure
    // on the TCPConnectionPool receive thread)
    return ( m_Quit.Load() == false );
}

//------------------------------------------------------------------------------
