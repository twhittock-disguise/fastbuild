// PchDataCache.cpp - Coordinator-side metadata cache for PCH distribution
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "PchDataCache.h"

// Core
#include "Core/Process/Mutex.h"

// Static
//------------------------------------------------------------------------------
static PchDataCache s_PchDataCache;

// Get
//------------------------------------------------------------------------------
/*static*/ PchDataCache & PchDataCache::Get()
{
    return s_PchDataCache;
}

// Store
//------------------------------------------------------------------------------
void PchDataCache::Store( const Entry & entry )
{
    MutexHolder mh( m_Mutex );

    // Skip if already stored
    for ( const Entry & e : m_Entries )
    {
        if ( e.pchId == entry.pchId )
        {
            return;
        }
    }

    m_Entries.Append( entry );
}

// Find
//------------------------------------------------------------------------------
bool PchDataCache::Find( uint64_t pchId, Entry & outEntry ) const
{
    MutexHolder mh( m_Mutex );

    for ( const Entry & e : m_Entries )
    {
        if ( e.pchId == pchId )
        {
            outEntry = e;
            return true;
        }
    }
    return false;
}

// FindByPath
//------------------------------------------------------------------------------
bool PchDataCache::FindByPath( const AString & filePath, Entry & outEntry ) const
{
    MutexHolder mh( m_Mutex );

    for ( const Entry & e : m_Entries )
    {
        if ( e.filePath == filePath )
        {
            outEntry = e;
            return true;
        }
    }
    return false;
}

// SetUndefBlock
//------------------------------------------------------------------------------
void PchDataCache::SetUndefBlock( uint64_t pchId, const AString & undefBlock )
{
    MutexHolder mh( m_Mutex );

    for ( Entry & e : m_Entries )
    {
        if ( e.pchId == pchId )
        {
            e.undefBlock = undefBlock;
            return;
        }
    }
}

// Clear
//------------------------------------------------------------------------------
void PchDataCache::Clear()
{
    MutexHolder mh( m_Mutex );
    m_Entries.Clear();
}

//------------------------------------------------------------------------------
