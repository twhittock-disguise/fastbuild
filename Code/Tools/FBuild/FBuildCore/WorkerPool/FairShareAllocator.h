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
// no slots. The remainder below the water level goes one slot each to the clients that want more:
// - at a level of 0 (fewer slots than clients with demand), to the one served a remainder slot longest ago,
//   so the slots rotate evenly and no client is left out for good;
// - above it, to those that already hold one, so that one spare slot does not move on every Recompute.
// A smaller allowance replaces the old one only past the hysteresis threshold; a larger one, one equal to the
// client's full demand (0 included), or the loss of the single slot of a level of 0, always does. An allowance
// held back is still never more than the client's demand or the pool.
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
        uint64_t m_LastRemainderSerial = 0; // 0: never given a remainder slot
    };

    // The highest level L at which the clients' min( demand, L ) fit in the pool
    uint32_t GetWaterLevel() const;
    // Whether a comes before b for a remainder slot. Ties keep the list order.
    static bool TakesRemainderBefore( const Client & a, const Client & b, uint32_t level );
    uint32_t GetNewAllowance( const Client & client, uint32_t level ) const;
    Client * FindClient( uint32_t clientId );
    const Client * FindClient( uint32_t clientId ) const;

    Array<Client>   m_Clients;          // In the order they were added
    uint32_t        m_PoolSlots = 0;
    uint64_t        m_RemainderSerial = 0; // Of the last remainder slot given
};

//------------------------------------------------------------------------------
