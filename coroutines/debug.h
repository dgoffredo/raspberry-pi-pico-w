#pragma once

#include <stdio.h>

#ifdef NDEBUG
#define debug(...) do {} while (false)
#else
#define debug(...) do { printf(__VA_ARGS__); fflush(stdout); } while (false)
#endif
