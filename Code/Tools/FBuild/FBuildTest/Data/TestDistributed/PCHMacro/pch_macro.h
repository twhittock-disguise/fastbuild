#pragma once

// PCH header. Defines a function-like macro that, after PCH state is
// restored on a remote worker, would re-expand any literal `MYMACRO(...)`
// token-sequence that survived the coordinator's /E pass.
#define MYMACRO(x) #x
