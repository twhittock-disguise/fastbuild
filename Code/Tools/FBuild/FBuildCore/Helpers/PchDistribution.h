// PchDistribution.h - Helpers for distributing PCH files to remote workers
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Env/Types.h"
#include "Core/Strings/AString.h"

// Forward Declarations
//------------------------------------------------------------------------------
class Job;

// PchDistribution
//------------------------------------------------------------------------------
class PchDistribution
{
public:
    // Magic value at start of PCH-bundled job data: "FBPH"
    static const uint32_t MAGIC = 0x46425048;

    // Extract a value from an MSVC compiler option (e.g. /Fp"path" -> "path")
    static bool ExtractMSVCArgValue( const AString & options,
                                     const char * prefix,
                                     AString & outValue );

    // Find the PCH/source boundary in preprocessed output and return a pointer
    // to the start of the post-PCH content.
    static bool StripPCHSection( const char * data,
                                 size_t dataSize,
                                 const AString & sourceFileName,
                                 const char * & outStart,
                                 size_t & outSize );

    // On the coordinator: strip PCH content from preprocessed output, prepend
    // an #include trigger + undef block, and attach PCH metadata to the job.
    // pchCreationOptions/pchSourceFile are from the PCH creation node (has /Yc).
    static bool BundleForDistribution( Job * job,
                                       const AString & compilerOptions,
                                       const AString & sourceFileName,
                                       const AString & compilerExe,
                                       const char * compilerEnvironment,
                                       const AString & pchCreationOptions,
                                       const AString & pchSourceFile );

    // On the worker: detect a PCH-bundled payload and strip the header.
    // Returns true if PCH bundle was found; dataToWrite/Size are updated
    // to point at the preprocessed content (within the original buffer).
    static bool ExtractBundle( const void * & dataToWrite,
                               size_t & dataToWriteSize,
                               Job * job );

    // Check whether a data buffer starts with the PCH distribution magic.
    static bool ExtractHeader( const void * data,
                               size_t dataSize,
                               uint64_t & outPchId,
                               const void * & outPreprocessedData,
                               size_t & outPreprocessedSize );

    // Run the MSVC /PD macro dump pass and generate an #undef block for all
    // macros defined by the PCH. Uses disk cache keyed by pchId.
    static bool GetUndefBlock( const AString & compilerExe,
                               const char * compilerEnvironment,
                               const AString & pchCreationOptions,
                               const AString & pchSourceFile,
                               uint64_t pchId,
                               AString & outUndefBlock );

private:
    // Run cl.exe with the PCH creation command, but /Yc replaced by
    // /Zc:preprocessor /PD to dump macro definitions instead of creating a PCH.
    static bool RunMacroDumpPass( const AString & compilerExe,
                                  const char * compilerEnvironment,
                                  const AString & pchCreationOptions,
                                  const AString & pchSourceFile,
                                  uint64_t pchId,
                                  AString & outUndefBlock );

    // Parse /PD output lines and generate #undef block
    static void ParseMacroDumpOutput( const AString & pdOutput,
                                      AString & outUndefBlock );

    // Disk cache helpers
    static bool LoadUndefBlockFromDisk( uint64_t pchId, AString & outBlock );
    static void SaveUndefBlockToDisk( uint64_t pchId, const AString & block );
};

//------------------------------------------------------------------------------
