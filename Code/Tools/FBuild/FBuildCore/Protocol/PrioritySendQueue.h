// PrioritySendQueue.h - Messages waiting to be sent, with bulk data behind everything else
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Containers/Array.h"
#include "Core/Containers/Move.h"
#include "Core/Env/Assert.h"

// PrioritySendQueue
//------------------------------------------------------------------------------
// Bulk data goes out in pieces, one behind each batch of the other messages, so what is queued
// after a large transfer has started waits for one piece of it at most, and a steady stream of
// messages cannot hold the transfer up for good. ITEM::IsBulk() says which kind an item is.
// Not thread safe.
template <class ITEM>
class PrioritySendQueue
{
public:
    void Push( ITEM && item )
    {
        ( item.IsBulk() ? m_Bulk : m_Normal ).EmplaceBack( Move( item ) );
    }

    // Takes everything that is not bulk data, in the order it was pushed, then the next piece of
    // bulk data if there is any. Takes nothing if nothing is queued.
    void Take( Array<ITEM> & outItems )
    {
        ASSERT( outItems.IsEmpty() );
        m_Normal.Swap( outItems );
        if ( m_BulkNext < m_Bulk.GetSize() )
        {
            outItems.EmplaceBack( Move( m_Bulk[ m_BulkNext ] ) );
            ++m_BulkNext;
            if ( m_BulkNext == m_Bulk.GetSize() )
            {
                m_Bulk.Clear();
                m_BulkNext = 0;
            }
        }
    }

private:
    Array<ITEM> m_Normal;
    Array<ITEM> m_Bulk;
    size_t      m_BulkNext = 0; // The next piece of m_Bulk to send
};

//------------------------------------------------------------------------------
