// DemandRamp - How many jobs a client asks a worker for
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"

// DemandRamp
//------------------------------------------------------------------------------
// A client's demand on one worker starts small and doubles, at most every kGrowIntervalSeconds, while
// jobs wait and kGrowWhenUsedPercent of the ramp is in flight. After kShrinkAfterIdleSeconds with
// nothing waiting it falls back to max( kStartSlots, in flight ). A demand is reported when it differs
// from the last one sent by at least max( kSendMinDelta, kSendMinDeltaPercent ), and at least
// kSendMinIntervalSeconds after it; the first, and a change to or from 0, are sent at once.
// Time is the caller's, in seconds.
//------------------------------------------------------------------------------
class DemandRamp   // Not thread safe: one thread only (the client thread)
{
public:
    static constexpr uint32_t kStartSlots = 4;
    static constexpr uint32_t kGrowFactor = 2;
    static constexpr uint32_t kGrowWhenUsedPercent = 75;
    static constexpr float kGrowIntervalSeconds = 0.5f;
    static constexpr float kShrinkAfterIdleSeconds = 2.0f;
    static constexpr uint32_t kMaxDemand = 65535;
    static constexpr uint32_t kSendMinDelta = 2;
    static constexpr uint32_t kSendMinDeltaPercent = 12;
    static constexpr float kSendMinIntervalSeconds = 0.25f;  // A change to or from 0 is sent at once

    uint32_t Update( uint32_t waiting, uint32_t inFlight, float nowSeconds ); // Returns min( GetRamp(), inFlight + waiting )
    uint32_t GetRamp() const { return m_Ramp; }
    bool ShouldSend( uint32_t demand, float nowSeconds ) const;
    void OnSent( uint32_t demand, float nowSeconds );

private:
    uint32_t    m_Ramp = kStartSlots;
    bool        m_Started = false;          // Update has been called
    bool        m_Idle = false;             // Nothing waited at the last Update
    bool        m_HasSent = false;
    float       m_LastGrowSeconds = 0.0f;   // Or the first Update
    float       m_IdleSinceSeconds = 0.0f;  // While m_Idle
    uint32_t    m_SentDemand = 0;
    float       m_SentSeconds = 0.0f;
};

//------------------------------------------------------------------------------
