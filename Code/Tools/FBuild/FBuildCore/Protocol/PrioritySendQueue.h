// PrioritySendQueue.h - Messages waiting to be sent, with bulk data behind everything else
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Array.h"
#include "Core/Containers/Move.h"

// PrioritySendQueue
//------------------------------------------------------------------------------
// Bulk data goes out in pieces. A piece is only sent when nothing else is waiting, so what
// is queued after a large transfer has started waits for one piece of it at most.
// Not thread safe.
template <class ITEM>
class PrioritySendQueue
{
public:
    void Push( ITEM && item, bool isBulk )
    {
        ( isBulk ? m_Bulk : m_Normal ).EmplaceBack( Move( item ) );
    }

    // Takes everything that is not bulk data, or if there is none the next piece of bulk data,
    // in the order they were pushed. Returns false if nothing is queued.
    bool Take( Array<ITEM> & outItems )
    {
        ASSERT( outItems.IsEmpty() );
        if ( m_Normal.IsEmpty() == false )
        {
            m_Normal.Swap( outItems );
            return true;
        }
        if ( m_Bulk.IsEmpty() == false )
        {
            outItems.EmplaceBack( Move( m_Bulk[ 0 ] ) );
            m_Bulk.EraseIndex( 0 );
            return true;
        }
        return false;
    }

private:
    Array<ITEM> m_Normal;
    Array<ITEM> m_Bulk;
};

//------------------------------------------------------------------------------
