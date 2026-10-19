// FairShareAllocator.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FairShareAllocator.h"

// Core
#include "Core/Env/Assert.h"
#include "Core/Math/Conversions.h"

// SetPoolSlots
//------------------------------------------------------------------------------
void FairShareAllocator::SetPoolSlots( uint32_t slots )
{
    m_PoolSlots = slots;
}

// AddClient
//------------------------------------------------------------------------------
void FairShareAllocator::AddClient( uint32_t clientId )
{
    if ( FindClient( clientId ) == nullptr )
    {
        m_Clients.EmplaceBack().m_Id = clientId;
    }
}

// SetDemand
//------------------------------------------------------------------------------
void FairShareAllocator::SetDemand( uint32_t clientId, uint32_t demand )
{
    Client * client = FindClient( clientId );
    if ( client )
    {
        client->m_Demand = demand;
    }
}

// RemoveClient
//------------------------------------------------------------------------------
void FairShareAllocator::RemoveClient( uint32_t clientId )
{
    Client * client = FindClient( clientId );
    if ( client )
    {
        m_Clients.Erase( client );
    }
}

// Recompute
//------------------------------------------------------------------------------
void FairShareAllocator::Recompute( Array<uint32_t> & outChanged )
{
    outChanged.Clear();

    // Everyone gets their demand up to the water level
    const uint32_t level = GetWaterLevel();
    uint64_t used = 0;
    bool anyWantMore = false;
    for ( Client & client : m_Clients )
    {
        client.m_Share = Math::Min( client.m_Demand, level );
        used += client.m_Share;
        anyWantMore |= ( client.m_Demand > level );
    }

    // When some want more, the slots left are fewer than they are: one each, in order
    const uint32_t remainder = anyWantMore ? (uint32_t)( m_PoolSlots - used ) : 0;
    const size_t numClients = m_Clients.GetSize();
    for ( uint32_t i = 0; i < remainder; ++i )
    {
        size_t best = numClients;
        for ( size_t c = 0; c < numClients; ++c )
        {
            const Client & client = m_Clients[ c ];
            if ( ( client.m_Demand > level ) && ( client.m_Share == level ) &&
                 ( ( best == numClients ) || TakesRemainderBefore( c, best, level ) ) )
            {
                best = c;
            }
        }
        ASSERT( best < numClients );
        ++m_Clients[ best ].m_Share;
    }

    for ( Client & client : m_Clients )
    {
        const uint32_t allowance = GetNewAllowance( client, level, ( remainder > 0 ) );
        if ( allowance != client.m_Allowance )
        {
            client.m_Allowance = allowance;
            outChanged.Append( client.m_Id );
        }
    }
    ++m_Rotation;
}

// IsScarce
//------------------------------------------------------------------------------
bool FairShareAllocator::IsScarce() const
{
    uint32_t numWithDemand = 0;
    for ( const Client & client : m_Clients )
    {
        numWithDemand += ( client.m_Demand > 0 ) ? 1 : 0;
    }
    return ( numWithDemand > m_PoolSlots );
}

// GetAllowance
//------------------------------------------------------------------------------
uint32_t FairShareAllocator::GetAllowance( uint32_t clientId ) const
{
    const Client * client = FindClient( clientId );
    return client ? client->m_Allowance : 0;
}

// GetWaterLevel
//------------------------------------------------------------------------------
uint32_t FairShareAllocator::GetWaterLevel() const
{
    uint32_t maxDemand = 0;
    for ( const Client & client : m_Clients )
    {
        maxDemand = Math::Max( maxDemand, client.m_Demand );
    }
    auto fits = [ this ]( uint32_t level )
    {
        uint64_t sum = 0;
        for ( const Client & client : m_Clients )
        {
            sum += Math::Min( client.m_Demand, level );
        }
        return ( sum <= m_PoolSlots );
    };
    if ( fits( maxDemand ) )
    {
        return maxDemand;
    }

    // fits( low ) holds and fits( high ) does not
    uint32_t low = 0;
    uint32_t high = maxDemand;
    while ( ( high - low ) > 1 )
    {
        const uint32_t mid = low + ( ( high - low ) / 2 );
        if ( fits( mid ) )
        {
            low = mid;
        }
        else
        {
            high = mid;
        }
    }
    return low;
}

// TakesRemainderBefore
//------------------------------------------------------------------------------
bool FairShareAllocator::TakesRemainderBefore( size_t a, size_t b, uint32_t level ) const
{
    const uint32_t unmetA = ( m_Clients[ a ].m_Demand - level );
    const uint32_t unmetB = ( m_Clients[ b ].m_Demand - level );
    if ( unmetA != unmetB )
    {
        return ( unmetA > unmetB );
    }
    const size_t numClients = m_Clients.GetSize();
    const size_t start = ( m_Rotation % numClients );
    return ( ( ( a + numClients - start ) % numClients ) < ( ( b + numClients - start ) % numClients ) );
}

// GetNewAllowance
//------------------------------------------------------------------------------
uint32_t FairShareAllocator::GetNewAllowance( const Client & client, uint32_t level, bool hasRemainder ) const
{
    const uint32_t target = client.m_Share;
    const uint32_t old = client.m_Allowance;
    if ( target >= old )
    {
        return target;
    }
    const uint32_t reduction = ( old - target );
    const bool lostARemainderSlot = hasRemainder && ( client.m_Demand > level ) && ( target == level ) && ( reduction == 1 );
    const bool pastThreshold = ( reduction >= kHysteresisSlots ) &&
                               ( ( (uint64_t)reduction * 100 ) >= ( (uint64_t)old * kHysteresisPercent ) );
    if ( lostARemainderSlot || pastThreshold )
    {
        return target;
    }
    // Held back, but never above the demand or the pool: a target of the full demand (0 included) applies at once
    return Math::Min( old, Math::Min( client.m_Demand, m_PoolSlots ) );
}

// FindClient
//------------------------------------------------------------------------------
FairShareAllocator::Client * FairShareAllocator::FindClient( uint32_t clientId )
{
    for ( Client & client : m_Clients )
    {
        if ( client.m_Id == clientId )
        {
            return &client;
        }
    }
    return nullptr;
}

// FindClient
//------------------------------------------------------------------------------
const FairShareAllocator::Client * FairShareAllocator::FindClient( uint32_t clientId ) const
{
    for ( const Client & client : m_Clients )
    {
        if ( client.m_Id == clientId )
        {
            return &client;
        }
    }
    return nullptr;
}

//------------------------------------------------------------------------------
