#include "game.h"
#include <algorithm>
#include <string>
#include <vector>

// Стычки в перелёте и ремонт корпуса (plans_6). Бой разрешается мгновенно как
// событие — постоянной сущности врага нет. Стиль: C-style, данные, без классов.
// ВАЖНО: tech.tactics уже запечена в ship.heavyWeapons/lightWeapons (chromo.cpp),
// поэтому берём СЫРЫЕ поля оружия и НЕ умножаем на tactics повторно.

namespace {
// Округление до целого для новостных строк ("+150 cr" и т.п.).
std::string intStr(double v) {
    return std::to_string(int(v + 0.5));
}
}


bool Game::playerRepairHull() {
    if (playerAgent < 0 || playerAgent >= (int)agents.size()) return false;
    Agent& p = agents[playerAgent];
    if (p.ship.enRoute) {
        lastEvent = "cannot repair in transit";
        return false;
    }

    // Нужен ремонтный док: верфь >=1 ИЛИ просто обитаемая текущая звезда.
    bool canRepair = false;
    if (shipyardLevelAtStar(p.currentStar) >= 1) {
        canRepair = true;
    } else if (p.currentStar >= 0 && p.currentStar < (int)cluster.stars.size() &&
               cluster.stars[p.currentStar].population > 0.0) {
        canRepair = true;
    }
    if (!canRepair) {
        lastEvent = "no repair facility here";
        pushNews("No repair facility here", 0);
        return false;
    }

    if (p.ship.hullHP >= p.ship.maxHullHP) {
        lastEvent = "hull already intact";
        return false;
    }

    // Харизма удешевляет ремонт (>=1). Латаем столько HP, сколько по карману.
    double costPerHP = 3.0 / std::max(1.0, tech.charisma);
    double missing = p.ship.maxHullHP - p.ship.hullHP;
    if (p.money <= 0.0) {
        lastEvent = "not enough credits to repair";
        pushNews("Not enough credits to repair", 0);
        return false;
    }
    double affordableHP = p.money / costPerHP;
    double repaired = std::min(missing, affordableHP);
    p.ship.hullHP += repaired;
    p.money -= repaired * costPerHP;

    pushNews("Hull repaired +" + intStr(repaired) + " (-" + intStr(repaired * costPerHP) + " cr)", 4);
    p.lastAction = "repaired hull";
    return true;
}
