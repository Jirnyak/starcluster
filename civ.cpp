// civ.cpp
#include "civ.h"
#include "game.h"
#include <limits>
#include <cmath>
#include <algorithm>
#include <random>

Civ::Civ(uint8_t id, SDL_Color color) : id(id), color(color) {}

void Civ::occupy(Star* s) {
    if (s->civ_id == 0) {
        s->civ_id = id;
        controlled.push_back(s);
    }
}

float Civ::total_resources() const {
    float sum = 0;
    for (Star* s : controlled) {
        for (int i = 0; i < RESOURCE_TYPES; ++i) {
            sum += s->resources[i];
        }
    }
    return sum;
}

void Civ::spend_resources(float amount) {
    for (Star* s : controlled) {
        for (int i = 0; i < RESOURCE_TYPES; ++i) {
            float take = std::min(amount / RESOURCE_TYPES, s->resources[i]);
            s->resources[i] -= take;
            amount -= take;
            if (amount <= 0) return;
        }
    }
}

void Civ::tick(std::vector<Star>& stars, std::vector<Civ>& all_civs) {
    for (Star* s : controlled) {
        for (int i = 0; i < RESOURCE_TYPES; ++i)
            s->resources[i] += 0.1f;
    }

    if (controlled.size() < 100) {
        float sum_x = 0, sum_y = 0;
        for (Star* s : controlled) {
            sum_x += s->x;
            sum_y += s->y;
        }
        float cx = sum_x / controlled.size();
        float cy = sum_y / controlled.size();

        std::vector<std::pair<float, Star*>> candidates;

        for (Star& s : stars) {
            if (s.civ_id == 0 || s.civ_id != id) {
                float dx = s.x - cx;
                float dy = s.y - cy;
                float dist_sq = dx * dx + dy * dy;
                candidates.emplace_back(dist_sq, &s);
            }
        }

        int n_targets = std::max(10, static_cast<int>(controlled.size() / 5));
        std::sort(candidates.begin(), candidates.end(),
          [](const std::pair<float, Star*>& a, const std::pair<float, Star*>& b) {
              return a.first < b.first;
          });

        if (!candidates.empty()) {
            int pick = std::min(n_targets, static_cast<int>(candidates.size()));
            Star* target = candidates[randomer(rng, pick - 1)].second;

            bool conflict = false;
            for (const Star& s : stars) {
                if (s.civ_id != 0 && s.civ_id != id) {
                    float dx = s.x - target->x;
                    float dy = s.y - target->y;
                    float dist_sq = dx * dx + dy * dy;
                    if (dist_sq < 0.2f) {
                        conflict = true;

                        float self_cost = total_resources() / (controlled.size() + 1) + 5.0f;
                        float enemy_cost = 0;

                        for (Civ& other : all_civs) {
                            if (other.id == s.civ_id) {
                                enemy_cost = other.total_resources() / (other.controlled.size() + 1) + 5.0f;
                                if (self_cost > enemy_cost) {
                                    spend_resources(self_cost);
                                    other.spend_resources(enemy_cost);
                                    occupy(target);
                                } else {
                                    spend_resources(self_cost);
                                    other.spend_resources(enemy_cost);
                                }
                                return;
                            }
                        }
                    }
                }
            }

            if (!conflict) occupy(target);
        }
    }
}

void CivManager::spawn(Galaxy& galaxy) {
    for (int i = 1; i <= CIV_COUNT; ++i) {
        int index = randomer(rng, galaxy.stars.size() - 1);
        SDL_Color c = { (Uint8)randomer(rng, 255), (Uint8)randomer(rng, 255), (Uint8)randomer(rng, 255), 255 };
        civs.emplace_back(i, c);
        civs.back().occupy(&galaxy.stars[index]);
    }
}

void CivManager::tick(Galaxy& galaxy) {
    for (auto& civ : civs) {
        civ.tick(galaxy.stars, civs);
    }
}
