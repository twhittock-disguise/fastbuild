// DistributionStats.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "DistributionStats.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchTransform.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"

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

    // In MB, or in KB below 1 MB so small transfers do not show as 0.0 MB
    void AppendSize( AString & text, uint64_t bytes )
    {
        if ( bytes >= MEGABYTE )
        {
            text.AppendFormat( "%.1f MB", ToMB( bytes ) );
        }
        else
        {
            text.AppendFormat( "%.1f KB", ( (double)bytes / 1024.0 ) );
        }
    }

    // "<label> <n> msgs, <raw> raw, <sent> sent, handoff time <t>s[, encode time <t>s][, max wait <t>s], last sent at <t>s"
    void AppendBulkLine( AString & text, const char * label, const DistributionSendStats & s )
    {
        text.AppendFormat( "  %-10s %u msgs, ", label, s.m_Messages );
        AppendSize( text, s.m_RawBytes );
        text += " raw, ";
        AppendSize( text, s.m_SentBytes );
        text.AppendFormat( " sent, handoff time %.1fs", s.m_SendTime );
        if ( s.m_EncodeTime > 0.0 )
        {
            text.AppendFormat( ", encode time %.1fs", s.m_EncodeTime );
        }
        if ( s.m_MaxQueueDelay > 0.0 )
        {
            text.AppendFormat( ", max wait %.1fs", s.m_MaxQueueDelay );
        }
        text.AppendFormat( ", last sent at %.1fs\n", s.m_LastSentAt );
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
    if ( stats.m_NumSkippedToolchains > 0 )
    {
        outText.AppendFormat( "  toolchain: %u cached on the worker, not sent\n", stats.m_NumSkippedToolchains );
    }
    AppendBulkLine( outText, "pch:", stats.m_Pch );
    if ( stats.m_NumTransformedPchs > 0 )
    {
        outText.AppendFormat( "  pch-transform: %u sent after the transform (ptrxf v%u)\n", stats.m_NumTransformedPchs, (uint32_t)PchTransform::kPtrXfVersion );
    }
    AppendBulkLine( outText, "jobs:", stats.m_Jobs );
    outText.AppendFormat( "  results:   %u received, ", stats.m_ResultsReceived );
    AppendSize( outText, stats.m_ResultRawBytes );
    outText += " raw, ";
    AppendSize( outText, stats.m_ResultBytesReceived );
    outText += " received\n";
    if ( totalSent > Protocol::kClientSendBufferSize )
    {
        outText.AppendFormat( "  upload:    %.1f MB in %.1fs of handoff time = %.2f MB/s (%.1f Mbit/s)\n\n",
                              ToMB( totalSent ), totalSendTime, mbPerSecond, mbPerSecond * 8.0 );
    }
    else
    {
        // The socket buffer took it all, so the handoff time says nothing about the link
        outText.AppendFormat( "  upload:    %.1f MB in %.1fs of handoff time, too small to measure the link\n\n",
                              ToMB( totalSent ), totalSendTime );
    }
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

// GetLocalTimestamp
//------------------------------------------------------------------------------
/*static*/ void DistributionStats::GetLocalTimestamp( AString & outTimestamp )
{
    outTimestamp.Clear();
    const time_t now = time( nullptr );
    PRAGMA_DISABLE_PUSH_MSVC( 4996 ) // This function or variable may be unsafe...
    PRAGMA_DISABLE_PUSH_CLANG_WINDOWS( "-Wdeprecated-declarations" )
    const struct tm * localTime = localtime( &now );
    PRAGMA_DISABLE_POP_CLANG_WINDOWS
    PRAGMA_DISABLE_POP_MSVC
    if ( localTime )
    {
        char timestamp[ 32 ] = "";
        strftime( timestamp, sizeof( timestamp ), "%Y-%m-%d %H:%M:%S", localTime );
        outTimestamp = timestamp;
    }
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

    AStackString<> timestamp;
    GetLocalTimestamp( timestamp );

    AStackString<> text;
    Format( stats, timestamp.Get(), text );
    const uint64_t kMaxLogSize = ( 4 * MEGABYTE );
    (void)AppendToLog( path, text, kMaxLogSize );
}

//------------------------------------------------------------------------------
