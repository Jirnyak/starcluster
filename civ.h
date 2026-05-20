#pragma once
#include <vector>
#include <SDL.h>
#include "galaxy.h"

class Civ {
public:
    uint8_t id;
    SDL_Color color;
    std::vector<Star*> controlled;

    Civ(uint8_t id, SDL_Color color);
    void occupy(Star* s);
    void tick(std::vector<Star>& stars, std::vector<Civ>& all_civs); // ← updated signature

    float total_resources() const;  // ← new declaration
    void spend_resources(float amount);  // ← new declaration
};

class CivManager {
public:
    std::vector<Civ> civs;
    void spawn(Galaxy& galaxy);
    void tick(Galaxy& galaxy);
};