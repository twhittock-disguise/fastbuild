// GatewayConfig.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "GatewayConfig.h"

// Core
#include "Core/FileIO/FileStream.h"
#include "Core/Strings/AStackString.h"

// Helpers
//------------------------------------------------------------------------------
namespace
{
    bool IsConfigSpace( char c )
    {
        return ( c == ' ' ) || ( c == '\t' ) || ( c == '\r' );
    }

    // "1".."65535", digits only
    bool ParseConfigPort( const char * begin, const char * end, uint16_t & outPort )
    {
        if ( ( begin == end ) || ( ( end - begin ) > 5 ) )
        {
            return false;
        }
        uint32_t port = 0;
        for ( const char * c = begin; c < end; ++c )
        {
            if ( ( *c < '0' ) || ( *c > '9' ) )
            {
                return false;
            }
            port = ( port * 10 ) + (uint32_t)( *c - '0' );
        }
        if ( ( port == 0 ) || ( port > 0xFFFF ) )
        {
            return false;
        }
        outPort = (uint16_t)port;
        return true;
    }
}

// Parse
//------------------------------------------------------------------------------
/*static*/ bool GatewayConfig::Parse( const AString & text, GatewayConfig & outConfig, AString & outError )
{
    outConfig.m_Workers.Clear();

    uint32_t lineNumber = 0;
    const char * line = text.Get();
    const char * const textEnd = text.GetEnd();
    while ( line < textEnd )
    {
        ++lineNumber;
        const char * lineEnd = line;
        while ( ( lineEnd < textEnd ) && ( *lineEnd != '\n' ) )
        {
            ++lineEnd;
        }
        const char * const nextLine = ( lineEnd < textEnd ) ? ( lineEnd + 1 ) : textEnd;

        // The entry: up to a comment, without the spaces around it
        const char * end = line;
        while ( ( end < lineEnd ) && ( *end != ';' ) && ( *end != '#' ) )
        {
            ++end;
        }
        const char * begin = line;
        while ( ( begin < end ) && IsConfigSpace( *begin ) )
        {
            ++begin;
        }
        while ( ( end > begin ) && IsConfigSpace( *( end - 1 ) ) )
        {
            --end;
        }
        line = nextLine;
        if ( begin == end )
        {
            continue;
        }
        const AStackString<> entry( begin, end );

        Worker worker;
        const char * colon = entry.Find( ':' );
        worker.m_Host.Assign( entry.Get(), colon ? colon : entry.GetEnd() );
        if ( worker.m_Host.IsEmpty() )
        {
            outError.Format( "Worker list, line %u: '%s' has no host", lineNumber, entry.Get() );
            return false;
        }
        if ( colon && ( ParseConfigPort( colon + 1, entry.GetEnd(), worker.m_Port ) == false ) )
        {
            outError.Format( "Worker list, line %u: '%s' has a port that is not 1 to 65535", lineNumber, entry.Get() );
            return false;
        }
        for ( const Worker & other : outConfig.m_Workers )
        {
            if ( other.m_Host.EqualsI( worker.m_Host ) && ( other.m_Port == worker.m_Port ) )
            {
                outError.Format( "Worker list, line %u: '%s' is listed more than once", lineNumber, entry.Get() );
                return false;
            }
        }
        outConfig.m_Workers.Append( worker );
    }
    return true;
}

// Load
//------------------------------------------------------------------------------
/*static*/ bool GatewayConfig::Load( const AString & path, GatewayConfig & outConfig, AString & outError )
{
    FileStream f;
    if ( f.Open( path.Get(), FileStream::READ_ONLY ) == false )
    {
        outError.Format( "Cannot open the worker list '%s'", path.Get() );
        return false;
    }
    const uint64_t size = f.GetFileSize();
    AString text;
    text.SetLength( (uint32_t)size );
    if ( f.ReadBuffer( text.Get(), size ) != size )
    {
        outError.Format( "Cannot read the worker list '%s'", path.Get() );
        return false;
    }
    return Parse( text, outConfig, outError );
}

//------------------------------------------------------------------------------
