// PchDistribution.cpp - Helpers for distributing PCH files to remote workers
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "PchDistribution.h"
#include "PchDataCache.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Graph/ObjectNode.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/Job.h"

// Core
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Math/xxHash.h"
#include "Core/Mem/Mem.h"
#include "Core/Process/Process.h"
#include "Core/Strings/AStackString.h"

#include <string.h>

// ExtractMSVCArgValue
//------------------------------------------------------------------------------
/*static*/ bool PchDistribution::ExtractMSVCArgValue( const AString & options,
                                                      const char * prefix,
                                                      AString & outValue )
{
    StackArray<AString> tokens;
    options.Tokenize( tokens );
    for ( const AString & token : tokens )
    {
        if ( ObjectNode::IsStartOfCompilerArg_MSVC( token, prefix ) )
        {
            const size_t prefixLen = AString::StrLen( prefix );
            const char * start = token.Get() + 1 + prefixLen; // skip / or - plus prefix
            const char * end = token.GetEnd();

            // Strip quotes if present
            if ( ( start < end ) && ( *start == '"' ) )
            {
                ++start;
            }
            if ( ( start < end ) && ( end[ -1 ] == '"' ) )
            {
                --end;
            }
            outValue.Assign( start, end );
            return true;
        }
    }
    return false;
}

// StripPCHSection
//------------------------------------------------------------------------------
/*static*/ bool PchDistribution::StripPCHSection( const char * data,
                                                   size_t dataSize,
                                                   const AString & sourceFileName,
                                                   const char * & outStart,
                                                   size_t & outSize )
{
    // Extract basename from source file path
    const char * baseName = sourceFileName.FindLast( NATIVE_SLASH );
    if ( baseName == nullptr )
    {
        baseName = sourceFileName.FindLast( '/' );
    }
    baseName = baseName ? ( baseName + 1 ) : sourceFileName.Get();

    const char * pos = data;
    const char * end = data + dataSize;
    uint32_t matchCount = 0;

    // Scan line by line for #line N "...basename"
    while ( pos < end )
    {
        // Find end of line
        const char * lineEnd = pos;
        while ( lineEnd < end && *lineEnd != '\n' )
        {
            ++lineEnd;
        }

        // Check for #line directive
        const char * p = pos;

        // Skip whitespace
        while ( p < lineEnd && ( *p == ' ' || *p == '\t' ) )
        {
            ++p;
        }

        // Match "#line "
        if ( ( lineEnd - p > 6 ) && ( memcmp( p, "#line ", 6 ) == 0 ) )
        {
            p += 6;

            // Parse line number
            uint32_t lineNum = 0;
            while ( p < lineEnd && *p >= '0' && *p <= '9' )
            {
                lineNum = lineNum * 10 + ( *p - '0' );
                ++p;
            }

            // Skip whitespace
            while ( p < lineEnd && ( *p == ' ' || *p == '\t' ) )
            {
                ++p;
            }

            // Check for quoted filename containing our basename
            if ( p < lineEnd && *p == '"' )
            {
                ++p;
                const char * fnameStart = p;
                while ( p < lineEnd && *p != '"' )
                {
                    ++p;
                }
                const char * fnameEnd = p;

                // Check if the filename ends with our basename
                const size_t fnameLen = (size_t)( fnameEnd - fnameStart );
                const size_t baseLen = AString::StrLen( baseName );
                if ( fnameLen >= baseLen )
                {
                    // Case-insensitive compare of the basename portion
                    const char * fnameBasePart = fnameEnd - baseLen;
                    bool matches = true;
                    for ( size_t i = 0; i < baseLen; ++i )
                    {
                        char a = fnameBasePart[ i ];
                        char b = baseName[ i ];
                        if ( a >= 'A' && a <= 'Z' ) { a += 32; }
                        if ( b >= 'A' && b <= 'Z' ) { b += 32; }
                        if ( a == '\\' ) { a = '/'; }
                        if ( b == '\\' ) { b = '/'; }
                        if ( a != b ) { matches = false; break; }
                    }

                    // Verify it's at a path boundary
                    if ( matches && ( fnameBasePart == fnameStart ||
                                      fnameBasePart[ -1 ] == '\\' ||
                                      fnameBasePart[ -1 ] == '/' ) )
                    {
                        matchCount++;

                        FLOG_VERBOSE( "PCH strip: match #%u at #line %u (offset %zu/%zu)\n",
                                      matchCount, lineNum,
                                      (size_t)( pos - data ), dataSize );

                        // For explicit #include mode: first match with lineNum >= 2
                        // For /FI mode: second match (first is #line 1 before /FI expansion)
                        if ( lineNum >= 2 || matchCount >= 2 )
                        {
                            outStart = pos;
                            outSize = (size_t)( end - pos );
                            FLOG_VERBOSE( "PCH strip: boundary at offset %zu, kept %.1f KB\n",
                                          (size_t)( pos - data ), (float)outSize / 1024.0f );
                            return true;
                        }
                    }
                }
            }
        }

        pos = ( lineEnd < end ) ? ( lineEnd + 1 ) : lineEnd;
    }

    return false;
}

// BundleForDistribution
//------------------------------------------------------------------------------
/*static*/ bool PchDistribution::BundleForDistribution( Job * job,
                                                        const AString & compilerOptions,
                                                        const AString & sourceFileName )
{
    // Extract /Fp and /Yu from compiler options
    AStackString pchFilePath;
    AStackString stopHeaderName;
    if ( ExtractMSVCArgValue( compilerOptions, "Fp", pchFilePath ) == false )
    {
        return true; // No /Fp — skip PCH distribution
    }
    if ( ExtractMSVCArgValue( compilerOptions, "Yu", stopHeaderName ) == false )
    {
        return true; // No /Yu — skip
    }

    // Resolve relative .pch path to absolute
    if ( PathUtils::IsFullPath( pchFilePath ) == false )
    {
        AStackString workingDir;
        VERIFY( FileIO::GetCurrentDir( workingDir ) );
        PathUtils::EnsureTrailingSlash( workingDir );
        AStackString fullPath;
        fullPath = workingDir;
        fullPath += pchFilePath;
        pchFilePath = fullPath;
    }

    // Get PCH metadata (cached in-memory or read from disk)
    uint64_t pchId;
    uint32_t pchFileSize;
    {
        PchDataCache::Entry cached;
        if ( PchDataCache::Get().FindByPath( pchFilePath, cached ) )
        {
            pchId = cached.pchId;
            pchFileSize = cached.uncompressedSize;
        }
        else
        {
            FileStream pchFile;
            if ( pchFile.Open( pchFilePath.Get(), FileStream::READ_ONLY ) == false )
            {
                FLOG_ERROR( "PCH distribution: cannot open '%s'\n", pchFilePath.Get() );
                return false;
            }
            pchFileSize = (uint32_t)pchFile.GetFileSize();
            UniquePtr<char, FreeDeletor> pchData( (char *)ALLOC( pchFileSize ) );
            if ( pchFile.Read( pchData.Get(), pchFileSize ) != pchFileSize )
            {
                FLOG_ERROR( "PCH distribution: error reading '%s'\n", pchFilePath.Get() );
                return false;
            }
            pchFile.Close();

            pchId = xxHash::Calc64( pchData.Get(), pchFileSize );

            PchDataCache::Entry entry;
            entry.pchId = pchId;
            entry.uncompressedSize = pchFileSize;
            entry.filePath = pchFilePath;
            PchDataCache::Get().Store( entry );
        }
    }

    // Read the .undefs file generated by the PCH creation node
    AStackString undefsPath( pchFilePath );
    undefsPath += ".undefs";
    AString undefBlock;
    {
        FileStream f;
        if ( f.Open( undefsPath.Get(), FileStream::READ_ONLY ) == false )
        {
            FLOG_ERROR( "PCH distribution: cannot open '%s' — was the PCH built with distribution support?\n",
                        undefsPath.Get() );
            return false;
        }
        const uint32_t fileSize = (uint32_t)f.GetFileSize();
        undefBlock.SetLength( fileSize );
        if ( f.Read( undefBlock.Get(), fileSize ) != fileSize )
        {
            FLOG_ERROR( "PCH distribution: error reading '%s'\n", undefsPath.Get() );
            return false;
        }
    }

    // Strip the PCH section from the preprocessed output
    const char * jobData = (const char *)job->GetData();
    const size_t jobDataSize = job->GetDataSize();

    const char * strippedStart = nullptr;
    size_t strippedSize = 0;
    if ( StripPCHSection( jobData, jobDataSize, sourceFileName, strippedStart, strippedSize ) == false )
    {
        FLOG_ERROR( "PCH distribution: cannot find PCH boundary in preprocessed output for '%s'\n",
                    job->GetNode()->GetName().Get() );
        return false;
    }

    // Build payload: [magic][pchId][#include "header"\n][undef block][stripped content]
    AStackString includePrefix;
    includePrefix.Format( "#include \"%s\"\n", stopHeaderName.Get() );

    const size_t headerSize = sizeof( uint32_t ) + sizeof( uint64_t );
    const size_t newSize = headerSize + includePrefix.GetLength() + undefBlock.GetLength() + strippedSize;
    char * newData = (char *)ALLOC( newSize + 1 );

    char * dest = newData;
    memcpy( dest, &MAGIC, sizeof( uint32_t ) );
    dest += sizeof( uint32_t );
    memcpy( dest, &pchId, sizeof( uint64_t ) );
    dest += sizeof( uint64_t );
    memcpy( dest, includePrefix.Get(), includePrefix.GetLength() );
    dest += includePrefix.GetLength();
    memcpy( dest, undefBlock.Get(), undefBlock.GetLength() );
    dest += undefBlock.GetLength();
    memcpy( dest, strippedStart, strippedSize );
    dest += strippedSize;
    *dest = '\0';

    FLOG_VERBOSE( "PCH distribution: '%s' stripped %.1f KB -> %.1f KB (%.1f%% reduction, undef %.1f KB)\n",
                  job->GetNode()->GetName().Get(),
                  (float)jobDataSize / 1024.0f,
                  (float)( newSize - headerSize ) / 1024.0f,
                  100.0f * ( 1.0f - (float)( newSize - headerSize ) / (float)jobDataSize ),
                  (float)undefBlock.GetLength() / 1024.0f );

    job->OwnData( newData, newSize );
    job->SetPchId( pchId );
    return true;
}

// ExtractHeader
//------------------------------------------------------------------------------
/*static*/ bool PchDistribution::ExtractHeader( const void * data,
                                                size_t dataSize,
                                                uint64_t & outPchId,
                                                const void * & outPreprocessedData,
                                                size_t & outPreprocessedSize )
{
    const size_t headerSize = sizeof( uint32_t ) + sizeof( uint64_t );
    if ( dataSize < headerSize )
    {
        return false;
    }

    uint32_t magic;
    memcpy( &magic, data, sizeof( uint32_t ) );
    if ( magic != MAGIC )
    {
        return false;
    }

    memcpy( &outPchId, (const char *)data + sizeof( uint32_t ), sizeof( uint64_t ) );
    outPreprocessedData = (const char *)data + headerSize;
    outPreprocessedSize = dataSize - headerSize;
    return true;
}

// ExtractBundle
//------------------------------------------------------------------------------
/*static*/ bool PchDistribution::ExtractBundle( const void * & dataToWrite,
                                                size_t & dataToWriteSize,
                                                Job * job )
{
    uint64_t pchId = 0;
    const void * preprocessedData = nullptr;
    size_t preprocessedSize = 0;
    if ( ExtractHeader( dataToWrite, dataToWriteSize, pchId, preprocessedData, preprocessedSize ) == false )
    {
        return false;
    }

    // Update data pointers to skip the PCH header
    dataToWrite = preprocessedData;
    dataToWriteSize = preprocessedSize;

    const AString & pchCachePath = job->GetPchCachePath();
    FLOG_VERBOSE( "PCH extract: pchId=0x%016" PRIx64 " cachePath='%s' ppSize=%zu\n",
                  pchId, pchCachePath.Get(), preprocessedSize );
    if ( pchCachePath.IsEmpty() )
    {
        FLOG_WARN( "PCH extract: header present but no cache path for pchId 0x%016" PRIx64 "\n", pchId );
    }

    return true;
}

// GenerateUndefsFile
//------------------------------------------------------------------------------
/*static*/ bool PchDistribution::GenerateUndefsFile( const AString & compilerExe,
                                                     const char * compilerEnvironment,
                                                     const AString & pchCreationOptions,
                                                     const AString & pchSourceFile,
                                                     const AString & outputPath )
{
    // Take the exact PCH creation command, replace /Yc with /Zc:preprocessor /PD,
    // and replace /c with /P (preprocess to .i file instead of compiling).
    // The /PD #define lines are interleaved in the .i file output.
    StackArray<AString> tokens;
    pchCreationOptions.Tokenize( tokens );

    AStackString args;

    for ( size_t i = 0; i < tokens.GetSize(); ++i )
    {
        const AString & token = tokens[ i ];

        // Replace /Yc with /Zc:preprocessor /PD
        if ( ObjectNode::IsStartOfCompilerArg_MSVC( token, "Yc" ) )
        {
            args += " /Zc:preprocessor /PD";
            continue;
        }

        // Replace /c with /P (preprocess to file instead of compile)
        if ( ObjectNode::IsCompilerArg_MSVC( token, "c" ) )
        {
            args += " /P";
            continue;
        }

        // Strip /Fp and /Fo (not needed for macro dump)
        if ( ObjectNode::IsStartOfCompilerArg_MSVC( token, "Fp" ) ||
             ObjectNode::IsStartOfCompilerArg_MSVC( token, "Fo" ) )
        {
            continue;
        }

        // Skip tokens containing FASTBuild substitution variables (%1-%9)
        {
            bool hasSubstitution = false;
            for ( char c = '1'; c <= '9'; ++c )
            {
                char pattern[ 3 ] = { '%', c, '\0' };
                if ( token.Find( pattern ) )
                {
                    hasSubstitution = true;
                    break;
                }
            }
            if ( hasSubstitution ) { continue; }
        }

        args += ' ';
        args += token;
    }

    // Direct the .i output next to the final .undefs file (same directory)
    // Use .i extension for the temp preprocessed output
    AStackString iFile( outputPath );
    iFile.SetLength( iFile.GetLength() - 7 ); // strip ".undefs"
    iFile += ".pd.i";
    args.AppendFormat( " /Fi\"%s\"", iFile.Get() );

    // Append the original PCH source file
    args += " \"";
    args += pchSourceFile;
    args += '"';

    // Run from the same working directory as the build
    AStackString workingDir;
    VERIFY( FileIO::GetCurrentDir( workingDir ) );

    Process p;
    if ( p.Spawn( compilerExe.Get(), args.Get(), workingDir.Get(), compilerEnvironment ) == false )
    {
        FLOG_ERROR( "PCH /PD pass: failed to spawn compiler\n" );
        return false;
    }

    AString stdOut;
    AString stdErr;
    p.ReadAllData( stdOut, stdErr );
    p.WaitForExit();

    // Read the .i file
    FileStream f;
    if ( f.Open( iFile.Get(), FileStream::READ_ONLY ) == false )
    {
        FLOG_ERROR( "PCH /PD pass: cannot open .i file '%s'\nstderr:\n%s\n",
                    iFile.Get(), stdErr.Get() );
        return false;
    }
    const size_t fileSize = (size_t)f.GetFileSize();
    AString iContent;
    iContent.SetLength( (uint32_t)fileSize );
    if ( f.Read( iContent.Get(), fileSize ) != fileSize )
    {
        FLOG_ERROR( "PCH /PD pass: failed to read .i file '%s'\n", iFile.Get() );
        return false;
    }
    f.Close();

    // Clean up temp .i file
    FileIO::FileDelete( iFile.Get() );

    if ( iContent.Find( "#define " ) == nullptr )
    {
        FLOG_ERROR( "PCH /PD pass: no macro definitions found\nstderr:\n%s\n", stdErr.Get() );
        return false;
    }

    // Parse and write the .undefs file
    AString undefBlock;
    ParseMacroDumpOutput( iContent, undefBlock );

    FileStream out;
    if ( out.Open( outputPath.Get(), FileStream::WRITE_ONLY ) == false )
    {
        FLOG_ERROR( "PCH /PD pass: cannot write '%s'\n", outputPath.Get() );
        return false;
    }
    if ( out.Write( undefBlock.Get(), undefBlock.GetLength() ) != undefBlock.GetLength() )
    {
        FLOG_ERROR( "PCH /PD pass: error writing '%s'\n", outputPath.Get() );
        return false;
    }

    FLOG_VERBOSE( "PCH undefs: generated '%s' (%u bytes)\n",
                  outputPath.Get(), undefBlock.GetLength() );
    return true;
}

// ParseMacroDumpOutput
//------------------------------------------------------------------------------
/*static*/ void PchDistribution::ParseMacroDumpOutput( const AString & pdOutput,
                                                       AString & outUndefBlock )
{
    // /PD output has lines like:
    //   #define MACRO_NAME value
    //   #define FUNC_MACRO(x,y) body
    // We extract just the macro name and emit #undef for each.

    // Pre-size: rough estimate
    outUndefBlock.Clear();
    outUndefBlock.SetReserved( pdOutput.GetLength() / 4 );

    const char * pos = pdOutput.Get();
    const char * end = pdOutput.GetEnd();

    while ( pos < end )
    {
        // Find end of line
        const char * lineEnd = pos;
        while ( lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r' )
        {
            ++lineEnd;
        }

        // Check for "#define "
        if ( ( lineEnd - pos > 8 ) && ( memcmp( pos, "#define ", 8 ) == 0 ) )
        {
            const char * nameStart = pos + 8;
            const char * nameEnd = nameStart;
            while ( nameEnd < lineEnd && *nameEnd != ' ' && *nameEnd != '(' && *nameEnd != '\t' )
            {
                ++nameEnd;
            }

            if ( nameEnd > nameStart )
            {
                // Skip compiler built-in/reserved macros: names starting with
                // __ or _[A-Z] are reserved by the C++ standard and cause
                // warnings (C4117, C5308) when #undef'd.
                const bool isReserved = ( nameStart[ 0 ] == '_' ) &&
                                        ( nameEnd - nameStart >= 2 ) &&
                                        ( nameStart[ 1 ] == '_' || ( nameStart[ 1 ] >= 'A' && nameStart[ 1 ] <= 'Z' ) );
                if ( !isReserved )
                {
                    outUndefBlock += "#undef ";
                    outUndefBlock.Append( nameStart, (size_t)( nameEnd - nameStart ) );
                    outUndefBlock += '\n';
                }
            }
        }

        // Skip line ending
        pos = lineEnd;
        while ( pos < end && ( *pos == '\n' || *pos == '\r' ) )
        {
            ++pos;
        }
    }
}

//------------------------------------------------------------------------------
