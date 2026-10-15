// SendKind.h - What a message a client sends to a worker is for
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"

// SendKind
//------------------------------------------------------------------------------
enum class SendKind : uint8_t
{
    Control,
    Toolchain,
    Pch,
    Job,
    NumKinds
};

// Bulk data is sent in pieces behind everything else, so that jobs and control messages are not
// held up by it
inline bool IsBulkSendKind( SendKind kind )
{
    return ( kind == SendKind::Pch );
}

//------------------------------------------------------------------------------
