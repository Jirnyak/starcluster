#include "game.h"
#include "ship.h"
#include <algorithm>
#include <string>

// Хромокоры: получение ядер и накопление исследований (design.md §2).
// Множители >= 1.0. Материалы/кинематика/тактика "запекаются" в поля корабля
// игрока (чтобы легаси-код их видел); остальные читаются в точке использования.

namespace {
const char* STAT_NAMES[TECH_STAT_COUNT] = {
    "INTELLECT", "CHARISMA", "MATERIALS", "TACTICS", "KINEMATICS", "SENSORS", "LUCK"
};
double& statRef(TechState& t, int stat) {
    switch (stat) {
        case TECH_INTELLECT: return t.intellect;
        case TECH_CHARISMA: return t.charisma;
        case TECH_MATERIALS: return t.materials;
        case TECH_TACTICS: return t.tactics;
        case TECH_KINEMATICS: return t.kinematics;
        case TECH_SENSORS: return t.sensors;
        default: return t.luck;
    }
}
}

const char* chromocoreStatLabel(int stat) {
    if (stat < 0 || stat >= TECH_STAT_COUNT) return "CORE";
    return STAT_NAMES[stat];
}

void Game::grantChromocore(int stat) {
    if (stat < 0 || stat >= TECH_STAT_COUNT) stat = TECH_INTELLECT;
    tech.cores += 1;
    const double step = 0.06;
    double& s = statRef(tech, stat);
    s *= (1.0 + step);

    // Запекаем в поля корабля игрока статы, которые читает легаси-код.
    if (playerAgent >= 0 && playerAgent < int(agents.size())) {
        Ship& ship = agents[playerAgent].ship;
        if (stat == TECH_MATERIALS) {
            ship.cargoCapacity *= (1.0 + step);
            ship.maxHullHP *= (1.0 + step);
            ship.hullHP = ship.maxHullHP;
        } else if (stat == TECH_KINEMATICS) {
            ship.speed = std::min(0.5, ship.speed * (1.0 + step));
            ship.acceleration *= (1.0 + step * 0.5);
        } else if (stat == TECH_TACTICS) {
            ship.heavyWeapons *= (1.0 + step);
            ship.lightWeapons *= (1.0 + step);
        }
    }

    const std::string label = chromocoreStatLabel(stat);
    lastEvent = "CHROMOCORE +" + label;
    pushNews("Chromocore attuned: " + label + " model refined", 4);
}

void Game::addResearch(double amount) {
    if (amount <= 0.0) return;
    tech.research += amount * std::max(1.0, tech.intellect);
    for (;;) {
        const double threshold = 100.0 + tech.cores * 40.0;
        if (tech.research < threshold) break;
        tech.research -= threshold;
        const int stat = randomer(rng, TECH_STAT_COUNT - 1);
        grantChromocore(stat);
    }
}
