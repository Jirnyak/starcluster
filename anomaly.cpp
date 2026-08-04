#include "game.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Аномалии / дереликты (plans_10). См. game.h для API.
// Стиль: C-style, данные, без новых классов. Детерминированный общий rng.

namespace {

// Добавляет `amount` единиц элемента `idx` в трюм. Что не влезает по массе —
// конвертируется в кредиты по базовой цене и падает в `money`.
void addLootOrCredits(Ship& ship, double& money, int idx, double amount) {
    const std::vector<ElementDefinition>& elems = elementDefinitions();
    const int ecount = int(elementCount());
    if (idx < 0 || idx >= ecount || amount <= 0.0) return;

    const double unit = std::max(0.001, resourceUnitMassByIndex(idx));
    const double freeMass = ship.cargoCapacity - shipCargoMass(ship);
    const double fitUnits = freeMass > 0.0 ? freeMass / unit : 0.0;
    const double take = std::min(amount, fitUnits);
    const double overflow = amount - take;

    if (take > 0.0) {
        const std::string sym(elems[idx].symbol);
        bool found = false;
        for (size_t i = 0; i < ship.cargo.size(); ++i) {
            if (ship.cargo[i].element == sym) { ship.cargo[i].amount += take; found = true; break; }
        }
        if (!found) ship.cargo.emplace_back(sym, take);
    }
    if (overflow > 0.0) money += overflow * marketReferencePrice(idx);
}

// Размещает одну аномалию рядом со случайной звездой (общий для сева и респауна).
void placeAnomaly(Game& g) {
    if (g.cluster.stars.empty()) return;
    const int scount = int(g.cluster.stars.size());
    int s = randomer(rng, scount - 1);
    if (s < 0) s = 0;
    if (s >= scount) s = scount - 1;
    const ClusterStar& st = g.cluster.stars[s];

    const std::vector<ElementDefinition>& elems = elementDefinitions();
    const int ecount = int(elementCount());

    Anomaly a;
    a.x = st.x + (randomer(rng, 200) - 100) * 0.02; // ±2 ly
    a.y = st.y + (randomer(rng, 200) - 100) * 0.02;
    a.z = st.z + (randomer(rng, 200) - 100) * 0.02;
    a.nearStar = s;
    a.discovered = false;
    a.resolved = false;

    // Вес: Derelict ~40%, Cache ~24%, Echo ~16%, IonStorm ~12%, Vault ~8%.
    const int r = randomer(rng, 99);
    if (r < 40) {
        a.kind = AnomalyKind::DerelictShip;
        a.credits = 300 + randomer(rng, 1200);
        if (ecount > 0 && randomer(rng, 99) < 60) {
            a.lootElement = randomer(rng, ecount - 1);
            a.lootAmount = 3 + randomer(rng, 12);
        }
        a.hazard = 0.05 + (randomer(rng, 15) / 100.0);
        a.name = "DERELICT";
    } else if (r < 64) {
        a.kind = AnomalyKind::AncientCache;
        if (ecount > 0) {
            // Тянем к дорогому: сэмплируем несколько кандидатов, оставляем самый ценный.
            a.lootElement = randomer(rng, ecount - 1);
            double best = marketReferencePrice(a.lootElement);
            for (int t = 0; t < 4; ++t) {
                const int cand = randomer(rng, ecount - 1);
                if (marketReferencePrice(cand) > best) { best = marketReferencePrice(cand); a.lootElement = cand; }
            }
            a.lootAmount = 6 + randomer(rng, 26);
        }
        a.credits = randomer(rng, 400);
        a.hazard = 0.02;
        a.name = "CACHE";
    } else if (r < 80) {
        a.kind = AnomalyKind::NebulaEcho;
        a.hazard = 0.0;
        a.name = "ECHO";
    } else if (r < 92) {
        a.kind = AnomalyKind::IonStorm;
        a.hazard = 0.35 + randomer(rng, 40) / 100.0;
        a.name = "ION STORM";
    } else {
        a.kind = AnomalyKind::ChromocoreVault;
        a.chromocoreStat = randomer(rng, 6);
        a.hazard = 0.10 + randomer(rng, 20) / 100.0;
        a.name = "VAULT";
    }

    g.anomalies.push_back(a);
}

} // namespace

void Game::seedAnomalies() {
    anomalies.clear();
    if (cluster.stars.empty()) return;
    const int n = std::min<int>(60, std::max<int>(12, int(cluster.stars.size() / 150)));
    for (int i = 0; i < n; ++i) placeAnomaly(*this);
}

void Game::updateAnomalies(double dt) {
    const int scount = int(cluster.stars.size());

    // --- Обнаружение сенсорами игрока ---
    if (playerAgent >= 0 && playerAgent < int(agents.size())) {
        Ship& sh = agents[playerAgent].ship;
        const double range = 1.2 + 0.9 * tech.sensors + sh.utility * 0.03;
        for (size_t i = 0; i < anomalies.size(); ++i) {
            Anomaly& a = anomalies[i];
            if (a.discovered || a.resolved) continue;
            const double dx = a.x - sh.x, dy = a.y - sh.y, dz = a.z - sh.z;
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            const bool nearDock = (a.nearStar >= 0 && a.nearStar < scount && playerAtStar(a.nearStar));
            if (d <= range || nearDock) {
                a.discovered = true;
                const std::string starName =
                    (a.nearStar >= 0 && a.nearStar < scount) ? cluster.stars[a.nearStar].name
                                                             : std::string("deep space");
                pushNews("Anomaly detected: " + a.name + " near " + starName, 3);
                addResearch(0.5);
            }
        }
    }

    // --- Респаун (ограничен по числу нерешённых и общему размеру) ---
    anomalyTimer += dt;
    const double INTERVAL = 22.0;
    while (anomalyTimer >= INTERVAL) {
        anomalyTimer -= INTERVAL;
        int unresolved = 0;
        for (size_t i = 0; i < anomalies.size(); ++i) if (!anomalies[i].resolved) ++unresolved;
        if (unresolved < 70 && anomalies.size() < 240) placeAnomaly(*this);
    }
}

bool Game::playerScanAnomaly() {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return false;
    Agent& p = agents[playerAgent];
    if (p.ship.enRoute) { lastEvent = "cannot scan in transit"; return false; }

    // Ближайшая обнаруженная, ещё не обследованная аномалия в радиусе действия.
    int best = -1;
    double bestD = 0.0;
    for (size_t i = 0; i < anomalies.size(); ++i) {
        Anomaly& a = anomalies[i];
        if (!a.discovered || a.resolved) continue;
        const double dx = a.x - p.ship.x, dy = a.y - p.ship.y, dz = a.z - p.ship.z;
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        const bool inRange = (d <= 2.5) || (a.nearStar == p.currentStar);
        if (!inRange) continue;
        if (best < 0 || d < bestD) { best = int(i); bestD = d; }
    }
    if (best < 0) { lastEvent = "no anomaly in scan range"; return false; }

    Anomaly& a = anomalies[best];
    a.resolved = true;

    const double luck = std::max(1.0, tech.luck);
    const int ecount = int(elementCount());

    switch (a.kind) {
        case AnomalyKind::DerelictShip: {
            const double cr = a.credits * luck;
            p.money += cr;
            if (a.lootElement >= 0 && a.lootElement < ecount && a.lootAmount > 0.0)
                addLootOrCredits(p.ship, p.money, a.lootElement, a.lootAmount);
            addResearch(4);
            pushNews("Salvaged derelict: +" + std::to_string(int(cr)) + " cr", 3);
            break;
        }
        case AnomalyKind::AncientCache: {
            if (a.credits > 0.0) p.money += a.credits * luck; // тайник = кредиты + редкие элементы
            if (a.lootElement >= 0 && a.lootElement < ecount && a.lootAmount > 0.0)
                addLootOrCredits(p.ship, p.money, a.lootElement, a.lootAmount);
            addResearch(20 * luck);
            pushNews("Cache recovered", 3);
            break;
        }
        case AnomalyKind::ChromocoreVault: {
            grantChromocore(a.chromocoreStat >= 0 ? a.chromocoreStat : 0);
            addResearch(30);
            pushNews("Chromocore vault attuned!", 3);
            break;
        }
        case AnomalyKind::NebulaEcho: {
            addResearch(55 * luck);
            pushNews("Nebula echo decoded — research surge", 3);
            break;
        }
        case AnomalyKind::IonStorm: {
            const double dmg = a.hazard * 30.0 / luck;
            p.ship.hullHP = std::max(1.0, p.ship.hullHP - dmg);
            p.money += (400 + randomer(rng, 800)) * luck;
            addResearch(18);
            pushNews("Rode the ion storm: hull -" + std::to_string(int(dmg)) + ", data +", 2);
            break;
        }
    }

    p.lastAction = "scanned anomaly";
    return true;
}
