#pragma once

#ifdef NDEBUG
inline
void debug(...) {}
#else
#include <stdio.h>
#define debug(...) do { printf(__VA_ARGS__); fflush(stdout); } while (false)
#endif
