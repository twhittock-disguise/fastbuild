// TestFBuildOptions.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

#include "Tools/FBuild/FBuildCore/FBuildOptions.h"

// Core
#include "Core/Env/Env.h"
#include "Core/Strings/AStackString.h"

// TestFBuildOptions
//------------------------------------------------------------------------------
class TestFBuildOptions : public FBuildTest
{
private:
    DECLARE_TESTS

    void ParsePort() const;
    void WorkerPortEnvironmentVariable() const;
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestFBuildOptions )
    REGISTER_TEST( ParsePort )
    REGISTER_TEST( WorkerPortEnvironmentVariable )
REGISTER_TESTS_END

// ParsePort
//------------------------------------------------------------------------------
void TestFBuildOptions::ParsePort() const
{
    // Valid ports, including both ends of the range
    const struct
    {
        const char *    m_Text;
        uint16_t        m_Port;
    } valid[] = { { "1", 1 }, { "31264", 31264 }, { "31265", 31265 }, { "65535", 65535 } };
    for ( const auto & v : valid )
    {
        uint16_t port = 7;
        TEST_ASSERT( FBuildOptions::ParsePort( AStackString( v.m_Text ), port ) );
        TEST_ASSERT( port == v.m_Port );
    }

    // Invalid values are refused and leave the port untouched. 65536 and 4294967296
    // must not wrap around into a valid port.
    const char * invalid[] = { "", "0", "00", "65536", "65537", "4294967296", "-1", "+1", " 1", "1 ", "31264x", "x31264", "3.5", "0x10" };
    for ( const char * text : invalid )
    {
        uint16_t port = 7;
        TEST_ASSERT( FBuildOptions::ParsePort( AStackString( text ), port ) == false );
        TEST_ASSERT( port == 7 );
    }
}

// WorkerPortEnvironmentVariable
//------------------------------------------------------------------------------
void TestFBuildOptions::WorkerPortEnvironmentVariable() const
{
    AStackString<> original;
    const bool hadOriginal = Env::GetEnvVariable( "FASTBUILD_WORKER_PORT", original );
    char programName[] = "fbuild";
    char * argv[] = { programName };

    // The port is used when it is valid
    Env::SetEnvVariable( "FASTBUILD_WORKER_PORT", AStackString( "31265" ) );
    {
        FBuildOptions options;
        TEST_ASSERT( options.ProcessCommandLine( 1, argv ) == FBuildOptions::OPTIONS_OK );
        TEST_ASSERT( options.m_DistributionPort == 31265 );
    }

    // A mistyped port must not quietly send the build to the real workers
    Env::SetEnvVariable( "FASTBUILD_WORKER_PORT", AStackString( "31264x" ) );
    {
        FBuildOptions options;
        TEST_ASSERT( options.ProcessCommandLine( 1, argv ) == FBuildOptions::OPTIONS_ERROR );
    }

    // An empty variable is the same as none
    Env::SetEnvVariable( "FASTBUILD_WORKER_PORT", AStackString<>() );
    {
        FBuildOptions options;
        TEST_ASSERT( options.ProcessCommandLine( 1, argv ) == FBuildOptions::OPTIONS_OK );
        TEST_ASSERT( options.m_DistributionPort == Protocol::kPort );
    }

    if ( hadOriginal == false )
    {
        original.Clear();
    }
    Env::SetEnvVariable( "FASTBUILD_WORKER_PORT", original );
}

//------------------------------------------------------------------------------
