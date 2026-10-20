// LruSelect - Picking cache entries to evict, least recently used first
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Array.h"
#include "Core/Env/Types.h"

// LruLimits
//------------------------------------------------------------------------------
struct LruLimits
{
    size_t      m_MaxEntries;
    size_t      m_MinEntries;   // Not evicted for the sake of m_MaxBytes
    uint64_t    m_MaxBytes;
};

// SelectLeastRecentlyUsed
//------------------------------------------------------------------------------
// Picks entries to evict, least recently used (lowest age value) first, until at most m_MaxEntries remain
// and they come to at most m_MaxBytes (which never takes the count below m_MinEntries).
// Protected entries are never picked, so more may remain.
template <class AGE, class SIZE, class PROTECTED>
void SelectLeastRecentlyUsed( size_t numEntries,
                              const LruLimits & limits,
                              const AGE & ageOf,
                              const SIZE & sizeOf,
                              const PROTECTED & isProtected,
                              Array<size_t> & outIndicesToEvict )
{
    outIndicesToEvict.Clear();

    uint64_t remainingBytes = 0;
    for ( size_t i = 0; i < numEntries; ++i )
    {
        remainingBytes += sizeOf( i );
    }
    const auto isOverLimits = [ &limits ]( size_t remaining, uint64_t bytes )
    {
        return ( remaining > limits.m_MaxEntries ) ||
               ( ( bytes > limits.m_MaxBytes ) && ( remaining > limits.m_MinEntries ) );
    };
    if ( isOverLimits( numEntries, remainingBytes ) == false )
    {
        return;
    }

    Array<size_t> indicesOldestFirst;
    indicesOldestFirst.SetCapacity( numEntries );
    for ( size_t i = 0; i < numEntries; ++i )
    {
        indicesOldestFirst.Append( i );
    }
    indicesOldestFirst.Sort( [ &ageOf ]( size_t a, size_t b ) -> bool
    {
        return ( ageOf( a ) < ageOf( b ) );
    } );

    size_t remaining = numEntries;
    for ( const size_t idx : indicesOldestFirst )
    {
        if ( isOverLimits( remaining, remainingBytes ) == false )
        {
            break;
        }
        if ( isProtected( idx ) )
        {
            continue;
        }
        outIndicesToEvict.Append( idx );
        --remaining;
        remainingBytes -= sizeOf( idx );
    }
}

//------------------------------------------------------------------------------
