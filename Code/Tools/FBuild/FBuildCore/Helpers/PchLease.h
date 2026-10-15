// PchLease.h - A job's hold on a PCH
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"

// PchLease
//------------------------------------------------------------------------------
// While a job that needs a PCH exists, it holds a lease on it: the PCH is not evicted, and a
// working copy of it that is unpacked stays until the job is gone. Whoever hands out leases must
// outlive them.
class PchLease
{
public:
    class Owner
    {
    public:
        virtual void ReleasePchLease( uint64_t pchId ) = 0;

    protected:
        ~Owner() = default;
    };

    PchLease() = default;
    PchLease( Owner * owner, uint64_t pchId )
        : m_Owner( owner )
        , m_PchId( pchId )
    {
    }
    PchLease( PchLease && other )
        : m_Owner( other.m_Owner )
        , m_PchId( other.m_PchId )
    {
        other.m_Owner = nullptr;
    }
    PchLease & operator=( PchLease && other )
    {
        if ( this != &other )
        {
            Release();
            m_Owner = other.m_Owner;
            m_PchId = other.m_PchId;
            other.m_Owner = nullptr;
        }
        return *this;
    }
    ~PchLease() { Release(); }

    // Non-copyable
    explicit PchLease( const PchLease & other ) = delete;
    PchLease & operator=( const PchLease & other ) = delete;

    bool IsHeld() const { return ( m_Owner != nullptr ); }

    void Release()
    {
        if ( m_Owner )
        {
            Owner * owner = m_Owner;
            m_Owner = nullptr;
            owner->ReleasePchLease( m_PchId );
        }
    }

private:
    Owner *     m_Owner = nullptr;
    uint64_t    m_PchId = 0;
};

//------------------------------------------------------------------------------
