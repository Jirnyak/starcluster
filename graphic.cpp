// graphic.cpp
#include "graphic.h"

namespace Graphic {
    void render(SDL_Renderer* renderer, const Galaxy& galaxy, const CivManager& civs) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        for (const auto& s : galaxy.stars) {
            SDL_Color col = {255, 255, 255, 255};
            if (s.civ_id > 0)
                col = civs.civs[s.civ_id - 1].color;

            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 255);
            SDL_FRect r = {400 + s.x * 200, 300 + s.y * 200, 2, 2};
            SDL_RenderFillRectF(renderer, &r);
        }

        SDL_RenderPresent(renderer);
    }
}
  