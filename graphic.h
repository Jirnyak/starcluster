// graphic.h
#pragma once
#include <SDL.h>
#include "galaxy.h"
#include "civ.h"

namespace Graphic {
    void render(SDL_Renderer* renderer, const Galaxy& galaxy, const CivManager& civs);
}
