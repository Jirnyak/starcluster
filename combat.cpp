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

    // (§52) РЕМОНТ СТОИТ ПО-МЕСТНОМУ, А НЕ ПО КОНСТАНТЕ.
    //
    // До этого 3.0 Cr за HP брали одинаково в нищем пограничье и в столице —
    // единственная цена в игре, не знавшая ни рынка, ни владельца, ни
    // расстояния. Верфь чинит корпус трудом и материалами, а обе эти вещи на
    // рынке уже есть ценой: `serviceCostAvg` — средняя цена ЕДИНИЦЫ УСЛУГИ в
    // этом порту. Делим её на медиану по скоплению (`measureClusterServiceCost`
    // — тот же измеритель, на котором стоит ставка тарифа, §21): в порту
    // СРЕДНЕЙ дороговизны множитель равен 1.0, то есть прежние 3.0 Cr/HP
    // остаются якорем, и баланс никуда не съезжает — двигается только разброс.
    //
    // ⚠️ Пределы ЗАМЕРЕНЫ, а не выбраны: `repair_probe`, 8192 звезды, 150 лет,
    // сид 20260814 — отношение к медиане идёт от 0.05 (1-й перцентиль 0.27)
    // до 2.98 (99-й 2.46). Хвосты и обрезаем по 1% и 99%: без нижнего порога
    // выродившийся порт чинил бы почти даром, без верхнего — ремонт в дорогом
    // порту становился бы неподъёмным, а неподъёмный ремонт запирает игру
    // насмерть (§42).
    double locality = 1.0;
    if (p.currentStar >= 0 && p.currentStar < int(markets.size())) {
        const double local = markets[size_t(p.currentStar)].serviceCostAvg;
        const double median = measureClusterServiceCost();
        if (local > 0.0 && median > 0.0) {
            locality = std::max(0.27, std::min(2.46, local / median));
        }
    }
    // Харизма удешевляет ремонт (>=1). Латаем столько HP, сколько по карману.
    const double costPerHP = 3.0 * locality / std::max(1.0, tech.charisma);
    // Вторая сторона проводки (§47): владелец системы получает свою пошлину.
    // Раньше за ремонт ему не капало ничего — порт чинил чужие корпуса даром
    // для казны. Харизма сбивает и её, ровно как при заправке.
    const double tariff = playerPortTariff(p.currentStar) / std::max(1.0, tech.charisma);
    const double perHP = costPerHP * (1.0 + tariff);

    double missing = p.ship.maxHullHP - p.ship.hullHP;
    if (p.money <= 0.0 || perHP <= 0.0) {
        lastEvent = "not enough credits to repair";
        pushNews("Not enough credits to repair", 0);
        return false;
    }
    double affordableHP = p.money / perHP;
    double repaired = std::min(missing, affordableHP);
    const double base = repaired * costPerHP;
    const double fee = repaired * costPerHP * tariff;
    p.ship.hullHP += repaired;
    p.money -= base + fee;
    const int owner = p.currentStar >= 0 && p.currentStar < int(cluster.stars.size())
        ? cluster.stars[size_t(p.currentStar)].ownerFaction : -1;
    if (owner >= 0 && owner < int(factions.size()) && fee > 0.0) {
        factions[size_t(owner)].treasury += fee;
        if (fee > 0.01) queueSettlementSignal(owner, p.currentStar, fee);
    }

    pushNews("Hull repaired +" + intStr(repaired) + " (-" + intStr(base + fee) + " cr)", 4);
    p.lastAction = "repaired hull";
    return true;
}
