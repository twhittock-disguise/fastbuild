// WorkerLog.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "WorkerLog.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Helpers/DistributionStats.h"

// Core
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Strings/AStackString.h"

#include <stdarg.h>

// CONSTRUCTOR
//------------------------------------------------------------------------------
WorkerLog::WorkerLog( const AString & directory, uint64_t maxFileSize )
    : m_MaxFileSize( maxFileSize )
{
    if ( directory.IsEmpty() )
    {
        return;
    }
    m_Path = directory;
    if ( ( m_Path.EndsWith( '/' ) == false ) && ( m_Path.EndsWith( NATIVE_SLASH ) == false ) )
    {
        m_Path += NATIVE_SLASH;
    }
    m_Path += kFileName;
    m_Enabled.Store( FileIO::EnsurePathExists( directory ) );
}

// Write
//------------------------------------------------------------------------------
void WorkerLog::Write( MSVC_SAL_PRINTF const char * fmtString, ... )
{
    if ( m_Enabled.Load() == false )
    {
        return;
    }

    AStackString<> timestamp;
    DistributionStats::GetLocalTimestamp( timestamp );

    AStackString<> line;
    line.Format( "[%s] ", timestamp.Get() );
    va_list args;
    va_start( args, fmtString );
    AStackString<> message;
    message.VFormat( fmtString, args );
    va_end( args );
    line += message;
    line += '\n';

    if ( DistributionStats::AppendToLog( m_Path, line, m_MaxFileSize ) == false )
    {
        m_Enabled.Store( false );
        if ( m_NumFailures.Increment() == 1 )
        {
            FLOG_WARN( "Worker log '%s' cannot be written and is switched off", m_Path.Get() );
        }
    }
}

//------------------------------------------------------------------------------
