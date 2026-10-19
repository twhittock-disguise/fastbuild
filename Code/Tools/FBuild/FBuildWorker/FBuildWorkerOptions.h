// Main
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
// FBuild
#include "Tools/FBuild/FBuildWorker/Worker/WorkerSettings.h"

// Core
#include "Core/Env/Types.h"
#include "Core/Strings/AString.h"

// FBuildWorkerOptions
//------------------------------------------------------------------------------
class FBuildWorkerOptions
{
public:
    FBuildWorkerOptions();

    bool ProcessCommandLine( const AString & commandLine );

    // sub-process control
#if defined( __WINDOWS__ )
    bool m_IsSubprocess = false; // Process is child? (Internal)
    bool m_UseSubprocess = true; // Should we launch a sub-process?
#endif

    // resource usage
    bool m_OverrideCPUAllocation = false;
    uint32_t m_CPUAllocation = 0;
    bool m_OverrideWorkMode = false;
    WorkerSettings::Mode m_WorkMode = WorkerSettings::WHEN_IDLE;
    uint32_t m_MinimumFreeMemoryMiB = 0; // Minimum OS free memory including virtual memory to let worker do its work
    bool m_OverridePrefetch = false;
    uint32_t m_PrefetchBuffer = 0;      // 0 = use default ceil(cpus/2)

    // Console mode
    bool m_ConsoleMode = false;

    // Gateway
    AString m_GatewayConfigFile;        // Non-empty: run as a gateway for the workers listed in this file
    bool m_NoBrokerage = false;         // Don't register in the worker brokerage

    // Other
    bool m_PeriodicRestart = false;

private:
    void ShowUsageError();
};

//------------------------------------------------------------------------------
