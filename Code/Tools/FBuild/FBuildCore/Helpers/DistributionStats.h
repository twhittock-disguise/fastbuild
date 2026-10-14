// DistributionStats.h - Per worker connection upload summary for distributed builds
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"
#include "Core/Strings/AString.h"

// DistributionSendStats
//------------------------------------------------------------------------------
struct DistributionSendStats
{
    uint32_t    m_Messages = 0;
    uint64_t    m_RawBytes = 0;     // Before compression
    uint64_t    m_SentBytes = 0;
    double      m_SendTime = 0.0;   // Seconds spent handing data to the socket
    double      m_LastSentAt = 0.0; // Connection time of the last message handed over
};

// DistributionConnectionStats
//------------------------------------------------------------------------------
struct DistributionConnectionStats
{
    AString                 m_WorkerAddress;
    double                  m_ConnectedTime = 0.0;
    DistributionSendStats   m_Toolchain;
    DistributionSendStats   m_Pch;
    DistributionSendStats   m_Jobs;
    uint32_t                m_ResultsReceived = 0;
    uint64_t                m_ResultBytesReceived = 0;
};

// DistributionStats
//------------------------------------------------------------------------------
class DistributionStats
{
public:
    static void Format( const DistributionConnectionStats & stats,
                        const char * timestamp,
                        AString & outText );

    // Appends to the log. A log that would pass maxLogSize is first moved to
    // <path>.old, replacing any earlier one.
    static bool AppendToLog( const AString & path, const AString & text, uint64_t maxLogSize );

    // Appends a summary of a closed connection to the log in the temp directory
    // (best effort)
    static void WriteToTempLog( const DistributionConnectionStats & stats );
};

//------------------------------------------------------------------------------
