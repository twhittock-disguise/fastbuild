#include "pch_macro.h"          // PCH trigger
#include "pch_macro_pusher.h"   // non-PCH; survives /E with literal MYMACRO tokens

int g_pchMacroConsumer = CallMYMACRO();
