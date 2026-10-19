// WorkerLog.h - A persistent, timestamped log of what a worker does
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"
#include "Core/Process/Atomic.h"
#include "Core/Strings/AString.h"

// WorkerLog
//------------------------------------------------------------------------------
// Events only, one line each: "[YYYY-MM-DD HH:MM:SS] <event> <key=value ...>". It outlives the worker
// process, so what a worker did can be read after it was restarted. Best effort: a log that cannot
// be written is switched off, and never stops the worker from serving.
class WorkerLog
{
public:
    static constexpr const char * kFileName = "FBuildWorker.log";
    static constexpr uint64_t kMaxFileSize = ( 10 * MEGABYTE ); // Then moved to <name>.old: two files at most

    // Creates the directory. One that is empty or cannot be created disables the log.
    explicit WorkerLog( const AString & directory, uint64_t maxFileSize = kMaxFileSize );

    // Appends one timestamped line. Thread safe. A write that fails disables the log, with one warning.
    void Write( MSVC_SAL_PRINTF const char * fmtString, ... ) FORMAT_STRING( 2, 3 );

    bool IsEnabled() const { return m_Enabled.Load(); }
    const AString & GetPath() const { return m_Path; }

private:
    AString             m_Path;
    uint64_t            m_MaxFileSize;
    Atomic<bool>        m_Enabled{ false };
    Atomic<uint32_t>    m_NumFailures{ 0 };
};

//------------------------------------------------------------------------------
