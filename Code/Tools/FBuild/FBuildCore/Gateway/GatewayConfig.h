// GatewayConfig - The workers behind a gateway, and its timings
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Strings/AString.h"

// GatewayConfig
//------------------------------------------------------------------------------
struct GatewayConfig
{
    struct Worker
    {
        AString  m_Host;
        uint16_t m_Port = Protocol::kPort;
    };
    Array<Worker> m_Workers;
    AString m_CacheDir;                   // Empty: FBuild::GetWorkerCacheDir

    float m_JobTimeoutSeconds = 600.0f;
    float m_ReconnectSeconds = 10.0f;
    float m_NoWorkerGiveUpSeconds = 30.0f; // How long a waiting job that no worker can take waits before it is returned to its client
    float m_StatsSeconds = 60.0f;

    // One worker per line: "host" or "host:port"; ';' or '#' starts a comment; blank lines skipped.
    // Duplicates, an empty host and a port outside 1..65535 are errors. Read once, at start.
    [[nodiscard]] static bool Parse( const AString & text, GatewayConfig & outConfig, AString & outError );
    [[nodiscard]] static bool Load( const AString & path, GatewayConfig & outConfig, AString & outError );
};

//------------------------------------------------------------------------------
