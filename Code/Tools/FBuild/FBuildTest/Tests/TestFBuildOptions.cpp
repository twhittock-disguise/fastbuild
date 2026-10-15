// TestFBuildOptions.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

#include "Tools/FBuild/FBuildCore/FBuildOptions.h"

// Core
#include "Core/Strings/AStackString.h"

// TestFBuildOptions
//------------------------------------------------------------------------------
class TestFBuildOptions : public FBuildTest
{
private:
    DECLARE_TESTS

    void ParsePort() const;
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestFBuildOptions )
    REGISTER_TEST( ParsePort )
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

//------------------------------------------------------------------------------
