#pragma once

// Non-PCH header included by the consumer. Uses the push_macro / undef /
// pop_macro pattern to disable MYMACRO temporarily so the same identifier
// can name a function. The push/undef/pop pragmas are consumed by the
// preprocessor and lost from /E output, so on a remote worker the surviving
// `inline int MYMACRO(int x)` and `MYMACRO(2)` tokens would re-expand once
// /Yu restores PCH state — unless the bundled payload prepends `#undef
// MYMACRO` from the .undefs block.
#pragma push_macro("MYMACRO")
#undef MYMACRO

inline int MYMACRO(int x) { return x + 1; }
inline int CallMYMACRO() { return MYMACRO(2); }

#pragma pop_macro("MYMACRO")
