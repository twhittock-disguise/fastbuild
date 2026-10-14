// DistributionStats.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "DistributionStats.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FBuild.h"

// Core
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/Process/Mutex.h"
#include "Core/Strings/AStackString.h"

#include <time.h>

// Static
//------------------------------------------------------------------------------
namespace
{
    double ToMB( uint64_t bytes ) { return ( (double)bytes / (double)MEGABYTE ); }

    // "<label> <n> msgs, <raw> MB raw, <sent> MB sent, send time <t>s, done at <t>s"
    void AppendBulkLine( AString & text, const char * label, const DistributionSendStats & s )
    {
        text.AppendFormat( "  %-10s %u msgs, %.1f MB raw, %.1f MB sent, send time %.1fs, done at %.1fs\n",
                           label, s.m_Messages, ToMB( s.m_RawBytes ), ToMB( s.m_SentBytes ),
                           s.m_SendTime, s.m_LastSentAt );
    }
}

// Format
//------------------------------------------------------------------------------
/*static*/ void DistributionStats::Format( const DistributionConnectionStats & stats,
                                           const char * timestamp,
                                           AString & outText )
{
    const uint64_t totalSent = stats.m_Toolchain.m_SentBytes + stats.m_Pch.m_SentBytes + stats.m_Jobs.m_SentBytes;
    const double totalSendTime = stats.m_Toolchain.m_SendTime + stats.m_Pch.m_SendTime + stats.m_Jobs.m_SendTime;
    const double mbPerSecond = ( totalSendTime > 0.0 ) ? ( ToMB( totalSent ) / totalSendTime ) : 0.0;

    outText.AppendFormat( "[%s] worker %s, connected %.1fs\n",
                          timestamp, stats.m_WorkerAddress.Get(), stats.m_ConnectedTime );
    AppendBulkLine( outText, "toolchain:", stats.m_Toolchain );
    for ( const uint64_t toolId : stats.m_SkippedToolchains )
    {
        outText.AppendFormat( "  toolchain: 0x%016" PRIx64 " skipped (cached on worker)\n", toolId );
    }
    AppendBulkLine( outText, "pch:", stats.m_Pch );
    outText.AppendFormat( "  jobs:      %u sent, %.1f MB, send time %.1fs\n",
                          stats.m_Jobs.m_Messages, ToMB( stats.m_Jobs.m_SentBytes ), stats.m_Jobs.m_SendTime );
    outText.AppendFormat( "  results:   %u received, %.1f MB\n",
                          stats.m_ResultsReceived, ToMB( stats.m_ResultBytesReceived ) );
    outText.AppendFormat( "  upload:    %.1f MB in %.1fs of send time = %.2f MB/s (%.1f Mbit/s)\n\n",
                          ToMB( totalSent ), totalSendTime, mbPerSecond, mbPerSecond * 8.0 );
}

// AppendToLog
//------------------------------------------------------------------------------
/*static*/ bool DistributionStats::AppendToLog( const AString & path,
                                                 const AString & text,
                                                 uint64_t maxLogSize )
{
    // Connections close on different threads
    static Mutex s_Mutex;
    MutexHolder mh( s_Mutex );

    uint64_t size = 0;
    {
        FileStream f;
        if ( f.Open( path.Get(), FileStream::READ_ONLY ) )
        {
            size = f.GetFileSize();
        }
    }
    if ( ( size > 0 ) && ( ( size + text.GetLength() ) > maxLogSize ) )
    {
        AStackString<> oldPath( path );
        oldPath += ".old";
        if ( FileIO::FileMove( path, oldPath ) == false )
        {
            return false;
        }
        size = 0;
    }

    FileStream f;
    if ( f.Open( path.Get(), FileStream::OPEN_OR_CREATE_READ_WRITE ) == false )
    {
        return false;
    }
    return f.Seek( size ) && f.WriteFromString( text );
}

// WriteToTempLog
//------------------------------------------------------------------------------
/*static*/ void DistributionStats::WriteToTempLog( const DistributionConnectionStats & stats )
{
    AStackString<> path;
    if ( FBuild::GetTempDir( path ) == false )
    {
        return;
    }
    path += "FBuild-distribution-stats.log";

    const time_t now = time( nullptr );
    char timestamp[ 32 ] = "";
    PRAGMA_DISABLE_PUSH_MSVC( 4996 ) // This function or variable may be unsafe...
    PRAGMA_DISABLE_PUSH_CLANG_WINDOWS( "-Wdeprecated-declarations" )
    const struct tm * localTime = localtime( &now );
    PRAGMA_DISABLE_POP_CLANG_WINDOWS
    PRAGMA_DISABLE_POP_MSVC
    if ( localTime )
    {
        strftime( timestamp, sizeof( timestamp ), "%Y-%m-%d %H:%M:%S", localTime );
    }

    AStackString<> text;
    Format( stats, timestamp, text );
    const uint64_t kMaxLogSize = ( 4 * MEGABYTE );
    (void)AppendToLog( path, text, kMaxLogSize );
}

//------------------------------------------------------------------------------
