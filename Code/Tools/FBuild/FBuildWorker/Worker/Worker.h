// Worker
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "IdleDetection.h"

// FBuild
#include "Tools/FBuild/FBuildCore/WorkerPool/WorkerBrokerageServer.h"

// Core
#include "Core/Containers/Singleton.h"
#include "Core/Env/MSVCStaticAnalysis.h"
#include "Core/FileIO/FileStream.h"

// Forward Declarations
//------------------------------------------------------------------------------
class Gateway;
class Server;
class WorkerWindow;
class JobQueueRemote;
class NetworkStartupHelper;
class WorkerSettings;

// Worker
//------------------------------------------------------------------------------
class Worker : public Singleton<Worker>
{
public:
    explicit Worker( const AString & args, bool consoleMode, bool periodicRestart, uint32_t numThreadsInJobQueue = 0, uint32_t prefetchBuffer = 0,
            const AString & gatewayConfigFile = AString::GetEmpty(), bool noBrokerage = false );
    ~Worker();

    int32_t Work();

    void SetWantToQuit() { m_WantToQuit = true; }

    Worker & operator=( Worker & ) = delete;

private:
    static uint32_t WorkThreadWrapper( void * userData );
    uint32_t WorkThread();

    void UpdateAvailability();
    void UpdateUI();
    void CheckIfRestartNeeded();
    void SignalStopWorkers();
    bool HasEnoughDiskSpace();
    bool HasEnoughMemory();

    bool InConsoleMode() const { return m_ConsoleMode; }

    void StatusMessage( MSVC_SAL_PRINTF const char * fmtString, ... ) const FORMAT_STRING( 2, 3 );
    void ErrorMessage( MSVC_SAL_PRINTF const char * fmtString, ... ) const FORMAT_STRING( 2, 3 );

    const bool m_ConsoleMode;
    const bool m_PeriodicRestart;
    WorkerWindow * m_MainWindow = nullptr;
    Server * m_ConnectionPool = nullptr;    // The gateway's when there is one
    Gateway * m_Gateway = nullptr;
    AString m_GatewayConfigError;
    const bool m_UseBrokerage;
    NetworkStartupHelper * m_NetworkStartupHelper = nullptr;
    WorkerSettings * m_WorkerSettings = nullptr;
    IdleDetection m_IdleDetection;
    WorkerBrokerageServer m_WorkerBrokerage;
    AString m_BaseExeName;
    AString m_BaseArgs;
    uint64_t m_LastWriteTime = 0;
    bool m_WantToQuit = false;
    bool m_RestartNeeded = false;
    Timer m_PeriodicRestartTimer;
    Timer m_UIUpdateTimer;
    FileStream m_TargetIncludeFolderLock;
#if defined( __WINDOWS__ )
    Timer m_TimerLastDiskSpaceCheck;
    int32_t m_LastDiskSpaceResult = -1; // -1 : No check done yet. 0=Not enough space right now. 1=OK for now.

    Timer m_TimerLastMemoryCheck;
    int32_t m_LastMemoryCheckResult = -1; // -1 : No check done yet. 0=Not enough memory right now. 1=OK for now.
#endif
    mutable AString m_LastStatusMessage;
};

//------------------------------------------------------------------------------
