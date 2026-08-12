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

    // Запекаем в поля корабля игрока статы, которые читает легаси-код. Закон
    // запекания — общий с пересадкой на новый корпус (shipApplyChromocoreFactors),
    // сюда он приходит с приростом ровно одного ядра.
    if (playerAgent >= 0 && playerAgent < int(agents.size())) {
        Ship& ship = agents[playerAgent].ship;
        const double f = 1.0 + step;
        shipApplyChromocoreFactors(ship,
                                   stat == TECH_MATERIALS ? f : 1.0,
                                   stat == TECH_TACTICS ? f : 1.0,
                                   stat == TECH_KINEMATICS ? f : 1.0);
        if (stat == TECH_MATERIALS) ship.hullHP = ship.maxHullHP;
    }

    const std::string label = chromocoreStatLabel(stat);
    lastEvent = "CHROMOCORE +" + label;
    pushNews("Chromocore attuned: " + label + " model refined", 4);
}

void Game::rebakePlayerBakedBonuses() {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return;
    Ship& ship = agents[playerAgent].ship;
    shipApplyChromocoreFactors(ship, tech.materials, tech.tactics, tech.kinematics);
    // Хайтек-переоснастка (§31.4) запекается тем же порядком и по той же
    // причине: ёмкость ячейки, броня, корпус и масса — это ОТРАЖЕНИЕ ступеней
    // `containmentLevel`/`platingLayers`, и новый корпус это отражение стирает.
    // Сами ступени живут на корабле, потому и переживают пересадку игрока.
    ship.containment = double(ship.containmentLevel) * CONTAINMENT_STEP_UNITS;
    if (ship.platingLayers > 0) {
        ship.armor += PLATING_ARMOR_PER_LAYER * ship.platingLayers;
        ship.maxHullHP += PLATING_HULL_PER_LAYER * ship.platingLayers;
        ship.dryMass += PLATING_MASS_PER_LAYER * ship.platingLayers;
    }
    ship.hullHP = std::min(ship.hullHP, ship.maxHullHP);
    // Ячейка могла ужаться (капсула после гибели) — лишнее вещество выпадает
    // из удержания. Аннигилировать его на борту было бы честнее физически, но
    // игрока это убивало бы без предупреждения; считаем, что его сбрасывают.
    double held = 0.0;
    for (int k = 0; k < EX_COUNT; ++k) held += ship.exotic[k];
    if (held > ship.containment && held > 0.0) {
        const double keep = ship.containment / held;
        for (int k = 0; k < EX_COUNT; ++k) ship.exotic[k] *= keep;
    }
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
