#pragma once

#include <SDL3/SDL.h>

namespace sagepatch {

void init();
void shutdown();

bool handleKeyDown(const SDL_KeyboardEvent& ev);

#ifdef __APPLE__
// Takes Cmd+Q off the Quit item of the app menu (QuitKey_macos.cpp).
void removeQuitKey();
#endif

}
