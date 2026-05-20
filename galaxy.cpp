// galaxy.cpp
#include "galaxy.h"
#include <random>

void Galaxy::generate_stars() {
    stars.reserve(STAR_COUNT);
    std::normal_distribution<float> dist(0.0f, 0.4f);
    for (int i = 0; i < STAR_COUNT; ++i) {
        float x = dist(rng);
        float y = dist(rng);
        stars.emplace_back(x, y);
    }
}
