#pragma once

// MSVC no longer ships the old DOS-era <mem.h> compatibility header used by
// the original Red Alert sources. <memory.h> provides the C memory routines
// that code expects (memcpy, memmove, memset, memcmp, ...).
#include <memory.h>
