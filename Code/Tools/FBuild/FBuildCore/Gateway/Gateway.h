// Gateway - One worker to its clients, in front of the workers of a site
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildCore/Gateway/DownstreamConnection.h"
#include "Tools/FBuild/FBuildCore/Gateway/GatewayConfig.h"
#include "Tools/FBuild/FBuildCore/Gateway/GatewayScheduler.h"
#include "Tools/FBuild/FBuildCore/Gateway/ToolchainStash.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchLease.h"
#include "Tools/FBuild/FBuildCore/Protocol/Server.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Mutex.h"
#include "Core/Process/Semaphore.h"
#include "Core/Process/Thread.h"
#include "Core/Strings/AString.h"
#include "Core/Time/Timer.h"

// Gateway
//------------------------------------------------------------------------------
// Its Server takes clients, and hands it their jobs, toolchains and PCHs. It keeps each job until
// the job's result has come back from a downstream worker, and sends that result to the client
// that sent the job, with the client's job id restored and the thread index moved to the worker's lane.
//
// Slots: its Server shares out the pool, the sum of its ready workers' pools, between its clients by their
// demand. Toward each worker the gateway is a client like any other: it reports what waits for and runs on
// that worker as its demand, and runs there as many jobs as the worker's allowance.
//
// A worker's faults are the gateway's to retry: a job that fails, is lost with its worker or outlives
// the watchdog is tried again elsewhere, at most GatewayScheduler::kMaxAttempts times in all. Only then
// does the client get a failed result, flagged Protocol::kResultReturnedByGateway, so it requeues the
// job and keeps the gateway. A lost worker is connected again after GatewayConfig::m_ReconnectSeconds.
//
// Locking: m_Mutex guards everything below it. Under it the gateway only pushes to queues, calls
// the scheduler, reads connection state and takes the Server's PCH cache lock. Sends to clients,
// disk access (the stash's, the log's) and the Server's client list come after it is released.
//------------------------------------------------------------------------------
class Gateway : private Server::Forwarder, private DownstreamConnection::Owner
{
    friend class TestGateway;
public:
    static constexpr uint32_t kLocalWorkerIndex = 0;   // Used from Task 18
    // A client that holds more jobs at the gateway than this many per slot of the largest allowance it was
    // given does not keep to its allowance, and is disconnected: each job is held in memory with a toolchain pin
    static constexpr uint32_t kMaxHeldJobsPerSlot = 4;
    // At most this long between the gateway's demand reports to its workers (DemandRamp paces what is sent)
    static constexpr uint32_t kDemandTickMs = 100;
    explicit Gateway( const GatewayConfig & config );  // Phase A: a pure forwarder
    virtual ~Gateway();

    explicit Gateway( const Gateway & other ) = delete;
    Gateway & operator=( const Gateway & other ) = delete;

    bool Listen( uint16_t port );
    Server & GetServer();

    struct Stats
    {
        uint32_t m_Clients = 0;
        uint32_t m_Workers = 0;        // Configured
        uint32_t m_ReadyWorkers = 0;
        uint32_t m_PoolSlots = 0;
        uint32_t m_Waiting = 0;
        uint32_t m_Parked = 0;
        uint32_t m_Running = 0;
        uint64_t m_HeldJobBytes = 0;
        uint32_t m_Forwarded = 0;
        uint32_t m_Retries = 0;
        uint32_t m_GiveUps = 0;
        uint32_t m_Timeouts = 0;
        uint64_t m_PchBytesForwarded = 0;
        uint32_t m_ToolchainRequests = 0;
    };
    void GetStats( Stats & outStats ) const;

private:
    // Server::Forwarder
    virtual uint32_t GetPoolSlots() const override;
    virtual void GetToolchainInventory( uint32_t clientId, Array<uint64_t> & outToolIds ) override;
    virtual void OnClientJob( uint32_t clientId, const Protocol::MsgJob & msg, void * jobData, size_t jobDataSize ) override;
    virtual void OnClientManifest( uint32_t clientId, uint64_t toolId, const void * payload, size_t payloadSize ) override;
    virtual void OnClientFile( uint32_t clientId, uint64_t toolId, uint32_t fileId, const void * payload, size_t payloadSize ) override;
    virtual void OnPchStored( uint64_t pchId, uint32_t uncompressedSize, uint32_t compressedSize ) override;
    virtual void OnClientGone( uint32_t clientId ) override;
    virtual void OnLocalJobEnded( uint32_t jobId, bool hasResult, bool compressed, MemoryStream & payload ) override;

    // DownstreamConnection::Owner
    virtual void OnDownstreamReady( DownstreamConnection & connection ) override;
    virtual void OnDownstreamResult( DownstreamConnection & connection, bool compressed, void * payload, uint32_t payloadSize ) override;
    virtual void OnDownstreamLost( DownstreamConnection & connection ) override;
    virtual void OnDownstreamCapacity( DownstreamConnection & connection, uint32_t allowance, uint32_t poolSlots ) override;
    virtual bool FindPch( uint64_t pchId, StoredPch & outPch, PchLease & outLease ) override;
    virtual bool FindToolchainFile( uint64_t toolId, uint32_t fileId, AString & outPath, uint32_t & outSize ) override;
    virtual uint32_t GetToolchainNumFiles( uint64_t toolId ) override;

    // A client's job, held until its result has been passed on
    struct GatewayJob
    {
        GatewayJob() = default;
        explicit GatewayJob( const GatewayJob & other ) = delete;
        GatewayJob & operator=( const GatewayJob & other ) = delete;

        uint32_t    m_Id = 0;                   // The scheduler's
        uint32_t    m_ClientId = 0;
        uint32_t    m_ClientJobId = 0;
        uint64_t    m_ToolId = 0;
        uint64_t    m_PchId = 0;
        int16_t     m_ResultCompressionLevel = 0;
        void *      m_Data = nullptr;           // As decoded, with the client's job id
        size_t      m_DataSize = 0;
        PchLease    m_PchLease;                 // From its arrival, so the PCH is not evicted while it is held
        uint32_t    m_NumAttempts = 0;          // Dispatched so far
        bool        m_Parked = false;           // Waiting for its toolchain or PCH, and not yet in the scheduler
    };

    // One dispatch of a job. The attempt id is the job id the worker sees.
    struct Attempt
    {
        uint32_t    m_AttemptId = 0;
        uint32_t    m_JobId = 0;
        uint32_t    m_WorkerIndex = 0;
        Timer       m_Started;
        bool        m_Expired = false;          // Failed by the watchdog: kept only so its late result frees the slot
    };

    struct Worker
    {
        DownstreamConnection *  m_Connection = nullptr;
        bool                    m_Ready = false;    // In the scheduler and the pool: its connection is ready and its pool has slots
        bool                    m_Lost = false;     // Its connection has ended, and is replaced once m_LostTimer passes m_ReconnectSeconds
        Timer                   m_LostTimer;
    };

    struct Client
    {
        uint32_t        m_Id = 0;
        uint32_t        m_NumJobs = 0;          // Held for it
        Array<uint64_t> m_Pins;     // The toolchains it was advertised
    };

    // A PCH in the Server's store, as far as the gateway knows. Those found at start are measured once.
    struct KnownPch
    {
        uint64_t    m_Id = 0;
        uint32_t    m_UncompressedSize = 0;
        uint32_t    m_CompressedSize = 0;
        bool        m_Measured = false;
    };

    // A client that is sending a toolchain the stash does not hold whole yet
    struct Upload
    {
        uint64_t    m_ToolId = 0;
        uint32_t    m_ClientId = 0;
        bool        m_Requested = false;        // The gateway asked it for the manifest
        bool        m_FilesRequested = false;
    };

    // What is done once m_Mutex is released, in this order
    struct Actions
    {
        struct PchAsk { uint32_t m_ClientId; uint64_t m_PchId; };
        struct ToolchainRequest { uint32_t m_ClientId; uint64_t m_ToolId; uint32_t m_FileId; }; // kManifestFileId: the manifest
        struct GivenUp { uint32_t m_ClientId; bool m_Compressed; MemoryStream * m_Payload; }; // Sent flagged kResultReturnedByGateway
        struct Demand { DownstreamConnection * m_Connection; uint32_t m_Waiting; uint32_t m_InFlight; };

        Actions() = default;
        ~Actions();
        explicit Actions( const Actions & other ) = delete;
        Actions & operator=( const Actions & other ) = delete;

        Array<uint64_t>         m_DropIncomplete;
        Array<uint64_t>         m_ReleaseUses;
        Array<GatewayJob *>     m_FreeJobs;
        Array<uint64_t>         m_DropPchs;     // From the Server's store
        Array<uint32_t>         m_Disconnects;
        Array<PchAsk>           m_PchAsks;
        Array<ToolchainRequest> m_ToolchainRequests;
        Array<GivenUp>          m_GivenUp;
        Array<AString>          m_LogLines;
        Array<Demand>           m_Demands;      // The housekeeping thread's only: it is the one that destroys connections
    };
    void Run( Actions & actions );

    class WorkerAffinity;

    // The caller holds m_Mutex
    void Dispatch( Actions & actions );
    void Schedule( GatewayJob & job );
    void Park( GatewayJob & job, Actions & actions );
    void ReleaseParked();       // Schedules those that have what they wait for
    bool IsReady( const GatewayJob & job );
    bool IsPchStored( uint64_t pchId );
    bool LookUpPch( uint64_t pchId, StoredPch & outPch, PchLease & outLease );
    void RequestToolchain( uint32_t clientId, uint64_t toolId, bool askForManifest, Actions & actions ); // Else its missing files
    void RequestMissingFiles( Upload & upload, Actions & actions );
    void OnToolchainPayload( uint32_t clientId, uint64_t toolId, Actions & actions );
    bool IsUploaded( uint64_t toolId ) const; // Some live client is sending it
    void RemoveJob( GatewayJob * job, Actions & actions );
    // An attempt ended without a usable result. Retries the job, or gives it up: true if the client is then
    // to get the job's failed result (it is still connected), and the job is removed either way.
    bool FailAttempt( GatewayJob & job, uint32_t workerIndex, const char * reason, Actions & actions );
    void GiveUpWithoutResult( GatewayJob & job, Actions & actions ); // After FailAttempt: a lost-job result goes up
    void ExpireHungAttempts( Actions & actions );
    // Waiting jobs that no worker can take for m_NoWorkerGiveUpSeconds are given up. Waiting behind busy workers is not.
    void GiveUpJobsNoWorkerCanTake( Actions & actions );
    bool CanAnyWorkerTakeAJob() const; // Some ready worker has a slot in its pool that no expired attempt holds
    // A worker is in the scheduler, with its allowance as its slots, while its connection is ready and its pool
    // has slots. One whose pool falls to 0 leaves it, and finishes the jobs it runs.
    void UpdateWorker( Worker & worker, DownstreamConnection & connection, Actions & actions );
    // Out of the scheduler. The jobs it runs are tried elsewhere only if it is lost.
    void RemoveWorker( Worker & worker, DownstreamConnection & connection, const char * reason, bool lost, Actions & actions );
    void CollectDemands( Actions & actions ); // What waits for and runs on each worker
    void UpdatePool();
    Client * FindClient( uint32_t clientId );
    Client & FindOrAddClient( uint32_t clientId );
    GatewayJob * FindJob( uint32_t jobId ) const;
    Worker * FindWorker( const DownstreamConnection & connection );
    KnownPch * FindKnownPch( uint64_t pchId );
    void GetStatsLocked( Stats & outStats ) const;
    void FormatStats( AString & outLine ) const;

    static uint32_t HousekeepingThreadFuncStatic( void * param );
    void HousekeepingThreadFunc();
    void ReconnectLostWorkers();
    static uint32_t MeasureThreadFuncStatic( void * param );
    void MeasureStoredPchs(); // Those found at start

    const GatewayConfig     m_Config;
    ToolchainStash          m_Stash;
    Server *                m_Server = nullptr; // Destroyed before the stash, and once nothing holds a lease
    Atomic<uint32_t>        m_PoolSlots{ 0 };

    mutable Mutex           m_Mutex;
    bool                    m_ShuttingDown = false;
    GatewayScheduler        m_Scheduler;
    Array<Worker>           m_Workers;          // Worker index - 1
    Array<Client>           m_Clients;
    Array<GatewayJob *>     m_Jobs;             // In the order they arrived
    Array<Attempt>          m_Attempts;
    Array<KnownPch>         m_Pchs;
    Array<Upload>           m_Uploads;
    uint32_t                m_NextJobId = 1;
    uint32_t                m_NextAttemptId = 1;
    uint64_t                m_HeldJobBytes = 0;
    uint32_t                m_NumParked = 0;
    uint32_t                m_Forwarded = 0;
    uint32_t                m_Retries = 0;
    uint32_t                m_GiveUps = 0;
    uint32_t                m_Timeouts = 0;
    uint64_t                m_PchBytesForwarded = 0;
    uint32_t                m_ToolchainRequests = 0;         // MSG_REQUEST_MANIFEST and MSG_REQUEST_FILE sent
    bool                    m_NoWorkerForWaitingJobs = false; // Since m_NoWorkerTimer started
    Timer                   m_NoWorkerTimer;

    Thread                  m_HousekeepingThread;
    Semaphore               m_HousekeepingSemaphore;
    Thread                  m_MeasureThread;    // Started if the store held PCHs at start, as measuring them can take minutes
    Atomic<bool>            m_Quit{ false };

    // Runs before each PCH found at start is measured, so a test can hold the measurement until quit is set
    typedef void ( *MeasureTestHook )( const Atomic<bool> & quit );
    inline static MeasureTestHook s_MeasureTestHook = nullptr;
};

//------------------------------------------------------------------------------
