// FairShareAllocator - How many of a worker's slots each client may use
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Array.h"
#include "Core/Env/Types.h"

// FairShareAllocator
//------------------------------------------------------------------------------
// Shares a worker's slots between its clients by demand alone: max-min fairness (water-filling). No demand,
// no slots. When there are fewer slots than clients can use, the remainder goes one slot each to the clients
// with the most unmet demand, and ties start from a position that rotates on every Recompute, so a client is
// never left out for good. A smaller allowance replaces the old one only past the hysteresis threshold; a larger
// one, one equal to the client's full demand (0 included), or a change of a single remainder slot, always does.
// An allowance held back is still never more than the client's demand or the pool.
// Allowances below what a client has in flight only stop new admissions. Not thread safe.
//------------------------------------------------------------------------------
class FairShareAllocator
{
public:
    static constexpr uint32_t kInitialDemand = 4;         // Assumed for a new client (= DemandRamp::kStartSlots); its share is the ack's allowance
    static constexpr uint32_t kHysteresisSlots = 2;
    static constexpr uint32_t kHysteresisPercent = 10;    // A reduction applies if >= max( kHysteresisSlots, this % )

    void SetPoolSlots( uint32_t slots );
    uint32_t GetPoolSlots() const { return m_PoolSlots; }
    void AddClient( uint32_t clientId );
    void SetDemand( uint32_t clientId, uint32_t demand );
    void RemoveClient( uint32_t clientId );
    void Recompute( Array<uint32_t> & outChanged );       // The ids whose allowance changed, in the order they were added
    bool IsScarce() const;                                // Fewer slots than the clients with demand; the caller recomputes each second to rotate
    uint32_t GetAllowance( uint32_t clientId ) const;     // 0 for an unknown client

private:
    struct Client
    {
        uint32_t m_Id = 0;
        uint32_t m_Demand = kInitialDemand;
        uint32_t m_Allowance = 0;
        uint32_t m_Share = 0;           // Within Recompute: the water level's share, then the target
    };

    // The highest level L at which the clients' min( demand, L ) fit in the pool
    uint32_t GetWaterLevel() const;
    // Whether client a comes before b for a remainder slot: more unmet demand, then the rotated position
    bool TakesRemainderBefore( size_t a, size_t b, uint32_t level ) const;
    uint32_t GetNewAllowance( const Client & client, uint32_t level, bool hasRemainder ) const;
    Client * FindClient( uint32_t clientId );
    const Client * FindClient( uint32_t clientId ) const;

    Array<Client>   m_Clients;          // In the order they were added
    uint32_t        m_PoolSlots = 0;
    uint32_t        m_Rotation = 0;     // Advanced by every Recompute
};

//------------------------------------------------------------------------------
