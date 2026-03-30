#include "pch_macro.h"

// Trivial code — the macro collision is in the PCH headers themselves.
// If the undef block is missing, the MYMACRO function declaration in
// pch_macro_pusher.h will be re-expanded by /Yu and cause a compile error.
int g_pchMacroConsumer = 1;
