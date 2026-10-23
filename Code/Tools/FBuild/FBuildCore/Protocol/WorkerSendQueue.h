// WorkerSendQueue.h - Messages waiting to be sent to a worker, and the loop that sends them
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildCore/Helpers/DistributionStats.h"
#include "Tools/FBuild/FBuildCore/Helpers/JobStream.h"
#include "Tools/FBuild/FBuildCore/Protocol/PrioritySendQueue.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/Protocol/SendKind.h"

// Core
#include "Core/Containers/Move.h"
#include "Core/Env/Assert.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Mutex.h"
#include "Core/Process/Semaphore.h"
#include "Core/Strings/AString.h"
#include "Core/Time/Timer.h"

// Forward Declarations
//------------------------------------------------------------------------------
class ConnectionInfo;
class TCPConnectionPool;

// SendQueueItem
//------------------------------------------------------------------------------
class SendQueueItem
{
public:
    SendQueueItem() = default;
    explicit SendQueueItem( const Protocol::IMessage & msg )
        : m_Message( msg.GetSize() ) // Prealloc exact size needed
        , m_HasPayload( false )
    {
        VERIFY( m_Message.WriteBuffer( &msg, msg.GetSize() ) == msg.GetSize() );
    }
    explicit SendQueueItem( const Protocol::IMessage & msg,
                            ConstMemoryStream && payload,
                            SendKind kind,
                            uint64_t rawBytes,
                            bool streamCompress,
                            uint64_t pchId = 0 )
        : m_Message( msg.GetSize() ) // Prealloc exact size needed
        , m_HasPayload( true )
        , m_Payload( Move( payload ) )
        , m_Kind( kind )
        , m_RawBytes( rawBytes )
        , m_StreamCompress( streamCompress )
        , m_PchId( pchId )
    {
        VERIFY( m_Message.WriteBuffer( &msg, msg.GetSize() ) == msg.GetSize() );
    }

    // A payload read from a file when it is sent, so queued data is not held in memory
    explicit SendQueueItem( const Protocol::IMessage & msg,
                            const AString & payloadFile,
                            uint64_t payloadOffset,
                            uint32_t payloadLength,
                            SendKind kind,
                            uint64_t rawBytes,
                            uint64_t pchId,
                            bool pchTransformed )
        : m_Message( msg.GetSize() ) // Prealloc exact size needed
        , m_HasPayload( true )
        , m_PayloadFile( payloadFile )
        , m_PayloadOffset( payloadOffset )
        , m_PayloadLength( payloadLength )
        , m_Kind( kind )
        , m_RawBytes( rawBytes )
        , m_PchId( pchId )
        , m_PchTransformed( pchTransformed )
    {
        VERIFY( m_Message.WriteBuffer( &msg, msg.GetSize() ) == msg.GetSize() );
    }

    // non copyable
    explicit SendQueueItem( const SendQueueItem & other ) = delete;
    void operator=( const SendQueueItem & other ) = delete;

    // movable
    explicit SendQueueItem( SendQueueItem && other ) = default;
    SendQueueItem & operator=( SendQueueItem && other ) = default;

    bool IsBulk() const { return IsBulkSendKind( m_Kind ); }

    MemoryStream m_Message;
    bool m_HasPayload = false;
    ConstMemoryStream m_Payload;
    AString m_PayloadFile; // If set, the payload is this slice of the file
    uint64_t m_PayloadOffset = 0;
    uint32_t m_PayloadLength = 0;
    SendKind m_Kind = SendKind::Control;
    uint64_t m_RawBytes = 0; // Size before compression, for stats
    bool m_StreamCompress = false; // Payload is compressed by the send thread, against the jobs sent before it
    uint64_t m_PchId = 0; // The PCH the payload completes, if it is the last chunk of one
    bool m_PchTransformed = false; // Of that PCH: it is the form that a transform makes (see PchTransform)
    Timer m_Enqueued; // For how long it waited to be sent
};

// WorkerSendQueue
//------------------------------------------------------------------------------
// The messages for one connection to a worker. Any thread pushes; the connection's send thread
// runs the loop that sends them. The chunks of a PCH go out behind everything else.
class WorkerSendQueue
{
public:
    class Listener
    {
    public:
        virtual void OnPchDelivered( uint64_t pchId ) = 0; // Send thread: the last chunk of a PCH is about to be sent
    protected:
        ~Listener() = default;
    };

    WorkerSendQueue( Listener & listener,
                     const AString & peerName,
                     int jobCompressionLevel = JobStream::kCompressionLevel,
                     int jobWindowLog = JobStream::kWindowLog );
    ~WorkerSendQueue();

    // Non-copyable
    explicit WorkerSendQueue( const WorkerSendQueue & other ) = delete;
    WorkerSendQueue & operator=( const WorkerSendQueue & other ) = delete;

    void Push( SendQueueItem && item ); // Any thread; never blocks on the socket
    void Quit();
    bool IsQuitting() const { return m_Quit.Load(); }
    bool HasFailed() const { return m_Failed.Load(); } // It stopped as what it was to send could not be read or compressed
    const Atomic<bool> & GetQuitFlag() const { return m_Quit; } // For Client::RunTestHook

    // Send thread: sends until told to quit, or until a send fails
    void Run( TCPConnectionPool & pool, const ConnectionInfo * connection );

    uint32_t GetNumUnsentJobs() const { return m_UnsentJobs.Load(); }
    uint64_t GetUnsentJobBytes() const { return m_UnsentJobBytes.Load(); } // Before compression

    // The thread that pushes jobs has stopped for want of room: the semaphore is signalled when the send thread next
    // sends a job
    void SetWakeSemaphore( Semaphore * semaphore ) { m_ClientWake = semaphore; } // Before the first Push
    void WakeWhenJobsSent() { m_WakeWhenJobsSent.Store( true ); }
    uint32_t GetNumTransformedPchs() const { return m_NumTransformedPchs.Load(); }
    const DistributionSendStats & GetStats( SendKind kind ) const { return m_SendStats[ (size_t)kind ]; } // Send thread, or once it has ended

private:
    bool SendItem( TCPConnectionPool & pool, const ConnectionInfo * ci, const SendQueueItem & item ); // false if the thread should stop

    Listener & m_Listener;
    const AString m_PeerName; // For messages

    Semaphore m_WakeSemaphore;
    Atomic<bool> m_Quit{ false };
    Atomic<bool> m_Failed{ false };
    Atomic<uint32_t> m_UnsentJobs{ 0 }; // Queued for the send thread, not yet sent
    Atomic<uint64_t> m_UnsentJobBytes{ 0 }; // The raw size of those
    Semaphore * m_ClientWake = nullptr; // The client thread's
    Atomic<bool> m_WakeWhenJobsSent{ false };
    Atomic<uint32_t> m_NumTransformedPchs{ 0 }; // PCHs delivered in the form that a transform makes (see PchTransform)
    JobStreamEncoder m_JobEncoder; // Send thread only. A new connection starts a new stream.

    DistributionSendStats m_SendStats[ (size_t)SendKind::NumKinds ]; // Send thread only
    const Timer m_ConnectionTimer;

    Mutex m_SendQueueMutex;
    PrioritySendQueue<SendQueueItem> m_SendQueue;
};

//------------------------------------------------------------------------------
