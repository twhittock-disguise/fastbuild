// DownstreamConnection - A gateway's connection to one of its workers
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildCore/Gateway/GatewayConfig.h"
#include "Tools/FBuild/FBuildCore/Gateway/GatewayScheduler.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchLease.h"
#include "Tools/FBuild/FBuildCore/Protocol/WorkerSendQueue.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/DemandRamp.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/PchDispatch.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Network/TCPConnectionPool.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Mutex.h"
#include "Core/Process/Thread.h"
#include "Core/Strings/AString.h"
#include "Core/Time/Timer.h"

// Forward Declarations
//------------------------------------------------------------------------------
class ConstMemoryStream;

// StoredPch
//------------------------------------------------------------------------------
// A PCH the gateway holds, compressed as its client sent it
struct StoredPch
{
    uint64_t m_Id = 0;
    AString  m_File;
    uint32_t m_UncompressedSize = 0;
    uint32_t m_CompressedSize = 0;
};

// DownstreamConnection
//------------------------------------------------------------------------------
// The gateway is this worker's client. Whatever the worker does wrong (it cannot be reached, is
// another version, asks for a PCH too often or for one the gateway does not hold) ends this
// connection and nothing else: the owner is told the connection is lost, and decides what
// happens to the jobs it had sent.
//
// The send thread connects, sends the handshake and then the queue. The receive thread handles
// what the worker sends. A worker's MSG_PCH_NEEDED is answered from the owner's store. As any client
// does, the gateway reports its demand on the worker, which shares its slots out by demand.
//------------------------------------------------------------------------------
class DownstreamConnection : public TCPConnectionPool, private WorkerSendQueue::Listener
{
public:
    // Called on the connection's threads, with none of its locks held. FindToolchainFile and GetToolchainNumFiles
    // are also called by SendJob, on its caller's thread with m_SendJobMutex held: they must not wait for a lock
    // that a caller of SendJob may hold.
    class Owner
    {
    public:
        virtual void OnDownstreamReady( DownstreamConnection & connection ) = 0;
        virtual void OnDownstreamResult( DownstreamConnection & connection, bool compressed, void * payload, uint32_t payloadSize ) = 0; // Takes payload
        virtual void OnDownstreamLost( DownstreamConnection & connection ) = 0;    // Once, also from the destructor
        virtual bool FindPch( uint64_t pchId, StoredPch & outPch, PchLease & outLease ) = 0;
        virtual bool FindToolchainFile( uint64_t toolId, uint32_t fileId, AString & outPath, uint32_t & outSize ) = 0;
        virtual uint32_t GetToolchainNumFiles( uint64_t toolId ) = 0;    // The Gateway answers from ToolchainStash::GetNumFiles
        // A MSG_CAPACITY once the connection is ready. One that comes before is in what the connection reports when ready.
        virtual void OnDownstreamCapacity( DownstreamConnection & /*connection*/, uint32_t /*allowance*/, uint32_t /*poolSlots*/ ) {}
    protected:
        ~Owner() = default;
    };
    static constexpr uint32_t kManifestFileId = 0xFFFFFFFF;

    DownstreamConnection( Owner & owner, const GatewayConfig::Worker & worker, uint32_t index ); // index 1..kMaxWorkerIndex
    virtual ~DownstreamConnection() override;

    // Non-copyable
    explicit DownstreamConnection( const DownstreamConnection & other ) = delete;
    DownstreamConnection & operator=( const DownstreamConnection & other ) = delete;

    void Start();
    void Close( const char * reason );
    const char * GetCloseReason() const;   // The first given, or null
    bool HasEnded() const;
    bool IsClosing() const;                // Closed or ended, so it takes no more jobs: the owner is told it is lost, if it has not been yet
    const AString & GetAddress() const;
    uint32_t GetIndex() const;
    uint32_t GetAllowance() const;         // The slots the worker gives the gateway: the ack's, then the last MSG_CAPACITY's
    uint32_t GetPoolSlots() const;         // The worker's whole pool: the ack's capacity, then the last MSG_CAPACITY's
    GatewayScheduler::Affinity GetAffinity( uint64_t pchId, uint64_t toolId ) const;

    // Reports the gateway's demand on the worker through a DemandRamp, once the connection is ready.
    // The owner's housekeeping thread only.
    void UpdateDemand( uint32_t waiting, uint32_t inFlight );

    // Queues the job's toolchain unless the worker has it, then the PCH unless the worker has it or it is
    // on its way, then the job. A job whose PCH is not on the worker yet waits here until the PCH's last
    // chunk has been handed to the socket, because a worker asks for a PCH that a job needs and that it
    // neither holds nor is receiving. pchLease is kept while the PCH's chunks are queued. The toolchain's
    // files are asked of the owner (GetToolchainNumFiles, FindToolchainFile) on the caller's thread.
    void SendJob( const Protocol::MsgJob & msg, ConstMemoryStream && job, const StoredPch * pch, PchLease && pchLease );

    // A job that used the PCH completed on the worker: the PCH was there, so the times it was sent again
    // before do not count against it any more
    void OnJobCompleted( uint64_t pchId );

private:
    virtual void OnDisconnected( const ConnectionInfo * connection ) override;
    virtual void OnReceive( const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory ) override;
    virtual void OnPchDelivered( uint64_t pchId ) override;

    void Process( const Protocol::MsgConnectionAck * msg, uint32_t size );
    bool ProcessInventory( const void * payload, size_t payloadSize, uint32_t numEntries, bool isPch ); // false: not valid here
    void Process( const Protocol::MsgPchNeeded * msg );
    void ProcessToolchainRequest( uint64_t toolId, uint32_t fileId );

    bool QueueToolchainFile( uint64_t toolId, uint32_t fileId ); // false if the owner does not hold it
    void QueuePch( const StoredPch & pch, PchLease && lease ); // Caller holds m_Mutex
    void End(); // Tells the owner, once

    static uint32_t SendThreadFuncStatic( void * param );
    void SendThreadFunc();

    Owner & m_Owner;
    const AString m_Host;
    const uint16_t m_Port;
    const uint32_t m_Index;
    AString m_Address; // "host", or "host:port" when the port is not the default

    Thread m_SendThread;
    Atomic<bool> m_SendThreadDone{ false };     // It no longer uses the connection
    Atomic<bool> m_Ended{ false };
    Atomic<bool> m_Ready{ false };
    Atomic<uint32_t> m_Allowance{ 0 };
    Atomic<uint32_t> m_PoolSlots{ 0 };
    const Protocol::IMessage * m_CurrentMessage = nullptr; // Receive thread: a header whose payload is next
    DemandRamp m_DemandRamp;    // UpdateDemand's thread only. Its time is m_DemandTimer's.
    const Timer m_DemandTimer;

    // Receive thread: the handshake as far as it has come
    bool m_AckReceived = false;
    bool m_PchInventoryReceived = false;
    bool m_ToolchainInventoryReceived = false;

    Mutex m_SendJobMutex; // One SendJob at a time, so a job never goes ahead of its toolchain

    struct JobWaitingForPch
    {
        JobWaitingForPch( uint64_t pchId, SendQueueItem && item ) : m_PchId( pchId ), m_Item( Move( item ) ) {}
        JobWaitingForPch( JobWaitingForPch && other ) = default;
        JobWaitingForPch & operator=( JobWaitingForPch && other ) = default;
        explicit JobWaitingForPch( const JobWaitingForPch & other ) = delete;
        JobWaitingForPch & operator=( const JobWaitingForPch & other ) = delete;

        uint64_t m_PchId = 0;
        SendQueueItem m_Item;
    };

    struct QueuedPchLease
    {
        QueuedPchLease() = default;
        QueuedPchLease( QueuedPchLease && other ) = default;
        QueuedPchLease & operator=( QueuedPchLease && other ) = default;
        explicit QueuedPchLease( const QueuedPchLease & other ) = delete;
        QueuedPchLease & operator=( const QueuedPchLease & other ) = delete;

        uint64_t m_PchId = 0;
        PchLease m_Lease;
    };

    mutable Mutex m_Mutex;
    const ConnectionInfo * m_Connection = nullptr;
    const char * m_CloseReason = nullptr;
    PchSendState m_PchState;
    Array<uint64_t> m_Toolchains;           // The worker has them, or they are queued
    Array<QueuedPchLease> m_PchLeases;      // Of PCHs whose chunks are queued
    Array<JobWaitingForPch> m_JobsWaitingForPch; // Queued once their PCH is on the worker

    WorkerSendQueue m_SendQueue;
};

//------------------------------------------------------------------------------
