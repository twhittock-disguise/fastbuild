// A large, very compressible object file, to check that results are returned compressed
#define REPEAT4( x ) x x x x
#define PATTERN "\1\2\3\4\5\6\7\10"

extern const char g_ResultCompressionTable[];
const char g_ResultCompressionTable[] = REPEAT4( REPEAT4( REPEAT4( REPEAT4( REPEAT4( REPEAT4( PATTERN ) ) ) ) ) );
