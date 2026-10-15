// Server.h - Handles Server Side connections
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildCore/Helpers/JobStream.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchReceiver.h"

#include "Core/Network/TCPConnectionPool.h"
#include "Core/Process/Atomic.h"
#include "Core/Time/Timer.h"

// Forward Declarations
//------------------------------------------------------------------------------
class Job;
class JobQueueRemote;
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
class Server : public TCPConnectionPool
{
    friend class TestDistributed;
public:
    Server( uint32_t numThreadsInJobQueue = 0, uint32_t prefetchBuffer = 0 );
    virtual ~Server() override;

    static void GetHostForJob( const Job * job, AString & hostName );

    bool IsSynchingTool( AString & statusStr ) const;

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

    void FinalizeCompletedJobs();
    void TouchToolchains();
    void ReleaseReadyWaitingJobs();

    void RecalculateCapacity();        // Takes m_ClientListMutex
    void RecalculateCapacityLocked();  // Caller must hold m_ClientListMutex

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
    };

    // Messages for one connection come from the server thread (results), the connection's
    // receive thread (ack, inventories) and OnDisconnected (file requests), so every send
    // goes through here. Lock order: m_ClientListMutex, cs.m_Mutex, m_ToolManifestsMutex, m_SendMutex.
    static bool SendTo( ClientState & cs, const Protocol::IMessage & msg );
    static bool SendTo( ClientState & cs, const Protocol::IMessage & msg, const MemoryStream & payload );
    template <class MSG>
    static void SendIdInventory( ClientState & cs, const Array<uint64_t> & ids );
    void RequestMissingFiles( ClientState & cs, ToolManifest * manifest ) const;

    uint32_t m_PrefetchBuffer = 0;
    JobQueueRemote * m_JobQueueRemote;

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

    // PCH cache for distributed builds
    struct PchCacheEntry
    {
        uint64_t    pchId;
        AString     filePath;
        uint64_t    lastWriteTime;
    };
    void ScanPchCache();
    static void RemoveStaleTempPchFiles( const AString & basePath ); // Left by a crashed worker
    static void RemoveEmptyPchDirectories( const AString & basePath ); // Left by evicted PCHs
    bool FindCachedPch( uint64_t pchId, AString & outPath ); // refreshes LRU recency

    // PCH ids referenced by jobs that are waiting, pending or in flight
    void CollectPchIdsInUse( Array<uint64_t> & outPchIds ) const;

    // Oldest entries first, skipping those in pchIdsInUse
    static void SelectPchEvictions( const Array<PchCacheEntry> & entries,
                                     uint32_t maxEntries,
                                     const Array<uint64_t> & pchIdsInUse,
                                     Array<size_t> & outIndicesToEvict );
    static void EvictPchs( Array<PchCacheEntry> & entries, const Array<uint64_t> & pchIdsInUse );

    mutable Mutex m_PchCacheMutex;
    Array<PchCacheEntry> m_PchCache;
    bool m_PchCacheScanned = false;

#if defined( __OSX__ ) || defined( __LINUX__ )
    Timer m_TouchToolchainTimer;
#endif
};

//------------------------------------------------------------------------------
