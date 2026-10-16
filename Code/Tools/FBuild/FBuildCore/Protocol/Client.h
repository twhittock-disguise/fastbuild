// Client.h - Handles Client Side connections
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
// FBuild
#include "Tools/FBuild/FBuildCore/WorkerPool/WorkerBrokerageClient.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Containers/UniquePtr.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Mutex.h"
#include "Core/Process/Thread.h"
#include "Core/Strings/AString.h"
#include "Core/Time/Timer.h"

// Forward Declarations
//------------------------------------------------------------------------------
class ClientToWorkerConnection;
class ClientWorkerInfo;
class Job;
class MemoryStream;
class MultiBuffer;
namespace Protocol
{
    class IMessage;
    class MsgConnectionAck;
    class MsgJobResult;
    class MsgJobResultCompressed;
    class MsgRequestManifest;
    class MsgPchInventory;
    class MsgRequestFile;
    class MsgPchNeeded;
}
class ToolManifest;

// Client
//------------------------------------------------------------------------------
class Client
{
public:
    Client( const Array<AString> & explicitWorkerList,
            uint16_t port,
            uint32_t workerConnectionLimit,
            bool detailedLogging );
    ~Client();

    uint16_t GetPort() const { return m_Port; }

    uint32_t GetNumConnections() const;
    uint32_t GetNumDenyListedWorkers() const; // Workers that refused this client or misbehaved
    void DenyListWorker( ClientWorkerInfo & worker ); // Called from the receive thread of the worker's connection

    // Toolchains the build is about to need on workers
    void AddToolchain( const ToolManifest * manifest );
    void GetToolchains( Array<const ToolManifest *> & outManifests ) const;
    uint32_t GetToolchainsGeneration() const { return m_ToolchainsGeneration.Load(); } // Changes when one is added

    // Test seam: points of a connection where the order of two of its threads matters, so that a test can hold the
    // thread that reaches one until the other has come as far as it needs. The hook runs on the thread that reached
    // the point, and is given whether the connection has ended.
    enum HookPoint
    {
        BEFORE_PCH_DELIVERED,   // The send thread, before it counts the last chunk of a PCH as delivered
        BEFORE_JOB_TRACKED,     // The thread that pushes jobs, before it adds a job it took to those of the connection
    };
    typedef void ( *TestHook )( HookPoint point, const Atomic<bool> & connectionEnded );
    static void SetTestHook( TestHook hook ) { s_TestHook = hook; }
    static void RunTestHook( HookPoint point, const Atomic<bool> & connectionEnded )
    {
        if ( s_TestHook )
        {
            s_TestHook( point, connectionEnded );
        }
    }

private:
    inline static TestHook s_TestHook = nullptr;
    Client & operator=( const Client & other ) = delete;

    static uint32_t ThreadFuncStatic( void * param );
    void ThreadFunc();

    void FindPotentialWorkers();
    void RegisterFoundWorkers( const Array<AString> & workerList,
                               const AString * brokeragePaths );
    void ConnectToWorkers();
    void CommunicateJobAvailability();

    // Worker pool
    bool m_WorkerDiscoveryDone = false;
    Array<AString> m_StaticWorkerList; // optional explicit list to use (overrides discovery)
    Array<UniquePtr<ClientWorkerInfo>> m_WorkerPool; // workers to potentially connect to
    uint32_t m_NextWorkerIndex = 0; // next worker we will attempt to connect to
    WorkerBrokerageClient m_WorkerBrokerage; // Helper for access to brokerage

    // Primary "Client" thread
    Atomic<bool> m_ShouldExit{ false }; // signal from main thread
    Thread m_Thread; // the thread to find and manage workers

    // state
    Timer m_StatusUpdateTimer;

    // Workers we are connected to (or establishing/cleaning up a connection to)
    Array<UniquePtr<ClientToWorkerConnection>> m_ActiveConnections;
    Atomic<uint32_t> m_NumDenyListedWorkers{ 0 };

    mutable Mutex m_ToolchainsMutex;
    Array<const ToolManifest *> m_Toolchains;
    Atomic<uint32_t> m_ToolchainsGeneration{ 0 };

    // State that remains constant throughout the build
    const bool m_DetailedLogging;
    const uint32_t m_WorkerConnectionLimit;
    const uint16_t m_Port;
};

//------------------------------------------------------------------------------
