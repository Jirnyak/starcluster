// galaxy.h
#pragma once
#include <vector>
#include "game.h"

struct Star {
    float x, y;
    float resources[RESOURCE_TYPES] = {0};
    uint8_t civ_id = 0;

    Star(float x = 0, float y = 0) : x(x), y(y) {}
};

class Galaxy {
public:
    std::vector<Star> stars;
    void generate_stars();
};
