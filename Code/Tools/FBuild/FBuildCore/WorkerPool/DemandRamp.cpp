// DemandRamp.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "DemandRamp.h"

// Core
#include "Core/Math/Conversions.h"

// Update
//------------------------------------------------------------------------------
uint32_t DemandRamp::Update( uint32_t waiting, uint32_t inFlight, float nowSeconds )
{
    if ( m_Started == false )
    {
        m_Started = true;
        m_LastGrowSeconds = nowSeconds;
    }

    if ( waiting > 0 )
    {
        m_Idle = false;
        const bool mostInUse = ( ( (uint64_t)inFlight * 100 ) >= ( (uint64_t)m_Ramp * kGrowWhenUsedPercent ) );
        if ( mostInUse && ( ( nowSeconds - m_LastGrowSeconds ) >= kGrowIntervalSeconds ) && ( m_Ramp < kMaxDemand ) )
        {
            m_Ramp = (uint32_t)Math::Min<uint64_t>( (uint64_t)m_Ramp * kGrowFactor, kMaxDemand );
            m_LastGrowSeconds = nowSeconds;
        }
    }
    else
    {
        if ( m_Idle == false )
        {
            m_Idle = true;
            m_IdleSinceSeconds = nowSeconds;
        }
        if ( ( nowSeconds - m_IdleSinceSeconds ) >= kShrinkAfterIdleSeconds )
        {
            m_Ramp = Math::Min( Math::Max( kStartSlots, inFlight ), kMaxDemand );
        }
    }

    return (uint32_t)Math::Min<uint64_t>( m_Ramp, (uint64_t)inFlight + waiting );
}

// ShouldSend
//------------------------------------------------------------------------------
bool DemandRamp::ShouldSend( uint32_t demand, float nowSeconds ) const
{
    if ( m_HasSent == false )
    {
        return true;
    }
    if ( demand == m_SentDemand )
    {
        return false;
    }
    if ( ( demand == 0 ) || ( m_SentDemand == 0 ) )
    {
        return true;
    }
    if ( ( nowSeconds - m_SentSeconds ) < kSendMinIntervalSeconds )
    {
        return false;
    }
    const uint32_t delta = ( demand > m_SentDemand ) ? ( demand - m_SentDemand ) : ( m_SentDemand - demand );
    return ( delta >= kSendMinDelta ) &&
           ( ( (uint64_t)delta * 100 ) >= ( (uint64_t)m_SentDemand * kSendMinDeltaPercent ) );
}

// OnSent
//------------------------------------------------------------------------------
void DemandRamp::OnSent( uint32_t demand, float nowSeconds )
{
    m_HasSent = true;
    m_SentDemand = demand;
    m_SentSeconds = nowSeconds;
}

//------------------------------------------------------------------------------
