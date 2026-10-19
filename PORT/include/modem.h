#pragma once

// Greenleaf Communications Library is intentionally not part of the modern
// Windows build. The serial/modem transport is retired; current networking is
// handled by the Winsock path. This compatibility header exists only so legacy
// umbrella headers can still be parsed while modem-only translation units are
// excluded from the executable.
