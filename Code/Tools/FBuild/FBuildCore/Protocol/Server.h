// Server.h - Handles Server Side connections
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
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
    void Process( const ConnectionInfo * connection, const Protocol::MsgConnection * msg );
    void Process( const ConnectionInfo * connection, const Protocol::MsgJob * msg, const void * payload, size_t payloadSize );
    void Process( const ConnectionInfo * connection, const Protocol::MsgManifest * msg, const void * payload, size_t payloadSize );
    void Process( const ConnectionInfo * connection, const Protocol::MsgFile * msg, const void * payload, size_t payloadSize );
    void Process( const ConnectionInfo * connection, const Protocol::MsgPchFile * msg, const void * payload, size_t payloadSize );

    static uint32_t ThreadFuncStatic( void * param );
    void ThreadFunc();

    void FinalizeCompletedJobs();
    void TouchToolchains();
    void ReleaseReadyWaitingJobs();

    void RequestMissingFiles( const ConnectionInfo * connection, ToolManifest * manifest ) const;
    void RecalculateCapacity();        // Takes m_ClientListMutex
    void RecalculateCapacityLocked();  // Caller must hold m_ClientListMutex

    struct ClientState
    {
        explicit ClientState( const ConnectionInfo * ci )
            : m_Connection( ci )
        {
            m_WaitingJobs.SetCapacity( 16 );
        }

        Mutex m_Mutex;

        const Protocol::IMessage * m_CurrentMessage = nullptr;
        const ConnectionInfo * m_Connection = nullptr;
        Atomic<uint32_t> m_NumJobsActive;
        uint32_t m_AllocatedCapacity = 0;

        AString m_HostName;

        Array<Job *> m_WaitingJobs; // jobs waiting for manifests/toolchains
    };

    uint32_t m_PrefetchBuffer = 0;
    JobQueueRemote * m_JobQueueRemote;

    Atomic<bool> m_ShouldExit; // signal from main thread
    Thread m_Thread; // the thread to manage workload
    Mutex m_ClientListMutex;
    Array<ClientState *> m_ClientList;

    mutable Mutex m_ToolManifestsMutex;
    Array<ToolManifest *> m_Tools;

    // PCH cache for distributed builds
    struct PchCacheEntry
    {
        uint64_t    pchId;
        AString     filePath;
    };
    void ScanPchCache();
    bool FindCachedPch( uint64_t pchId, AString & outPath ) const;
    mutable Mutex m_PchCacheMutex;
    Array<PchCacheEntry> m_PchCache;
    bool m_PchCacheScanned = false;

#if defined( __OSX__ ) || defined( __LINUX__ )
    Timer m_TouchToolchainTimer;
#endif
};

//------------------------------------------------------------------------------
