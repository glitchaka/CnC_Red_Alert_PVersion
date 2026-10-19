#include <SDL3/SDL.h>
#include <AL/al.h>
#include <AL/alc.h>

// This translation unit intentionally stays small. It proves that the Win11
// build is wired against the modern platform libraries while the old engine
// entry points are migrated behind compatibility shims.
extern "C" int RA_ModernPlatform_LinkAnchor()
{
    // Compile-time SDL version plus an OpenAL symbol reference forces both
    // dependencies to be present in the final link without changing gameplay.
    (void)alGetError();
    return SDL_MAJOR_VERSION;
}
