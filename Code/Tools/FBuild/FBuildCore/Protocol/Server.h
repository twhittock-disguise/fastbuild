// Server.h - Handles Server Side connections
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildCore/Helpers/JobStream.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchLease.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchReceiver.h"
#include "Tools/FBuild/FBuildCore/Helpers/WorkerLog.h"

#include "Core/Network/TCPConnectionPool.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Semaphore.h"
#include "Core/Time/Timer.h"

// Forward Declarations
//------------------------------------------------------------------------------
class Job;
class JobQueueRemote;
class MemoryStream;
namespace Protocol
{
    class IMessage;
    class MsgConnection;
    class MsgJob;
    class MsgManifest;
    class MsgPchFile;
    class MsgPchInventory;
    class MsgFile;
}
class ToolManifest;

// Protocol
//------------------------------------------------------------------------------
class Server : public TCPConnectionPool, private PchLease::Owner
{
    friend class TestDistributed;
    friend class TestGateway;
public:
    // What a gateway does with what its clients send. Called with no Server lock held.
    class Forwarder
    {
    public:
        virtual uint32_t GetPoolSlots() const = 0;  // Phase A: every client's ack. 0: the handshake is closed without an ack.
        virtual void GetToolchainInventory( uint32_t clientId, Array<uint64_t> & outToolIds ) = 0; // Pins what it returns for that client
        virtual void OnClientJob( uint32_t clientId, const Protocol::MsgJob & msg, void * jobData, size_t jobDataSize ) = 0; // Takes jobData (FREE)
        virtual void OnClientManifest( uint32_t clientId, uint64_t toolId, const void * payload, size_t payloadSize ) = 0;
        virtual void OnClientFile( uint32_t clientId, uint64_t toolId, uint32_t fileId, const void * payload, size_t payloadSize ) = 0;
        virtual void OnPchStored( uint64_t pchId, uint32_t uncompressedSize, uint32_t compressedSize ) = 0;
        // Every client, handshake or not, once it has left the client list: nothing is queued for it from here on,
        // and a PCH it was sending no longer counts as being received, so another client can be asked for it
        virtual void OnClientGone( uint32_t clientId ) = 0;
    protected:
        ~Forwarder() = default;
    };

    Server( uint32_t numThreadsInJobQueue = 0, uint32_t prefetchBuffer = 0 );
    // Gateway mode, pure forwarder: no JobQueueRemote, no toolchain extraction, no unpacking. cacheDir: the
    // root of the PCH store and the log (an empty string: FBuild::GetWorkerCacheDir). The forwarder outlives the Server.
    Server( Forwarder & forwarder, const AString & cacheDir );
    virtual ~Server() override;

    explicit Server( const Server & other ) = delete;
    Server & operator=( const Server & other ) = delete;

    static void GetHostForJob( const Job * job, AString & hostName );

    bool IsSynchingTool( AString & statusStr ) const;

    const AString & GetLogPath() const { return m_Log.GetPath(); }

    // A result for the client's outbox (see ClientState::m_Id). False if the client has gone.
    bool QueueResult( uint32_t clientId, bool compressed, MemoryStream && payload );
    // A message without a payload, sent by the client's outbox after what is queued before it, so it never
    // waits for a client that reads slowly. False if the client has gone.
    bool SendToClient( uint32_t clientId, const Protocol::IMessage & msg );

    bool FindCachedPch( uint64_t pchId, AString & outPath ); // The compressed PCH. Refreshes LRU recency.
    PchLease AcquirePchLease( uint64_t pchId );
    void GetCachedPchIds( Array<uint64_t> & outPchIds ) const; // Those in the store now, as the PCH inventory sends them

    // Asks the client for a PCH that is not cached, once until it arrives (see RequestPchIfMissing).
    // Sent by the client's outbox, so it never waits for a client that reads slowly.
    void AskClientForPch( uint32_t clientId, uint64_t pchId );
    void DisconnectClient( uint32_t clientId );
    WorkerLog & GetLog() { return m_Log; }

private:
    // TCPConnection interface
    virtual void OnConnected( const ConnectionInfo * connection ) override;
    virtual void OnDisconnected( const ConnectionInfo * connection ) override;
    virtual void OnReceive( const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory ) override;

    // helpers to handle messages
    void Process( const ConnectionInfo * connection, const Protocol::MsgConnection * msg, uint32_t size );
    void Process( const ConnectionInfo * connection, const Protocol::MsgJob * msg, const void * payload, size_t payloadSize );
    void Process( const ConnectionInfo * connection, const Protocol::MsgManifest * msg, const void * payload, size_t payloadSize );
    void Process( const ConnectionInfo * connection, const Protocol::MsgFile * msg, const void * payload, size_t payloadSize );
    void Process( const ConnectionInfo * connection, const Protocol::MsgPchFile * msg, const void * payload, size_t payloadSize );

    static uint32_t ThreadFuncStatic( void * param );
    void ThreadFunc();

    // Warns on the console, and in the log as "error <text>"
    void LogWarning( MSVC_SAL_PRINTF const char * fmtString, ... ) FORMAT_STRING( 2, 3 );

    void FinalizeCompletedJobs();
    void TouchToolchains();
    void ReleaseReadyWaitingJobs();

    void RecalculateCapacity();        // Takes m_ClientListMutex
    void RecalculateCapacityLocked();  // Caller must hold m_ClientListMutex

    class ClientOutbox;
    struct ClientState
    {
        explicit ClientState( const ConnectionInfo * ci )
            : m_Connection( ci )
        {
            m_WaitingJobs.SetCapacity( 16 );
        }

        // Non-copyable
        explicit ClientState( const ClientState & other ) = delete;
        ClientState & operator=( const ClientState & other ) = delete;

        Mutex m_Mutex;
        Mutex m_SendMutex; // Held for every send on this connection (see SendTo). Innermost lock.

        const Protocol::IMessage * m_CurrentMessage = nullptr;
        const ConnectionInfo * m_Connection = nullptr;
        Atomic<uint32_t> m_NumJobsActive;
        uint32_t m_AllocatedCapacity = 0;

        AString m_HostName;

        Array<Job *> m_WaitingJobs; // jobs waiting for manifests/toolchains

        // Jobs are compressed against the earlier ones from the same connection,
        // so they are decoded in arrival order on the connection's receive thread
        JobStreamDecoder m_JobDecoder;
        PchReceiver m_PchReceiver; // The chunks of the PCH being received, in order
        Array<ToolManifest *> m_KnownTools; // toolchains this client may send jobs for, each holding a use
        Array<uint64_t> m_PchNeededSent; // PCHs asked of this client that have not arrived yet (under m_Mutex)

        uint32_t m_Id = 0;                  // From m_NextClientId; never reused
        ClientOutbox * m_Outbox = nullptr;  // Under m_ClientListMutex. Null once the client is going.
    };

    // A thread per client that sends what is queued for it, so a client that reads slowly holds back only itself.
    // Lock order: m_ClientListMutex, then the outbox's own mutex; the thread sends holding only cs.m_SendMutex.
    class ClientOutbox
    {
    public:
        explicit ClientOutbox( ClientState & cs );
        ~ClientOutbox(); // Stops and joins; what is queued is dropped

        // Caller holds m_ClientListMutex. A result counts in cs.m_NumJobsActive until it is sent.
        void PushResult( bool compressed, MemoryStream && payload );
        void PushMessage( const Protocol::IMessage & msg ); // Without a payload. Sent after what is queued before it.
        void Wake();

        explicit ClientOutbox( const ClientOutbox & other ) = delete;
        ClientOutbox & operator=( const ClientOutbox & other ) = delete;

    private:
        struct QueuedResult
        {
            MemoryStream *  m_Payload;              // A result's payload, or the bytes of a message
            uint32_t        m_AllocatedCapacity;    // Of the client when the result was queued
            bool            m_Compressed;
            bool            m_IsMessage;            // m_Payload is a whole message, sent as it is
        };

        static uint32_t ThreadFuncStatic( void * param );
        void ThreadFunc();
        void Send( const QueuedResult & result );

        ClientState & m_ClientState;
        Mutex m_Mutex;
        Array<QueuedResult> m_Results; // Under m_Mutex
        Semaphore m_WakeSemaphore;
        Atomic<bool> m_Quit;
        Thread m_Thread;
    };
    uint32_t m_NextClientId = 1; // Under m_ClientListMutex

    // Messages for one connection come from the server thread (results), the connection's
    // receive thread (ack, inventories) and OnDisconnected (file requests), so every send
    // goes through here. Lock order: m_ClientListMutex, cs.m_Mutex, m_ToolManifestsMutex, m_SendMutex.
    static bool SendTo( ClientState & cs, const Protocol::IMessage & msg );
    static bool SendTo( ClientState & cs, const Protocol::IMessage & msg, const MemoryStream & payload );
    template <class MSG>
    static void SendIdInventory( ClientState & cs, const Array<uint64_t> & ids );
    void RequestMissingFiles( ClientState & cs, ToolManifest * manifest ) const;

    // A job needs a PCH that is not cached. Asks the client for it, once until it arrives, unless
    // the client cannot be asked or the PCH is already being received. The caller holds
    // m_ClientListMutex and cs.m_Mutex.
    void RequestPchIfMissing( ClientState & cs, uint64_t pchId );
    bool MarkPchNeeded( ClientState & cs, uint64_t pchId ); // True if the client is to be asked. Same locks.
    bool IsPchBeingReceived( uint64_t pchId ) const; // Caller holds m_ClientListMutex
    bool IsPchCached( uint64_t pchId ); // Does not refresh LRU recency

    Forwarder * const m_Forwarder = nullptr; // Gateway mode
    AString m_CacheDir; // The root of the PCH store and the log, with a trailing slash. Empty if there is none.
    WorkerLog m_Log;
    uint32_t m_PrefetchBuffer = 0;
    JobQueueRemote * m_JobQueueRemote = nullptr; // None in a pure gateway

    Atomic<bool> m_ShouldExit; // signal from main thread
    Thread m_Thread; // the thread to manage workload
    mutable Mutex m_ClientListMutex;
    Array<ClientState *> m_ClientList;

    mutable Mutex m_ToolManifestsMutex;
    Array<ToolManifest *> m_Tools;

    // Toolchain cache: synchronized toolchains stay on disk (with a persisted manifest) across
    // connections and worker restarts, and are advertised to clients
    static constexpr uint32_t kMaxCachedToolchains = 6;
    void ScanToolchainCache();
    void SendToolchainInventory( ClientState & cs );
    void OnToolchainSynchronized( ToolManifest * manifest );
    void EvictToolchains( const ToolManifest * keep );
    Atomic<uint32_t> m_NumManifestsReceived;
    Atomic<uint32_t> m_NumToolFilesReceived;
    Atomic<uint32_t> m_NumPchChunksReceived;
    Atomic<uint32_t> m_NumPchsUnpacked;
    Atomic<uint32_t> m_NumPchUnpackAttempts;

    // PCH cache for distributed builds. A PCH is kept compressed, as it was sent (pch.<id>/pch.zst).
    // A job that needs one is held until it is unpacked into a working copy for the compiler
    // (pch.<id>/pch.pch), by a thread of its own so that no connection waits for the disk.
    struct PchCacheEntry
    {
        uint64_t    pchId;
        AString     filePath;       // The compressed PCH
        uint64_t    lastWriteTime;
        uint64_t    size = 0;       // Bytes on disk of the compressed PCH
    };

    // The compressed PCHs keep to kPchCacheSharePercent of ( free space + what the cache holds ), within
    // the min and max bytes; the oldest beyond that go, but not those in use or the last kMinCachedPchEntries.
    static constexpr uint64_t kMaxPchCacheBytes = ( 8ull * 1024 * 1024 * 1024 );
    static constexpr uint64_t kMinPchCacheBytes = ( 1ull * 1024 * 1024 * 1024 );
    static constexpr uint32_t kPchCacheSharePercent = 50;
    static constexpr uint32_t kMinCachedPchEntries = 4;
    static constexpr uint32_t kMaxCachedPchEntries = 256;
    static uint64_t CalculatePchCacheBudget( uint64_t freeBytes, uint64_t cacheBytes ); // Pure

    // The working copies that no job is using are kept for the next ones, up to kRawPchSharePercent of
    // ( free space + what the cache holds ) and at most kMaxRawPchBytes, and for kRawPchIdleSeconds.
    static constexpr uint64_t kMaxRawPchBytes = ( 2ull * 1024 * 1024 * 1024 );
    static constexpr uint32_t kRawPchSharePercent = 20;
    static constexpr uint32_t kRawPchIdleSeconds = 300;
    static uint64_t CalculateRawPchBudget( uint64_t freeBytes, uint64_t cacheBytes ); // Pure

    struct PchCacheLimits
    {
        uint64_t m_Bytes;           // Of compressed PCHs
        uint32_t m_MinEntries;
        uint64_t m_RawBytes = 0;    // Of working copies nothing uses
    };
    // The limits for a cache holding cacheBytes (compressed PCHs and working copies), or false if they
    // cannot be told. Tests replace it.
    using PchCacheLimitsFunc = bool ( * )( uint64_t cacheBytes, PchCacheLimits & outLimits );
    static bool QueryPchCacheLimits( uint64_t cacheBytes, PchCacheLimits & outLimits ); // From the free space of the cache volume
    PchCacheLimits GetPchCacheLimits( uint64_t cacheBytes ) const; // Reads the disk: not under m_PchCacheMutex
    static uint64_t CountPchBytes( const Array<PchCacheEntry> & entries );
    void ScanPchCache();
    static void RemoveEmptyPchDirectories( const AString & basePath ); // Left by evicted PCHs
    PchCacheEntry * FindStoredPch( uint64_t pchId ); // Caller holds m_PchCacheMutex
    static void RemovePchFiles( const PchCacheEntry & entry ); // The stored PCH, its working copy and its directory

    // The working copy of a PCH
    struct PchRawEntry
    {
        enum State : uint8_t
        {
            ABSENT,     // There is no working copy
            UNPACKING,  // A thread is making it
            READY,      // It is on disk, whole
        };
        uint64_t    pchId;
        State       state = ABSENT;
        uint32_t    numLeases = 0;      // Jobs that need the PCH
        uint64_t    size = 0;           // Bytes on disk when READY
        uint64_t    lastUsed = 0;       // File time
    };

    // PchLease::Owner: the job that held a lease is gone
    virtual void ReleasePchLease( uint64_t pchId ) override;

    // Sets the working copy as the job's PCH if it exists. If it does not, asks for it to be made (once)
    // and returns false: the job waits. Also false if the PCH is not cached. The caller holds cs.m_Mutex.
    bool TryResolvePch( Job & job );

    static uint32_t UnpackThreadFuncStatic( void * param );
    void UnpackThreadFunc();
    void UnpackPch( uint64_t pchId );
    static bool GetRawPchPath( const AString & storedFile, AString & outRawFile ); // The working copy next to the stored PCH

    // What goes: the working copies nothing uses, the longest unused first, until they come to budgetBytes;
    // and those unused for more than idleSeconds at file time now
    static void SelectRawPchTrims( const Array<PchRawEntry> & entries,
                                   uint64_t budgetBytes,
                                   uint64_t now,
                                   uint32_t idleSeconds,
                                   Array<size_t> & outIndicesToTrim );
    void TrimRawPchs( uint64_t now ); // Reads the disk, then takes m_PchCacheMutex
    void DropUnusedRawPchs(); // All of them, to make room
    void RemoveUnusedRawPchs( uint64_t now, uint64_t budgetBytes );
    uint64_t CountRawPchBytes() const; // Caller holds m_PchCacheMutex
    PchRawEntry * FindRawPch( uint64_t pchId ); // Caller holds m_PchCacheMutex

    // Disconnects the clients with jobs waiting for the PCH, so that the jobs go back to them.
    void DisconnectClientsWaitingForPch( uint64_t pchId );

    // Oldest entries first, skipping those in pchIdsInUse, until the entries are within budgetBytes
    // and kMaxCachedPchEntries (but not below minEntries because of the bytes)
    static void SelectPchEvictions( const Array<PchCacheEntry> & entries,
                                     uint64_t budgetBytes,
                                     uint32_t minEntries,
                                     const Array<uint64_t> & pchIdsInUse,
                                     Array<size_t> & outIndicesToEvict );
    // Deletes the PCHs, and their working copies. The ids of those evicted are added to outEvictedIds.
    static void EvictPchs( Array<PchCacheEntry> & entries,
                           uint64_t budgetBytes,
                           uint32_t minEntries,
                           const Array<uint64_t> & pchIdsInUse,
                           Array<uint64_t> * outEvictedIds = nullptr );

    mutable Mutex m_PchCacheMutex;
    Array<PchCacheEntry> m_PchCache;
    Array<PchRawEntry> m_PchRaw;                // Under m_PchCacheMutex, like the above
    Array<uint64_t> m_PchUnpackQueue;           // Under m_PchCacheMutex
    bool m_PchCacheScanned = false;
    PchCacheLimitsFunc m_PchCacheLimitsFunc = &QueryPchCacheLimits;
    Thread m_PchUnpackThread;
    Semaphore m_PchUnpackSemaphore;
    Timer m_PchTrimTimer;

#if defined( __OSX__ ) || defined( __LINUX__ )
    Timer m_TouchToolchainTimer;
#endif
};

//------------------------------------------------------------------------------
