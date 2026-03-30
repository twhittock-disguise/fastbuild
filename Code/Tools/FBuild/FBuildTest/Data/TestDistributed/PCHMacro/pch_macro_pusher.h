#pragma once

// Temporarily disable MYMACRO so it can be used as a function name.
// The #pragma push/undef/pop sequence is consumed by the preprocessor
// and lost from /E output. When /Yu restores PCH state, MYMACRO would
// re-expand this function name without the #undef block fix.
#pragma push_macro("MYMACRO")
#undef MYMACRO
inline int MYMACRO(int x) { return x + 1; }
#pragma pop_macro("MYMACRO")
