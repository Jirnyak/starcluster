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

void Game::updateEncounters(double dt) {
    if (playerAgent < 0 || playerAgent >= (int)agents.size()) return;
    Agent& p = agents[playerAgent];
    if (!p.ship.enRoute) return; // бой возможен только в перелёте — стыковка безопасна

    // Триггер стычки — редкий пуассоновский поток, не более одной за тик.
    const double lambda = 0.10; // ожидаемых стычек в год
    if (!(randomer(rng, 999999) < int(lambda * dt * 1000000.0))) return;

    // --- Мгновенное разрешение боя ---
    // Враг масштабируется с прогрессом игрока, чтобы оставаться значимым.
    double enemy = 8.0 + tech.cores * 2.0 + randomer(rng, 10);
    // СЫРЫЕ поля оружия (тактика уже в них запечена) + лёгкая подкрутка удачи.
    double player = (p.ship.heavyWeapons * 0.6 + p.ship.lightWeapons * 0.4 + p.ship.armor * 0.3)
                    * (0.9 + 0.2 * std::max(1.0, tech.luck));

    if (player >= enemy) {
        // --- ПОБЕДА: награда, мелкие царапины, подпитка исследований ---
        double bounty = 200.0 + enemy * 30.0 + randomer(rng, 400);
        p.money += bounty;
        p.ship.hullHP = std::max(1.0, p.ship.hullHP - randomer(rng, 5));
        addResearch(4);

        // Возможный трофей: ~35% шанс закинуть немного случайного элемента.
        // Ёмкость трюма намеренно не считаем — просто добавляем запись ~2..6 массы.
        if (randomer(rng, 999999) < int(0.35 * 1000000.0)) {
            const std::vector<ElementDefinition>& elems = elementDefinitions();
            const int ecount = (int)elementCount();
            if (ecount > 0) {
                int idx = randomer(rng, ecount - 1);
                double amt = 2.0 + randomer(rng, 4); // ~2..6 массы
                std::string sym(elems[idx].symbol);
                p.ship.cargo.emplace_back(sym, amt);
            }
        }

        pushNews("Ambush repelled — bounty +" + intStr(bounty) + " cr", 2);
        p.lastAction = "won skirmish";
    } else {
        // --- ПОРАЖЕНИЕ: урон корпусу и грабёж груза ---
        double dmg = 6.0 + (enemy - player) * 3.0 + randomer(rng, 8);
        p.ship.hullHP = std::max(1.0, p.ship.hullHP - dmg);

        // Пираты крадут до ~30% первой записи груза; пустую запись убираем.
        if (!p.ship.cargo.empty()) {
            double stolen = p.ship.cargo[0].amount * 0.30;
            p.ship.cargo[0].amount -= stolen;
            if (p.ship.cargo[0].amount <= 0.01) {
                p.ship.cargo.erase(p.ship.cargo.begin());
            }
        }

        pushNews("Pirate ambush! Hull -" + intStr(dmg), 2);
        if (p.ship.hullHP <= p.ship.maxHullHP * 0.25) {
            pushNews("Hull critical — dock and repair (J)", 2);
        }
        p.lastAction = "took damage";
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
