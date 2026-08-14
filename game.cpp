#include "game.h"
#include "drive.h"
#include "econ.h"
#include "i18n.h"
#include "modules.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <queue>
#include <thread>
#include <atomic>
#include <cstdio>   // (§48, замер) разбор быстрых прибытий на stderr
// Глобальный ГСЧ симуляции. Раньше засевался прямо здесь из `std::random_device`,
// то есть поле `Game::seed` ни на что не влияло, а мир менялся при каждом запуске
// НЕВОСПРОИЗВОДИМО: soak и shot-харнесы давали разные числа от прогона к прогону
// (проверено: deaths=5968 против deaths=718 на одном и том же бинаре), поэтому
// проверки «нулевого регресса» ничего не проверяли, а баг игрока нельзя было
// повторить. Теперь засев делает `Game::init` из `Game::seed` — разнообразие миров
// задаётся выбором seed на старте новой игры, а не неуправляемым состоянием.
std::mt19937 rng(42u);

int randomer(std::mt19937& rng_, int max) {
    if (max <= 0) return 0;
    std::uniform_int_distribution<int> dist(0, max);
    return dist(rng_);
}

namespace {

struct TradePlan {
    int destStar = -1;
    int elementIndex = -1;
    double amount = 0.0;
    double fuelCost = 0.0;
    double buyPrice = 0.0;
    double sellPrice = 0.0;
    double score = 0.0;
};

struct FactionSeed {
    const char* name;
    int r;
    int g;
    int b;
    double aggression;
};

struct ThreatCandidate {
    bool valid = false;
    int starIndex = -1;
    int sourceAgent = -1;
    int sourceFaction = -1;
    double observedAt = -1.0;
    double threatValue = 0.0;
    double cargoValue = 0.0;
    bool piracy = false;
};

bool validStar(const Game& game, int starIndex) {
    return starIndex >= 0 && starIndex < int(game.cluster.stars.size());
}

bool validFaction(const Game& game, int factionIndex) {
    return factionIndex >= 0 && factionIndex < int(game.factions.size());
}

// (§47) ВЛАСТЬ НАД СИСТЕМОЙ. Ничейных систем в скоплении нет: у звезды либо
// есть держава-владелец, либо она ГОСУДАРСТВЕННАЯ — под прямым управлением
// центра, клиринговой палаты, которая живёт с квот и тарифов (§18) и уже
// субсидирует колонии.
//
// ⚠️ Это единственный смысл, в котором звёзды «государственные»: поле
// `ownerFaction` у них остаётся −1 (решение пользователя). Владение НЕ
// прописывается, потому что тогда `controlledStars` палаты разрослась бы до
// восьми тысяч, а колонизация («ищу ничейную звезду») перестала бы находить
// хоть что-нибудь. Спрашивать «чья это система» надо ЗДЕСЬ, а не читать
// `ownerFaction` напрямую, иначе центр снова окажется невидимым.
int starAuthority(const Game& game, int starIndex) {
    if (validStar(game, starIndex)) {
        const int owner = game.cluster.stars[starIndex].ownerFaction;
        if (validFaction(game, owner)) return owner;
    }
    return validFaction(game, game.clearingFaction) ? game.clearingFaction : -1;
}

size_t factionKnowledgeIndex(const Game& game, int factionIndex, int starIndex) {
    return size_t(factionIndex) * game.cluster.stars.size() + size_t(starIndex);
}

size_t factionMarketPriceIndex(const Game& game, int factionIndex, int starIndex, int elementIndex) {
    const size_t starCount = game.cluster.stars.size();
    const size_t resourceCount = elementCount();
    return (size_t(factionIndex) * starCount + size_t(starIndex)) * resourceCount + size_t(elementIndex);
}

size_t factionRelationIndex(const Game& game, int factionA, int factionB) {
    return size_t(factionA) * game.factions.size() + size_t(factionB);
}

int clampRelation(int value) {
    return std::max(-128, std::min(128, value));
}

const char* contractTypeLabel(ContractType type) {
    switch (type) {
    case ContractType::Delivery: return "DEL";
    case ContractType::Courier: return "CUR";
    case ContractType::Scout: return "SCT";
    case ContractType::Bounty: return "BNT";
    case ContractType::Escort: return "ESC";
    case ContractType::Raid: return "RAD";
    case ContractType::ColonySupply: return "SUP";
    }
    return "JOB";
}

const unsigned short ROUTE_NO_HOP = 65535;
const int ROUTE_NEIGHBORS = 32;
const double ROUTE_REBUILD_INTERVAL_YEARS = 1000.0;
const double MARKET_UPDATE_INTERVAL_YEARS = 1.0;
const int SIGNAL_MEMORY_PER_STAR = 24;

bool contractUsesCargo(ContractType type) {
    return type == ContractType::Delivery || type == ContractType::ColonySupply;
}

bool contractNeedsTargetAgent(ContractType type) {
    return type == ContractType::Bounty || type == ContractType::Escort;
}

double averageValue(const std::vector<double>& values) {
    if (values.empty()) return 1.0;
    double sum = 0.0;
    for (double value : values) sum += value;
    return sum / double(values.size());
}

std::vector<double> marketSupplyPressureSnapshot(const Market& market) {
    std::vector<double> snapshot;
    snapshot.reserve(market.prices.size());
    for (size_t i = 0; i < market.prices.size(); ++i) {
        const double supply = i < market.supply.size() ? market.supply[i].amount : 0.0;
        const double demand = i < market.demand.size() ? market.demand[i].amount : 0.0;
        const double production = i < market.productionRate.size() ? market.productionRate[i] * 70.0 : 0.0;
        const double consumption = i < market.demandRate.size() ? market.demandRate[i] * 80.0 : 0.0;
        snapshot.push_back((supply + production + 1.0) / (demand + consumption + 1.0));
    }
    return snapshot;
}

std::vector<double> marketDemandPressureSnapshot(const Market& market) {
    std::vector<double> snapshot;
    snapshot.reserve(market.prices.size());
    for (size_t i = 0; i < market.prices.size(); ++i) {
        const double supply = i < market.supply.size() ? market.supply[i].amount : 0.0;
        const double demand = i < market.demand.size() ? market.demand[i].amount : 0.0;
        const double production = i < market.productionRate.size() ? market.productionRate[i] * 70.0 : 0.0;
        const double consumption = i < market.demandRate.size() ? market.demandRate[i] * 80.0 : 0.0;
        snapshot.push_back((demand + consumption + 1.0) / (supply + production + 1.0));
    }
    return snapshot;
}

double marketMemoryTau(const ElementDefinition& element) {
    const double stableGoods = element.nobleStability * 12.0 + element.structuralTrait * 8.0;
    const double volatileGoods = element.handlingRisk * 10.0 + element.demandWeight * 1.4;
    return std::max(4.0, std::min(42.0, 18.0 + stableGoods - volatileGoods));
}

double tariffFor(const Game& game, int starIndex, int ownerFaction, double externalRate);
bool startJourney(Game& game, Agent& agent, int destStar);
double combatPower(const Game& game, const Agent& agent);
double cargoValueAt(const Game& game, const Agent& agent, int starIndex);
bool agentIsPiracyThreat(const Agent& agent);
double cachedRouteDistance(const Game& game, int originStar, int targetStar);

double distanceBetween(const ClusterStar& a, const ClusterStar& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double distanceShipToStar(const Ship& ship, const ClusterStar& star) {
    const double dx = ship.x - star.x;
    const double dy = ship.y - star.y;
    const double dz = ship.z - star.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double distanceSquaredStarToStar(const ClusterStar& a, const ClusterStar& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

double distancePointToSegment(const ClusterStar& a, const ClusterStar& b, const ClusterStar& p) {
    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double abz = b.z - a.z;
    const double apx = p.x - a.x;
    const double apy = p.y - a.y;
    const double apz = p.z - a.z;
    const double len2 = abx * abx + aby * aby + abz * abz;
    const double t = len2 > 0.000001 ? std::max(0.0, std::min(1.0, (apx * abx + apy * aby + apz * abz) / len2)) : 0.0;
    const double cx = a.x + abx * t;
    const double cy = a.y + aby * t;
    const double cz = a.z + abz * t;
    const double dx = p.x - cx;
    const double dy = p.y - cy;
    const double dz = p.z - cz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double travelTimeEstimate(double distance, const Ship& ship) {
    const double accel = std::max(0.001, std::min(ship.acceleration, ship.driveThrust / shipTotalMass(ship)));
    const double maxSpeed = std::max(0.1, ship.speed);
    const double accelDistance = maxSpeed * maxSpeed / accel;
    if (distance <= accelDistance) {
        return 2.0 * std::sqrt(distance / accel);
    }
    return distance / maxSpeed + maxSpeed / accel;
}

int colonyIndexAt(const Game& game, int starIndex) {
    for (size_t i = 0; i < game.colonies.size(); ++i) {
        if (game.colonies[i].starIndex == starIndex) return int(i);
    }
    return -1;
}

void removeControlledStar(Faction& faction, int starIndex) {
    faction.controlledStars.erase(std::remove(faction.controlledStars.begin(), faction.controlledStars.end(), starIndex), faction.controlledStars.end());
}

void setStarOwner(Game& game, int starIndex, int factionIndex) {
    if (!validStar(game, starIndex)) return;

    ClusterStar& star = game.cluster.stars[starIndex];
    if (star.ownerFaction == factionIndex) return;

    if (validFaction(game, star.ownerFaction)) {
        removeControlledStar(game.factions[star.ownerFaction], starIndex);
    }

    star.ownerFaction = factionIndex;
    star.occupyingFaction = -1;
    star.captureProgress = 0.0;
    star.capturePressure = 0.0;
    star.contestedAt = -1.0;
    if (validFaction(game, factionIndex)) {
        std::vector<int>& controlled = game.factions[factionIndex].controlledStars;
        if (std::find(controlled.begin(), controlled.end(), starIndex) == controlled.end()) {
            controlled.push_back(starIndex);
        }
    }
}

void registerFactionAgent(Game& game, int agentIndex) {
    if (agentIndex < 0 || agentIndex >= int(game.agents.size())) return;
    const int factionIndex = game.agents[agentIndex].ship.ownerFaction;
    if (!validFaction(game, factionIndex)) return;
    std::vector<int>& fleet = game.factions[factionIndex].fleetAgents;
    if (std::find(fleet.begin(), fleet.end(), agentIndex) == fleet.end()) {
        fleet.push_back(agentIndex);
    }
}

void transferColonies(Game& game, int starIndex, int factionIndex) {
    for (Colony& colony : game.colonies) {
        if (colony.starIndex == starIndex) colony.ownerFaction = factionIndex;
    }
}

void addColony(Game& game, int starIndex, int factionIndex, bool capital) {
    if (!validStar(game, starIndex) || !validFaction(game, factionIndex)) return;
    if (colonyIndexAt(game, starIndex) >= 0) return;

    ClusterStar& star = game.cluster.stars[starIndex];
    const double populationShare = capital ? 0.42 : 0.16;
    const size_t population = size_t(std::max(120.0, star.population * populationShare));
    const double infrastructure = std::max(0.55, star.industry * (capital ? 1.35 : 0.72));
    game.colonies.emplace_back(star.name + (capital ? "_Prime" : "_Charter"), population, star.economyRole, starIndex, factionIndex, infrastructure);
    star.defense += capital ? 2.0 : 0.8;
}

void addColonyStockpile(Colony& colony, int resourceIndex, double amount) {
    if (resourceIndex < 0 || resourceIndex >= int(elementCount()) || amount <= 0.0) return;
    const char* symbol = elementDefinitions()[resourceIndex].symbol;
    for (Resource& resource : colony.stockpile) {
        if (resource.element == symbol) {
            resource.amount += amount;
            return;
        }
    }
    colony.stockpile.emplace_back(symbol, amount);
}

void applyColonySupplyDelivery(Game& game, int starIndex, int resourceIndex, double amount) {
    const int index = colonyIndexAt(game, starIndex);
    if (index < 0 || resourceIndex < 0 || resourceIndex >= int(elementCount()) || amount <= 0.0) return;

    Colony& colony = game.colonies[index];
    const ElementDefinition& element = elementDefinitions()[resourceIndex];
    const double mass = amount * resourceUnitMassByIndex(resourceIndex);
    const double energyTrait = std::max(element.fusionFuelTrait, element.fissionFuelTrait);
    const double buildUtility =
        element.structuralTrait * 0.48 +
        element.conductorTrait * 0.28 +
        element.catalystTrait * 0.10 +
        energyTrait * 0.14;

    addColonyStockpile(colony, resourceIndex, amount);
    colony.energyCapacity += mass * energyTrait * 0.018;
    colony.automation += mass * (element.conductorTrait + element.catalystTrait) * 0.00035;
    colony.infrastructure += mass * buildUtility * 0.0008;
    colony.marketAccess = std::min(1.0, colony.marketAccess + buildUtility * 0.006);

    if (!colony.constructionQueue.empty()) {
        ConstructionItem& item = colony.constructionQueue.front();
        item.progress += mass * (5.0 + buildUtility * 24.0);
    }
}

int pickFactionHome(const Cluster& cluster, const std::vector<int>& used) {
    int best = 0;
    double bestScore = -std::numeric_limits<double>::max();

    for (size_t i = 0; i < cluster.stars.size(); ++i) {
        if (std::find(used.begin(), used.end(), int(i)) != used.end()) continue;

        const ClusterStar& star = cluster.stars[i];
        double spacing = 1.0;
        if (!used.empty()) {
            spacing = 1e9;
            for (int usedStar : used) {
                spacing = std::min(spacing, distanceBetween(star, cluster.stars[usedStar]));
            }
        }

        const double score = (starPopulationWeight(star) * 6.6 + star.industry * 14.0 + star.habitability * 18.0) * (0.4 + spacing);
        if (score > bestScore) {
            bestScore = score;
            best = int(i);
        }
    }

    return best;
}

void claimInitialHoldings(Game& game, int factionIndex) {
    if (!validFaction(game, factionIndex)) return;

    const int home = game.factions[factionIndex].homeStar;
    if (!validStar(game, home)) return;

    setStarOwner(game, home, factionIndex);
    addColony(game, home, factionIndex, true);

    for (int claim = 0; claim < 3; ++claim) {
        int best = -1;
        double bestScore = -std::numeric_limits<double>::max();
        for (size_t i = 0; i < game.cluster.stars.size(); ++i) {
            const ClusterStar& star = game.cluster.stars[i];
            if (star.ownerFaction >= 0) continue;

            const double distance = distanceBetween(game.cluster.stars[home], star);
            const double score = star.habitability * 10.0 + star.industry * 2.5 + starPopulationWeight(star) * 1.6 - distance * 0.18;
            if (score > bestScore) {
                bestScore = score;
                best = int(i);
            }
        }
        if (best >= 0) {
            setStarOwner(game, best, factionIndex);
            addColony(game, best, factionIndex, false);
        }
    }
}

double nearestOwnedDistance(const Game& game, int starIndex, int factionIndex) {
    if (!validFaction(game, factionIndex) || !validStar(game, starIndex)) return 1e9;

    double best = 1e9;
    for (int owned : game.factions[factionIndex].controlledStars) {
        if (validStar(game, owned)) {
            const double routeDistance = cachedRouteDistance(game, starIndex, owned);
            best = std::min(best, routeDistance >= 0.0 ? routeDistance : distanceBetween(game.cluster.stars[starIndex], game.cluster.stars[owned]));
        }
    }
    return best;
}

int sampledStarCount(const Game& game, int smallWorldLimit, int largeWorldSamples) {
    const int count = int(game.cluster.stars.size());
    if (count <= smallWorldLimit) return count;
    return std::min(count, largeWorldSamples);
}

// ⚠️ ГЕНЕРАТОР ПЕРЕДАЁТСЯ ЯВНО (§2.3, §46). Выборка направлений тянет числа
// на каждой «мысли» планировщика, и для NPC это правильно — они и есть
// симуляция. Но тот же `findBestTrade` зовёт АВТОПИЛОТ ФЛОТА игрока (§35), а
// §35 писался узкой копией `updateTrader` ровно затем, чтобы не сдвигать
// глобальный поток. Одно обращение обошли, а 384 в год на борт остались:
// замер — два одинаковых мира, вся разница в поднятом флаге AUTO, через 60 лет
// расхождение 3445 Cr и разное следующее число `rng`. Соак-бейзлайны от этого
// становятся несравнимыми — ровно то, чего §35 боялся.
int sampledStarAt(const Game& game, int sampleIndex, int sampleCount, std::mt19937& gen) {
    const int count = int(game.cluster.stars.size());
    if (count <= sampleCount) return sampleIndex;
    return randomer(gen, count - 1);
}

int sampledStarAt(const Game& game, int sampleIndex, int sampleCount) {
    return sampledStarAt(game, sampleIndex, sampleCount, rng);
}

bool routeHasEdge(const std::vector<RouteEdge>& edges, int star) {
    for (size_t i = 0; i < edges.size(); ++i) {
        if (edges[i].star == star) return true;
    }
    return false;
}

void routeAddEdge(std::vector<std::vector<RouteEdge> >& graph, int a, int b, double distance) {
    if (a < 0 || b < 0 || a == b || a >= int(graph.size()) || b >= int(graph.size())) return;
    if (!routeHasEdge(graph[a], b)) graph[a].push_back(RouteEdge(b, distance));
    if (!routeHasEdge(graph[b], a)) graph[b].push_back(RouteEdge(a, distance));
}

double cachedRouteDistance(const Game& game, int originStar, int targetStar) {
    if (!validStar(game, originStar) || !validStar(game, targetStar)) return -1.0;
    if (originStar == targetStar) return 0.0;

    double distance = 0.0;
    int current = originStar;
    const int guardLimit = std::max(1, int(game.cluster.stars.size()));
    for (int guard = 0; guard < guardLimit && current != targetStar; ++guard) {
        const int next = game.routeNextStar(current, targetStar);
        if (!validStar(game, next) || next == current) {
            return distanceBetween(game.cluster.stars[originStar], game.cluster.stars[targetStar]);
        }
        distance += distanceBetween(game.cluster.stars[current], game.cluster.stars[next]);
        current = next;
    }

    if (current != targetStar) return distanceBetween(game.cluster.stars[originStar], game.cluster.stars[targetStar]);
    return distance;
}

double cachedRouteTravelTime(const Game& game, const Ship& ship, int originStar, int targetStar) {
    if (!validStar(game, originStar) || !validStar(game, targetStar)) return -1.0;
    if (originStar == targetStar) return 0.0;

    double years = 0.0;
    int current = originStar;
    const int guardLimit = std::max(1, int(game.cluster.stars.size()));
    for (int guard = 0; guard < guardLimit && current != targetStar; ++guard) {
        const int next = game.routeNextStar(current, targetStar);
        if (!validStar(game, next) || next == current) {
            return travelTimeEstimate(distanceBetween(game.cluster.stars[originStar], game.cluster.stars[targetStar]), ship);
        }
        years += travelTimeEstimate(distanceBetween(game.cluster.stars[current], game.cluster.stars[next]), ship);
        current = next;
    }
    return current == targetStar ? years : travelTimeEstimate(distanceBetween(game.cluster.stars[originStar], game.cluster.stars[targetStar]), ship);
}

// Запас 8% сверх расчёта — на манёвры и неточность оценки скорости.
// Больше делать бессмысленно: лишнее рабочее тело корабль везёт на себе и
// сжигает пропорционально, поэтому padding не улучшает запас хода.
const double ROUTE_MARGIN = 1.08;

// Плечо маршрута: расход считается по локальным ценам, чтобы движок выбирал
// экономически осмысленную скорость истечения (см. shipRouteCost).
RouteCost legCost(const Ship& ship, double distance, double propellantPrice, double fuelPrice) {
    RouteCost leg = shipEstimateRoute(ship, distance, propellantPrice, fuelPrice);
    leg.propellantMass *= ROUTE_MARGIN;
    leg.fuelMass *= ROUTE_MARGIN;
    return leg;
}

void addCost(RouteCost& total, const RouteCost& leg) {
    total.feasible = total.feasible && leg.feasible;
    total.propellantMass += leg.propellantMass;
    total.fuelMass += leg.fuelMass;
    total.exhaustVelocity = std::max(total.exhaustVelocity, leg.exhaustVelocity);
}

// Цены на топливо и рабочее тело корабля в конкретной системе, за единицу
// МАССЫ (расход теперь считается в массе, а не в штуках). Для смеси берём
// средневзвешенную цену её состава. Если система неизвестна — 1.0, тогда
// оптимум скорости истечения сводится к минимуму суммарной массы.
double mixPricePerMass(const Market& market, const std::vector<Resource>& mix, int fallbackElement) {
    double mass = 0.0;
    double cost = 0.0;
    for (size_t i = 0; i < mix.size(); ++i) {
        const int idx = elementIndex(mix[i].element);
        if (idx < 0 || idx >= int(market.prices.size())) continue;
        const double m = mix[i].amount * elementUnitMass(idx);
        mass += m;
        cost += mix[i].amount * market.prices[idx];
    }
    if (mass > 0.0) return std::max(0.01, cost / mass);
    if (fallbackElement >= 0 && fallbackElement < int(market.prices.size())) {
        return std::max(0.01, market.prices[fallbackElement] / std::max(1e-6, elementUnitMass(fallbackElement)));
    }
    return 1.0;
}

void routePrices(const Game& game, const Ship& ship, int starIndex,
                 double& propellantPrice, double& fuelPrice) {
    propellantPrice = 1.0;
    fuelPrice = 1.0;
    if (!validStar(game, starIndex)) return;
    const Market& market = game.markets[starIndex];
    propellantPrice = mixPricePerMass(market, ship.propellant, shipDominantPropellantElement(ship));
    fuelPrice = mixPricePerMass(market, ship.fuel, shipDominantFuelElement(ship));
}

RouteCost cachedRouteCost(const Game& game, const Ship& ship, int originStar, int targetStar,
                          double propellantPrice, double fuelPrice) {
    RouteCost total;
    total.feasible = true;
    if (!validStar(game, originStar) || !validStar(game, targetStar)) {
        total.feasible = false;
        return total;
    }
    if (originStar == targetStar) return total;

    const double directDistance = distanceBetween(game.cluster.stars[originStar], game.cluster.stars[targetStar]);
    int current = originStar;
    const int guardLimit = std::max(1, int(game.cluster.stars.size()));
    for (int guard = 0; guard < guardLimit && current != targetStar; ++guard) {
        const int next = game.routeNextStar(current, targetStar);
        if (!validStar(game, next) || next == current) {
            return legCost(ship, directDistance, propellantPrice, fuelPrice);
        }
        addCost(total, legCost(ship, distanceBetween(game.cluster.stars[current], game.cluster.stars[next]),
                               propellantPrice, fuelPrice));
        current = next;
    }
    if (current != targetStar) return legCost(ship, directDistance, propellantPrice, fuelPrice);
    return total;
}

bool shipCanFlyDirect(const Ship& ship, double distance) {
    if (distance < 0.0) return false;
    const RouteCost leg = legCost(ship, distance, 1.0, 1.0);
    if (!leg.feasible) return false;
    if (shipFuelMix(ship).mass + 1e-6 < leg.fuelMass) return false;
    if (!driveUsesFuelAsPropellant(ship.driveIndex) &&
        shipPropellantMix(ship).mass + 1e-6 < leg.propellantMass) return false;
    return true;
}

double plannedRouteDistance(const Game& game, const Ship& ship, int originStar, int targetStar) {
    if (!validStar(game, originStar) || !validStar(game, targetStar)) return -1.0;
    const double direct = distanceBetween(game.cluster.stars[originStar], game.cluster.stars[targetStar]);
    if (shipCanFlyDirect(ship, direct)) return direct;
    return cachedRouteDistance(game, originStar, targetStar);
}

double plannedRouteTravelTime(const Game& game, const Ship& ship, int originStar, int targetStar) {
    if (!validStar(game, originStar) || !validStar(game, targetStar)) return -1.0;
    const double direct = distanceBetween(game.cluster.stars[originStar], game.cluster.stars[targetStar]);
    if (shipCanFlyDirect(ship, direct)) return travelTimeEstimate(direct, ship);
    return cachedRouteTravelTime(game, ship, originStar, targetStar);
}

// Наценка станции за очистку и хранение: купить расходник можно только через
// неё, и это ровно то, что делает СОБСТВЕННУЮ заправку (привезти вещество
// грузом и перелить) вдвое выгоднее. Объявлена здесь, а не у `buyConsumable`,
// потому что оба планировщика маршрута обязаны считать дорогу по ней же.
const double REFINERY_MARKUP = 0.90;

// Во сколько кредитов обойдётся долить недостающее в этой системе. Считает
// ОБА расходника: и топливо, и рабочее тело, каждое по своей локальной цене.
//
// ⚠️ ПО СТАНЦИОННОЙ цене, а не по сырьевой. Купить расходник можно только
// через `buyConsumable`, а тот берёт `price * (1 + REFINERY_MARKUP)` — то есть
// вдвое. Пока здесь стояла голая рыночная цена, оба планировщика (совет игроку
// и `findBestTrade` у NPC) занижали дорогу примерно вдвое и звали в рейсы,
// которые сами же делали убыточными.
double refillCost(const Game& game, const Ship& ship, int starIndex, const RouteCost& need) {
    if (!need.feasible || !validStar(game, starIndex)) return 0.0;
    double propellantPrice = 1.0;
    double fuelPrice = 1.0;
    routePrices(game, ship, starIndex, propellantPrice, fuelPrice);
    double cost = std::max(0.0, need.fuelMass - shipFuelMix(ship).mass) * fuelPrice;
    if (!driveUsesFuelAsPropellant(ship.driveIndex)) {
        cost += std::max(0.0, need.propellantMass - shipPropellantMix(ship).mass) * propellantPrice;
    }
    return cost * (1.0 + REFINERY_MARKUP);
}

// (§46) КОРАБЛЬ, КАКИМ ОН ВЫЙДЕТ ИЗ ПОРТА: трюм под завязку, баки залиты тем,
// чем эта станция заправляет. Планировать надо по нему, а не по тому, что стоит
// у причала, — и это одна и та же грабля с двух концов:
//
//  • расход растёт вместе с массой, а игрок заправляется ДО закупки и летит
//    ПОСЛЕ неё. Расчёт по пустому корпусу съедал весь запас 1.5x ровно тем
//    грузом, который игрок купит следующим действием: один отказ «propellant
//    short» на десять рейсов, причём деньги за груз уже списаны;
//  • проходимость маршрута зависит от СОСТАВА баков, а не только от их объёма.
//    На пустом баке не проходима НИ ОДНА цель (замер: 0 из 40), и советчик
//    молчал — а бак пуст после каждого рейса. Получался круг: совет требует
//    топлива, топливо требует цели, цель даёт совет.
//
// Заливаем ёмкости целиком: это ВЕРХНЯЯ оценка массы, то есть ошибка в сторону
// осторожности, и она одинакова для всех целей — значит порядок целей не врёт.
// ⚠️ `fillHold` РАЗНЫЙ у двух потребителей, и это не небрежность.
//   • Заправка (`refuelTargets`) считает по ПОЛНОМУ трюму: она случается до
//     закупки, а лететь предстоит после неё — недолив дороже перелива.
//   • Совет (`playerBestRun`) считает по трюму КАК ЕСТЬ: сколько игрок реально
//     увезёт, решает его кошелёк, и у нищего это далеко не полный трюм.
//     Полный трюм в совете загонял `k` за стену достижимости (§12), и советчик
//     переставал находить хоть что-нибудь — замер: 0 рейсов из 25.
// Заливка ёмкостей на цену дороги при этом НЕ влияет: в `shipRouteCost`
// полезная нагрузка — это корпус и груз, а топливо решается из уравнения.
// Заливка нужна только чтобы у смеси появились состав и удельная энергия.
Ship shipAsItWillLeave(const Ship& ship, bool fillHold) {
    Ship out = ship;
    const double room = fillHold ? std::max(0.0, out.cargoCapacity - shipCargoMass(out)) : 0.0;
    if (room > 1e-9 && elementCount() > 0) {
        // Чем именно набит трюм, Циолковскому безразлично — важна масса.
        const double unitMass = std::max(1e-9, resourceUnitMassByIndex(0));
        out.cargo.push_back(Resource(elementDefinitions()[0].symbol, room / unitMass));
    }
    for (int pass = 0; pass < 2; ++pass) {
        const bool bunker = pass == 0;
        if (!bunker && driveUsesFuelAsPropellant(out.driveIndex)) continue;
        const int element = bunker ? shipDominantFuelElement(out) : shipDominantPropellantElement(out);
        if (element < 0 || element >= int(elementCount())) continue;
        const MixSummary mix = bunker ? shipFuelMix(out) : shipPropellantMix(out);
        const double capacity = bunker ? shipFuelTankVolume(out) : out.propellantVolume;
        const double units = std::max(0.0, capacity - mix.volume) /
                             std::max(1e-9, elementUnitVolume(element));
        if (units <= 1e-9) continue;
        const std::string symbol = elementDefinitions()[size_t(element)].symbol;
        std::vector<Resource>& dest = bunker ? out.fuel : out.propellant;
        bool merged = false;
        for (size_t i = 0; i < dest.size(); ++i) {
            if (dest[i].element != symbol) continue;
            dest[i].amount += units;
            merged = true;
            break;
        }
        if (!merged) dest.push_back(Resource(symbol, units));
        out.cruiseExhaust = 0.0;   // состав сменился — рабочую точку подобрать заново
    }
    return out;
}

// Тот же корабль с ЗАДАННОЙ загрузкой трюма. Пишет в переданный буфер, чтобы
// не копировать вектора на каждом шаге перебора объёма: перебор идёт
// цели × элементы × 10 ступеней, и копия там стоила бы дороже самого счёта.
const Ship& shipCarrying(const Ship& base, int elementIndex, double cargoMass, Ship& scratch) {
    scratch = base;
    scratch.cargo.clear();
    if (cargoMass > 1e-9 && elementIndex >= 0 && elementIndex < int(elementCount())) {
        const double unitMass = std::max(1e-9, resourceUnitMassByIndex(elementIndex));
        scratch.cargo.push_back(Resource(elementDefinitions()[size_t(elementIndex)].symbol,
                                         cargoMass / unitMass));
    }
    return scratch;
}

// ПОЛНАЯ цена дороги: то, что СГОРИТ, по станционной цене — а не недостача до
// маршрута, как считает `refillCost`.
//
// ⚠️ Разница не косметическая. Игрок, нажавший рекомендованную новеллой кнопку
// заправки ДО вопроса советчику (реплика 14 — «в порту короткий ответ — это
// кнопка», реплики 10–11 — «спроси совет»), получал дорогу «за ноль»: недостача
// уже покрыта, значит по `refillCost` рейс ничего не стоит. Совет выбирал рейсы,
// которые сам же и делал убыточными: замер по 69 рейсам — расходники съедают
// 16% валовой прибыли, три рейса в чистый минус, худший −15 154 Cr.
double burnCost(const Game& game, const Ship& ship, int starIndex, const RouteCost& need) {
    if (!need.feasible || !validStar(game, starIndex)) return 0.0;
    double propellantPrice = 1.0;
    double fuelPrice = 1.0;
    routePrices(game, ship, starIndex, propellantPrice, fuelPrice);
    double cost = need.fuelMass * fuelPrice;
    if (!driveUsesFuelAsPropellant(ship.driveIndex)) {
        cost += need.propellantMass * propellantPrice;
    }
    return cost * (1.0 + REFINERY_MARKUP);
}

RouteCost plannedRouteCost(const Game& game, const Ship& ship, int originStar, int targetStar) {
    RouteCost bad;
    if (!validStar(game, originStar) || !validStar(game, targetStar)) return bad;
    double propellantPrice = 1.0;
    double fuelPrice = 1.0;
    routePrices(game, ship, originStar, propellantPrice, fuelPrice);
    const double direct = distanceBetween(game.cluster.stars[originStar], game.cluster.stars[targetStar]);
    if (shipCanFlyDirect(ship, direct)) return legCost(ship, direct, propellantPrice, fuelPrice);
    return cachedRouteCost(game, ship, originStar, targetStar, propellantPrice, fuelPrice);
}

int nearestSignalRelay(const Game& game, int factionIndex, int originStar) {
    if (!validFaction(game, factionIndex) || !validStar(game, originStar)) return -1;

    if (factionIndex == game.playerFaction && game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
        const Agent& player = game.agents[game.playerAgent];
        if (!player.ship.enRoute && validStar(game, player.currentStar)) return player.currentStar;
    }

    int best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (int starIndex : game.factions[factionIndex].controlledStars) {
        if (!validStar(game, starIndex)) continue;
        const double distance = distanceBetween(game.cluster.stars[originStar], game.cluster.stars[starIndex]);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = starIndex;
        }
    }
    if (best >= 0) return best;
    return validStar(game, game.factions[factionIndex].homeStar) ? game.factions[factionIndex].homeStar : originStar;
}

bool startSignalRoute(const Game& game, SignalPacket& signal, int originStar, int destinationStar, double now) {
    if (!validStar(game, originStar) || !validStar(game, destinationStar)) return false;
    signal.originStar = originStar;
    signal.destinationStar = destinationStar;
    signal.sendTime = now;
    if (originStar == destinationStar) {
        signal.hopStar = destinationStar;
        signal.arrivalTime = now;
        return true;
    }

    int hop = game.routeNextStar(originStar, destinationStar);
    if (!validStar(game, hop) || hop == originStar) hop = destinationStar;
    signal.hopStar = hop;
    signal.arrivalTime = now + distanceBetween(game.cluster.stars[originStar], game.cluster.stars[hop]);
    return true;
}

bool forwardSignalRoute(const Game& game, SignalPacket& signal, double now) {
    if (!validStar(game, signal.destinationStar) || !validStar(game, signal.hopStar)) return false;
    const int currentStar = signal.hopStar;
    if (currentStar == signal.destinationStar) return false;

    int next = game.routeNextStar(currentStar, signal.destinationStar);
    if (!validStar(game, next) || next == currentStar) next = signal.destinationStar;
    signal.sendTime = now;
    signal.hopStar = next;
    signal.arrivalTime = now + distanceBetween(game.cluster.stars[currentStar], game.cluster.stars[next]);
    return true;
}

bool signalArrivalLess(const SignalPacket& a, const SignalPacket& b) {
    if (a.arrivalTime != b.arrivalTime) return a.arrivalTime < b.arrivalTime;
    if (a.observedTime != b.observedTime) return a.observedTime < b.observedTime;
    return a.subjectStar < b.subjectStar;
}

bool signalArrivalBeforeTime(const SignalPacket& signal, double arrivalTime) {
    return signal.arrivalTime < arrivalTime;
}

bool signalCanDedupe(const SignalPacket& signal) {
    return signal.type == SignalType::OwnerReport ||
        signal.type == SignalType::MarketReport ||
        signal.type == SignalType::ContractReport ||
        signal.type == SignalType::DiplomacyReport ||
        ((signal.type == SignalType::CombatReport || signal.type == SignalType::SettlementReport) && signal.eventId != 0);
}

bool signalDedupeKeyMatches(const SignalPacket& a, const SignalPacket& b) {
    if (a.type != b.type ||
        a.recipientFaction != b.recipientFaction ||
        a.destinationStar != b.destinationStar ||
        a.hopStar != b.hopStar) {
        return false;
    }
    if (a.eventId != 0 || b.eventId != 0) return a.eventId == b.eventId;
    if (a.type == SignalType::ContractReport) return a.contractId == b.contractId;
    if (a.type == SignalType::DiplomacyReport) return a.targetFaction == b.targetFaction;
    return a.subjectStar == b.subjectStar;
}

bool signalDominates(const SignalPacket& newer, const SignalPacket& older) {
    const double eps = 0.000001;
    return newer.observedTime + eps >= older.observedTime &&
        newer.arrivalTime <= older.arrivalTime + eps;
}

void sortPendingSignals(std::vector<SignalPacket>& signals) {
    std::stable_sort(signals.begin(), signals.end(), signalArrivalLess);
}

unsigned long long allocateSignalEventId(Game& game) {
    if (game.nextSignalEventId == 0) game.nextSignalEventId = 1;
    const unsigned long long id = game.nextSignalEventId;
    game.nextSignalEventId += 1;
    if (game.nextSignalEventId == 0) game.nextSignalEventId = 1;
    return id;
}

void enqueuePendingSignal(Game& game, const SignalPacket& queued) {
    if (!signalCanDedupe(queued)) {
        std::vector<SignalPacket>::iterator pos = std::lower_bound(
            game.pendingSignals.begin(), game.pendingSignals.end(), queued.arrivalTime, signalArrivalBeforeTime);
        game.pendingSignals.insert(pos, queued);
        return;
    }

    for (std::vector<SignalPacket>::iterator it = game.pendingSignals.begin(); it != game.pendingSignals.end(); ) {
        if (!signalDedupeKeyMatches(*it, queued)) {
            ++it;
            continue;
        }
        if (signalDominates(*it, queued)) return;
        if (signalDominates(queued, *it)) {
            it = game.pendingSignals.erase(it);
            continue;
        }
        ++it;
    }

    std::vector<SignalPacket>::iterator pos = std::lower_bound(
        game.pendingSignals.begin(), game.pendingSignals.end(), queued.arrivalTime, signalArrivalBeforeTime);
    game.pendingSignals.insert(pos, queued);
}

bool signalMemoryMatches(const SignalMemoryRecord& record, const SignalPacket& signal) {
    if (record.type != signal.type ||
        record.recipientFaction != signal.recipientFaction ||
        record.subjectStar != signal.subjectStar) {
        return false;
    }
    if (record.eventId != 0 || signal.eventId != 0) return record.eventId == signal.eventId;
    if (signal.type == SignalType::ContractReport) return record.contractId == signal.contractId;
    if (signal.type == SignalType::CombatReport) {
        return record.sourceAgent == signal.sourceAgent && record.targetAgent == signal.targetAgent;
    }
    if (signal.type == SignalType::SettlementReport) return false;
    if (signal.type == SignalType::DiplomacyReport) return record.targetFaction == signal.targetFaction;
    return true;
}

void fillSignalMemoryRecord(SignalMemoryRecord& record, const SignalPacket& signal) {
    const bool preserveSettlementAbsorbed =
        record.type == SignalType::SettlementReport &&
        signal.type == SignalType::SettlementReport &&
        record.eventId != 0 &&
        record.eventId == signal.eventId &&
        record.absorbed;
    record.type = signal.type;
    record.eventId = signal.eventId;
    record.recipientFaction = signal.recipientFaction;
    record.subjectStar = signal.subjectStar;
    record.destinationStar = signal.destinationStar;
    record.sourceAgent = signal.sourceAgent;
    record.targetAgent = signal.targetAgent;
    record.sourceFaction = signal.sourceFaction;
    record.targetFaction = signal.targetFaction;
    record.ownerFaction = signal.ownerFaction;
    record.contractId = signal.contractId;
    record.observedTime = signal.observedTime;
    record.amount = signal.amount;
    record.relationValue = signal.relationValue;
    record.contractType = signal.contractType;
    record.contractOriginStar = signal.contractOriginStar;
    record.contractTargetStar = signal.contractTargetStar;
    record.contractTargetAgent = signal.contractTargetAgent;
    record.contractResource = signal.contractResource;
    record.contractAcceptedByAgent = signal.contractAcceptedByAgent;
    record.contractAmount = signal.contractAmount;
    record.contractReward = signal.contractReward;
    record.contractDeposit = signal.contractDeposit;
    record.contractPostedTime = signal.contractPostedTime;
    record.contractDeadline = signal.contractDeadline;
    record.contractRisk = signal.contractRisk;
    record.contractProgress = signal.contractProgress;
    record.contractCompleted = signal.contractCompleted;
    record.contractFailed = signal.contractFailed;
    record.absorbed = preserveSettlementAbsorbed;
    record.marketPrices = signal.marketPrices;
    record.marketSupplyPressure = signal.marketSupplyPressure;
    record.marketDemandPressure = signal.marketDemandPressure;
    if (!signal.marketSupplyPressure.empty()) record.averageSupplyPressure = averageValue(signal.marketSupplyPressure);
    if (!signal.marketDemandPressure.empty()) record.averageDemandPressure = averageValue(signal.marketDemandPressure);
}

void mergeSignalAtStar(Game& game, int starIndex, const SignalPacket& signal) {
    if (!validStar(game, starIndex)) return;
    if (game.signalMemory.size() != game.cluster.stars.size()) {
        game.signalMemory.assign(game.cluster.stars.size(), std::vector<SignalMemoryRecord>());
    }

    std::vector<SignalMemoryRecord>& memory = game.signalMemory[size_t(starIndex)];
    for (SignalMemoryRecord& record : memory) {
        if (!signalMemoryMatches(record, signal)) continue;
        if (record.observedTime <= signal.observedTime) fillSignalMemoryRecord(record, signal);
        return;
    }

    if (memory.size() >= size_t(SIGNAL_MEMORY_PER_STAR)) {
        size_t oldest = 0;
        for (size_t i = 1; i < memory.size(); ++i) {
            if (memory[i].observedTime < memory[oldest].observedTime) oldest = i;
        }
        memory[oldest] = SignalMemoryRecord();
        fillSignalMemoryRecord(memory[oldest], signal);
        return;
    }

    memory.push_back(SignalMemoryRecord());
    fillSignalMemoryRecord(memory.back(), signal);
}

const SignalMemoryRecord* latestSignalMemoryRecord(const Game& game, int observerStar, SignalType type, int factionIndex, int subjectStar) {
    if (!validFaction(game, factionIndex) || !validStar(game, observerStar) || !validStar(game, subjectStar)) return nullptr;
    if (observerStar < 0 || observerStar >= int(game.signalMemory.size())) return nullptr;

    const SignalMemoryRecord* best = nullptr;
    const std::vector<SignalMemoryRecord>& memory = game.signalMemory[size_t(observerStar)];
    for (const SignalMemoryRecord& record : memory) {
        if (record.type != type ||
            record.recipientFaction != factionIndex ||
            record.subjectStar != subjectStar ||
            record.observedTime < 0.0) {
            continue;
        }
        if (!best || record.observedTime > best->observedTime) best = &record;
    }
    return best;
}

int factionObserverStar(const Game& game, int factionIndex) {
    if (!validFaction(game, factionIndex)) return -1;
    if (factionIndex == game.playerFaction && game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
        const Agent& player = game.agents[game.playerAgent];
        if (!player.ship.enRoute && validStar(game, player.currentStar)) return player.currentStar;
    }
    if (validStar(game, game.factions[factionIndex].homeStar)) return game.factions[factionIndex].homeStar;
    if (!game.factions[factionIndex].controlledStars.empty() && validStar(game, game.factions[factionIndex].controlledStars[0])) {
        return game.factions[factionIndex].controlledStars[0];
    }
    return -1;
}

bool signalThreatPiracy(const Game& game, const SignalMemoryRecord& record) {
    return record.sourceAgent >= 0 && record.sourceAgent < int(game.agents.size()) &&
        agentIsPiracyThreat(game.agents[record.sourceAgent]);
}

double signalThreatHostility(const Game& game, int factionIndex, const SignalMemoryRecord& record) {
    if (!validFaction(game, factionIndex) || !validFaction(game, record.sourceFaction)) return 1.0;
    const int relation = game.factionRelation(factionIndex, record.sourceFaction);
    return relation < 0 ? 1.0 + double(-relation) / 128.0 : 0.55;
}

double signalThreatConfidence(const Game& game, const SignalMemoryRecord& record, double tau) {
    if (record.observedTime < 0.0) return 0.0;
    return std::exp(-std::max(0.0, game.time - record.observedTime) / tau);
}

double signalThreatValue(const Game& game, int factionIndex, const SignalMemoryRecord& record) {
    return (record.amount * 0.065 + record.relationValue * 0.00010) *
        signalThreatHostility(game, factionIndex, record) *
        signalThreatConfidence(game, record, 10.0);
}

bool usableThreatSignal(const Game& game, int factionIndex, const SignalMemoryRecord& record, bool requirePiracy) {
    if (record.type != SignalType::CombatReport || record.recipientFaction != factionIndex) return false;
    if (record.observedTime < 0.0 || game.time - record.observedTime > 24.0) return false;
    if (!validStar(game, record.subjectStar) || record.amount <= 0.0) return false;
    if (requirePiracy && !signalThreatPiracy(game, record)) return false;
    return true;
}

void applyLocalFactionReports(Game& game, int factionIndex, int observerStar) {
    if (!validFaction(game, factionIndex) || !validStar(game, observerStar)) return;
    if (observerStar < 0 || observerStar >= int(game.signalMemory.size())) return;

    std::vector<SignalMemoryRecord>& memory = game.signalMemory[size_t(observerStar)];
    for (SignalMemoryRecord& record : memory) {
        if (record.recipientFaction != factionIndex || record.observedTime < 0.0) continue;
        const double age = std::max(0.0, game.time - record.observedTime);
        if (record.type == SignalType::OwnerReport && validStar(game, record.subjectStar)) {
            game.applyOwnerKnowledge(factionIndex, record.subjectStar, record.ownerFaction, record.observedTime,
                record.subjectStar == observerStar);
        } else if (record.type == SignalType::MarketReport && validStar(game, record.subjectStar) && !record.marketPrices.empty()) {
            game.applyMarketKnowledge(factionIndex, record.subjectStar, record.marketPrices,
                record.marketSupplyPressure, record.marketDemandPressure, record.observedTime);
        } else if (record.type == SignalType::SettlementReport) {
            if (record.destinationStar != observerStar) continue;
            if (!record.absorbed) {
                game.factions[factionIndex].estimatedTreasury = std::max(0.0,
                    game.factions[factionIndex].estimatedTreasury + record.amount);
                record.absorbed = true;
            }
        } else if (record.type == SignalType::CombatReport && age <= 24.0) {
            game.factions[factionIndex].raidPressure = std::max(game.factions[factionIndex].raidPressure,
                std::min(1.0, record.amount / 900.0) * signalThreatConfidence(game, record, 14.0));
        } else if (record.type == SignalType::DiplomacyReport && age <= 36.0 && validFaction(game, record.targetFaction)) {
            const double delta = std::abs(double(record.relationValue - game.factionRelation(factionIndex, record.targetFaction)));
            game.factions[factionIndex].diplomacyPressure += std::min(1.0, delta / 128.0) * signalThreatConfidence(game, record, 18.0);
        }
    }
}

bool activeContract(const Contract& contract) {
    return !contract.completed && !contract.failed;
}

int activeContractsAtOrigin(const Game& game, int originStar) {
    int count = 0;
    for (const Contract& contract : game.contracts) {
        if (activeContract(contract) && contract.acceptedByAgent < 0 && contract.originStar == originStar) count += 1;
    }
    return count;
}

bool targetAgentHasActiveContract(const Game& game, int targetAgent, ContractType type) {
    for (const Contract& contract : game.contracts) {
        if (activeContract(contract) && contract.type == type && contract.targetAgent == targetAgent) return true;
    }
    return false;
}

ThreatCandidate bestThreatReportAt(const Game& game, int factionIndex, int originStar, bool requirePiracy) {
    ThreatCandidate best;
    double bestScore = 0.0;
    if (!validStar(game, originStar) || originStar >= int(game.signalMemory.size())) return best;

    const std::vector<SignalMemoryRecord>& memory = game.signalMemory[size_t(originStar)];
    for (const SignalMemoryRecord& record : memory) {
        if (!usableThreatSignal(game, factionIndex, record, requirePiracy)) continue;
        if (record.sourceAgent < 0 || record.sourceAgent >= int(game.agents.size())) continue;
        if (targetAgentHasActiveContract(game, record.sourceAgent, ContractType::Bounty)) continue;

        const Agent& source = game.agents[record.sourceAgent];
        if (source.type != "pirate" && requirePiracy) continue;
        if (!validStar(game, record.subjectStar)) continue;

        const double distance = distanceBetween(game.cluster.stars[originStar], game.cluster.stars[record.subjectStar]);
        const int relation = validFaction(game, factionIndex) ? game.factionRelation(factionIndex, source.ship.ownerFaction) : -20;
        const double hostility = relation < 0 ? double(-relation) / 128.0 : 0.08;
        const double agePenalty = std::min(10.0, (game.time - record.observedTime) * 0.4);
        const double score = record.amount * (0.65 + hostility) + record.relationValue * 0.01 - distance * 0.08 - agePenalty;
        if (score > bestScore) {
            bestScore = score;
            best.valid = true;
            best.starIndex = record.subjectStar;
            best.sourceAgent = record.sourceAgent;
            best.sourceFaction = source.ship.ownerFaction;
            best.observedAt = record.observedTime;
            best.threatValue = record.amount;
            best.cargoValue = record.relationValue;
            best.piracy = signalThreatPiracy(game, record);
        }
    }
    return best;
}

double localEscortRisk(const Game& game, int factionIndex, int starIndex) {
    double risk = 0.0;
    if (!validStar(game, starIndex) || starIndex >= int(game.signalMemory.size())) return risk;
    const std::vector<SignalMemoryRecord>& memory = game.signalMemory[size_t(starIndex)];
    for (const SignalMemoryRecord& record : memory) {
        if (!usableThreatSignal(game, factionIndex, record, true)) continue;
        const double age = game.time - record.observedTime;
        if (age > 16.0) continue;
        const double distance = distanceBetween(game.cluster.stars[starIndex], game.cluster.stars[record.subjectStar]);
        if (distance > 18.0) continue;
        const double relation = validFaction(game, factionIndex) && validFaction(game, record.sourceFaction) ?
            game.factionRelation(factionIndex, record.sourceFaction) : -20;
        const double hostility = relation < 0 ? 1.0 + double(-relation) / 128.0 : 0.8;
        risk += record.amount * hostility * signalThreatConfidence(game, record, 10.0) / (distance + 2.0);
    }
    return risk;
}

Contract* contractById(Game& game, int contractId) {
    for (Contract& contract : game.contracts) {
        if (contract.id == contractId) return &contract;
    }
    return nullptr;
}

const Contract* contractById(const Game& game, int contractId) {
    for (const Contract& contract : game.contracts) {
        if (contract.id == contractId) return &contract;
    }
    return nullptr;
}

void fillSignalContractSnapshot(SignalPacket& signal, const Contract& contract) {
    signal.contractId = contract.id;
    signal.contractType = contract.type;
    signal.contractOriginStar = contract.originStar;
    signal.contractTargetStar = contract.targetStar;
    signal.contractTargetAgent = contract.targetAgent;
    signal.contractResource = contract.resource;
    signal.contractAcceptedByAgent = contract.acceptedByAgent;
    signal.contractAmount = contract.amount;
    signal.contractReward = contract.reward;
    signal.contractDeposit = contract.deposit;
    signal.contractPostedTime = contract.postedTime;
    signal.contractDeadline = contract.deadline;
    signal.contractRisk = contract.risk;
    signal.contractProgress = contract.progress;
    signal.contractCompleted = contract.completed;
    signal.contractFailed = contract.failed;
}

Contract contractFromSignalRecord(const SignalMemoryRecord& record) {
    Contract contract;
    contract.id = record.contractId;
    contract.type = record.contractType;
    contract.issuerFaction = record.sourceFaction;
    contract.originStar = record.contractOriginStar;
    contract.targetStar = record.contractTargetStar;
    contract.targetAgent = record.contractTargetAgent;
    contract.resource = record.contractResource;
    contract.amount = record.contractAmount;
    contract.reward = record.contractReward;
    contract.deposit = record.contractDeposit;
    contract.postedTime = record.contractPostedTime;
    contract.deadline = record.contractDeadline;
    contract.risk = record.contractRisk;
    contract.progress = record.contractProgress;
    contract.acceptedByAgent = record.contractAcceptedByAgent;
    contract.completed = record.contractCompleted;
    contract.failed = record.contractFailed;
    return contract;
}

bool contractListHasId(const std::vector<Contract>& contracts, int contractId) {
    for (const Contract& contract : contracts) {
        if (contract.id == contractId) return true;
    }
    return false;
}

Ship contractRouteShip(const Agent& agent, const Contract& contract) {
    Ship routeShip = agent.ship;
    if (contract.acceptedByAgent < 0 && contractUsesCargo(contract.type) &&
        contract.resource >= 0 && contract.resource < int(elementCount()) && contract.amount > 0.0) {
        routeShip.cargo.clear();
        // ⚠️ Больше СВОЕГО трюма этот корабль не повезёт: остальное уедет на
        // других бортах каравана (§24). Без клампа прогноз крупного заказа
        // считался бы для корабля с сорокакратным перегрузом — ETA уходила в
        // бесконечность, и на доске крупный заказ выглядел нелетающим.
        const double unitMass = std::max(0.001, resourceUnitMassByIndex(contract.resource));
        const double share = std::min(contract.amount, routeShip.cargoCapacity / unitMass);
        routeShip.cargo.emplace_back(elementDefinitions()[contract.resource].symbol, share);
    }
    return routeShip;
}

Contract* activeContractForAgent(Game& game, int agentIndex) {
    for (Contract& contract : game.contracts) {
        if (activeContract(contract) && contract.acceptedByAgent == agentIndex) return &contract;
    }
    return nullptr;
}

// Штраф за провал (§24). В отличие от успеха (всегда +1) он считается ПО
// РАЗМЕРУ заказа: `2 * sqrt(масса / базовая масса)`. Корень здесь не украшение,
// а единственное, что делает шкалу пригодной: масса растёт в 6667 раз от низа
// к верху, и без него сорванный топовый заказ обнулял бы вообще всё.
//
// Замер по формуле: базовый (145 т) −3, крупный (30 000 т) −45, топовый
// (400 000 т) −163 сданных заказа. Отсюда и вся острота верха лестницы:
// репутация, которую копил тысячу заказов, ставится на кон каждым рейсом.
double applyContractFailureReputation(Game& game, const Contract& contract) {
    if (!validFaction(game, contract.issuerFaction)) return 0.0;
    const double mass = std::max(JOB_CARGO_BASE, game.contractCargoMass(contract));
    const double loss = REPUTATION_FAIL_FACTOR * std::sqrt(mass / JOB_CARGO_BASE);
    game.resizeFactionReputation();
    double& reputation = game.factionReputation[size_t(contract.issuerFaction)];
    const double before = reputation;
    reputation = std::max(0.0, reputation - loss);
    return before - reputation;
}

// Надбавка за ДОСРОЧНУЮ сдачу (§24). Считается от доли срока, оставшейся к
// моменту сдачи: привёз «в ноль времени» — надбавки нет, привёз вдвое быстрее —
// половина максимума. Гнать становится осмысленно: хороший корпус и точный
// расчёт маршрута окупаются деньгами, а не только спокойствием.
double contractEarlyBonus(const Game& game, const Contract& contract) {
    const double window = contract.deadline - contract.postedTime;
    if (window <= 0.0) return 0.0;
    const double leftShare = (contract.deadline - game.time) / window;
    return JOB_EARLY_BONUS_MAX * std::max(0.0, std::min(1.0, leftShare));
}

bool payContractReward(Game& game, Contract& contract, Agent& agent, bool emitSignals) {
    if (!activeContract(contract)) return false;
    const bool late = game.time > contract.deadline;
    const double lateFactor = late ? CONTRACT_LATE_FACTOR : 1.0;
    // Просрочка и досрочность взаимоисключающи — одно из двух всегда 0.
    const double earlyBonus = late ? 0.0 : contractEarlyBonus(game, contract);
    // (§46) ЗАКАЗЧИК ОПЛАЧИВАЕТ ДОРОГУ — но только привёзшему В СРОК. Ставка
    // `CONTRACT_PAY_PER_YEAR` откалибрована (§23) против свободного рейса
    // НИЩЕГО игрока, а сгорающее дорожает вместе с корпусом: замер по шести
    // мирам — 45 строк из 48 (94%) убыточны по одному топливу, типичная
    // «награда 1 081 Cr, сгорит на 4 643». Надбавка не делает заказ выгоднее
    // свободного рейса — она перестаёт делать его заведомо убыточным.
    //
    // ⚠️ Просрочка съедает надбавку ЦЕЛИКОМ, а не режется `CONTRACT_LATE_FACTOR`:
    // компенсация — это часть уговора «в срок», иначе опоздавший везёт даром, но
    // хотя бы за счёт заказчика, и срок перестаёт что-либо значить.
    const double fuelPaid = late ? 0.0 : std::max(0.0, contract.fuelAllowance);
    const double payout = contract.reward * lateFactor * (1.0 + earlyBonus) + fuelPaid;
    agent.money += payout;
    // ⚠️ Отдельного списания надбавки ЗДЕСЬ НЕТ И БЫТЬ НЕ ДОЛЖНО: она уже внутри
    // `payout`, а `payout` списывается с казны плательщика в конце функции.
    // Первая версия §46 списывала обе половины, и казна теряла ровно вдвое
    // больше надбавки (замер: игроку +5607, из казны −9932 при надбавке 4325).
    // Это ровно класс §44 — добавлена одна половина проводки, а вторая уже была.
    // (§37.3) Залог возвращается ЦЕЛИКОМ и при просрочке тоже: он платой за
    // опоздание не является — за опоздание режется награда (CONTRACT_LATE_FACTOR).
    const double refund = agent.playerControlled ? contract.deposit : 0.0;
    if (refund > 0.0) {
        agent.money += refund;
        if (validFaction(game, contract.issuerFaction)) {
            game.factions[size_t(contract.issuerFaction)].treasury =
                std::max(0.0, game.factions[size_t(contract.issuerFaction)].treasury - refund);
        }
        game.journalExplained += refund;
    }

    // Репутация. УСПЕХ ВСЕГДА +1, независимо от размера: единица шкалы — один
    // сданный заказ, и потолок честно стоит своей тысячи (§24). Асимметрия
    // живёт на другой стороне — в штрафе за провал, который считается по массе.
    if (agent.playerControlled && validFaction(game, contract.issuerFaction)) {
        game.resizeFactionReputation();
        double& reputation = game.factionReputation[size_t(contract.issuerFaction)];
        reputation = std::min(REPUTATION_CAP_JOBS, reputation + 1.0);
    }

    if (agent.playerControlled) {
        char paid[64];
        std::snprintf(paid, sizeof(paid), " +%.0F Cr", payout);
        std::string mark;
        if (late) {
            mark = " LATE";
        } else if (earlyBonus > 0.01) {
            char early[32];
            std::snprintf(early, sizeof(early), " EARLY +%.0F%%", earlyBonus * 100.0);
            mark = early;
        }
        if (fuelPaid > 0.01) {
            char fuelNote[40];
            std::snprintf(fuelNote, sizeof(fuelNote), " FUEL +%.0F", fuelPaid);
            mark += fuelNote;
        }
        game.pushJournal(JournalKind::JobCompleted,
            game.contractJournalText(contract) + mark + paid, payout, agent.currentStar);
        game.journalExplained += payout;
    }
    agent.lastProfit = payout;
    agent.trades += 1;
    agent.lastAction = lateFactor < 1.0 ? "late contract" : std::string("completed ") + contractTypeLabel(contract.type);
    contract.completed = true;
    contract.reportSignalPending = false;
    contract.reportDelivered = contract.reportDelivered || contract.type == ContractType::Scout;
    // ⚠️ ПЛАТЕЛЬЩИК ЕСТЬ ВСЕГДА. `issuerFaction` берётся у владельца целевой
    // звезды, а владельца имеют 1.65% звёзд — значит у 46–64% живых заказов
    // заказчика НЕТ, и награда прилетала игроку из ниоткуда. Надбавка на дорогу
    // (§46) увеличила эту печать восьмикратно: 8 407 Cr за штуку при награде
    // 981. Заказ без заказчика идёт через КЛИРИНГОВУЮ ПАЛАТУ (§18) — тот самый
    // банк, который собирает лицензионные деньги и субсидирует колонии.
    const int payer = validFaction(game, contract.issuerFaction) ? contract.issuerFaction
                                                                 : game.clearingFaction;
    if (validFaction(game, payer)) {
        game.factions[payer].treasury = std::max(0.0, game.factions[payer].treasury - payout);
        if (emitSignals) game.queueSettlementSignal(payer, contract.targetStar, -payout);
    }
    if (emitSignals) game.queueContractSignal(contract.issuerFaction, contract.id, contract.targetStar, contract.targetStar);
    game.lastEvent = "contract completed";
    return true;
}

void publishContractPosting(Game& game, const Contract& contract) {
    game.queueContractSignal(contract.issuerFaction, contract.id, contract.originStar, contract.targetStar);
    if (validFaction(game, game.playerFaction) && contract.issuerFaction != game.playerFaction) {
        game.queueContractSignal(game.playerFaction, contract.id, contract.originStar, contract.targetStar);
    }
}

int pickFactionOrderTarget(const Game& game, int factionIndex, FactionOrderType type) {
    if (!validFaction(game, factionIndex)) return -1;
    int best = -1;
    double bestPriority = 0.0;
    const std::vector<FactionOrder>& orders = game.factions[factionIndex].orders;
    for (const FactionOrder& order : orders) {
        if (order.completed || order.type != type || !validStar(game, order.targetStar)) continue;
        if (order.priority > bestPriority) {
            bestPriority = order.priority;
            best = order.targetStar;
        }
    }
    return best;
}

// Что ЦЕЛЬ реально просит, а отправитель может дать (§24).
//
// ⚠️ Это замена прежнего `pickSurplusResource`, и замена принципиальная. Тот
// выбирал груз по формуле `(1.18 - давление) * sqrt(запас) / удельная масса` —
// то есть делил на массу и потому почти всегда выдавал ВОДОРОД и лёгкие газы.
// Заказ был не «системе нужен вольфрам», а «у нас завалялось что полегче»:
// доска не имела никакого отношения к тому, чем живут рынки, и это ровно тот
// перекос плотности стоимости, что описан в §20.3-A.
//
// Теперь ведущая величина — НУЖДА ЦЕЛИ: годовое потребление (`demandRate`),
// незакрытый спрос, ценовое давление и общая задыхаемость системы (`strain`).
// Масса из выбора убрана совсем: сколько везти — решает тир (§24), а не то,
// что легче поднять. Отправитель проверяется одним условием — хватает ли у
// него запаса, чтобы столько отгрузить.
int pickNeededResource(const Game& game, int originStar, int targetStar, double wantedMass) {
    if (!validStar(game, originStar) || originStar >= int(game.markets.size())) return -1;
    if (!validStar(game, targetStar) || targetStar >= int(game.markets.size())) return -1;

    const Market& origin = game.markets[originStar];
    const Market& target = game.markets[targetStar];
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    const double strainBoost = 1.0 + target.strain * 2.0;

    int best = -1;
    double bestScore = 0.0;
    for (size_t i = 0; i < target.prices.size() && i < elements.size(); ++i) {
        const double reference = marketReferencePrice(int(i));
        if (reference <= 0.0) continue;
        const double unitMass = std::max(0.001, resourceUnitMassByIndex(int(i)));
        // Отгрузить надо ПО МАССЕ, а запас на рынке — в единицах. Заказ не
        // должен вычищать склад отправителя: берём не больше пятой части.
        const double unitsWanted = wantedMass / unitMass;
        if (origin.supply[i].amount < unitsWanted * 5.0) continue;

        const double pressure = target.prices[i] / reference;
        if (pressure < 1.02) continue;              // цель этим не бедствует
        const double consumption = i < target.demandRate.size() ? target.demandRate[i] : 0.0;
        const double unmet = i < target.demand.size() ? target.demand[i].amount : 0.0;
        // Голод считается в МАССЕ, а не в молях: система просит тонны, а не
        // штуки, и без этого лёгкие газы снова перевесили бы всё остальное.
        const double hungerMass = (consumption * 4.0 + unmet) * unitMass;
        if (hungerMass <= 0.0) continue;

        const double score = hungerMass * pressure * strainBoost;
        if (score > bestScore) {
            bestScore = score;
            best = int(i);
        }
    }
    return best;
}

// --- Срок и плата заказа считаются ОДНОЙ моделью полёта (§23) ---------------
//
// Заказчик не знает, кто возьмётся, поэтому мерит рейс по обычному грузовику —
// стартовому «Hauler» с половинным трюмом. Важно, что мерит он ТОЙ ЖЕ
// `travelTimeEstimate`, которой считается ETA игрока: до §23 срок был прямой
// `distance / 0.11 + 3`, и на коротких плечах она врала в разы, потому что там
// разгон и торможение — почти весь рейс, а в линейной формуле их нет вовсе.
// Замер тогда дал 9 невыполнимых заказов из 15 видимых.
const Ship& contractReferenceShip() {
    static const Ship reference = [] {
        Ship s("Hauler", 0.0, 0.0, 0.0, 0.12, -1);
        s.acceleration = 0.22;
        for (size_t c = 0; c < shipClasses().size(); ++c) {
            if (shipClasses()[c].name != "Hauler") continue;
            shipApplyClass(s, shipClasses()[c]);
            break;
        }
        // Полный трюм. Пустой корпус разгоняется бодрее любого реального
        // возчика, и срок вышел бы оптимистичным ровно там, где заказ И ЕСТЬ
        // груз. Масса трюма здесь заодно стоит за баки: у опорного корпуса они
        // пусты, а настоящий рейс везёт и топливо, и рабочее тело. Разница
        // видна только на КОРОТКИХ плечах, где разгон и торможение — почти всё
        // время рейса; на них замер и ловил единственный несдаваемый заказ.
        s.cargo.emplace_back(elementDefinitions()[0].symbol,
            s.cargoCapacity / std::max(0.001, resourceUnitMassByIndex(0)));
        return s;
    }();
    return reference;
}

}   // namespace

// Сколько лет уйдёт у опорного корпуса на этот маршрут.
//
// Считается через `plannedRouteTravelTime` — ту же развилку «лечу напрямую или
// прыжками», по которой считается ETA игрока. Через `cachedRouteTravelTime`
// звать нельзя: она ВСЕГДА ведёт по прыжкам, а прыжковый маршрут разгоняется и
// тормозит на каждом плече, и срок расходился с реальным рейсом в полтора раза.
double contractRouteYears(const Game& game, int originStar, int targetStar) {
    const double years = plannedRouteTravelTime(game, contractReferenceShip(), originStar, targetStar);
    return years > 0.0 ? years : 0.0;
}

namespace {

// Плата ЗА ПОЛЁТ: годовая ставка фрахта на время опорного рейса. Это весь
// «скелет» награды; надбавки за дефицит, угрозу и вражду каждый тип заказа
// добавляет сверху сам — они объясняют, чем ЭТОТ заказ особенный.
double contractTripPay(const Game& game, int originStar, int targetStar) {
    return CONTRACT_PAY_PER_YEAR * contractRouteYears(game, originStar, targetStar);
}

// Срок = опорный рейс плюс запас. Пола в 4 года хватает на соседнюю систему.
double contractDeadlineFor(const Game& game, int originStar, int targetStar) {
    return game.time + std::max(4.0, contractRouteYears(game, originStar, targetStar) * CONTRACT_DEADLINE_SLACK);
}

// --- Тир конкретного заказа (§24) -------------------------------------------
//
// Репутация задаёт ПОТОЛОК, а не сам заказ. Каждый заказ берёт свой тир
// случайно в пределах этого потолка, и распределение смещено ВНИЗ (куб): на
// доске у заслуженного возчика по-прежнему в основном обычная работа, а
// крупное попадается редко. Ровно то, что просили: «базовые заказы и с
// урежением высокие» — причём урежение выходит само, без отдельного списка
// редкостей и без второго слота на доске.
double rollContractTier(const Game& game, int issuerFaction) {
    const double ceiling = game.factionJobTier(issuerFaction);
    if (ceiling <= 0.0) return 0.0;
    const double roll = double(randomer(rng, 1000)) / 1000.0;
    return ceiling * roll * roll * roll;
}

// Сколько массы заказчик хочет отправить при таком тире. Разброс ±25%, чтобы
// два заказа одного тира не выглядели близнецами.
double rollContractCargoMass(double tier) {
    const double spread = 0.75 + double(randomer(rng, 500)) / 1000.0;
    return Game::jobCargoForTier(tier) * spread;
}

// Тир двигает и ДАЛЬНОСТЬ: базовый заказ — к соседям, топовый — через всё
// скопление. Возвращает долю радиуса скопления, в которой ищется цель.
double contractRangeShare(double tier) {
    return JOB_RANGE_BASE + (JOB_RANGE_TOP - JOB_RANGE_BASE) * tier;
}

// Казна выдающей фракции — ПОТОЛОК награды (§24, решение пользователя).
// Фракция не выставляет заказ, который не может оплатить, поэтому миллиардные
// заказы водятся только у богатых и редко: «урежение» верха выходит из
// экономики, а не из искусственного рандома. Берём не всю казну — государство
// не тратит на один рейс всё, что у него есть.
double contractTreasuryCeiling(const Game& game, int issuerFaction) {
    if (!validFaction(game, issuerFaction)) return 0.0;
    return std::max(0.0, game.factions[size_t(issuerFaction)].treasury * 0.35);
}

// Срочный заказ: срок урезан, ставка поднята, и обе вещи видны на доске ДО
// взятия. Выбор делается на берегу, а не в полёте (§24).
void applyRushRoll(Game& game, Contract& contract) {
    if (double(randomer(rng, 1000)) / 1000.0 >= JOB_RUSH_CHANCE) return;
    contract.rushFactor = JOB_RUSH_PAY;
    contract.reward *= JOB_RUSH_PAY;
    const double window = std::max(0.0, contract.deadline - game.time);
    contract.deadline = game.time + std::max(3.0, window * JOB_RUSH_DEADLINE);
}

// Общий финал всех генераторов: обрезать награду казной и раскатать срочность.
// Возвращает false, если заказчик не тянет даже урезанный заказ — тогда заказ
// просто не выставляется.
bool finishContract(Game& game, Contract& contract) {
    const double ceiling = contractTreasuryCeiling(game, contract.issuerFaction);
    if (ceiling <= 0.0) {
        // Безденежная или ничейная фракция всё ещё может дать мелкую работу:
        // иначе доска в бедных краях вымирает совсем.
        contract.reward = std::min(contract.reward, 400.0);
    } else if (contract.reward > ceiling) {
        contract.reward = ceiling;
    }
    if (contract.reward < 50.0) return false;
    applyRushRoll(game, contract);
    game.contracts.push_back(contract);
    publishContractPosting(game, game.contracts.back());
    return true;
}

// Цель ищется НА ЗАДАННОЙ ДАЛЬНОСТИ (её диктует тир) и по тому, насколько
// тяжело там живётся. До §24 цель выбиралась по спреду и штрафовалась за
// расстояние, поэтому все заказы были одинаково короткими: дальность вообще не
// была осью, по которой заказы различались.
int pickNeedyTarget(const Game& game, int originStar, double rangeShare) {
    if (!validStar(game, originStar)) return -1;

    // Радиус скопления оцениваем один раз по опорному рейсу до самой дальней
    // из выборки — брать «настоящий» диаметр неоткуда и незачем.
    int best = -1;
    double bestScore = 0.0;
    const int samples = std::min(72, std::max(12, int(game.cluster.stars.size())));
    for (int i = 0; i < samples; ++i) {
        const int target = randomer(rng, int(game.cluster.stars.size()) - 1);
        if (target == originStar || target < 0 || target >= int(game.markets.size())) continue;

        const Market& market = game.markets[target];
        const double distance = cachedRouteDistance(game, originStar, target);
        if (distance <= 0.0) continue;

        // Чем выше тир, тем дальше «своя» дистанция. Близость к ней и есть
        // главный вес: заказ должен ЛЕЖАТЬ в своём поясе дальности, а не быть
        // просто самым выгодным из ближних.
        const double wanted = 6.0 + 90.0 * rangeShare;
        const double miss = std::abs(distance - wanted) / wanted;
        const double rangeFit = 1.0 / (1.0 + miss * miss);
        const double score = rangeFit * (0.25 + market.strain) *
            (0.5 + starPopulationWeight(game.cluster.stars[target]) * 0.5 + market.pricePressure());
        if (score > bestScore) {
            bestScore = score;
            best = target;
        }
    }
    return best;
}

bool tryCreateDeliveryContract(Game& game, int originStar) {
    if (!validStar(game, originStar) || originStar >= int(game.markets.size())) return false;
    if (activeContractsAtOrigin(game, originStar) >= CONTRACTS_PER_SYSTEM) return false;

    // Порядок обратный прежнему и это суть §24: сперва решаем, КАКОЙ ВЕЛИЧИНЫ
    // будет заказ (тир от репутации), потом ищем цель на подходящей дальности,
    // и только потом — что именно ей нужно. Раньше первым шагом был груз «что
    // полегче лежит на складе», а величина не выбиралась вовсе.
    //
    // ⚠️ Заказчик — владелец системы ОТПРАВЛЕНИЯ, то есть той доски, у которой
    // игрок стоит. До §24 доставку выдавал владелец ЦЕЛИ, и с репутацией это
    // разъехалось бы насмерть: тир открывала бы одна фракция, а росла
    // репутация у другой. Теперь тир, оплата и репутация — про одну и ту же
    // фракцию, и остальные шесть типов заказа давно делают именно так.
    const int issuer = starAuthority(game, originStar);
    const double tier = rollContractTier(game, issuer);
    const int targetStar = pickNeedyTarget(game, originStar, contractRangeShare(tier));
    if (!validStar(game, targetStar)) return false;

    const double wantedMass = rollContractCargoMass(tier);
    const int resourceIndex = pickNeededResource(game, originStar, targetStar, wantedMass);
    if (resourceIndex < 0) return false;

    Market& origin = game.markets[originStar];
    const Market& target = game.markets[targetStar];
    const double unitMass = std::max(0.001, resourceUnitMassByIndex(resourceIndex));
    const double amount = std::min(origin.supply[resourceIndex].amount * 0.18, wantedMass / unitMass);
    if (amount <= 0.25) return false;
    const double cargoMass = amount * unitMass;

    const double distance = cachedRouteDistance(game, originStar, targetStar);
    const double spread = std::max(0.0, target.prices[resourceIndex] - origin.prices[resourceIndex]);
    const double scarcityPay = amount * (spread * 0.055 + target.prices[resourceIndex] * 0.012);
    // Плата за рейс × множитель тира. Именно множитель и делает верх лестницы
    // выгоднее торговли. Доля `cargoMass / номинал тира` — чтобы разброс массы
    // ±25% внутри одного тира честно отражался в цене: два заказа одного тира
    // не обязаны стоить одинаково, если один тяжелее другого на четверть.
    const double nominalMass = std::max(1.0, Game::jobCargoForTier(tier));
    const double tripPay = contractTripPay(game, originStar, targetStar) *
        Game::jobPayMultiplierForTier(tier) * (cargoMass / nominalMass);

    Contract contract;
    contract.id = game.nextContractId++;
    contract.type = ContractType::Delivery;
    contract.tier = tier;
    contract.originStar = originStar;
    contract.targetStar = targetStar;
    contract.resource = resourceIndex;
    contract.amount = amount;
    contract.reward = std::max(80.0, scarcityPay + tripPay);
    contract.deposit = contract.reward * CONTRACT_DEPOSIT_RATE * contract.tier;
    contract.postedTime = game.time;
    contract.deadline = contractDeadlineFor(game, originStar, targetStar);
    contract.risk = std::min(1.0, distance / 95.0);
    contract.issuerFaction = validFaction(game, issuer) ? issuer : starAuthority(game, targetStar);
    contract.risk = std::min(1.0, contract.risk + game.factionRouteThreatRisk(contract.issuerFaction, originStar, targetStar) * 0.12);
    return finishContract(game, contract);
}

bool tryCreateCourierContract(Game& game, int originStar) {
    if (!validStar(game, originStar)) return false;
    if (activeContractsAtOrigin(game, originStar) >= CONTRACTS_PER_SYSTEM) return false;

    const double tier = rollContractTier(game, game.cluster.stars[originStar].ownerFaction);
    int best = -1;
    double bestScore = -std::numeric_limits<double>::max();
    const int samples = std::min(64, std::max(12, int(game.cluster.stars.size())));
    for (int i = 0; i < samples; ++i) {
        const int target = randomer(rng, int(game.cluster.stars.size()) - 1);
        if (target == originStar || !validStar(game, target)) continue;
        const double distance = cachedRouteDistance(game, originStar, target);
        const double ownerValue = game.cluster.stars[target].ownerFaction >= 0 ? 2.0 : 0.0;
        // Штраф за дальность тает с тиром: заслуженному курьеру дают дальние
        // концы, новичку — соседей. Массы у курьера нет, поэтому ДАЛЬНОСТЬ и
        // есть его тир (§24).
        const double score = ownerValue + game.cluster.stars[target].industry * 0.8 + starPopulationWeight(game.cluster.stars[target]) * 0.66 - distance * 0.025 * (1.0 - tier);
        if (score > bestScore) {
            bestScore = score;
            best = target;
        }
    }
    if (!validStar(game, best)) return false;

    const double distance = cachedRouteDistance(game, originStar, best);
    Contract contract;
    contract.id = game.nextContractId++;
    contract.type = ContractType::Courier;
    contract.tier = tier;
    contract.originStar = originStar;
    contract.targetStar = best;
    // Курьер везёт пакет, а не тонны: трюм свободен, поэтому платят скромнее
    // доставки — но всё равно за годы полёта, а не за расстояние по линейке.
    // Тир для него — это не масса (её нет), а ДАЛЬНОСТЬ и ставка.
    contract.reward = 65.0 + contractTripPay(game, originStar, best) * 0.75 *
        Game::jobPayMultiplierForTier(tier);
    contract.deposit = contract.reward * CONTRACT_DEPOSIT_RATE * contract.tier;
    contract.postedTime = game.time;
    contract.deadline = contractDeadlineFor(game, originStar, best);
    contract.risk = std::min(1.0, distance / 120.0);
    contract.issuerFaction = starAuthority(game, originStar);
    contract.risk = std::min(1.0, contract.risk + game.factionRouteThreatRisk(contract.issuerFaction, originStar, best) * 0.10);
    return finishContract(game, contract);
}

bool tryCreateScoutContract(Game& game, int originStar) {
    if (!validStar(game, originStar)) return false;
    if (activeContractsAtOrigin(game, originStar) >= CONTRACTS_PER_SYSTEM) return false;

    const int issuer = starAuthority(game, originStar);
    if (!validFaction(game, issuer)) return false;
    const double tier = rollContractTier(game, issuer);

    int best = -1;
    double bestScore = 0.0;
    const int samples = std::min(96, std::max(18, int(game.cluster.stars.size())));
    for (int i = 0; i < samples; ++i) {
        const int target = randomer(rng, int(game.cluster.stars.size()) - 1);
        if (target == originStar || !validStar(game, target)) continue;

        const bool ownerKnown = game.factionKnowsOwnerAt(issuer, originStar, target);
        const bool marketKnown = game.factionKnowsMarketAt(issuer, originStar, target);
        const double ownerAge = ownerKnown ? game.factionKnownOwnerAgeAt(issuer, originStar, target) : 80.0;
        const double marketAge = marketKnown ? game.factionKnownMarketAgeAt(issuer, originStar, target) : 80.0;
        if (ownerKnown && marketKnown && ownerAge < 10.0 && marketAge < 10.0) continue;

        const double distance = cachedRouteDistance(game, originStar, target);
        const ClusterStar& star = game.cluster.stars[target];
        const double unknownValue = (!ownerKnown ? 28.0 : std::min(18.0, ownerAge * 0.24)) +
            (!marketKnown ? 20.0 : std::min(16.0, marketAge * 0.18));
        // Как и у курьера, тир разведки — это дальность заброса.
        const double score = unknownValue + star.industry * 1.2 + star.habitability * 3.0 - distance * 0.12 * (1.0 - tier);
        if (score > bestScore) {
            bestScore = score;
            best = target;
        }
    }
    if (!validStar(game, best)) return false;

    const double distance = cachedRouteDistance(game, originStar, best);
    Contract contract;
    contract.id = game.nextContractId++;
    contract.type = ContractType::Scout;
    contract.issuerFaction = issuer;
    contract.originStar = originStar;
    contract.targetStar = best;
    // Разведка платит за полёт плюс за ценность неизвестного (`bestScore`).
    contract.tier = tier;
    contract.reward = 90.0 + bestScore * 2.2 +
        contractTripPay(game, originStar, best) * Game::jobPayMultiplierForTier(tier);
    contract.deposit = contract.reward * CONTRACT_DEPOSIT_RATE * contract.tier;
    contract.postedTime = game.time;
    // Разведке мало долететь — на месте ещё смотрят и ждут ухода сигнала,
    // поэтому сверх дороги ей даётся четверть рейса, как охоте и конвою.
    contract.deadline = contractDeadlineFor(game, originStar, best) +
        contractRouteYears(game, originStar, best) * 0.25;
    contract.risk = std::min(1.0, 0.16 + distance / 135.0);
    contract.risk = std::min(1.0, contract.risk + game.factionRouteThreatRisk(issuer, originStar, best) * 0.10);
    return finishContract(game, contract);
}

bool tryCreateColonySupplyContract(Game& game, int originStar) {
    if (!validStar(game, originStar) || originStar >= int(game.markets.size())) return false;
    if (activeContractsAtOrigin(game, originStar) >= CONTRACTS_PER_SYSTEM) return false;

    // Тот же порядок, что и у доставки (§24): величина -> цель -> что везём.
    // Отличие снабжения — цель обязана быть КОЛОНИЕЙ, поэтому дальность здесь
    // не «пояс», а фильтр: колоний в скоплении немного, и требовать от них ещё
    // и попадания в радиус тира значило бы вовсе не выдавать этот тип.
    const int issuer = starAuthority(game, originStar);
    const double tier = rollContractTier(game, issuer);
    const double wantedMass = rollContractCargoMass(tier);

    int best = -1;
    double bestScore = 0.0;
    const int samples = std::min(96, std::max(18, int(game.cluster.stars.size())));
    for (int i = 0; i < samples; ++i) {
        const int target = randomer(rng, int(game.cluster.stars.size()) - 1);
        if (target == originStar || !validStar(game, target) || target >= int(game.markets.size())) continue;
        if (colonyIndexAt(game, target) < 0) continue;

        const Market& targetMarket = game.markets[target];
        const double distance = cachedRouteDistance(game, originStar, target);
        const double score = (0.25 + targetMarket.strain) * 18.0 +
            starPopulationWeight(game.cluster.stars[target]) * 0.53 +
            game.cluster.stars[target].industry * 1.4 - distance * 0.11 * (1.0 - tier);
        if (score > bestScore) {
            bestScore = score;
            best = target;
        }
    }
    if (!validStar(game, best)) return false;

    const int resourceIndex = pickNeededResource(game, originStar, best, wantedMass);
    if (resourceIndex < 0) return false;

    Market& origin = game.markets[originStar];
    const Market& target = game.markets[best];
    const double unitMass = std::max(0.001, resourceUnitMassByIndex(resourceIndex));
    const double amount = std::min(origin.supply[resourceIndex].amount * 0.16, wantedMass / unitMass);
    if (amount <= 0.25) return false;

    const double distance = cachedRouteDistance(game, originStar, best);
    const double scarcity = std::max(0.0, target.prices[resourceIndex] / std::max(0.1, marketReferencePrice(resourceIndex)) - 1.0);

    Contract contract;
    contract.id = game.nextContractId++;
    contract.type = ContractType::ColonySupply;
    contract.tier = tier;
    contract.issuerFaction = validFaction(game, issuer) ? issuer : starAuthority(game, best);
    contract.originStar = originStar;
    contract.targetStar = best;
    contract.resource = resourceIndex;
    contract.amount = amount;
    contract.reward = std::max(100.0,
        amount * target.prices[resourceIndex] * 0.035 + scarcity * 120.0 +
        contractTripPay(game, originStar, best) * Game::jobPayMultiplierForTier(tier));
    contract.deposit = contract.reward * CONTRACT_DEPOSIT_RATE * contract.tier;
    contract.postedTime = game.time;
    contract.deadline = contractDeadlineFor(game, originStar, best);
    contract.risk = std::min(1.0, 0.12 + distance / 110.0);
    contract.risk = std::min(1.0, contract.risk + game.factionRouteThreatRisk(contract.issuerFaction, originStar, best) * 0.12);
    return finishContract(game, contract);
}

bool tryCreateBountyContract(Game& game, int originStar) {
    if (!validStar(game, originStar)) return false;
    if (activeContractsAtOrigin(game, originStar) >= CONTRACTS_PER_SYSTEM) return false;
    const int issuer = starAuthority(game, originStar);
    if (!validFaction(game, issuer)) return false;
    const double tier = rollContractTier(game, issuer);

    // ⚠️ Отчёт об угрозе берётся из ПАМЯТИ заказчика, а не из мира: центр
    // выставит награду только за то, что видел сам. Государственная система на
    // отшибе, куда палата не заглядывала, охоты не породит — и это замысел,
    // а не недоделка (§16: никто не знает мир целиком).
    const ThreatCandidate report = bestThreatReportAt(game, issuer, originStar, true);
    if (!report.valid) return false;

    Contract contract;
    contract.id = game.nextContractId++;
    contract.type = ContractType::Bounty;
    contract.issuerFaction = issuer;
    contract.originStar = originStar;
    contract.targetStar = report.starIndex;
    contract.targetAgent = report.sourceAgent;
    contract.tier = tier;
    contract.reward = 180.0 + report.threatValue * 64.0 + report.cargoValue * 0.018 +
        contractTripPay(game, originStar, report.starIndex) * Game::jobPayMultiplierForTier(tier);
    contract.deposit = contract.reward * CONTRACT_DEPOSIT_RATE * contract.tier;
    contract.postedTime = game.time;
    // Охоте нужен запас сверх дороги: цель ещё надо застать на месте.
    contract.deadline = contractDeadlineFor(game, originStar, report.starIndex) +
        contractRouteYears(game, originStar, report.starIndex) * 0.25;
    contract.risk = std::min(1.0, 0.28 + report.threatValue / 24.0 + std::min(0.25, (game.time - report.observedAt) * 0.018));
    return finishContract(game, contract);
}

bool tryCreateRaidContract(Game& game, int originStar) {
    if (!validStar(game, originStar)) return false;
    if (activeContractsAtOrigin(game, originStar) >= CONTRACTS_PER_SYSTEM) return false;
    // ⚠️ ЕДИНСТВЕННЫЙ тип заказа БЕЗ государственного заказчика (§47). Налёт —
    // это «ударь по державе-сопернику», а у центра соперников нет: он банк и
    // арбитр, он финансирует патрули, а не рейды. Государственная система даёт
    // возить, разведывать, снабжать и охотиться за головами — но не грабить.
    const int issuer = game.cluster.stars[originStar].ownerFaction;
    if (!validFaction(game, issuer)) return false;
    const double tier = rollContractTier(game, issuer);

    int best = -1;
    int bestKnownOwner = -1;
    double bestScore = 0.0;
    const int samples = std::min(96, std::max(18, int(game.cluster.stars.size())));
    for (int i = 0; i < samples; ++i) {
        const int target = randomer(rng, int(game.cluster.stars.size()) - 1);
        if (target == originStar || !validStar(game, target)) continue;
        if (!game.factionKnowsOwnerAt(issuer, originStar, target)) continue;

        const int knownOwner = game.factionKnownOwnerAt(issuer, originStar, target);
        if (!validFaction(game, knownOwner) || knownOwner == issuer) continue;

        const int relation = game.factionRelation(issuer, knownOwner);
        if (relation > -18 && game.factions[issuer].aggression < 0.70) continue;

        const ClusterStar& star = game.cluster.stars[target];
        const double distance = cachedRouteDistance(game, originStar, target);
        const double hostility = relation < 0 ? double(-relation) / 128.0 : 0.05;
        const double infoAge = game.factionKnownOwnerAgeAt(issuer, originStar, target);
        const double stalePenalty = std::min(12.0, infoAge * 0.10);
        const double targetValue =
            star.industry * 3.2 +
            starPopulationWeight(star) * 0.92 +
            star.defense * 0.45 +
            hostility * 18.0 +
            game.factions[issuer].aggression * 8.0;
        const double routeRisk = game.factionRouteThreatRisk(issuer, originStar, target);
        const double score = targetValue - distance * 0.11 - stalePenalty - routeRisk * 6.0;
        if (score > bestScore) {
            bestScore = score;
            best = target;
            bestKnownOwner = knownOwner;
        }
    }
    if (!validStar(game, best) || !validFaction(game, bestKnownOwner)) return false;

    const double distance = cachedRouteDistance(game, originStar, best);
    const int relation = game.factionRelation(issuer, bestKnownOwner);
    const double hostility = relation < 0 ? double(-relation) / 128.0 : 0.10;
    const ClusterStar& target = game.cluster.stars[best];

    Contract contract;
    contract.id = game.nextContractId++;
    contract.type = ContractType::Raid;
    contract.issuerFaction = issuer;
    contract.originStar = originStar;
    contract.targetStar = best;
    contract.tier = tier;
    contract.reward = 150.0 + bestScore * 12.0 + target.industry * 42.0 + hostility * 180.0 +
        contractTripPay(game, originStar, best) * Game::jobPayMultiplierForTier(tier);
    contract.deposit = contract.reward * CONTRACT_DEPOSIT_RATE * contract.tier;
    contract.postedTime = game.time;
    contract.deadline = contractDeadlineFor(game, originStar, best);
    contract.risk = std::min(1.0, 0.24 + target.defense * 0.018 + distance / 125.0 + hostility * 0.20);
    return finishContract(game, contract);
}

bool tryCreateEscortContract(Game& game, int originStar) {
    if (!validStar(game, originStar)) return false;
    if (activeContractsAtOrigin(game, originStar) >= CONTRACTS_PER_SYSTEM) return false;
    const int issuer = starAuthority(game, originStar);
    if (!validFaction(game, issuer)) return false;
    const double tier = rollContractTier(game, issuer);
    const double threatRisk = localEscortRisk(game, issuer, originStar);
    if (threatRisk <= 0.45) return false;

    int escortAgent = -1;
    double bestNeed = 0.0;
    const int agentSamples = std::min(96, std::max(16, int(game.agents.size())));
    for (int sample = 0; sample < agentSamples; ++sample) {
        const int index = randomer(rng, int(game.agents.size()) - 1);
        if (index < 0 || index >= int(game.agents.size())) continue;
        const Agent& agent = game.agents[index];
        if (agent.ship.enRoute || agent.currentStar != originStar) continue;
        if (agent.type != "trader" && agent.type != "colonist") continue;
        if (targetAgentHasActiveContract(game, index, ContractType::Escort)) continue;
        const double cargoValue = cargoValueAt(game, agent, originStar);
        const double need = cargoValue * 0.01 + (agent.type == "colonist" ? 8.0 : 2.0) +
            std::max(0.0, 1.0 - agent.riskTolerance) * 4.0 + threatRisk * 5.5;
        if (need > bestNeed) {
            bestNeed = need;
            escortAgent = index;
        }
    }
    if (escortAgent < 0) return false;

    int targetStar = -1;
    double bestScore = -std::numeric_limits<double>::max();
    const int samples = std::min(64, std::max(12, int(game.cluster.stars.size())));
    for (int i = 0; i < samples; ++i) {
        const int target = randomer(rng, int(game.cluster.stars.size()) - 1);
        if (target == originStar || !validStar(game, target)) continue;
        const double distance = cachedRouteDistance(game, originStar, target);
        const double score = game.cluster.stars[target].industry * 1.3 + starPopulationWeight(game.cluster.stars[target]) * 0.79 - distance * 0.04;
        if (score > bestScore) {
            bestScore = score;
            targetStar = target;
        }
    }
    if (!validStar(game, targetStar)) return false;

    const double distance = cachedRouteDistance(game, originStar, targetStar);
    Contract contract;
    contract.id = game.nextContractId++;
    contract.type = ContractType::Escort;
    contract.issuerFaction = validFaction(game, game.agents[escortAgent].ship.ownerFaction) ? game.agents[escortAgent].ship.ownerFaction : issuer;
    contract.originStar = originStar;
    contract.targetStar = targetStar;
    contract.targetAgent = escortAgent;
    contract.tier = tier;
    contract.reward = 120.0 + bestNeed * 12.0 + threatRisk * 80.0 +
        contractTripPay(game, originStar, targetStar) * Game::jobPayMultiplierForTier(tier);
    contract.deposit = contract.reward * CONTRACT_DEPOSIT_RATE * contract.tier;
    contract.postedTime = game.time;
    // Конвой идёт не быстрее подопечного — срок щедрее обычного.
    contract.deadline = contractDeadlineFor(game, originStar, targetStar) +
        contractRouteYears(game, originStar, targetStar) * 0.25;
    contract.risk = std::min(1.0, 0.18 + distance / 140.0 + bestNeed * 0.01 + threatRisk * 0.12);
    return finishContract(game, contract);
}

bool agentCanLoadContract(const Game& game, const Agent& agent, const Contract& contract) {
    if (!activeContract(contract) || contract.acceptedByAgent >= 0) return false;
    if (agent.ship.enRoute || agent.currentStar != contract.originStar || !agent.ship.cargo.empty()) return false;
    if (!validStar(game, contract.originStar) || !validStar(game, contract.targetStar)) return false;
    if (contractNeedsTargetAgent(contract.type)) {
        if (contract.targetAgent < 0 || contract.targetAgent >= int(game.agents.size())) return false;
        const Agent& target = game.agents[contract.targetAgent];
        if (contract.type == ContractType::Escort &&
            (target.ship.enRoute || target.currentStar != contract.originStar)) return false;
        if (contract.type == ContractType::Bounty && !agentIsPiracyThreat(target)) return false;
    }
    if (!contractUsesCargo(contract.type)) return true;
    if (contract.resource < 0 || contract.resource >= int(elementCount()) || contract.amount <= 0.0) return false;
    if (contract.originStar >= int(game.markets.size())) return false;
    if (contract.resource >= int(game.markets[contract.originStar].supply.size())) return false;
    if (game.markets[contract.originStar].supply[contract.resource].amount < contract.amount) return false;

    const double cargoMass = contract.amount * resourceUnitMassByIndex(contract.resource);
    return cargoMass <= agent.ship.cargoCapacity - shipCargoMass(agent.ship) + 0.001;
}

bool tryAcceptBestContract(Game& game, int agentIndex) {
    if (agentIndex < 0 || agentIndex >= int(game.agents.size())) return false;
    Agent& agent = game.agents[agentIndex];
    if (activeContractForAgent(game, agentIndex) || agent.ship.enRoute || !validStar(game, agent.currentStar)) return false;

    const AgentRoleProfile profile = agentRoleProfile(agent);
    int bestId = -1;
    double bestScore = 0.0;
    for (const Contract& contract : game.contracts) {
        if (!agentCanLoadContract(game, agent, contract)) continue;

        Ship routeShip = agent.ship;
        routeShip.cargo.clear();
        if (contractUsesCargo(contract.type)) {
            routeShip.cargo.emplace_back(elementDefinitions()[contract.resource].symbol, contract.amount);
        }
        const double distance = plannedRouteDistance(game, routeShip, contract.originStar, contract.targetStar);
        if (distance <= 0.0) continue;
        const RouteCost need = plannedRouteCost(game, routeShip, contract.originStar, contract.targetStar);
        if (!need.feasible) continue;
        const double fuelCost = refillCost(game, routeShip, contract.originStar, need);
        const double years = plannedRouteTravelTime(game, routeShip, contract.originStar, contract.targetStar);
        const double deadlinePenalty = game.time + years > contract.deadline ? contract.reward * 0.55 : 0.0;
        const double routeThreat = game.factionRouteThreatRisk(agent.ship.ownerFaction, contract.originStar, contract.targetStar);
        const double riskPenalty =
            contract.risk * (220.0 + 260.0 * (1.0 - profile.riskTolerance)) +
            routeThreat * (140.0 + contract.reward * 0.18) * (1.15 - profile.riskTolerance * 0.55);
        const double rawRoleWeight = agentContractRoleWeight(profile, contract.type);
        if (contract.type == ContractType::Raid && rawRoleWeight < 0.35) continue;
        double roleWeight = std::max(0.1, rawRoleWeight);
        if (contract.type == ContractType::Bounty) roleWeight = std::max(roleWeight, profile.patrolWeight + profile.raidWeight * 0.6);
        if (contract.type == ContractType::Escort) roleWeight = std::max(roleWeight, profile.patrolWeight * 0.8 + profile.courierWeight * 0.5);
        const double opportunityCost = years * 7.5 * std::max(0.1, profile.opportunityCostBias);
        const double score = contract.reward * roleWeight - fuelCost - riskPenalty - deadlinePenalty - opportunityCost;
        if (score > bestScore) {
            bestScore = score;
            bestId = contract.id;
        }
    }

    if (bestId < 0) return false;
    Contract* contract = contractById(game, bestId);
    if (!contract || !game.agentAcceptContract(agentIndex, bestId)) return false;
    if (!startJourney(game, agent, contract->targetStar)) {
        agent.lastAction = "contract wait fuel";
        return true;
    }
    agent.lastAction = std::string(contractTypeLabel(contract->type)) + " route";
    return true;
}

TradePlan findBestTrade(const Game& game, const Agent& agent, std::mt19937* gen = 0) {
    TradePlan best;
    // Нет своего генератора — значит зовёт симуляция, и берём глобальный.
    std::mt19937& pick = gen ? *gen : rng;
    const int current = agent.currentStar;
    if (current < 0 || current >= int(game.markets.size())) return best;
    if (!validFaction(game, agent.ship.ownerFaction)) return best;

    const Market& source = game.markets[current];

    // Направления, о которых фракция вообще что-то знает, отбираются ОДИН РАЗ.
    // Проверки знания от элемента не зависят, а раньше стояли внутри цикла по
    // элементам и повторялись для каждого из 118 — стократно лишняя работа в
    // самом горячем месте симуляции (в профиле factionKnowsOwnerAt был первым
    // по времени). Порядок и состав списка те же, поведение ИИ не меняется.
    const int destinationSamples = sampledStarCount(game, 96, 96);
    std::vector<int> knownDestinations;
    knownDestinations.reserve(size_t(destinationSamples));
    for (int sample = 0; sample < destinationSamples; ++sample) {
        const int dest = sampledStarAt(game, sample, destinationSamples, pick);
        if (dest == current || dest < 0 || dest >= int(game.markets.size())) continue;
        if (!game.factionKnowsOwnerAt(agent.ship.ownerFaction, current, dest)) continue;
        if (!game.factionKnowsMarketAt(agent.ship.ownerFaction, current, dest)) continue;
        knownDestinations.push_back(dest);
    }
    if (knownDestinations.empty()) return best;

    // Тариф и свободный объём трюма тоже не зависят от элемента.
    const double localTariff = tariffFor(game, current, agent.ship.ownerFaction, 0.014);
    const double cargoSpace = std::max(0.0, agent.ship.cargoCapacity - shipCargoMass(agent.ship));

    for (size_t element = 0; element < source.prices.size(); ++element) {
        const double buyPrice = source.prices[element];
        const double available = source.supply[element].amount;
        if (buyPrice <= 0.0 || available <= 0.01) continue;

        const double massLimitedAmount = cargoSpace / resourceUnitMassByIndex(int(element));
        const double amount = std::min(massLimitedAmount, std::min(available, agent.money / buyPrice));
        if (amount <= 0.01) continue;

        for (size_t d = 0; d < knownDestinations.size(); ++d) {
            const int dest = knownDestinations[d];

            const double sellPrice = game.factionKnownPriceAt(agent.ship.ownerFaction, current, dest, int(element));
            const double spread = sellPrice - buyPrice;
            if (spread <= 0.0) continue;
            const double confidence = game.factionKnownMarketConfidenceAt(agent.ship.ownerFaction, current, dest, int(element));
            if (confidence <= 0.05) continue;

            Ship routeShip = agent.ship;
            routeShip.cargo.clear();
            routeShip.cargo.emplace_back(elementDefinitions()[element].symbol, amount);
            const double distance = plannedRouteDistance(game, routeShip, current, dest);
            if (distance <= 0.0) continue;
            const double years = plannedRouteTravelTime(game, routeShip, current, dest);
            const RouteCost need = plannedRouteCost(game, routeShip, current, dest);
            if (!need.feasible) continue;
            const double fuelCost = refillCost(game, routeShip, current, need) * (1.0 + localTariff);
            const double cargoCost = amount * buyPrice * (1.0 + localTariff);
            if (cargoCost + fuelCost > agent.money) continue;
            const double stalePricePenalty = amount * sellPrice * (1.0 - confidence) * (0.45 + (1.0 - agent.riskTolerance) * 0.75);
            const double routeThreat = game.factionRouteThreatRisk(agent.ship.ownerFaction, current, dest);
            const double riskLoss = amount * sellPrice * std::min(0.82, routeThreat * 0.11) * (1.15 - agent.riskTolerance * 0.65);
            const double expectedProfit = spread * amount * confidence - fuelCost - stalePricePenalty - riskLoss;
            if (expectedProfit <= 0.0) continue;
            const int knownOwner = game.factionKnownOwnerAt(agent.ship.ownerFaction, current, dest);
            const double ownerBias = knownOwner == agent.ship.ownerFaction ? 1.08 : 1.0;
            const double demandPressure = std::max(0.35, game.factionKnownDemandPressureAt(agent.ship.ownerFaction, current, dest, int(element)));
            const double supplyPressure = std::max(0.35, game.factionKnownSupplyPressureAt(agent.ship.ownerFaction, current, dest, int(element)));
            const double staleOwnerPenalty = std::min(12.0, game.factionKnownOwnerAgeAt(agent.ship.ownerFaction, current, dest) * 0.04);
            const double staleMarketPenalty = std::min(18.0, game.factionKnownMarketAgeAt(agent.ship.ownerFaction, current, dest) * 0.08);
            const double pressureBias = std::max(0.4, std::min(2.5, demandPressure / supplyPressure));
            const double routeRiskPenalty = routeThreat * (1.0 + (1.0 - agent.riskTolerance) * 2.5);
            const double score = expectedProfit * ownerBias * pressureBias / (years + 0.25 + staleOwnerPenalty + staleMarketPenalty + routeRiskPenalty);
            if (score > best.score) {
                best.destStar = dest;
                best.elementIndex = int(element);
                best.amount = amount;
                best.fuelCost = fuelCost;
                best.buyPrice = buyPrice;
                best.sellPrice = sellPrice;
                best.score = score;
            }
        }
    }

    return best;
}

// Манёвр с раскладкой продуктов горения. Рабочее тело улетает в сопло, а топливо
// перегорает в активной зоне — зола остаётся на борту и ссыпается в трюм.
// Если трюм полон, лишнее приходится стравливать за борт.
double consumeAndStoreAsh(Ship& ship, double deltaV) {
    std::vector<Resource> ash;
    const double achieved = shipConsumeForDeltaV(ship, deltaV, &ash);
    // Зола ложится в трюм ЦЕЛИКОМ, даже сверх нормы: она заменяет собой
    // сгоревшее топливо, поэтому общая масса корабля не растёт. Перегруз с
    // золой не запирает игрока в полёте — он мешает лишь следующему вылету.
    for (size_t i = 0; i < ash.size(); ++i) {
        if (ash[i].amount <= 1e-9) continue;
        bool merged = false;
        for (size_t c = 0; c < ship.cargo.size(); ++c) {
            if (ship.cargo[c].element != ash[i].element) continue;
            ship.cargo[c].amount += ash[i].amount;
            merged = true;
            break;
        }
        if (!merged) ship.cargo.push_back(Resource(ash[i].element, ash[i].amount));
    }
    return achieved;
}

// (§48, замер) См. game.h: считаем прибытия, которым нечем было тормозить.
// Счётчики живут здесь, рядом с местом события; наружу их отдают обёртки в
// конце файла — этот кусок лежит в анонимном namespace (внутренняя линковка).
long long s_strandArrivals = 0;
long long s_strandDryArrivals = 0;
long long s_strandOvershoots = 0;
long long s_strandRiskyDepartures = 0;   // вылетов впритык, по нищете (§48.8)
long long s_strandTows = 0;              // сколько раз кого-то вытащили
long long s_strandFastArrivals = 0;   // прибытий с ОЩУТИМОЙ остаточной скоростью (>0.01c)
double s_strandWorstSpeed = 0.0;
double s_strandWorstAny = 0.0;        // худший остаток по ЛЮБОЙ причине
double s_ratioSum = 0.0;              // сумма (сожжено / обещано расчётом) по нормальным прибытиям
long long s_ratioCount = 0;
double s_ratioMax = 0.0;
double s_rapSum = 0.0, s_rapMax = 0.0;   // выданная быстрота / запланированная
double s_honestSum = 0.0;                // сожжено / расход по фактической массе
long long s_honestCount = 0;
long long s_rapCount = 0;
double s_dryLoadedOverPlanned = 0.0;  // сколько залили сверх расчёта те, кому ВСЁ РАВНО не хватило
long long s_dryLoadedCount = 0;
bool s_strandTrace = false;           // печатать разбор первых быстрых прибытий
int s_strandTraceLeft = 12;
// (§50) Спасение: чем кончилось и сколько ждали. Ожидание меряется от подъёма
// маяка до стыковки — в него входит и ход света, и полёт спасателя.
long long s_rescueBeacons = 0;
long long s_rescueLive = 0;           // вытащил живой борт
long long s_rescueState = 0;          // вытащил казённый спасатель
long long s_rescueLastResort = 0;     // сработал TOW_WAIT_YEARS: физика не справилась
long long s_rescueLoots = 0;          // первым пришёл чужой и вычистил трюм
double s_rescueWaitSum = 0.0, s_rescueWaitWorst = 0.0;
long long s_rescueWaitCount = 0;
double s_rescueLightSum = 0.0;        // из ожидания — только ход света до спасателя
long long s_rescueLightCount = 0;
// Проводка награды обеими сторонами: сколько ушло из казны и сколько дошло до
// карманов. Меряется здесь, а не разницей казны снаружи: казна живёт своей
// жизнью (тарифы, выкупы, субсидии), и на её фоне награда — шум.
double s_rescueBountyPaid = 0.0;      // списано с казны (награда + залитое топливо)
double s_rescueBountyCash = 0.0;      // зачислено спасателям деньгами

bool moveShipToward(Ship& ship, const ClusterStar& target, double dt) {
    const double dx = target.x - ship.x;
    const double dy = target.y - ship.y;
    const double dz = target.z - ship.z;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double speed = std::sqrt(ship.vx * ship.vx + ship.vy * ship.vy + ship.vz * ship.vz);
    const double accel = shipCurrentAcceleration(ship);

    if (dist < 0.0001 && speed <= accel * dt) {
        ship.x = target.x;
        ship.y = target.y;
        ship.z = target.z;
        ship.vx = ship.vy = ship.vz = 0.0;
        ++s_strandArrivals;
        return true;
    }

    const double dirX = dist > 0.0 ? dx / dist : 0.0;
    const double dirY = dist > 0.0 ? dy / dist : 0.0;
    const double dirZ = dist > 0.0 ? dz / dist : 0.0;
    const double stoppingDistance = accel > 0.0 ? speed * speed / (2.0 * accel) : 1e9;
    const double deltaV = accel * dt;
    // Расход считается в БЫСТРОТЕ: dw = dv / (1 - v²). На малой скорости это
    // просто dv, но чем ближе к световой, тем дороже обходится тот же прирост
    // скорости — та самая релятивистская расходимость, без отдельных костылей.
    const double rapidityCost = 1.0 / std::max(1e-6, 1.0 - speed * speed);

    // (§48, замер) Торможению не хватило РАСХОДНИКА, а не времени. Нулевое
    // ускорение — тот же голод: shipCurrentAcceleration возвращает 0, когда
    // кончился любой из двух расходников (топливо ИЛИ рабочее тело).
    bool starvedBraking = (accel <= 0.0 && speed > 0.0);
    if (stoppingDistance + speed * dt * 0.5 >= dist && speed > 0.0) {
        const double want = std::min(deltaV, speed);
        const double brake = consumeAndStoreAsh(ship, want * rapidityCost) / rapidityCost;
        if (brake < want * 0.999) starvedBraking = true;
        ship.dbgUsedRapidity += brake * rapidityCost;   // (§48, замер)
        ship.vx -= ship.vx / speed * brake;
        ship.vy -= ship.vy / speed * brake;
        ship.vz -= ship.vz / speed * brake;
    } else if (accel > 0.0 && speed < shipCruiseSpeed(ship) - 1e-9) {
        // Тратим ровно столько, сколько ещё влезает до потолка скорости.
        // Раньше двигатель жёг deltaV каждый тик и на крейсерском участке, а
        // прирост скорости тут же срезался клампом — то есть топливо горело в
        // пустоту, и расход не сходился с маршрутной оценкой, которая считает
        // только разгон и торможение. В вакууме крейсер обязан быть бесплатным.
        const double wanted = std::min(deltaV, shipCruiseSpeed(ship) - speed);
        const double thrust = consumeAndStoreAsh(ship, wanted * rapidityCost) / rapidityCost;
        ship.dbgUsedRapidity += thrust * rapidityCost;   // (§48, замер)
        ship.vx += dirX * thrust;
        ship.vy += dirY * thrust;
        ship.vz += dirZ * thrust;

        // Страховка: тяга идёт вдоль направления на цель, а вектор скорости
        // может смотреть чуть иначе, поэтому итог всё же нормируем.
        const double newSpeed = std::sqrt(ship.vx * ship.vx + ship.vy * ship.vy + ship.vz * ship.vz);
        if (newSpeed > shipCruiseSpeed(ship)) {
            const double k = shipCruiseSpeed(ship) / newSpeed;
            ship.vx *= k;
            ship.vy *= k;
            ship.vz *= k;
        }
    }

    const double oldDist = dist;
    ship.x += ship.vx * dt;
    ship.y += ship.vy * dt;
    ship.z += ship.vz * dt;

    const double ndx = target.x - ship.x;
    const double ndy = target.y - ship.y;
    const double ndz = target.z - ship.z;
    const double newDist = std::sqrt(ndx * ndx + ndy * ndy + ndz * ndz);
    if (newDist > oldDist && oldDist < std::max(0.01, speed * dt * 2.0)) {
        // (§48, замер) Остаточная скорость на момент пролёта. Если её нельзя
        // убрать и следующим тиком тяги — корабль пролетает мимо цели, и
        // сегодняшнее обнуление ниже это ему ПРОЩАЕТ.
        //
        // ⚠️ Два РАЗНЫХ повода для остатка, и путать их нельзя: «не хватило
        // ВРЕМЕНИ» (грубый шаг интегрирования — артефакт харнеса, в игре шаг
        // 0.01 года) и «не хватило РАСХОДНИКА» — только второе станет
        // застреванием, когда прощение уберут.
        const double residual = std::sqrt(ship.vx * ship.vx + ship.vy * ship.vy + ship.vz * ship.vz);
        if (accel <= 0.0) {   // (§48.8) мёртвый борт проходит мимо — это не прибытие
            ++s_strandDryArrivals;
            if (residual > s_strandWorstSpeed) s_strandWorstSpeed = residual;
            return false;
        }
        ++s_strandArrivals;
        if (residual > s_strandWorstAny) s_strandWorstAny = residual;
        // (§48, замер) Сверка расчёта с расходом. Сожжено = что залили минус
        // остаток; обещано = оценка плеча, по которой борт и заправлялся.
        if (ship.dbgPlannedRapidity > 1e-9) {
            const double rr = ship.dbgUsedRapidity / ship.dbgPlannedRapidity;
            s_rapSum += rr; ++s_rapCount;
            if (rr > s_rapMax) s_rapMax = rr;
        }
        if (ship.dbgPlannedProp > 1e-9) {
            const double left = shipPropellantMix(ship).mass;
            const double burned = std::max(0.0, ship.dbgLoadedProp - left);
            if (ship.dbgHonestNeed > 1e-9) {
                s_honestSum += burned / ship.dbgHonestNeed;
                ++s_honestCount;
            }
            if (starvedBraking) {
                s_dryLoadedOverPlanned += ship.dbgLoadedProp / ship.dbgPlannedProp;
                ++s_dryLoadedCount;
            } else {
                const double ratio = burned / ship.dbgPlannedProp;
                s_ratioSum += ratio;
                ++s_ratioCount;
                if (ratio > s_ratioMax) s_ratioMax = ratio;
            }
        }
        if (residual > 0.01) {
            ++s_strandFastArrivals;
            if (s_strandTrace && s_strandTraceLeft > 0) {
                --s_strandTraceLeft;
                const double stopNeed = accel > 0.0 ? residual * residual / (2.0 * accel) : -1.0;
                std::fprintf(stderr,
                    "  [быстрое прибытие] остаток %.4f c, ускорение %.5f, тормозной путь %.4f ly, "
                    "плечо %.3f ly, расчёт обещал %.3f, залито было %.3f, осталось %.3f, голодал %d\n",
                    residual, accel, stopNeed, ship.dbgLegDistance,
                    ship.dbgPlannedProp, ship.dbgLoadedProp,
                    shipPropellantMix(ship).mass, int(starvedBraking));
            }
        }
        if (residual > accel * dt * 1.5) {
            ++s_strandOvershoots;
            if (starvedBraking) {
                ++s_strandDryArrivals;
                if (residual > s_strandWorstSpeed) s_strandWorstSpeed = residual;
            }
        }
        // (§48.8) ШВАРТУЕТСЯ ТОЛЬКО ЖИВОЙ ДВИГАТЕЛЬ.
        //
        // ⚠️ Здесь стояло безусловное обнуление скорости, и корабль, которому
        // нечем тормозить, влетал в звезду на 0.29c и получал полную остановку
        // ДАРОМ. Именно это прощение прятало недосчёт маршрутной оценки (§48) —
        // а заодно делало сухой бак в пути безнаказанным, из-за чего борта ИИ
        // не застревали НИКОГДА.
        //
        // ⚠️ Но и мерить остаток порогом «в один тик тяги» нельзя: шаг
        // интегрирования конечен, идеально погасить ход ровно в точке нельзя
        // никогда, и на замере борта заплясали вокруг звёзд — 91% прибытий
        // «мимо», прибытий вдесятеро больше, топливо в пустоту. Различать надо
        // не величину остатка, а ЖИВ ЛИ ДВИГАТЕЛЬ: борт с тягой довёл манёвр и
        // швартуется (остаток — артефакт тика, худший замеренный 0.012c), борт
        // без тяги не сможет пришвартоваться никогда и уходит в дрейф.
        if (accel <= 0.0) return false;
        ship.x = target.x;
        ship.y = target.y;
        ship.z = target.z;
        ship.vx = ship.vy = ship.vz = 0.0;
        return true;
    }

    return false;
}

double tariffFor(const Game& game, int starIndex, int ownerFaction, double externalRate) {
    if (!validStar(game, starIndex)) return 0.0;
    const int marketOwner = game.cluster.stars[starIndex].ownerFaction;
    if (!validFaction(game, marketOwner)) return 0.0;
    if (marketOwner == ownerFaction) return externalRate * 0.35;
    const int relation = validFaction(game, ownerFaction) ? game.factionRelation(marketOwner, ownerFaction) : 0;
    const double hostility = relation < 0 ? double(-relation) / 128.0 : 0.0;
    const double alliance = relation > 0 ? double(relation) / 128.0 : 0.0;
    return externalRate * std::max(0.35, 1.0 + hostility * 2.2 - alliance * 0.45);
}

// Наценка станции за готовый к заливке расходник. Станция продаёт очищенное,
// обогащённое и упакованное вещество; сырой элемент из трюма переливается
// бесплатно. Наценка намеренно кусачая: своя топливная схема должна окупаться.

// Станционный макрос: купить расходник и сразу залить его в бак. Один поток
// для игрока и для ИИ — правила общие (ship.md, «AI Requirements»).
// bunker=true заливает топливо в реактор, иначе рабочее тело в бак.
// --------------------------------------------------------- СВОЙ РЫНОК ДАРОМ --
// В системе, которой владеет игрок, торговли как обмена деньгами нет: товар
// просто переходит между трюмом и складом, потому что склад и так его. Цена — 0,
// пошлины и лицензионный тариф — 0.
//
// Но ЗАПАС И ЦЕНА ДВИГАЮТСЯ ровно как при обычной сделке. Это не уступка балансу,
// а та же физика: склад системы конечен, и владелец, вывозящий его подчистую,
// поднимает себе же strain, душит рост населения и роняет собственный доход.
// Ограничение встроено в модель, а не приделано лимитом из воздуха.
//
// Только для корабля ПОД УПРАВЛЕНИЕМ игрока: нанятые борта той же фракции живут
// по общим правилам, иначе они возили бы дармовой товар на чужие рынки.
bool freeMarketFor(const Game& game, const Agent& agent, int starIndex) {
    return agent.playerControlled && validStar(game, starIndex) &&
           validFaction(game, game.playerFaction) &&
           game.cluster.stars[starIndex].ownerFaction == game.playerFaction;
}

bool buyConsumable(Game& game, Agent& agent, int starIndex, bool bunker, double targetMass) {
    if (!validStar(game, starIndex)) return false;
    Ship& ship = agent.ship;
    if (!bunker && driveUsesFuelAsPropellant(ship.driveIndex)) return false;

    // Докупаем тот сорт, что уже преобладает в ёмкости: станция доливает то же,
    // чем корабль заправлен, а не подмешивает случайное.
    const int element = bunker ? shipDominantFuelElement(ship) : shipDominantPropellantElement(ship);
    if (element < 0 || element >= int(elementCount())) return false;

    const MixSummary mix = bunker ? shipFuelMix(ship) : shipPropellantMix(ship);
    const double capacityVolume = bunker ? shipFuelTankVolume(ship) : ship.propellantVolume;
    const double unitVolume = elementUnitVolume(element);
    const double unitMass = elementUnitMass(element);
    const double roomUnits = std::max(0.0, (capacityVolume - mix.volume) / std::max(1e-9, unitVolume));
    const double wantedUnits = std::min(std::max(0.0, targetMass - mix.mass) / std::max(1e-6, unitMass), roomUnits);
    if (wantedUnits <= 0.01) return false;

    Market& market = game.markets[starIndex];
    if (element >= int(market.supply.size()) || element >= int(market.prices.size())) return false;

    double tariff = tariffFor(game, starIndex, agent.ship.ownerFaction, 0.014);
    if (agent.playerControlled) tariff /= std::max(1.0, game.tech.charisma);
    const double unitCost = market.prices[element] * (1.0 + REFINERY_MARKUP) * (1.0 + tariff);
    if (unitCost <= 0.0) return false;

    // Своя система заправляет даром: топливо — такой же товар со своего склада,
    // и наценка за очистку своей же станции с владельца не берётся. Запас склада
    // при этом тратится как обычно.
    const bool freeMarket = freeMarketFor(game, agent, starIndex);
    const double amount = freeMarket
        ? std::min(wantedUnits, market.supply[element].amount)
        : std::min(wantedUnits, std::min(market.supply[element].amount, agent.money / unitCost));
    if (amount <= 0.01) return false;

    const double baseCost = freeMarket ? 0.0 : amount * market.prices[element] * (1.0 + REFINERY_MARKUP);
    const double fee = freeMarket ? 0.0 : baseCost * tariff;
    const int owner = game.cluster.stars[starIndex].ownerFaction;
    market.applyTrade(element, -amount);
    agent.money -= baseCost + fee;
    if (validFaction(game, owner)) {
        game.factions[owner].treasury += fee;
        if (fee > 0.01) game.queueSettlementSignal(owner, starIndex, fee);
    }
    // Станция заливает напрямую в ёмкость, минуя трюм — за это и берут наценку.
    const std::string symbol = elementDefinitions()[element].symbol;
    std::vector<Resource>& dest = bunker ? ship.fuel : ship.propellant;
    bool merged = false;
    for (size_t i = 0; i < dest.size(); ++i) {
        if (dest[i].element != symbol) continue;
        dest[i].amount += amount;
        merged = true;
        break;
    }
    if (!merged) dest.push_back(Resource(symbol, amount));
    agent.lastAction = bunker ? "bunkered fuel" : "loaded propellant";
    return true;
}

// Долить всё, чего не хватает на маршрут. Возвращает true, если что-то залили.
bool buyRouteConsumables(Game& game, Agent& agent, int starIndex, const RouteCost& need) {
    if (!need.feasible) return false;
    bool any = false;
    if (need.fuelMass > shipFuelMix(agent.ship).mass) {
        any = buyConsumable(game, agent, starIndex, true, need.fuelMass) || any;
    }
    if (!driveUsesFuelAsPropellant(agent.ship.driveIndex) &&
        need.propellantMass > shipPropellantMix(agent.ship).mass) {
        any = buyConsumable(game, agent, starIndex, false, need.propellantMass) || any;
    }
    if (any) agent.lastAction = "refueled";
    return any;
}

bool sellCargo(Game& game, Agent& agent, int starIndex, double requestedAmount = std::numeric_limits<double>::max(), const std::string& elementSymbol = "") {
    if (!validStar(game, starIndex) || agent.ship.cargo.empty() || requestedAmount <= 0.0) return false;

    int cargoIndex = -1;
    if (!elementSymbol.empty()) {
        for (size_t i = 0; i < agent.ship.cargo.size(); ++i) {
            if (agent.ship.cargo[i].element == elementSymbol) { cargoIndex = int(i); break; }
        }
    } else {
        // Первая партия, которая ВООБЩЕ продаётся. В трюме лежат не только
        // элементы: купленный модуль едет туда же псевдо-товаром «Module: …»
        // (modules.cpp). Раньше здесь стоял безусловный `cargoIndex = 0`, и
        // модуль, оказавшийся первым, возвращал `false` — а `agentSellAllCargo`
        // на первом же `false` прерывает цикл. Итог: кнопка «продать всё»
        // молча не продавала НИЧЕГО и писала «hold empty» при полном трюме.
        // Тем же путём вставал и NPC-возчик в `updateTrader`.
        for (size_t i = 0; i < agent.ship.cargo.size(); ++i) {
            if (agent.ship.cargo[i].amount <= 0.0) continue;
            if (elementIndex(agent.ship.cargo[i].element) < 0) continue;
            cargoIndex = int(i);
            break;
        }
    }
    if (cargoIndex < 0 || agent.ship.cargo[cargoIndex].amount <= 0.0) return false;

    Market& market = game.markets[starIndex];
    const int resourceIndex = elementIndex(agent.ship.cargo[cargoIndex].element);
    if (resourceIndex < 0 || resourceIndex >= int(market.prices.size())) return false;

    const double cargoAmount = agent.ship.cargo[cargoIndex].amount;
    const double amount = std::min(cargoAmount, requestedAmount);
    if (amount <= 0.01) return false;
    // Цена исполнения — СРЕДНЯЯ по сделке: сбрасывая большой груз в тонкий рынок,
    // продавец сам сбивает себе цену уже по ходу продажи, а не после неё.
    // В своей системе выручка нулевая: это передача на собственный склад.
    const bool freeMarket = freeMarketFor(game, agent, starIndex);
    const double gross = freeMarket ? 0.0 : amount * market.executionPrice(resourceIndex, amount, true);
    double tariff = tariffFor(game, starIndex, agent.ship.ownerFaction, 0.026);
    if (agent.playerControlled) tariff /= std::max(1.0, game.tech.charisma);
    const double fee = freeMarket ? 0.0 : gross * tariff;
    const int owner = game.cluster.stars[starIndex].ownerFaction;
    const double costShare = agent.cargoCost * (amount / std::max(0.001, cargoAmount));

    // (§47) Лицензионный тариф удерживается СО ВСЕХ ШЕСТНАДЦАТИ, а не с одного
    // игрока. До §47 державы торговали в скоплении беспошлинно, и «центр,
    // живущий с квот и тарифов» кормился одним капитаном: квота была не
    // правилом скопления, а личным налогом на игрока.
    //
    // ⚠️ Палата себе не платит: она и есть получатель. Иначе тариф ходил бы по
    // кругу и раздувал её же казну из ничего.
    const int licencePayer = agent.ship.ownerFaction;
    const bool licensable = validFaction(game, licencePayer) && licencePayer != game.clearingFaction;
    const double licenceFee = licensable && !game.licenceOf(licencePayer).revoked
                                  ? gross * game.licenceOf(licencePayer).tariffRate
                                  : 0.0;
    // (§37.7) ОТРАБОТКА ВЫКУПА. При отозванной лицензии выручка не идёт в
    // карман — она гасит долг по выкупу, и когда долг закрыт, торговля
    // размораживается сама.
    //
    // ⚠️ Раньше отзыв лицензии просто запирал продажу, включая продажу
    // НАКОПАННОГО в поясе, — вопреки собственному комментарию в updateLicence,
    // который обещал «есть чем откопаться». Откопаться было нечем: руду копать
    // можно, продать нельзя, а награда за заказ в квоту не идёт. Теперь путь
    // назад есть, и он честный: работать за долг.
    // Закон отработки живёт в ОДНОМ месте — `Game::applyLicenceBuyback`: его
    // зовут три продажи (руда, экзотика, акции), и расходиться им нельзя.
    const double debtPaid = agent.playerControlled ? game.applyLicenceBuyback(gross - fee) : 0.0;

    market.applyTrade(resourceIndex, amount);
    market.demand[resourceIndex].amount = std::max(0.0, market.demand[resourceIndex].amount - amount);
    agent.money += gross - fee - licenceFee - debtPaid;
    if (validFaction(game, owner)) {
        game.factions[owner].treasury += fee;
        if (fee > 0.01) game.queueSettlementSignal(owner, starIndex, fee);
    }
    if (licenceFee > 0.0) {
        game.licenceOf(licencePayer).quotaPaid += licenceFee;
        // Лицензиями и квотами ведает КЛИРИНГОВАЯ ПАЛАТА, а не своя же фракция.
        // Иначе игрок платил тариф сам себе, а после §16 (доступ к казне) ещё и
        // снимал его обратно — квота не стоила ничего (замер: удержано 33.6 Cr,
        // снято обратно 33.6 Cr).
        if (validFaction(game, game.clearingFaction)) game.factions[game.clearingFaction].treasury += licenceFee;
    }
    // Выручка, ушедшая в погашение выкупа, прибылью НЕ является: игрок её не
    // видел. Иначе он получал бы исследования за деньги, которых не получил.
    agent.lastProfit = gross - fee - licenceFee - debtPaid - costShare;
    // (§34) Удачная сделка — тоже знание: рейс проверил модель рынка на деле.
    //
    // ⚠️ Прибыль КОПИТСЯ и превращается в очки раз в год (`Game::update`), а не
    // начисляется тут же. Сразу было написано «начислить логарифм от прибыли», и
    // замер это похоронил: слагаемое не зависело от объёма, а нижний порог
    // сделки — сотая единицы. Одна партия в 100 единиц давала 1.10 очка целиком
    // и 100.11 очка, если продавать её по половинке за клик, — то есть готовое
    // ядро за тридцать секунд кликанья. Логарифм, поставленный ради того, чтобы
    // куш не давал в тысячу раз больше, обходился с другой стороны — дроблением.
    // Годовой накопитель от дробления не зависит по построению.
    //
    // ⚠️ И только у КАПИТАНА (`agentIndex == playerAgent` проверяется в
    // накопителе): борт под автопилотом (§35) не исследует ничего. Иначе
    // `addResearch` на пороге дёргает `randomer(rng, …)`, и один поднятый флаг
    // сдвигал бы весь дальнейший мир — то самое §2.3, ради которого автопилот и
    // писался отдельной функцией.
    if (agent.playerControlled && !agent.autoTrade && agent.lastProfit > 0.0) {
        game.tradeInsightPending += agent.lastProfit;
    }
    agent.cargoCost = std::max(0.0, agent.cargoCost - costShare);
    agent.trades += 1;
    agent.lastAction = (freeMarket ? "gave " : "sold ") + agent.ship.cargo[cargoIndex].element;
    agent.ship.cargo[cargoIndex].amount -= amount;
    if (agent.ship.cargo[cargoIndex].amount <= 0.01) {
        agent.ship.cargo.erase(agent.ship.cargo.begin() + cargoIndex);
        if (agent.ship.cargo.empty()) agent.cargoCost = 0.0;
    }
    return true;
}

void buyCargo(Game& game, Agent& agent, int starIndex, const TradePlan& plan) {
    if (!validStar(game, starIndex) || plan.elementIndex < 0 || plan.elementIndex >= int(game.markets[starIndex].supply.size())) return;
    Market& market = game.markets[starIndex];
    const ElementDefinition& element = elementDefinitions()[plan.elementIndex];

    // Сколько влезет по ЦЕНЕ ДО СДЕЛКИ — только первая прикидка: скупая рынок,
    // покупатель сам разгоняет себе цену, поэтому уточняем объём по средней цене
    // исполнения. Две итерации сходятся: функция монотонна и пологая.
    const bool freeMarket = freeMarketFor(game, agent, starIndex);
    const double capAmount = std::min(plan.amount, market.supply[plan.elementIndex].amount);
    double amount = capAmount;
    if (freeMarket) {
        // Со своего склада берут даром, поэтому деньги больше не потолок — им
        // становится трюм. Без этого запрошенный «максимум» вычерпал бы систему
        // целиком в корабль, который столько не увезёт.
        const double freeMass = std::max(0.0, agent.ship.cargoCapacity - shipCargoMass(agent.ship));
        amount = std::min(capAmount, freeMass / std::max(1e-9, resourceUnitMassByIndex(plan.elementIndex)));
    } else {
        // ⚠️ И ТРЮМ ТОЖЕ ПОТОЛОК. Платная ветка ограничивала объём ТОЛЬКО
        // деньгами: игрок платил за груз, который физически не увезёт, и
        // упирался в «route blocked: overweight» уже после списания. Выход был
        // (кнопка «за борт»), но это чистая потеря — за перегруз платили
        // дважды. Перегруз как СОСТОЯНИЕ остаётся достижимым через переливы и
        // золу (§12), но покупать себя в него больше нельзя.
        const double freeMass = std::max(0.0, agent.ship.cargoCapacity - shipCargoMass(agent.ship));
        const double holdUnits = freeMass / std::max(1e-9, resourceUnitMassByIndex(plan.elementIndex));
        // Пошлина ОБЯЗАНА входить в бюджет закупки. Раньше объём считался как
        // `деньги / цена`, а списывалось `цена + пошлина`, — и «купить
        // максимум» на любом элементе оставляло кошелёк ровно в минусе на
        // размер пошлины (замер: 10 000 Cr -> −140 Cr при ставке 1.4%).
        // Отрицательных денег не ждёт никто: заправка перестаёт заправлять
        // (`money/unitCost` уходит в минус), взнос на счёт возвращает ноль, а
        // ограбление такого борта ДОБАВЛЯЕТ ему денег (пират считает долю от
        // отрицательного кошелька). Тот же путь у NPC через `updateTrader`.
        double budgetTariff = tariffFor(game, starIndex, agent.ship.ownerFaction, 0.014);
        if (agent.playerControlled) budgetTariff /= std::max(1.0, game.tech.charisma);
        const double budget = std::max(0.0, agent.money) / (1.0 + std::max(0.0, budgetTariff));
        amount = std::min(std::min(capAmount, holdUnits), budget / std::max(1e-9, market.prices[plan.elementIndex]));
        for (int pass = 0; pass < 2 && amount > 0.0; ++pass) {
            const double avg = market.executionPrice(plan.elementIndex, amount, false);
            amount = std::min(std::min(capAmount, holdUnits), budget / std::max(1e-9, avg));
        }
    }
    if (amount <= 0.01) return;

    const double unitCost = freeMarket ? 0.0 : market.executionPrice(plan.elementIndex, amount, false);
    const double baseCost = amount * unitCost;
    double tariff = tariffFor(game, starIndex, agent.ship.ownerFaction, 0.014);
    if (agent.playerControlled) tariff /= std::max(1.0, game.tech.charisma);
    const double fee = freeMarket ? 0.0 : baseCost * tariff;
    const int owner = game.cluster.stars[starIndex].ownerFaction;

    market.applyTrade(plan.elementIndex, -amount);
    market.demand[plan.elementIndex].amount += amount * 0.45;
    agent.money -= (baseCost + fee);
    if (validFaction(game, owner)) {
        game.factions[owner].treasury += fee;
        if (fee > 0.01) game.queueSettlementSignal(owner, starIndex, fee);
    }
    agent.cargoCost += baseCost + fee;
    
    int cargoIndex = -1;
    for (size_t i = 0; i < agent.ship.cargo.size(); ++i) {
        if (agent.ship.cargo[i].element == element.symbol) { cargoIndex = int(i); break; }
    }
    if (cargoIndex < 0) {
        agent.ship.cargo.emplace_back(element.symbol, amount);
    } else {
        agent.ship.cargo[cargoIndex].amount += amount;
    }
    agent.lastAction = (freeMarket ? "took " : "bought ") + std::string(element.symbol);
}

bool startJourney(Game& game, Agent& agent, int destStar) {
    if (destStar < 0 || destStar == agent.currentStar) return false;
    if (!validStar(game, destStar)) return false;
    // Из пустоты летим НАПРЯМУЮ от координат корабля: звёздный граф нужен лишь
    // для выбора промежуточных портов, а когда порта под ногами нет, выбирать
    // нечего. Перемещение привязано к координатам, а не к системам.
    const bool docked = validStar(game, agent.currentStar);
    double propellantPrice = 1.0;
    double fuelPrice = 1.0;
    routePrices(game, agent.ship, agent.currentStar, propellantPrice, fuelPrice);

    // Перегруз не запрещает грузить — он запрещает ВЗЛЕТАТЬ. Паспортная норма
    // трюма и есть тот предел, выше которого корпус не сертифицирован к тяге.
    const double overload = shipCargoOverload(agent.ship);
    if (overload > 0.001) {
        agent.lastAction = "overloaded";
        return false;
    }

    // (§48) КРЕЙСЕР ВЫБИРАЕТСЯ ПОД Cr ЗА ГОД ПОЛЁТА — той же меркой, которой
    // меряется всё остальное в игре (§23).
    //
    // Ручка «доля потолка скорости» — размен: медленнее значит дешевле, но и
    // реже. Считать её по одной цене (минимум расхода) неверно — так борт
    // уполз бы на 0.2 и вёз бы груз веками. Правильный вопрос — сколько рейс
    // приносит В ГОД, и у него есть максимум внутри диапазона: на замере
    // (плечо 7 ly, бедный старт) −11 Cr/год на полном ходу, +61 на половине,
    // +52 на трёх десятых.
    //
    // Выручка берётся как стоимость трюма ТАМ, КУДА ЛЕТИМ: пустой борт (патруль,
    // пират, перегон) размена не имеет и остаётся на умолчании — иначе он бы
    // крался на самом дешёвом делении, а торопиться ему как раз незачем платить.
    // Игроку ручку не крутим: она его, кроме борта под автопилотом.
    if ((!agent.playerControlled || agent.autoTrade) && docked) {
        const double revenue = cargoValueAt(game, agent, destStar);
        if (revenue > 0.0) {
            const double keep = agent.ship.cruiseFraction;
            const double legDistance = distanceShipToStar(agent.ship, game.cluster.stars[destStar]);
            double bestPerYear = -1e300;
            double bestCruise = keep;
            for (int ci = 0; ci <= 4; ++ci) {
                const double frac = 0.2 + 0.8 * double(ci) / 4.0;
                agent.ship.cruiseFraction = frac;
                shipTuneDrive(agent.ship, propellantPrice, fuelPrice);
                const RouteCost r = legCost(agent.ship, legDistance, propellantPrice, fuelPrice);
                if (!r.feasible) continue;
                const double years = legDistance / std::max(1e-9, shipCruiseSpeed(agent.ship));
                const double cost = r.propellantMass * propellantPrice + r.fuelMass * fuelPrice;
                const double perYear = (revenue - cost) / std::max(1e-9, years);
                if (perYear > bestPerYear) { bestPerYear = perYear; bestCruise = frac; }
            }
            agent.ship.cruiseFraction = bestPerYear > -1e299 ? bestCruise : keep;
        }
    }

    // Двигатель настраивается ПЕРЕД вылетом по ценам порта отправления и
    // дальше не меняется: планировщик и реальный расход считают по одной ve.
    shipTuneDrive(agent.ship, propellantPrice, fuelPrice);

    // Дистанция всегда от ФАКТИЧЕСКОГО положения корабля, а не от звезды, к
    // которой он формально приписан: после экстренной остановки это разные
    // точки, иногда на световые годы.
    const double directDistance = distanceShipToStar(agent.ship, game.cluster.stars[destStar]);
    const RouteCost directNeed = legCost(agent.ship, directDistance, propellantPrice, fuelPrice);
    if ((!agent.playerControlled || agent.autoTrade) && docked) buyRouteConsumables(game, agent, agent.currentStar, directNeed);

    const bool direct = !docked || shipCanFlyDirect(agent.ship, directDistance);
    const int legStar = direct ? destStar : game.routeNextStar(agent.currentStar, destStar);
    if (!validStar(game, legStar) || legStar == agent.currentStar) return false;
    const double distance = distanceShipToStar(agent.ship, game.cluster.stars[legStar]);
    const RouteCost need = legCost(agent.ship, distance, propellantPrice, fuelPrice);
    if ((!agent.playerControlled || agent.autoTrade) && docked) buyRouteConsumables(game, agent, agent.currentStar, need);
    if (!need.feasible) {
        // Пара движок/рабочее тело физически не тянет это плечо — доливать
        // бесполезно, надо менять схему.
        agent.lastAction = "drive cannot reach";
        return false;
    }
    // (§48.8) НИЩЕТА ВЫНУЖДАЕТ ЛЕТЕТЬ ВПРИТЫК.
    //
    // Заправка уже отработала выше и купила всё, на что хватило денег и что
    // было на складе. Если запаса всё равно не хватает, у борта ИИ есть ровно
    // два выхода: стоять в порту вечно или лететь на том, что есть. Он летит —
    // и это единственная причина, по которой борта ИИ вообще попадают в беду
    // (решение пользователя, §48.8). Частота отсюда вытекает сама: она равна
    // тому, как часто торговец оказывается на мели, и руками не задана.
    //
    // ⚠️ Игрока так не выпускаем: отправить его в дрейф молча, не спросив, —
    // это не риск, а подстава. Ему по-прежнему отказ.
    const bool shortFuel = shipFuelMix(agent.ship).mass < need.fuelMass;
    const bool shortProp = !driveUsesFuelAsPropellant(agent.ship.driveIndex) &&
                           shipPropellantMix(agent.ship).mass < need.propellantMass;
    if (shortFuel || shortProp) {
        if (agent.playerControlled) {
            agent.lastAction = shortFuel ? "need fuel" : "need propellant";
            return false;
        }
        // Совсем пустой бак — это не риск, а гарантированная гибель на первом
        // же тике: без обоих расходников двигатель не даёт тяги вовсе, и борт
        // не сдвинулся бы с места. Такой остаётся в порту.
        if (shipFuelMix(agent.ship).mass <= 0.0) { agent.lastAction = "need fuel"; return false; }
        if (!driveUsesFuelAsPropellant(agent.ship.driveIndex) &&
            shipPropellantMix(agent.ship).mass <= 0.0) { agent.lastAction = "need propellant"; return false; }
        ++s_strandRiskyDepartures;
        agent.lastAction = "departed on a shoestring";
    }
    agent.destStar = destStar;
    agent.ship.targetStar = legStar;
    agent.ship.enRoute = true;
    agent.ship.dbgPlannedProp = need.propellantMass;              // (§48, замер)
    agent.ship.dbgLoadedProp = shipPropellantMix(agent.ship).mass; // (§48, замер)
    agent.ship.dbgLegDistance = distance;                          // (§48, замер)
    {   // (§48, замер) Бюджет быстроты, который заложила оценка плеча: тот же
        // профиль, что в shipEstimateRoute — разгон до пика и торможение.
        const double aEst = std::max(0.001, std::min(agent.ship.acceleration,
            agent.ship.driveThrust / shipTotalMass(agent.ship)));
        const double peak = std::min(shipCruiseSpeed(agent.ship), std::sqrt(distance * aEst));
        const double b = std::max(-0.999999, std::min(0.999999, peak));
        agent.ship.dbgPlannedRapidity = 2.0 * (0.5 * std::log((1.0 + b) / (1.0 - b)));
        agent.ship.dbgUsedRapidity = 0.0;
        // Тот же манёвр, но посчитанный ФОРМУЛОЙ САМОГО ПОЛЁТА: Циолковский от
        // фактической массы борта (полные баки — тоже груз, который надо разгонять).
        agent.ship.dbgHonestNeed = need.exhaustVelocity > 1e-9
            ? shipTotalMass(agent.ship) * (1.0 - std::exp(-std::min(60.0,
                  agent.ship.dbgPlannedRapidity * DELTAV_SCALE / need.exhaustVelocity)))
            : 0.0;
    }
    agent.lastAction = "departed";
    return true;
}

int pickColonistTarget(const Game& game, int factionIndex, int observerStar) {
    if (!validFaction(game, factionIndex) || !validStar(game, observerStar) || game.factions[factionIndex].controlledStars.empty()) return -1;

    int best = -1;
    double bestScore = -std::numeric_limits<double>::max();
    const int samples = sampledStarCount(game, 160, 160);
    for (int sample = 0; sample < samples; ++sample) {
        const int starIndex = sampledStarAt(game, sample, samples);
        if (!validStar(game, starIndex)) continue;
        const ClusterStar& star = game.cluster.stars[starIndex];
        const bool ownerKnown = game.factionKnowsOwnerAt(factionIndex, observerStar, starIndex);
        const int knownOwner = ownerKnown ? game.factionKnownOwnerAt(factionIndex, observerStar, starIndex) : -2;
        if (ownerKnown && knownOwner >= 0) continue;

        const double distance = nearestOwnedDistance(game, starIndex, factionIndex);
        const double uncertainty = ownerKnown ? 0.0 : 2.5;
        const double stalePenalty = ownerKnown ? std::min(6.0, game.factionKnownOwnerAgeAt(factionIndex, observerStar, starIndex) * 0.025) : 0.0;
        const double score = star.habitability * 15.0 + star.industry * 3.0 + starPopulationWeight(star) * 1.8 - distance * 0.32 - uncertainty - stalePenalty;
        if (score > bestScore) {
            bestScore = score;
            best = starIndex;
        }
    }
    if (best >= 0) return best;

    for (int owned : game.factions[factionIndex].controlledStars) {
        if (!validStar(game, owned)) continue;
        const ClusterStar& star = game.cluster.stars[owned];
        const double score = star.habitability * 3.0 + 6.0 - star.defense * 0.4;
        if (score > bestScore) {
            bestScore = score;
            best = owned;
        }
    }
    return best;
}

void settleCurrentStar(Game& game, Agent& agent) {
    const int starIndex = agent.currentStar;
    const int factionIndex = agent.ship.ownerFaction;
    if (!validStar(game, starIndex) || !validFaction(game, factionIndex)) return;

    ClusterStar& star = game.cluster.stars[starIndex];
    const bool newOwner = star.ownerFaction != factionIndex;
    if (newOwner) setStarOwner(game, starIndex, factionIndex);
    game.queueOwnerSignal(factionIndex, starIndex, starIndex);
    game.queueMarketSignal(factionIndex, starIndex, starIndex);

    const double settlers = agent.ship.cargo.empty() ? 0.0 : agent.ship.cargo[0].amount;
    int colonyIndex = colonyIndexAt(game, starIndex);
    if (colonyIndex < 0) {
        addColony(game, starIndex, factionIndex, false);
        colonyIndex = colonyIndexAt(game, starIndex);
        game.lastEvent = game.factions[factionIndex].name + " chartered " + star.name;
    } else if (newOwner) {
        transferColonies(game, starIndex, factionIndex);
        game.lastEvent = game.factions[factionIndex].name + " integrated " + star.name;
    }

    if (colonyIndex >= 0) {
        Colony& colony = game.colonies[colonyIndex];
        const size_t addedPopulation = size_t(std::max(30.0, settlers * 5.0));
        colony.population += addedPopulation;
        colony.infrastructure += 0.06 + settlers * 0.0004;
        colony.ownerFaction = factionIndex;
        star.population += double(addedPopulation);
        star.industry += 0.015 + star.habitability * 0.012;
        star.defense += 0.18 + colony.infrastructure * 0.015;
    }

    agent.ship.cargo.clear();
    agent.money = std::max(0.0, agent.money - 120.0);
    agent.missionCooldown = 0.8;
    agent.lastAction = "settled";
}

void updateColonist(Game& game, Agent& agent, double dt) {
    if (!validFaction(game, agent.ship.ownerFaction)) return;

    if (agent.missionCooldown > 0.0) {
        agent.missionCooldown = std::max(0.0, agent.missionCooldown - dt);
        return;
    }

    if (!agent.ship.cargo.empty() && agent.ship.cargo[0].element == "Settlers") {
        settleCurrentStar(game, agent);
        return;
    }

    int target = pickFactionOrderTarget(game, agent.ship.ownerFaction, FactionOrderType::Colonize);
    if (target < 0) target = pickColonistTarget(game, agent.ship.ownerFaction, agent.currentStar);
    if (target < 0) return;

    agent.ship.cargo.clear();
    agent.ship.cargo.emplace_back("Settlers", std::min(320.0, agent.ship.cargoCapacity));
    if (!startJourney(game, agent, target)) {
        settleCurrentStar(game, agent);
    }
}

int pickMilitaryTarget(const Game& game, int factionIndex, int observerStar) {
    if (!validFaction(game, factionIndex) || !validStar(game, observerStar) || game.factions[factionIndex].controlledStars.empty()) return -1;

    const Faction& faction = game.factions[factionIndex];
    int best = -1;
    double bestScore = -std::numeric_limits<double>::max();

    const int samples = sampledStarCount(game, 160, 160);
    for (int sample = 0; sample < samples; ++sample) {
        const int starIndex = sampledStarAt(game, sample, samples);
        if (!validStar(game, starIndex)) continue;
        const ClusterStar& star = game.cluster.stars[starIndex];
        const bool ownerKnown = game.factionKnowsOwnerAt(factionIndex, observerStar, starIndex);
        const int knownOwner = ownerKnown ? game.factionKnownOwnerAt(factionIndex, observerStar, starIndex) : -2;
        if (ownerKnown && knownOwner == factionIndex) continue;
        if (ownerKnown && knownOwner >= 0) {
            const int relation = game.factionRelation(factionIndex, knownOwner);
            if (relation > -35 && faction.aggression < 0.78) continue;
        }

        const double distance = nearestOwnedDistance(game, starIndex, factionIndex);
        const int relation = ownerKnown && knownOwner >= 0 ? game.factionRelation(factionIndex, knownOwner) : 0;
        const double hostility = relation < 0 ? double(-relation) / 128.0 : 0.0;
        const double enemyValue = !ownerKnown ? 3.0 + faction.aggression * 2.0 :
            (knownOwner >= 0 ? 8.0 + hostility * 26.0 + faction.aggression * 8.0 : 4.0 + faction.aggression * 4.0);
        const double stalePenalty = ownerKnown ? std::min(8.0, game.factionKnownOwnerAgeAt(factionIndex, observerStar, starIndex) * 0.035) : 4.0;
        const double score = enemyValue + star.industry * 2.4 + starPopulationWeight(star) * 1.05 - distance * 0.28 - star.defense * 0.18 - stalePenalty;
        if (score > bestScore) {
            bestScore = score;
            best = starIndex;
        }
    }

    if (best >= 0) return best;

    for (int owned : faction.controlledStars) {
        if (!validStar(game, owned)) continue;
        const double score = 10.0 - game.cluster.stars[owned].defense;
        if (score > bestScore) {
            bestScore = score;
            best = owned;
        }
    }
    return best;
}

bool resolveMilitaryArrival(Game& game, Agent& agent) {
    const int starIndex = agent.currentStar;
    const int factionIndex = agent.ship.ownerFaction;
    if (!validStar(game, starIndex) || !validFaction(game, factionIndex)) return false;

    ClusterStar& star = game.cluster.stars[starIndex];
    if (star.ownerFaction == factionIndex) {
        star.defense += 0.06;
        agent.lastAction = "patrol";
        return false;
    }

    Faction& faction = game.factions[factionIndex];
    if (star.ownerFaction < 0) {
        if (faction.aggression < 0.55) {
            agent.lastAction = "scouted";
            return false;
        }
        setStarOwner(game, starIndex, factionIndex);
        game.queueOwnerSignal(factionIndex, starIndex, starIndex);
        game.queueMarketSignal(factionIndex, starIndex, starIndex);
        star.defense = std::max(star.defense, 1.8 + faction.strength);
        game.capturedSystems += 1;
        game.lastEvent = faction.name + " claimed " + star.name;
        agent.missionCooldown = 0.5;
        agent.lastAction = "claimed";
        return true;
    }

    const int enemyIndex = star.ownerFaction;
    const int relation = game.factionRelation(factionIndex, enemyIndex);
    if (relation > -35 && faction.aggression < 0.82) {
        agent.lastAction = "border patrol";
        agent.missionCooldown = 0.7;
        return false;
    }

    const double attack = 3.2 + agent.ship.speed * 8.0 + agent.ship.cargoCapacity * 0.035 + faction.strength + faction.treasury * 0.00008;
    const double defense = star.defense + (validFaction(game, enemyIndex) ? game.factions[enemyIndex].strength * 0.9 : 0.0);

    if (star.occupyingFaction != factionIndex) {
        star.occupyingFaction = factionIndex;
        star.captureProgress = 0.0;
    }
    const double pressure = attack / std::max(0.1, defense);
    star.capturePressure = pressure;
    star.contestedAt = game.time;
    if (pressure > 0.72) {
        star.captureProgress = std::min(1.0, star.captureProgress + std::min(0.34, 0.08 + (pressure - 0.72) * 0.16));
        star.defense = std::max(0.35, star.defense - attack * 0.055);
    } else {
        star.captureProgress = std::max(0.0, star.captureProgress - 0.06);
        star.defense += 0.04;
    }

    const bool capturedNow = star.captureProgress >= 1.0;
    if (capturedNow) {
        setStarOwner(game, starIndex, factionIndex);
        game.queueOwnerSignal(factionIndex, starIndex, starIndex);
        game.queueMarketSignal(factionIndex, starIndex, starIndex);
        transferColonies(game, starIndex, factionIndex);
        star.defense = std::max(1.0, attack * 0.42);
        faction.strength += 0.04;
        faction.treasury = std::max(0.0, faction.treasury - 60.0);
        if (validFaction(game, enemyIndex)) {
            game.factions[enemyIndex].strength = std::max(0.2, game.factions[enemyIndex].strength - 0.05);
        }
        game.adjustFactionRelation(factionIndex, enemyIndex, -30);
        const unsigned long long combatEvent = allocateSignalEventId(game);
        game.queueCombatSignal(factionIndex, starIndex, -1, -1, attack, combatEvent);
        game.queueCombatSignal(enemyIndex, starIndex, -1, -1, attack, combatEvent);
        game.queueDiplomacySignal(factionIndex, starIndex, enemyIndex, game.factionRelation(factionIndex, enemyIndex));
        game.queueDiplomacySignal(enemyIndex, starIndex, factionIndex, game.factionRelation(enemyIndex, factionIndex));
        game.capturedSystems += 1;
        game.lastEvent = faction.name + " captured " + star.name;
        agent.lastAction = "captured";
    } else {
        faction.strength = std::max(0.2, faction.strength - (pressure > 0.72 ? 0.004 : 0.014));
        agent.money = std::max(0.0, agent.money - (pressure > 0.72 ? 35.0 : 90.0));
        game.adjustFactionRelation(factionIndex, enemyIndex, pressure > 0.72 ? -6 : -12);
        const unsigned long long combatEvent = allocateSignalEventId(game);
        game.queueCombatSignal(factionIndex, starIndex, -1, -1, attack, combatEvent);
        game.queueCombatSignal(enemyIndex, starIndex, -1, -1, attack, combatEvent);
        game.queueDiplomacySignal(factionIndex, starIndex, enemyIndex, game.factionRelation(factionIndex, enemyIndex));
        game.queueDiplomacySignal(enemyIndex, starIndex, factionIndex, game.factionRelation(enemyIndex, factionIndex));
        const int pct = int(star.captureProgress * 100.0);
        game.lastEvent = faction.name + " contesting " + star.name + " " + std::to_string(pct) + "%";
        agent.lastAction = pressure > 0.72 ? "besieging" : "repelled";
    }

    agent.missionCooldown = capturedNow ? 1.1 : 0.75;
    return true;
}

void updateMilitary(Game& game, Agent& agent, double dt) {
    if (!validFaction(game, agent.ship.ownerFaction)) return;

    if (agent.missionCooldown > 0.0) {
        agent.missionCooldown = std::max(0.0, agent.missionCooldown - dt);
        return;
    }

    if (resolveMilitaryArrival(game, agent)) return;

    int target = pickFactionOrderTarget(game, agent.ship.ownerFaction, FactionOrderType::AttackSystem);
    if (target < 0) target = pickFactionOrderTarget(game, agent.ship.ownerFaction, FactionOrderType::Raid);
    if (target < 0) target = pickFactionOrderTarget(game, agent.ship.ownerFaction, FactionOrderType::Patrol);
    if (target < 0) target = pickMilitaryTarget(game, agent.ship.ownerFaction, agent.currentStar);
    if (target >= 0) startJourney(game, agent, target);
}

double combatPower(const Game& game, const Agent& agent) {
    double power = 0.8 + agent.ship.speed * 9.0 + agent.ship.cargoCapacity * 0.012 + shipTotalMass(agent.ship) * 0.004;
    if (agent.type == "military") power *= 1.85;
    if (agent.type == "pirate") power *= 1.28;
    if (agent.type == "colonist") power *= 0.72;
    if (validFaction(game, agent.ship.ownerFaction)) {
        power += game.factions[agent.ship.ownerFaction].strength * 0.42;
    }
    return std::max(0.1, power);
}

bool agentIsPiracyThreat(const Agent& agent) {
    return agent.type == "pirate" || agent.piracyBias > 0.55;
}

double cargoValueAt(const Game& game, const Agent& agent, int starIndex) {
    if (!validStar(game, starIndex) || starIndex >= int(game.markets.size()) || agent.ship.cargo.empty()) return 0.0;
    const int resourceIndex = elementIndex(agent.ship.cargo[0].element);
    if (resourceIndex < 0 || resourceIndex >= int(game.markets[starIndex].prices.size())) return 0.0;
    return agent.ship.cargo[0].amount * game.markets[starIndex].prices[resourceIndex];
}

int pickPirateVictim(const Game& game, int pirateIndex) {
    if (pirateIndex < 0 || pirateIndex >= int(game.agents.size())) return -1;
    const Agent& pirate = game.agents[pirateIndex];
    if (!validStar(game, pirate.currentStar)) return -1;

    int best = -1;
    double bestScore = 140.0;
    for (size_t i = 0; i < game.agents.size(); ++i) {
        if (int(i) == pirateIndex) continue;
        const Agent& target = game.agents[i];
        if (target.ship.enRoute || target.currentStar != pirate.currentStar || target.type == "pirate") continue;
        if (target.type == "military" && pirate.riskTolerance < 0.8) continue;
        if (validFaction(game, pirate.ship.ownerFaction) && pirate.ship.ownerFaction == target.ship.ownerFaction) continue;

        const double value = cargoValueAt(game, target, pirate.currentStar) + target.money * 0.08;
        const double danger = combatPower(game, target) * 80.0 * (1.25 - pirate.riskTolerance);
        const double score = value * (0.7 + pirate.piracyBias) - danger;
        if (score > bestScore) {
            bestScore = score;
            best = int(i);
        }
    }
    return best;
}

bool resolvePirateAttack(Game& game, int pirateIndex, int targetIndex) {
    if (pirateIndex < 0 || pirateIndex >= int(game.agents.size()) || targetIndex < 0 || targetIndex >= int(game.agents.size())) return false;
    Agent& pirate = game.agents[pirateIndex];
    Agent& target = game.agents[targetIndex];
    if (pirate.ship.enRoute || target.ship.enRoute || pirate.currentStar != target.currentStar) return false;

    const double attackRoll = 0.86 + double(randomer(rng, 28)) / 100.0;
    const double defenseRoll = 0.92 + double(randomer(rng, 24)) / 100.0;
    const double attack = combatPower(game, pirate) * attackRoll;
    const double defense = combatPower(game, target) * defenseRoll;
    const int starIndex = pirate.currentStar;

    if (attack <= defense) {
        pirate.money = std::max(0.0, pirate.money - 80.0);
        pirate.missionCooldown = 1.0;
        target.lastAction = "repelled pirate";
        pirate.lastAction = "raid failed";
        game.adjustFactionRelation(pirate.ship.ownerFaction, target.ship.ownerFaction, -5);
        const unsigned long long combatEvent = allocateSignalEventId(game);
        game.queueCombatSignal(pirate.ship.ownerFaction, starIndex, pirateIndex, targetIndex, attack, combatEvent);
        game.queueCombatSignal(target.ship.ownerFaction, starIndex, pirateIndex, targetIndex, attack, combatEvent);
        if (validFaction(game, game.cluster.stars[starIndex].ownerFaction)) {
            game.queueCombatSignal(game.cluster.stars[starIndex].ownerFaction, starIndex, pirateIndex, targetIndex, attack, combatEvent);
        }
        game.queueDiplomacySignal(pirate.ship.ownerFaction, starIndex, target.ship.ownerFaction, game.factionRelation(pirate.ship.ownerFaction, target.ship.ownerFaction));
        game.queueDiplomacySignal(target.ship.ownerFaction, starIndex, pirate.ship.ownerFaction, game.factionRelation(target.ship.ownerFaction, pirate.ship.ownerFaction));
        game.lastEvent = pirate.ship.name + " failed raid near " + game.cluster.stars[starIndex].name;
        return true;
    }

    double lootValue = 0.0;
    if (!target.ship.cargo.empty()) {
        const int resourceIndex = elementIndex(target.ship.cargo[0].element);
        const double freeMass = std::max(0.0, pirate.ship.cargoCapacity - shipCargoMass(pirate.ship));
        const double massLimitedAmount = resourceIndex >= 0 ? freeMass / resourceUnitMassByIndex(resourceIndex) : 0.0;
        const double amount = std::min(target.ship.cargo[0].amount * 0.55, massLimitedAmount);
        if (amount > 0.01) {
            const std::string element = target.ship.cargo[0].element;
            target.ship.cargo[0].amount -= amount;
            if (target.ship.cargo[0].amount <= 0.01) target.ship.cargo.clear();
            if (pirate.ship.cargo.empty()) {
                pirate.ship.cargo.emplace_back(element, amount);
            } else if (pirate.ship.cargo[0].element == element) {
                pirate.ship.cargo[0].amount += amount;
            }
            const int marketIndex = elementIndex(element);
            if (marketIndex >= 0 && validStar(game, starIndex) && starIndex < int(game.markets.size())) {
                lootValue += amount * game.markets[starIndex].prices[marketIndex];
            }
        }
    }

    const double credits = std::min(target.money * 0.12, 220.0 + lootValue * 0.05);
    target.money -= credits;
    pirate.money += credits;
    pirate.lastProfit = lootValue + credits;
    pirate.trades += 1;
    pirate.missionCooldown = 1.4;
    target.missionCooldown = std::max(target.missionCooldown, 0.7);
    target.lastAction = "robbed";
    pirate.lastAction = "raided";
    game.adjustFactionRelation(pirate.ship.ownerFaction, target.ship.ownerFaction, -16);
    const unsigned long long combatEvent = allocateSignalEventId(game);
    game.queueCombatSignal(pirate.ship.ownerFaction, starIndex, pirateIndex, targetIndex, lootValue + credits, combatEvent);
    game.queueCombatSignal(target.ship.ownerFaction, starIndex, pirateIndex, targetIndex, lootValue + credits, combatEvent);
    if (validFaction(game, game.cluster.stars[starIndex].ownerFaction)) {
        game.queueCombatSignal(game.cluster.stars[starIndex].ownerFaction, starIndex, pirateIndex, targetIndex, lootValue + credits, combatEvent);
    }
    const unsigned long long settlementEvent = allocateSignalEventId(game);
    game.queueSettlementSignal(pirate.ship.ownerFaction, starIndex, credits, settlementEvent);
    game.queueSettlementSignal(target.ship.ownerFaction, starIndex, -credits, settlementEvent);
    game.queueDiplomacySignal(pirate.ship.ownerFaction, starIndex, target.ship.ownerFaction, game.factionRelation(pirate.ship.ownerFaction, target.ship.ownerFaction));
    game.queueDiplomacySignal(target.ship.ownerFaction, starIndex, pirate.ship.ownerFaction, game.factionRelation(target.ship.ownerFaction, pirate.ship.ownerFaction));
    game.lastEvent = pirate.ship.name + " raided " + target.ship.name + " near " + game.cluster.stars[starIndex].name;
    return true;
}

int pickPirateRoute(const Game& game, const Agent& pirate) {
    int best = -1;
    double bestScore = -std::numeric_limits<double>::max();
    const int samples = std::min(72, std::max(16, int(game.cluster.stars.size())));
    for (int i = 0; i < samples; ++i) {
        const int starIndex = randomer(rng, int(game.cluster.stars.size()) - 1);
        if (starIndex == pirate.currentStar || !validStar(game, starIndex)) continue;
        const ClusterStar& star = game.cluster.stars[starIndex];
        if (validFaction(game, pirate.ship.ownerFaction) && star.ownerFaction == pirate.ship.ownerFaction) continue;
        const double distance = validStar(game, pirate.currentStar) ? distanceBetween(game.cluster.stars[pirate.currentStar], star) : 0.0;
        const int relation = validFaction(game, pirate.ship.ownerFaction) ? game.factionRelation(pirate.ship.ownerFaction, star.ownerFaction) : 0;
        const double hostility = relation < 0 ? double(-relation) / 128.0 : 0.25;
        const double score = starPopulationWeight(star) * 1.6 + star.industry * 2.0 + hostility * 10.0 - distance * 0.09;
        if (score > bestScore) {
            bestScore = score;
            best = starIndex;
        }
    }
    return best;
}

void updatePirate(Game& game, int agentIndex, Agent& agent, double dt) {
    if (agent.missionCooldown > 0.0) {
        agent.missionCooldown = std::max(0.0, agent.missionCooldown - dt);
        return;
    }

    const int victim = pickPirateVictim(game, agentIndex);
    if (victim >= 0 && resolvePirateAttack(game, agentIndex, victim)) return;

    if (!agent.ship.cargo.empty()) {
        sellCargo(game, agent, agent.currentStar);
        agent.missionCooldown = 0.4;
        return;
    }

    const int target = pickPirateRoute(game, agent);
    if (target >= 0) startJourney(game, agent, target);
}

int pickScoutTarget(const Game& game, int factionIndex, int currentStar) {
    if (!validFaction(game, factionIndex) || !validStar(game, currentStar)) return -1;

    int best = -1;
    double bestScore = -std::numeric_limits<double>::max();
    const int samples = sampledStarCount(game, 180, 180);
    for (int sample = 0; sample < samples; ++sample) {
        const int starIndex = sampledStarAt(game, sample, samples);
        if (starIndex == currentStar || !validStar(game, starIndex)) continue;
        const bool ownerKnown = game.factionKnowsOwnerAt(factionIndex, currentStar, starIndex);
        const bool marketKnown = game.factionKnowsMarketAt(factionIndex, currentStar, starIndex);
        const double ownerAge = ownerKnown ? game.factionKnownOwnerAgeAt(factionIndex, currentStar, starIndex) : 80.0;
        const double marketAge = marketKnown ? game.factionKnownMarketAgeAt(factionIndex, currentStar, starIndex) : 80.0;
        if (ownerKnown && marketKnown && ownerAge < 8.0 && marketAge < 8.0) continue;

        const ClusterStar& star = game.cluster.stars[starIndex];
        const double distance = distanceBetween(game.cluster.stars[currentStar], star);
        const double score =
            (!ownerKnown ? 24.0 : std::min(16.0, ownerAge * 0.18)) +
            (!marketKnown ? 18.0 : std::min(12.0, marketAge * 0.12)) +
            star.industry * 1.5 + star.habitability * 4.0 -
            distance * 0.08;
        if (score > bestScore) {
            bestScore = score;
            best = starIndex;
        }
    }
    return best;
}

void updateScout(Game& game, Agent& agent, double dt) {
    if (!validFaction(game, agent.ship.ownerFaction)) return;

    if (agent.missionCooldown > 0.0) {
        agent.missionCooldown = std::max(0.0, agent.missionCooldown - dt);
        return;
    }

    if (validStar(game, agent.currentStar)) {
        game.queueOwnerSignal(agent.ship.ownerFaction, agent.currentStar, agent.currentStar);
        game.queueMarketSignal(agent.ship.ownerFaction, agent.currentStar, agent.currentStar);
    }

    int target = pickFactionOrderTarget(game, agent.ship.ownerFaction, FactionOrderType::Scout);
    if (target < 0) target = pickScoutTarget(game, agent.ship.ownerFaction, agent.currentStar);
    if (target >= 0 && startJourney(game, agent, target)) {
        agent.lastAction = "scouting";
    } else {
        agent.missionCooldown = 0.5;
        agent.lastAction = "listening";
    }
}

void updateTrader(Game& game, int agentIndex, Agent& agent, double dt) {
    if (agent.missionCooldown > 0.0) {
        agent.missionCooldown = std::max(0.0, agent.missionCooldown - dt);
        return;
    }

    Contract* contract = activeContractForAgent(game, agentIndex);
    if (contract) {
        if (agent.currentStar == contract->targetStar) {
            game.agentCompleteContract(agentIndex, contract->id);
        } else {
            startJourney(game, agent, contract->targetStar);
        }
        return;
    }

    if (agent.questBias > 0.15 && tryAcceptBestContract(game, agentIndex)) return;

    if (agent.currentStar >= 0 && agent.currentStar < int(game.markets.size())) {
        sellCargo(game, agent, agent.currentStar);
    }

    const TradePlan plan = findBestTrade(game, agent);
    if (plan.destStar >= 0 && plan.elementIndex >= 0) {
        buyCargo(game, agent, agent.currentStar, plan);
        if (!agent.ship.cargo.empty()) {
            startJourney(game, agent, plan.destStar);
        }
    } else {
        agent.missionCooldown = 0.18 + double(randomer(rng, 12)) * 0.01;
        agent.lastAction = "watching market";
    }
}

// (§35) АВТОПИЛОТ-ТОРГОВЕЦ на борту флота игрока.
//
// Узкая копия `updateTrader`, а не вызов его, и на то три причины, каждая из
// которых сломала бы игру:
//   1. `updateTrader` берёт заказы сам (`tryAcceptBestContract`). Заказ игрока —
//      это его репутация и его носители груза; отдавать их автопилоту нельзя.
//   2. `updateTrader` в холостом ходу дёргает `randomer(rng, 12)`, то есть
//      СДВИГАЕТ ГЛОБАЛЬНЫЙ ПОТОК. Один поднятый флаг менял бы весь дальнейший
//      мир — прямое нарушение §2.3. Здесь пауза константой.
//   3. Борт-носитель взятого заказа обязан стоять на месте: иначе автопилот
//      продаст контрактный груз на первом же рынке и улетит, а заказ сгорит по
//      сроку вместе с репутацией.
void updateFleetTrader(Game& game, int agentIndex, Agent& agent, double dt) {
    if (agent.missionCooldown > 0.0) {
        agent.missionCooldown = std::max(0.0, agent.missionCooldown - dt);
        return;
    }
    // Заказы — дело игрока. Борт, который взял заказ ИЛИ везёт чужой груз как
    // носитель, автопилотом не управляется вовсе.
    if (activeContractForAgent(game, agentIndex)) return;
    for (size_t i = 0; i < game.contracts.size(); ++i) {
        const Contract& c = game.contracts[i];
        if (c.completed || c.failed || c.acceptedByAgent < 0) continue;
        for (size_t k = 0; k < c.carriers.size(); ++k) {
            if (c.carriers[k] == agentIndex) return;
        }
    }
    // Отозванная лицензия морозит торговлю. `playerTradingBlocked()` звать
    // нельзя: он не const и пишет в `lastEvent` — строка состояния мигала бы
    // каждый такт.
    // (§47) Морозит СВОЯ лицензия, а не игроцкая: до §47 отзыв у капитана
    // останавливал заодно и торговцев всех пятнадцати держав.
    if (game.licenceOf(agent.ship.ownerFaction).revoked) { agent.missionCooldown = 1.0; return; }
    if (!validStar(game, agent.currentStar)) return;
    // В собственной системе всё даром (§13), а `findBestTrade` считает по
    // рыночным ценам и потому оценивает такой рейс неверно — насос из
    // собственного склада это не торговля.
    //
    // ⚠️ Но «не торговать» не значит «стоять вечно». Сначала здесь был просто
    // выход по таймеру, и замер показал цену такой лаконичности: `freeMarketFor`
    // истинна для ЛЮБОГО борта игрока в его системе, а систему покупают обычно
    // там, где флот и стоит, — то есть кнопка AUTO после покупки дома переставала
    // делать что-либо навсегда и молча (400 лет, 0 сделок против 4 у того же
    // мира без покупки). Поэтому отсюда УЛЕТАЮТ: к ближайшему известному
    // чужому рынку, где торговать уже можно.
    if (freeMarketFor(game, agent, agent.currentStar)) {
        agent.missionCooldown = 1.0;
        int nearest = -1;
        double bestD2 = 1e300;
        const ClusterStar& from = game.cluster.stars[size_t(agent.currentStar)];
        for (size_t st = 0; st < game.cluster.stars.size(); ++st) {
            if (int(st) == agent.currentStar) continue;
            // ⚠️ ОБА знания, как и в `findBestTrade`. Здесь стояло только знание
            // рынка, а планировщик требует ещё и знания владельца — борт улетал
            // туда, где потом сам себе отказывал: замер при разведанных ТОЛЬКО
            // рынках — 400 лет, 0 сделок, 0 перелётов.
            if (!game.factionKnowsMarketAt(agent.ship.ownerFaction, agent.currentStar, int(st))) continue;
            if (!game.factionKnowsOwnerAt(agent.ship.ownerFaction, agent.currentStar, int(st))) continue;
            if (game.playerOwnsStar(int(st))) continue;
            const ClusterStar& to = game.cluster.stars[st];
            const double dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
            const double d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < bestD2) { bestD2 = d2; nearest = int(st); }
        }
        if (nearest >= 0) {
            startJourney(game, agent, nearest);
            agent.lastAction = "auto: leaving home port";
        } else {
            agent.lastAction = "auto: nowhere to trade";
        }
        return;
    }

    sellCargo(game, agent, agent.currentStar);

    // ⚠️ СВОЙ ПОТОК, а не глобальный (§2.3). Ради этого §35 и писался узкой
    // копией `updateTrader` — но обошёл одно обращение к `rng` и оставил 384 в
    // год на борт внутри `findBestTrade`. Засев детерминирован: сид мира, номер
    // борта и текущий год, — значит тот же сейв даёт то же поведение, а мир без
    // автопилота остаётся ПОБИТОВО прежним.
    std::mt19937 fleetRng(static_cast<unsigned int>(game.seed) * 2654435761u +
                          static_cast<unsigned int>(agentIndex) * 40503u +
                          static_cast<unsigned int>(game.time) * 97u + 1013u);
    const TradePlan plan = findBestTrade(game, agent, &fleetRng);
    // ⚠️ Пауза ставится В ЛЮБОМ исходе. Раньше она стояла только в ветке «плана
    // нет», и провалившийся `startJourney` (маршрут не строится, плечо
    // нефизично) оставлял борт покупать и продавать одно и то же на одном рынке
    // каждый такт, пока пошлины не съедят кошелёк. У NPC это тратило деньги NPC,
    // а тут — деньги игрока.
    agent.missionCooldown = 0.25;   // константа: ни бита из глобального rng
    if (plan.destStar >= 0 && plan.elementIndex >= 0) {
        buyCargo(game, agent, agent.currentStar, plan);
        if (!agent.ship.cargo.empty()) startJourney(game, agent, plan.destStar);
        agent.lastAction = "auto: hauling";
    } else {
        agent.lastAction = "auto: watching market";
    }
}

int signalTypeId(SignalType type) {
    switch (type) {
    case SignalType::OwnerReport: return 0;
    case SignalType::MarketReport: return 1;
    case SignalType::ContractReport: return 2;
    case SignalType::CombatReport: return 3;
    case SignalType::SettlementReport: return 4;
    case SignalType::DiplomacyReport: return 5;
    }
    return 0;
}

SignalType signalTypeFromId(int id) {
    switch (id) {
    case 1: return SignalType::MarketReport;
    case 2: return SignalType::ContractReport;
    case 3: return SignalType::CombatReport;
    case 4: return SignalType::SettlementReport;
    case 5: return SignalType::DiplomacyReport;
    default: return SignalType::OwnerReport;
    }
}

int contractTypeId(ContractType type) {
    switch (type) {
    case ContractType::Delivery: return 0;
    case ContractType::Courier: return 1;
    case ContractType::Scout: return 2;
    case ContractType::Bounty: return 3;
    case ContractType::Escort: return 4;
    case ContractType::Raid: return 5;
    case ContractType::ColonySupply: return 6;
    }
    return 0;
}

ContractType contractTypeFromId(int id) {
    switch (id) {
    case 1: return ContractType::Courier;
    case 2: return ContractType::Scout;
    case 3: return ContractType::Bounty;
    case 4: return ContractType::Escort;
    case 5: return ContractType::Raid;
    case 6: return ContractType::ColonySupply;
    default: return ContractType::Delivery;
    }
}

int orderTypeId(FactionOrderType type) {
    switch (type) {
    case FactionOrderType::Trade: return 0;
    case FactionOrderType::Patrol: return 1;
    case FactionOrderType::Colonize: return 2;
    case FactionOrderType::Scout: return 3;
    case FactionOrderType::Raid: return 4;
    case FactionOrderType::AttackSystem: return 5;
    case FactionOrderType::DefendSystem: return 6;
    case FactionOrderType::Courier: return 7;
    }
    return 3;
}

FactionOrderType orderTypeFromId(int id) {
    switch (id) {
    case 0: return FactionOrderType::Trade;
    case 1: return FactionOrderType::Patrol;
    case 2: return FactionOrderType::Colonize;
    case 4: return FactionOrderType::Raid;
    case 5: return FactionOrderType::AttackSystem;
    case 6: return FactionOrderType::DefendSystem;
    case 7: return FactionOrderType::Courier;
    default: return FactionOrderType::Scout;
    }
}

int constructionEffectId(ColonyConstructionEffect effect) {
    switch (effect) {
    case ColonyConstructionEffect::Shipyard: return 1;
    case ColonyConstructionEffect::Automation: return 2;
    case ColonyConstructionEffect::Defense: return 3;
    case ColonyConstructionEffect::None: break;
    }
    return 0;
}

ColonyConstructionEffect constructionEffectFromId(int id) {
    switch (id) {
    case 1: return ColonyConstructionEffect::Shipyard;
    case 2: return ColonyConstructionEffect::Automation;
    case 3: return ColonyConstructionEffect::Defense;
    default: return ColonyConstructionEffect::None;
    }
}

char hexDigit(unsigned value) {
    return value < 10 ? char('0' + value) : char('A' + value - 10);
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::string saveToken(const std::string& value) {
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c == '%' || std::isspace(c)) {
            out.push_back('%');
            out.push_back(hexDigit(c >> 4));
            out.push_back(hexDigit(c & 15));
        } else {
            out.push_back(char(c));
        }
    }
    return out.empty() ? "%" : out;
}

std::string loadToken(const std::string& value) {
    if (value == "%") return "";
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = hexValue(value[i + 1]);
            const int lo = hexValue(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(char((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

void writeDoubleVector(std::ostream& out, const char* tag, const std::vector<double>& values) {
    out << tag << ' ' << values.size();
    for (double value : values) out << ' ' << value;
    out << '\n';
}

bool readDoubleVector(std::istream& in, const char* expectedTag, std::vector<double>& values) {
    std::string tag;
    size_t count = 0;
    if (!(in >> tag >> count) || tag != expectedTag) return false;
    values.assign(count, 0.0);
    for (size_t i = 0; i < count; ++i) {
        if (!(in >> values[i])) return false;
    }
    return true;
}

void writeResourceList(std::ostream& out, const std::vector<Resource>& resources) {
    out << resources.size();
    for (const Resource& resource : resources) {
        out << ' ' << saveToken(resource.element) << ' ' << resource.amount;
    }
}

bool readResourceList(std::istream& in, std::vector<Resource>& resources) {
    size_t count = 0;
    if (!(in >> count)) return false;
    resources.clear();
    resources.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        std::string element;
        double amount = 0.0;
        if (!(in >> element >> amount)) return false;
        resources.emplace_back(loadToken(element), amount);
    }
    return true;
}

void writeIntList(std::ostream& out, const std::vector<int>& values) {
    out << values.size();
    for (int value : values) out << ' ' << value;
}

bool readIntList(std::istream& in, std::vector<int>& values) {
    size_t count = 0;
    if (!(in >> count)) return false;
    values.assign(count, 0);
    for (size_t i = 0; i < count; ++i) {
        if (!(in >> values[i])) return false;
    }
    return true;
}

bool shouldSaveSignalMemoryRecord(const Game& game, int observerStar, const SignalMemoryRecord& record) {
    if (!validStar(game, observerStar) ||
        !validFaction(game, record.recipientFaction) ||
        !validStar(game, record.subjectStar) ||
        record.observedTime < 0.0) {
        return false;
    }

    const double age = std::max(0.0, game.time - record.observedTime);
    switch (record.type) {
    case SignalType::OwnerReport:
        return true;
    case SignalType::MarketReport:
        return !record.marketPrices.empty();
    case SignalType::CombatReport:
        return record.amount > 0.0 && age <= 36.0;
    case SignalType::SettlementReport:
        return !record.absorbed && record.amount != 0.0 && validStar(game, record.destinationStar);
    case SignalType::DiplomacyReport:
        return validFaction(game, record.targetFaction) && age <= 48.0;
    case SignalType::ContractReport:
        return record.contractId >= 0 && age <= 48.0;
    }
    return false;
}

size_t savedSignalMemoryCount(const Game& game) {
    size_t count = 0;
    for (size_t starIndex = 0; starIndex < game.signalMemory.size(); ++starIndex) {
        for (const SignalMemoryRecord& record : game.signalMemory[starIndex]) {
            if (shouldSaveSignalMemoryRecord(game, int(starIndex), record)) count += 1;
        }
    }
    return count;
}

void writeSignalMemoryRecord(std::ostream& out, int observerStar, const SignalMemoryRecord& record) {
    out << "SM " << observerStar << ' ' << signalTypeId(record.type) << ' '
        << record.eventId << ' ' << record.observedTime << ' ' << record.subjectStar << ' ' << record.destinationStar << ' '
        << record.sourceAgent << ' ' << record.targetAgent << ' ' << record.sourceFaction << ' '
        << record.targetFaction << ' ' << record.recipientFaction << ' ' << record.ownerFaction << ' '
        << record.contractId << ' ' << record.amount << ' ' << record.relationValue << ' '
        << record.averageSupplyPressure << ' ' << record.averageDemandPressure << ' '
        << int(record.absorbed) << ' ' << static_cast<int>(record.contractType) << ' '
        << record.contractOriginStar << ' ' << record.contractTargetStar << ' '
        << record.contractTargetAgent << ' ' << record.contractResource << ' '
        << record.contractAcceptedByAgent << ' ' << record.contractAmount << ' '
        << record.contractReward << ' ' << record.contractDeposit << ' '
        << record.contractPostedTime << ' ' << record.contractDeadline << ' '
        << record.contractRisk << ' ' << record.contractProgress << ' '
        << int(record.contractCompleted) << ' ' << int(record.contractFailed) << '\n';
    writeDoubleVector(out, "SMP", record.marketPrices);
    writeDoubleVector(out, "SMS", record.marketSupplyPressure);
    writeDoubleVector(out, "SMD", record.marketDemandPressure);
}

bool expectTag(std::istream& in, const char* expectedTag) {
    std::string tag;
    return bool(in >> tag) && tag == expectedTag;
}

}

// (§5.13.18) Торговец-зеркало (co-located макро-агент) при стыковке в локальном полёте продаёт груз
//   на местном рынке — тот же детерминированный sellCargo, что зовёт макро-updateTrader на прибытии
//   (см. updateTrader). Продаём один передний стак (как updateTrader за тик). true, если продано.
//   Без RNG (sellCargo — чистая арифметика по рынку/деньгам/тарифам), поэтому §2.3-safe: локальный
//   режим может это звать, не трогая глобальный rng. Макро-симуляция на время локального полёта
//   заморожена (main.cpp) ⇒ двойного счёта с updateTrader нет. ВНИМАНИЕ: sellCargo лежит в анонимном
//   namespace (внутренняя линковка) — эта обёртка ОБЯЗАНА жить в game.cpp ВНЕ анонимного namespace,
//   чтобы иметь внешнюю линковку для localsim.cpp, но при этом видеть sellCargo по неквалиф. поиску TU.
bool localDockSellCargo(Game& game, int agentIndex, int starIndex) {
    if (agentIndex < 0 || agentIndex >= (int)game.agents.size()) return false;
    return sellCargo(game, game.agents[agentIndex], starIndex);
}

// (§48, замер) Обёртки над счётчиками «бесплатного прибытия» — по той же причине,
// что и localDockSellCargo: сами счётчики лежат в анонимном namespace.
long long strandStatArrivals() { return s_strandArrivals; }
long long strandStatDryArrivals() { return s_strandDryArrivals; }
long long strandStatOvershoots() { return s_strandOvershoots; }
long long strandStatRiskyDepartures() { return s_strandRiskyDepartures; }
long long strandStatTows() { return s_strandTows; }
long long strandStatFastArrivals() { return s_strandFastArrivals; }
void strandStatTrace(bool on) { s_strandTrace = on; }
double strandStatBurnRatio() { return s_ratioCount ? s_ratioSum / double(s_ratioCount) : 0.0; }
double strandStatBurnRatioMax() { return s_ratioMax; }
double strandStatRapidityRatio() { return s_rapCount ? s_rapSum / double(s_rapCount) : 0.0; }
double strandStatRapidityMax() { return s_rapMax; }
double strandStatHonestRatio() { return s_honestCount ? s_honestSum / double(s_honestCount) : 0.0; }
double strandStatDryReserve() { return s_dryLoadedCount ? s_dryLoadedOverPlanned / double(s_dryLoadedCount) : 0.0; }
double strandStatWorstAny() { return s_strandWorstAny; }
double strandStatWorstSpeed() { return s_strandWorstSpeed; }
// (§50) Спасение: обёртки над теми же счётчиками.
long long strandStatBeacons() { return s_rescueBeacons; }
long long strandStatLiveRescues() { return s_rescueLive; }
long long strandStatStateRescues() { return s_rescueState; }
long long strandStatLastResort() { return s_rescueLastResort; }
long long strandStatLoots() { return s_rescueLoots; }
double strandStatWaitAverage() { return s_rescueWaitCount ? s_rescueWaitSum / double(s_rescueWaitCount) : 0.0; }
double strandStatWaitWorst() { return s_rescueWaitWorst; }
double strandStatLightAverage() { return s_rescueLightCount ? s_rescueLightSum / double(s_rescueLightCount) : 0.0; }
double strandStatBountyPaid() { return s_rescueBountyPaid; }
double strandStatBountyCash() { return s_rescueBountyCash; }
void strandStatReset() { s_strandArrivals = s_strandDryArrivals = s_strandOvershoots = s_strandFastArrivals = s_strandRiskyDepartures = s_strandTows = 0;
    s_strandWorstSpeed = s_strandWorstAny = 0.0;
    s_ratioSum = s_ratioMax = s_dryLoadedOverPlanned = 0.0; s_ratioCount = s_dryLoadedCount = 0;
    s_rapSum = s_rapMax = 0.0; s_rapCount = 0; s_honestSum = 0.0; s_honestCount = 0;
    s_rescueBeacons = s_rescueLive = s_rescueState = s_rescueLastResort = s_rescueLoots = 0;
    s_rescueWaitSum = s_rescueWaitWorst = s_rescueLightSum = 0.0;
    s_rescueWaitCount = s_rescueLightCount = 0;
    s_rescueBountyPaid = s_rescueBountyCash = 0.0; }

Game::Game() : time(0.0) {}

void Game::pushNews(const std::string& text, int kind) {
    NewsItem item;
    item.text = text;
    item.time = time;
    item.kind = kind;
    news.push_back(item);
    const size_t cap = 64;
    if (news.size() > cap) news.erase(news.begin(), news.begin() + (news.size() - cap));
}

bool Game::saveToFile(const std::string& path) {
    std::ofstream out(path.c_str());
    if (!out) {
        lastEvent = "save failed";
        return false;
    }
    out << std::setprecision(17);
    // (§52) Версия 21: блок `MARKETCLOCK` — фаза обхода рынков и таймеры
    // событий. До неё сейв доносил ЦЕНЫ, но не ЧАСЫ, которые эти цены двигают.
    out << "STARCLUSTER_SAVE 21 " << cluster.stars.size() << '\n';
    out << "SEED " << seed << '\n';
    out << "RNG " << rng << '\n';
    out << "TIME " << time << ' ' << contractUpdateTimer << ' ' << factionUpdateTimer << ' '
        << nextContractId << ' ' << playerAgent << ' ' << playerFaction << ' '
        << boughtSystems << ' ' << capturedSystems << ' ' << nextSignalEventId << ' ' << saveToken(lastEvent) << '\n';
    // clusterPriceLevel идёт в той же строке — он тоже про деньги скопления.
    // До версии 13 он жил в файловом глобале `market.cpp` и в сейв не попадал
    // вовсе, поэтому ставка тарифа зависела от того, сколько времени прожил
    // ПРОЦЕСС, а не партия.
    out << "LICENCE " << licence().quotaPaid << ' ' << licence().periodEnd << ' ' << licence().tariffRate << ' '
        << licence().buyback << ' ' << licence().count << ' ' << (licence().revoked ? 1 : 0) << ' '
        << licence().periodsMet << ' ' << licence().quotaBase << ' '
        << clusterPriceLevel << ' ' << clusterPriceBase << ' ' << clearingFaction << '\n';

    out << "STARS " << cluster.stars.size() << '\n';
    for (const ClusterStar& star : cluster.stars) {
        out << "STAR " << star.x << ' ' << star.y << ' ' << star.z << ' '
            << saveToken(star.name) << ' ' << saveToken(star.economyRole) << ' '
            << star.population << ' ' << star.industry << ' ' << star.habitability << ' '
            << star.defense << ' ' << star.ownerFaction << ' ' << star.occupyingFaction << ' '
            << star.captureProgress << ' ' << star.capturePressure << ' ' << star.contestedAt << '\n';
    }

    out << "MARKETS " << markets.size() << '\n';
    for (const Market& market : markets) {
        out << "MARKET " << saveToken(market.role) << ' ';
        writeResourceList(out, market.supply);
        out << ' ';
        writeResourceList(out, market.demand);
        out << '\n';
        writeDoubleVector(out, "PRICES", market.prices);
        writeDoubleVector(out, "PROD", market.productionRate);
        writeDoubleVector(out, "DRATE", market.demandRate);
    }

    out << "FACTIONS " << factions.size() << '\n';
    for (const Faction& faction : factions) {
        out << "FACTION " << saveToken(faction.name) << ' '
            << faction.colorR << ' ' << faction.colorG << ' ' << faction.colorB << ' '
            << faction.homeStar << ' ' << faction.treasury << ' ' << faction.estimatedTreasury << ' '
            << faction.militaryBudget << ' ' << faction.tradeBudget << ' ' << faction.colonyBudget << ' '
            << faction.strength << ' ' << faction.aggression << ' ' << faction.riskTolerance << ' '
            << faction.tradeBias << ' ' << faction.expansionBias << ' ' << faction.defenseBias << ' '
            << faction.diplomacyPressure << ' ' << faction.borderPressure << ' '
            << faction.raidPressure << ' ' << faction.tradePressure << '\n';
        out << "CONTROLLED ";
        writeIntList(out, faction.controlledStars);
        out << '\n';
        out << "FLEETS ";
        writeIntList(out, faction.fleetAgents);
        out << '\n';
        out << "ORDERS " << faction.orders.size() << '\n';
        for (const FactionOrder& order : faction.orders) {
            out << "ORDER " << orderTypeId(order.type) << ' ' << order.originStar << ' '
                << order.targetStar << ' ' << order.targetFaction << ' ' << order.priority << ' '
                << order.createdAt << ' ' << order.assignedAgent << ' ' << int(order.completed) << '\n';
        }
    }

    out << "RELATIONS " << factionRelations.size();
    for (int relation : factionRelations) out << ' ' << relation;
    out << '\n';

    out << "COLONIES " << colonies.size() << '\n';
    for (const Colony& colony : colonies) {
        out << "COLONY " << saveToken(colony.name) << ' ' << colony.population << ' '
            << saveToken(colony.role) << ' ' << colony.starIndex << ' ' << colony.ownerFaction << ' '
            << colony.infrastructure << ' ' << colony.growth << ' ' << colony.automation << ' '
            << colony.energyCapacity << ' ' << colony.defense << ' ' << colony.shipyardLevel << ' '
            << colony.marketAccess << ' ' << colony.damage << ' ' << colony.localLedger << ' '
            << colony.stockpileValue << '\n';
        out << "STOCKPILE ";
        writeResourceList(out, colony.stockpile);
        out << '\n';
        out << "QUEUE " << colony.constructionQueue.size() << '\n';
        for (const ConstructionItem& item : colony.constructionQueue) {
            out << "ITEM " << saveToken(item.name) << ' ' << item.cost << ' '
                << item.progress << ' ' << constructionEffectId(colonyConstructionEffect(item)) << '\n';
        }
    }

    out << "CONTRACTS " << contracts.size() << '\n';
    for (const Contract& contract : contracts) {
        out << "CONTRACT " << contract.id << ' ' << contractTypeId(contract.type) << ' '
            << contract.issuerFaction << ' ' << contract.originStar << ' ' << contract.targetStar << ' '
            << contract.targetAgent << ' ' << contract.resource << ' ' << contract.amount << ' '
            << contract.reward << ' ' << contract.deposit << ' ' << contract.postedTime << ' '
            << contract.deadline << ' ' << contract.risk << ' ' << contract.progress << ' '
            << int(contract.reportSignalPending) << ' ' << int(contract.reportDelivered) << ' '
            << int(contract.escortArrived) << ' ' << contract.acceptedByAgent << ' '
            << int(contract.completed) << ' ' << int(contract.failed) << '\n';
    }

    // --- Довески версии 15 (§24) ---------------------------------------------
    // Новые поля вынесены в ОТДЕЛЬНЫЕ блоки, а не дописаны в конец строк
    // `CONTRACT`/`FACTION`. Так сейв 14 читается прежним разбором строки без
    // единой правки, и обратная совместимость достаётся даром: нет блока —
    // остаются значения по умолчанию.
    out << "JOBEXTRA " << contracts.size() << '\n';
    for (const Contract& contract : contracts) {
        out << "JOBX " << contract.id << ' ' << contract.tier << ' ' << contract.rushFactor << ' '
            << contract.carriers.size();
        for (int carrier : contract.carriers) out << ' ' << carrier;
        // Довесок версии 16: сбита ли цель заказа на голову. Дописан В КОНЕЦ
        // строки, а не отдельным блоком, — разбор 15-й версии читает ровно
        // столько чисел, сколько ждёт, и на лишнее не смотрит.
        // Довесок версии 18: надбавка на дорогу (§46). Дописан тем же приёмом —
        // В КОНЕЦ строки, — и разбор версий 15..17 на него не смотрит.
        out << ' ' << int(contract.targetDown) << ' ' << contract.fuelAllowance << '\n';
    }

    out << "REPUTATION " << factionReputation.size() << '\n';
    for (double reputation : factionReputation) out << reputation << '\n';

    out << "JOURNAL " << transactions.size() << '\n';
    for (const Transaction& entry : transactions) {
        out << "JLINE " << int(entry.kind) << ' ' << entry.time << ' ' << entry.starIndex << ' '
            << entry.amount << ' ' << saveToken(entry.text) << '\n';
    }

    out << "AGENTS " << agents.size() << '\n';
    for (const Agent& agent : agents) {
        out << "AGENT " << saveToken(agent.type) << ' ' << agent.currentStar << ' ' << agent.homeStar << ' '
            << agent.destStar << ' ' << int(agent.toDest) << ' ' << agent.money << ' '
            << agent.cargoCost << ' ' << agent.lastProfit << ' ' << agent.trades << ' '
            << agent.missionCooldown << ' ' << agent.targetFaction << ' ' << int(agent.playerControlled) << ' '
            << agent.tradeBias << ' ' << agent.questBias << ' ' << agent.piracyBias << ' '
            << agent.scoutBias << ' ' << agent.riskTolerance << ' ' << saveToken(agent.lastAction) << '\n';
        const Ship& ship = agent.ship;
        out << "SHIP " << saveToken(ship.name) << ' ' << ship.x << ' ' << ship.y << ' ' << ship.z << ' '
            << ship.speed << ' ' << ship.vx << ' ' << ship.vy << ' ' << ship.vz << ' '
            << ship.acceleration << ' ' << ship.dryMass << ' ' << ship.driveThrust << ' '
            << ship.driveEfficiency << ' ' << ship.driveIndex << ' '
            << ship.fuelVolume << ' ' << ship.propellantVolume << ' ' << ship.throttle << ' '
            << ship.cruiseExhaust << ' ' << ship.cruiseFraction << ' '
            << ship.cargoCapacity << ' ' << ship.ownerFaction << ' '
            << ship.targetStar << ' ' << int(ship.enRoute) << ' '
            << ship.heavyWeapons << ' ' << ship.lightWeapons << ' ' << ship.armor << ' '
            // maxModules — часть корпуса, а не производная: он приходит из
            // таблицы классов (1 у капсулы, 20 у крепости). Без него round-trip
            // возвращал всем бортам конструкторский дефолт 3, то есть капитал
            // молча терял слоты, а перехватчик их получал.
            // miningRig — тот же случай, что и maxModules: бонус ЗАПЕЧЁН в поле
            // (CHROMOCORE §2.4), а список модулей при загрузке заново не
            // применяется. Без записи буровая тихо исчезала бы при загрузке.
            << ship.utility << ' ' << ship.hullHP << ' ' << ship.maxHullHP << ' ' << ship.maxModules
            << ' ' << ship.miningRig << '\n';
        out << "CARGO ";
        writeResourceList(out, ship.cargo);
        out << '\n';
        out << "FUEL ";
        writeResourceList(out, ship.fuel);
        out << '\n';
        out << "PROPELLANT ";
        writeResourceList(out, ship.propellant);
        out << '\n';
        out << "MODULES ";
        writeIntList(out, ship.modules);
        out << '\n';
        // (§31) Экзотика в удерживающей ячейке. Ёмкость (`containment`) не
        // пишется: она ЗАПЕКАЕТСЯ из `containmentLevel` при загрузке — одно
        // авторитетное число вместо двух расходящихся.
        //
        // ⚠️ Флаг автопилота (§35) едет ЗДЕСЬ, а не в строке `AGENT`: та
        // читается фиксированной последовательностью токенов и упирается в тег
        // `SHIP`, поэтому дописывать в неё поле нельзя — старый сейв отдал бы
        // в число слово «SHIP» и загрузка развалилась бы.
        out << "EXOHOLD";
        for (int k = 0; k < EX_COUNT; ++k) out << ' ' << ship.exotic[k];
        out << ' ' << int(agent.autoTrade) << ' ' << ship.containmentLevel << ' '
            << ship.platingLayers << '\n';
    }

    size_t knowledgeCount = 0;
    for (size_t f = 0; f < factions.size(); ++f) {
        for (size_t s = 0; s < cluster.stars.size(); ++s) {
            const size_t index = factionKnowledgeIndex(*this, int(f), int(s));
            const bool ownerKnown = index < factionKnowledge.size() && factionKnowledge[index].ownerKnown;
            const bool marketKnown = index < factionMarketKnowledge.size() && factionMarketKnowledge[index].known;
            if (ownerKnown || marketKnown) knowledgeCount += 1;
        }
    }
    out << "KNOWLEDGE " << knowledgeCount << '\n';
    for (size_t f = 0; f < factions.size(); ++f) {
        for (size_t s = 0; s < cluster.stars.size(); ++s) {
            const size_t index = factionKnowledgeIndex(*this, int(f), int(s));
            const bool ownerKnown = index < factionKnowledge.size() && factionKnowledge[index].ownerKnown;
            const bool marketKnown = index < factionMarketKnowledge.size() && factionMarketKnowledge[index].known;
            if (!ownerKnown && !marketKnown) continue;
            const FactionStarKnowledge owner = ownerKnown ? factionKnowledge[index] : FactionStarKnowledge();
            const FactionMarketKnowledge market = marketKnown ? factionMarketKnowledge[index] : FactionMarketKnowledge();
            out << "K " << f << ' ' << s << ' ' << int(ownerKnown) << ' ' << owner.ownerFaction << ' '
                << owner.ownerKnownAt << ' ' << int(owner.visited) << ' ' << int(marketKnown) << ' '
                << market.observedAt << ' ' << market.averageSupplyPressure << ' ' << market.averageDemandPressure << '\n';
            if (marketKnown) {
                const size_t resources = elementCount();
                out << "KP " << resources;
                for (size_t e = 0; e < resources; ++e) out << ' ' << factionMarketPrices[factionMarketPriceIndex(*this, int(f), int(s), int(e))];
                out << '\n';
                out << "KS " << resources;
                for (size_t e = 0; e < resources; ++e) out << ' ' << factionMarketSupplyPressure[factionMarketPriceIndex(*this, int(f), int(s), int(e))];
                out << '\n';
                out << "KD " << resources;
                for (size_t e = 0; e < resources; ++e) out << ' ' << factionMarketDemandPressure[factionMarketPriceIndex(*this, int(f), int(s), int(e))];
                out << '\n';
            }
        }
    }

    size_t playerKnowledgeCount = 0;
    for (const PlayerStarKnowledge& knowledge : playerKnowledge) {
        if (knowledge.ownerKnown) playerKnowledgeCount += 1;
    }
    out << "PLAYER_KNOWLEDGE " << playerKnowledgeCount << '\n';
    for (size_t i = 0; i < playerKnowledge.size(); ++i) {
        const PlayerStarKnowledge& knowledge = playerKnowledge[i];
        if (!knowledge.ownerKnown) continue;
        out << "PK " << i << ' ' << knowledge.ownerFaction << ' ' << knowledge.ownerKnownAt << ' '
            << int(knowledge.visited) << '\n';
    }

    out << "SIGNALS " << pendingSignals.size() << '\n';
    for (const SignalPacket& signal : pendingSignals) {
        out << "SIGNAL " << signalTypeId(signal.type) << ' ' << signal.eventId << ' ' << signal.observedTime << ' '
            << signal.sendTime << ' ' << signal.arrivalTime << ' ' << signal.originStar << ' '
            << signal.destinationStar << ' ' << signal.hopStar << ' ' << signal.subjectStar << ' ' << signal.sourceAgent << ' '
            << signal.targetAgent << ' ' << signal.sourceFaction << ' ' << signal.targetFaction << ' '
            << signal.recipientFaction << ' ' << signal.ownerFaction << ' ' << signal.contractId << ' '
            << signal.amount << ' ' << signal.relationValue << ' ' << static_cast<int>(signal.contractType) << ' '
            << signal.contractOriginStar << ' ' << signal.contractTargetStar << ' '
            << signal.contractTargetAgent << ' ' << signal.contractResource << ' '
            << signal.contractAcceptedByAgent << ' ' << signal.contractAmount << ' '
            << signal.contractReward << ' ' << signal.contractDeposit << ' '
            << signal.contractPostedTime << ' ' << signal.contractDeadline << ' '
            << signal.contractRisk << ' ' << signal.contractProgress << ' '
            << int(signal.contractCompleted) << ' ' << int(signal.contractFailed) << '\n';
        writeDoubleVector(out, "MP", signal.marketPrices);
        writeDoubleVector(out, "MS", signal.marketSupplyPressure);
        writeDoubleVector(out, "MD", signal.marketDemandPressure);
    }

    out << "SIGNAL_MEMORY " << savedSignalMemoryCount(*this) << '\n';
    for (size_t starIndex = 0; starIndex < signalMemory.size(); ++starIndex) {
        for (const SignalMemoryRecord& record : signalMemory[starIndex]) {
            if (shouldSaveSignalMemoryRecord(*this, int(starIndex), record)) {
                writeSignalMemoryRecord(out, int(starIndex), record);
            }
        }
    }

    out << "TECH " << tech.intellect << ' ' << tech.charisma << ' ' << tech.materials << ' '
        << tech.tactics << ' ' << tech.kinematics << ' ' << tech.sensors << ' ' << tech.luck << ' '
        << tech.cores << ' ' << tech.research << '\n';

    out << "MARKET_EVENTS " << marketEvents.size() << '\n';
    for (const MarketEvent& ev : marketEvents) {
        out << "MEV " << ev.star << ' ' << int(ev.kind) << ' ' << ev.startTime << ' '
            << ev.endTime << ' ' << ev.magnitude << ' ' << int(ev.announced) << ' ';
        writeIntList(out, ev.elements);
        out << '\n';
    }

    out << "ANOMALIES " << anomalies.size() << '\n';
    for (const Anomaly& a : anomalies) {
        out << "ANOM " << a.x << ' ' << a.y << ' ' << a.z << ' ' << int(a.kind) << ' '
            << int(a.discovered) << ' ' << int(a.resolved) << ' ' << a.lootElement << ' '
            << a.lootAmount << ' ' << a.credits << ' ' << a.chromocoreStat << ' ' << a.hazard << ' '
            << a.nearStar << ' ' << saveToken(a.name) << '\n';
    }

    out << "NEWS " << news.size() << '\n';
    for (const NewsItem& n : news) {
        out << "NEWSITEM " << n.time << ' ' << n.kind << ' ' << saveToken(n.text) << '\n';
    }

    out << "MINING " << int(playerMining) << ' ' << miningTimer << ' ' << miningStar << ' '
        << miningYieldAccum << '\n';

    // Приход, ещё не покрытый светом. Без него сейв «печатал» бы деньги:
    // счёт полон, а в пути ничего — то есть всё сразу доступно к трате.
    out << "CREDIT_FLOAT " << creditFloat.size() << '\n';
    for (const CreditFloat& c : creditFloat) {
        out << "CF " << c.faction << ' ' << c.amount << ' ' << c.clearsAt << '\n';
    }

    // --- Довески версии 16 (§32) ---
    // Заявки микромира. Без них сейв возвращал бы забранные радиоисточники и
    // выбранные бюджеты наград — то есть чинил бы ровно ту дыру, ради которой
    // они и заведены.
    out << "LOCAL_CLAIMS " << localClaims.size() << ' ' << playerShieldFrac << '\n';
    for (const LocalClaims& c : localClaims) {
        // Довесок версии 21 — выработанность пояса — дописан В КОНЕЦ строки:
        // разбор версий 16..20 читает ровно четыре числа и на лишние не смотрит.
        out << "LC " << c.starIndex << ' ' << c.radioMask << ' ' << c.bountyPaid << ' '
            << c.bountyAt << ' ' << c.minedMass << ' ' << c.minedAt << '\n';
    }

    // Хайтек-этаж (§31). Ступень ячейки и слои брони обязаны быть здесь: они
    // ЗАПЕКАЮТСЯ в поля корабля, а поля пересобираются из таблицы классов, —
    // не сохранив их, загрузка тихо разденет корпус (та же грабля §32.2).
    // (§33) Акции держав. Портфель и КНИГИ: без книг котировка после загрузки
    // была бы нулевой до первого такта фракций, то есть портфель на миллиард
    // показывался бы пустым, а продать его было бы нельзя.
    out << "SHARES " << factions.size() << ' ' << factionBookCursor << '\n';
    for (size_t f = 0; f < factions.size(); ++f) {
        out << "SH " << (f < factionBook.size() ? factionBook[f] : 0.0) << ' '
            << (f < factionIncome.size() ? factionIncome[f] : 0.0) << ' '
            << (f < factionBookAt.size() ? factionBookAt[f] : -1.0e18) << ' '
            << (f < playerShares.size() ? playerShares[f] : 0.0) << ' '
            << (f < shareCostBasis.size() ? shareCostBasis[f] : 0.0) << '\n';
    }

    // (§47) Книги лицензий ВСЕХ шестнадцати и доли держав друг в друге. Без
    // этого блока загрузка молча теряла бы половину скопления: у держав
    // обнулялись квоты и портфели, то есть сохранённый мир возвращался другим.
    out << "LICENCEBOOK " << factions.size() << '\n';
    for (size_t f = 0; f < factions.size(); ++f) {
        const FactionLicence& book = licenceOf(int(f));
        out << "LB " << book.quotaPaid << ' ' << book.quotaBase << ' ' << book.periodEnd << ' '
            << book.tariffRate << ' ' << book.buyback << ' ' << book.count << ' '
            << (book.revoked ? 1 : 0) << ' ' << book.periodsMet << '\n';
    }
    out << "BUYCLOCK " << factionNextSystemBuy.size() << ' ' << factionNextFleetBuy.size() << '\n';
    for (size_t f = 0; f < factionNextSystemBuy.size(); ++f) {
        out << factionNextSystemBuy[f] << (((f + 1) % 16 == 0) ? '\n' : ' ');
    }
    if (!factionNextSystemBuy.empty() && factionNextSystemBuy.size() % 16 != 0) out << '\n';
    for (size_t f = 0; f < factionNextFleetBuy.size(); ++f) {
        out << factionNextFleetBuy[f] << (((f + 1) % 16 == 0) ? '\n' : ' ');
    }
    if (!factionNextFleetBuy.empty() && factionNextFleetBuy.size() % 16 != 0) out << '\n';

    out << "FACTIONSHARES " << factionShares.size() << ' ' << factionExpansionCursor << '\n';
    for (size_t i = 0; i < factionShares.size(); ++i) {
        out << factionShares[i] << (((i + 1) % 16 == 0) ? '\n' : ' ');
    }
    if (!factionShares.empty() && factionShares.size() % 16 != 0) out << '\n';

    out << "EXOTIC " << exoticStocks.size() << ' ' << coresForged << '\n';
    for (const ExoticStock& e : exoticStocks) {
        out << "EX " << e.starIndex << ' ' << e.updatedAt;
        for (int k = 0; k < EX_COUNT; ++k) out << ' ' << e.stock[k];
        out << '\n';
    }

    // (§46, версия 18) ПРОГРЕСС, КОТОРЫЙ ОТКАТЫВАЛСЯ КАЖДОЙ ЗАГРУЗКОЙ.
    // Три поля писались и читались в игре, но в сейв не попадали:
    //  • `everEnteredLocal` — панель целей показывает окно вокруг первой
    //    незакрытой ступени, и без флага игроку с ячейкой удержания и кузницей
    //    после загрузки снова писали «ЛЕТАТЬ В СИСТЕМЕ: L», а весь хайтек-этаж
    //    уходил с экрана (замер: 1 -> 0);
    //  • `tradeInsightPending`/`tradeInsightTimer` — накопленная торговлей
    //    прокачка (§34) тратится раз в ГОД симуляции, значит игрок, который
    //    сохраняется чаще, не получал от торговли ни одного очка (замер:
    //    12345 -> 0).
    out << "PROGRESS " << (everEnteredLocal ? 1 : 0) << ' '
        << tradeInsightPending << ' ' << tradeInsightTimer << ' '
        << exoticUnitsSold << '\n';

    // (§50) БЕДА ПЕРЕЖИВАЕТ ЗАГРУЗКУ. До этого `driftYears` не сохранялся
    // намеренно: ждать было нечего, помощь приходила по таймеру. С маяком у
    // беды появился возраст — сигнал ушёл в такой-то год, спасатель уже в пути,
    // — и терять это при загрузке значило бы звать помощь дважды и заново
    // отсчитывать ход света.
    //
    // Блок отдельный и один на всё, что добавил §50: маяки, состояние
    // спасателей и годы дрейфа. Дописывать поля в строку `AGENT` нельзя —
    // последним в ней идёт токен `lastAction`, и разбор старой версии схватил
    // бы вместо него следующий тег (та же грабля, что развела версии 16 и 17).
    // Спасатели пишутся РАЗРЕЖЕННО: их единицы на тысячу бортов.
    // ⚠️ `distressTimer` СОХРАНЯЕТСЯ, хотя это доля года внутри одного опроса.
    // Сначала он был объявлен «не стоящим сейва»: после загрузки диспетчер
    // отработал бы на четверть года позже, а ожидание меряется годами. Проверка
    // полного мира это опровергла: круг «сейв -> загрузка» сходился точно, но
    // сорок лет ПОСЛЕ загрузки расходились с оригиналом (казна 3.908e9 против
    // 3.861e9). Сдвиг фазы опроса меняет, кого назначат спасателем, а дальше
    // расходится всё. Детерминизм мира (§2.3) — это про будущее, а не только
    // про момент загрузки.
    out << "DISTRESS " << distress.size() << ' ' << distressTimer << '\n';
    for (const DistressBeacon& beacon : distress) {
        out << "DB " << beacon.agent << ' ' << beacon.x << ' ' << beacon.y << ' ' << beacon.z << ' '
            << beacon.vx << ' ' << beacon.vy << ' ' << beacon.vz << ' '
            << beacon.raisedTime << ' ' << beacon.responder << ' '
            << (beacon.stateSent ? 1 : 0) << ' ' << (beacon.looted ? 1 : 0) << '\n';
    }
    size_t rescuerCount = 0;
    for (const Agent& agent : agents) {
        if (agent.rescueTarget >= 0 || agent.driftYears > 0.0) ++rescuerCount;
    }
    out << "RESCUERS " << rescuerCount << '\n';
    for (size_t i = 0; i < agents.size(); ++i) {
        const Agent& agent = agents[i];
        if (agent.rescueTarget < 0 && agent.driftYears <= 0.0) continue;
        out << "RS " << i << ' ' << agent.rescueTarget << ' ' << (agent.rescueKnows ? 1 : 0) << ' '
            << agent.rescueResumeStar << ' ' << agent.driftYears << '\n';
    }

    // (§52) ЧАСЫ РЫНКОВ. Цены сохранялись с самого начала, а часы, которые их
    // двигают, — нет, и загрузка ставила всем звёздам `time - INTERVAL`, то
    // есть «каждой пора обновиться на целый интервал». Рынки обходятся по
    // кругу курсором с дробным бюджетом (`updateMarkets`), поэтому в живом мире
    // у каждой звезды свой возраст обновления; после загрузки все получали
    // ОДИН И ТОТ ЖЕ, максимальный. Момент загрузки от этого не менялся — цены
    // ведь сохранены, — а будущее менялось: замер на 600 звёздах, 20 лет до
    // сейва и 10 после, дал казну 1.10327e9 против 1.12378e9 (1.9%).
    // ⚠️ Отсюда и правило: круг «сохранил -> загрузил -> сравнил» такое НЕ
    // ловит, ловит только «покрутил ОБЕ копии -> сравнил» (§51.5).
    //
    // Таймеры событий и аномалий в той же строке: они тоже фаза, а не
    // состояние, и обнулением сдвигали, в каком году случится следующее.
    out << "MARKETCLOCK " << marketUpdatedAt.size() << ' ' << marketUpdateCursor << ' '
        << marketUpdateBudget << ' ' << marketEventTimer << ' ' << anomalyTimer << '\n';
    for (size_t i = 0; i < marketUpdatedAt.size(); ++i) {
        out << marketUpdatedAt[i] << (((i + 1) % 16 == 0) ? '\n' : ' ');
    }
    if (!marketUpdatedAt.empty() && marketUpdatedAt.size() % 16 != 0) out << '\n';

    // (§52) ЗАДЫХАЕМОСТЬ СИСТЕМЫ. `strain` и `serviceCostAvg` при загрузке
    // пересеивались в ноль вместе с моделью нужды (см. ⚠️ в разборе `MARKET`):
    // их считали чистой функцией звезды. Это верно для `needs`, `pref`,
    // `tradeAccess` и `seededScale` — те и правда выводятся из населения с
    // индустрией, — но не для этих двух: они НАКАПЛИВАЮТСЯ обновлением рынка.
    // А обновление ходит по кругу курсором, то есть до своей очереди рынок
    // стоит с нулём, тогда как колонии читают `strain` КАЖДЫЙ такт
    // (`supplySatisfaction`, game.cpp). Отсюда и расходилось население звёзд
    // на первом же году после загрузки.
    // (§52) Чем игрок зарабатывал исследования после последнего ядра: от этого
    // зависит, каким ядро выйдет. Без сейва накопленное занятие терялось, и
    // сохранившийся перед самым ядром игрок получал его случайным.
    out << "LEAN " << researchLean.size();
    for (size_t k = 0; k < researchLean.size(); ++k) out << ' ' << researchLean[k];
    out << '\n';

    out << "MARKETSTATE " << markets.size() << '\n';
    for (size_t i = 0; i < markets.size(); ++i) {
        out << markets[i].strain << ' ' << markets[i].serviceCostAvg << ' '
            << markets[i].seededScale << (((i + 1) % 8 == 0) ? '\n' : ' ');
    }
    if (!markets.empty() && markets.size() % 8 != 0) out << '\n';

    if (!out) {
        lastEvent = "save failed";
        return false;
    }
    lastEvent = "saved " + path;
    return true;
}

bool Game::loadFromFile(const std::string& path) {
    std::ifstream in(path.c_str());
    if (!in) {
        lastEvent = "load failed";
        return false;
    }

    std::string tag;
    int version = 0;
    size_t starCount = 0;
    // Читаем 14..17. Ломать сохранения ради новых полей не нужно: всё, что
    // добавила §24, лежит в отдельных блоках и при version==14 просто
    // отсутствует (см. «довески версии 15» ниже); то же и с §32 в версии 16.
    //
    // ⚠️ 17 отделена от 16 не ради красоты. В строку `JOBX` дописан признак
    // «цель заказа сбита», а в `EXOHOLD` — флаг автопилота: оба дописаны В
    // КОНЕЦ СТРОКИ, и разбор версии 16 схватил бы вместо них следующий тег.
    // Правило «новое поле — отдельным блоком ИЛИ новой версией» здесь работает
    // вторым способом.
    // 18 добавлена блоком `PROGRESS` в конце (§46) — три поля прогресса, что
    // писались и читались в игре, но в сейв не попадали. Блок отдельный, значит
    // старые сейвы читаются прежним путём.
    if (!(in >> tag >> version >> starCount) || tag != "STARCLUSTER_SAVE" ||
        version < 14 || version > 21) {
        lastEvent = "load failed: version";
        return false;
    }

    Game loaded;
    loaded.seed = 42;
    if (version >= 7) {
        if (!expectTag(in, "SEED") || !(in >> loaded.seed)) {
            lastEvent = "load failed: seed";
            return false;
        }
    }
    loaded.cluster.generate(starCount, loaded.seed);
    loaded.markets.assign(starCount, Market());
    loaded.playerKnowledge.assign(starCount, PlayerStarKnowledge());
    loaded.time = 0.0;

    std::mt19937 loadedRng;
    if (!expectTag(in, "RNG") || !(in >> loadedRng)) {
        lastEvent = "load failed: rng";
        return false;
    }

    std::string eventToken;
    if (!expectTag(in, "TIME") ||
        !(in >> loaded.time >> loaded.contractUpdateTimer >> loaded.factionUpdateTimer >>
            loaded.nextContractId >> loaded.playerAgent >> loaded.playerFaction >>
            loaded.boughtSystems >> loaded.capturedSystems >> loaded.nextSignalEventId >> eventToken)) {
        lastEvent = "load failed: time";
        return false;
    }
    loaded.lastEvent = loadToken(eventToken);
    if (loaded.nextSignalEventId == 0) loaded.nextSignalEventId = 1;

    // ⚠️ Читаем во ВРЕМЕННУЮ строку, а не сразу в книгу (§47). Фракции грузятся
    // НИЖЕ, и до них `licence()` не к чему обратиться: игрока в книге ещё нет,
    // и всё прочитанное ушло бы в запасную строку — то есть в никуда. Молча:
    // лицензия загружалась бы девственно чистой, а игрок получал бы назад
    // отозванную лицензию как новенькую.
    FactionLicence loadedLicence;
    if (version >= 8) {
        int revoked = 0;
        if (!expectTag(in, "LICENCE") ||
            !(in >> loadedLicence.quotaPaid >> loadedLicence.periodEnd >> loadedLicence.tariffRate >>
                loadedLicence.buyback >> loadedLicence.count >> revoked >> loadedLicence.periodsMet >>
                loadedLicence.quotaBase >> loaded.clusterPriceLevel >> loaded.clusterPriceBase >> loaded.clearingFaction)) {
            lastEvent = "load failed: licence";
            return false;
        }
        loadedLicence.revoked = revoked != 0;
    } else {
        // Сейв до введения квоты: начинаем новый отчётный период с текущего момента.
        loadedLicence.periodEnd = loaded.time + LICENCE_PERIOD_YEARS;
    }

    size_t count = 0;
    if (!expectTag(in, "STARS") || !(in >> count) || count != loaded.cluster.stars.size()) {
        lastEvent = "load failed: stars";
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        std::string name;
        std::string role;
        ClusterStar& star = loaded.cluster.stars[i];
        if (!expectTag(in, "STAR") ||
            !(in >> star.x >> star.y >> star.z >> name >> role >> star.population >>
                star.industry >> star.habitability >> star.defense >> star.ownerFaction >>
                star.occupyingFaction >> star.captureProgress >> star.capturePressure >> star.contestedAt)) {
            lastEvent = "load failed: star";
            return false;
        }
        star.name = loadToken(name);
        star.economyRole = loadToken(role);
    }
    // Имена пришли из файла, а не из seed (сейв мог быть снят другой сборкой),
    // поэтому реестр локализации пересобираем по загруженным именам.
    loaded.cluster.registerNames();

    if (!expectTag(in, "MARKETS") || !(in >> count) || count != starCount) {
        lastEvent = "load failed: markets";
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        std::string role;
        if (!expectTag(in, "MARKET") || !(in >> role)) {
            lastEvent = "load failed: market";
            return false;
        }
        // ⚠️ Рынок ОБЯЗАН быть засеян до наложения сохранённого состояния.
        //
        // В файле лежат только «движущиеся части» — запас, цены, темпы. Модель
        // НУЖДЫ (`needs`, `rationing`, `pref`, `tradeAccess`, `serviceCostAvg`)
        // — чистая функция звезды, и в сейв она не пишется. Раньше её никто и
        // не восстанавливал: `Market::update` на первом же такте видел пустой
        // `needs`, заполнял его НУЛЯМИ, и спрос исчезал навсегда. Замер до
        // правки (один и тот же мир, 60 лет после загрузки): needs 1744 -> 0,
        // demandRate 9852 -> 0, цена 4.30 -> 0.34, оборот системы 1.26e5 ->
        // 7.7e3, `strain` вечный ноль (колонии «здоровы» при пустом рынке),
        // `measureClusterServiceCost()` -> 0, то есть тысячелетняя сверка и
        // ставка тарифа замирали. Партия после первой загрузки была ДРУГОЙ
        // игрой — и об этом не было ни строчки в интерфейсе.
        //
        // Звёзды читаются выше, поэтому здесь уже доступны их сохранённые
        // население и индустрия: сеем ровно теми же данными, что и `init`.
        const ClusterStar& star = loaded.cluster.stars[i];
        Market market;
        market.seed(star.resources, star.demandBias, loadToken(role), star.population, star.industry);
        market.role = loadToken(role);
        if (!readResourceList(in, market.supply) || !readResourceList(in, market.demand) ||
            !readDoubleVector(in, "PRICES", market.prices) ||
            !readDoubleVector(in, "PROD", market.productionRate) ||
            !readDoubleVector(in, "DRATE", market.demandRate)) {
            lastEvent = "load failed: market vectors";
            return false;
        }
        loaded.markets[i] = market;
    }

    if (!expectTag(in, "FACTIONS") || !(in >> count)) {
        lastEvent = "load failed: factions";
        return false;
    }
    loaded.factions.clear();
    loaded.factions.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        std::string name;
        Faction faction("");
        if (!expectTag(in, "FACTION") ||
            !(in >> name >> faction.colorR >> faction.colorG >> faction.colorB >> faction.homeStar >>
                faction.treasury >> faction.estimatedTreasury >> faction.militaryBudget >>
                faction.tradeBudget >> faction.colonyBudget >> faction.strength >> faction.aggression >>
                faction.riskTolerance >> faction.tradeBias >> faction.expansionBias >> faction.defenseBias >>
                faction.diplomacyPressure >> faction.borderPressure >> faction.raidPressure >> faction.tradePressure)) {
            lastEvent = "load failed: faction";
            return false;
        }
        faction.name = loadToken(name);
        if (!expectTag(in, "CONTROLLED") || !readIntList(in, faction.controlledStars) ||
            !expectTag(in, "FLEETS") || !readIntList(in, faction.fleetAgents)) {
            lastEvent = "load failed: faction lists";
            return false;
        }
        size_t orderCount = 0;
        if (!expectTag(in, "ORDERS") || !(in >> orderCount)) {
            lastEvent = "load failed: orders";
            return false;
        }
        faction.orders.clear();
        faction.orders.reserve(orderCount);
        for (size_t o = 0; o < orderCount; ++o) {
            int type = 0;
            int completed = 0;
            FactionOrder order;
            if (!expectTag(in, "ORDER") ||
                !(in >> type >> order.originStar >> order.targetStar >> order.targetFaction >>
                    order.priority >> order.createdAt >> order.assignedAgent >> completed)) {
                lastEvent = "load failed: order";
                return false;
            }
            order.type = orderTypeFromId(type);
            order.completed = completed != 0;
            faction.orders.push_back(order);
        }
        loaded.factions.push_back(faction);
    }
    // Фракции есть — только теперь книге лицензий есть куда лечь (§47).
    loaded.resizeFactionLicence();
    loaded.licence() = loadedLicence;

    size_t relationCount = 0;
    if (!expectTag(in, "RELATIONS") || !(in >> relationCount)) {
        lastEvent = "load failed: relations";
        return false;
    }
    loaded.factionRelations.assign(relationCount, 0);
    for (size_t i = 0; i < relationCount; ++i) {
        if (!(in >> loaded.factionRelations[i])) {
            lastEvent = "load failed: relation";
            return false;
        }
    }
    if (relationCount != loaded.factions.size() * loaded.factions.size()) {
        lastEvent = "load failed: relation size";
        return false;
    }
    for (size_t i = 0; i < loaded.factions.size(); ++i) {
        loaded.factions[i].relationRowOffset = int(i * loaded.factions.size());
    }

    if (!expectTag(in, "COLONIES") || !(in >> count)) {
        lastEvent = "load failed: colonies";
        return false;
    }
    loaded.colonies.clear();
    loaded.colonies.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        std::string name;
        std::string role;
        size_t population = 0;
        if (!expectTag(in, "COLONY") || !(in >> name >> population >> role)) {
            lastEvent = "load failed: colony";
            return false;
        }
        Colony colony(loadToken(name), population, loadToken(role));
        if (!(in >> colony.starIndex >> colony.ownerFaction >> colony.infrastructure >> colony.growth >>
            colony.automation >> colony.energyCapacity >> colony.defense >> colony.shipyardLevel >>
            colony.marketAccess >> colony.damage >> colony.localLedger >> colony.stockpileValue)) {
            lastEvent = "load failed: colony state";
            return false;
        }
        if (!expectTag(in, "STOCKPILE") || !readResourceList(in, colony.stockpile)) {
            lastEvent = "load failed: stockpile";
            return false;
        }
        size_t queueCount = 0;
        if (!expectTag(in, "QUEUE") || !(in >> queueCount)) {
            lastEvent = "load failed: queue";
            return false;
        }
        colony.constructionQueue.clear();
        colony.constructionQueue.reserve(queueCount);
        for (size_t q = 0; q < queueCount; ++q) {
            std::string itemName;
            double cost = 0.0;
            double progress = 0.0;
            int effect = 0;
            if (!expectTag(in, "ITEM") || !(in >> itemName >> cost >> progress >> effect)) {
                lastEvent = "load failed: item";
                return false;
            }
            colony.constructionQueue.push_back(ConstructionItem(loadToken(itemName), cost, constructionEffectFromId(effect)));
            colony.constructionQueue.back().progress = progress;
        }
        loaded.colonies.push_back(colony);
    }

    if (!expectTag(in, "CONTRACTS") || !(in >> count)) {
        lastEvent = "load failed: contracts";
        return false;
    }
    loaded.contracts.clear();
    loaded.contracts.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        int type = 0;
        int reportSignalPending = 0;
        int reportDelivered = 0;
        int escortArrived = 0;
        int completed = 0;
        int failed = 0;
        Contract contract;
        if (!expectTag(in, "CONTRACT") ||
            !(in >> contract.id >> type >> contract.issuerFaction >> contract.originStar >>
                contract.targetStar >> contract.targetAgent >> contract.resource >> contract.amount >>
                contract.reward >> contract.deposit >> contract.postedTime >> contract.deadline >>
                contract.risk >> contract.progress >> reportSignalPending >> reportDelivered >>
                escortArrived >> contract.acceptedByAgent >> completed >> failed)) {
            lastEvent = "load failed: contract";
            return false;
        }
        contract.type = contractTypeFromId(type);
        contract.reportSignalPending = reportSignalPending != 0;
        contract.reportDelivered = reportDelivered != 0;
        contract.escortArrived = escortArrived != 0;
        contract.completed = completed != 0;
        contract.failed = failed != 0;
        loaded.contracts.push_back(contract);
    }

    // --- Довески версии 15 (§24) ---------------------------------------------
    // Сейв 14 этих блоков не содержит: тир, срочность, носители, репутация и
    // журнал остаются нулевыми, и старая партия продолжается как заказы нулевой
    // репутации. Ломать чужие сохранения ради новой механики не нужно.
    if (version >= 15) {
        if (!expectTag(in, "JOBEXTRA") || !(in >> count)) {
            lastEvent = "load failed: job tiers";
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            int id = 0;
            size_t carriers = 0;
            double tier = 0.0;
            double rush = 1.0;
            if (!expectTag(in, "JOBX") || !(in >> id >> tier >> rush >> carriers)) {
                lastEvent = "load failed: job tier";
                return false;
            }
            std::vector<int> carrierList;
            carrierList.reserve(carriers);
            for (size_t c = 0; c < carriers; ++c) {
                int agentIndex = -1;
                if (!(in >> agentIndex)) {
                    lastEvent = "load failed: job carriers";
                    return false;
                }
                carrierList.push_back(agentIndex);
            }
            // Блок пишется тем же порядком, что и `CONTRACTS`, но привязка идёт
            // ПО ID, а не по индексу: так строка переживёт любую будущую чистку
            // списка заказов между двумя блоками.
            int targetDown = 0;
            if (version >= 17 && !(in >> targetDown)) {
                lastEvent = "load failed: job target";
                return false;
            }
            double allowance = 0.0;
            if (version >= 18 && !(in >> allowance)) {
                lastEvent = "load failed: job fuel allowance";
                return false;
            }
            for (Contract& contract : loaded.contracts) {
                if (contract.id != id) continue;
                contract.targetDown = targetDown != 0;
                contract.tier = tier;
                contract.rushFactor = rush;
                contract.carriers = carrierList;
                contract.fuelAllowance = allowance;
                break;
            }
        }

        if (!expectTag(in, "REPUTATION") || !(in >> count)) {
            lastEvent = "load failed: reputation";
            return false;
        }
        loaded.factionReputation.assign(count, 0.0);
        for (size_t i = 0; i < count; ++i) {
            if (!(in >> loaded.factionReputation[i])) {
                lastEvent = "load failed: reputation value";
                return false;
            }
        }

        if (!expectTag(in, "JOURNAL") || !(in >> count)) {
            lastEvent = "load failed: journal";
            return false;
        }
        loaded.transactions.clear();
        loaded.transactions.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            int kind = 0;
            std::string text;
            Transaction entry;
            if (!expectTag(in, "JLINE") ||
                !(in >> kind >> entry.time >> entry.starIndex >> entry.amount >> text)) {
                lastEvent = "load failed: journal line";
                return false;
            }
            entry.kind = kind >= 0 && kind <= int(JournalKind::JobFailed) ?
                JournalKind(kind) : JournalKind::Money;
            entry.text = loadToken(text);
            loaded.transactions.push_back(entry);
        }
    }

    if (!expectTag(in, "AGENTS") || !(in >> count)) {
        lastEvent = "load failed: agents";
        return false;
    }
    loaded.agents.clear();
    loaded.agents.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        std::string type;
        std::string lastAction;
        int toDest = 0;
        int playerControlled = 0;
        Agent agent("loaded", Ship("loaded", 0.0, 0.0, 0.0, 0.1, -1));
        if (!expectTag(in, "AGENT") ||
            !(in >> type >> agent.currentStar >> agent.homeStar >> agent.destStar >> toDest >>
                agent.money >> agent.cargoCost >> agent.lastProfit >> agent.trades >>
                agent.missionCooldown >> agent.targetFaction >> playerControlled >> agent.tradeBias >>
                agent.questBias >> agent.piracyBias >> agent.scoutBias >> agent.riskTolerance >> lastAction)) {
            lastEvent = "load failed: agent";
            return false;
        }
        std::string shipName;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double speed = 0.1;
        int enRoute = 0;
        if (!expectTag(in, "SHIP") ||
            !(in >> shipName >> x >> y >> z >> speed >> agent.ship.vx >> agent.ship.vy >> agent.ship.vz >>
                agent.ship.acceleration >> agent.ship.dryMass >> agent.ship.driveThrust >>
                agent.ship.driveEfficiency >> agent.ship.driveIndex >>
                agent.ship.fuelVolume >> agent.ship.propellantVolume >> agent.ship.throttle >>
                agent.ship.cruiseExhaust >> agent.ship.cruiseFraction >>
                agent.ship.cargoCapacity >> agent.ship.ownerFaction >>
                agent.ship.targetStar >> enRoute >>
                agent.ship.heavyWeapons >> agent.ship.lightWeapons >> agent.ship.armor >>
                agent.ship.utility >> agent.ship.hullHP >> agent.ship.maxHullHP >>
                agent.ship.maxModules >> agent.ship.miningRig)) {
            lastEvent = "load failed: ship";
            return false;
        }
        agent.type = loadToken(type);
        agent.toDest = toDest != 0;
        agent.playerControlled = playerControlled != 0;
        agent.lastAction = loadToken(lastAction);
        agent.ship.name = loadToken(shipName);
        agent.ship.x = x;
        agent.ship.y = y;
        agent.ship.z = z;
        agent.ship.speed = speed;
        agent.ship.enRoute = enRoute != 0;
        if (!expectTag(in, "CARGO") || !readResourceList(in, agent.ship.cargo)) {
            lastEvent = "load failed: cargo";
            return false;
        }
        if (!expectTag(in, "FUEL") || !readResourceList(in, agent.ship.fuel)) {
            lastEvent = "load failed: fuel";
            return false;
        }
        if (!expectTag(in, "PROPELLANT") || !readResourceList(in, agent.ship.propellant)) {
            lastEvent = "load failed: propellant";
            return false;
        }
        if (!expectTag(in, "MODULES") || !readIntList(in, agent.ship.modules)) {
            lastEvent = "load failed: modules";
            return false;
        }
        // (§31) Ячейка с экзотикой. В сейвах 14/15 блока нет — там и экзотики
        // не было, поэтому нули конструктора и есть правильное значение.
        if (version >= 16) {
            if (!expectTag(in, "EXOHOLD")) {
                lastEvent = "load failed: exotic hold";
                return false;
            }
            for (int k = 0; k < EX_COUNT; ++k) {
                if (!(in >> agent.ship.exotic[k])) {
                    lastEvent = "load failed: exotic hold";
                    return false;
                }
            }
            int autoTrade = 0;
            if (version >= 17) {
                if (!(in >> autoTrade >> agent.ship.containmentLevel >> agent.ship.platingLayers)) {
                    lastEvent = "load failed: refit block";
                    return false;
                }
            }
            agent.autoTrade = autoTrade != 0;
        }
        loaded.agents.push_back(agent);
    }

    if (!expectTag(in, "KNOWLEDGE") || !(in >> count)) {
        lastEvent = "load failed: knowledge";
        return false;
    }
    loaded.resizeFactionKnowledge();
    for (size_t i = 0; i < count; ++i) {
        int factionIndex = -1;
        int starIndex = -1;
        int ownerKnown = 0;
        int ownerFaction = -1;
        double ownerKnownAt = -1.0;
        int visited = 0;
        int marketKnown = 0;
        double observedAt = -1.0;
        double averageSupply = 1.0;
        double averageDemand = 1.0;
        if (!expectTag(in, "K") ||
            !(in >> factionIndex >> starIndex >> ownerKnown >> ownerFaction >> ownerKnownAt >>
                visited >> marketKnown >> observedAt >> averageSupply >> averageDemand)) {
            lastEvent = "load failed: knowledge entry";
            return false;
        }
        if (!validFaction(loaded, factionIndex) || !validStar(loaded, starIndex)) {
            lastEvent = "load failed: knowledge index";
            return false;
        }
        const size_t index = factionKnowledgeIndex(loaded, factionIndex, starIndex);
        loaded.factionKnowledge[index].ownerKnown = ownerKnown != 0;
        loaded.factionKnowledge[index].ownerFaction = ownerFaction;
        loaded.factionKnowledge[index].ownerKnownAt = ownerKnownAt;
        loaded.factionKnowledge[index].visited = visited != 0;
        loaded.factionMarketKnowledge[index].known = marketKnown != 0;
        loaded.factionMarketKnowledge[index].observedAt = observedAt;
        loaded.factionMarketKnowledge[index].averageSupplyPressure = averageSupply;
        loaded.factionMarketKnowledge[index].averageDemandPressure = averageDemand;
        if (marketKnown) {
            std::vector<double> prices;
            std::vector<double> supply;
            std::vector<double> demand;
            if (!readDoubleVector(in, "KP", prices) ||
                !readDoubleVector(in, "KS", supply) ||
                !readDoubleVector(in, "KD", demand)) {
                lastEvent = "load failed: market knowledge";
                return false;
            }
            const size_t resources = std::min(elementCount(), prices.size());
            for (size_t e = 0; e < resources; ++e) {
                loaded.factionMarketPrices[factionMarketPriceIndex(loaded, factionIndex, starIndex, int(e))] = prices[e];
                if (e < supply.size()) loaded.factionMarketSupplyPressure[factionMarketPriceIndex(loaded, factionIndex, starIndex, int(e))] = supply[e];
                if (e < demand.size()) loaded.factionMarketDemandPressure[factionMarketPriceIndex(loaded, factionIndex, starIndex, int(e))] = demand[e];
            }
        }
    }

    if (!expectTag(in, "PLAYER_KNOWLEDGE") || !(in >> count)) {
        lastEvent = "load failed: player knowledge";
        return false;
    }
    loaded.playerKnowledge.assign(starCount, PlayerStarKnowledge());
    for (size_t i = 0; i < count; ++i) {
        int starIndex = -1;
        int ownerFaction = -1;
        double knownAt = -1.0;
        int visited = 0;
        if (!expectTag(in, "PK") || !(in >> starIndex >> ownerFaction >> knownAt >> visited) ||
            !validStar(loaded, starIndex)) {
            lastEvent = "load failed: player knowledge entry";
            return false;
        }
        loaded.playerKnowledge[starIndex].ownerKnown = true;
        loaded.playerKnowledge[starIndex].ownerFaction = ownerFaction;
        loaded.playerKnowledge[starIndex].ownerKnownAt = knownAt;
        loaded.playerKnowledge[starIndex].visited = visited != 0;
    }

    if (!expectTag(in, "SIGNALS") || !(in >> count)) {
        lastEvent = "load failed: signals";
        return false;
    }
    loaded.pendingSignals.clear();
    loaded.pendingSignals.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        int type = 0;
        int contractType = 0;
        int contractCompleted = 0;
        int contractFailed = 0;
        SignalPacket signal;
        if (!expectTag(in, "SIGNAL") ||
            !(in >> type >> signal.eventId >> signal.observedTime >> signal.sendTime >> signal.arrivalTime >>
                signal.originStar >> signal.destinationStar >> signal.hopStar)) {
            lastEvent = "load failed: signal";
            return false;
        }
        if (!(in >> signal.subjectStar >> signal.sourceAgent >>
                signal.targetAgent >> signal.sourceFaction >> signal.targetFaction >> signal.recipientFaction >>
                signal.ownerFaction >> signal.contractId >> signal.amount >> signal.relationValue)) {
            lastEvent = "load failed: signal";
            return false;
        }
        if (!(in >> contractType >> signal.contractOriginStar >> signal.contractTargetStar >>
                signal.contractTargetAgent >> signal.contractResource >> signal.contractAcceptedByAgent >>
                signal.contractAmount >> signal.contractReward >> signal.contractDeposit >>
                signal.contractPostedTime >> signal.contractDeadline >> signal.contractRisk >>
                signal.contractProgress >> contractCompleted >> contractFailed)) {
            lastEvent = "load failed: signal contract";
            return false;
        }
        signal.type = signalTypeFromId(type);
        signal.contractType = contractTypeFromId(contractType);
        signal.contractCompleted = contractCompleted != 0;
        signal.contractFailed = contractFailed != 0;
        if (!readDoubleVector(in, "MP", signal.marketPrices) ||
            !readDoubleVector(in, "MS", signal.marketSupplyPressure) ||
            !readDoubleVector(in, "MD", signal.marketDemandPressure)) {
            lastEvent = "load failed: signal payload";
            return false;
        }
        if (signal.eventId >= loaded.nextSignalEventId) loaded.nextSignalEventId = signal.eventId + 1;
        if (loaded.nextSignalEventId == 0) loaded.nextSignalEventId = 1;
        loaded.pendingSignals.push_back(signal);
    }
    sortPendingSignals(loaded.pendingSignals);

    loaded.signalMemory.assign(starCount, std::vector<SignalMemoryRecord>());
    if (!expectTag(in, "SIGNAL_MEMORY") || !(in >> count)) {
        lastEvent = "load failed: signal memory";
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        int observerStar = -1;
        int type = 0;
        int absorbed = 0;
        int contractType = 0;
        int contractCompleted = 0;
        int contractFailed = 0;
        SignalMemoryRecord record;
        if (!expectTag(in, "SM") ||
            !(in >> observerStar >> type >> record.eventId >> record.observedTime >> record.subjectStar >>
                record.destinationStar >> record.sourceAgent >> record.targetAgent >>
                record.sourceFaction >> record.targetFaction >> record.recipientFaction >>
                record.ownerFaction >> record.contractId >> record.amount >> record.relationValue >>
                record.averageSupplyPressure >> record.averageDemandPressure >> absorbed >>
                contractType >> record.contractOriginStar >> record.contractTargetStar >>
                record.contractTargetAgent >> record.contractResource >> record.contractAcceptedByAgent >>
                record.contractAmount >> record.contractReward >> record.contractDeposit >>
                record.contractPostedTime >> record.contractDeadline >> record.contractRisk >>
                record.contractProgress >> contractCompleted >> contractFailed)) {
            lastEvent = "load failed: signal memory entry";
            return false;
        }
        record.type = signalTypeFromId(type);
        record.absorbed = absorbed != 0;
        record.contractType = contractTypeFromId(contractType);
        record.contractCompleted = contractCompleted != 0;
        record.contractFailed = contractFailed != 0;
        if (!readDoubleVector(in, "SMP", record.marketPrices) ||
            !readDoubleVector(in, "SMS", record.marketSupplyPressure) ||
            !readDoubleVector(in, "SMD", record.marketDemandPressure)) {
            lastEvent = "load failed: signal memory payload";
            return false;
        }
        if (record.eventId >= loaded.nextSignalEventId) loaded.nextSignalEventId = record.eventId + 1;
        if (loaded.nextSignalEventId == 0) loaded.nextSignalEventId = 1;
        if (!validStar(loaded, observerStar) ||
            !validFaction(loaded, record.recipientFaction) ||
            !validStar(loaded, record.subjectStar)) {
            lastEvent = "load failed: signal memory index";
            return false;
        }
        if (loaded.signalMemory[size_t(observerStar)].size() < size_t(SIGNAL_MEMORY_PER_STAR)) {
            loaded.signalMemory[size_t(observerStar)].push_back(record);
        }
    }

    if (!expectTag(in, "TECH") ||
        !(in >> loaded.tech.intellect >> loaded.tech.charisma >> loaded.tech.materials >>
            loaded.tech.tactics >> loaded.tech.kinematics >> loaded.tech.sensors >>
            loaded.tech.luck >> loaded.tech.cores >> loaded.tech.research)) {
        lastEvent = "load failed: tech";
        return false;
    }

    if (!expectTag(in, "MARKET_EVENTS") || !(in >> count)) {
        lastEvent = "load failed: market events";
        return false;
    }
    loaded.marketEvents.clear();
    for (size_t i = 0; i < count; ++i) {
        MarketEvent ev;
        int kind = 0;
        int announced = 0;
        if (!expectTag(in, "MEV") ||
            !(in >> ev.star >> kind >> ev.startTime >> ev.endTime >> ev.magnitude >> announced)) {
            lastEvent = "load failed: market event";
            return false;
        }
        if (!readIntList(in, ev.elements)) {
            lastEvent = "load failed: market event elements";
            return false;
        }
        ev.kind = MarketEventKind(kind);
        ev.announced = announced != 0;
        loaded.marketEvents.push_back(ev);
    }

    if (!expectTag(in, "ANOMALIES") || !(in >> count)) {
        lastEvent = "load failed: anomalies";
        return false;
    }
    loaded.anomalies.clear();
    for (size_t i = 0; i < count; ++i) {
        Anomaly a;
        int kind = 0;
        int discovered = 0;
        int resolved = 0;
        std::string nameToken;
        if (!expectTag(in, "ANOM") ||
            !(in >> a.x >> a.y >> a.z >> kind >> discovered >> resolved >> a.lootElement >>
                a.lootAmount >> a.credits >> a.chromocoreStat >> a.hazard >> a.nearStar >> nameToken)) {
            lastEvent = "load failed: anomaly";
            return false;
        }
        a.kind = AnomalyKind(kind);
        a.discovered = discovered != 0;
        a.resolved = resolved != 0;
        a.name = loadToken(nameToken);
        loaded.anomalies.push_back(a);
    }

    if (!expectTag(in, "NEWS") || !(in >> count)) {
        lastEvent = "load failed: news";
        return false;
    }
    loaded.news.clear();
    for (size_t i = 0; i < count; ++i) {
        NewsItem n;
        std::string textToken;
        if (!expectTag(in, "NEWSITEM") || !(in >> n.time >> n.kind >> textToken)) {
            lastEvent = "load failed: news item";
            return false;
        }
        n.text = loadToken(textToken);
        loaded.news.push_back(n);
    }

    int playerMiningInt = 0;
    if (!expectTag(in, "MINING") ||
        !(in >> playerMiningInt >> loaded.miningTimer >> loaded.miningStar >> loaded.miningYieldAccum)) {
        lastEvent = "load failed: mining";
        return false;
    }
    loaded.playerMining = playerMiningInt != 0;

    if (!expectTag(in, "CREDIT_FLOAT") || !(in >> count)) {
        lastEvent = "load failed: credit float";
        return false;
    }
    loaded.creditFloat.clear();
    loaded.creditFloat.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        CreditFloat c;
        if (!expectTag(in, "CF") || !(in >> c.faction >> c.amount >> c.clearsAt)) {
            lastEvent = "load failed: credit float entry";
            return false;
        }
        loaded.creditFloat.push_back(c);
    }

    // --- Довески версии 16 (§32). В сейве 14/15 их просто нет. ---
    if (version >= 16) {
        if (!expectTag(in, "LOCAL_CLAIMS") || !(in >> count >> loaded.playerShieldFrac)) {
            lastEvent = "load failed: local claims";
            return false;
        }
        loaded.localClaims.clear();
        loaded.localClaims.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            LocalClaims c;
            if (!expectTag(in, "LC") ||
                !(in >> c.starIndex >> c.radioMask >> c.bountyPaid >> c.bountyAt)) {
                lastEvent = "load failed: local claim entry";
                return false;
            }
            // Выработанность пояса: сейвы 16..20 её не знают, и там пояс
            // остаётся нетронутым — прежнее поведение.
            if (version >= 21 && !(in >> c.minedMass >> c.minedAt)) {
                lastEvent = "load failed: local claim mining";
                return false;
            }
            loaded.localClaims.push_back(c);
        }

        size_t shareCount = 0;
        if (!expectTag(in, "SHARES") || !(in >> shareCount >> loaded.factionBookCursor)) {
            lastEvent = "load failed: shares";
            return false;
        }
        loaded.resizeShareBooks();
        for (size_t f = 0; f < shareCount; ++f) {
            double book = 0.0, income = 0.0, at = -1.0e18, held = 0.0, basis = 0.0;
            if (!expectTag(in, "SH") || !(in >> book >> income >> at >> held >> basis)) {
                lastEvent = "load failed: share entry";
                return false;
            }
            if (f >= loaded.factionBook.size()) continue;   // фракций стало меньше — лишнее отбрасываем
            loaded.factionBook[f] = book;
            loaded.factionIncome[f] = income;
            loaded.factionBookAt[f] = at;
            loaded.playerShares[f] = held;
            loaded.shareCostBasis[f] = basis;
        }

        // ⚠️ Формат строки СМЕНИЛСЯ между 16 и 17: версия 16 писала сюда
        // `containmentLevel` и `hullPlating` (тогда они жили в партии, §31.4),
        // версия 17 пишет `coresForged`. Поле не дописано и не выделено, а
        // ЗАМЕНЕНО на том же теге — третий способ сломать совместимость, и
        // единственный, который правило «новое поле отдельным блоком или новой
        // версией» не покрывает. Без этой развилки сейв 16 с непустым списком
        // рынков экзотики не грузился вовсе.
        // (§47) Блок появился в версии 19. Старый сейв читается как раньше: книги
    // держав остаются по умолчанию, а игрок свою уже получил из блока LICENCE.
    if (version >= 19) {
        size_t bookCount = 0;
        if (!expectTag(in, "LICENCEBOOK") || !(in >> bookCount)) {
            lastEvent = "load failed: licence book";
            return false;
        }
        loaded.resizeFactionLicence();
        for (size_t f = 0; f < bookCount; ++f) {
            FactionLicence book;
            int revoked = 0;
            if (!expectTag(in, "LB") ||
                !(in >> book.quotaPaid >> book.quotaBase >> book.periodEnd >> book.tariffRate >>
                    book.buyback >> book.count >> revoked >> book.periodsMet)) {
                lastEvent = "load failed: licence row";
                return false;
            }
            book.revoked = revoked != 0;
            if (f < loaded.factionLicence.size()) loaded.factionLicence[f] = book;
        }
        size_t clockCount = 0, fleetClockCount = 0;
        if (!expectTag(in, "BUYCLOCK") || !(in >> clockCount >> fleetClockCount)) {
            lastEvent = "load failed: buy clock";
            return false;
        }
        loaded.factionNextSystemBuy.assign(clockCount, 0.0);
        for (size_t f = 0; f < clockCount; ++f) {
            if (!(in >> loaded.factionNextSystemBuy[f])) {
                lastEvent = "load failed: buy clock row";
                return false;
            }
        }
        loaded.factionNextFleetBuy.assign(fleetClockCount, 0.0);
        for (size_t f = 0; f < fleetClockCount; ++f) {
            if (!(in >> loaded.factionNextFleetBuy[f])) {
                lastEvent = "load failed: fleet clock row";
                return false;
            }
        }
        size_t shareCells = 0;
        if (!expectTag(in, "FACTIONSHARES") || !(in >> shareCells >> loaded.factionExpansionCursor)) {
            lastEvent = "load failed: faction shares";
            return false;
        }
        loaded.factionShares.assign(shareCells, 0.0);
        for (size_t i = 0; i < shareCells; ++i) {
            if (!(in >> loaded.factionShares[i])) {
                lastEvent = "load failed: faction share";
                return false;
            }
        }
    }

    if (!expectTag(in, "EXOTIC") || !(in >> count)) {
            lastEvent = "load failed: exotics";
            return false;
        }
        if (version >= 17) {
            if (!(in >> loaded.coresForged)) {
                lastEvent = "load failed: forged cores";
                return false;
            }
        } else {
            // Версия 16: два числа, которые с тех пор переехали на корпус.
            // Ступени восстанавливаем на ПИЛОТИРУЕМЫЙ борт — другого владельца
            // у них тогда и не было.
            int oldBay = 0, oldPlating = 0;
            if (!(in >> oldBay >> oldPlating)) {
                lastEvent = "load failed: legacy refit";
                return false;
            }
            if (loaded.playerAgent >= 0 && loaded.playerAgent < int(loaded.agents.size())) {
                loaded.agents[size_t(loaded.playerAgent)].ship.containmentLevel = oldBay;
                loaded.agents[size_t(loaded.playerAgent)].ship.platingLayers = oldPlating;
            }
        }
        loaded.exoticStocks.clear();
        loaded.exoticStocks.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            ExoticStock e;
            if (!expectTag(in, "EX") || !(in >> e.starIndex >> e.updatedAt)) {
                lastEvent = "load failed: exotic entry";
                return false;
            }
            for (int k = 0; k < EX_COUNT; ++k) {
                if (!(in >> e.stock[k])) {
                    lastEvent = "load failed: exotic stock";
                    return false;
                }
            }
            loaded.exoticStocks.push_back(e);
        }
    }

    // (§46) Прогресс, откатывавшийся каждой загрузкой. Отдельным блоком и под
    // своей версией: сейвы 14..17 его просто не содержат, и там остаются
    // значения по умолчанию — ровно то поведение, что было до правки.
    if (version >= 18) {
        int enteredLocal = 0;
        if (!expectTag(in, "PROGRESS") ||
            !(in >> enteredLocal >> loaded.tradeInsightPending >> loaded.tradeInsightTimer >>
              loaded.exoticUnitsSold)) {
            lastEvent = "load failed: progress";
            return false;
        }
        loaded.everEnteredLocal = enteredLocal != 0;
    }

    // (§50) Маяки и спасатели. Сейвы 14..19 блока не содержат — там беда просто
    // не имела возраста, и после загрузки застрявший ждал заново.
    if (version >= 20) {
        size_t beaconCount = 0;
        if (!expectTag(in, "DISTRESS") || !(in >> beaconCount >> loaded.distressTimer)) {
            lastEvent = "load failed: distress";
            return false;
        }
        loaded.distress.clear();
        loaded.distress.reserve(beaconCount);
        for (size_t b = 0; b < beaconCount; ++b) {
            DistressBeacon beacon;
            int stateSent = 0, looted = 0;
            if (!expectTag(in, "DB") ||
                !(in >> beacon.agent >> beacon.x >> beacon.y >> beacon.z >>
                    beacon.vx >> beacon.vy >> beacon.vz >>
                    beacon.raisedTime >> beacon.responder >> stateSent >> looted)) {
                lastEvent = "load failed: distress beacon";
                return false;
            }
            beacon.stateSent = stateSent != 0;
            beacon.looted = looted != 0;
            loaded.distress.push_back(beacon);
        }
        size_t rescuerCount = 0;
        if (!expectTag(in, "RESCUERS") || !(in >> rescuerCount)) {
            lastEvent = "load failed: rescuers";
            return false;
        }
        for (size_t r = 0; r < rescuerCount; ++r) {
            size_t index = 0;
            int rescueTarget = -1, knows = 0, resumeStar = -1;
            double driftYears = 0.0;
            if (!expectTag(in, "RS") ||
                !(in >> index >> rescueTarget >> knows >> resumeStar >> driftYears)) {
                lastEvent = "load failed: rescuer row";
                return false;
            }
            if (index >= loaded.agents.size()) continue;
            loaded.agents[index].rescueTarget = rescueTarget;
            loaded.agents[index].rescueKnows = knows != 0;
            loaded.agents[index].rescueResumeStar = resumeStar;
            loaded.agents[index].driftYears = driftYears;
        }
    }

    // (§52) Часы рынков. Сейвы 14..20 их не содержат — там ниже остаётся старый
    // сброс фазы, то есть ровно то поведение, с которым эти сейвы писались.
    bool clockLoaded = false;
    if (version >= 21) {
        size_t clockCount = 0;
        if (!expectTag(in, "MARKETCLOCK") ||
            !(in >> clockCount >> loaded.marketUpdateCursor >> loaded.marketUpdateBudget >>
              loaded.marketEventTimer >> loaded.anomalyTimer)) {
            lastEvent = "load failed: market clock";
            return false;
        }
        loaded.marketUpdatedAt.assign(clockCount, 0.0);
        for (size_t i = 0; i < clockCount; ++i) {
            if (!(in >> loaded.marketUpdatedAt[i])) {
                lastEvent = "load failed: market clock row";
                return false;
            }
        }
        clockLoaded = true;

        size_t leanCount = 0;
        if (!expectTag(in, "LEAN") || !(in >> leanCount)) {
            lastEvent = "load failed: research lean";
            return false;
        }
        loaded.researchLean.assign(size_t(TECH_STAT_COUNT), 0.0);
        for (size_t k = 0; k < leanCount; ++k) {
            double v = 0.0;
            if (!(in >> v)) { lastEvent = "load failed: research lean row"; return false; }
            if (k < loaded.researchLean.size()) loaded.researchLean[k] = v;
        }

        size_t stateCount = 0;
        if (!expectTag(in, "MARKETSTATE") || !(in >> stateCount)) {
            lastEvent = "load failed: market state";
            return false;
        }
        for (size_t i = 0; i < stateCount; ++i) {
            double strain = 0.0, serviceCostAvg = 0.0, seededScale = 0.0;
            if (!(in >> strain >> serviceCostAvg >> seededScale)) {
                lastEvent = "load failed: market state row";
                return false;
            }
            if (i < loaded.markets.size()) {
                loaded.markets[i].strain = strain;
                loaded.markets[i].serviceCostAvg = serviceCostAvg;
                loaded.markets[i].restoreScale(seededScale);
            }
        }
    }

    if (!in) {
        lastEvent = "load failed";
        return false;
    }

    *this = loaded;
    rng = loadedRng;
    if (signalMemory.size() != cluster.stars.size()) {
        signalMemory.assign(cluster.stars.size(), std::vector<SignalMemoryRecord>());
    }
    // Сейв 21 приносит фазу обхода рынков с собой; старым сейвам её взять
    // неоткуда, и им остаётся прежний сброс — «всем пора обновиться».
    if (!clockLoaded || marketUpdatedAt.size() != markets.size()) {
        marketUpdatedAt.assign(markets.size(), time - MARKET_UPDATE_INTERVAL_YEARS);
        marketUpdateCursor = 0;
        marketUpdateBudget = 0.0;
    }
    // Денежный уровень — часть партии, а не процесса: восстанавливаем его в
    // ценовом слое сразу, не дожидаясь первого такта рынков.
    marketSetClusterLevel(clusterPriceLevel);
    // Ёмкость ячейки — ОТРАЖЕНИЕ ступени в поле корабля, а не второе
    // независимое число (§31.4), поэтому в сейв она не пишется и ставится
    // здесь — у КАЖДОГО борта, а не только у пилотируемого: экзотику может
    // везти любой из них.
    // ⚠️ Полный `rebakePlayerBakedBonuses()` тут звать НЕЛЬЗЯ: броня, корпус и
    // масса от слоёв нейтрониума уже лежат в сохранённых полях корабля, и
    // повторное наложение удвоило бы их при каждой загрузке. Перезапекание
    // нужно только там, где `shipApplyClass` эти поля ОБНУЛИЛ.
    for (size_t i = 0; i < agents.size(); ++i) {
        Ship& sh = agents[i].ship;
        sh.containment = double(sh.containmentLevel) * CONTAINMENT_STEP_UNITS;
    }
    rebuildRouteCache();
    lastEvent = "loaded " + path;
    return true;
}

void Game::rebuildRouteCache() {
    const int count = int(cluster.stars.size());
    routeNextHop.clear();
    routeCacheBuiltAt = time;
    if (count <= 0 || count >= int(ROUTE_NO_HOP)) return;

    std::vector<std::vector<RouteEdge> > graph;
    graph.resize(size_t(count));
    for (int i = 0; i < count; ++i) {
        std::vector<int> bestStar(ROUTE_NEIGHBORS, -1);
        std::vector<double> bestDistance2(ROUTE_NEIGHBORS, std::numeric_limits<double>::max());
        for (int j = 0; j < count; ++j) {
            if (i == j) continue;
            const double distance2 = distanceSquaredStarToStar(cluster.stars[i], cluster.stars[j]);
            int worst = 0;
            for (int k = 1; k < ROUTE_NEIGHBORS; ++k) {
                if (bestDistance2[k] > bestDistance2[worst]) worst = k;
            }
            if (distance2 < bestDistance2[worst]) {
                bestDistance2[worst] = distance2;
                bestStar[worst] = j;
            }
        }
        for (int k = 0; k < ROUTE_NEIGHBORS; ++k) {
            if (bestStar[k] >= 0) routeAddEdge(graph, i, bestStar[k], std::sqrt(bestDistance2[k]));
        }
    }

    // Ensure symmetry to guarantee bidirectional reachability
    for (int i = 0; i < count; ++i) {
        for (size_t e = 0; e < graph[i].size(); ++e) {
            const RouteEdge& edge = graph[i][e];
            bool found = false;
            for (size_t rev = 0; rev < graph[edge.star].size(); ++rev) {
                if (graph[edge.star][rev].star == i) { found = true; break; }
            }
            if (!found) {
                routeAddEdge(graph, edge.star, i, edge.distance);
            }
        }
    }

    const size_t countSize = size_t(count);
    routeNextHop.assign(countSize * countSize, ROUTE_NO_HOP);
    
    std::atomic<int> nextTarget(0);
    int numThreads = std::thread::hardware_concurrency();
    if (numThreads <= 0) numThreads = 4;
    
    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([this, count, countSize, &nextTarget, &graph]() {
            std::vector<double> dist(count);
            std::vector<int> nextHop(count);
            using PD = std::pair<double, int>;
            std::vector<PD> pq;
            pq.reserve(count * 8);

            while (true) {
                int target = nextTarget.fetch_add(1);
                if (target >= count) break;
                
                std::fill(dist.begin(), dist.end(), std::numeric_limits<double>::max());
                std::fill(nextHop.begin(), nextHop.end(), target);
                
                pq.clear();
                dist[target] = 0.0;
                pq.push_back({0.0, target});
                
                while (!pq.empty()) {
                    std::pop_heap(pq.begin(), pq.end(), std::greater<PD>());
                    PD top = pq.back();
                    pq.pop_back();
                    
                    double d = top.first;
                    int u = top.second;
                    
                    if (d > dist[u]) continue;
                    
                    for (size_t e = 0; e < graph[u].size(); ++e) {
                        const RouteEdge& edge = graph[u][e];
                        int v = edge.star;
                        double newDist = d + edge.distance;
                        if (newDist < dist[v]) {
                            dist[v] = newDist;
                            nextHop[v] = u;
                            pq.push_back({newDist, v});
                            std::push_heap(pq.begin(), pq.end(), std::greater<PD>());
                        }
                    }
                }
                
                for (int source = 0; source < count; ++source) {
                    const size_t index = size_t(source) * countSize + size_t(target);
                    if (source == target) {
                        routeNextHop[index] = static_cast<unsigned short>(source);
                    } else {
                        routeNextHop[index] = static_cast<unsigned short>(nextHop[source]);
                    }
                }
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
}

int Game::routeNextStar(int originStar, int targetStar) const {
    if (!validStar(*this, originStar) || !validStar(*this, targetStar)) return targetStar;
    if (originStar == targetStar) return targetStar;
    const size_t count = cluster.stars.size();
    const size_t index = size_t(originStar) * count + size_t(targetStar);
    if (count == 0 || routeNextHop.size() != count * count || index >= routeNextHop.size()) return targetStar;
    const unsigned short next = routeNextHop[index];
    if (next == ROUTE_NO_HOP || int(next) < 0 || int(next) >= int(count)) return targetStar;
    return int(next);
}

namespace {

// Сколько внятных торговых маршрутов есть у системы в радиусе `radiusLy`.
// Маршрут считается внятным, если сосед платит заметно больше, чем берут здесь,
// И у обоих рынков есть глубина — иначе «спред» невывозим из-за проскальзывания.
int starterRouteScore(const Game& game, int starIndex, double radiusLy) {
    if (!validStar(game, starIndex) || starIndex >= int(game.markets.size())) return 0;
    const ClusterStar& home = game.cluster.stars[starIndex];
    const Market& hm = game.markets[starIndex];
    const double MIN_RATIO = 2.0;      // сосед платит хотя бы вдвое
    const double MIN_DEPTH = 4.0;      // рынок съедает осмысленный объём
    int routes = 0;
    for (int i = 0; i < int(game.cluster.stars.size()) && i < int(game.markets.size()); ++i) {
        if (i == starIndex) continue;
        if (distanceBetween(home, game.cluster.stars[i]) > radiusLy) continue;
        const Market& tm = game.markets[i];
        for (int e = 0; e < int(hm.prices.size()) && e < int(tm.prices.size()); ++e) {
            const double buy = hm.prices[e];
            if (buy <= 0.001 || hm.supply[e].amount < 5.0) continue;
            if (tm.prices[e] < buy * MIN_RATIO) continue;
            if (hm.depthOf(e) < MIN_DEPTH || tm.depthOf(e) < MIN_DEPTH) continue;
            ++routes;
        }
    }
    return routes;
}

// Лучшая по числу маршрутов система среди владений стартовой фракции.
int pickStarterSystem(const Game& game) {
    if (game.factions.empty() || game.factions[0].controlledStars.empty()) return 0;
    const double RADIUS_LY = 8.0;      // примерно два прыжка стартового корабля
    int best = game.factions[0].homeStar;
    int bestScore = starterRouteScore(game, best, RADIUS_LY);
    for (int candidate : game.factions[0].controlledStars) {
        const int score = starterRouteScore(game, candidate, RADIUS_LY);
        if (score > bestScore) { bestScore = score; best = candidate; }
    }
    return validStar(game, best) ? best : 0;
}

} // namespace

void Game::init(size_t num_stars) {
    time = 0.0;
    // Засев ИЗ seed, а не запись seed ИЗ ГСЧ (было наоборот — поле ни на что не
    // влияло). Один seed ⇒ один и тот же мир: баг игрока воспроизводим, а
    // регресс-проверки (soak/shots) наконец сравнимы между прогонами.
    rng.seed(seed);
    // Тот же довод, что и строкой выше, но для ВТОРОГО мира в одном процессе.
    // `marketClusterLevel` — кэш в market.cpp; авторитетное значение живёт в
    // `clusterPriceLevel` и проталкивается вниз, но ВПЕРВЫЕ это происходит уже
    // после засева рынков (см. measureClusterPriceLevel ниже). До того рынки
    // релаксируют цены под уровень ПРЕДЫДУЩЕЙ партии, и один и тот же seed даёт
    // разную экономику: замер на трёх подряд мирах seed 42 — уровень до init
    // 1.0000 / 0.2652 / 0.4641, добыча против потребления молибдена 5.8 / 13.1 /
    // 7.2, цена сбыта 90.98 / 71.79 / 82.38. Первый мир в процессе всегда был
    // прав, поэтому игра этого не показывала (одна партия — один процесс), зато
    // балансовый стенд строит миры десятками и мерил их по остаткам соседа.
    marketSetClusterLevel(1.0);
    cluster.generate(num_stars, seed);
    markets.clear();
    markets.resize(num_stars);
    factions.clear();
    colonies.clear();
    contracts.clear();
    agents.clear();
    factionKnowledge.clear();
    factionMarketKnowledge.clear();
    factionMarketPrices.clear();
    factionMarketSupplyPressure.clear();
    factionMarketDemandPressure.clear();
    factionRelations.clear();
    playerKnowledge.clear();
    pendingSignals.clear();
    // (§50) Маяки — тоже состояние мира: без сброса беда одного мира зазвучала
    // бы в следующем, и харнес мерил бы чужое спасение.
    distress.clear();
    distressTimer = 0.0;
    // (§52) Чем зарабатывались исследования — тоже состояние мира: без сброса
    // занятия прошлой партии решали бы, каким выйдет ядро в следующей.
    researchLean.assign(size_t(TECH_STAT_COUNT), 0.0);
    signalMemory.clear();
    routeNextHop.clear();
    marketUpdatedAt.clear();
    routeCacheBuiltAt = -1.0;
    marketUpdateCursor = 0;
    marketUpdateBudget = 0.0;
    playerKnowledge.resize(num_stars);
    signalMemory.resize(num_stars);
    nextContractId = 1;
    nextSignalEventId = 1;
    contractUpdateTimer = 0.0;
    factionUpdateTimer = 0.0;
    playerAgent = -1;
    playerFaction = -1;
    clearingFaction = -1;
    boughtSystems = 0;
    capturedSystems = 0;
    tech = TechState();
    marketEvents.clear();
    anomalies.clear();
    news.clear();
    marketEventTimer = 0.0;
    anomalyTimer = 0.0;
    playerMining = false;
    miningTimer = 0.0;
    miningStar = -1;
    miningYieldAccum = 0.0;
    // ⚠️ Всё, что копится в ПАРТИИ, обязано сбрасываться здесь. Это та же
    // грабля, что уже ловили с денежным уровнем скопления (см. комментарий у
    // `marketSetClusterLevel` выше): `init` зовётся не только на пустом
    // объекте — оболочка вызывает его на том же `Game` после неудачной
    // загрузки, а балансовый стенд строит миры десятками подряд. Без сброса
    // новая партия наследовала бы чужую репутацию, замороженную лицензию,
    // чужие деньги «в пути» и чужой журнал.
    // ⚠️ Книга ОЧИЩАЕТСЯ, а не переписывается по полям: фракций на этот момент
    // ещё нет, и присваивание ушло бы в запасную строку, а настоящие строки
    // достались бы новому миру от предыдущего. Ровно тот класс, на котором
    // проект уже обжигался (уровень цен, наследованный между мирами).
    factionLicence.clear();
    clusterPriceLevel = 1.0;
    clusterPriceBase = 1.0;
    creditFloat.clear();
    factionReputation.clear();
    transactions.clear();
    lastPlayerMoney = -1.0;
    journalExplained = 0.0;
    everEnteredLocal = false;
    localClaims.clear();
    playerShieldFrac = 1.0;
    exoticStocks.clear();
    coresForged = 0;
    exoticUnitsSold = 0.0;
    tradeInsightPending = 0.0;
    tradeInsightTimer = 0.0;
    factionBook.clear();
    factionIncome.clear();
    factionBookAt.clear();
    playerShares.clear();
    shareCostBasis.clear();
    factionShares.clear();
    factionNextSystemBuy.clear();
    factionNextFleetBuy.clear();
    factionBookCursor = 0;
    // ⚠️ Курсор — тоже состояние мира: не сброшенный, он делает второй мир в
    // процессе не таким, как первый при том же seed (см. §47 и историю с
    // уровнем цен, наследованным между партиями).
    factionExpansionCursor = 0;
    lastEvent = "cluster seeded";
    if (num_stars == 0) return;

    for (size_t i = 0; i < num_stars; ++i) {
        const ClusterStar& star = cluster.stars[i];
        markets[i].seed(star.resources, star.demandBias, star.economyRole, star.population, star.industry);
    }
    marketUpdatedAt.assign(num_stars, time - MARKET_UPDATE_INTERVAL_YEARS);
    rebuildRouteCache();

    // --- Державы скопления (§47) ---------------------------------------------
    // Пятнадцать держав плюс игрок = шестнадцать «игроков», из которых пятнадцать
    // ведёт ИИ. Цвета разведены по кругу тона и разведены с уже занятыми: бледная
    // палата (210,214,236), жёлтый игрок (255,232,120) и кабинные cue микромира
    // (золото розыска 255,205,60, SOS 238,88,82, зелень эскорта 90,220,130,
    // синь подмоги 120,180,255). Агрессия рассыпана по всей шкале — иначе
    // пятнадцать держав ведут себя как одна.
    const FactionSeed seeds[] = {
        {"Aster Compact", 230, 76, 82, 0.68},
        {"Helion League", 244, 178, 70, 0.42},
        {"Cobalt Mandate", 80, 156, 255, 0.58},
        {"Green Arcology", 95, 210, 128, 0.34},
        {"Violet Synod", 190, 112, 240, 0.72},
        {"White Foundry", 220, 226, 214, 0.50},
        {"Teal Concord", 56, 204, 192, 0.30},
        {"Ember Dominion", 238, 104, 40, 0.80},
        {"Indigo Chorus", 112, 102, 236, 0.46},
        {"Rose Directorate", 242, 118, 170, 0.62},
        {"Lime Enclave", 178, 216, 72, 0.26},
        {"Slate Hegemony", 126, 148, 172, 0.66},
        {"Bronze Guild", 182, 126, 66, 0.38},
        {"Mint Covenant", 132, 232, 176, 0.54},
        {"Garnet Accord", 158, 44, 72, 0.76}
    };
    // ⚠️ Делитель 80, а не 180: при 180 полный состав требовал 2 700 звёзд, и
    // балансовые харнесы (миры по 1 200) катались на шести державах — то есть
    // проверяли НЕ ту конфигурацию, в которую играют. Потолок держав — размер
    // `seeds`, а не отдельное число, которое можно забыть обновить.
    const int factionSeedCount = int(sizeof(seeds) / sizeof(seeds[0]));
    const int factionCount = std::min<int>(factionSeedCount, std::max<int>(2, int(num_stars / 80)));
    std::vector<int> homes;
    homes.reserve(factionCount);
    for (int i = 0; i < factionCount; ++i) {
        const int home = pickFactionHome(cluster, homes);
        homes.push_back(home);
        factions.emplace_back(seeds[i].name, seeds[i].r, seeds[i].g, seeds[i].b);
        Faction& faction = factions.back();
        faction.homeStar = home;
        faction.treasury = 1800.0 + cluster.stars[home].industry * 520.0;
        faction.estimatedTreasury = faction.treasury;
        faction.militaryBudget = faction.treasury * (0.22 + seeds[i].aggression * 0.18);
        faction.tradeBudget = faction.treasury * (0.32 + (1.0 - seeds[i].aggression) * 0.16);
        faction.colonyBudget = faction.treasury * (0.20 + cluster.stars[home].habitability * 0.08);
        faction.strength = 0.8 + cluster.stars[home].industry * 0.18;
        faction.aggression = seeds[i].aggression;
        faction.riskTolerance = 0.28 + seeds[i].aggression * 0.55;
        faction.tradeBias = 0.65 - seeds[i].aggression * 0.18;
        faction.expansionBias = 0.36 + cluster.stars[home].habitability * 0.32;
        faction.defenseBias = 0.38 + cluster.stars[home].defense * 0.025;
        // ⚠️ СТОЛИЦА ЗАНИМАЕТСЯ СРАЗУ, а не внутри `claimInitialHoldings`.
        // Иначе соседей разбирают раньше, чем державы сели по домам: первая
        // держава берёт три ближайшие «ничейные» звезды, среди которых лежит
        // ЧУЖАЯ БУДУЩАЯ СТОЛИЦА, а потом `setStarOwner` молча отбирает её
        // обратно (§47). При шести державах дома стояли редко и это почти не
        // случалось; при пятнадцати Aster Compact оставался с ОДНОЙ звездой,
        // стартовая система выбиралась из неё одной и попадала в пустоту —
        // ближайший сосед 17.5 ly вместо 8, ровно тот отказ, от которого уже
        // защищались при посадке клиринговой палаты.
        setStarOwner(*this, home, int(factions.size()) - 1);
    }
    for (size_t i = 0; i < factions.size(); ++i) {
        claimInitialHoldings(*this, int(i));
    }

    // --- Клиринговая палата скопления -------------------------------------
    // Межзвёздный банк и биржа: держит богатые системы в ядре, собирает все
    // лицензионные деньги и тратит их как государство. Дом выбирается по той же
    // логике, что и у держав, но со смещением к ЦЕНТРУ скопления: банк сидит
    // там, куда свет доходит быстрее всего до любого края, — иначе сверка
    // счетов (§16) шла бы у него дольше, чем у всех клиентов.
    {
        clearingFaction = int(factions.size());
        int bankHome = -1;
        double bestScore = -1e18;
        for (size_t i = 0; i < cluster.stars.size(); ++i) {
            // ⚠️ Только НИЧЕЙНАЯ звезда. Без этой проверки банк садился на уже
            // занятую систему, а `setStarOwner` молча отбирал её у державы —
            // Aster Compact оставался с одной звездой, стартовая система
            // выбиралась из неё одной и оказывалась в пустоте (ближайший сосед
            // 17.5 ly вместо 8), после чего половина балансовых проверок не
            // находила куда лететь.
            if (cluster.stars[i].ownerFaction >= 0) continue;
            if (std::find(homes.begin(), homes.end(), int(i)) != homes.end()) continue;
            const ClusterStar& star = cluster.stars[i];
            const double dx = star.x - cluster.centreX;
            const double dy = star.y - cluster.centreY;
            const double dz = star.z - cluster.centreZ;
            const double fromCentre = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double score = starPopulationWeight(star) * 3.0 + star.industry * 4.0 - fromCentre * 0.25;
            if (score > bestScore) { bestScore = score; bankHome = int(i); }
        }
        factions.emplace_back("Clearing House", 210, 214, 236);
        Faction& bank = factions.back();
        bank.homeStar = bankHome;
        bank.treasury = 0.0;
        bank.estimatedTreasury = 0.0;
        // Банк не воюет и не расширяется силой: он кредитует.
        bank.strength = 1.2;
        bank.aggression = 0.05;
        bank.riskTolerance = 0.20;
        bank.tradeBias = 1.0;
        bank.expansionBias = 0.05;
        bank.defenseBias = 0.60;
        if (validStar(*this, bankHome)) claimInitialHoldings(*this, clearingFaction);
    }

    const int npcFactionCount = int(factions.size());
    playerFaction = int(factions.size());
    factions.emplace_back("Player Freehold", 255, 232, 120);
    factions.back().treasury = 0.0;
    factions.back().estimatedTreasury = 0.0;
    factions.back().militaryBudget = 0.0;
    factions.back().tradeBudget = 0.0;
    factions.back().colonyBudget = 0.0;
    factions.back().strength = 0.65;
    factions.back().aggression = 0.35;
    factions.back().riskTolerance = 0.45;
    factions.back().tradeBias = 0.75;
    factions.back().expansionBias = 0.30;
    factions.back().defenseBias = 0.30;
    resizeFactionRelations();
    resizeFactionReputation();
    resizeFactionLicence();
    for (size_t a = 0; a < factions.size(); ++a) {
        for (size_t b = 0; b < factions.size(); ++b) {
            if (a == b) {
                setFactionRelation(int(a), int(b), 128);
                continue;
            }
            int relation = 0;
            if (int(a) < npcFactionCount && int(b) < npcFactionCount &&
                validStar(*this, factions[a].homeStar) && validStar(*this, factions[b].homeStar)) {
                const double distance = distanceBetween(cluster.stars[factions[a].homeStar], cluster.stars[factions[b].homeStar]);
                relation = int(std::max(-42.0, std::min(26.0, 18.0 - distance * 0.85 - (factions[a].aggression + factions[b].aggression) * 10.0)));
            }
            setFactionRelation(int(a), int(b), relation);
        }
    }

    // Стартовая система выбирается не «первая попавшаяся столица фракции», а та из
    // владений игрока, у которой рядом РЕАЛЬНО есть что возить. Замер показал: на
    // 2 сидах из 5 у стартовой системы не было ни одного прибыльного маршрута в
    // радиусе двух прыжков — первые пять минут превращались в лотерею. Здесь мы
    // не подкручиваем цены, а лишь выбираем точку входа в уже сгенерированный мир.
    const int playerStart = pickStarterSystem(*this);
    // `homeStar` фракции игрока НАМЕРЕННО остаётся -1: посев знаний (ниже) её
    // пропускает, и игрок начинает, зная ровно одну систему — свою. Биржевая
    // сводка (§10.9) поначалу пуста и наполняется ТОЛЬКО тем, что игрок
    // облетел сам. В этом весь смысл: карта цен — не выданная таблица, а то,
    // что ты добыл ногами. Была попытка выдать стартовую разведку ради
    // непустой сводки — она убивала главную петлю мотивации и откачена.
    const ClusterStar& playerHome = cluster.stars[playerStart];
    // Имя = класс из shipClasses(): иначе buyShip не находит текущий корпус и не
    // засчитывает его стоимость (см. комментарий у класса "Hauler" в ship.cpp).
    Ship playerShip("Hauler", playerHome.x, playerHome.y, playerHome.z, 0.12, playerFaction);
    playerShip.acceleration = 0.22;
    // Стартовый корпус берётся ИЗ ТАБЛИЦЫ целиком, включая потолок скорости:
    // так лестница корпусов начинается ровно там, где написано в классе, и
    // покупка следующего корабля даёт реальную прибавку хода.
    for (size_t c = 0; c < shipClasses().size(); ++c) {
        if (shipClasses()[c].name != "Hauler") continue;
        shipApplyClass(playerShip, shipClasses()[c]);
        break;
    }
    Agent player("player", playerShip);
    player.playerControlled = true;
    player.currentStar = playerStart;
    player.homeStar = playerStart;
    player.destStar = playerStart;
    // Стартовый капитал. Решение пользователя: игрок начинает нищим (100 Cr),
    // а не с оборотным капиталом на первый рейс — первая цель игры превращается
    // из «куда везти» в «на что вообще купить груз».
    player.money = 100.0;
    player.lastAction = "ready";
    playerAgent = int(agents.size());
    agents.push_back(player);
    registerFactionAgent(*this, playerAgent);

    seedAnomalies();
    pushNews("Welcome, Captain. Trade, mine, and grow your fleet.", 0);

    resizeFactionKnowledge();
    for (size_t factionIndex = 0; factionIndex < factions.size(); ++factionIndex) {
        if (validStar(*this, factions[factionIndex].homeStar)) {
            seedFactionKnowledge(int(factionIndex), factions[factionIndex].homeStar, 10.0);
        }
        for (int starIndex : factions[factionIndex].controlledStars) {
            observeStarForFaction(int(factionIndex), starIndex);
            observeMarketForFaction(int(factionIndex), starIndex);
        }
    }
    observeStar(playerStart);
    for (int i = 0; i < 4; ++i) {
        tryCreateDeliveryContract(*this, playerStart);
    }

    // --- Население скопления -------------------------------------------------
    // Раньше на 10 000 звёзд приходилось ~134 борта — один корабль на 75 систем.
    // Игрок мог сделать десяток прыжков и не встретить никого: мир читался
    // пустым, а вся живая машинерия (конвои, патрули, рейды, розыск) почти
    // никогда не попадалась ему на глаза. Считаем целевое население ОТ РАЗМЕРА
    // мира и раскладываем по ролям, а не набираем случайными константами.
    const int targetAgents = std::max<int>(48,
        std::min<int>(AGENT_TARGET_FULL, int(double(AGENT_TARGET_FULL) * double(num_stars) / double(STAR_COUNT))));
    const int traderCount     = std::max<int>(12, int(targetAgents * 0.55));
    const int adventurerCount = std::max<int>(8,  int(targetAgents * 0.15));
    // На фракцию: закон численно превосходит преступность (патрулей вдвое больше
    // пиратов) — иначе фронтир становится непроходимым, а не опасным.
    const int perFactionPatrol   = npcFactionCount > 0 ? std::max(1, int(targetAgents * 0.12) / npcFactionCount) : 0;
    const int perFactionColonist = npcFactionCount > 0 ? std::max(1, int(targetAgents * 0.08) / npcFactionCount) : 0;
    const int perFactionScout    = npcFactionCount > 0 ? std::max(1, int(targetAgents * 0.05) / npcFactionCount) : 0;
    const int perFactionPirate   = npcFactionCount > 0 ? std::max(1, int(targetAgents * 0.05) / npcFactionCount) : 0;

    for (int i = 0; i < traderCount; ++i) {
        const int owner = npcFactionCount > 0 ? i % npcFactionCount : -1;
        int start = (i * 37) % int(num_stars);
        if (validFaction(*this, owner) && !factions[owner].controlledStars.empty()) {
            start = factions[owner].controlledStars[i % int(factions[owner].controlledStars.size())];
        }
        const ClusterStar& star = cluster.stars[start];
        const double maxSpeed = 0.11 + 0.16 * double((i * 17) % 100) / 99.0;
        Ship ship("Trader_" + std::to_string(i + 1), star.x, star.y, star.z, maxSpeed, owner);
        ship.cargoCapacity = 45.0 + double((i * 13) % 90);
        ship.acceleration = 0.12 + 0.16 * double((i * 11) % 100) / 99.0;
        shipAutofit(ship);

        Agent agent("trader", ship);
        agent.currentStar = start;
        agent.homeStar = start;
        agent.destStar = start;
        agent.money = 2500.0 + double(i) * 220.0;
        agent.tradeBias = validFaction(*this, owner) ? factions[owner].tradeBias : 1.0;
        agent.questBias = 0.24 + 0.28 * double((i * 7) % 100) / 99.0;
        agent.riskTolerance = validFaction(*this, owner) ? factions[owner].riskTolerance : 0.45;
        agent.lastAction = "idle";
        agents.push_back(agent);
        registerFactionAgent(*this, int(agents.size()) - 1);
    }

    for (int f = 0; f < npcFactionCount; ++f) {
        // ⚠️ Без владений индексировать `controlledStars` нечем. Держава без
        // звёзд возможна (палате может не достаться свободного дома), и все
        // четыре цикла ниже читали бы `[i % 0]`. Раньше это держалось на том,
        // что фракций было шесть и звёзд всем хватало.
        if (factions[f].controlledStars.empty()) continue;
        for (int i = 0; i < perFactionPatrol; ++i) {
            const int start = factions[f].controlledStars[i % int(factions[f].controlledStars.size())];
            const ClusterStar& star = cluster.stars[start];
            Ship ship(factions[f].name + "_Patrol_" + std::to_string(i + 1), star.x, star.y, star.z, 0.13 + 0.03 * i, f);
            ship.cargoCapacity = 70.0 + 18.0 * i;
            ship.acceleration = 0.18 + 0.03 * i;
            shipAutofit(ship);
            Agent agent("military", ship);
            agent.currentStar = start;
            agent.homeStar = factions[f].homeStar;
            agent.destStar = start;
            agent.money = 900.0;
            agent.riskTolerance = factions[f].riskTolerance;
            agent.lastAction = "ready";
            agents.push_back(agent);
            registerFactionAgent(*this, int(agents.size()) - 1);
        }

        for (int i = 0; i < perFactionColonist; ++i) {
            const int start = factions[f].controlledStars[(i + 1) % int(factions[f].controlledStars.size())];
            const ClusterStar& star = cluster.stars[start];
            Ship ship(factions[f].name + "_Charter_" + std::to_string(i + 1), star.x, star.y, star.z, 0.11 + 0.015 * i, f);
            ship.cargoCapacity = 240.0 + 80.0 * i;
            ship.acceleration = 0.11 + 0.02 * i;
            shipAutofit(ship);
            Agent agent("colonist", ship);
            agent.currentStar = start;
            agent.homeStar = factions[f].homeStar;
            agent.destStar = start;
            agent.money = 1400.0;
            agent.questBias = 0.18;
            agent.riskTolerance = factions[f].riskTolerance * 0.75;
            agent.lastAction = "ready";
            agents.push_back(agent);
            registerFactionAgent(*this, int(agents.size()) - 1);
        }

        for (int i = 0; i < perFactionScout; ++i) {
            const int scoutStart = factions[f].controlledStars[i % int(factions[f].controlledStars.size())];
            const ClusterStar& scoutStar = cluster.stars[scoutStart];
            Ship scoutShip(factions[f].name + "_Scout_" + std::to_string(i + 1), scoutStar.x, scoutStar.y, scoutStar.z, 0.22, f);
            scoutShip.cargoCapacity = 35.0;
            scoutShip.acceleration = 0.24;
            shipAutofit(scoutShip);
            Agent scout("scout", scoutShip);
            scout.currentStar = scoutStart;
            scout.homeStar = factions[f].homeStar;
            scout.destStar = scoutStart;
            scout.money = 700.0;
            scout.scoutBias = 1.0;
            scout.riskTolerance = 0.62 + factions[f].riskTolerance * 0.25;
            scout.lastAction = "ready";
            agents.push_back(scout);
            registerFactionAgent(*this, int(agents.size()) - 1);
        }

        for (int i = 0; i < perFactionPirate; ++i) {
            const int pirateStart = factions[f].controlledStars[(f + 2 + i) % int(factions[f].controlledStars.size())];
            const ClusterStar& pirateStar = cluster.stars[pirateStart];
            Ship pirateShip(factions[f].name + "_Raider_" + std::to_string(i + 1), pirateStar.x, pirateStar.y, pirateStar.z, 0.18, f);
            pirateShip.cargoCapacity = 80.0;
            pirateShip.acceleration = 0.20;
            shipAutofit(pirateShip);
            Agent pirate("pirate", pirateShip);
            pirate.currentStar = pirateStart;
            pirate.homeStar = factions[f].homeStar;
            pirate.destStar = pirateStart;
            pirate.money = 650.0;
            pirate.piracyBias = 0.85;
            pirate.riskTolerance = 0.72 + factions[f].aggression * 0.24;
            pirate.lastAction = "waiting";
            agents.push_back(pirate);
            registerFactionAgent(*this, int(agents.size()) - 1);
        }
    }

    for (int i = 0; i < adventurerCount; ++i) {
        const int owner = npcFactionCount > 0 ? (i * 5 + 1) % npcFactionCount : -1;
        const int start = validFaction(*this, owner) && !factions[owner].controlledStars.empty() ?
            factions[owner].controlledStars[(i + 2) % int(factions[owner].controlledStars.size())] :
            randomer(rng, int(num_stars) - 1);
        const ClusterStar& star = cluster.stars[start];
        Ship ship("Adventurer_" + std::to_string(i + 1), star.x, star.y, star.z, 0.12 + 0.10 * double((i * 19) % 100) / 99.0, owner);
        ship.cargoCapacity = 38.0 + double((i * 29) % 70);
        ship.acceleration = 0.15 + 0.12 * double((i * 23) % 100) / 99.0;
        shipAutofit(ship);
        Agent agent("adventurer", ship);
        agent.currentStar = start;
        agent.homeStar = start;
        agent.destStar = start;
        agent.money = 1600.0 + double(i) * 115.0;
        agent.tradeBias = 0.55 + double((i * 3) % 40) / 100.0;
        agent.questBias = 0.45 + double((i * 7) % 45) / 100.0;
        agent.riskTolerance = 0.35 + double((i * 11) % 58) / 100.0;
        agent.lastAction = "looking";
        agents.push_back(agent);
        registerFactionAgent(*this, int(agents.size()) - 1);
    }
    // (§47) Флот, с которым держава рождается, УЖЕ лицензирован: иначе она
    // стартует с одной лицензией на десяток бортов и первые тысячелетия только
    // догоняет саму себя, а её квота (считается от числа лицензий) оказывается
    // вдесятеро меньше настоящей. Покупкой растёт то, что СВЕРХ рождения.
    resizeFactionLicence();
    for (size_t f = 0; f < factions.size(); ++f) {
        if (int(f) == playerFaction || int(f) == clearingFaction) continue;
        licenceOf(int(f)).count = std::max(1, int(factions[f].fleetAgents.size()));
    }

    // Стартовая доска заданий масштабируется вместе с миром: 24 контракта на
    // 10 000 систем игрок не встречал нигде, кроме родной системы.
    const int seedContracts = std::max<int>(8,
        std::min<int>(CONTRACT_TARGET_FULL, int(double(CONTRACT_TARGET_FULL) * double(num_stars) / double(STAR_COUNT))));
    for (int i = 0; i < seedContracts; ++i) {
        tryCreateDeliveryContract(*this, randomer(rng, int(num_stars) - 1));
    }

    // Перепись при основании партии. Она задаёт СРАЗУ ДВЕ величины: точку, от
    // которой привод держит номинальные цены (отношение к якорю), и точку
    // отсчёта для ставки тарифа (насколько скопление подорожает ПОТОМ).
    // Поэтому ставка первого тысячелетия равна ровно `LICENCE_TARIFF_BASE`.
    clusterPriceLevel = measureClusterPriceLevel();
    clusterPriceBase = clusterPriceLevel;
    marketSetClusterLevel(clusterPriceLevel);
    licence().tariffRate = LICENCE_TARIFF_BASE;
}

void Game::update(double dt) {
    time += dt;
    if (routeCacheBuiltAt < 0.0 || time - routeCacheBuiltAt >= ROUTE_REBUILD_INTERVAL_YEARS) {
        rebuildRouteCache();
    }
    updateMarkets(dt);
    updateMarketEvents(dt);
    updateColonies(dt);
    updateMining(dt);
    updateFactions(dt);
    updateContracts(dt);
    updateAgents(dt);
    // (§50) Диспетчер маяков — ПОСЛЕ движения бортов: спасатель должен видеть
    // сегодняшнее положение дрейфующего, а не вчерашнее. Свой такт внутри.
    updateDistress(dt);
    processSignals();
    updateAnomalies(dt);
    updateLicence(dt);
    // (§34) Годовая сводка прибыли -> очки исследований. Раз в год, а не на
    // каждую продажу: см. комментарий в `sellCargo` про дробление партии.
    tradeInsightTimer += dt;
    if (tradeInsightTimer >= 1.0) {
        if (tradeInsightPending > 0.0) {
            // Множитель 4, а не 2: замер прогона показал, что при двойке чистый
            // торговец получал первое ядро примерно за тридцать рейсов, то есть
            // за тысячу лет. Прокачка от торговли должна быть медленной, но не
            // невидимой — теперь первое ядро выходит рейсов за двенадцать.
            addResearch(4.0 * std::log10(1.0 + tradeInsightPending / 1000.0), TECH_CHARISMA);
            tradeInsightPending = 0.0;
        }
        tradeInsightTimer = 0.0;
    }
    // Свет дошёл до края скопления — приход стал доступен к трате где угодно.
    // Список короткий (по одной записи на взнос), поэтому чистка тривиальна.
    if (!creditFloat.empty()) {
        creditFloat.erase(std::remove_if(creditFloat.begin(), creditFloat.end(),
            [this](const CreditFloat& c) { return c.clearsAt <= time; }), creditFloat.end());
    }
    if (playerAgent >= 0 && playerAgent < int(agents.size()) && !agents[playerAgent].ship.enRoute) {
        observeStar(agents[playerAgent].currentStar);
        agentCompleteContracts(playerAgent);
    }
    
    if (playerAgent >= 0 && playerAgent < int(agents.size())) {
        double currentMoney = agents[playerAgent].money;
        if (lastPlayerMoney >= 0.0) {
            // Из безымянной кассовой строки вычитается всё, что УЖЕ объяснено
            // отдельной строкой журнала (награда за заказ). Иначе одна выплата
            // ложилась бы в журнал дважды.
            double diff = currentMoney - lastPlayerMoney - journalExplained;
            if (std::abs(diff) > 0.01) {
                pushJournal(JournalKind::Money, std::string(), diff, agents[playerAgent].currentStar);
            }
        }
        journalExplained = 0.0;
        lastPlayerMoney = currentMoney;
    }
}

// Журнал — лента событий игрока, а не только касса. Порядок записей —
// хронологический; окно рисует их с конца.
void Game::pushJournal(JournalKind kind, const std::string& text, double amount, int starIndex) {
    Transaction t;
    t.time = time;
    t.starIndex = starIndex;
    t.amount = amount;
    t.kind = kind;
    t.text = text;
    transactions.push_back(t);
    if (transactions.size() > 100) {
        transactions.erase(transactions.begin(), transactions.begin() + 20);
    }
}

// «Что и куда» одной строкой. Собирается ПО-АНГЛИЙСКИ — перевод делает словарь
// в `UI::drawText` (§14), имена звёзд и числа он не трогает.
std::string Game::contractJournalText(const Contract& contract) const {
    // Тип пишется СЛОВОМ, а не трёхбуквенным `contractTypeLabel`: в тесной
    // строке доски «DEL» экономит место, а в журнале читать нечего — и словарь
    // §14 переводит только целые слова, так что «DEL» осталось бы латиницей.
    const char* type = "JOB";
    switch (contract.type) {
    case ContractType::Delivery:     type = "DELIVERY"; break;
    case ContractType::Courier:      type = "COURIER"; break;
    case ContractType::Scout:        type = "SCOUT"; break;
    case ContractType::Bounty:       type = "BOUNTY"; break;
    case ContractType::Escort:       type = "ESCORT"; break;
    case ContractType::Raid:         type = "RAID"; break;
    case ContractType::ColonySupply: type = "COLONY SUPPLY"; break;
    }
    std::string out = "#" + std::to_string(contract.id) + " " + type;
    if (contractUsesCargo(contract.type) &&
        contract.resource >= 0 && contract.resource < int(elementCount())) {
        char amount[32];
        std::snprintf(amount, sizeof(amount), " %.0F ", contract.amount);
        out += amount;
        out += elementDefinitions()[size_t(contract.resource)].symbol;
    }
    if (validStar(*this, contract.targetStar)) {
        out += " > " + cluster.stars[size_t(contract.targetStar)].name;
    }
    return out;
}

void Game::updateMarkets(double dt) {
    const int count = int(markets.size());
    if (count <= 0) return;
    // Уровень ПРИНАДЛЕЖИТ партии и меняется только на тысячелетней сверке;
    // сюда он просто проталкивается, чтобы ценовой слой не хранил своего
    // состояния и оставался чистой функцией данных рынка.
    marketSetClusterLevel(clusterPriceLevel);

    if (marketUpdatedAt.size() != markets.size()) {
        marketUpdatedAt.assign(markets.size(), time - MARKET_UPDATE_INTERVAL_YEARS);
        marketUpdateCursor = 0;
        marketUpdateBudget = 0.0;
    }
    if (marketUpdateCursor < 0 || marketUpdateCursor >= count) marketUpdateCursor = 0;

    auto updateOne = [this, count](int starIndex) {
        if (starIndex < 0 || starIndex >= count) return;
        const double elapsed = std::max(0.0, time - marketUpdatedAt[size_t(starIndex)]);
        if (elapsed <= 0.0) return;
        
        Market& m = markets[size_t(starIndex)];
        if (m.demandNoise.empty()) {
            m.demandNoise.assign(m.prices.size(), 0.0);
        }
        for (size_t i = 0; i < m.demandNoise.size(); ++i) {
            double phase = time * 0.03 + double(starIndex * 73 + i * 137);
            double noiseFactor = std::sin(phase) * std::sin(phase * 1.83);
            m.demandNoise[i] = noiseFactor > 0.0 ? noiseFactor * 25.0 : 0.0;
        }
        
        m.update(elapsed);
        marketUpdatedAt[size_t(starIndex)] = time;
    };

    marketUpdateBudget += double(count) * std::max(0.0, dt) / MARKET_UPDATE_INTERVAL_YEARS;
    const int steps = std::min(count, int(marketUpdateBudget));
    marketUpdateBudget -= double(steps);
    for (int i = 0; i < steps; ++i) {
        const int starIndex = marketUpdateCursor;
        marketUpdateCursor = (marketUpdateCursor + 1) % count;
        updateOne(starIndex);
    }
}

void Game::updateColonies(double dt) {
    for (Colony& colony : colonies) {
        if (!validStar(*this, colony.starIndex) || !validFaction(*this, colony.ownerFaction)) continue;

        ClusterStar& star = cluster.stars[colony.starIndex];
        Market& market = markets[colony.starIndex];
        // Незакрытые нужды рынка (модель замещения) бьют по колонии напрямую:
        // нечем дышать и строить — рост встаёт, производство чахнет.
        const double supplySatisfaction = std::max(0.12, 1.0 - market.strain * 1.15);
        const double damageFactor = std::max(0.1, 1.0 - colony.damage);
        const double growthRate = (0.0012 + star.habitability * 0.0035 + colony.infrastructure * 0.00035) * supplySatisfaction * damageFactor;
        // Потолок прироста — доля от самой колонии, а не абсолютные 1200 человек:
        // на прежнем масштабе населения это была страховка, на нынешнем стало бы
        // стеной, за которой метрополия не растёт вовсе.
        const double gained = std::min(double(colony.population) * 0.04,
                                       double(colony.population) * growthRate * dt);
        const size_t gainedPopulation = size_t(std::max(0.0, gained));
        colony.population += gainedPopulation;
        star.population += double(gainedPopulation);
        star.industry += colony.infrastructure * (0.00045 + colony.automation * 0.00025) * supplySatisfaction * dt;
        colony.energyCapacity = std::max(colony.energyCapacity, colony.infrastructure * (0.6 + colony.automation * 0.4));
        colony.marketAccess = std::max(0.15, supplySatisfaction);
        colony.defense += (0.002 + colony.infrastructure * 0.0011 + colony.energyCapacity * 0.0003) * damageFactor * dt;
        star.defense = std::max(star.defense, colony.defense);
        colony.infrastructure += star.industry * 0.000035 * supplySatisfaction * dt;

        // (§36) Хозяйство выросло — рынок обязан это заметить. Раньше `needs` и
        // `productionRate` задавались только при генерации мира: система с
        // миллиардом жителей потребляла столько же, сколько посёлок, из
        // которого она выросла, и петля «вложился в колонию → она больше
        // производит» была разомкнута. Пересчёт срабатывает ступенькой, при
        // расхождении масштаба на 5%, — не работа на каждый тик.
        // Новость — только про СВОИ колонии: ростом чужих лента забивалась
        // одной и той же строкой на длинной партии.
        if (market.rescale(star.population, star.industry) && playerOwnsStar(colony.starIndex)) {
            pushNews(star.name + " has outgrown its old market", 1);
        }

        const double income = colonyIncomeAt(colony.starIndex) * dt;
        colony.localLedger += income;
        colony.stockpileValue = 0.0;
        for (const Resource& resource : colony.stockpile) {
            const int index = elementIndex(resource.element);
            if (index >= 0 && index < int(market.prices.size())) colony.stockpileValue += resource.amount * market.prices[index];
        }
        if (!colony.constructionQueue.empty() && colony.localLedger > 0.0) {
            ConstructionItem& item = colony.constructionQueue.front();
            const double spend = std::min(colony.localLedger, std::max(1.0, item.cost * 0.08) * dt);
            colony.localLedger -= spend;
            // (§37.5) Привезённые материалы ИДУТ В ДЕЛО. `Colony::stockpile`
            // копился с поставок (`applyColonySupplyDelivery`) и не тратился
            // никогда: заказ «привезти колонии металл» кончался тем, что металл
            // ложился в вечную кучу, а стройка всё равно шла за деньги. Теперь
            // склад ускоряет стройку — вдвое, если материалов на весь остаток, —
            // и на это же и расходуется. Так доставка становится вкладом, а не
            // жестом вежливости.
            double boost = 0.0;
            if (!colony.stockpile.empty() && colony.stockpileValue > 0.0) {
                const double want = std::min(colony.stockpileValue, spend);
                const double share = want / std::max(1e-9, colony.stockpileValue);
                for (size_t r = 0; r < colony.stockpile.size(); ) {
                    colony.stockpile[r].amount *= (1.0 - share);
                    if (colony.stockpile[r].amount <= 1e-6) {
                        colony.stockpile.erase(colony.stockpile.begin() + long(r));
                    } else {
                        ++r;
                    }
                }
                colony.stockpileValue = std::max(0.0, colony.stockpileValue - want);
                boost = want;
            }
            item.progress += spend + boost;
            if (item.progress >= item.cost) {
                colonyApplyConstructionEffect(colony, item);
                colony.constructionQueue.erase(colony.constructionQueue.begin());
            }
        } else if (colony.constructionQueue.empty() && colony.localLedger > 750.0) {
            colony.constructionQueue.push_back(colonySuggestedConstruction(colony));
        }

        // Прибыль колонии УХОДИТ ВЛАДЕЛЬЦУ по проводам, а не ждёт, пока за ней
        // прилетят. Касса остаётся рабочим балансом стройки: колония держит
        // ровно столько, сколько нужно текущей очереди, остальное телеграфирует.
        // Империю больше не надо облетать ради денег — но и мгновенной она не
        // становится: у игрока приход ложится в поплавок (§16) и станет
        // доступным к трате, только когда известие покроет скопление.
        Faction& faction = factions[colony.ownerFaction];
        const double reserve = colony.constructionQueue.empty()
            ? 0.0
            : std::max(0.0, colony.constructionQueue.front().cost - colony.constructionQueue.front().progress);
        const double surplus = colony.localLedger - reserve;
        if (surplus > 0.0) {
            colony.localLedger -= surplus;
            faction.treasury += surplus;
            if (colony.ownerFaction == playerFaction) addCreditFloat(playerFaction, surplus, colony.starIndex);
        }
        faction.strength += 0.00015 * dt;
    }
}

void Game::updateContracts(double dt) {
    // Невзятое объявление снимают с доски сразу по сроку. Проверка КАЖДЫЙ кадр,
    // и это нарочно: она стоит одно сравнение на заказ.
    for (Contract& contract : contracts) {
        if (activeContract(contract) && contract.acceptedByAgent < 0 && contract.deadline < time) {
            contract.failed = true;
        }
    }

    contractUpdateTimer -= dt;
    if (contractUpdateTimer > 0.0 || cluster.stars.empty()) return;

    // ВЗЯТЫЙ заказ до §23 не мог провалиться вообще: он висел вечно и платил
    // 45% сколь угодно поздно. Замер: 443 года сверх срока — `failed=0`, заказ
    // всё ещё на руках. Срок был не обязательством, а ценником. Теперь сверх
    // срока даётся ещё один такой же рейс — и всё.
    //
    // ⚠️ Проверка стоит ПОСЛЕ таймера и за `deadline`-отсечкой: `contractRouteYears`
    // обходит маршрут по прыжкам, а заказов бывает под тысячу. Каждый кадр по
    // всем — это обход скопления на кадр. Такт в ~0.75 года против сроков в
    // десятки лет не теряет ничего.
    for (Contract& contract : contracts) {
        if (!activeContract(contract) || contract.acceptedByAgent < 0) continue;
        if (time <= contract.deadline) continue;
        const double grace = std::max(4.0,
            contractRouteYears(*this, contract.originStar, contract.targetStar) * CONTRACT_GRACE_FACTOR);
        if (time <= contract.deadline + grace) continue;

        contract.failed = true;
        if (contract.acceptedByAgent >= int(agents.size())) continue;
        Agent& holder = agents[contract.acceptedByAgent];
        holder.lastAction = "contract expired";
        if (!holder.playerControlled) continue;
        lastEvent = "contract expired";
        const double lost = applyContractFailureReputation(*this, contract);
        // Текст журнала собирается ПО-АНГЛИЙСКИ: перевод делает сама
        // `UI::drawText` по словарю (§14), числа и имена звёзд она не трогает.
        char penalty[96];
        std::snprintf(penalty, sizeof(penalty), " EXPIRED -%.0F REP -%.0F Cr DEPOSIT",
                      lost, contract.deposit);
        pushJournal(JournalKind::JobFailed,
            contractJournalText(contract) + penalty, 0.0, holder.currentStar);
    }
    contractUpdateTimer = 0.75 + 0.05 * double(randomer(rng, 10));

    if (playerAgent >= 0 && playerAgent < int(agents.size()) && !agents[playerAgent].ship.enRoute) {
        const int owner = cluster.stars[agents[playerAgent].currentStar].ownerFaction;
        if (validFaction(*this, owner)) observeLocalThreatsForFaction(owner, agents[playerAgent].currentStar);
        tryCreateDeliveryContract(*this, agents[playerAgent].currentStar);
        tryCreateCourierContract(*this, agents[playerAgent].currentStar);
        tryCreateScoutContract(*this, agents[playerAgent].currentStar);
        tryCreateBountyContract(*this, agents[playerAgent].currentStar);
        tryCreateRaidContract(*this, agents[playerAgent].currentStar);
        tryCreateEscortContract(*this, agents[playerAgent].currentStar);
    }
    const int samples = std::min(18, std::max(4, int(cluster.stars.size()) / 64));
    for (int i = 0; i < samples; ++i) {
        const int origin = randomer(rng, int(cluster.stars.size()) - 1);
        const int owner = cluster.stars[origin].ownerFaction;
        if (validFaction(*this, owner)) observeLocalThreatsForFaction(owner, origin);
        if (i % 7 == 0) {
            tryCreateScoutContract(*this, origin);
        } else if (i % 7 == 1) {
            tryCreateCourierContract(*this, origin);
        } else if (i % 7 == 2) {
            tryCreateColonySupplyContract(*this, origin);
        } else if (i % 7 == 3) {
            tryCreateBountyContract(*this, origin);
        } else if (i % 7 == 4) {
            tryCreateRaidContract(*this, origin);
        } else if (i % 7 == 5) {
            tryCreateEscortContract(*this, origin);
        } else {
            tryCreateDeliveryContract(*this, origin);
        }
    }

    if (contracts.size() > 900) {
        contracts.erase(std::remove_if(contracts.begin(), contracts.end(), [this](const Contract& contract) {
            return (contract.completed || contract.failed) && time - contract.postedTime > 18.0;
        }), contracts.end());
    }
}

// (§47) Держава ПОКУПАЕТ систему у центра — тем же прайсом (`systemPrice`) и с
// той же кассой, что и игрок (§13). Это и делает шестнадцать симметричными:
// раньше расширяться покупкой умел один капитан, а ИИ — только колонизацией.
//
// ⚠️ Ни одного обращения к ГПСЧ: выбор строго детерминирован (§2.3), иначе
// ветка двигала бы поток мира и «включённый ИИ» расходился бы с выключенным.
// Обзор ОГРАНИЧЕН окном в 64 звезды со скользящего курсора: проход по восьми
// тысячам звёзд на каждую державу каждый такт — это обход скопления на кадр.
void Game::factionTryBuySystem(int factionIndex) {
    if (!validFaction(*this, factionIndex)) return;
    if (factionIndex == playerFaction || factionIndex == clearingFaction) return;
    Faction& power = factions[size_t(factionIndex)];
    // Отозванная лицензия не покупает империю: сначала расплатись (§47).
    if (licenceOf(factionIndex).revoked) return;
    // Тратится ТОЛЬКО колониальный бюджет, и не весь: держава не спускает казну
    // на одну звезду. Тот же принцип, что у потолка награды за заказ (§24).
    const double purse = std::min(power.treasury, power.colonyBudget);
    if (!(purse > 0.0) || cluster.stars.empty()) return;
    // Предел управляемости (см. SYSTEM_ADMIN_*): деньги — не единственное, чем
    // покупается суверенитет. Колонизацию это НЕ трогает: там держава растит
    // владения своими руками, а не покупает готовое.
    const int adminCap = SYSTEM_ADMIN_BASE +
        SYSTEM_ADMIN_PER_HULL * int(power.fleetAgents.size());
    if (int(power.controlledStars.size()) >= adminCap) return;
    // Выдержка (см. SYSTEM_BUY_INTERVAL_YEARS): не чаще раза в тысячу лет.
    resizeShareBooks();
    if (size_t(factionIndex) < factionNextSystemBuy.size() &&
        time < factionNextSystemBuy[size_t(factionIndex)]) return;

    const size_t starCount = cluster.stars.size();
    const size_t window = std::min<size_t>(64, starCount);
    int best = -1;
    double bestScore = 0.0;
    double bestCost = 0.0;
    for (size_t k = 0; k < window; ++k) {
        const size_t i = (size_t(factionExpansionCursor) + k) % starCount;
        const ClusterStar& star = cluster.stars[i];
        if (star.ownerFaction >= 0) continue;          // покупаем только у центра
        const double cost = systemPrice(int(i)).total;
        if (!(cost > 0.0) || cost > purse) continue;
        // Ценность на кредит, со скидкой за расстояние от дома: держава берёт
        // то, что может держать, а не то, что дальше всех.
        double reach = 1.0;
        if (validStar(*this, power.homeStar)) {
            reach = 1.0 + distanceBetween(cluster.stars[size_t(power.homeStar)], star) * 0.05;
        }
        const double worth = (starPopulationWeight(star) * 3.0 + star.industry * 4.0 +
                              star.habitability * 5.0) / (cost * reach);
        if (worth > bestScore) { bestScore = worth; best = int(i); bestCost = cost; }
    }
    factionExpansionCursor = int((size_t(factionExpansionCursor) + window) % starCount);
    if (best < 0) return;

    power.treasury -= bestCost;
    // Выручка идёт центру: он и был продавцом (§47).
    if (validFaction(*this, clearingFaction)) factions[size_t(clearingFaction)].treasury += bestCost;
    setStarOwner(*this, best, factionIndex);
    transferColonies(*this, best, factionIndex);
    if (colonyIndexAt(*this, best) < 0) addColony(*this, best, factionIndex, false);
    if (size_t(factionIndex) < factionNextSystemBuy.size()) {
        factionNextSystemBuy[size_t(factionIndex)] = time + SYSTEM_BUY_INTERVAL_YEARS;
    }
}

void Game::updateFactions(double dt) {
    factionUpdateTimer -= dt;
    if (factionUpdateTimer > 0.0) return;
    factionUpdateTimer = 1.0;
    resizeFactionRelations();
    resizeFactionReputation();
    // (§33) Книга ОДНОЙ державы по кругу. Пересчёт стоит прохода по всем её
    // системам, поэтому делать его сразу для всех — расточительство; а отставание
    // котировки от жизни здесь не недостаток, а суть: биржа узнаёт о потерянной
    // системе позже, чем игрок, который там был.
    resizeShareBooks();
    if (!factions.empty()) {
        factionBookCursor %= int(factions.size());
        const int published = factionBookCursor;
        publishFactionBook(published);
        factionBookCursor = (factionBookCursor + 1) % int(factions.size());
        // По одной державе за такт — тем же кругом, что и публикация книги.
        factionTryBuySystem(published);
        factionTryBuyShares(published);
        factionTryGrowFleet(published);
        // (§37.1) РЕПУТАЦИЯ ОТКРЫВАЕТ КНИГИ. У доверенного возчика отчёт всегда
        // свежий: он и есть тот, кто возит их грузы и видит их склады изнутри.
        //
        // Это единственный выход у репутации, кроме размера заказов: до сих пор
        // она копилась до потолка 1000 и упиралась в звание. А отставание
        // котировки от жизни (§33) — главное, чем биржа вообще интересна:
        // знать раньше рынка и есть заработок.
        // ⚠️ Не больше ОДНОЙ дополнительной книги за такт. Смысл курсора — в
        // амортизации: проход по всем системам державы стоит `systemTurnover` на
        // каждую, а у доверенного возчика инсайдерских держав может оказаться
        // хоть пять. Круг по инсайдерам идёт своим курсором, поэтому свежесть
        // сохраняется, а цена такта — нет.
        int insider = -1;
        for (size_t k = 0; k < factions.size(); ++k) {
            const int f = (published + 1 + int(k)) % int(factions.size());
            if (f == published) continue;                // эту уже обновили выше
            if (factionJobTier(f) < SHARE_INSIDER_TIER) continue;
            insider = f;
            break;
        }
        if (insider >= 0) publishFactionBook(insider);
    }
    payShareDividends(1.0);

    for (size_t i = 0; i < factions.size(); ++i) {
        Faction& faction = factions[i];
        faction.orders.clear();
        faction.diplomacyPressure = 0.0;
        faction.borderPressure = 0.0;
        faction.raidPressure = 0.0;
        faction.tradePressure = 0.0;
        bool absorbedAnyRelay = false;
        for (int starIndex : faction.controlledStars) {
            if (!validStar(*this, starIndex)) continue;
            applyLocalFactionReports(*this, int(i), starIndex);
            absorbedAnyRelay = true;
        }
        if (!absorbedAnyRelay && validStar(*this, faction.homeStar)) {
            applyLocalFactionReports(*this, int(i), faction.homeStar);
        }
    }

    for (size_t a = 0; a < factions.size(); ++a) {
        for (size_t b = 0; b < factions.size(); ++b) {
            if (a == b) {
                setFactionRelation(int(a), int(b), 128);
                continue;
            }

            double nearestBorder = 1e9;
            for (int starA : factions[a].controlledStars) {
                if (!validStar(*this, starA)) continue;
                for (int starB : factions[b].controlledStars) {
                    if (!validStar(*this, starB)) continue;
                    nearestBorder = std::min(nearestBorder, distanceBetween(cluster.stars[starA], cluster.stars[starB]));
                }
            }

            int target = 4;
            if (nearestBorder < 18.0) target -= 12;
            if (nearestBorder < 9.0) target -= 20;
            target -= int((factions[a].aggression + factions[b].aggression) * 9.0);
            target += int((factions[a].tradeBias + factions[b].tradeBias) * 4.0);
            target = clampRelation(target);

            const int current = factionRelation(int(a), int(b));
            if (nearestBorder < 1e9) {
                const double border = std::max(0.0, (26.0 - nearestBorder) / 26.0);
                factions[a].borderPressure = std::max(factions[a].borderPressure, border);
                factions[a].diplomacyPressure += std::abs(double(target - current)) / 128.0;
                if (current < -20) factions[a].raidPressure = std::max(factions[a].raidPressure, double(-current) / 128.0);
                if (current > 15) factions[a].tradePressure = std::max(factions[a].tradePressure, double(current) / 128.0);
            }
            if (current < target) setFactionRelation(int(a), int(b), current + 1);
            if (current > target) setFactionRelation(int(a), int(b), current - 1);
        }
    }

    // --- Клиринговая палата тратит собранное как государство ----------------
    // Лицензионные деньги не исчезают: банк раз в такт субсидирует САМУЮ
    // задыхающуюся колонию скопления — ту, где выше всего доля незакрытых нужд.
    // Своих колоний игрока это касается ровно так же, как чужих: банк не знает
    // «свой-чужой», он гасит нестабильность, потому что от неё зависят его же
    // сборы. Деньги идут в кассу колонии, откуда та тратит их на свою стройку.
    if (validFaction(*this, clearingFaction) && factions[clearingFaction].treasury > 0.0) {
        int neediest = -1;
        double worstStrain = 0.15;      // ниже этого система живёт нормально
        for (size_t c = 0; c < colonies.size(); ++c) {
            const int starIndex = colonies[c].starIndex;
            if (!validStar(*this, starIndex) || starIndex >= int(markets.size())) continue;
            if (markets[starIndex].strain > worstStrain) {
                worstStrain = markets[starIndex].strain;
                neediest = int(c);
            }
        }
        if (neediest >= 0) {
            const double grant = std::min(factions[clearingFaction].treasury * 0.02,
                                          colonyIncomeAt(colonies[neediest].starIndex) * 40.0);
            if (grant > 0.0) {
                factions[clearingFaction].treasury -= grant;
                colonies[neediest].localLedger += grant;
            }
        }
    }

    for (size_t i = 0; i < factions.size(); ++i) {
        Faction& faction = factions[i];
        normalizeFactionStrategicFields(faction);
        if (faction.controlledStars.empty()) continue;
        const int origin = validStar(*this, faction.homeStar) ? faction.homeStar : faction.controlledStars[0];

        const int scoutTarget = pickScoutTarget(*this, int(i), origin);
        if (validStar(*this, scoutTarget)) {
            faction.orders.push_back(FactionOrder(FactionOrderType::Scout, origin, scoutTarget,
                10.0 + faction.diplomacyPressure * 6.0 + faction.tradePressure * 3.0, time));
        }

        const int colonyTarget = pickColonistTarget(*this, int(i), origin);
        if (validStar(*this, colonyTarget) && cluster.stars[colonyTarget].ownerFaction != int(i)) {
            faction.orders.push_back(FactionOrder(FactionOrderType::Colonize, origin, colonyTarget,
                8.0 + faction.expansionBias * 8.0 + faction.colonyBudget * 0.001, time));
        }

        const int attackTarget = pickMilitaryTarget(*this, int(i), origin);
        if (validStar(*this, attackTarget)) {
            const FactionOrderType type = faction.raidPressure > 0.25 || faction.aggression > 0.72 ?
                FactionOrderType::AttackSystem : FactionOrderType::Patrol;
            faction.orders.push_back(FactionOrder(type, origin, attackTarget,
                7.0 + faction.raidPressure * 16.0 + faction.aggression * 5.0 + faction.militaryBudget * 0.0007, time));
        }

        int defendTarget = -1;
        double weakest = std::numeric_limits<double>::max();
        for (int starIndex : faction.controlledStars) {
            if (!validStar(*this, starIndex)) continue;
            const double score = cluster.stars[starIndex].defense - cluster.stars[starIndex].industry * 0.8;
            if (score < weakest) {
                weakest = score;
                defendTarget = starIndex;
            }
        }
        if (validStar(*this, defendTarget)) {
            faction.orders.push_back(FactionOrder(FactionOrderType::DefendSystem, origin, defendTarget,
                6.0 + faction.borderPressure * 12.0 + faction.defenseBias * 5.0, time));
        }
    }
}

void Game::processSignals() {
    if (pendingSignals.empty()) return;

    const double eps = 0.000001;
    while (!pendingSignals.empty() && pendingSignals.front().arrivalTime <= time + eps) {
        size_t readyCount = 0;
        while (readyCount < pendingSignals.size() && pendingSignals[readyCount].arrivalTime <= time + eps) {
            readyCount += 1;
        }

        std::vector<SignalPacket> ready(pendingSignals.begin(), pendingSignals.begin() + readyCount);
        pendingSignals.erase(pendingSignals.begin(), pendingSignals.begin() + readyCount);

        for (const SignalPacket& signal : ready) {
            if (validStar(*this, signal.hopStar)) mergeSignalAtStar(*this, signal.hopStar, signal);
            if (validStar(*this, signal.hopStar) && signal.hopStar != signal.destinationStar) {
                SignalPacket forwarded = signal;
                if (forwardSignalRoute(*this, forwarded, time)) {
                    enqueuePendingSignal(*this, forwarded);
                }
                continue;
            }
            if (signal.type == SignalType::OwnerReport) {
                if (signal.recipientFaction == playerFaction && playerAtStar(signal.hopStar) && validStar(*this, signal.subjectStar)) {
                    const ClusterStar& star = cluster.stars[signal.subjectStar];
                    lastEvent = "signal arrived: owner report " + star.name;
                }
            } else if (signal.type == SignalType::MarketReport) {
                if (signal.recipientFaction == playerFaction && playerAtStar(signal.hopStar) && validStar(*this, signal.subjectStar)) {
                    const ClusterStar& star = cluster.stars[signal.subjectStar];
                    lastEvent = "signal arrived: market report " + star.name;
                }
            } else if (signal.type == SignalType::ContractReport) {
                Contract* contract = contractById(*this, signal.contractId);
                if (contract && activeContract(*contract) && contract->type == ContractType::Scout &&
                    contract->reportSignalPending && contract->acceptedByAgent >= 0 &&
                    contract->acceptedByAgent < int(agents.size())) {
                    contract->reportDelivered = true;
                    payContractReward(*this, *contract, agents[contract->acceptedByAgent], false);
                }
                if (signal.recipientFaction == playerFaction && validStar(*this, signal.subjectStar)) {
                    lastEvent = "signal arrived: contract report " + cluster.stars[signal.subjectStar].name;
                }
            } else if (signal.type == SignalType::CombatReport) {
                if (signal.recipientFaction == playerFaction && playerAtStar(signal.hopStar) && validStar(*this, signal.originStar)) {
                    lastEvent = "signal arrived: combat report " + cluster.stars[signal.originStar].name;
                }
            } else if (signal.type == SignalType::SettlementReport) {
                if (signal.recipientFaction == playerFaction && playerAtStar(signal.hopStar) && validStar(*this, signal.originStar)) {
                    lastEvent = "signal arrived: settlement " + cluster.stars[signal.originStar].name;
                }
            } else if (signal.type == SignalType::DiplomacyReport) {
                if (signal.recipientFaction == playerFaction && playerAtStar(signal.hopStar) && validFaction(*this, signal.targetFaction)) {
                    lastEvent = "signal arrived: diplomacy " + factions[signal.targetFaction].name;
                }
            }
        }
    }
}

// (§50) СПАСЕНИЕ ИЗ ДРЕЙФА — механика, а не таймер.
//
// До этого раздела помощь приходила из ниоткуда ровно через `TOW_WAIT_YEARS`:
// заглушка-телепорт. Теперь ожидание физическое и складывается из двух слагаемых,
// каждое из которых можно замерить отдельно:
//
//   1. СВЕТ ДОШЁЛ. Маяк — точка и момент; услышал тот, до кого свет успел дойти
//      (`time >= raisedTime + расстояние`). Скорость света в единицах игры
//      равна единице: расстояния — световые годы, время — годы. Ровно так же
//      считает вся очередь `pendingSignals` (§16).
//   2. СПАСАТЕЛЬ ДОЛЕТЕЛ. Ближайший из услышавших бросает дело и идёт на
//      перехват на полном ходу. Гарантия встречи держится ДОГОНЯЕМОСТЬЮ (§48.6):
//      в кандидаты берётся только борт, чей потолок скорости выше скорости
//      дрейфа. Гравитация тут ни при чём — она посчитана и отвергнута числом.
//
// Долетев до ТОЧКИ СИГНАЛА, спасатель получает настоящее положение борта:
// дрейф строго баллистический, поэтому «точка плюс скорость на прошедшее время»
// — не догадка, а расчёт (решение пользователя, §48.8 п.2). Догонит или нет —
// его забота: скорость дрейфа никто не гасит.
//
// ⚠️ На маяк слетаются не только спасатели. Пират или борт враждебной державы,
// услышавший сигнал первым, приходит за ГРУЗОМ, а не за жизнью: вычищает трюм и
// уходит. Спасение при этом никуда не девается — оно остаётся бесплатным и
// гарантированным (§48.8 п.3), просто застрявшего привезут в порт пустым. Так
// у беды появляется цена, которую нельзя переждать: время теряют все, а груз —
// тот, кто сломался далеко от своих.

// Цена корпуса по имени борта.
//
// ⚠️ Точного совпадения НЕ ХВАТАЕТ, и замер это поймал: награда за спасение
// вышла нулевой при списании 17 927 Cr с казны. У игрока имя борта и правда
// равно имени класса (иначе `buyShip` не найдёт текущий корпус), а флот держав
// строится под составным именем вида `Vekhra_Hauler_7` — и точный поиск по нему
// не находит ничего. Поэтому: сначала точное совпадение, потом вхождение имени
// класса в имя борта, и из подошедших берётся САМОЕ ДЛИННОЕ имя — иначе
// «Hauler» победил бы «Heavy Hauler» на одном и том же борту.
double hullClassPrice(const Ship& ship) {
    const std::vector<ShipClass>& classes = shipClasses();
    for (size_t c = 0; c < classes.size(); ++c) {
        if (classes[c].name == ship.name) return classes[c].price;
    }
    double price = 0.0;
    size_t longest = 0;
    for (size_t c = 0; c < classes.size(); ++c) {
        const std::string& name = classes[c].name;
        if (name.empty() || name.size() <= longest) continue;
        if (ship.name.find(name) == std::string::npos) continue;
        longest = name.size();
        price = classes[c].price;
    }
    if (price > 0.0) return price;

    // Ни то, ни другое: борта, которыми населяется мир на старте, — БЕЗЫМЯННЫЕ
    // по классу. Они собираются числами прямо в `Game::init` («Trader_7» с
    // выданными вручную трюмом и ускорением) и ни к одной строке таблицы не
    // привязаны. Для них цена берётся по БЛИЖАЙШЕЙ сухой массе: именно она
    // ведёт лестницу цен в таблице классов, и оценка «во что обошёлся бы такой
    // корпус на верфи» — это ровно тот вопрос, на который отвечает награда.
    double bestGap = 1e300;
    for (size_t c = 0; c < classes.size(); ++c) {
        if (classes[c].price <= 0.0) continue;
        const double gap = std::fabs(classes[c].dryMass - ship.dryMass);
        if (gap < bestGap) { bestGap = gap; price = classes[c].price; }
    }
    return price;
}

double distanceShipToPoint(const Ship& ship, double x, double y, double z) {
    const double dx = x - ship.x, dy = y - ship.y, dz = z - ship.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Положение борта по данным САМОГО МАЯКА: точка плюс скорость на прошедшее
// время. Совпадает с настоящим до последнего знака, пока дрейф баллистический
// (а он баллистический: у мёртвого двигателя нет тяги), — и это утверждение
// проверяется харнесом, а не берётся на веру.
void beaconPredictedPosition(const DistressBeacon& beacon, double now, double& x, double& y, double& z) {
    const double years = std::max(0.0, now - beacon.raisedTime);
    x = beacon.x + beacon.vx * years;
    y = beacon.y + beacon.vy * years;
    z = beacon.z + beacon.vz * years;
}

// (§50) ПЕРЕХВАТ: цель — не звезда, а точка, которая сама летит.
//
// `moveShipToward` для этого не годится: он гасит скорость В НОЛЬ у неподвижной
// звезды, а на перехвате гасить надо РАЗНИЦУ скоростей. Схема та же самая,
// только записанная в системе отсчёта цели: желаемая скорость = скорость цели
// плюс подход по направлению на неё, а подход ограничен тем, что ещё можно
// погасить на оставшемся пути (sqrt(2*a*s)). Ни одного нового числа: и потолок
// скорости, и расход в быстроте взяты у самого полёта.
//
// ⚠️ Критерий встречи ШАГОЗАВИСИМЫЙ, и это не небрежность, а урок §48.9.
// Погасить разницу ровно в точке нельзя никогда: шаг интегрирования конечен.
// Мерить надо не величину остатка, а ВЫПОЛНИМОСТЬ манёвра — «до цели меньше
// одного шага сближения И разницу можно снять одним импульсом тяги». Ровно так
// же устроено прибытие к звезде (`speed <= accel * dt`). Порог по остатку
// проверялся: на шаге 1.0 года спасатель проскакивал цель и болтался вокруг неё
// вечно, а на шаге 0.01 сходился — то есть проверялась ошибка интегрирования.
// `matchVelocity` разводит два разных дела. ТОЧКА СИГНАЛА — путевая: её надо
// ПРОЙТИ, а не остановиться в ней. ⚠️ Первая версия тормозила в ноль и там:
// замер показал скорость 0.000000 c в точке маяка, то есть борт гасил весь
// разгон, чтобы тут же набрать его заново. Это стоило лишнего цикла
// «разгон + торможение» — а топливо ему покупалось на ОДИН, по `legCost`, — и
// било в тот самый сценарий, где спасатель сохнет на догоне. Уравнивать
// скорость нужно только с самим дрейфующим бортом: там это и есть швартовка.
bool chaseShipToward(Ship& ship, double tx, double ty, double tz,
                     double tvx, double tvy, double tvz, double dt, bool matchVelocity) {
    const double dx = tx - ship.x, dy = ty - ship.y, dz = tz - ship.z;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double rvx = ship.vx - tvx, rvy = ship.vy - tvy, rvz = ship.vz - tvz;
    const double rel = std::sqrt(rvx * rvx + rvy * rvy + rvz * rvz);
    const double accel = shipCurrentAcceleration(ship);

    if (dist <= std::max(RESCUE_DOCK_DISTANCE, rel * dt) &&
        (!matchVelocity || rel <= std::max(RESCUE_MATCH_SPEED, accel * dt))) {
        if (!matchVelocity) return true;    // точку прошли, ход не гасим
        // Встреча состоялась: садимся борт к борту и уравниваем скорость.
        ship.x = tx; ship.y = ty; ship.z = tz;
        ship.vx = tvx; ship.vy = tvy; ship.vz = tvz;
        return true;
    }

    if (accel > 0.0) {
        const double dirX = dist > 1e-12 ? dx / dist : 0.0;
        const double dirY = dist > 1e-12 ? dy / dist : 0.0;
        const double dirZ = dist > 1e-12 ? dz / dist : 0.0;
        const double targetSpeed = std::sqrt(tvx * tvx + tvy * tvy + tvz * tvz);
        const double ceiling = shipCruiseSpeed(ship);
        // Запас над целью — это и есть догоняемость: если он нулевой, борт
        // висит на хвосте и не сближается. Диспетчер таких в спасатели не берёт.
        const double headroom = std::max(0.0, ceiling - targetSpeed);
        // Тормозной профиль нужен только там, где надо ОСТАНОВИТЬСЯ рядом. К
        // путевой точке идём на полном ходу и проходим её насквозь.
        const double approach = matchVelocity
            ? std::min(headroom, std::sqrt(2.0 * accel * std::max(0.0, dist)))
            : headroom;
        const double wantX = tvx + dirX * approach;
        const double wantY = tvy + dirY * approach;
        const double wantZ = tvz + dirZ * approach;
        const double needX = wantX - ship.vx, needY = wantY - ship.vy, needZ = wantZ - ship.vz;
        const double need = std::sqrt(needX * needX + needY * needY + needZ * needZ);
        if (need > 1e-12) {
            const double speed = std::sqrt(ship.vx * ship.vx + ship.vy * ship.vy + ship.vz * ship.vz);
            const double rapidityCost = 1.0 / std::max(1e-6, 1.0 - speed * speed);
            const double budget = std::min(need, accel * dt);
            const double given = consumeAndStoreAsh(ship, budget * rapidityCost) / rapidityCost;
            const double k = given / need;
            ship.vx += needX * k;
            ship.vy += needY * k;
            ship.vz += needZ * k;
        }
        const double after = std::sqrt(ship.vx * ship.vx + ship.vy * ship.vy + ship.vz * ship.vz);
        if (after > ceiling && after > 1e-12) {
            const double k = ceiling / after;
            ship.vx *= k; ship.vy *= k; ship.vz *= k;
        }
    }

    ship.x += ship.vx * dt;
    ship.y += ship.vy * dt;
    ship.z += ship.vz * dt;
    return false;
}

void closeDistress(Game& game, int beaconIndex);   // (§50) объявлена ниже, нужна буксиру

// (§38) Буксир для корабля, которому нечем тормозить.
//
// Берёт долю кошелька и половину трюма, ставит корабль в БЛИЖАЙШУЮ систему и
// заливает по глотку обоих расходников — ровно столько, чтобы можно было
// доползти до рынка, а не столько, чтобы происшествие ничего не стоило.
void towStrandedShip(Game& game, int agentIndex) {
    if (agentIndex < 0 || agentIndex >= int(game.agents.size())) return;
    Agent& agent = game.agents[size_t(agentIndex)];
    if (game.cluster.stars.empty()) return;

    int nearest = -1;
    double bestD2 = 1e300;
    for (size_t s = 0; s < game.cluster.stars.size(); ++s) {
        const ClusterStar& st = game.cluster.stars[s];
        const double dx = st.x - agent.ship.x, dy = st.y - agent.ship.y, dz = st.z - agent.ship.z;
        const double d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bestD2) { bestD2 = d2; nearest = int(s); }
    }
    if (nearest < 0) return;

    // (§48.8) СПАСЕНИЕ БЕСПЛАТНО. Цена — только ожидание.
    //
    // ⚠️ Здесь буксир забирал `TOW_CREDIT_SHARE` кошелька и `TOW_CARGO_SHARE`
    // трюма, то есть «спасение» было ШТРАФОМ, приходящим безлично и без всякого
    // решения игрока. Решение пользователя (§48.8): вытаскивает государство и
    // даром, а платит застрявший временем — сигнал идёт со светом, спасатель
    // летит своим ходом. Штраф деньгами поверх этого был бы второй платой за
    // одну беду и спорил бы с живым спасателем, которому награду платит казна.
    const ClusterStar& port = game.cluster.stars[size_t(nearest)];
    agent.ship.x = port.x; agent.ship.y = port.y; agent.ship.z = port.z;
    agent.ship.vx = agent.ship.vy = agent.ship.vz = 0.0;
    agent.ship.enRoute = false;
    agent.ship.targetStar = -1;
    agent.currentStar = nearest;
    agent.destStar = nearest;
    agent.missionCooldown = 0.0;
    // Глоток «на дорогу»: без него корабль встанет ровно так же на следующем
    // же плече, и буксир превратится в бесконечную петлю.
    shipEmergencyPrime(agent.ship);
    ++s_strandTows;
    // (§50) Беда кончилась — маяк снимается здесь же, а не на следующем опросе:
    // иначе спасатель ещё четверть года летел бы к уже вытащенному борту.
    agent.driftYears = 0.0;
    for (size_t b = 0; b < game.distress.size(); ++b) {
        if (game.distress[b].agent != agentIndex) continue;
        closeDistress(game, int(b));
        break;
    }
    agent.lastAction = "towed in";
    if (agent.playerControlled) {
        game.lastEvent = "rescued - brought in to " + port.name;
        game.pushNews("Distress call answered: towed to " + port.name, 0);
    }
}

// (§50) Поднять маяк. Один борт — один маяк: повторный вызов ничего не делает,
// поэтому звать её можно из каждой ветки дрейфа и на каждом тике.
void raiseDistress(Game& game, int agentIndex) {
    if (agentIndex < 0 || agentIndex >= int(game.agents.size())) return;
    for (size_t b = 0; b < game.distress.size(); ++b) {
        if (game.distress[b].agent == agentIndex) return;
    }
    const Ship& ship = game.agents[size_t(agentIndex)].ship;
    DistressBeacon beacon;
    beacon.agent = agentIndex;
    beacon.x = ship.x; beacon.y = ship.y; beacon.z = ship.z;
    beacon.vx = ship.vx; beacon.vy = ship.vy; beacon.vz = ship.vz;
    beacon.raisedTime = game.time;
    game.distress.push_back(beacon);
    ++s_rescueBeacons;
    if (game.agents[size_t(agentIndex)].playerControlled) {
        game.lastEvent = "distress beacon raised";
        game.pushNews("Distress beacon raised - adrift with dead engines", 2);
    }
}

// Снять маяк. ⚠️ `erase` СДВИГАЕТ индексы, а на них ссылается `rescueTarget` у
// спасателей: без пересчёта борт после чужого спасения молча улетал бы догонять
// не того. Маяков единицы, поэтому честный проход по агентам дешевле, чем
// заводить долгоживущие идентификаторы и хранить их в сейве.
void closeDistress(Game& game, int beaconIndex) {
    if (beaconIndex < 0 || beaconIndex >= int(game.distress.size())) return;
    for (size_t a = 0; a < game.agents.size(); ++a) {
        Agent& r = game.agents[a];
        if (r.rescueTarget == beaconIndex) {
            r.rescueTarget = -1;
            r.rescueKnows = false;
            r.ship.enRoute = false;
            r.ship.targetStar = -1;
            // ⚠️ Спасателя, чей рейс отменили, надо ОТШВАРТОВАТЬ, если он уже
            // в пустоте: `currentStar` продолжал бы указывать на порт вылета за
            // десятки световых лет, и `updateTrader` строил бы маршруты от
            // звезды, а торговал бы через пустоту. Ровно та же грабля, что в
            // ветке экстренной остановки и у бросившего погоню. Проверяется по
            // расстоянию, а не по флагу: борт, который так и не отошёл от
            // причала, пришвартован по-настоящему и трогать его нельзя.
            if (validStar(game, r.currentStar) &&
                distanceShipToStar(r.ship, game.cluster.stars[size_t(r.currentStar)]) > RESCUE_DOCK_DISTANCE) {
                r.currentStar = -1;
            }
        } else if (r.rescueTarget > beaconIndex) {
            --r.rescueTarget;
        }
    }
    game.distress.erase(game.distress.begin() + beaconIndex);
}

// Свет от маяка дошёл до точки? Единственная формула ожидания во всей механике.
bool distressHeardAt(const DistressBeacon& beacon, double now, double x, double y, double z) {
    const double dx = x - beacon.x, dy = y - beacon.y, dz = z - beacon.z;
    return now >= beacon.raisedTime + std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Кто пришёл на сигнал — спасать или грабить. Пират грабит всегда; борт державы
// — только если с хозяином застрявшего у него настоящая вражда. Порог −35 не
// выдуман здесь: ровно с него военный борт державы переходит от патрулирования
// к нападению (`updateMilitary`), и «достаточно враждебен, чтобы стрелять»
// обязано значить то же самое, что «достаточно враждебен, чтобы обобрать».
bool answersDistressAsLooter(const Game& game, const Agent& comer, const Agent& victim) {
    if (agentIsPiracyThreat(comer)) return true;
    if (!validFaction(game, comer.ship.ownerFaction) || !validFaction(game, victim.ship.ownerFaction)) return false;
    if (comer.ship.ownerFaction == victim.ship.ownerFaction) return false;
    return game.factionRelation(comer.ship.ownerFaction, victim.ship.ownerFaction) < -35;
}

// Спасатель довёл дело до конца: борт на буксир, обоих — в ближайший порт,
// награду — из казны.
void completeRescue(Game& game, int beaconIndex, int rescuerIndex) {
    // ⚠️ Маяк берётся КОПИЕЙ, и снимает его РОВНО ОДИН вызов — тот, что внутри
    // `towStrandedShip`. Первая версия держала ссылку и в конце гасила маяк
    // второй раз, уже по чужому индексу: `towStrandedShip` успевал снять свой,
    // вектор сжимался, и `closeDistress(beaconIndex)` убивал СОСЕДНЮЮ беду
    // вместе с её спасателем. Замер: 42% тактов в мире на 2048 звёзд идут с
    // двумя и более маяками одновременно (в полном мире их до 33), так что
    // «соседа» почти всегда есть кому найтись. Пострадавший тут же поднимал
    // маяк заново — с новой точкой и новым временем, то есть ход света и всё
    // ожидание отсчитывались с нуля, и со стороны это выглядело как «спасатели
    // почему-то не долетают».
    const DistressBeacon beacon = game.distress[size_t(beaconIndex)];
    const int victimIndex = beacon.agent;
    if (victimIndex < 0 || victimIndex >= int(game.agents.size())) { closeDistress(game, beaconIndex); return; }

    const double waited = std::max(0.0, game.time - beacon.raisedTime);
    s_rescueWaitSum += waited;
    if (waited > s_rescueWaitWorst) s_rescueWaitWorst = waited;
    ++s_rescueWaitCount;

    const double hull = hullClassPrice(game.agents[size_t(victimIndex)].ship);
    towStrandedShip(game, victimIndex);              // спасённый — в ближайший порт
    const int port = game.agents[size_t(victimIndex)].currentStar;

    Agent& rescuer = game.agents[size_t(rescuerIndex)];
    // Буксир идёт в порт ВМЕСТЕ с тем, кого тащит: иначе спасатель оставался бы
    // висеть в пустоте с полупустыми баками — то есть спасение порождало бы
    // следующее бедствие.
    if (validStar(game, port)) {
        const ClusterStar& star = game.cluster.stars[size_t(port)];
        rescuer.ship.x = star.x; rescuer.ship.y = star.y; rescuer.ship.z = star.z;
        rescuer.ship.vx = rescuer.ship.vy = rescuer.ship.vz = 0.0;
        rescuer.currentStar = port;
        rescuer.destStar = validStar(game, rescuer.rescueResumeStar) ? rescuer.rescueResumeStar : port;
    }
    rescuer.ship.enRoute = false;
    rescuer.ship.targetStar = -1;
    rescuer.rescueKnows = false;
    rescuer.rescueResumeStar = -1;
    rescuer.lastAction = "answered a distress call";

    // НАГРАДА: полный бак плюс доля цены корпуса, платит клиринговая палата
    // (решение пользователя, §48.8 п.5). ⚠️ Деньги и вещество не появляются из
    // воздуха: топливо доливается физически, а его рыночная стоимость и доля
    // корпуса списываются с казны — обе стороны проводки, как в §47.
    const double fuelBefore = shipFuelMix(rescuer.ship).mass + shipPropellantMix(rescuer.ship).mass;
    shipEmergencyPrime(rescuer.ship, 1.0);
    const double poured = std::max(0.0, shipFuelMix(rescuer.ship).mass + shipPropellantMix(rescuer.ship).mass - fuelBefore);
    double propellantPrice = 1.0, fuelPrice = 1.0;
    routePrices(game, rescuer.ship, port, propellantPrice, fuelPrice);
    const double bill = poured * std::max(propellantPrice, fuelPrice) + hull * RESCUE_HULL_SHARE;
    const double cash = hull * RESCUE_HULL_SHARE;
    if (validFaction(game, game.clearingFaction)) {
        Faction& state = game.factions[size_t(game.clearingFaction)];
        const double paid = std::min(bill, std::max(0.0, state.treasury));
        const double toPocket = std::min(cash, paid);
        state.treasury -= paid;
        rescuer.money += toPocket;
        s_rescueBountyPaid += paid;
        s_rescueBountyCash += toPocket;
    }

    if (rescuer.playerControlled) {
        game.lastEvent = "rescue complete - bounty paid";
        game.pushNews("Rescue complete: " + std::to_string(int(cash)) + " Cr and a full tank", 1);
    }
    if (rescuer.type == "rescue") ++s_rescueState; else ++s_rescueLive;
    // Маяк уже снят внутри `towStrandedShip` — второго `closeDistress` здесь
    // быть не должно (см. ⚠️ в шапке функции).
}

// Мародёр добрался первым. Трюм вычищен, борт брошен дрейфовать дальше —
// спасатели придут своим чередом, маяк остаётся в эфире.
void lootDistress(Game& game, int beaconIndex, int looterIndex) {
    DistressBeacon& beacon = game.distress[size_t(beaconIndex)];
    Agent& victim = game.agents[size_t(beacon.agent)];
    Agent& looter = game.agents[size_t(looterIndex)];

    // Груз ПЕРЕЕЗЖАЕТ, а не испаряется: сколько влезет в чужой трюм, столько и
    // увезут, остальное выброшено за борт. Мародёр не волшебник, и трюм у него
    // не резиновый — тот же учёт, что при обычном разбое.
    double room = std::max(0.0, looter.ship.cargoCapacity - shipCargoMass(looter.ship));
    for (size_t c = 0; c < victim.ship.cargo.size() && room > 0.0; ++c) {
        const int index = elementIndex(victim.ship.cargo[c].element);
        if (index < 0) continue;
        const double unit = std::max(0.001, resourceUnitMassByIndex(index));
        const double move = std::min(victim.ship.cargo[c].amount, room / unit);
        if (move <= 0.01) continue;
        bool merged = false;
        for (size_t k = 0; k < looter.ship.cargo.size(); ++k) {
            if (looter.ship.cargo[k].element != victim.ship.cargo[c].element) continue;
            looter.ship.cargo[k].amount += move;
            merged = true;
            break;
        }
        if (!merged) looter.ship.cargo.emplace_back(victim.ship.cargo[c].element, move);
        room -= move * unit;
    }
    victim.ship.cargo.clear();
    victim.cargoCost = 0.0;
    beacon.looted = true;
    beacon.responder = -1;
    looter.rescueTarget = -1;
    looter.rescueKnows = false;
    looter.ship.enRoute = false;
    looter.ship.targetStar = -1;
    looter.currentStar = -1;      // взял добычу в пустоте, а не в порту
    looter.lastAction = "stripped a distress call";
    ++s_rescueLoots;

    if (victim.playerControlled) {
        game.lastEvent = "boarded while adrift - hold stripped";
        game.pushNews("Raiders answered your beacon first: hold stripped", 2);
    } else if (looter.playerControlled) {
        game.lastEvent = "stripped an adrift hull";
    }
}

// (§50) СКОЛЬКО ИДТИ ДО ВСТРЕЧИ — мерка, по которой выбирается спасатель.
//
// ⚠️ Первая версия брала БЛИЖАЙШЕГО, и замер снял с этого выбора почву:
// ожидание вышло 63.8 года в среднем при ходе света 4.9 — то есть 93% ожидания
// съедал догон, а не расстояние. Ближний тихоход висит на хвосте вечно, дальний
// быстроход обгоняет его за десяток лет. Правильный вопрос — не «кто ближе», а
// «кто раньше будет там», и он раскладывается на два физических слагаемых:
//
//   долёт до точки сигнала            t1 = расстояние / крейсер
//   догон хвоста (борт всё это время уходит)
//                                     t2 = скорость_дрейфа * (t1 + возраст маяка)
//                                          / (крейсер − скорость_дрейфа)
//
// Знаменатель и есть ДОГОНЯЕМОСТЬ (§48.6): не догоняет — бесконечность, и такой
// борт отсеивается сам, без отдельного порога «на 15% быстрее».
double rescueEtaYears(const Game& game, const Agent& cand, const DistressBeacon& beacon, double driftSpeed) {
    // На перехват идут на полном ходу, поэтому мерка — потолок корпуса, а не
    // сегодняшнее экономичное деление.
    const double cruise = std::max(0.02, cand.ship.speed);
    const double closing = cruise - driftSpeed;
    if (closing <= 1e-6) return 1e300;
    const double t1 = distanceShipToPoint(cand.ship, beacon.x, beacon.y, beacon.z) / cruise;
    const double gap = driftSpeed * std::max(0.0, game.time + t1 - beacon.raisedTime);
    return t1 + gap / closing;
}

// Хватит ли борту расходников на весь перехват. Считается той же `legCost`, что
// и обычное плечо: перехват — такой же полёт, просто цель у него движется.
bool rescuerCanReach(const Game& game, const Agent& cand, double path) {
    double propellantPrice = 1.0, fuelPrice = 1.0;
    routePrices(game, cand.ship, cand.currentStar, propellantPrice, fuelPrice);
    const RouteCost need = legCost(cand.ship, path, propellantPrice, fuelPrice);
    if (!need.feasible) return false;
    if (shipFuelMix(cand.ship).mass < need.fuelMass) return false;
    if (!driveUsesFuelAsPropellant(cand.ship.driveIndex) &&
        shipPropellantMix(cand.ship).mass < need.propellantMass) return false;
    return true;
}

// Отправить борт на перехват. Возвращает false, если он не тянет дорогу, — и
// тогда борт остаётся при своих, а маяк ищет другого.
//
// ⚠️ Проверка топлива идёт ДО того, как борту выкрутят крейсер и перенастроят
// двигатель, и отказ ВОЗВРАЩАЕТ прежний режим. Первая версия проверяла после:
// отвергнутый торговец так и оставался на полном ходу и жёг топливо не по тому
// профилю весь остаток своего рейса — и так на каждом опросе.
bool launchRescuer(Game& game, int beaconIndex, int rescuerIndex, double light, double eta) {
    Agent& rescuer = game.agents[size_t(rescuerIndex)];
    const double keepFraction = rescuer.ship.cruiseFraction;
    // На перехват идут на полном ходу: экономичный крейсер (§48.4) — размен
    // «дешевле, но реже», а здесь второй половины размена нет.
    rescuer.ship.cruiseFraction = 1.0;
    shipTuneDrive(rescuer.ship, 1.0, 1.0);
    // Заправка НА ВЕСЬ ПУТЬ, а не на расстояние до точки сигнала: перехват — это
    // долёт плюс догон, и второе слагаемое обычно больше первого.
    const double path = std::max(light, rescuer.ship.speed * eta);
    if (validStar(game, rescuer.currentStar) && !rescuer.ship.enRoute) {
        double propellantPrice = 1.0, fuelPrice = 1.0;
        routePrices(game, rescuer.ship, rescuer.currentStar, propellantPrice, fuelPrice);
        const RouteCost need = legCost(rescuer.ship, path, propellantPrice, fuelPrice);
        buyRouteConsumables(game, rescuer, rescuer.currentStar, need);
    }
    // ⚠️ СПАСАТЕЛЬ, КОТОРЫЙ НЕ ДОЛЕТИТ, НЕ ЛЕТИТ ВОВСЕ. И это ровно
    // противоположно правилу вылета торговца: тому «лететь впритык» разрешено,
    // потому что альтернатива — стоять в порту вечно (§48.8). У спасателя
    // альтернатива другая: не полетит он — полетит кто-то ещё. Замер первой
    // версии, где рискнуть разрешалось: 13 вылетов впритык породили 47 маяков —
    // спасатели сохли на догоне и уходили в дрейф сами, то есть механика
    // спасения ПЛОДИЛА бедствия.
    if (!rescuerCanReach(game, rescuer, path)) {
        rescuer.ship.cruiseFraction = keepFraction;
        shipTuneDrive(rescuer.ship, 1.0, 1.0);
        rescuer.lastAction = "cannot reach the beacon";
        return false;
    }
    rescuer.rescueTarget = beaconIndex;
    rescuer.rescueKnows = false;
    rescuer.rescueResumeStar = rescuer.destStar;
    rescuer.ship.enRoute = true;
    rescuer.ship.targetStar = -3;      // цель — точка сигнала, а не звезда
    rescuer.lastAction = "answering a distress call";
    game.distress[size_t(beaconIndex)].responder = rescuerIndex;
    s_rescueLightSum += light;         // ход света до того, кто откликнулся
    ++s_rescueLightCount;
    return true;
}

// Казённый спасательный катер: сначала свободный из уже построенных, и только
// если такого нет — новый. Возвращает индекс агента или −1.
int dispatchStateCutter(Game& game, DistressBeacon& beacon, double driftSpeed,
                        double& outLight, double& outEta) {
    if (!validFaction(game, game.clearingFaction)) return -1;

    // Свободный катер, до которого дошёл свет, — берётся без всякой стройки.
    int idle = -1;
    double idleEta = 1e299;
    for (size_t a = 0; a < game.agents.size(); ++a) {
        const Agent& cutter = game.agents[a];
        if (cutter.type != "rescue" || cutter.rescueTarget >= 0) continue;
        if (!distressHeardAt(beacon, game.time, cutter.ship.x, cutter.ship.y, cutter.ship.z)) continue;
        const double eta = rescueEtaYears(game, cutter, beacon, driftSpeed);
        if (eta < idleEta) { idleEta = eta; idle = int(a); }
    }
    if (idle >= 0 && idleEta <= TOW_WAIT_YEARS) {
        outEta = idleEta;
        outLight = distanceShipToPoint(game.agents[size_t(idle)].ship, beacon.x, beacon.y, beacon.z);
        return idle;
    }
    // ⚠️ Флаг «казна уже слала» НЕ запирает постройку навсегда. Первая версия
    // запирала — и замер нашёл 12 маяков, у которых катер был построен, ушёл на
    // другую беду, а второго не полагалось: борта висели по 265 лет. Число
    // катеров и без флага упирается в три вещи, каждая из которых настоящая:
    // свободный катер ищется ПЕРВЫМ, есть потолок населения и есть казна.
    // Отсюда флот палаты сам равен числу ОДНОВРЕМЕННЫХ бедствий.

    // Потолок населения — тот же, что у флота держав (§47.11).
    const size_t starCount = std::max<size_t>(1, game.cluster.stars.size());
    const double targetAgents = std::max(48.0,
        double(AGENT_TARGET_FULL) * double(starCount) / double(STAR_COUNT));
    if (double(game.agents.size()) >= targetAgents * FLEET_POPULATION_HEADROOM) return -1;

    Faction& state = game.factions[size_t(game.clearingFaction)];
    if (state.controlledStars.empty()) return -1;

    // Порт приписки: система палаты, до которой сигнал УЖЕ дошёл и от которой
    // ближе всего лететь. Если свет не дошёл ни до одной — катер не выйдет, и
    // это правильно: палата ещё не знает о беде.
    int berth = -1;
    double bestDistance = 1e300;
    for (size_t k = 0; k < state.controlledStars.size(); ++k) {
        const int s = state.controlledStars[k];
        if (!validStar(game, s)) continue;
        const ClusterStar& star = game.cluster.stars[size_t(s)];
        if (!distressHeardAt(beacon, game.time, star.x, star.y, star.z)) continue;
        const double dx = star.x - beacon.x, dy = star.y - beacon.y, dz = star.z - beacon.z;
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < bestDistance) { bestDistance = d; berth = s; }
    }
    if (berth < 0) return -1;

    // Корпус — САМЫЙ БЫСТРЫЙ по карману, а не самый дорогой: у катера одна
    // работа, и это догоняемость. Медленнее дрейфа — не берём вовсе.
    const std::vector<ShipClass>& classes = shipClasses();
    int pick = -1;
    double pickSpeed = driftSpeed;
    for (size_t c = 0; c < classes.size(); ++c) {
        if (classes[c].price <= 0.0 || classes[c].price > state.treasury) continue;
        const double speed = shipClassMaxSpeed(classes[c]);
        if (speed > pickSpeed) { pickSpeed = speed; pick = int(c); }
    }
    if (pick < 0) return -1;

    // ⚠️ ПРОВЕРИТЬ ОЦЕНКУ ВСТРЕЧИ ДО СТРОЙКИ, а не после. Первая версия строила
    // катер и лишь потом узнавала, что тот не успеет: `rescueEtaYears` растёт
    // без границ с возрастом маяка (разрыв = скорость дрейфа × прожитые годы),
    // поэтому у старой беды НИ ОДИН корпус в игре не проходит порог. Катер
    // тут же освобождался — и через четверть года строился следующий.
    // Замер: 77 катеров на ЧЕТЫРЕ маяка за тридцать лет (4 в год, ровно по
    // одному на опрос), 308 млн Cr из казны, от причала отошёл ОДИН, спасений
    // ноль; в полном мире — 275 катеров на 1280 бортов, то есть 21% населения
    // мира стало балластом и заперло потолок, за которым державы перестали
    // строить флот. Оценка считается по корпусу-кандидату ДО покупки: не
    // успеет — не строим, и беда честно уходит на последний рубеж.
    const ClusterStar& star = game.cluster.stars[size_t(berth)];
    {
        Ship probe("probe", star.x, star.y, star.z, 0.12, game.clearingFaction);
        shipApplyClass(probe, classes[size_t(pick)]);
        Agent trial("rescue", probe);
        trial.currentStar = berth;
        const double eta = rescueEtaYears(game, trial, beacon, driftSpeed);
        if (eta > TOW_WAIT_YEARS) return -1;
        if (!rescuerCanReach(game, trial, std::max(bestDistance, probe.speed * eta))) return -1;
    }
    state.treasury -= classes[size_t(pick)].price;
    Ship hull("Cutter", star.x, star.y, star.z, 0.12, game.clearingFaction);
    shipApplyClass(hull, classes[size_t(pick)]);
    shipAutofit(hull);
    // ⚠️ Имя борта — это и есть имя КЛАССА (по нему считается цена корпуса и
    // ищется текущий корпус при покупке), поэтому переименовывать катер нельзя.
    hull.name = classes[size_t(pick)].name;
    Agent cutter("rescue", hull);
    cutter.currentStar = berth;
    cutter.homeStar = berth;
    cutter.destStar = berth;
    cutter.money = 0.0;
    cutter.lastAction = "commissioned as a cutter";
    game.agents.push_back(cutter);
    beacon.stateSent = true;
    outLight = bestDistance;
    outEta = rescueEtaYears(game, game.agents.back(), beacon, driftSpeed);
    return int(game.agents.size()) - 1;
}

// (§50) ДИСПЕТЧЕР: кто услышал маяк и кто пойдёт на перехват.
void Game::updateDistress(double dt) {
    distressTimer += dt;
    if (distress.empty()) { distressTimer = 0.0; return; }
    if (distressTimer < DISTRESS_POLL_YEARS) return;
    distressTimer = 0.0;

    for (size_t b = 0; b < distress.size(); ) {
        DistressBeacon& beacon = distress[b];
        if (beacon.agent < 0 || beacon.agent >= int(agents.size())) { closeDistress(*this, int(b)); continue; }
        const Agent& victim = agents[size_t(beacon.agent)];
        // Борт ожил (вытащили, долили, встал в порту) — маяк снят.
        //
        // ⚠️ Признак беды — МЁРТВЫЙ ДВИГАТЕЛЬ, а не «нет порта под ногами».
        // Первая версия спрашивала `validStar(currentStar)` и снимала маяк на
        // первом же опросе: в полёте `currentStar` продолжает указывать на порт
        // ВЫЛЕТА и становится −1 только после экстренной остановки. Замер это
        // и показал — 3207 маяков за 300 лет (один на 0.1 года) при нуле
        // спасений: сигнал гас раньше, чем свет успевал до кого-нибудь дойти.
        if (!victim.ship.enRoute || shipCurrentAcceleration(victim.ship) > 0.0) {
            closeDistress(*this, int(b));
            continue;
        }
        const double driftSpeed = std::sqrt(victim.ship.vx * victim.ship.vx +
                                            victim.ship.vy * victim.ship.vy +
                                            victim.ship.vz * victim.ship.vz);
        // Взявшийся спасатель ПЕРЕПРОВЕРЯЕТСЯ, а не держит маяк вечно.
        //
        // ⚠️ Замер первой версии: 32 маяка в эфире, старейшему 265 лет, и у
        // каждого спасатель ЕСТЬ. Разбор показал, чем они заняты: борт с
        // потолком 0.191c догонял дрейф 0.184c — сближение 0.007c, то есть
        // тысяча лет на десяток световых. Назначение было пожизненным, и один
        // безнадёжный кандидат навсегда закрывал маяк для всех остальных.
        // Теперь оценка встречи считается заново каждый опрос, и спасатель,
        // который проигрывает даже последнему рубежу, освобождает место.
        if (beacon.responder >= 0 && beacon.responder < int(agents.size()) &&
            agents[size_t(beacon.responder)].rescueTarget == int(b)) {
            if (rescueEtaYears(*this, agents[size_t(beacon.responder)], beacon, driftSpeed) <= TOW_WAIT_YEARS) {
                ++b;
                continue;
            }
            Agent& quitter = agents[size_t(beacon.responder)];
            quitter.rescueTarget = -1;
            quitter.rescueKnows = false;
            quitter.ship.enRoute = false;
            quitter.ship.targetStar = -1;
            // ⚠️ Борт бросил погоню ПОСРЕДИ ПУСТОТЫ и ни к одной системе не
            // пришвартован. Без этого `currentStar` продолжал бы указывать на
            // порт вылета — та же грабля, что в ветке экстренной остановки:
            // маршруты считались бы от звезды за световые годы, а торговать
            // можно было бы прямо из пустоты.
            quitter.currentStar = -1;
            quitter.lastAction = "lost the chase";
        }
        beacon.responder = -1;
        int best = -1;
        double bestEta = 1e299;
        double bestLight = 0.0;
        for (size_t a = 0; a < agents.size(); ++a) {
            const Agent& cand = agents[a];
            if (int(a) == beacon.agent || cand.rescueTarget >= 0) continue;
            // Игрок сам решает, лететь ему на сигнал или нет: отнимать у него
            // руль ради чужой беды — та же подстава, что молча выпустить его
            // в дрейф (§48.9).
            if (cand.playerControlled) continue;
            // ⚠️ ХОД СВЕТА ПРОВЕРЯЕТСЯ ПЕРВЫМ, и это не косметика, а такт.
            // Отсев по свету — три вычитания и корень, он отбрасывает почти всех
            // (скопление 100 ly, маяку обычно считанные годы); а
            // `shipCurrentAcceleration` обходит оба списка расходников на борту.
            // Раньше порядок был обратный, и полное скопление считало год за
            // 160.4 мс при бюджете 160.
            if (!distressHeardAt(beacon, time, cand.ship.x, cand.ship.y, cand.ship.z)) continue;
            // Мёртвому двигателю спасать некого — он сам скоро позовёт.
            if (shipCurrentAcceleration(cand.ship) <= 0.0) continue;
            // Обобранному маяку мародёр больше не интересен — брать нечего.
            if (beacon.looted && answersDistressAsLooter(*this, cand, victim)) continue;
            // ⚠️ Порог годности ВЫВЕДЕН, а не назначен: кандидат обязан успеть
            // раньше последнего рубежа. Спасатель, который не обгоняет казённый
            // буксир по таймеру, — театр: он занимает маяк и ничего не решает.
            const double eta = rescueEtaYears(*this, cand, beacon, driftSpeed);
            if (eta > TOW_WAIT_YEARS) continue;
            if (eta < bestEta) {
                bestEta = eta;
                bestLight = distanceShipToPoint(cand.ship, beacon.x, beacon.y, beacon.z);
                best = int(a);
            }
        }

        // Живой кандидат снаряжается ПЕРВЫМ, и только если он не тянет дорогу —
        // зовётся казна. ⚠️ Раньше отвергнутому по топливу живому борту казна на
        // смену не приходила вовсе: маяк оставался без спасателя, хотя палата
        // даже не пробовала.
        bool dispatched = best >= 0 && launchRescuer(*this, int(b), best, bestLight, bestEta);
        if (!dispatched) {
            // (§50) Живых нет — идёт КАЗНА (решение пользователя, §48.8).
            // Клиринговая палата держит спасательные катера: один корпус, одна
            // задача, самый быстрый, какой она может себе позволить, — потому
            // что вся работа катера и есть догоняемость.
            const int cutter = dispatchStateCutter(*this, distress[b], driftSpeed, bestLight, bestEta);
            if (cutter >= 0) dispatched = launchRescuer(*this, int(b), cutter, bestLight, bestEta);
        }
        (void)dispatched;
        ++b;
    }
}

// (§50) Маяки, чей свет ДОШЁЛ до борта. Игрок слышит ровно то же и по тому же
// правилу, что и ИИ, — никаких «оповещений для удобства»: сигнал, до которого
// свет ещё летит, для него не существует.
std::vector<int> Game::audibleDistress(int agentIndex) const {
    std::vector<int> heard;
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return heard;
    const Ship& ship = agents[size_t(agentIndex)].ship;
    for (size_t b = 0; b < distress.size(); ++b) {
        if (distress[b].agent == agentIndex) continue;
        if (!distressHeardAt(distress[b], time, ship.x, ship.y, ship.z)) continue;
        heard.push_back(int(b));
    }
    // Свежие первыми: старый маяк, скорее всего, уже кем-то взят.
    for (size_t i = 0; i + 1 < heard.size(); ++i) {
        for (size_t j = i + 1; j < heard.size(); ++j) {
            if (distress[size_t(heard[j])].raisedTime > distress[size_t(heard[i])].raisedTime) {
                const int t = heard[i]; heard[i] = heard[j]; heard[j] = t;
            }
        }
    }
    return heard;
}

// Игрок берётся за спасение. Отказы называются вслух: молчаливый отказ на
// кнопке — та же подстава, что молча выпустить борт в дрейф (§48.9).
bool Game::commandAgentToDistress(int agentIndex, int beaconIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    if (beaconIndex < 0 || beaconIndex >= int(distress.size())) return false;
    Agent& agent = agents[size_t(agentIndex)];
    const DistressBeacon& beacon = distress[size_t(beaconIndex)];
    if (beacon.agent == agentIndex) return false;
    if (agent.ship.enRoute) {
        if (agent.playerControlled) lastEvent = "rescue blocked: ship already en route";
        return false;
    }
    if (!distressHeardAt(beacon, time, agent.ship.x, agent.ship.y, agent.ship.z)) {
        if (agent.playerControlled) lastEvent = "rescue blocked: signal has not arrived yet";
        return false;
    }
    if (shipCurrentAcceleration(agent.ship) <= 0.0) {
        if (agent.playerControlled) lastEvent = "rescue blocked: no thrust";
        return false;
    }
    // ДОГОНЯЕМОСТЬ (§48.6). Тот же порог, что у диспетчера: браться за перехват
    // на борту, который дрейфующего не догонит, — это обещание, которого игра
    // не сдержит.
    const double driftSpeed = beacon.agent >= 0 && beacon.agent < int(agents.size())
        ? std::sqrt(agents[size_t(beacon.agent)].ship.vx * agents[size_t(beacon.agent)].ship.vx +
                    agents[size_t(beacon.agent)].ship.vy * agents[size_t(beacon.agent)].ship.vy +
                    agents[size_t(beacon.agent)].ship.vz * agents[size_t(beacon.agent)].ship.vz)
        : 0.0;
    // ⚠️ Игроку меряем по ФАКТИЧЕСКОМУ крейсеру, а не по потолку корпуса: ручку
    // ему не крутим (§48.4), и если он идёт на экономичном делении, догонять
    // придётся на нём же. Поднять тягу — его решение, и отказ прямо на это
    // указывает.
    if (shipCruiseSpeed(agent.ship) <= driftSpeed * 1.15) {
        if (agent.playerControlled) lastEvent = "rescue blocked: cruise too slow to overhaul the drift";
        return false;
    }
    // ⚠️ Прежнего откликнувшегося надо ОТПУСТИТЬ, а не просто затереть поле
    // маяка: иначе он продолжал бы лететь к той же беде и при этом считался бы
    // занятым для всех остальных маяков (`cand.rescueTarget >= 0` в диспетчере)
    // — один игрок молча вычёркивал чужой борт из спасателей навсегда.
    DistressBeacon& target = distress[size_t(beaconIndex)];
    if (target.responder >= 0 && target.responder < int(agents.size()) &&
        agents[size_t(target.responder)].rescueTarget == beaconIndex) {
        Agent& previous = agents[size_t(target.responder)];
        previous.rescueTarget = -1;
        previous.rescueKnows = false;
        previous.ship.enRoute = false;
        previous.ship.targetStar = -1;
        previous.lastAction = "stood down from the call";
    }
    agent.rescueTarget = beaconIndex;
    agent.rescueKnows = false;
    agent.rescueResumeStar = agent.destStar;
    agent.ship.enRoute = true;
    agent.ship.targetStar = -3;
    agent.lastAction = "answering a distress call";
    target.responder = agentIndex;
    if (agent.playerControlled) {
        const double light = distanceShipToPoint(agent.ship, beacon.x, beacon.y, beacon.z);
        lastEvent = "rescue set: beacon " + std::to_string(int(light + 0.5)) + " ly out";
    }
    return true;
}

void Game::updateAgents(double dt) {
    for (size_t i = 0; i < agents.size(); ++i) {
        Agent& agent = agents[i];
        // (§50) СПАСАТЕЛЬНЫЙ РЕЙС идёт первым: борт бросил дело, и торговый
        // планировщик не должен перехватывать у него руль на полпути.
        if (agent.rescueTarget >= 0) {
            if (agent.rescueTarget >= int(distress.size())) {
                agent.rescueTarget = -1;
                agent.rescueKnows = false;
                agent.ship.enRoute = false;
                agent.ship.targetStar = -1;
            } else {
                const int beaconIndex = agent.rescueTarget;
                const DistressBeacon& beacon = distress[size_t(beaconIndex)];
                // ⚠️ X STOP ОТМЕНЯЕТ И СПАСАТЕЛЬНЫЙ РЕЙС. Этот блок стоит выше
                // разбора `targetStar`, поэтому без явной проверки `abortAgentRoute`
                // ставил −2, а борт продолжал РАЗГОНЯТЬСЯ на перехват — и всё
                // это время интерфейс честно рисовал ему «X BRAKING». Взявшись
                // за чужую беду, игрок терял руль до конца рейса.
                if (agent.ship.targetStar == -2) {
                    agent.rescueTarget = -1;
                    agent.rescueKnows = false;
                    agent.lastAction = "broke off the call";
                    if (beaconIndex < int(distress.size())) distress[size_t(beaconIndex)].responder = -1;
                    // Дальше — обычная ветка экстренного торможения, ниже по циклу.
                } else if (shipCurrentAcceleration(agent.ship) <= 0.0) {
                    // ⚠️ Спасатель тоже смертен. Если по дороге кончился расходник —
                    // он бросает дело и сам поднимает маяк; иначе спасение плодило бы
                    // спасения, а застрявший ждал бы того, кто уже не долетит.
                    agent.driftYears += dt;
                    agent.rescueTarget = -1;
                    agent.rescueKnows = false;
                    agent.ship.targetStar = -2;   // дальше — обычный дрейф
                    if (beaconIndex < int(distress.size())) distress[size_t(beaconIndex)].responder = -1;
                    raiseDistress(*this, int(i));
                    continue;
                } else {
                    bool met = false;
                    if (!agent.rescueKnows) {
                        // Пока не долетел до ТОЧКИ СИГНАЛА, настоящего положения он
                        // не знает: свет принёс только «где было и с какой скоростью».
                        double px = beacon.x, py = beacon.y, pz = beacon.z;
                        if (chaseShipToward(agent.ship, px, py, pz, 0.0, 0.0, 0.0, dt, false)) {
                            agent.rescueKnows = true;
                            if (agent.playerControlled) {
                                beaconPredictedPosition(beacon, time, px, py, pz);
                                lastEvent = "beacon reached - hull located ahead";
                            }
                        }
                    } else if (beacon.agent >= 0 && beacon.agent < int(agents.size())) {
                        const Ship& hull = agents[size_t(beacon.agent)].ship;
                        met = chaseShipToward(agent.ship, hull.x, hull.y, hull.z,
                                              hull.vx, hull.vy, hull.vz, dt, true);
                    }
                    if (met) {
                        if (answersDistressAsLooter(*this, agent, agents[size_t(beacon.agent)])) {
                            lootDistress(*this, beaconIndex, int(i));
                        } else {
                            completeRescue(*this, beaconIndex, int(i));
                        }
                    }
                    continue;
                }
            }
        }
        if (agent.ship.enRoute) {
            if (agent.ship.targetStar == -2) {
                const double speed = std::sqrt(agent.ship.vx * agent.ship.vx + agent.ship.vy * agent.ship.vy + agent.ship.vz * agent.ship.vz);
                if (speed < 0.0001) {
                    agent.ship.vx = agent.ship.vy = agent.ship.vz = 0.0;
                    agent.ship.enRoute = false;
                    agent.ship.targetStar = -1;
                    // Корабль встал МЕЖДУ системами и больше ни к одной не
                    // пристыкован. Без этого currentStar продолжал указывать на
                    // порт вылета: маршруты считались от звезды, а не от
                    // корабля (9.6 ly вместо реальных 3.6), и с рынком за
                    // световые годы можно было торговать прямо из пустоты.
                    // Все ветки «не в системе» уже есть и включаются отсюда.
                    agent.currentStar = -1;
                    agent.lastAction = "stopped in deep space";
                } else {
                    const double accel = shipCurrentAcceleration(agent.ship);
                    const double deltaV = accel * dt;
                    const double brake = consumeAndStoreAsh(agent.ship, std::min(deltaV, speed));
                    if (brake > 0.0) {
                        agent.ship.vx -= agent.ship.vx / speed * brake;
                        agent.ship.vy -= agent.ship.vy / speed * brake;
                        agent.ship.vz -= agent.ship.vz / speed * brake;
                        agent.missionCooldown = 0.0;   // тормозим — счётчик дрейфа сбрасываем
                    } else {
                        // (§38) Тормозить НЕЧЕМ — беспомощный дрейф.
                        // (§50) Первым делом поднимается МАЯК: сигнал уходит со
                        // светом, и с этой секунды ожидание физическое. Буксир по
                        // TOW_WAIT_YEARS остался последним рубежом на случай,
                        // когда никто не долетел (см. game.h).
                        //
                        // ⚠️ СЧЁТ ИДЁТ ПО `driftYears`, а не по `missionCooldown`.
                        // Здесь стоял `missionCooldown` — общий кулдаун, который
                        // ветка `-2` НЕ ПЕРЕХВАТЫВАЕТ (нет `continue`), так что
                        // ниже по циклу отрабатывал `updateTrader` и обнулял его.
                        // Последний рубеж не срабатывал НИКОГДА: замер §50 нашёл
                        // 12 бортов с `driftYears` до 265 лет при `cooldown` 0.0.
                        // Отдельный счётчик дрейфа для того и заведён (§48.8).
                        agent.driftYears += dt;
                        if (agent.driftYears >= TOW_WAIT_YEARS) {
                            ++s_rescueLastResort;
                            towStrandedShip(*this, int(i));
                        }
                    }
                    agent.ship.x += agent.ship.vx * dt;
                    agent.ship.y += agent.ship.vy * dt;
                    agent.ship.z += agent.ship.vz * dt;
                    // ⚠️ МАЯК ПОДНИМАЕТСЯ ПОСЛЕ ХОДА, а не до него, и это не
                    // придирка. `raiseDistress` записывает положение и метит его
                    // сегодняшним временем; `Game::update` увеличивает время в
                    // самом начале такта, а корабль двигается вот здесь, в
                    // конце. Подъём до хода записывал вчерашнюю точку под
                    // сегодняшней датой, и модель маяка промахивалась ровно на
                    // один тик пути — замер поймал 0.0103 ly при v*dt = 0.0103.
                    // В ветке маршрута такой беды нет: там `moveShipToward`
                    // двигает корабль до проверки.
                    if (shipCurrentAcceleration(agent.ship) <= 0.0) raiseDistress(*this, int(i));
                }
            } else if (agent.ship.targetStar >= 0 && agent.ship.targetStar < int(cluster.stars.size())) {
                const bool arrived = moveShipToward(agent.ship, cluster.stars[agent.ship.targetStar], dt);
                // (§48.8) БЕДА СЛУЧАЕТСЯ И НА МАРШРУТЕ, а не только после X STOP.
                //
                // Двигатель без тяги (кончился любой из двух расходников) — это
                // и есть беспомощный дрейф: цель впереди, но затормозить у неё
                // нечем, и корабль пройдёт мимо. Считаем годы и через
                // TOW_WAIT_YEARS зовём помощь — тем же путём, что и застрявший
                // после экстренной остановки. До §48 сюда было не попасть:
                // прощение при пролёте выдавало таким бортам бесплатную
                // стыковку, а маршрутная оценка вдобавок занижала расход вдвое.
                if (!arrived) {
                    if (shipCurrentAcceleration(agent.ship) <= 0.0) {
                        // (§50) Маяк поднимается СРАЗУ, а не через срок ожидания:
                        // борт узнаёт о мёртвом двигателе первым тиком, и сигнал
                        // уходит тогда же. Ждать после этого приходится не
                        // таймер, а свет и спасателя.
                        raiseDistress(*this, int(i));
                        agent.driftYears += dt;
                        if (agent.driftYears >= TOW_WAIT_YEARS) {
                            ++s_rescueLastResort;
                            towStrandedShip(*this, int(i));
                            agent.driftYears = 0.0;
                            continue;
                        }
                    } else {
                        agent.driftYears = 0.0;
                    }
                }
                if (arrived) {
                    agent.driftYears = 0.0;
                    agent.currentStar = agent.ship.targetStar;
                    agent.ship.targetStar = -1;
                    agent.ship.enRoute = false;

                    // Здесь стоял случайный въездной побор (1% шанс, сумма rand(1000)+проценты):
                    // наказание без решения игрока и без связи с чем-либо. Его место заняла
                    // лицензионная квота — та же тема «налог», но с целью, счётчиком и выбором.

                    observeLocalThreatsForFaction(agent.ship.ownerFaction, agent.currentStar);
                    queueOwnerSignal(agent.ship.ownerFaction, agent.currentStar, agent.currentStar);
                    queueMarketSignal(agent.ship.ownerFaction, agent.currentStar, agent.currentStar);
                    agentCompleteContracts(int(i));
                    if (agent.destStar != agent.currentStar && validStar(*this, agent.destStar)) {
                        if (startJourney(*this, agent, agent.destStar)) continue;
                    }
                } else {
                    continue;
                }
            } else {
                agent.ship.targetStar = -1;
                agent.ship.enRoute = false;
            }
        }

        // Руль игрока не отнимаем НИКОГДА — даже у борта с поднятым флагом.
        if (int(i) == playerAgent) continue;
        if (agent.playerControlled) {
            if (agent.autoTrade) updateFleetTrader(*this, int(i), agent, dt);
            continue;
        }

        if (agent.type == "trader" || agent.type == "adventurer") {
            updateTrader(*this, int(i), agent, dt);
        } else if (agent.type == "military") {
            updateMilitary(*this, agent, dt);
        } else if (agent.type == "colonist") {
            updateColonist(*this, agent, dt);
        } else if (agent.type == "scout") {
            updateScout(*this, agent, dt);
        } else if (agent.type == "pirate") {
            updatePirate(*this, int(i), agent, dt);
        }
    }
}

// «Смерть» макро-агента: корабль деградирует в спас-капсулу (класс 0), груз сброшен.
// Кредиты НЕ трогаем (§5.13.14 — «credits immaterial»). Чистая арифметика: без rng, без
// перезаписи фракции/индекса — агент никогда не стирается, индекс стабилен. Общий помощник
// для robAgent (макро-бой) и write-back из локального полёта (сбитое борт-зеркало).
void downgradeAgentToEscapePod(Agent& a) {
    const std::vector<ShipClass>& classes = shipClasses();
    const ShipClass& pod = classes[0];
    a.ship.name = "Escape Pod";
    a.ship.cargo.clear();
    // Модули гибнут вместе с корпусом — как и груз. Раньше список `modules`
    // переживал даунгрейд, хотя `shipApplyClass` уже обнулил поля корабля
    // табличными значениями капсулы: бонусов нет, а записи есть. Дальше
    // `unequipModule` вычитал бонусы, которые никогда не применялись
    // (Aegis Bulwark уносил maxHullHP капсулы в −565), и возвращал модуль в
    // трюм — то есть из уничтоженного корабля можно было выгрести апгрейды.
    a.ship.modules.clear();
    shipApplyClass(a.ship, pod);
    a.cargoCost = 0.0;
    // (§31.4) Экзотика и переоснастка гибнут вместе с корпусом.
    //
    // Трюм сбрасывается, а ячейка с конденсатом на полтораста миллионов —
    // нет? Ячейка и наваренная броня — это сваренный металл на КОНКРЕТНОМ
    // корпусе, и от корпуса не осталось ничего. Без этого гибель выходила
    // дешевле стоянки, а спасательная капсула получала броню линкора.
    for (int k = 0; k < EX_COUNT; ++k) a.ship.exotic[k] = 0.0;
    a.ship.containment = 0.0;
    a.ship.containmentLevel = 0;
    a.ship.platingLayers = 0;
}

bool Game::robAgent(int attackerIndex, int victimIndex) {
    if (attackerIndex < 0 || attackerIndex >= int(agents.size())) return false;
    if (victimIndex < 0 || victimIndex >= int(agents.size()) || attackerIndex == victimIndex) return false;
    Agent& attacker = agents[attackerIndex];
    Agent& victim = agents[victimIndex];
    
    if (attacker.ship.enRoute || victim.ship.enRoute || attacker.currentStar != victim.currentStar) return false;

    // Fleeing check
    if (victim.ship.acceleration > attacker.ship.acceleration * 1.2) {
        if (double(randomer(rng, 100)) / 100.0 < 0.6) {
            attacker.lastAction = "failed to catch " + victim.type;
            victim.lastAction = "fled from " + attacker.type;
            
            // Still lowers relations
            if (validFaction(*this, victim.ship.ownerFaction) && validFaction(*this, attacker.ship.ownerFaction)) {
                adjustFactionRelation(victim.ship.ownerFaction, attacker.ship.ownerFaction, -10);
            }
            return false;
        }
    }
    
    // Determine success based on weapons, speed, and armor
    auto calcPower = [](const Ship& atk, const Ship& def) {
        const double defManeuver = def.acceleration * 100.0;
        const double heavyHitChance = std::max(0.1, 1.0 - (defManeuver / 50.0));
        const double damage = (atk.heavyWeapons * heavyHitChance) + atk.lightWeapons;
        const double effectiveDmg = std::max(0.0, damage - def.armor);
        return effectiveDmg + atk.utility;
    };
    
    const double attackPower = calcPower(attacker.ship, victim.ship);
    const double victimPower = calcPower(victim.ship, attacker.ship);
    const double roll = double(randomer(rng, 100)) / 100.0;
    
    // Robbing ruins relations with the victim's faction
    if (validFaction(*this, victim.ship.ownerFaction)) {
        if (validFaction(*this, attacker.ship.ownerFaction)) {
            adjustFactionRelation(victim.ship.ownerFaction, attacker.ship.ownerFaction, -25);
            adjustFactionRelation(attacker.ship.ownerFaction, victim.ship.ownerFaction, -25);
        }
    }
    
    const double successThreshold = (attackPower / (attackPower + victimPower + 0.1));
    
    // Function to transfer cargo
    auto lootCargo = [&](Agent& winner, Agent& loser) {
        for (const Resource& res : loser.ship.cargo) {
            bool found = false;
            for (Resource& ares : winner.ship.cargo) {
                if (ares.element == res.element) {
                    ares.amount += res.amount;
                    found = true;
                    break;
                }
            }
            if (!found) {
                winner.ship.cargo.emplace_back(res.element, res.amount);
            }
        }
        loser.ship.cargo.clear();
        loser.cargoCost = 0.0;
    };
    
    // Resolve combat
    if (roll < successThreshold + 0.15) {
        // Attacker wins
        if (attackPower > victimPower * 1.5 && roll < successThreshold) {
            // Victim Destroyed
            attacker.lastAction = "destroyed " + victim.type;
            victim.lastAction = "destroyed by " + attacker.type;
            lootCargo(attacker, victim);
            
            // Credits are immaterial and cannot be stolen
            downgradeAgentToEscapePod(victim);
            rebakeBakedBonuses(victimIndex);
        } else {
            // Victim surrenders cargo
            attacker.lastAction = "robbed " + victim.type;
            victim.lastAction = "robbed by " + attacker.type;
            lootCargo(attacker, victim);
        }
        return true;
    } else {
        // Defender wins
        if (victimPower > attackPower * 1.5 && roll > successThreshold + 0.3) {
            // Attacker Destroyed
            victim.lastAction = "destroyed pirate " + attacker.type;
            attacker.lastAction = "destroyed by " + victim.type;
            lootCargo(victim, attacker);
            
            // Credits are immaterial and cannot be stolen
            downgradeAgentToEscapePod(attacker);
            rebakeBakedBonuses(attackerIndex);
        } else {
            // Attacker repelled
            attacker.lastAction = "repelled by " + victim.type;
            victim.lastAction = "repelled pirate " + attacker.type;
        }
        return false;
    }
}

double Game::robOdds(int attackerIndex, int victimIndex) const {
    if (attackerIndex < 0 || attackerIndex >= int(agents.size())) return 0.0;
    if (victimIndex < 0 || victimIndex >= int(agents.size())) return 0.0;
    const Agent& attacker = agents[size_t(attackerIndex)];
    const Agent& victim = agents[size_t(victimIndex)];
    // Тот же расклад сил, что и в `robAgent`, только без броска: доля исходов,
    // в которых нападающий не проигрывает.
    const double attackPower = combatPower(*this, attacker);
    const double victimPower = combatPower(*this, victim);
    const double ratio = attackPower / std::max(0.1, attackPower + victimPower);
    return std::max(0.0, std::min(1.0, ratio * 0.5 + 0.15));
}

bool Game::buyShip(int agentIndex, int starIndex, int classId) {
    if (agentIndex < 0 || agentIndex >= int(agents.size()) || !validStar(*this, starIndex)) return false;
    Agent& agent = agents[agentIndex];
    if (agent.currentStar != starIndex || agent.ship.enRoute) return false;
    // Здесь стояло `colonies[starIndex]` — вектор колоний индексировался номером
    // ЗВЕЗДЫ. Колоний единицы, звёзд тысячи, так что это было чтение за границей
    // вектора (UB на объекте со string/vector внутри), а «проверка верфи» просто
    // сравнивала мусор. Гейт по верфи живёт в модулях (`minShipyard`), корпус же
    // продаётся везде — как оно фактически и работало.
    const auto& classes = shipClasses();
    if (classId < 0 || classId >= int(classes.size())) return false;
    const ShipClass& sc = classes[classId];

    double currentHullPrice = 0.0;
    for (const auto& c : classes) {
        if (c.name == agent.ship.name) {
            currentHullPrice = c.price;
            break;
        }
    }
    double upgradePrice = std::max(0.0, sc.price - currentHullPrice);
    
    if (agent.money < upgradePrice) return false;
    
    agent.money -= upgradePrice;
    agent.ship.name = sc.name;
    const std::vector<int> installed = agent.ship.modules;
    shipApplyClass(agent.ship, sc);
    // Бонусы модулей ЗАПЕЧЕНЫ в поля корпуса (правило CHROMOCORE), а новый
    // корпус переписывает эти поля табличными значениями. Раньше список
    // `modules` переживал покупку, а бонусы — нет: игрок покупал корпус выше
    // ступенью и молча терял всю навеску (трюм, броню, корпус, сенсоры),
    // причём слоты оставались занятыми. Запекаем заново тем же путём, что и
    // `equipModule`, — один закон на установку и на пересадку.
    const std::vector<ModuleDef>& moduleTable = moduleDefs();
    for (size_t i = 0; i < installed.size(); ++i) {
        const int defIndex = installed[i];
        if (defIndex < 0 || defIndex >= int(moduleTable.size())) continue;
        applyModuleToShip(agent.ship, moduleTable[defIndex]);
    }
    // Хромокоры — такой же ЗАПЕЧЁННЫЙ бонус, как модули, и теряются они по той
    // же причине: `shipApplyClass` переписал поля табличными значениями. Три
    // ветки прокачки из семи не хранятся больше нигде, поэтому без этой строки
    // покупка корпуса молча стирала всё, что игрок в них вложил.
    rebakeBakedBonuses(agentIndex);
    agent.ship.hullHP = agent.ship.maxHullHP;
    // Состав и ёмкости изменились — рабочая точка двигателя подбирается заново.
    shipTuneDrive(agent.ship, 1.0, 1.0);
    agent.lastAction = "bought " + sc.name;
    return true;
}

bool Game::buyAdditionalShip(int agentIndex, int starIndex, int classId) {
    if (agentIndex < 0 || agentIndex >= int(agents.size()) || !validStar(*this, starIndex)) return false;
    if (agents[agentIndex].currentStar != starIndex || agents[agentIndex].ship.enRoute) return false;
    // См. buyShip выше: та же ошибка индексации, тот же вывод.
    const auto& classes = shipClasses();
    if (classId < 0 || classId >= int(classes.size())) return false;
    const ShipClass& sc = classes[classId];

    // Второй борт стоил `price + 1000000` — заградительная константа, которая просто
    // отключала механику. Теперь ограничение содержательное: КАЖДЫЙ борт летает по
    // отдельной лицензии, и купить её надо заранее на бирже (§10.4). Расширение
    // флота = осознанно поднятая себе квота, а не стена из миллиона кредитов.
    if (agents[agentIndex].playerControlled && playerFreeLicences() <= 0) {
        lastEvent = "no free licence - buy one at the brokerage (E)";
        return false;
    }

    const double totalPrice = sc.price;
    if (agents[agentIndex].money < totalPrice) return false;

    agents[agentIndex].money -= totalPrice;
    
    // Борт рождается В ТОЙ СИСТЕМЕ, где его купили. Раньше он ставился в
    // (0,0,0) — центр скопления, — а `currentStar` указывал на верфь: два
    // разных места. Перемещение привязано к КООРДИНАТАМ (§12.7), поэтому
    // окно считало маршрут от звезды, а корабль потом летел от центра
    // скопления — расхождение до сотни световых лет.
    const ClusterStar& berth = cluster.stars[starIndex];
    Ship newShip(sc.name, berth.x, berth.y, berth.z, 0, agents[agentIndex].ship.ownerFaction);
    shipApplyClass(newShip, sc);
    // Второй борт — тоже борт КАПИТАНА, и его экипаж пользуется теми же
    // моделями. Иначе флот из двух кораблей вёл бы себя как два разных игрока.
    //
    // ⚠️ (§51) Условие было `agentIndex == playerAgent`, то есть корпус,
    // купленный НЕ с флагмана (а именно так и покупают третий борт — стоя на
    // втором), рождался вообще без хромоядер. Спрашивать надо про
    // собственность, а не про то, за чьим штурвалом сидит игрок в эту секунду:
    // ровно так же исправлялось `buyShip` (см. `rebakeBakedBonuses`).
    if (agents[agentIndex].playerControlled) {
        shipApplyChromocoreFactors(newShip, tech.materials, tech.tactics, tech.kinematics);
        newShip.hullHP = newShip.maxHullHP;
    }

    Agent newAgent(agents[agentIndex].type, newShip);
    newAgent.playerControlled = agents[agentIndex].playerControlled;
    newAgent.currentStar = starIndex;
    newAgent.homeStar = starIndex;
    newAgent.destStar = starIndex;
    newAgent.money = 0.0;
    newAgent.lastAction = "bought " + sc.name;

    int newAgentIndex = int(agents.size());
    agents.push_back(newAgent);
    registerFactionAgent(*this, newAgentIndex);
    // Управление НЕ перебрасывается на новый борт. Раньше перебрасывалось, и
    // игрок сразу после покупки оказывался на пустом корабле без единого
    // кредита (деньги остаются у капитана, который платил), причём без всякого
    // предупреждения. Переключение — отдельное осознанное действие `W SWITCH`,
    // а деньги переводятся `playerTransferCredits`, пока оба борта стоят рядом.
    if (agentIndex == playerAgent) lastEvent = "new hull docked here - W to switch, HOLD to fund it";

    return true;
}

bool Game::commandAgentToStar(int agentIndex, int starIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size()) || !validStar(*this, starIndex)) return false;
    Agent& agent = agents[agentIndex];
    if (shipCargoMass(agent.ship) > agent.ship.cargoCapacity + 0.01) {
        agent.lastAction = "overweight";
        if (agent.playerControlled) lastEvent = "route blocked: overweight";
        return false;
    }
    if (agent.currentStar == starIndex && !agent.ship.enRoute) return false;
    if (agent.ship.enRoute) {
        if (agent.playerControlled) lastEvent = "route blocked: ship already en route";
        return false;
    }

    const double risk = agentRouteThreatRisk(agentIndex, starIndex);
    const bool departed = startJourney(*this, agent, starIndex);
    if (!agent.playerControlled) return departed;

    // ⚠️ Цифры для игрока считаются ПОСЛЕ попытки, и это принципиально.
    // `startJourney` первым делом перенастраивает движок по ценам порта вылета
    // (§12.4), то есть меняет рабочую точку — а вместе с ней и требуемое
    // количество вещества. Раньше сообщение о нехватке считалось ДО вылета, по
    // СТАРОЙ точке, и получалось «route blocked: fuel 2/2 short 0»: игрок
    // залил ровно столько, сколько ему показали, и всё равно получил отказ без
    // объяснимой причины. Попытка ничего не ломает — она лишь фиксирует ту
    // самую точку, по которой корабль и полетит, поэтому замер после неё честен.
    const double years = agentRouteTravelTime(agentIndex, starIndex);
    const double fuelNeeded = agentRouteFuelNeeded(agentIndex, starIndex);
    const double shortfall = agentRouteFuelShortfall(agentIndex, starIndex);
    const double propShortfall = agentRoutePropellantShortfall(agentIndex, starIndex);

    if (!departed) {
        // Разводим два разных отказа: «нечего жечь» и «нечего выбрасывать в
        // сопло» — это разные покупки, и игрок должен видеть, какая нужна.
        const double overload = shipCargoOverload(agent.ship);
        if (overload > 0.001) {
            lastEvent = "route blocked: overloaded by " +
                std::to_string(int(std::ceil(overload))) + " over rated hold";
        } else if (!agentRouteCost(agentIndex, starIndex).feasible) {
            lastEvent = "route blocked: drive cannot reach on this propellant";
        } else if (propShortfall > shortfall) {
            lastEvent = "route blocked: propellant short " +
                std::to_string(int(std::ceil(propShortfall)));
        } else {
            lastEvent = "route blocked: fuel " + std::to_string(int(shipFuelMix(agent.ship).mass)) +
                "/" + std::to_string(int(std::ceil(fuelNeeded))) +
                " short " + std::to_string(int(std::ceil(shortfall)));
        }
        return false;
    }

    lastEvent = "route set: " + cluster.stars[starIndex].name +
        " " + std::to_string(int(std::ceil(years))) + "Y fuel " +
        std::to_string(int(std::ceil(fuelNeeded))) + " risk " +
        std::to_string(int(std::ceil(risk * 100.0))) + "%";
    return true;
}

double Game::routeDistance(int originStar, int targetStar) const {
    if (!validStar(*this, originStar) || !validStar(*this, targetStar)) return -1.0;
    return cachedRouteDistance(*this, originStar, targetStar);
}

double Game::agentRouteDistance(int agentIndex, int targetStar) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size()) || !validStar(*this, targetStar)) return -1.0;
    const Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute && validStar(*this, agent.ship.targetStar)) {
        const double leg = distanceShipToStar(agent.ship, cluster.stars[agent.ship.targetStar]);
        const double rest = agent.ship.targetStar == targetStar ? 0.0 : plannedRouteDistance(*this, agent.ship, agent.ship.targetStar, targetStar);
        return leg + std::max(0.0, rest);
    }
    if (!validStar(*this, agent.currentStar)) return distanceShipToStar(agent.ship, cluster.stars[targetStar]);
    return plannedRouteDistance(*this, agent.ship, agent.currentStar, targetStar);
}

double Game::agentRouteTravelTime(int agentIndex, int targetStar) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return -1.0;
    if (!validStar(*this, targetStar)) return -1.0;
    const Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute && validStar(*this, agent.ship.targetStar)) {
        const double leg = travelTimeEstimate(distanceShipToStar(agent.ship, cluster.stars[agent.ship.targetStar]), agent.ship);
        const double rest = agent.ship.targetStar == targetStar ? 0.0 : plannedRouteTravelTime(*this, agent.ship, agent.ship.targetStar, targetStar);
        return leg + std::max(0.0, rest);
    }
    if (!validStar(*this, agent.currentStar)) {
        return travelTimeEstimate(distanceShipToStar(agent.ship, cluster.stars[targetStar]), agent.ship);
    }
    return plannedRouteTravelTime(*this, agent.ship, agent.currentStar, targetStar);
}

double Game::agentRouteFuelNeeded(int agentIndex, int targetStar) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return -1.0;
    if (!validStar(*this, targetStar)) return -1.0;
    const RouteCost cost = agentRouteCost(agentIndex, targetStar);
    return cost.feasible ? cost.fuelMass : -1.0;
}

RouteCost Game::agentRouteCost(int agentIndex, int targetStar) const {
    RouteCost bad;
    if (agentIndex < 0 || agentIndex >= int(agents.size()) || !validStar(*this, targetStar)) return bad;
    const Agent& agent = agents[agentIndex];

    double propellantPrice = 1.0;
    double fuelPrice = 1.0;
    routePrices(*this, agent.ship, agent.currentStar, propellantPrice, fuelPrice);

    if (agent.ship.enRoute && validStar(*this, agent.ship.targetStar)) {
        RouteCost total = legCost(agent.ship,
            distanceShipToStar(agent.ship, cluster.stars[agent.ship.targetStar]), propellantPrice, fuelPrice);
        if (agent.ship.targetStar != targetStar) {
            addCost(total, cachedRouteCost(*this, agent.ship, agent.ship.targetStar, targetStar,
                                           propellantPrice, fuelPrice));
        }
        return total;
    }
    if (!validStar(*this, agent.currentStar)) {
        return legCost(agent.ship, distanceShipToStar(agent.ship, cluster.stars[targetStar]),
                       propellantPrice, fuelPrice);
    }
    return plannedRouteCost(*this, agent.ship, agent.currentStar, targetStar);
}

double Game::agentRouteFuelShortfall(int agentIndex, int targetStar) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    const RouteCost cost = agentRouteCost(agentIndex, targetStar);
    if (!cost.feasible) return 0.0;
    return std::max(0.0, cost.fuelMass - shipFuelMix(agents[agentIndex].ship).mass);
}

double Game::agentRoutePropellantShortfall(int agentIndex, int targetStar) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    const Ship& ship = agents[agentIndex].ship;
    if (driveUsesFuelAsPropellant(ship.driveIndex)) return 0.0;
    const RouteCost cost = agentRouteCost(agentIndex, targetStar);
    if (!cost.feasible) return 0.0;
    return std::max(0.0, cost.propellantMass - shipPropellantMix(ship).mass);
}

double Game::agentRouteThreatRisk(int agentIndex, int targetStar) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size()) || !validStar(*this, targetStar)) return 0.0;
    const Agent& agent = agents[agentIndex];
    if (!validFaction(*this, agent.ship.ownerFaction) || !validStar(*this, agent.currentStar)) return 0.0;
    if (agent.currentStar == targetStar && !agent.ship.enRoute) return 0.0;
    return factionRouteThreatRisk(agent.ship.ownerFaction, agent.currentStar, targetStar);
}

double Game::playerRouteMarketConfidence(int targetStar, int elementIndex) const {
    if (!validStar(*this, targetStar) || elementIndex < 0 || elementIndex >= int(elementCount())) return 0.0;
    if (playerAtStar(targetStar)) return 1.0;
    if (!playerKnowsMarket(targetStar)) return 0.0;
    return playerKnownMarketConfidence(targetStar, elementIndex);
}

double Game::agentContractRouteDistance(int agentIndex, int contractId) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return -1.0;
    const Contract* contract = contractById(*this, contractId);
    if (!contract || !activeContract(*contract) || !validStar(*this, contract->targetStar)) return -1.0;
    if (contract->acceptedByAgent >= 0 && contract->acceptedByAgent != agentIndex) return -1.0;
    return agentRouteDistance(agentIndex, contract->targetStar);
}

double Game::agentContractRouteTravelTime(int agentIndex, int contractId) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return -1.0;
    const Contract* contract = contractById(*this, contractId);
    if (!contract || !activeContract(*contract)) return -1.0;
    const double distance = agentContractRouteDistance(agentIndex, contractId);
    if (distance < 0.0) return -1.0;
    const Ship routeShip = contractRouteShip(agents[agentIndex], *contract);
    const Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute && validStar(*this, agent.ship.targetStar)) {
        const double leg = travelTimeEstimate(distanceShipToStar(agent.ship, cluster.stars[agent.ship.targetStar]), routeShip);
        const double rest = agent.ship.targetStar == contract->targetStar ? 0.0 :
            plannedRouteTravelTime(*this, routeShip, agent.ship.targetStar, contract->targetStar);
        return leg + std::max(0.0, rest);
    }
    if (!validStar(*this, agent.currentStar)) return travelTimeEstimate(distance, routeShip);
    return plannedRouteTravelTime(*this, routeShip, agent.currentStar, contract->targetStar);
}

double Game::agentContractRouteFuelNeeded(int agentIndex, int contractId) const {
    const RouteCost cost = agentContractRouteCost(agentIndex, contractId);
    return cost.feasible ? cost.fuelMass : -1.0;
}

// (§46) ДОРОГА ЗАКАЗА В КРЕДИТАХ. Строка заказа называла тонны («FUEL 4 OK») и
// не называла денег, а слово `OK` означает «уже в баке» и читается как
// «бесплатно». Замер по шести мирам: 45 строк из 48 (94%) убыточны по одному
// топливу — награда 1 081 Cr при сгорающем на 4 643. Планировщик NPC вычитает
// `fuelCost` из оценки заказа с самого начала (`findBestTrade`); игроку той же
// оценки не показывали.
double Game::agentContractRoadCost(int agentIndex, int contractId) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return -1.0;
    const RouteCost cost = agentContractRouteCost(agentIndex, contractId);
    if (!cost.feasible) return -1.0;
    const Agent& agent = agents[size_t(agentIndex)];
    if (!validStar(*this, agent.currentStar)) return -1.0;
    return burnCost(*this, agent.ship, agent.currentStar, cost);
}

RouteCost Game::agentContractRouteCost(int agentIndex, int contractId) const {
    RouteCost bad;
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return bad;
    const Contract* contract = contractById(*this, contractId);
    if (!contract || !activeContract(*contract)) return bad;
    const double distance = agentContractRouteDistance(agentIndex, contractId);
    if (distance < 0.0) return bad;
    const Ship routeShip = contractRouteShip(agents[agentIndex], *contract);
    const Agent& agent = agents[agentIndex];

    double propellantPrice = 1.0;
    double fuelPrice = 1.0;
    routePrices(*this, routeShip, agent.currentStar, propellantPrice, fuelPrice);

    if (agent.ship.enRoute && validStar(*this, agent.ship.targetStar)) {
        RouteCost total = legCost(routeShip,
            distanceShipToStar(agent.ship, cluster.stars[agent.ship.targetStar]), propellantPrice, fuelPrice);
        if (agent.ship.targetStar != contract->targetStar) {
            addCost(total, cachedRouteCost(*this, routeShip, agent.ship.targetStar, contract->targetStar,
                                           propellantPrice, fuelPrice));
        }
        return total;
    }
    if (!validStar(*this, agent.currentStar)) return legCost(routeShip, distance, propellantPrice, fuelPrice);
    return plannedRouteCost(*this, routeShip, agent.currentStar, contract->targetStar);
}

double Game::agentContractRouteFuelShortfall(int agentIndex, int contractId) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    const RouteCost cost = agentContractRouteCost(agentIndex, contractId);
    if (!cost.feasible) return 0.0;
    return std::max(0.0, cost.fuelMass - shipFuelMix(agents[agentIndex].ship).mass);
}

double Game::agentContractRoutePropellantShortfall(int agentIndex, int contractId) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    const Ship& ship = agents[agentIndex].ship;
    if (driveUsesFuelAsPropellant(ship.driveIndex)) return 0.0;
    const RouteCost cost = agentContractRouteCost(agentIndex, contractId);
    if (!cost.feasible) return 0.0;
    return std::max(0.0, cost.propellantMass - shipPropellantMix(ship).mass);
}

double Game::agentContractRouteThreatRisk(int agentIndex, int contractId) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    const Contract* contract = contractById(*this, contractId);
    if (!contract || !activeContract(*contract) || !validStar(*this, contract->targetStar)) return 0.0;
    if (contract->acceptedByAgent >= 0 && contract->acceptedByAgent != agentIndex) return 0.0;
    const Agent& agent = agents[agentIndex];
    if (!validFaction(*this, agent.ship.ownerFaction) || !validStar(*this, agent.currentStar)) return 0.0;
    if (agent.currentStar == contract->targetStar && !agent.ship.enRoute) return 0.0;
    return factionRouteThreatRisk(agent.ship.ownerFaction, agent.currentStar, contract->targetStar);
}

bool Game::agentContractCargoFits(int agentIndex, int contractId) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    const Contract* contract = contractById(*this, contractId);
    if (!contract || !activeContract(*contract)) return false;
    if (contract->acceptedByAgent >= 0) return contract->acceptedByAgent == agentIndex;
    if (!contractUsesCargo(contract->type)) return true;
    if (contract->resource < 0 || contract->resource >= int(elementCount()) || contract->amount <= 0.0) return false;
    const Agent& agent = agents[agentIndex];
    if (!agent.ship.cargo.empty()) return false;
    const double cargoMass = contract->amount * resourceUnitMassByIndex(contract->resource);
    // Мерка — ВЕСЬ ФЛОТ в системе отправления, а не один трюм (§24). Иначе
    // крупный заказ был бы вечно серым: он и задуман как не влезающий в
    // одиночку. Для обычного заказа ответ не меняется — капитан входит в флот.
    if (agent.playerControlled) {
        return cargoMass <= playerFleetCapacityAt(contract->originStar) + 0.001;
    }
    return cargoMass <= agent.ship.cargoCapacity - shipCargoMass(agent.ship) + 0.001;
}

bool Game::agentBuyElement(int agentIndex, int elementIndex) {
    return agentBuyElementAmount(agentIndex, elementIndex, std::numeric_limits<double>::max());
}

bool Game::agentBuyElementAmount(int agentIndex, int elementIndex, double amount) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Agent& agent = agents[agentIndex];
    // Отозванная лицензия замораживает торговлю игрока (добыча и контракты живы).
    if (agent.playerControlled && playerTradingBlocked()) return false;
    if (agent.ship.enRoute || !validStar(*this, agent.currentStar)) return false;
    if (elementIndex < 0 || elementIndex >= int(elementCount()) || amount <= 0.0) return false;
    const auto& element = elementDefinitions()[elementIndex];


    Market& market = markets[agent.currentStar];
    const double buyPrice = market.prices[elementIndex];
    if (buyPrice <= 0.0 || market.supply[elementIndex].amount <= 0.01) return false;

    // ⚠️ «КУПИТЬ МАКСИМУМ» ОСТАВЛЯЕТ НА ТОПЛИВО.
    //
    // Замер, ради которого это написано: игрок делает удачный рейс (+3000 Cr),
    // на следующей станции жмёт «максимум», тратит всё до копейки на груз — и
    // не может вылететь, потому что баки пусты, а денег на заправку нет.
    // Дальше он заперт с полным трюмом в системе, где этот груз никому не
    // нужен: продать обратно можно только в убыток, и об этом нигде не сказано.
    // Прогон «разумной игры» на трёх сидах давал ровно это — 2..4 сделки и
    // вечные 10 000 Cr состояния (то есть один корпус и пустой кошелёк).
    //
    // Планировщик NPC этой ямы не знает с самого начала: `findBestTrade`
    // отсекает сделку по `cargoCost + fuelCost > money` (game.cpp). Здесь тот
    // же закон, только выраженный через станционную котировку: на долив баков
    // деньги придерживаются. Явно названный объём не трогается — игрок,
    // который вписал число, знает, что делает.
    double budgetCap = amount;
    if (agent.playerControlled && amount > 1.0e11) {
        // ⚠️ Резерв считается на ЗАГРУЖЕННОМ корабле. Расход рабочего тела
        // растёт вместе с массой, а котировка снималась с пустого трюма — и
        // придержанного не хватало ровно тогда, когда груз тяжёлый: замер
        // показывал «придержано 454, после погрузки нужно 643», и кошелёк
        // садился в ноль. Считаем по копии корабля, набитого под завязку тем
        // самым элементом, который сейчас покупаем.
        double reserve = agentRefuelQuote(agentIndex);
        if (validStar(*this, agent.destStar) && agent.destStar != agent.currentStar) {
            Ship loaded = agent.ship;
            loaded.cargo.clear();
            const double room = std::max(0.0, loaded.cargoCapacity);
            loaded.cargo.emplace_back(element.symbol,
                                      room / std::max(1e-9, resourceUnitMassByIndex(elementIndex)));
            const RouteCost need = plannedRouteCost(*this, loaded, agent.currentStar, agent.destStar);
            reserve = std::max(reserve, refillCost(*this, loaded, agent.currentStar, need));
        }
        reserve = std::min(reserve, std::max(0.0, agent.money) * 0.5);
        if (reserve > 0.0 && buyPrice > 0.0) {
            budgetCap = std::max(0.0, agent.money - reserve) / buyPrice;
            if (budgetCap <= 0.01) {
                lastEvent = "keeping " + std::to_string(int(std::ceil(reserve))) + " Cr for fuel";
                return false;
            }
        }
    }

    TradePlan plan;
    plan.destStar = agent.currentStar;
    plan.elementIndex = elementIndex;
    plan.amount = budgetCap;
    plan.buyPrice = buyPrice;
    plan.sellPrice = buyPrice;
    double beforeAmount = 0.0;
    for (const auto& res : agent.ship.cargo) {
        if (res.element == element.symbol) {
            beforeAmount = res.amount;
            break;
        }
    }
    buyCargo(*this, agent, agent.currentStar, plan);
    double afterAmount = 0.0;
    for (const auto& res : agent.ship.cargo) {
        if (res.element == element.symbol) {
            afterAmount = res.amount;
            break;
        }
    }
    return afterAmount > beforeAmount + 0.001;
}

// Сколько МАССЫ каждого расходника надо долить, чтобы уверенно пройти
// назначенный маршрут. Общая для котировки и для самой покупки — иначе кнопка
// показывала бы одно, а списывала другое.
//
// ⚠️ Раньше и то, и другое считалось «под пробку», и это была ловушка, а не
// удобство: бак рабочего тела держит объём на сотню рейсов, поэтому полный
// долив стоил 26 000 … 112 000 Cr при рейсе в 3 000 … 10 000. Игрок, нажавший
// рекомендованную Тимертией кнопку после первой удачной сделки, оставался с
// полными баками и пустым кошельком. Настоящая дорога стоит 12…22% рейса —
// вот столько и надо покупать.
// ТИПИЧНОЕ ПЛЕЧО из этой системы: четвёртая по близости соседка. Именно она, а
// не ближайшая: ближайшая бывает в полусветовом годе, и залив под неё игрок
// встанет на первом же настоящем рейсе. Четвёртая лежит там же, где по замерам
// §26 всегда лежит победитель совета — в двух-трёх световых годах.
int Game::typicalHopStar(int originStar) const {
    if (!validStar(*this, originStar)) return -1;
    const ClusterStar& from = cluster.stars[size_t(originStar)];
    const int WANT = 4;
    double best[WANT];
    int bestIdx[WANT];
    for (int i = 0; i < WANT; ++i) { best[i] = 1e300; bestIdx[i] = -1; }
    const int limit = int(std::min(cluster.stars.size(), markets.size()));
    for (int i = 0; i < limit; ++i) {
        if (i == originStar) continue;
        const double d = distanceBetween(from, cluster.stars[size_t(i)]);
        for (int s = 0; s < WANT; ++s) {
            if (d >= best[s]) continue;
            for (int t = WANT - 1; t > s; --t) { best[t] = best[t - 1]; bestIdx[t] = bestIdx[t - 1]; }
            best[s] = d; bestIdx[s] = i;
            break;
        }
    }
    for (int i = WANT - 1; i >= 0; --i) if (bestIdx[i] >= 0) return bestIdx[i];
    return -1;
}

void Game::refuelTargets(int agentIndex, double& fuelMass, double& propMass) const {
    fuelMass = 0.0;
    propMass = 0.0;
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return;
    const Agent& agent = agents[size_t(agentIndex)];
    const MixSummary fuelMix = shipFuelMix(agent.ship);
    const MixSummary propMix = shipPropellantMix(agent.ship);

    // ⚠️ Считаем по кораблю, каким он ВЫЙДЕТ из порта (§46): трюм под завязку.
    // Игрок заправляется ДО закупки, а летит ПОСЛЕ неё, и расход растёт вместе
    // с массой — запас в половину съедался ровно тем грузом, который игрок
    // купит следующим действием.
    const Ship planned = shipAsItWillLeave(agent.ship, true);

    // Есть назначенная цель — считаем ПО НЕЙ, с запасом в половину: маршрут
    // может пойти другим плечом, а встать без топлива дороже, чем перекупить.
    //
    // ⚠️ ЦЕЛИ ОБЫЧНО НЕТ. По прибытии `destStar == currentStar`, а кнопку
    // нажимают именно по прибытии — новелла учит этому репликой 14. Прежняя
    // ветка «цели нет» доливала до ПОЛОВИНЫ ЁМКОСТИ, то есть под сотню рейсов
    // вперёд: замер — переплата x1.1…x86 против настоящего плеча, кнопка
    // стабильно забирала 60% кошелька, и с одиннадцатого рейса состояние
    // переставало расти вовсе (x1.12 за пятнадцать рейсов). Поэтому без цели
    // мерим ТИПИЧНОЕ ПЛЕЧО — дорогу до ближайшей системы, где есть рынок.
    int leg = agent.destStar;
    if (!validStar(*this, leg) || leg == agent.currentStar) leg = typicalHopStar(agent.currentStar);

    RouteCost need;
    if (validStar(*this, leg) && validStar(*this, agent.currentStar)) {
        need = plannedRouteCost(*this, planned, agent.currentStar, leg);
    }
    if (need.feasible) {
        fuelMass = need.fuelMass * 1.5;
        propMass = need.propellantMass * 1.5;
    } else {
        // Плечо не считается вовсе (борт в пустоте, мир без соседей) — тогда
        // прежний закон: половина ёмкости. Это всё ещё много, но это последний
        // рубеж, а не обычный путь.
        const int fe = shipDominantFuelElement(agent.ship);
        const int pe = shipDominantPropellantElement(agent.ship);
        if (fe >= 0 && fe < int(elementCount())) {
            fuelMass = shipFuelTankVolume(agent.ship) * 0.5 /
                       std::max(1e-9, elementUnitVolume(fe)) * elementUnitMass(fe);
        }
        if (pe >= 0 && pe < int(elementCount())) {
            propMass = agent.ship.propellantVolume * 0.5 /
                       std::max(1e-9, elementUnitVolume(pe)) * elementUnitMass(pe);
        }
    }
    fuelMass = std::max(fuelMass, fuelMix.mass);
    propMass = std::max(propMass, propMix.mass);
}

double Game::agentRefuelQuote(int agentIndex) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    const Agent& agent = agents[size_t(agentIndex)];
    if (agent.ship.enRoute || !validStar(*this, agent.currentStar)) return 0.0;
    const Market& market = markets[size_t(agent.currentStar)];
    double tariff = tariffFor(*this, agent.currentStar, agent.ship.ownerFaction, 0.014);
    if (agent.playerControlled) tariff /= std::max(1.0, tech.charisma);
    if (freeMarketFor(*this, agent, agent.currentStar)) return 0.0;

    double fuelTarget = 0.0, propTarget = 0.0;
    refuelTargets(agentIndex, fuelTarget, propTarget);

    double total = 0.0;
    for (int pass = 0; pass < 2; ++pass) {
        // Учитываем и то, чего на рынке ПРОСТО НЕТ: `buyConsumable` режет объём
        // по запасу склада, и без этого котировка обещала бы то, чего не купит.
        const bool bunker = pass == 0;
        if (!bunker && driveUsesFuelAsPropellant(agent.ship.driveIndex)) continue;
        const int element = bunker ? shipDominantFuelElement(agent.ship)
                                   : shipDominantPropellantElement(agent.ship);
        if (element < 0 || element >= int(elementCount())) continue;
        if (element >= int(market.prices.size())) continue;
        const MixSummary mix = bunker ? shipFuelMix(agent.ship) : shipPropellantMix(agent.ship);
        const double capacity = bunker ? shipFuelTankVolume(agent.ship) : agent.ship.propellantVolume;
        const double target = bunker ? fuelTarget : propTarget;
        const double unitMass = elementUnitMass(element);
        const double roomUnits = std::max(0.0, (capacity - mix.volume) /
                                          std::max(1e-9, elementUnitVolume(element)));
        double wantUnits = std::min(std::max(0.0, target - mix.mass) / std::max(1e-6, unitMass), roomUnits);
        if (element < int(market.supply.size())) {
            wantUnits = std::min(wantUnits, std::max(0.0, market.supply[size_t(element)].amount));
        }
        total += wantUnits * market.prices[size_t(element)] * (1.0 + REFINERY_MARKUP) * (1.0 + tariff);
    }
    // ⚠️ И потолок кошелька — тот же, что применит покупка. Без него кнопка
    // показывала 232 133 Cr там, где списывала 600: цена на кнопке врала в
    // сотни раз ровно в том случае, ради которого её и завели, — у бедного
    // игрока с сухими баками.
    if (agent.playerControlled) {
        total = std::min(total, std::max(0.0, agent.money) * REFUEL_WALLET_SHARE);
    }
    return total;
}

bool Game::agentBuyFuel(int agentIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute || !validStar(*this, agent.currentStar)) return false;
    // Заливаем обе ёмкости под пробку по станционной цене (с наценкой за очистку).
    // Цель задаём в массе: сколько влезет по объёму на текущем составе.
    double fuelTarget = 0.0, propTarget = 0.0;
    refuelTargets(agentIndex, fuelTarget, propTarget);

    // Потолок расхода: заправка не имеет права оставить игрока без оборотного
    // капитала. Дальше `buyConsumable` сам режет объём по остатку кошелька,
    // поэтому достаточно временно спрятать неприкосновенную часть.
    const double keep = agent.playerControlled
        ? std::max(0.0, agent.money) * (1.0 - REFUEL_WALLET_SHARE) : 0.0;
    const double purse = agent.money;
    agent.money = std::max(0.0, purse - keep);

    // ⚠️ РАБОЧЕЕ ТЕЛО ПЕРВЫМ. Бункер заливался первым и выбирал весь лимит
    // кошелька целиком: топливо (актиний, 500…2200 Cr/ед) на два-три порядка
    // дороже рабочего тела (водород, 2…4 Cr/ед), и до второй строки деньги
    // просто не доходили. Замер: восемь нажатий подряд при кошельке 2 000 …
    // 80 000 Cr тратили его ДО КОПЕЙКИ и оставляли 0.00 т рабочего тела — а без
    // рабочего тела корабль не движется вообще. Сперва то, без чего не улететь.
    bool any = false;
    if (!driveUsesFuelAsPropellant(agent.ship.driveIndex)) {
        any = buyConsumable(*this, agent, agent.currentStar, false, propTarget);
    }
    any = buyConsumable(*this, agent, agent.currentStar, true, fuelTarget) || any;
    const double spent = std::max(0.0, purse - keep) - agent.money;
    agent.money = purse - spent;
    if (any && keep > 0.0 && spent >= (purse - keep) - 0.01) {
        lastEvent = "partial fill: keeping " + std::to_string(int(std::ceil(keep))) + " Cr as working capital";
    }
    return any;
}

// --- Ручной перелив трюм <-> ёмкости. Бесплатен: игрок сам привёз элемент. ---

double Game::agentLoadFuelFromCargo(int agentIndex, int elementIdx, double units) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute) { lastEvent = "cannot transfer in transit"; return 0.0; }
    const double moved = shipLoadFuel(agent.ship, elementIdx, units);
    if (moved > 0.0) agent.lastAction = "bunkered fuel";
    return moved;
}

double Game::agentLoadPropellantFromCargo(int agentIndex, int elementIdx, double units) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute) { lastEvent = "cannot transfer in transit"; return 0.0; }
    const double moved = shipLoadPropellant(agent.ship, elementIdx, units);
    if (moved > 0.0) agent.lastAction = "loaded propellant";
    return moved;
}

// Двигатель настраивается только НА СТОЯНКЕ. Рабочая точка `cruiseExhaust`
// фиксируется на вылете (§12.4), и планировщик с реальным расходом обязаны
// считать по одной и той же скорости истечения — иначе это грабля §12.5.2,
// только запущенная рукой игрока, а не кодом.
//
// Гейт — именно `enRoute`, а НЕ «стоим в системе»: после экстренного STOP
// корабль встаёт посреди пустоты (`enRoute=false`, `currentStar=-1`), и там
// перенастроить схему под новую цель не только можно, но и нужно.
void Game::agentSetThrottle(int agentIndex, double throttle) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute) {
        if (agent.playerControlled) lastEvent = "drive locked in flight - STOP first";
        return;
    }
    agent.ship.throttle = std::max(0.0, std::min(1.0, throttle));
    double propellantPrice = 1.0;
    double fuelPrice = 1.0;
    routePrices(*this, agent.ship, agent.currentStar, propellantPrice, fuelPrice);
    shipTuneDrive(agent.ship, propellantPrice, fuelPrice);
}

bool Game::setAgentDestination(int agentIndex, int starIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size()) || !validStar(*this, starIndex)) return false;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute) {
        if (agent.playerControlled) lastEvent = "cannot retarget in transit";
        return false;
    }
    agent.destStar = starIndex;
    if (agent.playerControlled) lastEvent = "destination: " + cluster.stars[starIndex].name;
    return true;
}

// Тот же гейт, что у ручки режима (см. agentSetThrottle): крейсер входит в
// маршрутную оценку через `nominalDeltaV`, поэтому в полёте он тоже заперт.
void Game::agentSetCruiseFraction(int agentIndex, double fraction) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute) {
        if (agent.playerControlled) lastEvent = "cruise locked in flight - STOP first";
        return;
    }
    agent.ship.cruiseFraction = std::max(0.2, std::min(1.0, fraction));
    double propellantPrice = 1.0;
    double fuelPrice = 1.0;
    routePrices(*this, agent.ship, agent.currentStar, propellantPrice, fuelPrice);
    shipTuneDrive(agent.ship, propellantPrice, fuelPrice);
}

bool Game::agentOptimiseForTarget(int agentIndex, int targetStar) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    if (!validStar(*this, targetStar)) return false;
    Agent& agent = agents[agentIndex];
    // Кнопка OPTIMAL перебирает пару (throttle, cruise) — то есть делает то же,
    // что две ручки, и запирается тем же гейтом. Рисовалась она неактивной уже
    // давно (`canOptimise` в drawCargoWindow), но клик всё равно проходил.
    if (agent.ship.enRoute) {
        if (agent.playerControlled) lastEvent = "drive locked in flight - STOP first";
        return false;
    }

    double propellantPrice = 1.0;
    double fuelPrice = 1.0;
    routePrices(*this, agent.ship, agent.currentStar, propellantPrice, fuelPrice);

    const double keepThrottle = agent.ship.throttle;
    const double keepCruise = agent.ship.cruiseFraction;

    // Перебираем пару. Сетка грубая намеренно: обе оси пологие, а окно
    // достижимости узкое, поэтому мелкий шаг ничего не уточнит, зато кнопка
    // должна отрабатывать мгновенно.
    double bestCost = -1.0;
    double bestThrottle = keepThrottle;
    double bestCruise = keepCruise;
    for (int ti = 0; ti <= 20; ++ti) {
        for (int ci = 0; ci <= 16; ++ci) {
            agent.ship.throttle = double(ti) / 20.0;
            agent.ship.cruiseFraction = 0.2 + 0.8 * double(ci) / 16.0;
            shipTuneDrive(agent.ship, propellantPrice, fuelPrice);
            const RouteCost r = agentRouteCost(agentIndex, targetStar);
            if (!r.feasible) continue;
            // Считаем только то, что реально влезает в баки корабля.
            const MixSummary pm = shipPropellantMix(agent.ship);
            const MixSummary fm = shipFuelMix(agent.ship);
            const double propVol = pm.mass > 0.0 ? r.propellantMass * pm.volume / pm.mass : 0.0;
            const double fuelVol = fm.mass > 0.0 ? r.fuelMass * fm.volume / fm.mass : 0.0;
            if (propVol > agent.ship.propellantVolume) continue;
            if (fuelVol > shipFuelTankVolume(agent.ship)) continue;
            const double cost = r.propellantMass * propellantPrice + r.fuelMass * fuelPrice;
            if (bestCost < 0.0 || cost < bestCost) {
                bestCost = cost;
                bestThrottle = agent.ship.throttle;
                bestCruise = agent.ship.cruiseFraction;
            }
        }
    }

    agent.ship.throttle = bestCost >= 0.0 ? bestThrottle : keepThrottle;
    agent.ship.cruiseFraction = bestCost >= 0.0 ? bestCruise : keepCruise;
    shipTuneDrive(agent.ship, propellantPrice, fuelPrice);
    if (bestCost < 0.0) {
        lastEvent = "no drive setting reaches that system";
        return false;
    }
    lastEvent = "drive tuned for " + cluster.stars[targetStar].name;
    return true;
}

double Game::agentJettisonCargo(int agentIndex, int elementIdx, double units) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    if (elementIdx < 0 || elementIdx >= int(elementCount())) return 0.0;
    Agent& agent = agents[agentIndex];
    const std::string symbol = elementDefinitions()[elementIdx].symbol;
    for (size_t i = 0; i < agent.ship.cargo.size(); ++i) {
        if (agent.ship.cargo[i].element != symbol) continue;
        const double moved = std::min(units, agent.ship.cargo[i].amount);
        agent.ship.cargo[i].amount -= moved;
        if (agent.ship.cargo[i].amount <= 1e-9) agent.ship.cargo.erase(agent.ship.cargo.begin() + i);
        if (moved > 0.0) {
            agent.lastAction = "jettisoned " + symbol;
            lastEvent = "jettisoned " + std::to_string(int(moved)) + " " + symbol;
        }
        return moved;
    }
    return 0.0;
}

double Game::agentDrainFuelToCargo(int agentIndex, int elementIdx, double units) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute) { lastEvent = "cannot transfer in transit"; return 0.0; }
    return shipDrainFuel(agent.ship, elementIdx, units);
}

double Game::agentDrainPropellantToCargo(int agentIndex, int elementIdx, double units) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute) { lastEvent = "cannot transfer in transit"; return 0.0; }
    return shipDrainPropellant(agent.ship, elementIdx, units);
}

bool Game::agentSellCargo(int agentIndex) {
    return agentSellCargoAmount(agentIndex, std::numeric_limits<double>::max(), -1);
}

bool Game::agentSellCargoAmount(int agentIndex, double amount, int elementIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Agent& agent = agents[agentIndex];
    // ⚠️ Продажа при отозванной лицензии РАЗРЕШЕНА — вся выручка уходит в
    // погашение выкупа (см. sellCargo). Это единственный путь назад для того,
    // у кого нет денег на выкуп: покупка по-прежнему заперта, значит новый
    // груз не взять, и остаётся то, что уже в трюме, и то, что накопаешь.
    if (agent.playerControlled && licence().revoked && licence().buyback <= 0.0) {
        playerTradingBlocked();
        return false;
    }
    if (agent.ship.enRoute || !validStar(*this, agent.currentStar) || agent.ship.cargo.empty() || amount <= 0.0) return false;
    std::string elementSymbol = "";
    if (elementIndex >= 0 && elementIndex < int(elementCount())) {
        elementSymbol = elementDefinitions()[elementIndex].symbol;
    }
    return sellCargo(*this, agent, agent.currentStar, amount, elementSymbol);
}

int Game::agentSellAllCargo(int agentIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0;
    Agent& agent = agents[agentIndex];
    if (agent.playerControlled && licence().revoked && licence().buyback <= 0.0) {
        playerTradingBlocked();
        return 0;
    }
    if (agent.ship.enRoute || !validStar(*this, agent.currentStar)) return 0;

    // ⚠️ ГРУЗ ЗАКАЗА НЕ ПРОДАЁТСЯ. Он лежит в трюме обычной партией того же
    // элемента, и «продать всё» сдавало его вместе с остальным — после чего
    // заказ становился несдаваемым, а игрок узнавал об этом на другом конце
    // маршрута. Считаем, сколько единиц этот борт кому-то должен, и оставляем
    // их на месте.
    std::vector<std::pair<std::string, double> > owed;
    for (size_t c = 0; c < contracts.size(); ++c) {
        const Contract& contract = contracts[c];
        if (contract.completed || contract.failed || contract.acceptedByAgent < 0) continue;
        if (!contractUsesCargo(contract.type)) continue;
        if (contract.resource < 0 || contract.resource >= int(elementCount())) continue;
        bool carriesIt = contract.acceptedByAgent == agentIndex;
        for (size_t k = 0; k < contract.carriers.size(); ++k) {
            if (contract.carriers[k] == agentIndex) carriesIt = true;
        }
        if (!carriesIt) continue;
        const std::string symbol = elementDefinitions()[size_t(contract.resource)].symbol;
        bool merged = false;
        for (size_t i = 0; i < owed.size(); ++i) {
            if (owed[i].first != symbol) continue;
            owed[i].second += contract.amount;
            merged = true;
            break;
        }
        if (!merged) owed.push_back(std::make_pair(symbol, contract.amount));
    }

    // Партии сдаются по одной с головы вектора: sellCargo сам стирает опустевшую,
    // поэтому индекс не нужен. Ограничитель по числу партий — от зацикливания,
    // если очередная партия окажется меньше порога 0.01 и sellCargo вернёт false
    // (тогда вектор не уменьшится и цикл обязан прерваться).
    // ⚠️ Резерв тратится ПО ВСЕМУ БОРТУ, а не вычитается из каждой партии. При
    // взятии заказа его груз кладётся ОТДЕЛЬНОЙ партией (`emplace_back`), даже
    // если элемент в трюме уже есть, — поэтому партий одного символа бывает две,
    // и вычитание из каждой сделало бы непродаваемыми обе.
    //
    // ⚠️ Резервируем ВЕСЬ объём заказа на каждом носителе, хотя караван (§24)
    // режет груз между бортами: сколько именно легло на ЭТОТ борт, нигде не
    // записано. Ошибка сознательно в безопасную сторону — перерезервировать
    // значит не дать продать свой же товар того же элемента (неприятно, но
    // обратимо), недорезервировать значит сдать чужой груз и остаться с
    // несдаваемым заказом на другом конце маршрута.
    int lots = 0;
    size_t guard = agent.ship.cargo.size();
    size_t skip = 0;   // сколько партий уже отложено под заказ
    while (agent.ship.cargo.size() > skip && guard-- > 0) {
        // По ЗНАЧЕНИЮ: `sellCargo` может стереть эту самую партию, и ссылка на
        // элемент вектора после неё повиснет.
        const std::string symbol = agent.ship.cargo[skip].element;
        const double had = agent.ship.cargo[skip].amount;
        double reserved = 0.0;
        size_t owedAt = owed.size();
        for (size_t i = 0; i < owed.size(); ++i) {
            if (owed[i].first == symbol) { reserved = owed[i].second; owedAt = i; break; }
        }
        const double hold = std::min(reserved, had);
        const double sellable = had - hold;
        if (sellable <= 0.01) {
            if (owedAt < owed.size()) owed[owedAt].second -= hold;   // остаток резерва
            ++skip;
            continue;
        }
        const size_t before = agent.ship.cargo.size();
        if (!sellCargo(*this, agent, agent.currentStar, sellable, symbol)) { ++skip; continue; }
        const bool lotGone = agent.ship.cargo.size() < before;
        if (!lotGone && agent.ship.cargo[skip].amount >= had - 1e-9) break;   // не сдвинулись — выходим
        if (!lotGone) {
            if (owedAt < owed.size()) owed[owedAt].second -= hold;
            ++skip;   // партия ужалась до остатка под заказ
        }
        ++lots;
    }
    if (lots > 0) {
        lastEvent = "hold sold: " + std::to_string(lots) + " lots";
    } else if (agent.playerControlled) {
        lastEvent = "hold empty";
    }
    return lots;
}

bool Game::agentAcceptContract(int agentIndex, int contractId) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Contract* contract = contractById(*this, contractId);
    if (!contract || !activeContract(*contract) || contract->acceptedByAgent >= 0) return false;
    if (!validStar(*this, contract->originStar) || !validStar(*this, contract->targetStar)) return false;

    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute || agent.currentStar != contract->originStar) return false;

    // (§37.3) ЗАЛОГ. Берётся только с игрока и только при взятии: без него
    // провал стоил одной репутации, а груз ложился в трюм даром — взять
    // срочный заказ и бросить его было выгоднее, чем везти.
    if (agent.playerControlled && contract->deposit > 0.0) {
        if (agent.money < contract->deposit) {
            lastEvent = "job needs a " + std::to_string(int(std::ceil(contract->deposit))) + " Cr deposit";
            agent.lastAction = "deposit too high";
            return false;
        }
    }

    if (contractUsesCargo(contract->type)) {
        if (contract->resource < 0 || contract->resource >= int(elementCount()) || contract->amount <= 0.0) return false;
        Market& origin = markets[contract->originStar];
        if (contract->resource >= int(origin.supply.size()) || origin.supply[contract->resource].amount < contract->amount) return false;

        const double unitMass = std::max(0.001, resourceUnitMassByIndex(contract->resource));
        const double cargoMass = contract->amount * unitMass;

        // --- Груз раскладывается ПО ФЛОТУ (§24) ------------------------------
        //
        // ⚠️ ТОЛЬКО У ИГРОКА. NPC грузится в свой единственный трюм, как и до
        // §24. Первая версия этого не различала, и получилось буквально: NPC
        // брал заказ, а «караваном» ему записывался стоявший рядом корабль
        // ИГРОКА — тот молча улетал с чужим грузом, и доска у игрока пустела
        // навсегда, потому что он вечно был в пути.
        std::vector<int> carriers;
        if (!agent.playerControlled) {
            if (cargoMass > agent.ship.cargoCapacity - shipCargoMass(agent.ship) + 0.001) {
                agent.lastAction = "contract too heavy";
                return false;
            }
            agent.ship.cargo.emplace_back(elementDefinitions()[contract->resource].symbol, contract->amount);
        } else {
            // До §24 условие было «влезет ли в ОДИН трюм», и потолок заказа этим
            // же и задавался: больше стартового трюма никто ничего не возил.
            // Теперь считается суммарная свободная ёмкость бортов игрока,
            // СТОЯЩИХ В ЭТОЙ системе, а груз режется между ними. Борт в пути не
            // участвует — его здесь нет.
            if (cargoMass > playerFleetCapacityAt(contract->originStar) + 0.001) {
                agent.lastAction = "contract too heavy";
                return false;
            }

            // Очередь погрузки: капитан, затем остальные борта по порядку.
            // Капитан первым — чтобы одноместный заказ лёг именно в тот
            // корабль, которым игрок управляет сейчас, а не в случайный борт
            // с трюмом побольше.
            std::vector<int> loadOrder;
            loadOrder.push_back(agentIndex);
            for (size_t i = 0; i < agents.size(); ++i) {
                if (int(i) != agentIndex) loadOrder.push_back(int(i));
            }

            double left = contract->amount;
            for (size_t i = 0; i < loadOrder.size() && left > 1e-9; ++i) {
                Agent& carrier = agents[size_t(loadOrder[i])];
                if (!carrier.playerControlled || carrier.ship.enRoute ||
                    carrier.currentStar != contract->originStar) {
                    continue;
                }
                const double room = carrier.ship.cargoCapacity - shipCargoMass(carrier.ship);
                if (room <= 0.001) continue;
                const double take = std::min(left, room / unitMass);
                if (take <= 1e-9) continue;
                carrier.ship.cargo.emplace_back(elementDefinitions()[contract->resource].symbol, take);
                carrier.cargoCost = 0.0;
                carriers.push_back(loadOrder[i]);
                left -= take;
            }
            if (left > 1e-6) {
                // Не влезло — откатываем всё, что уже нагрузили. Половина заказа
                // в трюме и никакого заказа на руках была бы худшей из ошибок.
                for (int index : carriers) {
                    Ship& hold = agents[size_t(index)].ship;
                    if (!hold.cargo.empty()) hold.cargo.pop_back();
                }
                agent.lastAction = "contract too heavy";
                return false;
            }
        }

        origin.applyTrade(contract->resource, -contract->amount);
        contract->carriers = carriers;
    }
    agent.cargoCost = 0.0;
    contract->acceptedByAgent = agentIndex;
    // (§46) Надбавку на дорогу фиксируем ЗДЕСЬ, по кораблю, который заказ
    // берёт: заказчик оплачивает сгоревшее тому, кто привезёт в срок. Считается
    // до того, как борт тронулся, поэтому число видно на доске и не зависит от
    // того, каким плечом маршрут в итоге пойдёт.
    {
        const double road = agentContractRoadCost(agentIndex, contract->id);
        contract->fuelAllowance = road > 0.0 ? road : 0.0;
    }

    // Залог уходит выдавшему заказ: он и есть тот, кто рискует грузом.
    if (agent.playerControlled && contract->deposit > 0.0) {
        agent.money -= contract->deposit;
        if (validFaction(*this, contract->issuerFaction)) {
            factions[size_t(contract->issuerFaction)].treasury += contract->deposit;
        }
        pushJournal(JournalKind::JobAccepted,
            contractJournalText(*contract) + " DEPOSIT -" +
                std::to_string(int(std::ceil(contract->deposit))) + " Cr",
            -contract->deposit, agent.currentStar);
        journalExplained -= contract->deposit;
    }

    // Остальные носители уходят к цели САМИ (§24). Иначе игроку пришлось бы
    // переключаться на каждый борт и вести его вручную по одному маршруту —
    // работа без единого решения. Капитанский корабль не трогаем: им игрок
    // управляет сам, и отнимать у него руль на взятии заказа нельзя.
    for (int carrierIndex : contract->carriers) {
        if (carrierIndex == agentIndex || carrierIndex < 0 || carrierIndex >= int(agents.size())) continue;
        Agent& carrier = agents[size_t(carrierIndex)];
        if (carrier.ship.enRoute || carrier.currentStar == contract->targetStar) continue;
        startJourney(*this, carrier, contract->targetStar);
        carrier.lastAction = "hauling contract";
    }

    if (contract->type == ContractType::Escort &&
        contract->targetAgent >= 0 && contract->targetAgent < int(agents.size())) {
        Agent& escorted = agents[contract->targetAgent];
        if (!escorted.ship.enRoute && escorted.currentStar == contract->originStar) {
            startJourney(*this, escorted, contract->targetStar);
            escorted.lastAction = "under escort";
        }
    }
    agent.lastAction = std::string("accepted ") + contractTypeLabel(contract->type);
    if (agent.playerControlled) {
        // Белая строка журнала: взял — вот что и куда. Именно её не хватало:
        // заказ брался, игрок улетал и терял из виду, ЧТО он везёт.
        char due[48];
        std::snprintf(due, sizeof(due), " DUE %.0FY", std::max(0.0, contract->deadline - time));
        pushJournal(JournalKind::JobAccepted,
            std::string("TOOK ") + contractJournalText(*contract) + due, 0.0, agent.currentStar);

        const double years = agentContractRouteTravelTime(agentIndex, contract->id);
        const double fuelNeeded = agentContractRouteFuelNeeded(agentIndex, contract->id);
        const double shortfall = agentContractRouteFuelShortfall(agentIndex, contract->id);
        const double risk = agentContractRouteThreatRisk(agentIndex, contract->id);
        lastEvent = std::string("contract ") + contractTypeLabel(contract->type) + ": " +
            cluster.stars[contract->targetStar].name + " " +
            std::to_string(int(std::ceil(std::max(0.0, years)))) + "Y fuel " +
            std::to_string(int(std::ceil(std::max(0.0, fuelNeeded))));
        if (shortfall > 0.05) {
            lastEvent += " short " + std::to_string(int(std::ceil(shortfall)));
        }
        lastEvent += " risk " + std::to_string(int(std::ceil(risk * 100.0))) + "%";
    } else {
        lastEvent = "contract accepted";
    }
    queueContractSignal(contract->issuerFaction, contract->id, contract->originStar, contract->targetStar);
    return true;
}

bool Game::agentCompleteContract(int agentIndex, int contractId) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Contract* contract = contractById(*this, contractId);
    if (!contract || !activeContract(*contract) || contract->acceptedByAgent != agentIndex) return false;
    if (!validStar(*this, contract->targetStar)) return false;

    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute || agent.currentStar != contract->targetStar) return false;

    if (contractUsesCargo(contract->type)) {
        if (contract->resource < 0 || contract->resource >= int(elementCount())) return false;

        // Сдаёт ВЕСЬ КАРАВАН (§24). Список носителей — те борта, по которым
        // груз разложили при взятии; пустой список означает обычный одиночный
        // рейс, и тогда носитель ровно один — капитан.
        std::vector<int> haulers = contract->carriers;
        if (haulers.empty()) haulers.push_back(agentIndex);

        // Все обязаны быть ЗДЕСЬ. Половина каравана на месте — это не половина
        // сдачи, а несдача: заказчику нужен весь объём разом.
        for (int index : haulers) {
            if (index < 0 || index >= int(agents.size())) return false;
            const Agent& hauler = agents[size_t(index)];
            if (hauler.ship.enRoute || hauler.currentStar != contract->targetStar) {
                agent.lastAction = "fleet still inbound";
                return false;
            }
        }

        const std::string& targetSymbol = elementDefinitions()[contract->resource].symbol;
        double totalFound = 0.0;
        for (int index : haulers) {
            for (const Resource& res : agents[size_t(index)].ship.cargo) {
                if (res.element == targetSymbol) totalFound += res.amount;
            }
        }
        if (totalFound + 0.001 < contract->amount) return false;

        double remainingToTake = contract->amount;
        for (size_t h = 0; h < haulers.size() && remainingToTake > 0.001; ++h) {
            std::vector<Resource>& hold = agents[size_t(haulers[h])].ship.cargo;
            for (auto it = hold.begin(); it != hold.end() && remainingToTake > 0.001; ) {
                if (it->element == targetSymbol) {
                    if (it->amount > remainingToTake) {
                        it->amount -= remainingToTake;
                        remainingToTake = 0.0;
                        ++it;
                    } else {
                        remainingToTake -= it->amount;
                        it = hold.erase(it);
                    }
                } else {
                    ++it;
                }
            }
        }
        Market& target = markets[contract->targetStar];
        target.applyTrade(contract->resource, contract->amount);
        target.demand[contract->resource].amount = std::max(0.0, target.demand[contract->resource].amount - contract->amount);
        if (contract->type == ContractType::ColonySupply) {
            applyColonySupplyDelivery(*this, contract->targetStar, contract->resource, contract->amount);
        }
    } else if (contract->type == ContractType::Scout) {
        if (!contract->reportDelivered) {
            if (!contract->reportSignalPending && validFaction(*this, contract->issuerFaction)) {
                queueOwnerSignal(contract->issuerFaction, contract->targetStar, contract->targetStar);
                queueMarketSignal(contract->issuerFaction, contract->targetStar, contract->targetStar);
                queueContractSignal(contract->issuerFaction, contract->id, contract->targetStar, contract->targetStar);
                contract->reportSignalPending = true;
                contract->progress = 0.5;
                agent.lastAction = "scout report sent";
                lastEvent = "scout report sent";
                return true;
            }
            agent.lastAction = "awaiting signal";
            return false;
        }
    } else if (contract->type == ContractType::Bounty) {
        if (contract->targetAgent < 0 || contract->targetAgent >= int(agents.size())) return false;
        Agent& target = agents[contract->targetAgent];
        // (§34) Цель уже сбита в локальном полёте — заказ закрыт по факту.
        // Требовать после этого ещё и абстрактной стычки в порту было бы
        // издевательством: пират уже спасательная капсула.
        if (contract->targetDown) {
            contract->progress = 1.0;
        } else {
        if (target.ship.enRoute || target.currentStar != agent.currentStar || target.type != "pirate") return false;
        const double hunterPower = combatPower(*this, agent);
        const double targetPower = combatPower(*this, target);
        const double margin = hunterPower / std::max(0.1, targetPower);
        const double damageCost = std::max(0.0, (1.12 - margin) * 140.0);
        agent.money = std::max(0.0, agent.money - damageCost);
        target.money = std::max(0.0, target.money - 180.0 - hunterPower * 18.0);
        target.piracyBias = std::max(0.0, target.piracyBias - 0.45);
        target.missionCooldown = std::max(target.missionCooldown, 5.0 + hunterPower * 0.15);
        target.lastAction = "bounty suppressed";
        contract->progress = 1.0;
        const unsigned long long combatEvent = allocateSignalEventId(*this);
        queueCombatSignal(agent.ship.ownerFaction, agent.currentStar, agentIndex, contract->targetAgent, hunterPower, combatEvent);
        queueCombatSignal(target.ship.ownerFaction, agent.currentStar, agentIndex, contract->targetAgent, hunterPower, combatEvent);
        adjustFactionRelation(agent.ship.ownerFaction, target.ship.ownerFaction, -4);
        }
    } else if (contract->type == ContractType::Escort) {
        if (contract->targetAgent < 0 || contract->targetAgent >= int(agents.size())) return false;
        Agent& escorted = agents[contract->targetAgent];
        if (!escorted.ship.enRoute && escorted.currentStar != contract->targetStar) {
            startJourney(*this, escorted, contract->targetStar);
            escorted.lastAction = "under escort";
        }
        if (escorted.ship.enRoute || escorted.currentStar != contract->targetStar) {
            agent.lastAction = "escort waiting";
            return false;
        }
        contract->escortArrived = true;
        contract->progress = 1.0;
        escorted.lastAction = "escorted";
    } else if (contract->type == ContractType::Raid) {
        ClusterStar& targetStar = cluster.stars[contract->targetStar];
        const int defenderFaction = targetStar.ownerFaction;
        const int raiderFaction = validFaction(*this, agent.ship.ownerFaction) ? agent.ship.ownerFaction : contract->issuerFaction;
        if (validFaction(*this, defenderFaction) && defenderFaction == contract->issuerFaction) {
            contract->failed = true;
            agent.lastAction = "raid target stale";
            lastEvent = "raid contract stale: " + targetStar.name;
            if (agent.playerControlled) {
                const double lost = applyContractFailureReputation(*this, *contract);
                char penalty[56];
                std::snprintf(penalty, sizeof(penalty), " TARGET STALE -%.0F REP", lost);
                pushJournal(JournalKind::JobFailed,
                    contractJournalText(*contract) + penalty, 0.0, agent.currentStar);
            }
            return false;
        }

        const double attack = combatPower(*this, agent);
        const double defense =
            targetStar.defense +
            (validFaction(*this, defenderFaction) ? factions[defenderFaction].strength * 0.85 : 0.0);
        const double margin = attack / std::max(0.25, defense);
        const double severity = std::max(0.04, std::min(0.42, 0.08 + margin * 0.13));
        const double repairCost = std::max(0.0, (0.95 - margin) * 120.0);

        agent.money = std::max(0.0, agent.money - repairCost);
        targetStar.defense = std::max(0.25, targetStar.defense - attack * (0.035 + severity * 0.055));
        targetStar.industry = std::max(0.05, targetStar.industry * (1.0 - severity * 0.075));
        targetStar.capturePressure = std::max(targetStar.capturePressure, margin);
        targetStar.contestedAt = time;

        const int colonyIndex = colonyIndexAt(*this, contract->targetStar);
        if (colonyIndex >= 0) {
            colonyApplyRaidDamage(colonies[colonyIndex], severity);
        }

        if (contract->targetStar >= 0 && contract->targetStar < int(markets.size())) {
            Market& market = markets[contract->targetStar];
            const std::vector<ElementDefinition>& elements = elementDefinitions();
            for (size_t i = 0; i < market.supply.size() && i < elements.size(); ++i) {
                const ElementDefinition& element = elements[i];
                const double energyTrait = std::max(element.fusionFuelTrait, element.fissionFuelTrait);
                const double strategicTrait =
                    element.structuralTrait * 0.34 +
                    element.conductorTrait * 0.26 +
                    element.catalystTrait * 0.12 +
                    energyTrait * 0.28;
                const double supplyLoss = severity * (0.018 + strategicTrait * 0.030);
                market.supply[i].amount = std::max(0.0, market.supply[i].amount * (1.0 - supplyLoss));
                if (i < market.demand.size()) {
                    const double emergencyNeed = (i < market.demandRate.size() ? market.demandRate[i] : 0.0) *
                        (3.0 + severity * 12.0) * (0.35 + strategicTrait);
                    market.demand[i].amount += emergencyNeed;
                }
            }
            market.updatePrices();
        }

        if (validFaction(*this, contract->issuerFaction) && validFaction(*this, defenderFaction)) {
            adjustFactionRelation(contract->issuerFaction, defenderFaction, -18);
            adjustFactionRelation(defenderFaction, contract->issuerFaction, -22);
            queueDiplomacySignal(contract->issuerFaction, contract->targetStar, defenderFaction,
                factionRelation(contract->issuerFaction, defenderFaction));
            queueDiplomacySignal(defenderFaction, contract->targetStar, contract->issuerFaction,
                factionRelation(defenderFaction, contract->issuerFaction));
        }
        if (validFaction(*this, raiderFaction) && validFaction(*this, defenderFaction) && raiderFaction != contract->issuerFaction) {
            adjustFactionRelation(raiderFaction, defenderFaction, -10);
        }
        const unsigned long long combatEvent = allocateSignalEventId(*this);
        queueCombatSignal(contract->issuerFaction, contract->targetStar, agentIndex, -1, attack * (1.0 + severity), combatEvent);
        if (validFaction(*this, defenderFaction)) {
            queueCombatSignal(defenderFaction, contract->targetStar, agentIndex, -1, attack * (1.0 + severity), combatEvent);
        }
        contract->progress = 1.0;
        lastEvent = "raid completed: " + targetStar.name;
    } else if (contract->type == ContractType::Courier) {
        if (validFaction(*this, contract->issuerFaction)) {
            queueContractSignal(contract->issuerFaction, contract->id, contract->targetStar, contract->targetStar);
        }
    }

    return payContractReward(*this, *contract, agent, true);
}

int Game::agentCompleteContracts(int agentIndex) {
    int completed = 0;
    for (Contract& contract : contracts) {
        if (activeContract(contract) && contract.acceptedByAgent == agentIndex) {
            if (agentCompleteContract(agentIndex, contract.id)) completed += 1;
        }
    }
    return completed;
}

bool Game::abortAgentRoute(int agentIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Agent& agent = agents[agentIndex];
    if (!agent.ship.enRoute) return false;
    
    // -2 is the targetStar flag for emergency braking in deep space.
    agent.ship.targetStar = -2;
    agent.lastAction = "emergency braking";
    return true;
}

bool Game::agentAutoTrade(int agentIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute || !validStar(*this, agent.currentStar) || !agent.ship.cargo.empty()) return false;

    // Ручное «сторгуй за меня» — тоже действие ИГРОКА, а не симуляции, и поток
    // мира сдвигать не имеет права по той же причине, что и автопилот выше.
    std::mt19937 handRng(static_cast<unsigned int>(seed) * 2654435761u +
                         static_cast<unsigned int>(agentIndex) * 40503u +
                         static_cast<unsigned int>(time) * 97u + 7717u);
    const TradePlan plan = findBestTrade(*this, agent, &handRng);
    if (plan.destStar < 0 || plan.elementIndex < 0) return false;
    buyCargo(*this, agent, agent.currentStar, plan);
    if (agent.ship.cargo.empty()) return false;
    return startJourney(*this, agent, plan.destStar);
}

// ----------------------------------------------------- СОБСТВЕННОСТЬ НА СИСТЕМУ --
// Цена — произведение независимых множителей, каждый из которых отвечает «во
// сколько раз система ценнее пустого камня». Всё контекстно: ни одной константы,
// приписанной конкретной звезде, — только то, что уже живёт в мире (люди, заводы,
// обитаемость, годовой выпуск недр, построенное, владелец).
// Годовой выпуск недр в кредитах. Единственный источник и для цены системы, и
// для дохода её владельца — поэтому «производит больше ⇒ стоит дороже» и
// «производит больше ⇒ приносит больше» это одно и то же утверждение, а не два
// независимо подогнанных.
double Game::systemTurnover(int starIndex) const {
    if (starIndex < 0 || starIndex >= int(markets.size())) return 0.0;
    const Market& market = markets[starIndex];
    const size_t count = std::min(market.productionRate.size(), market.prices.size());
    double turnover = 0.0;
    for (size_t i = 0; i < count; ++i) turnover += market.productionRate[i] * market.prices[i];
    return turnover;
}

SystemPrice Game::systemPrice(int starIndex) const {
    SystemPrice price;
    if (!validStar(*this, starIndex)) return price;
    const ClusterStar& star = cluster.stars[starIndex];

    price.population = 1.0 + std::max(0.0, star.population) / SYSTEM_PRICE_POP_REF;
    price.industry = 1.0 + std::max(0.0, star.industry) / SYSTEM_PRICE_IND_REF;
    price.habitability = 1.0 + std::max(0.0, star.habitability) * SYSTEM_PRICE_HAB_W;

    // Недра — не «сколько тонн лежит», а СКОЛЬКО ЭТО СТОИТ В ГОД: годовой выпуск
    // по местным ценам. Богатая иридием система дороже богатой песком ровно во
    // столько раз, во сколько иридий дороже песка, и ни одной таблицы для этого
    // не нужно — рынок уже всё посчитал.
    price.resources = 1.0 + std::max(0.0, systemTurnover(starIndex)) / SYSTEM_PRICE_TURNOVER_REF;

    const int colonyIndex = colonyIndexAt(*this, starIndex);
    if (colonyIndex >= 0) {
        const Colony& colony = colonies[colonyIndex];
        price.development = 1.0 + std::max(0.0, colony.infrastructure) * SYSTEM_PRICE_INFRA_W +
                            double(std::max(0, colony.shipyardLevel)) * SYSTEM_PRICE_SHIPYARD_W;
    }

    // Суверенная надбавка: выкуп у чужой фракции тем дороже, чем она сильнее.
    if (star.ownerFaction >= 0 && star.ownerFaction != playerFaction &&
        validFaction(*this, star.ownerFaction)) {
        price.sovereignty = 1.0 + std::max(0.0, factions[star.ownerFaction].strength) * SYSTEM_PRICE_FOREIGN_W;
    }

    price.total = SYSTEM_PRICE_BASE * price.population * price.industry * price.habitability *
                  price.resources * price.development * price.sovereignty;
    return price;
}

bool Game::playerOwnsStar(int starIndex) const {
    return validStar(*this, starIndex) && validFaction(*this, playerFaction) &&
           cluster.stars[starIndex].ownerFaction == playerFaction;
}

// Покупка = смена владельца. Никакого «основания»: система как жила, так и живёт,
// меняется только чья она. Если колонии на звезде не было, она заводится здесь же
// из уже имеющегося населения и индустрии — чтобы у собственности была касса.
bool Game::playerBuySystem() {
    if (playerAgent < 0 || playerAgent >= int(agents.size()) || !validFaction(*this, playerFaction)) return false;
    Agent& player = agents[playerAgent];
    if (player.ship.enRoute || !validStar(*this, player.currentStar)) {
        lastEvent = "buy blocked: dock in the system first";
        return false;
    }
    const int starIndex = player.currentStar;
    ClusterStar& star = cluster.stars[starIndex];
    if (star.ownerFaction == playerFaction) {
        player.lastAction = "already yours";
        lastEvent = "buy blocked: " + star.name + " is already yours";
        return false;
    }

    const double cost = systemPrice(starIndex).total;
    if (player.money < cost) {
        player.lastAction = "need credits";
        lastEvent = "buy blocked: need " + std::to_string((long long)std::ceil(cost)) + " Cr";
        return false;
    }

    // (§47) Продавец — ВЛАСТЬ над системой: держава-владелец или центр. Раньше
    // здесь стоял сырой `star.ownerFaction`, и покупка ничейной звезды —
    // а таких 98% — списывала деньги в НИКУДА: комментарий строкой ниже обещал
    // «деньги не исчезают из мира», и ровно в самом частом случае обещание не
    // выполнялось.
    const int seller = starAuthority(*this, starIndex);
    player.money -= cost;
    // Деньги не исчезают из мира: прежний владелец получает всё и пустит их на
    // свою экспансию и флот — ты буквально финансируешь соседа, у которого купил.
    if (validFaction(*this, seller)) factions[seller].treasury += cost;

    setStarOwner(*this, starIndex, playerFaction);
    // Звезда и колония на ней — две записи, и владелец у них меняется РАЗНЫМИ
    // вызовами (`setStarOwner` трогает только звезду). Забыть второй значит
    // купить систему, чья касса продолжает работать на прежнего хозяина.
    transferColonies(*this, starIndex, playerFaction);
    if (colonyIndexAt(*this, starIndex) < 0) addColony(*this, starIndex, playerFaction, false);
    observeStar(starIndex);
    boughtSystems += 1;
    player.lastAction = "bought system";
    lastEvent = "player bought " + star.name;
    return true;
}

// Своя система — своё имя. Номер звезды остаётся: он адрес, по которому систему
// находят в списках и заказах, и трогать его нельзя (§25). Игрок правит только
// основу, и она сразу идёт в тот же реестр имён, что и сгенерированные, —
// поэтому «HOME-212» на русском покажется «ХОМЕ-212», а не латиницей посреди
// кириллической строки.
bool Game::playerRenameSystem(int starIndex, const std::string& stem) {
    if (!validStar(*this, starIndex) || !playerOwnsStar(starIndex)) return false;
    // Стоя в системе, как и всё остальное в окне собственности: империю
    // облетают, а не переименовывают из списка (§13).
    if (!playerAtStar(starIndex)) {
        lastEvent = "rename blocked: dock in the system first";
        return false;
    }
    // Обрезка пробелов по краям: имя из одних пробелов — это пустое имя.
    size_t from = 0, to = stem.size();
    while (from < to && (unsigned char)stem[from] <= ' ') ++from;
    while (to > from && (unsigned char)stem[to - 1] <= ' ') --to;
    const std::string clean = stem.substr(from, to - from);
    if (clean.empty()) {
        lastEvent = "rename blocked: name is empty";
        return false;
    }

    ClusterStar& star = cluster.stars[starIndex];
    const std::string before = star.name;
    star.name = clean + "-" + std::to_string(starIndex);
    // Латинское имя читается кириллицей той же таблицей фонем; набранное
    // кириллицей регистрировать незачем — словарь ключуется латиницей.
    bool ascii = true;
    for (size_t i = 0; i < clean.size(); ++i) {
        if ((unsigned char)clean[i] > 127) { ascii = false; break; }
    }
    if (ascii) I18N::registerProperNoun(clean, starNameCyrillic(clean));
    lastEvent = "renamed " + before + " to " + star.name;
    return true;
}

// Другой борт игрока, стоящий в ТОЙ ЖЕ системе. Первый попавшийся: бортов у
// игрока единицы (каждый стоит лицензии), перебор дешёв и порядок стабилен.
int Game::playerOtherShipHere() const {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return -1;
    const Agent& me = agents[playerAgent];
    if (me.ship.enRoute || !validStar(*this, me.currentStar)) return -1;
    for (size_t i = 0; i < agents.size(); ++i) {
        if (int(i) == playerAgent) continue;
        const Agent& other = agents[i];
        if (!other.playerControlled || other.ship.enRoute) continue;
        if (other.currentStar == me.currentStar) return int(i);
    }
    return -1;
}

// Перевод кредитов между своими бортами. amount <= 0 переводит всё, что есть у
// отдающего. Возвращает реально переведённое.
double Game::playerTransferCredits(double amount, bool give) {
    const int other = playerOtherShipHere();
    if (other < 0) {
        lastEvent = "transfer blocked: no second ship of yours docked here";
        return 0.0;
    }
    Agent& me = agents[playerAgent];
    Agent& mate = agents[other];
    Agent& from = give ? me : mate;
    Agent& to = give ? mate : me;
    const double moved = std::min(from.money, amount > 0.0 ? amount : from.money);
    if (moved <= 0.0) {
        lastEvent = give ? "transfer blocked: your wallet is empty"
                         : "transfer blocked: the other ship has nothing";
        return 0.0;
    }
    from.money -= moved;
    to.money += moved;
    me.lastAction = give ? "handed credits over" : "took credits aboard";
    lastEvent = (give ? "sent " : "drew ") + std::to_string((long long)moved) + " Cr";
    return moved;
}

// ------------------------------------------------- СЧЁТ ФРАКЦИИ ПО СВЕТУ --
// Верхняя оценка «за сколько лет свет отсюда покроет всё скопление»: от звезды
// до центра плюс радиус. Точное значение — максимум по всем звёздам, но считать
// его на каждую транзакцию значит гонять 10 000 расстояний; оценка сверху лишь
// делает банк чуть осторожнее, а это ровно та сторона, где ошибаться безопасно.
double Game::creditClearYears(int starIndex) const {
    if (!validStar(*this, starIndex)) return cluster.radiusLy * 2.0;
    const ClusterStar& star = cluster.stars[starIndex];
    const double dx = star.x - cluster.centreX;
    const double dy = star.y - cluster.centreY;
    const double dz = star.z - cluster.centreZ;
    return std::sqrt(dx * dx + dy * dy + dz * dz) + cluster.radiusLy;
}

double Game::factionTreasuryAt(int factionIndex) const {
    return validFaction(*this, factionIndex) ? factions[factionIndex].treasury : 0.0;
}

double Game::factionCreditsInFlight(int factionIndex) const {
    double sum = 0.0;
    for (size_t i = 0; i < creditFloat.size(); ++i) {
        if (creditFloat[i].faction == factionIndex) sum += creditFloat[i].amount;
    }
    return sum;
}

// Тратить можно только то, о чём знает ВСЁ скопление. Иначе один и тот же
// кредит снимался бы дважды в двух концах одновременно — а быстрее света
// договориться нельзя, это и есть сеттинг.
double Game::factionClearedTreasury(int factionIndex) const {
    return std::max(0.0, factionTreasuryAt(factionIndex) - factionCreditsInFlight(factionIndex));
}

void Game::addCreditFloat(int factionIndex, double amount, int originStar) {
    if (!validFaction(*this, factionIndex) || amount <= 0.0) return;
    // Сверка привязана к ГОДУ: колония телеграфирует прибыль каждый тик, и без
    // склейки поплавок рос бы вектором на десятки тысяч записей за партию.
    // Округление вверх делает банк чуть осторожнее — сторона, где ошибаться
    // безопасно.
    const double clearsAt = std::ceil(time + creditClearYears(originStar));
    for (size_t i = 0; i < creditFloat.size(); ++i) {
        if (creditFloat[i].faction != factionIndex) continue;
        if (std::abs(creditFloat[i].clearsAt - clearsAt) > 1e-9) continue;
        creditFloat[i].amount += amount;
        return;
    }
    CreditFloat pending;
    pending.faction = factionIndex;
    pending.amount = amount;
    pending.clearsAt = clearsAt;
    creditFloat.push_back(pending);
}

double Game::playerAccountDeposit(double amount) {
    if (playerAgent < 0 || playerAgent >= int(agents.size()) || !validFaction(*this, playerFaction)) return 0.0;
    Agent& player = agents[playerAgent];
    if (player.ship.enRoute || !validStar(*this, player.currentStar)) {
        lastEvent = "account blocked: dock in a system first";
        return 0.0;
    }
    const double moved = std::min(player.money, amount > 0.0 ? amount : player.money);
    if (moved <= 0.0) return 0.0;
    player.money -= moved;
    factions[playerFaction].treasury += moved;
    addCreditFloat(playerFaction, moved, player.currentStar);

    player.lastAction = "banked credits";
    lastEvent = "deposited - clears cluster-wide in " +
                std::to_string(int(std::ceil(creditClearYears(player.currentStar)))) + " yr";
    return moved;
}

double Game::playerAccountWithdraw(double amount) {
    if (playerAgent < 0 || playerAgent >= int(agents.size()) || !validFaction(*this, playerFaction)) return 0.0;
    Agent& player = agents[playerAgent];
    if (player.ship.enRoute || !validStar(*this, player.currentStar)) {
        lastEvent = "account blocked: dock in a system first";
        return 0.0;
    }
    const double cleared = factionClearedTreasury(playerFaction);
    const double moved = std::min(cleared, amount > 0.0 ? amount : cleared);
    if (moved <= 0.0) {
        lastEvent = factionCreditsInFlight(playerFaction) > 0.0
                        ? "account blocked: credits still in flight"
                        : "account blocked: nothing cleared";
        return 0.0;
    }
    // Снятие бьёт по счёту немедленно и потому безопасно: за световые годы
    // отсюда нельзя снять то, что уже снято здесь.
    factions[playerFaction].treasury -= moved;
    player.money += moved;
    player.lastAction = "drew on the account";
    return moved;
}

double Game::colonyLedgerAt(int starIndex) const {
    const int colonyIndex = colonyIndexAt(*this, starIndex);
    return colonyIndex < 0 ? 0.0 : colonies[colonyIndex].localLedger;
}

// Доход колонии в кредитах за год — тот же расчёт, что в updateColonies, но без
// шага симуляции. Одна формула на два места: цифра в окне и есть то, что капает.
double Game::colonyIncomeAt(int starIndex) const {
    const int colonyIndex = colonyIndexAt(*this, starIndex);
    if (colonyIndex < 0 || !validStar(*this, starIndex)) return 0.0;
    const Colony& colony = colonies[colonyIndex];
    // Доход — доля ГОДОВОГО ВЫПУСКА, а не формула по `star.industry`. Разница
    // принципиальна: выпуск считает рынок, он живёт вместе с ценами, зависит от
    // построенного и падает, когда склад вычерпан. Прежняя формула была
    // синтетикой поверх сгенерированного поля 0.4..2.6 и не могла дать больше
    // единиц кредитов, что бы в системе ни происходило.
    //
    // `strain` наконец работает в обе стороны: вывез склад подчистую — уронил
    // себе же доход, ровно как обещает §13.5.
    const double health = std::max(0.0, 1.0 - markets[starIndex].strain);
    return systemTurnover(starIndex) * COLONY_OWNER_DUTY * colony.marketAccess * health;
}

double Game::playerColonyDeposit(double amount) {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return 0.0;
    Agent& player = agents[playerAgent];
    if (player.ship.enRoute || !playerOwnsStar(player.currentStar)) {
        lastEvent = "vault blocked: dock in your own system";
        return 0.0;
    }
    const int colonyIndex = colonyIndexAt(*this, player.currentStar);
    if (colonyIndex < 0) return 0.0;
    const double moved = std::min(player.money, amount > 0.0 ? amount : player.money);
    if (moved <= 0.0) return 0.0;
    player.money -= moved;
    colonies[colonyIndex].localLedger += moved;
    player.lastAction = "funded colony";
    return moved;
}

double Game::playerColonyWithdraw(double amount) {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return 0.0;
    Agent& player = agents[playerAgent];
    if (player.ship.enRoute || !playerOwnsStar(player.currentStar)) {
        lastEvent = "vault blocked: dock in your own system";
        return 0.0;
    }
    const int colonyIndex = colonyIndexAt(*this, player.currentStar);
    if (colonyIndex < 0) return 0.0;
    Colony& colony = colonies[colonyIndex];
    const double moved = std::min(colony.localLedger, amount > 0.0 ? amount : colony.localLedger);
    if (moved <= 0.0) return 0.0;
    colony.localLedger -= moved;
    player.money += moved;
    player.lastAction = "drew colony profit";
    return moved;
}

// ---------------------------------------------------------------- ЛИЦЕНЗИЯ --
// Квота растёт с числом лицензий: каждая лицензия — это разрешение на ещё один
// борт, но и обязательство наторговать на него. Расширяться = добровольно
// поднимать себе ставку.
//
// Ставка ЛИНЕЙНА: одна лицензия — одна квота. При `LICENCE_QUOTA_PER_EXTRA = 1`
// формула сводится к `licence().quotaBase * licence().count`, и планка считается в
// уме — три борта, тридцать тысяч (§21: читаемость шкалы важнее точности).
double Game::licenceQuotaTarget() const {
    return licenceQuotaTargetOf(playerFaction);
}

// (§47) Та же планка для ЛЮБОЙ фракции. Правило одно на всех: одна лицензия —
// одна квота, лицензий столько же, сколько бортов.
double Game::licenceQuotaTargetOf(int factionIndex) const {
    const FactionLicence& book = licenceOf(factionIndex);
    return book.quotaBase * (1.0 + LICENCE_QUOTA_PER_EXTRA * double(std::max(0, book.count - 1)));
}

// Медиана стоимости единицы услуги по скоплению. Медиана, а не среднее: рынки
// на отшибе уходят в разы от типичного, и среднее тянул бы хвост.
//
// Берутся ВСЕ рынки, и это не «всеведение»: физическое ограничение здесь не
// «кто про кого знает», а СКОЛЬКО ИДЁТ СВЕТ. Оно уже смоделировано тем, что
// перепись проводится раз в тысячелетие и между сверками уровень — константа.
// Фильтровать выборку по знанию фракции нельзя ещё и практически: на старте
// лицензиару известен ОДИН рынок, и медиана из одного элемента — шум, а не
// замер. Чистая функция состояния: без RNG, без мутаций — §2.3-safe.
double Game::measureClusterServiceCost() const {
    std::vector<double> samples;
    samples.reserve(markets.size());
    for (size_t i = 0; i < markets.size(); ++i) {
        if (markets[i].serviceCostAvg > 0.0) samples.push_back(markets[i].serviceCostAvg);
    }
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

// Уровень — отношение к ЯКОРЮ `ECON_CREDITS_PER_SERVICE`, и это принципиально:
// на нём висит не только ставка тарифа, но и ценовой привод `gLevelDrive`,
// который единственный закрепляет НОМИНАЛЬНЫЙ уровень цен (доли замещения
// зависят только от относительных цен, поэтому номинал сам по себе свободно
// дрейфует — см. market.cpp). Мерить уровень «от начала партии» было заманчиво
// (1.0 по построению), но тогда привод обнуляется и якорь перестаёт держать
// цены вовсе: замер показал падение котировок на треть.
double Game::measureClusterPriceLevel() const {
    const double now = measureClusterServiceCost();
    if (now <= 0.0) return clusterPriceLevel;
    return std::max(1e-6, now / ECON_CREDITS_PER_SERVICE);
}

void Game::updateLicence(double dt) {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return;

    // Ставка следует за тем, насколько скопление подорожало С НАЧАЛА ПАРТИИ, а
    // не за отношением к якорю: якорь — точка отсчёта для ЦЕН, а не для денег
    // игрока. Отсюда `LICENCE_TARIFF_BASE` наконец означает написанное — ставку
    // первого тысячелетия, — а клампы становятся границами дрейфа вместо
    // вечного пола. Между сверками величина КОНСТАНТА, поэтому строка
    // идемпотентна и сама ставит ставку на место после загрузки сейва.
    const double drift = clusterPriceBase > 0.0 ? clusterPriceLevel / clusterPriceBase : 1.0;
    // (§47) Ставка — свойство СКОПЛЕНИЯ, а не капитана: она одна для всех
    // шестнадцати книг. Хранится по строкам, потому что строка и есть лицензия.
    resizeFactionLicence();
    const double clusterRate = std::max(LICENCE_TARIFF_MIN,
                                        std::min(LICENCE_TARIFF_MAX, LICENCE_TARIFF_BASE * drift));
    for (size_t f = 0; f < factionLicence.size(); ++f) factionLicence[f].tariffRate = clusterRate;

    if (time < licence().periodEnd) return;

    const double target = licenceQuotaTarget();
    // Тысячелетний рубеж: свет доходит от края скопления до края за век с лишним,
    // поэтому цены сверяют разом и с той же оказией пересматривают квоты.
    // ЗДЕСЬ И ТОЛЬКО ЗДЕСЬ пересчитывается денежный уровень скопления. Сверка
    // перестаёт быть таймером и становится событием: ставка тарифа меняется
    // ступенькой ровно в тот момент, о котором игру предупредили в прологе.
    // Банк СВЕРЯЕТ курс, а не переставляет его рывком: новый уровень берётся
    // на полпути к замеру. Это не украшение — контур, который меряют раз в
    // тысячу лет и применяют на полную, входит в устойчивый двухтактный цикл
    // (замер без сглаживания: 0.265 ↔ 0.428 ↔ 0.297 ↔ 0.430, ставка прыгала
    // 0.080 → 0.129 → 0.090 → 0.130 идеально через раз, что читается как
    // артефакт, а не как экономика).
    clusterPriceLevel += (measureClusterPriceLevel() - clusterPriceLevel) * 0.5;
    marketSetClusterLevel(clusterPriceLevel);
    const double reconciledDrift = clusterPriceBase > 0.0 ? clusterPriceLevel / clusterPriceBase : 1.0;
    const double reconciledRate = std::max(LICENCE_TARIFF_MIN,
                                           std::min(LICENCE_TARIFF_MAX, LICENCE_TARIFF_BASE * reconciledDrift));
    for (size_t f = 0; f < factionLicence.size(); ++f) factionLicence[f].tariffRate = reconciledRate;
    pushNews("Millennial relativistic market correction: cluster prices at " +
             std::to_string(int(reconciledDrift * 100.0 + 0.5)) + "% of founding, tariff now " +
             std::to_string(int(licence().tariffRate * 1000.0 + 0.5)) + " per mille.", 1);
    if (licence().quotaPaid + 1e-6 >= target) {
        licence().periodsMet += 1;
        pushNews("Licence renewed: quota met (" + std::to_string(int(licence().quotaPaid)) +
                 "/" + std::to_string(int(target)) + " Cr).", 4);
        lastEvent = "licence renewed";
    } else if (!licence().revoked) {
        // Отзыв: торговля замерзает, пока игрок не выкупит лицензию. Добыча (M),
        // контракты и локальный полёт продолжают работать — есть чем откопаться.
        const double shortfall = target - licence().quotaPaid;
        licence().revoked = true;
        licence().buyback = std::max(LICENCE_BUYBACK_MIN, shortfall * LICENCE_BUYBACK_K);
        pushNews("LICENCE REVOKED: quota short by " + std::to_string(int(std::ceil(shortfall))) +
                 " Cr. Trading frozen until bought back (" + std::to_string(int(licence().buyback)) + " Cr).", 2);
        lastEvent = "licence revoked - trading frozen";
    }
    // Планка ползёт вверх независимо от исхода: скопление богатеет, и вечно жить
    // на одном отработанном маршруте не выйдет — геймплей обязан двигаться.
    licence().quotaBase *= LICENCE_QUOTA_GROWTH;
    licence().quotaPaid = 0.0;
    licence().periodEnd = time + LICENCE_PERIOD_YEARS;

    // (§47) Тот же рубеж закрывает период и пятнадцати державам. Сверка одна на
    // скопление (уровень цен пересчитан выше ОДИН раз), а книги — у каждого свои.
    for (size_t f = 0; f < factions.size(); ++f) {
        if (int(f) == playerFaction || int(f) == clearingFaction) continue;
        closeFactionLicencePeriod(int(f));
    }
}

// Конец отчётного периода державы. Правило то же, что у игрока, и отличается
// ровно одним: у ИИ нет кнопки, поэтому выкуп он платит из казны сам и сразу,
// как только она это тянет.
//
// ⚠️ Держава, которая не может выкупиться, ОСТАЁТСЯ отозванной, и её торговцы
// стоят. Это не тупик, а давление: казна продолжает капать с колоний, и рано
// или поздно выкуп становится по карману. Автоматическое прощение долга здесь
// сделало бы отзыв бессмысленным для пятнадцати из шестнадцати.
void Game::closeFactionLicencePeriod(int factionIndex) {
    if (!validFaction(*this, factionIndex)) return;
    FactionLicence& book = licenceOf(factionIndex);
    // ⚠️ Здесь `count` БОЛЬШЕ НЕ ВЫВОДИТСЯ из размера флота. Выводить значило бы
    // выдавать лицензии даром: у игрока каждая покупается за деньги
    // (`playerBuyLicence`), и держава, получающая их бесплатно, — не «такой же
    // игрок». Лицензии растут только через `factionTryGrowFleet`.
    const double target = licenceQuotaTargetOf(factionIndex);
    if (book.quotaPaid + 1e-6 >= target) {
        book.periodsMet += 1;
    } else if (!book.revoked) {
        const double shortfall = target - book.quotaPaid;
        book.revoked = true;
        book.buyback = std::max(LICENCE_BUYBACK_MIN, shortfall * LICENCE_BUYBACK_K);
    }
    if (book.revoked && book.buyback > 0.0) {
        Faction& power = factions[size_t(factionIndex)];
        if (power.treasury >= book.buyback) {
            power.treasury -= book.buyback;
            if (validFaction(*this, clearingFaction)) factions[size_t(clearingFaction)].treasury += book.buyback;
            book.revoked = false;
            book.buyback = 0.0;
        }
    }
    book.quotaBase *= LICENCE_QUOTA_GROWTH;
    book.quotaPaid = 0.0;
    book.periodEnd = time + LICENCE_PERIOD_YEARS;
}

bool Game::playerBuybackLicence() {
    if (!licence().revoked) {
        lastEvent = "licence is valid";
        return false;
    }
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return false;
    Agent& player = agents[playerAgent];
    if (player.money < licence().buyback) {
        lastEvent = "buyback needs " + std::to_string(int(std::ceil(licence().buyback))) + " Cr";
        return false;
    }
    player.money -= licence().buyback;
    // Выкуп идёт в казну лицензиара — деньги не исчезают из экономики.
    if (validFaction(*this, clearingFaction)) factions[clearingFaction].treasury += licence().buyback;
    licence().revoked = false;
    licence().buyback = 0.0;
    licence().quotaPaid = 0.0;
    licence().periodEnd = time + LICENCE_PERIOD_YEARS;
    pushNews("Licence bought back. Trading resumed.", 4);
    lastEvent = "licence bought back";
    return true;
}

// Биржевая сводка: чем торговать прямо сейчас. Считается по ЖИВОМУ рынку системы,
// где игрок стоит (цена покупки честная, с проскальзыванием §10.3), и по ИЗВЕСТНЫМ
// ценам соседей — то есть по данным, которые могут быть старыми. Возраст и
// уверенность возвращаются вместе со сделкой, чтобы UI показал их игроку: сводка
// это подсказка разведки, а не гарантия. Ничего не мутирует, RNG не трогает.
int Game::playerSurveyedMarketCount() const {
    int n = 0;
    for (int i = 0; i < int(cluster.stars.size()); ++i) {
        if (playerKnowsMarket(i)) ++n;
    }
    return n;
}

// Единственное место, где живёт МОДЕЛЬ цены чужого рынка. Сейчас модель — это само
// наблюдение: «столько там стоило, когда я там был». Насколько ей верить сегодня,
// говорит отдельно confidence (exp(-age/tau)).
//
// ⚠️ ИЗМЕРЕНО И ОТВЕРГНУТО (2026-08-04): экстраполяция «сползанием к опорной цене
// скопления» с весом confidence выглядела элегантно (симуляция действительно тянет
// цены к опорной через importBandDrive), но на замере оказалась ХУЖЕ простого
// снимка. Медианная ошибка в разах по 200 разведанным рынкам:
//     возраст 20 лет — снимок 1.45x, сползание 1.59x (−31% точности)
//     возраст 40 лет — снимок 1.55x, сползание 1.63x (−14%)
//     возраст 60 лет — снимок 1.58x, сползание 1.62x (−6%)
// Причина содержательная: цены систем УСТОЙЧИВЫ. Система, дорогая по железу, дорога
// по железу и через век — её недра и нужды не меняются. Сползание к средней по
// скоплению выбрасывает ровно эту информацию. Не повторяй этот заход без замера.
//
// Куда копать за настоящей моделью: в системе знаний уже лежат supplyPressure и
// demandPressure на момент наблюдения — это НАПРАВЛЕНИЕ движения цены, а не уровень.
// Прогноз по тренду («видел дефицит ⇒ с тех пор подорожало») может дать реальный
// выигрыш там, где возврат к среднему проиграл. Проверять тем же стендом.
double Game::playerProjectedPrice(int starIndex, int elementIndex) const {
    return playerKnownPrice(starIndex, elementIndex);
}

std::vector<ArbitrageDeal> Game::playerArbitrageBoard(int originStar, int maxDeals, int elementFilter) const {
    std::vector<ArbitrageDeal> deals;
    if (!validStar(*this, originStar) || originStar >= int(markets.size())) return deals;
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return deals;

    const Agent& player = agents[playerAgent];
    const Market& home = markets[originStar];
    const ClusterStar& hs = cluster.stars[originStar];
    const double freeMass = std::max(0.0, player.ship.cargoCapacity - shipCargoMass(player.ship));
    if (freeMass <= 0.0 || player.money <= 0.0) return deals;

    const double sellTariff = licence().tariffRate;    // лицензионный тариф удержат с продажи
    const int elems = std::min(int(elementCount()), int(home.prices.size()));
    // При фильтре по одному элементу список короткий по построению (не больше числа
    // разведанных систем), поэтому режем его гораздо мягче — иначе фильтр «покажи всё
    // по железу» упирался бы в тот же потолок, что и общий список.
    const int keep = maxDeals > 0 ? maxDeals : (elementFilter >= 0 ? 2000 : 200);
    const int firstElem = elementFilter >= 0 ? elementFilter : 0;
    const int lastElem = elementFilter >= 0 ? elementFilter + 1 : elems;
    if (firstElem < 0 || firstElem >= elems) return deals;

    // Сколько чего можно увезти — зависит ТОЛЬКО от элемента и кошелька, а не от
    // пункта назначения. Считаем один раз на элемент, а не заново для каждой из
    // тысяч разведанных систем: раньше это был главный источник стоимости.
    struct Leg { double maxUnits; double buyBase; bool usable; };
    std::vector<Leg> legs(static_cast<size_t>(elems));   // скобки без cast'а — vexing parse
    for (int e = 0; e < elems; ++e) {
        Leg& leg = legs[size_t(e)];
        leg.usable = false;
        leg.buyBase = home.prices[e];
        leg.maxUnits = 0.0;
        if (leg.buyBase <= 0.001 || home.supply[e].amount < 1.0) continue;
        const double unitMass = resourceUnitMassByIndex(e);
        if (unitMass <= 0.0) continue;
        double u = std::min(freeMass / unitMass, home.supply[e].amount);
        if (u <= 0.01) continue;
        // Верхняя граница по деньгам — по ФАКТИЧЕСКОЙ цене исполнения (она выше
        // котировки: покупка сама разгоняет рынок, §10.3).
        for (int pass = 0; pass < 2 && u > 0.0; ++pass) {
            const double avg = home.executionPrice(e, u, false);
            u = std::min(u, player.money / std::max(1e-9, avg));
        }
        if (u <= 0.01) continue;
        leg.maxUnits = u;
        leg.usable = true;
    }

    // Радиуса поиска НЕТ: сводка охватывает ВСЁ, что игрок когда-либо разведал, и
    // растёт вместе с его картой — до всех 10 000 систем. Дальние строки не прячем,
    // а показываем с дистанцией: везти за 60 ly или нет — решает игрок.
    // Порог `worst` — прибыль худшей строки в текущей top-N: пара, которая не может
    // его перебить даже в идеале, отбрасывается до дорогого перебора объёмов.
    // Дорога считается по кораблю, каким он ВЫЙДЕТ из порта (§46) — той же
    // меркой, что и в совете: иначе сводка и совет в одной системе называли бы
    // разную цену одного и того же плеча.
    const Ship planned = shipAsItWillLeave(player.ship, false);
    const double tankPropellantMass = shipPropellantMix(planned).mass;
    const double tankFuelMass = shipFuelMix(planned).mass;
    Ship scratch = planned;

    double worst = 0.0;
    for (int target = 0; target < int(cluster.stars.size()) && target < int(markets.size()); ++target) {
        if (target == originStar) continue;
        if (!playerKnowsMarket(target)) continue;   // неразведанное не показываем — знание и есть ресурс

        const double distance = distanceBetween(hs, cluster.stars[target]);
        const double age = playerKnownMarketAge(target);
        const Market& tm = markets[target];
        for (int e = firstElem; e < lastElem; ++e) {
            const Leg& leg = legs[size_t(e)];
            if (!leg.usable) continue;
            // Цена назначения — МОДЕЛЬ на сейчас, а не снимок: живого канала с чужой
            // системой нет, есть «что я видел» плюс сползание к опорной цене по мере
            // старения (см. комментарий к ArbitrageDeal).
            const double sellPrice = playerProjectedPrice(target, e);
            if (sellPrice <= 0.0) continue;
            // Быстрый отсев «там не дороже» — только для общего списка. Под фильтром
            // игрок хочет видеть ВСЕ разведанные системы по элементу, включая те, где
            // продавать в убыток: это и есть карта «где почём».
            if (elementFilter < 0 && sellPrice <= leg.buyBase * 1.05) continue;

            // Под фильтром по одному элементу показываем ВСЁ разведанное по нему —
            // и убыточное тоже: игрок просил полную картину «где почём», а не только
            // готовые сделки. Без фильтра список ранжирован по прибыли и отсечения
            // обязательны, иначе он вырождается в 390 000 строк шума.
            if (elementFilter < 0) {
                // Оптимистичная оценка сверху: продали ВЕСЬ объём по модельной цене без
                // проскальзывания, купили по котировке. Реальная прибыль всегда ниже,
                // поэтому не перебивший порог кандидат не может попасть в top-N.
                const double bound = leg.maxUnits * (sellPrice * (1.0 - sellTariff) - leg.buyBase);
                if (bound <= worst) continue;
            }

            // «Полный трюм» не оптимум: при тонком рынке назначения прибыль по объёму
            // имеет МАКСИМУМ (покупка дорожает, продажа дешевеет). Ищем его перебором —
            // сводка подсказывает не только КУДА, но и СКОЛЬКО.
            const double targetDepth = tm.depthOf(e);
            double units = 0.0, cost = 0.0, profit = 0.0;
            bool haveBest = false;
            for (int step = 1; step <= 10; ++step) {
                const double u = leg.maxUnits * double(step) / 10.0;
                const double c = u * home.executionPrice(e, u, false);
                const double r = u * sellPrice * marketExecutionFactor(u, targetDepth, true) * (1.0 - sellTariff);
                if (!haveBest || r - c > profit) { profit = r - c; units = u; cost = c; haveBest = true; }
            }
            if (units <= 0.01) continue;
            // Под фильтром пропускаем только заведомый мусор (нулевой объём); без
            // фильтра — всё, что не бьёт текущий порог top-N.
            //
            // ⚠️ Порог `worst` сравнивается с прибылью ДО вычета дороги, а
            // хранится прибыль ПОСЛЕ. Это нарочно: валовая прибыль — верхняя
            // оценка чистой, значит отсев остаётся осторожным и не выбросит
            // строку, которая могла бы победить. Дорога считается только для
            // тех строк, что уже прошли порог: маршрут по всем разведанным
            // системам стоил бы дороже самой сводки.
            if (elementFilter < 0 && profit <= worst) continue;

            // (§46) ДОРОГА, СРОК И ДОСТИЖИМОСТЬ — той же меркой, что в совете.
            const Ship& carried = shipCarrying(planned, e, units * resourceUnitMassByIndex(e), scratch);
            const RouteCost rc = plannedRouteCost(*this, carried, originStar, target);
            const bool fits = rc.feasible &&
                              rc.propellantMass <= tankPropellantMass + 1e-6 &&
                              rc.fuelMass <= tankFuelMass + 1e-6;
            const double years = fits ? plannedRouteTravelTime(*this, carried, originStar, target) : 0.0;
            const double fuel = fits ? burnCost(*this, carried, originStar, rc) : 0.0;
            // Строка, куда этот корпус с этим грузом не долетит, — не сделка, а
            // ловушка. Под фильтром по элементу оставляем: там игрок просит
            // карту «где почём», а не список готовых рейсов.
            if (elementFilter < 0 && !fits) continue;

            ArbitrageDeal deal;
            deal.element = e;
            deal.targetStar = target;
            deal.buyPrice = cost / units;
            deal.sellPrice = sellPrice;
            deal.observedPrice = playerKnownPrice(target, e);
            deal.units = units;
            deal.profit = profit - fuel;
            deal.fuelCost = fuel;
            deal.years = years;
            deal.perYear = years > 0.0 ? deal.profit / years : 0.0;
            deal.distanceLy = distance;
            deal.ageYears = age;
            deal.confidence = playerKnownMarketConfidence(target, e);
            deals.push_back(deal);

            // Держим список ограниченным: 390 000 строк не влезают ни в память, ни в
            // глаза, а листать имеет смысл лучшие. Подрезаем вдвое реже, чем растём.
            // Под фильтром порог `worst` не двигаем — иначе он снова отрежет убыточные.
            if (elementFilter < 0 && int(deals.size()) >= keep * 2) {
                std::partial_sort(deals.begin(), deals.begin() + keep, deals.end(),
                                  [](const ArbitrageDeal& a, const ArbitrageDeal& b) { return a.profit > b.profit; });
                deals.resize(size_t(keep));
                worst = deals.back().profit;
            }
        }
    }

    std::sort(deals.begin(), deals.end(), [](const ArbitrageDeal& a, const ArbitrageDeal& b) {
        return a.profit > b.profit;
    });
    if (int(deals.size()) > keep) deals.resize(size_t(keep));
    return deals;
}

TradeRun Game::playerBestRun(int originStar, int nearestSystems, bool knownOnly) const {
    TradeRun best;
    if (!validStar(*this, originStar) || originStar >= int(markets.size())) return best;
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return best;

    const Agent& player = agents[playerAgent];
    const Market& home = markets[originStar];
    const ClusterStar& hs = cluster.stars[originStar];
    const double freeMass = std::max(0.0, player.ship.cargoCapacity - shipCargoMass(player.ship));
    if (freeMass <= 0.0 || player.money <= 0.0) return best;

    const double sellTariff = licence().tariffRate;
    const int elems = std::min(int(elementCount()), int(home.prices.size()));

    // Что и сколько отсюда вообще можно увезти — считается один раз на элемент,
    // как в `playerArbitrageBoard`: объём зависит от кошелька и трюма, а не от цели.
    struct Leg { double maxUnits; bool usable; };
    std::vector<Leg> legs(static_cast<size_t>(elems));
    for (int e = 0; e < elems; ++e) {
        Leg& leg = legs[size_t(e)];
        leg.usable = false;
        leg.maxUnits = 0.0;
        if (home.prices[e] <= 0.001 || home.supply[e].amount < 1.0) continue;
        const double unitMass = resourceUnitMassByIndex(e);
        if (unitMass <= 0.0) continue;
        double u = std::min(freeMass / unitMass, home.supply[e].amount);
        for (int pass = 0; pass < 2 && u > 0.0; ++pass) {
            const double avg = home.executionPrice(e, u, false);
            u = std::min(u, player.money / std::max(1e-9, avg));
        }
        if (u <= 0.01) continue;
        leg.maxUnits = u;
        leg.usable = true;
    }

    // Ближайшие системы. Cr/год сама душит дальние цели, поэтому перебирать всё
    // скопление незачем: замер на 8192 системах — 128 ближайших лежат в 1 мс и
    // всегда содержат победителя, потому что он в двух-трёх световых годах.
    std::vector<std::pair<double, int> > near;
    near.reserve(size_t(std::max(1, nearestSystems)) * 2);
    for (int i = 0; i < int(cluster.stars.size()) && i < int(markets.size()); ++i) {
        if (i == originStar) continue;
        if (knownOnly && !playerKnowsMarket(i)) continue;
        near.push_back(std::make_pair(distanceBetween(hs, cluster.stars[i]), i));
    }
    if (near.empty()) return best;
    const size_t keep = std::min(near.size(), size_t(std::max(1, nearestSystems)));
    std::partial_sort(near.begin(), near.begin() + long(keep), near.end());
    near.resize(keep);

    // ⚠️ Совет считается по кораблю, каким он ВЫЙДЕТ из порта (§46): трюм полон,
    // баки залиты. Прежде считалось по тому, что стоит у причала, и это давало
    // круг — совет требует топлива, топливо требует цели, цель даёт совет: на
    // пустом баке не проходима ни одна цель (замер: 0 из 40), а бак пуст после
    // КАЖДОГО рейса. Единственным выходом из круга оставалась кнопка заправки,
    // то есть самая дорогая покупка в игре.
    const Ship planned = shipAsItWillLeave(player.ship, false);

    // Сколько расходников ВЛЕЗАЕТ в ёмкости. `planned` залит под пробку, значит
    // это и есть потолок бака и бункера в массе.
    const double tankPropellantMass = shipPropellantMix(planned).mass;
    const double tankFuelMass = shipFuelMix(planned).mass;
    Ship scratch = planned;   // буфер под корабль с загрузкой; см. `shipCarrying`

    // Запасной ответ: лучший рейс из тех, что окупают только КАССОВЫЙ расход.
    // Он идёт в дело, когда ни один рейс не окупает собственного топлива —
    // обычно это самое начало партии, где бак пришёл вместе с корпусом.
    TradeRun prepaid;

    for (size_t k = 0; k < near.size(); ++k) {
        const int target = near[k].second;
        // Цель, до которой маршрут не строится, — не совет, а ловушка (§12.5):
        // корабль с такими баками туда просто не полетит.
        // Дешёвый предварительный отсев: если туда не летит даже ПУСТОЙ корпус,
        // с грузом тем более. Настоящая проверка — ниже, с реальной загрузкой.
        const RouteCost legCost = plannedRouteCost(*this, planned, originStar, target);
        if (!legCost.feasible) continue;
        if (plannedRouteTravelTime(*this, planned, originStar, target) <= 0.0) continue;
        // ⚠️ ДОРОГА ТОЖЕ СТОИТ. Совет считал `выручка - закупка` и молчал про
        // топливо, хотя планировщик NPC (`findBestTrade`) вычитает его с самого
        // начала. Замер: расходники съедают 12…22% обещанного, а на дальнем
        // плече могут съесть и всё — то есть Тимертия звала в рейсы, которые
        // сама же делала убыточными.
        // ⚠️ ДВЕ МЕРКИ ДОРОГИ, и обе нужны (§46).
        //
        //  • `legBurn` — что СГОРИТ, по станционной цене. Это настоящая цена
        //    рейса, и по ней рейсы РАНЖИРУЮТСЯ. Прежняя единственная мерка —
        //    недостача до маршрута — делала уже залитое топливо бесплатным:
        //    игрок, нажавший кнопку заправки до вопроса (а новелла учит именно
        //    этому), получал дорогу «за ноль», и совет выбирал рейсы, которые
        //    сам же делал убыточными — 16% валовой прибыли в среднем, три рейса
        //    из 69 в чистый минус, худший −15 154 Cr.
        //
        //  • `legRefill` — что придётся ЗАПЛАТИТЬ здесь и сейчас. По ней рейс
        //    ДОПУСКАЕТСЯ. Стартовый бак пришёл вместе с корпусом, он уже
        //    оплачен, и мерить первый рейс ценой его замещения значит объявить
        //    партию невозможной с первого хода: замер по сиду 1234 — 0 рейсов
        //    из 25 при кошельке в 100 Cr.
        //
        // Порядок такой: сперва ищем рейс, который окупает СВОЁ ЖЕ топливо;
        // если такого нет вовсе — лучший из тех, что окупают кассовый расход.
        const Market& tm = markets[target];
        for (int e = 0; e < elems; ++e) {
            const Leg& leg = legs[size_t(e)];
            if (!leg.usable) continue;
            // Разведанное — по МОДЕЛИ (наблюдение стареет), обучающее всезнание —
            // по живой котировке: врать первой целью партии нельзя.
            const double sellPrice = knownOnly ? playerProjectedPrice(target, e)
                                               : (e < int(tm.prices.size()) ? tm.prices[e] : 0.0);
            if (sellPrice <= 0.0) continue;

            const double targetDepth = tm.depthOf(e);
            const double unitMass = resourceUnitMassByIndex(e);
            for (int step = 1; step <= 10; ++step) {
                const double u = leg.maxUnits * double(step) / 10.0;
                if (u <= 0.01) continue;
                // ⚠️ ПРОХОДИМОСТЬ МЕРИТСЯ С ТОЙ ЗАГРУЗКОЙ, КОТОРУЮ СОВЕТУЕМ.
                // Совет проверял маршрут на ПУСТОМ корпусе, а рекомендовал
                // полный трюм. На больших корпусах это расходится вдвое-вчетверо:
                // Megafreighter физически увозит 54% своего трюма, Giga — 24%.
                // Замер прогона на 20 000 лет: 523 сорванных вылета на 812
                // состоявшихся рейсов (39%), и сообщение «propellant short 2»
                // не говорило, что лечится это выгрузкой половины трюма.
                // Теперь советчик сам называет ту загрузку, которая долетит.
                const Ship& carried = shipCarrying(planned, e, u * unitMass, scratch);
                const RouteCost rc = plannedRouteCost(*this, carried, originStar, target);
                if (!rc.feasible) continue;
                // Стена достижимости `k < 1` — не единственный предел: нужное
                // рабочее тело обязано ВЛЕЗТЬ В БАК. `shipRouteCost` объём бака
                // не смотрит вовсе, и именно этот зазор давал «short 2» при
                // полном баке и сотне миллионов в кармане.
                if (rc.propellantMass > tankPropellantMass + 1e-6) continue;
                if (rc.fuelMass > tankFuelMass + 1e-6) continue;
                const double years = plannedRouteTravelTime(*this, carried, originStar, target);
                if (years <= 0.0) continue;
                const double legBurn = burnCost(*this, carried, originStar, rc);
                // ⚠️ ПО НАСТОЯЩЕМУ БОРТУ, а не по `carried`. `shipAsItWillLeave` заливает
        // баки под пробку, а `refillCost` считает недостачу — по залитому корпусу
        // она тождественно НОЛЬ (замер: 0.000000 на всех 200 целях трёх миров при
        // настоящей недостаче 42 201 Cr). Тогда `cash == выручка − закупка`, то
        // есть запасной ответ ранжировался ВАЛОВОЙ прибылью без дороги — ровно
        // тем, что §42 назвал главной ошибкой советчика. Замер последствий: 13
        // ответов из 25 обещали меньше, чем сожжёт тот же рейс, худшее 32x.
        // Груз на цену долива не влияет: `refillCost` смотрит только на баки.
        const double legRefill = refillCost(*this, player.ship, originStar, rc);
                const double avgBuy = home.executionPrice(e, u, false);
                const double cost = u * avgBuy;
                const double rev = u * sellPrice * marketExecutionFactor(u, targetDepth, true) * (1.0 - sellTariff);
                const double net = rev - cost - legBurn;
                const double cash = rev - cost - legRefill;
                TradeRun* slot = 0;
                double score = 0.0, profit = 0.0;
                if (net > 0.0 && net / years > best.perYear) {
                    slot = &best; score = net / years; profit = net;
                } else if (best.valid == false && cash > 0.0 && cash / years > prepaid.perYear) {
                    slot = &prepaid; score = cash / years; profit = cash;
                }
                if (!slot) continue;
                slot->element = e;
                slot->targetStar = target;
                slot->units = u;
                slot->buyPrice = avgBuy;
                slot->sellPrice = sellPrice;
                slot->profit = profit;
                slot->years = years;
                slot->perYear = score;
                slot->distanceLy = near[k].first;
                slot->valid = true;
            }
        }
    }

    if (!best.valid && prepaid.valid) best = prepaid;

    // ПОЧЕМУ здесь дёшево. Величина осмысленна только там, где элемент вообще
    // потребляют: у синтетических сверхтяжёлых потребление нулевое, и отношение
    // улетает в 1e12 — такое не произносят, а молчат о нём (см. ui.cpp).
    if (best.valid) {
        const double cons = best.element < int(home.demandRate.size()) ? home.demandRate[best.element] : 0.0;
        const double prod = best.element < int(home.productionRate.size()) ? home.productionRate[best.element] : 0.0;
        best.pressure = cons > 1e-6 ? prod / cons : -1.0;
    }
    return best;
}

// (§52) Пошлина владельца порта для борта игрока. Обёртка нужна затем, что сам
// `tariffFor` объявлен в безымянном пространстве имён этого файла, а платить
// владельцу обязан и ремонт, живущий в `combat.cpp`. Ставка та же, что у
// заправки (0.014): порт берёт с услуги столько же, сколько с товара.
double Game::playerPortTariff(int starIndex) const {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return 0.0;
    return tariffFor(*this, starIndex, agents[size_t(playerAgent)].ship.ownerFaction, 0.014);
}

// (§52) РЕПУТАЦИЯ РАСТИТ ФЛОТ — ЧЕРЕЗ ЗАЛОГ, А НЕ ЧЕРЕЗ РАЗРЕШЕНИЕ.
//
// Развилка стояла так: флот — это «сколько купишь» или «скольким доверяют»?
// Было первое, и репутация не влияла на него никак: `playerBuyLicence` смотрел
// в кошелёк и только. Второе крыло — «тир поднимает разрешённое число бортов» —
// отвергнуто: тогда лицензия перестала бы быть покупкой, а квота (которая
// растёт с каждой лицензией, `LICENCE_QUOTA_PER_EXTRA`) осталась бы висеть на
// шее у того, кто её не выбирал.
//
// Взято то, что честнее по смыслу самой бумаги: лицензия — это ЗАЛОГ, который
// палата берёт за право возить с оборотом. Капитан с доказанной историей сдач —
// меньший риск, и залог с него меньше. Деньги остаются входным билетом, но
// репутация их удешевляет.
//
// Ни одного нового числа: берётся ГОТОВЫЙ `factionJobTier` (тот же корень из
// доли пути, §24) той державы, что доверяет игроку больше прочих — она за него
// и ручается. Делитель `1 + тир` структурный: без репутации цена прежняя, на
// вершине лестницы — половина.
//
// Замер (`journey_probe`, сид 1234, 1200 звёзд, 60 рейсов): второй корпус
// брался на 44-м рейсе к 1519 году; после правки — см. §52 канона.
double Game::playerBestTrustTier() const {
    double best = 0.0;
    for (size_t f = 0; f < factionReputation.size(); ++f) {
        best = std::max(best, factionJobTier(int(f)));
    }
    return best;
}

double Game::licencePrice() const {
    const double bond = licence().quotaBase * LICENCE_PRICE_K * double(std::max(1, licence().count));
    return bond / (1.0 + playerBestTrustTier());
}

// (§47) Цена следующей лицензии для ЛЮБОЙ из шестнадцати — та же формула, что и
// у игрока: чем больше уже держишь, тем дороже следующая.
double Game::licencePriceOf(int factionIndex) const {
    const FactionLicence& book = licenceOf(factionIndex);
    return book.quotaBase * LICENCE_PRICE_K * double(std::max(1, book.count));
}

// Сколько у державы лицензий БЕЗ борта. Зеркало `playerFreeLicences()`:
// у игрока борта считаются по `playerControlled`, у державы — по её флоту.
int Game::factionFreeLicences(int factionIndex) const {
    if (!validFaction(*this, factionIndex)) return 0;
    const int hulls = int(factions[size_t(factionIndex)].fleetAgents.size());
    return std::max(0, licenceOf(factionIndex).count - std::max(1, hulls));
}

double Game::licenceSettleCost() const {
    const double remaining = std::max(0.0, licenceQuotaTarget() - licence().quotaPaid);
    return remaining * LICENCE_SETTLE_K;
}

int Game::playerShipCount() const {
    int ships = 0;
    for (const Agent& a : agents) {
        if (a.playerControlled) ships += 1;
    }
    return std::max(1, ships);
}

int Game::playerFreeLicences() const {
    return std::max(0, licence().count - playerShipCount());
}

bool Game::playerBuyLicence() {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return false;
    Agent& player = agents[playerAgent];
    const double price = licencePrice();
    if (player.money < price) {
        lastEvent = "licence needs " + std::to_string(int(std::ceil(price))) + " Cr";
        return false;
    }
    player.money -= price;
    if (validFaction(*this, clearingFaction)) factions[clearingFaction].treasury += price;
    licence().count += 1;
    pushNews("Trading licence #" + std::to_string(licence().count) + " acquired. Quota is now " +
             std::to_string(int(licenceQuotaTarget())) + " Cr per period.", 4);
    lastEvent = "licence acquired - one more hull permitted";
    return true;
}

bool Game::playerSettleQuota() {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return false;
    // Погашать квоту ОТОЗВАННОЙ лицензии бессмысленно: торговлю разморозит
    // только выкуп (`playerBuybackLicence`), а квота отозванной лицензии ни на
    // что не влияет. Раньше кнопка работала и в этом состоянии — игрок платил
    // полторы цены остатка и не получал ровно ничего.
    if (licence().revoked) {
        lastEvent = "licence revoked: buy it back first (F2)";
        return false;
    }
    const double remaining = std::max(0.0, licenceQuotaTarget() - licence().quotaPaid);
    if (remaining <= 0.0) {
        lastEvent = "quota already met";
        return false;
    }
    Agent& player = agents[playerAgent];
    const double cost = licenceSettleCost();
    if (player.money < cost) {
        lastEvent = "settlement needs " + std::to_string(int(std::ceil(cost))) + " Cr";
        return false;
    }
    player.money -= cost;
    if (validFaction(*this, clearingFaction)) factions[clearingFaction].treasury += cost;
    licence().quotaPaid += remaining;
    pushNews("Quota settled in cash for " + std::to_string(int(std::ceil(cost))) + " Cr.", 4);
    lastEvent = "quota settled";
    return true;
}

// --- Акции держав (§33) -----------------------------------------------------

void Game::resizeShareBooks() {
    const size_t n = factions.size();
    if (factionShares.size() != n * n) factionShares.assign(n * n, 0.0);
    // Выдержки РАЗВЕДЕНЫ по фазе: иначе все пятнадцать держав покупают в один
    // год, потом тысячу лет молчат — пульс вместо истории.
    if (factionNextSystemBuy.size() != n) {
        // ⚠️ Фазы раздаются ТОЛЬКО при первом заведении. Пройтись по всему
        // вектору при любом изменении размера значило бы сбрасывать уже идущие
        // выдержки — держава, только что купившая систему, получила бы право
        // купить снова.
        const bool fresh = factionNextSystemBuy.empty();
        factionNextSystemBuy.resize(n, 0.0);
        if (fresh) {
            for (size_t f = 0; f < n; ++f) {
                factionNextSystemBuy[f] = SYSTEM_BUY_INTERVAL_YEARS * double(f) / double(n > 0 ? n : 1);
            }
        }
    }
    if (factionNextFleetBuy.size() != n) {
        const bool fresh = factionNextFleetBuy.empty();
        factionNextFleetBuy.resize(n, 0.0);
        if (fresh) {
            for (size_t f = 0; f < n; ++f) {
                factionNextFleetBuy[f] = FLEET_BUY_INTERVAL_YEARS * double(f) / double(n > 0 ? n : 1);
            }
        }
    }
    if (factionBook.size() != n) factionBook.assign(n, 0.0);
    if (factionIncome.size() != n) factionIncome.assign(n, 0.0);
    if (factionBookAt.size() != n) factionBookAt.assign(n, -1.0e18);
    if (playerShares.size() != n) playerShares.resize(n, 0.0);
    if (shareCostBasis.size() != n) shareCostBasis.resize(n, 0.0);
}

void Game::publishFactionBook(int factionIndex) {
    resizeShareBooks();
    if (!validFaction(*this, factionIndex)) return;
    const Faction& f = factions[size_t(factionIndex)];
    // Активы державы — это её казна плюс её системы, оценённые ровно тем же
    // законом, каким система продаётся игроку (§13). Никакой отдельной
    // «стоимости фракции» не заводим: если бы она считалась иначе, покупка
    // системы у державы двигала бы её книгу не на цену сделки.
    // Доход державы — это её суверенный сбор с оборота подвластных систем, та
    // же величина и тот же закон, что и доход владельца системы (§18).
    double income = 0.0;
    for (size_t i = 0; i < f.controlledStars.size(); ++i) {
        const int starIndex = f.controlledStars[i];
        if (!validStar(*this, starIndex)) continue;
        const double own = colonyIncomeAt(starIndex);
        income += own > 0.0 ? own : systemTurnover(starIndex) * COLONY_OWNER_DUTY;
    }
    // Капитализация: касса плюс поток, оценённый в годах. Активы (цена систем)
    // в цену акции НЕ входят — см. комментарий у SHARE_CAPITALISATION_YEARS.
    const double book = std::max(0.0, f.treasury) + income * SHARE_CAPITALISATION_YEARS;
    // (§41) Отчёт — СОБЫТИЕ, а не тихое обновление числа. Держава теряет
    // системы в войнах, беднеет от выплат за головы, растит колонии — до сих
    // пор всё это происходило за кадром, и биржа была таблицей без сюжета.
    // Заметное движение книги попадает в ленту новостей, а если игрок в доле —
    // с прямым указанием, что это касается его денег.
    const double before = factionBook[size_t(factionIndex)];
    const bool hadReport = factionBookAt[size_t(factionIndex)] > -1.0e17;
    factionBook[size_t(factionIndex)] = book;
    factionIncome[size_t(factionIndex)] = income;
    factionBookAt[size_t(factionIndex)] = time;
    if (hadReport && before > 0.0) {
        const double move = (book - before) / before;
        const bool held = size_t(factionIndex) < playerShares.size() &&
                          playerShares[size_t(factionIndex)] > 0.0;
        if (move <= -0.10 || move >= 0.10) {
            char note[192];
            std::snprintf(note, sizeof(note), "%s report: shares %s %d%%%s",
                          f.name.c_str(), move > 0.0 ? "up" : "down",
                          int(std::fabs(move) * 100.0 + 0.5),
                          held ? " - you hold a stake" : "");
            pushNews(note, held ? 4 : 1);
        }
    }
}

double Game::factionSharePrice(int factionIndex) const {
    if (!validFaction(*this, factionIndex)) return 0.0;
    if (size_t(factionIndex) >= factionBook.size()) return 0.0;
    if (factionBookAt[size_t(factionIndex)] < -1.0e17) return 0.0;   // отчёта ещё не было
    return std::max(0.0, factionBook[size_t(factionIndex)]) / SHARE_FLOAT;
}

double Game::factionShareDividend(int factionIndex) const {
    if (!validFaction(*this, factionIndex)) return 0.0;
    if (size_t(factionIndex) >= factionIncome.size()) return 0.0;
    // Владелец системы берёт ВСЮ пошлину с её оборота; акционер — только ту
    // долю, которую держава распределяет, а не тратит на флоты и войны. Отсюда
    // и вся разница доходности: система окупается быстрее акции в четыре раза,
    // зато за её кассой надо летать.
    return std::max(0.0, factionIncome[size_t(factionIndex)]) * SHARE_PAYOUT / SHARE_FLOAT;
}

double Game::factionBookAge(int factionIndex) const {
    if (!validFaction(*this, factionIndex)) return -1.0;
    if (size_t(factionIndex) >= factionBookAt.size()) return -1.0;
    if (factionBookAt[size_t(factionIndex)] < -1.0e17) return -1.0;
    return std::max(0.0, time - factionBookAt[size_t(factionIndex)]);
}

double Game::playerShareValue() const {
    double total = 0.0;
    for (size_t f = 0; f < playerShares.size(); ++f) {
        total += playerShares[f] * factionSharePrice(int(f));
    }
    return total;
}

Game::NetWorth Game::playerNetWorth() const {
    NetWorth w;
    for (size_t i = 0; i < agents.size(); ++i) {
        const Agent& a = agents[i];
        if (!a.playerControlled) continue;
        w.wallets += std::max(0.0, a.money);
        for (int k = 0; k < EX_COUNT; ++k) {
            if (a.ship.exotic[k] <= 0.0) continue;
            // Экзотика оценивается ЗДЕСЬ, где борт стоит: увезённая к
            // потребителю она стоит втрое, и делать вид, что это уже случилось,
            // было бы враньём в свою пользу.
            const double price = exoticPriceAt(a.currentStar, k);
            w.exotics += a.ship.exotic[k] * (price > 0.0 ? price : exoticDefs()[size_t(k)].basePrice * 0.5);
        }
        // Корпус: цена класса из таблицы. Модули и переоснастка в неё не входят —
        // они не продаются обратно, и записывать их в состояние было бы
        // приписыванием несуществующего.
        for (size_t c = 0; c < shipClasses().size(); ++c) {
            if (shipClasses()[c].name != a.ship.name) continue;
            w.hulls += shipClasses()[c].price;
            break;
        }
    }
    w.account = factionClearedTreasury(playerFaction);
    w.inFlight = factionCreditsInFlight(playerFaction);
    for (size_t i = 0; i < colonies.size(); ++i) {
        if (!playerOwnsStar(colonies[i].starIndex)) continue;
        w.vaults += std::max(0.0, colonies[i].localLedger);
    }
    w.shares = playerShareValue();
    w.total = w.wallets + w.account + w.inFlight + w.vaults + w.shares + w.exotics + w.hulls;
    return w;
}

double Game::playerBuyShares(int factionIndex, double shares) {
    resizeShareBooks();
    if (!validFaction(*this, factionIndex) || !(shares > 0.0)) return 0.0;
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return 0.0;
    if (playerTradingBlocked()) return 0.0;
    Agent& player = agents[size_t(playerAgent)];
    if (player.ship.enRoute) { lastEvent = "cannot trade in transit"; return 0.0; }
    if (factionIndex == playerFaction) {
        // Своя фракция — это и есть кошелёк игрока (§16): покупка её акций была
        // бы переводом денег самому себе с потерей на дивидендах.
        lastEvent = "you cannot buy shares in your own freehold";
        return 0.0;
    }
    const double price = factionSharePrice(factionIndex);
    if (price <= 0.0) { lastEvent = "no published report for this power"; return 0.0; }
    // Больше четверти державы не продадут никому: остальное лежит у тех, кто
    // её и построил. Без этого потолка игрок с поздними деньгами скупал бы
    // скопление целиком одной кнопкой.
    const double roomShares = std::max(0.0, SHARE_FLOAT * SHARE_MAX_STAKE - playerShares[size_t(factionIndex)]);
    double amount = std::min(shares, roomShares);
    amount = std::min(amount, std::max(0.0, player.money) / price);
    if (amount <= 1e-6) {
        lastEvent = roomShares <= 1e-6 ? "stake capped at a quarter of the power"
                                       : "not enough credits for shares";
        return 0.0;
    }
    const double cost = amount * price;
    player.money -= cost;
    playerShares[size_t(factionIndex)] += amount;
    shareCostBasis[size_t(factionIndex)] += cost;
    lastEvent = "bought shares in " + factions[size_t(factionIndex)].name;
    return amount;
}

double Game::playerSellShares(int factionIndex, double shares) {
    resizeShareBooks();
    if (!validFaction(*this, factionIndex) || !(shares > 0.0)) return 0.0;
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return 0.0;
    // Продажа акций при отозванной лицензии разрешена по той же причине, что и
    // продажа экзотики (§46): выручка гасит долг, а не ложится в карман.
    Agent& player = agents[size_t(playerAgent)];
    if (player.ship.enRoute) { lastEvent = "cannot trade in transit"; return 0.0; }
    const double price = factionSharePrice(factionIndex);
    if (price <= 0.0) { lastEvent = "no published report for this power"; return 0.0; }
    const double amount = std::min(shares, playerShares[size_t(factionIndex)]);
    if (amount <= 1e-6) { lastEvent = "no shares to sell"; return 0.0; }
    const double held = playerShares[size_t(factionIndex)];
    // Себестоимость списывается ПРОПОРЦИОНАЛЬНО проданной доле — тем же
    // законом, что и `cargoCost` при продаже части трюма.
    const double basisShare = held > 1e-9 ? shareCostBasis[size_t(factionIndex)] * (amount / held) : 0.0;
    const double gross = amount * price;
    player.money += gross - applyLicenceBuyback(gross);
    playerShares[size_t(factionIndex)] -= amount;
    shareCostBasis[size_t(factionIndex)] = std::max(0.0, shareCostBasis[size_t(factionIndex)] - basisShare);
    lastEvent = "sold shares in " + factions[size_t(factionIndex)].name;
    return amount;
}

// (§47) Держава ПОКУПАЕТ ДОЛЮ в соседе — из торгового бюджета и по той же
// котировке `factionSharePrice`, что видит игрок. Держава без своих денег в
// чужой книге была последним местом, где «шестнадцать игроков» оставались
// декорацией: биржа существовала ради одного капитана.
//
// ⚠️ Ни одного обращения к ГПСЧ (§2.3): выбор — максимум доходности по книге.
void Game::factionTryBuyShares(int factionIndex) {
    if (!validFaction(*this, factionIndex)) return;
    if (factionIndex == playerFaction || factionIndex == clearingFaction) return;
    resizeShareBooks();
    Faction& holder = factions[size_t(factionIndex)];
    if (licenceOf(factionIndex).revoked) return;
    const double purse = std::min(holder.treasury, holder.tradeBudget);
    if (!(purse > 0.0)) return;

    int best = -1;
    double bestYield = 0.0;
    double bestPrice = 0.0;
    for (size_t f = 0; f < factions.size(); ++f) {
        if (int(f) == factionIndex || int(f) == playerFaction || int(f) == clearingFaction) continue;
        const double price = factionSharePrice(int(f));
        if (!(price > 0.0)) continue;
        const double yield = factionShareDividend(int(f)) / price;
        if (yield > bestYield) { bestYield = yield; best = int(f); bestPrice = price; }
    }
    if (best < 0) return;

    // Тот же потолок доли, что и у игрока: больше четверти державы не продадут
    // никому. Считается по ЭТОМУ держателю — доли держав друг в друге не
    // складываются в один лимит.
    double& held = factionShareHolding(factionIndex, best);
    const double room = std::max(0.0, SHARE_FLOAT * SHARE_MAX_STAKE - held);
    if (room <= 1e-6) return;
    const double amount = std::min(room, purse / bestPrice);
    if (amount <= 1e-6) return;
    const double cost = amount * bestPrice;
    holder.treasury -= cost;
    held += amount;
}

// (§47) Держава БЕРЁТ ЛИЦЕНЗИЮ И СТРОИТ БОРТ — по правилу игрока: каждый борт
// летает по отдельной лицензии, лицензия покупается заранее и деньги за неё
// идут центру. До этого флоты держав были заданы при рождении мира и только
// убывали: корабли гибли, строить их было некому, и длинная партия вымирала.
//
// ⚠️ Ни одного обращения к ГПСЧ (§2.3). За один заход делается ОДНА покупка:
// сначала лицензия, в следующий раз — борт под неё.
void Game::factionTryGrowFleet(int factionIndex) {
    if (!validFaction(*this, factionIndex)) return;
    if (factionIndex == playerFaction || factionIndex == clearingFaction) return;
    if (licenceOf(factionIndex).revoked) return;
    Faction& power = factions[size_t(factionIndex)];
    if (power.controlledStars.empty()) return;
    resizeShareBooks();
    if (size_t(factionIndex) < factionNextFleetBuy.size() &&
        time < factionNextFleetBuy[size_t(factionIndex)]) return;

    // Потолок населения: мир рассчитан на `AGENT_TARGET_FULL` бортов на полное
    // скопление, и строить сверх этого — топить симуляцию ради симметрии.
    const size_t starCount = std::max<size_t>(1, cluster.stars.size());
    const double targetAgents = std::max(48.0,
        double(AGENT_TARGET_FULL) * double(starCount) / double(STAR_COUNT));
    if (double(agents.size()) >= targetAgents * FLEET_POPULATION_HEADROOM) return;

    const double purse = std::min(power.treasury, power.militaryBudget + power.tradeBudget);
    if (!(purse > 0.0)) return;

    // Шаг 1: нет свободной лицензии — покупаем её, и на этом ход окончен.
    if (factionFreeLicences(factionIndex) <= 0) {
        const double price = licencePriceOf(factionIndex);
        if (!(price > 0.0) || price > purse) return;
        power.treasury -= price;
        if (validFaction(*this, clearingFaction)) factions[size_t(clearingFaction)].treasury += price;
        licenceOf(factionIndex).count += 1;
        factionNextFleetBuy[size_t(factionIndex)] = time + FLEET_BUY_INTERVAL_YEARS;
        return;
    }

    // Шаг 2: лицензия есть — под неё строится борт. Класс выбирается ПО КАРМАНУ:
    // самый дорогой, который держава тянет, — она строит лучшее, что может.
    const std::vector<ShipClass>& classes = shipClasses();
    int pick = -1;
    double pickPrice = 0.0;
    for (size_t c = 0; c < classes.size(); ++c) {
        const double price = classes[c].price;
        if (price > 0.0 && price <= purse && price > pickPrice) { pickPrice = price; pick = int(c); }
    }
    if (pick < 0) return;

    // Роль — та, которой держава недобирает против собственного уклона: возчик
    // при торговом смещении, военный при боевом. Без RNG и без таблиц.
    int traders = 0, hulls = 0;
    for (size_t k = 0; k < power.fleetAgents.size(); ++k) {
        const int ai = power.fleetAgents[k];
        if (ai < 0 || ai >= int(agents.size())) continue;
        ++hulls;
        if (agents[size_t(ai)].type == "trader") ++traders;
    }
    const double tradeShare = hulls > 0 ? double(traders) / double(hulls) : 0.0;
    const bool wantTrader = tradeShare < std::max(0.0, std::min(1.0, power.tradeBias));

    const int berth = power.controlledStars[size_t(factionExpansionCursor) % power.controlledStars.size()];
    if (!validStar(*this, berth)) return;
    const ClusterStar& star = cluster.stars[size_t(berth)];

    power.treasury -= pickPrice;
    Ship hull(power.name + (wantTrader ? "_Hauler_" : "_Guard_") +
              std::to_string(licenceOf(factionIndex).count),
              star.x, star.y, star.z, 0.12, factionIndex);
    shipApplyClass(hull, classes[size_t(pick)]);
    shipAutofit(hull);

    Agent built(wantTrader ? "trader" : "military", hull);
    built.currentStar = berth;
    built.homeStar = power.homeStar;
    built.destStar = berth;
    built.money = 0.0;
    built.tradeBias = power.tradeBias;
    built.riskTolerance = power.riskTolerance;
    built.lastAction = "commissioned";
    agents.push_back(built);
    registerFactionAgent(*this, int(agents.size()) - 1);
    factionNextFleetBuy[size_t(factionIndex)] = time + FLEET_BUY_INTERVAL_YEARS;
}

double& Game::factionShareHolding(int holderFaction, int issuerFaction) {
    resizeShareBooks();
    static double orphan = 0.0;
    orphan = 0.0;
    if (!validFaction(*this, holderFaction) || !validFaction(*this, issuerFaction)) return orphan;
    const size_t index = size_t(holderFaction) * factions.size() + size_t(issuerFaction);
    if (index >= factionShares.size()) return orphan;
    return factionShares[index];
}

double Game::factionShareHoldingOf(int holderFaction, int issuerFaction) const {
    if (!validFaction(*this, holderFaction) || !validFaction(*this, issuerFaction)) return 0.0;
    const size_t index = size_t(holderFaction) * factions.size() + size_t(issuerFaction);
    return index < factionShares.size() ? factionShares[index] : 0.0;
}

void Game::payShareDividends(double years) {
    if (!(years > 0.0) || playerFaction < 0) return;
    resizeShareBooks();
    // Сначала дивиденды ДЕРЖАВАМ-акционерам: та же проводка, что и игроку, но
    // без светового ожидания — держава считает деньги в своей же столице.
    for (size_t h = 0; h < factions.size(); ++h) {
        if (int(h) == playerFaction || int(h) == clearingFaction) continue;
        for (size_t f = 0; f < factions.size(); ++f) {
            if (f == h) continue;
            const double stake = factionShareHoldingOf(int(h), int(f));
            if (stake <= 0.0) continue;
            double payout = stake * factionShareDividend(int(f)) * years;
            payout = std::min(payout, std::max(0.0, factions[f].treasury));
            if (payout <= 0.0) continue;
            factions[f].treasury = std::max(0.0, factions[f].treasury - payout);
            factions[h].treasury += payout;
        }
    }
    for (size_t f = 0; f < playerShares.size(); ++f) {
        if (playerShares[f] <= 0.0) continue;
        // Держава платит из СВОЕЙ казны и не больше, чем в ней есть. Раньше
        // выплата начислялась безусловно, а списание зажималось нулём: разорённая
        // войной фракция продолжала платить полный дивиденд из ниоткуда.
        double payout = playerShares[f] * factionShareDividend(int(f)) * years;
        if (validFaction(*this, int(f))) payout = std::min(payout, std::max(0.0, factions[f].treasury));
        if (payout <= 0.0) continue;
        // Дивиденд платит держава ОТТУДА, ГДЕ ОНА ЕСТЬ, — из своей столицы.
        // Значит, деньги идут на счёт и подчиняются световой сверке (§16):
        // получить их можно, но не мгновенно и не где угодно.
        int origin = factions[f].homeStar;
        if (!validStar(*this, origin)) {
            origin = (playerAgent >= 0 && playerAgent < int(agents.size()))
                         ? agents[size_t(playerAgent)].currentStar : -1;
        }
        // ⚠️ ДЕНЬГИ СНАЧАЛА ЗАЧИСЛЯЮТСЯ, и только потом придерживаются светом.
        //
        // Здесь стоял один `addCreditFloat` без зачисления в казну — то есть
        // приход существовал ТОЛЬКО как задержка. А доступное к трате считается
        // как `казна − в пути`, и каждый дивиденд ВЫЧИТАЛ из счёта: замер —
        // казна 0, в пути 820 675 Cr, доступно 0, и внесённые игроком 10 000
        // после сорока лет ожидания не снимались вовсе. Счёт ломался навсегда.
        // Все остальные вызывающие (`playerAccountDeposit`, излишек колонии)
        // делают именно эту пару.
        if (validFaction(*this, playerFaction)) factions[size_t(playerFaction)].treasury += payout;
        addCreditFloat(playerFaction, payout, origin);
        if (validFaction(*this, int(f))) {
            factions[f].treasury = std::max(0.0, factions[f].treasury - payout);
        }
    }
}

// --- Экзотическая материя (§31) ---------------------------------------------

namespace {
int exoticStockIndex(const std::vector<ExoticStock>& list, int starIndex) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].starIndex == starIndex) return int(i);
    }
    return -1;
}
}  // namespace

double Game::exoticStockAt(int starIndex, int kind) const {
    if (!validStar(*this, starIndex) || kind < 0 || kind >= EX_COUNT) return 0.0;
    const ClusterStar& star = cluster.stars[size_t(starIndex)];
    const double target = exoticTargetStock(star, kind);
    if (target <= 0.0) return 0.0;
    const int at = exoticStockIndex(exoticStocks, starIndex);
    if (at < 0) return target;   // игрок сюда не заходил — запас на своей отметке
    // Ленивое восстановление: считаем в момент обращения, а не тиком по всем
    // 8192 системам. Рынок экзотики трогает ровно один корабль, и держать ради
    // него ещё один проход по скоплению было бы просто расточительством.
    const ExoticStock& rec = exoticStocks[size_t(at)];
    return exoticRelaxStock(star, kind, rec.stock[kind], std::max(0.0, time - rec.updatedAt));
}

double Game::exoticPriceAt(int starIndex, int kind) const {
    if (!validStar(*this, starIndex) || kind < 0 || kind >= EX_COUNT) return 0.0;
    return ::exoticPriceAt(cluster.stars[size_t(starIndex)], kind,
                           exoticStockAt(starIndex, kind), clusterPriceLevel);
}

bool Game::exoticMarketAt(int starIndex) const {
    if (!validStar(*this, starIndex)) return false;
    return exoticStarHasMarket(cluster.stars[size_t(starIndex)]);
}

namespace {
// Общая часть покупки и продажи: найти запись о звезде, привести запас к
// «сейчас» и вернуть ссылку, готовую к изменению.
ExoticStock& exoticRecordFor(Game& game, int starIndex) {
    int at = exoticStockIndex(game.exoticStocks, starIndex);
    if (at < 0) {
        ExoticStock rec;
        rec.starIndex = starIndex;
        rec.updatedAt = game.time;
        const ClusterStar& star = game.cluster.stars[size_t(starIndex)];
        for (int k = 0; k < EX_COUNT; ++k) rec.stock[k] = exoticTargetStock(star, k);
        game.exoticStocks.push_back(rec);
        at = int(game.exoticStocks.size()) - 1;
    }
    ExoticStock& rec = game.exoticStocks[size_t(at)];
    const ClusterStar& star = game.cluster.stars[size_t(starIndex)];
    const double years = std::max(0.0, game.time - rec.updatedAt);
    for (int k = 0; k < EX_COUNT; ++k) rec.stock[k] = exoticRelaxStock(star, k, rec.stock[k], years);
    rec.updatedAt = game.time;
    return rec;
}
}  // namespace

double Game::playerBuyExotic(int kind, double units) {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return 0.0;
    if (kind < 0 || kind >= EX_COUNT || !(units > 0.0)) return 0.0;
    if (playerTradingBlocked()) return 0.0;
    Agent& player = agents[size_t(playerAgent)];
    if (player.ship.enRoute) { lastEvent = "cannot trade in transit"; return 0.0; }
    const int starIndex = player.currentStar;
    if (!validStar(*this, starIndex)) return 0.0;
    const ClusterStar& star = cluster.stars[size_t(starIndex)];
    if (exoticTargetStock(star, kind) <= 0.0) {
        lastEvent = "no exotics market here";
        return 0.0;
    }
    if (player.ship.containment <= 0.0) {
        lastEvent = "no containment bay - refit first";
        return 0.0;
    }

    ExoticStock& rec = exoticRecordFor(*this, starIndex);
    // Три ограничителя, и все три настоящие: ячейка, склад системы и деньги.
    // Склад до дна не выбирается (`exoticStockAfterTrade` держит 2% пола), но
    // последние крохи стоят вчетверо — покупать их просто незачем.
    double amount = std::min(units, shipExoticRoom(player.ship));
    amount = std::min(amount, std::max(0.0, rec.stock[kind] - exoticTargetStock(star, kind) * 0.03));
    if (amount <= 1e-6) { lastEvent = "nothing to buy here"; return 0.0; }
    for (int pass = 0; pass < 2 && amount > 0.0; ++pass) {
        const double avg = exoticExecutionPrice(star, kind, rec.stock[kind], clusterPriceLevel, amount, false);
        if (avg <= 0.0) break;
        amount = std::min(amount, std::max(0.0, player.money) / avg);
    }
    if (amount <= 1e-6) { lastEvent = "not enough credits"; return 0.0; }

    const double avg = exoticExecutionPrice(star, kind, rec.stock[kind], clusterPriceLevel, amount, false);
    const double cost = avg * amount;
    if (cost > player.money) return 0.0;
    player.money -= cost;
    player.ship.exotic[kind] += amount;
    rec.stock[kind] = exoticStockAfterTrade(star, kind, rec.stock[kind], -amount);
    // Экзотика — товар, и лицензия распространяется на неё так же, как на руду:
    // пошлина владельцу системы. Тариф КВОТЫ снимается только с продажи (см.
    // sellCargo), поэтому здесь его нет.
    const int owner = star.ownerFaction;
    if (validFaction(*this, owner)) {
        // Пошлина УРЕЗАЕТСЯ по остатку кошелька, а не пропускается. Раньше
        // стояло `if (money >= fee)`, а подгонка объёма выше специально сводит
        // остаток к 1-3% сделки — то есть ровно к размеру пошлины: покупка «на
        // всё» систематически уходила от сбора, а осторожная платила.
        const double fee = std::min(std::max(0.0, player.money),
                                    cost * tariffFor(*this, starIndex, player.ship.ownerFaction, 0.014) /
                                        std::max(1.0, tech.charisma));
        if (fee > 0.0) {
            player.money -= fee;
            factions[size_t(owner)].treasury += fee;
        }
    }
    lastEvent = "bought " + std::string(exoticDefs()[size_t(kind)].symbol);
    return amount;
}

double Game::playerSellExotic(int kind, double units) {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return 0.0;
    if (kind < 0 || kind >= EX_COUNT || !(units > 0.0)) return 0.0;
    // ⚠️ Здесь НЕТ гейта `playerTradingBlocked` (§46): при отозванной лицензии
    // продавать экзотику МОЖНО, просто выручка идёт в отработку долга. Иначе
    // возчик хайтека запирался насмерть — трюм у него пуст по построению, а всё
    // состояние лежит в ячейке удержания, и игра советовала «продайте груз».
    Agent& player = agents[size_t(playerAgent)];
    if (player.ship.enRoute) { lastEvent = "cannot trade in transit"; return 0.0; }
    const int starIndex = player.currentStar;
    if (!validStar(*this, starIndex)) return 0.0;
    const ClusterStar& star = cluster.stars[size_t(starIndex)];
    if (exoticTargetStock(star, kind) <= 0.0) {
        lastEvent = "no exotics market here";
        return 0.0;
    }
    const double amount = std::min(units, player.ship.exotic[kind]);
    if (amount <= 1e-6) { lastEvent = "nothing to sell"; return 0.0; }

    ExoticStock& rec = exoticRecordFor(*this, starIndex);
    const double avg = exoticExecutionPrice(star, kind, rec.stock[kind], clusterPriceLevel, amount, true);
    const double gross = avg * amount;
    player.ship.exotic[kind] -= amount;
    rec.stock[kind] = exoticStockAfterTrade(star, kind, rec.stock[kind], amount);

    double tariff = tariffFor(*this, starIndex, player.ship.ownerFaction, 0.026) /
                    std::max(1.0, tech.charisma);
    const double fee = gross * tariff;
    // Лицензионный тариф с экзотики удерживается ТОЧНО ТАК ЖЕ, как с руды, и в
    // ту же квоту. Это и делает хайтек-этаж выходом из квотной ловушки поздней
    // игры: один рейс с конденсатом закрывает тысячелетнюю квоту целиком.
    const double licenceFee = licence().revoked ? 0.0 : gross * licence().tariffRate;
    const double debtPaid = applyLicenceBuyback(gross - fee);
    player.money += gross - fee - licenceFee - debtPaid;
    const int owner = star.ownerFaction;
    if (validFaction(*this, owner)) factions[size_t(owner)].treasury += fee;
    // Владелец системы узнаёт о своём крупнейшем сборе тем же светом, что и о
    // сборе с руды (`sellCargo`): без этого сигнала касса колонии молча
    // расходилась с тем, что игрок реально заплатил.
    if (fee > 0.01 && validFaction(*this, owner)) queueSettlementSignal(owner, starIndex, fee);
    if (licenceFee > 0.0) {
        licence().quotaPaid += licenceFee;
        if (validFaction(*this, clearingFaction)) factions[size_t(clearingFaction)].treasury += licenceFee;
    }
    player.trades += 1;
    exoticUnitsSold += amount;
    lastEvent = "sold " + std::string(exoticDefs()[size_t(kind)].symbol);
    return amount;
}

int Game::playerRefitLevel(bool plating) const {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return 0;
    const Ship& sh = agents[size_t(playerAgent)].ship;
    return plating ? sh.platingLayers : sh.containmentLevel;
}

double Game::containmentNextPrice() const {
    const int level = playerRefitLevel(false);
    if (level >= CONTAINMENT_MAX_LEVEL) return 0.0;
    // Квадратично по ступени — как и цена систем (§13): вторая ячейка не «ещё
    // одна такая же», она вчетверо больше по деньгам при той же прибавке.
    const double step = double(level + 1);
    return CONTAINMENT_STEP_PRICE * step * step;
}

bool Game::playerUpgradeContainment() {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return false;
    Agent& player = agents[size_t(playerAgent)];
    if (player.ship.enRoute) { lastEvent = "cannot refit in transit"; return false; }
    if (player.ship.containmentLevel >= CONTAINMENT_MAX_LEVEL) { lastEvent = "containment at maximum"; return false; }
    // Ячейку варят на верфи, а не в поле: удержание антивещества — это
    // сверхпроводящая магнитная ловушка, а не ящик.
    if (shipyardLevelAtStar(player.currentStar) < 2) {
        lastEvent = "needs shipyard lvl 2 to fit containment";
        return false;
    }
    const double price = containmentNextPrice();
    if (player.money < price) { lastEvent = "containment bay needs more credits"; return false; }
    player.money -= price;
    player.ship.containmentLevel += 1;
    // ⚠️ Здесь НЕЛЬЗЯ звать полное перезапекание: хромокоры и прежние слои
    // брони уже лежат в полях корабля, и повторное наложение их удвоило бы.
    // Ёмкость — присваивание, а не прибавка, поэтому её ставим напрямую.
    player.ship.containment = double(player.ship.containmentLevel) * CONTAINMENT_STEP_UNITS;
    pushNews("Containment bay fitted: exotic matter can be carried", 4);
    lastEvent = "containment bay fitted";
    return true;
}

double Game::platingNextPrice() const {
    const int layers = playerRefitLevel(true);
    if (layers >= PLATING_MAX_LAYERS) return 0.0;
    const double step = double(layers + 1);
    return 900000.0 * step;
}

bool Game::playerAddHullPlating() {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return false;
    Agent& player = agents[size_t(playerAgent)];
    if (player.ship.enRoute) { lastEvent = "cannot refit in transit"; return false; }
    if (player.ship.platingLayers >= PLATING_MAX_LAYERS) { lastEvent = "plating at maximum"; return false; }
    if (shipyardLevelAtStar(player.currentStar) < 2) {
        lastEvent = "needs shipyard lvl 2 to weld plating";
        return false;
    }
    if (player.ship.exotic[EX_NEUTRONIUM] < PLATING_NEUTRONIUM_UNITS) {
        lastEvent = "needs neutronium in the containment bay";
        return false;
    }
    const double price = platingNextPrice();
    if (player.money < price) { lastEvent = "plating needs more credits"; return false; }
    player.money -= price;
    player.ship.exotic[EX_NEUTRONIUM] -= PLATING_NEUTRONIUM_UNITS;
    player.ship.platingLayers += 1;
    // Наваривается ОДИН слой — прибавкой, а не полным перезапеканием: всё
    // прежнее уже в полях корабля (см. комментарий в playerUpgradeContainment).
    player.ship.armor += PLATING_ARMOR_PER_LAYER;
    player.ship.maxHullHP += PLATING_HULL_PER_LAYER;
    player.ship.dryMass += PLATING_MASS_PER_LAYER;
    player.ship.hullHP = player.ship.maxHullHP;
    pushNews("Neutronium welded onto the hull", 4);
    lastEvent = "hull plated with neutronium";
    return true;
}

double Game::forgeCondensateCost() const {
    return FORGE_CONDENSATE_BASE + FORGE_CONDENSATE_PER_CORE * double(tech.cores);
}

double Game::forgeCreditCost() const {
    // Кредитная часть намеренно скромная рядом с ценой конденсата: платит не
    // кошелёк, а РЕЙС за конденсатом. Иначе кузница стала бы просто ещё одной
    // покупкой, а она — повод лететь к мёртвой звезде.
    return 250000.0 * double(tech.cores + 1);
}

bool Game::playerForgeChromocore(int stat) {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return false;
    if (stat < 0 || stat >= TECH_STAT_COUNT) return false;
    Agent& player = agents[size_t(playerAgent)];
    if (player.ship.enRoute) { lastEvent = "cannot forge in transit"; return false; }
    if (shipyardLevelAtStar(player.currentStar) < 2) {
        lastEvent = "core forge needs shipyard lvl 2";
        return false;
    }
    const double need = forgeCondensateCost();
    if (player.ship.exotic[EX_CONDENSATE] < need) {
        lastEvent = "core forge needs condensate";
        return false;
    }
    const double price = forgeCreditCost();
    if (player.money < price) { lastEvent = "core forge needs more credits"; return false; }
    player.ship.exotic[EX_CONDENSATE] -= need;
    player.money -= price;
    // ⚠️ Именно ВЫБРАННЫЙ стат, а не случайный. Ядра, падающие из исследований,
    // так и остаются рулеткой (`addResearch`) — это фон прогресса. Кузница же
    // это РЕШЕНИЕ, и оно стоит рейса к мёртвой звезде: за конденсатом больше
    // некуда лететь.
    grantChromocore(stat);
    coresForged += 1;
    return true;
}

// --- Что уже взято в микромире (§32.1) --------------------------------------

namespace {
// Запись о звезде в списке заявок. Список короткий (только те системы, где
// игрок реально что-то брал), поэтому линейный поиск честнее хеша.
int localClaimsIndex(const std::vector<LocalClaims>& claims, int starIndex) {
    for (size_t i = 0; i < claims.size(); ++i) {
        if (claims[i].starIndex == starIndex) return int(i);
    }
    return -1;
}
}  // namespace

bool Game::localRadioClaimed(int starIndex, int radioIndex) const {
    if (radioIndex < 0 || radioIndex >= 32) return false;
    const int at = localClaimsIndex(localClaims, starIndex);
    if (at < 0) return false;
    return (localClaims[size_t(at)].radioMask & (1u << unsigned(radioIndex))) != 0u;
}

void Game::markLocalRadioClaimed(int starIndex, int radioIndex) {
    if (radioIndex < 0 || radioIndex >= 32) return;
    int at = localClaimsIndex(localClaims, starIndex);
    if (at < 0) {
        LocalClaims rec;
        rec.starIndex = starIndex;
        rec.bountyAt = time;
        localClaims.push_back(rec);
        at = int(localClaims.size()) - 1;
    }
    localClaims[size_t(at)].radioMask |= (1u << unsigned(radioIndex));
}

double Game::payLocalBounty(int starIndex, double amount) {
    if (!(amount > 0.0)) return 0.0;
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return 0.0;

    int at = localClaimsIndex(localClaims, starIndex);
    if (at < 0) {
        LocalClaims rec;
        rec.starIndex = starIndex;
        rec.bountyAt = time;
        localClaims.push_back(rec);
        at = int(localClaims.size()) - 1;
    }
    LocalClaims& rec = localClaims[size_t(at)];

    // Ленивое восстановление бюджета: считаем в момент обращения, а не тиком по
    // всем звёздам. Тот же приём, что у рынка экзотики.
    const double years = std::max(0.0, time - rec.bountyAt);
    rec.bountyPaid *= std::exp(-years / BOUNTY_RECOVERY_YEARS);
    rec.bountyAt = time;

    // Бюджет охраны путей. У глубокого космоса (starIndex < 0) хозяина нет —
    // платить некому, и это правильно: за головы платят В СИСТЕМЕ.
    const double turnover = validStar(*this, starIndex) ? systemTurnover(starIndex) : 0.0;
    const double budget = starIndex < 0 ? 0.0
                        : std::max(BOUNTY_SYSTEM_FLOOR, turnover * BOUNTY_SYSTEM_SHARE);
    const double available = std::max(0.0, budget - rec.bountyPaid);
    const double paid = std::min(amount, available);
    if (paid <= 0.0) return 0.0;

    rec.bountyPaid += paid;
    agents[playerAgent].money += paid;
    // Деньги приходят из казны хозяина системы, а если хозяина нет — из
    // клиринговой палаты: она и так ведает лицензиями и безопасностью путей.
    const int owner = validStar(*this, starIndex) ? cluster.stars[size_t(starIndex)].ownerFaction : -1;
    const int payer = validFaction(*this, owner) ? owner : clearingFaction;
    if (validFaction(*this, payer)) {
        factions[size_t(payer)].treasury = std::max(0.0, factions[size_t(payer)].treasury - paid);
    }
    return paid;
}

// (§52) ПОЯС ВЫРАБАТЫВАЕТСЯ. Множитель к куску руды, 0..1.
//
// Развилка стояла так: `star.miningRichness` не уменьшался нигде, и пояс был
// полон при каждом входе. Ограничителем служила только цена в КОНКРЕТНОМ порту
// — переехал в соседний, и петля обнулилась, тогда как вся остальная экономика
// в игре конечна.
//
// Что именно кончается — важно. Недра системы лоне капитану не по зубам: рядом
// работает промышленность, ворочающая тысячи масс в год, и уменьшать от ручной
// добычи `miningRichness` (свойство НЕДР) было бы враньём физике. Кончается
// другое — ДОСЯГАЕМОЕ: обработанные глыбы в коридоре входа, те самые, до
// которых долетает один корабль. Их естественная мера — его собственный трюм:
// «шлюпочный» пояс держит примерно корабельную загрузку рабочей руды, а новые
// камни надрейфовывают десятилетиями.
//
// Отсюда обе величины БЕЗ единого нового числа:
//   • масштаб — `cargoCapacity` корабля игрока: выскреб полный трюм — отдача
//     вдвое ниже, три трюма — вчетверо (закон 1/(1+x), без ступенек);
//   • восстановление — `BOUNTY_RECOVERY_YEARS` (40 лет), тот же медленный
//     клапан системы, что и у бюджета наград. Второй такой константы не
//     заводим: разделять «скорость надрейфовывания камней» и «скорость
//     пополнения казны охраны» нечем — ни замера, ни физики под это нет.
//
// ⚠️ ЦЕНА ОТМЕНЫ: одна функция и её вызов в `mining.cpp`; поля `minedMass` и
// `minedAt` останутся в сейве мёртвым грузом (версия 21 их уже несёт).
// ⚠️ Игру это не запирает: истощается ОДНА система, а их 8192 — добыча
// становится кочевой, а не мёртвой.
double Game::minedRichnessAt(int starIndex) {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return 1.0;
    int at = localClaimsIndex(localClaims, starIndex);
    if (at < 0) return 1.0;                       // тут ещё не копали
    LocalClaims& rec = localClaims[size_t(at)];

    // Ленивое восстановление: считаем в момент обращения, а не тиком по всем
    // звёздам. Тот же приём, что у бюджета наград и рынка экзотики.
    const double years = std::max(0.0, time - rec.minedAt);
    rec.minedMass *= std::exp(-years / BOUNTY_RECOVERY_YEARS);
    rec.minedAt = time;

    const double hold = std::max(1.0, agents[size_t(playerAgent)].ship.cargoCapacity);
    return 1.0 / (1.0 + rec.minedMass / hold);
}

void Game::addMinedMass(int starIndex, double mass) {
    if (!(mass > 0.0)) return;
    int at = localClaimsIndex(localClaims, starIndex);
    if (at < 0) {
        LocalClaims rec;
        rec.starIndex = starIndex;
        rec.minedAt = time;
        localClaims.push_back(rec);
        at = int(localClaims.size()) - 1;
    }
    LocalClaims& rec = localClaims[size_t(at)];
    const double years = std::max(0.0, time - rec.minedAt);
    rec.minedMass *= std::exp(-years / BOUNTY_RECOVERY_YEARS);
    rec.minedAt = time;
    rec.minedMass += mass;
}

bool Game::playerSetAutoTrade(int agentIndex, bool on) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Agent& agent = agents[size_t(agentIndex)];
    if (!agent.playerControlled) return false;
    if (agentIndex == playerAgent) {
        lastEvent = "this is the hull you are flying";
        return false;
    }
    if (agent.ship.enRoute) {
        lastEvent = "cannot change orders in transit";
        return false;
    }
    for (size_t i = 0; i < contracts.size(); ++i) {
        const Contract& c = contracts[i];
        if (c.completed || c.failed || c.acceptedByAgent < 0) continue;
        for (size_t k = 0; k < c.carriers.size(); ++k) {
            if (c.carriers[k] != agentIndex) continue;
            lastEvent = "this hull is carrying a job";
            return false;
        }
    }
    agent.autoTrade = on;
    agent.missionCooldown = 0.0;
    agent.lastAction = on ? "auto: standing by" : "idle";
    lastEvent = on ? "hull put on autopilot" : "hull back under manual orders";
    return true;
}

bool Game::markLocalBountyTarget(int agentIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    bool marked = false;
    // Два прохода: сперва заказ, взятый ИГРОКОМ, потом любой свободный. Иначе
    // выбор зависел бы от порядка на доске, а не от того, что игрок сделал.
    for (int pass = 0; pass < 2 && !marked; ++pass) {
    for (size_t i = 0; i < contracts.size(); ++i) {
        Contract& c = contracts[i];
        if (c.completed || c.failed) continue;
        if (c.type != ContractType::Bounty) continue;
        if (c.targetAgent != agentIndex) continue;
        if (pass == 0 && c.acceptedByAgent != playerAgent) continue;
        if (pass == 1 && c.acceptedByAgent >= 0) continue;
        // Ничей заказ или ВЗЯТЫЙ ИГРОКОМ. Если игрок сбил пирата раньше, чем
        // взял заказ, доска обязана это признать — иначе награда зависела бы от
        // порядка нажатий. Но заказ, который везёт конкурент, чужой: закрывать
        // его чужой работой значило бы платить NPC за то, чего он не делал.
        c.targetDown = true;
        c.progress = std::max(c.progress, 0.9);
        marked = true;
        // ⚠️ РОВНО ОДИН заказ за убийство. Заказы на голову выписываются
        // независимо разными системами, и на одного пирата их может висеть
        // несколько: без выхода одно убийство закрывало бы их все разом, то
        // есть платило бы несколько наград за одно дело. Взятый игроком идёт
        // первым (он его и брал), потому что цикл встречает его раньше только
        // при равных условиях — поэтому сперва ищем именно его.
        break;
    }
    }
    if (marked) pushNews("Bounty target destroyed - claim it at the board", 2);
    return marked;
}

bool Game::playerTradingBlocked() {
    if (!licence().revoked) return false;
    // ⚠️ «Sell cargo» здесь было ВРАНЬЁМ для половины игры: у возчика экзотики
    // трюм пуст по определению, всё его состояние лежит в ячейке удержания и в
    // портфеле. Формулировка называет все три источника, а `applyLicenceBuyback`
    // делает так, чтобы любой из них действительно гасил долг.
    // ⚠️ ПЕРЕМЕННАЯ ЧАСТЬ — В КОНЦЕ СТРОКИ. Это единственный текст, который
    // видит запертый игрок, и он обязан читаться по-русски. Точным ключом
    // (`EXACT`) его не взять — внутри число; значит остаётся пословная сборка
    // (§14), а она не берёт оборот, кончающийся знаком препинания. Поэтому всё
    // словесное собрано в ОДИН оборот без скобок и запятых на концах, а сумма
    // приписана после него.
    lastEvent = "trading frozen - pay it with F2 or sell cargo, exotics or shares to work off " +
                std::to_string(int(std::ceil(licence().buyback))) + " CR";
    return true;
}

double Game::applyLicenceBuyback(double available) {
    if (!licence().revoked) return 0.0;
    // ⚠️ ОТОЗВАННАЯ ЛИЦЕНЗИЯ С НУЛЕВЫМ ДОЛГОМ — тупик без выхода: отработать
    // нечего, а торговля заперта, и сообщение предлагает «отработать 0 Cr».
    // Состояние не задумано никем (отзыв всегда ставит долг не меньше
    // `LICENCE_BUYBACK_MIN`), но достижимо арифметически. Лечим на месте.
    if (licence().buyback <= 0.0) { licence().revoked = false; return 0.0; }
    if (!(available > 0.0)) return 0.0;
    const double paid = std::min(licence().buyback, available);
    if (!(paid > 0.0)) return 0.0;
    licence().buyback -= paid;
    if (validFaction(*this, clearingFaction)) {
        factions[size_t(clearingFaction)].treasury += paid;
    }
    if (licence().buyback <= 0.01) {
        licence().buyback = 0.0;
        licence().revoked = false;
        licence().quotaPaid = 0.0;
        licence().periodEnd = time + LICENCE_PERIOD_YEARS;
        pushNews("Licence worked off. Trading resumed.", 4);
        lastEvent = "licence worked off - trading resumed";
    }
    return paid;
}

int Game::playerColonyCount() const {
    if (playerFaction < 0) return 0;
    int count = 0;
    for (const Colony& colony : colonies) {
        if (colony.ownerFaction == playerFaction) count += 1;
    }
    return count;
}

bool Game::playerCanOpenContractsAt(int starIndex) const {
    if (!validStar(*this, starIndex)) return false;
    if (playerAtStar(starIndex)) return true;
    if (!validFaction(*this, playerFaction) || playerAgent < 0 || playerAgent >= int(agents.size())) return false;

    const Agent& player = agents[playerAgent];
    if (player.ship.enRoute || !validStar(*this, player.currentStar) || player.currentStar >= int(signalMemory.size())) return false;
    const std::vector<SignalMemoryRecord>& memory = signalMemory[size_t(player.currentStar)];
    for (const SignalMemoryRecord& record : memory) {
        if (record.type != SignalType::ContractReport ||
            record.recipientFaction != playerFaction ||
            record.contractOriginStar != starIndex ||
            record.contractId < 0 ||
            record.contractAcceptedByAgent >= 0 ||
            record.contractCompleted ||
            record.contractFailed) {
            continue;
        }
        if (time - record.observedTime <= 72.0) return true;
    }
    return false;
}

std::vector<Contract> Game::playerVisibleContractsAt(int starIndex) const {
    std::vector<Contract> visible;
    if (!validStar(*this, starIndex)) return visible;

    for (const Contract& contract : contracts) {
        if (activeContract(contract) && contract.acceptedByAgent == playerAgent) {
            visible.push_back(contract);
        }
    }

    if (playerAtStar(starIndex)) {
        for (const Contract& contract : contracts) {
            if (activeContract(contract) &&
                contract.acceptedByAgent < 0 &&
                contract.originStar == starIndex &&
                !contractListHasId(visible, contract.id)) {
                visible.push_back(contract);
            }
        }
        return visible;
    }

    if (!validFaction(*this, playerFaction) || playerAgent < 0 || playerAgent >= int(agents.size())) return visible;
    const Agent& player = agents[playerAgent];
    if (player.ship.enRoute || !validStar(*this, player.currentStar) || player.currentStar >= int(signalMemory.size())) return visible;

    const std::vector<SignalMemoryRecord>& memory = signalMemory[size_t(player.currentStar)];
    for (const SignalMemoryRecord& record : memory) {
        if (record.type != SignalType::ContractReport ||
            record.recipientFaction != playerFaction ||
            record.contractOriginStar != starIndex ||
            record.contractId < 0 ||
            record.contractAcceptedByAgent >= 0 ||
            record.contractCompleted ||
            record.contractFailed ||
            time - record.observedTime > 72.0 ||
            contractListHasId(visible, record.contractId)) {
            continue;
        }
        visible.push_back(contractFromSignalRecord(record));
    }

    return visible;
}

void Game::resizeFactionKnowledge() {
    const size_t starCount = cluster.stars.size();
    const size_t wanted = factions.size() * starCount;
    if (factionKnowledge.size() != wanted) {
        factionKnowledge.resize(wanted);
    }
    if (factionMarketKnowledge.size() != wanted) {
        factionMarketKnowledge.resize(wanted);
    }
    const size_t wantedPrices = wanted * elementCount();
    if (factionMarketPrices.size() != wantedPrices) {
        factionMarketPrices.resize(wantedPrices, 0.0);
    }
    if (factionMarketSupplyPressure.size() != wantedPrices) {
        factionMarketSupplyPressure.resize(wantedPrices, 1.0);
    }
    if (factionMarketDemandPressure.size() != wantedPrices) {
        factionMarketDemandPressure.resize(wantedPrices, 1.0);
    }
    if (playerKnowledge.size() != starCount) {
        playerKnowledge.resize(starCount);
    }
}

void Game::seedFactionKnowledge(int factionIndex, int centerStar, double radiusLy) {
    if (!validFaction(*this, factionIndex) || !validStar(*this, centerStar)) return;
    resizeFactionKnowledge();

    const ClusterStar& center = cluster.stars[centerStar];
    for (size_t i = 0; i < cluster.stars.size(); ++i) {
        if (distanceBetween(center, cluster.stars[i]) <= radiusLy) {
            FactionStarKnowledge& knowledge = factionKnowledge[factionKnowledgeIndex(*this, factionIndex, int(i))];
            knowledge.ownerKnown = true;
            knowledge.ownerFaction = cluster.stars[i].ownerFaction;
            knowledge.ownerKnownAt = time;
            if (factionIndex == playerFaction) playerKnowledge[i] = knowledge;
            applyMarketKnowledge(factionIndex, int(i), markets[i].prices,
                marketSupplyPressureSnapshot(markets[i]), marketDemandPressureSnapshot(markets[i]), time);
        }
    }
}

void Game::observeStarForFaction(int factionIndex, int starIndex) {
    if (!validFaction(*this, factionIndex) || !validStar(*this, starIndex)) return;
    applyOwnerKnowledge(factionIndex, starIndex, cluster.stars[starIndex].ownerFaction, time, true);
    observeLocalThreatsForFaction(factionIndex, starIndex);
}

void Game::observeMarketForFaction(int factionIndex, int starIndex) {
    if (!validFaction(*this, factionIndex) || !validStar(*this, starIndex) || starIndex >= int(markets.size())) return;
    applyMarketKnowledge(factionIndex, starIndex, markets[starIndex].prices,
        marketSupplyPressureSnapshot(markets[starIndex]), marketDemandPressureSnapshot(markets[starIndex]), time);
}

void Game::applyOwnerKnowledge(int factionIndex, int starIndex, int ownerFaction, double observedTime, bool visited) {
    if (!validFaction(*this, factionIndex) || !validStar(*this, starIndex)) return;
    resizeFactionKnowledge();

    FactionStarKnowledge& knowledge = factionKnowledge[factionKnowledgeIndex(*this, factionIndex, starIndex)];
    if (knowledge.ownerKnown && knowledge.ownerKnownAt > observedTime) return;
    knowledge.ownerKnown = true;
    knowledge.ownerFaction = ownerFaction;
    knowledge.ownerKnownAt = observedTime;
    knowledge.visited = knowledge.visited || visited;
    if (factionIndex == playerFaction && starIndex >= 0 && starIndex < int(playerKnowledge.size())) {
        playerKnowledge[starIndex] = knowledge;
    }
}

void Game::applyMarketKnowledge(int factionIndex, int starIndex, const std::vector<double>& prices, const std::vector<double>& supplyPressure, const std::vector<double>& demandPressure, double observedTime) {
    if (!validFaction(*this, factionIndex) || !validStar(*this, starIndex) || prices.empty()) return;
    resizeFactionKnowledge();

    FactionMarketKnowledge& knowledge = factionMarketKnowledge[factionKnowledgeIndex(*this, factionIndex, starIndex)];
    if (knowledge.known && knowledge.observedAt > observedTime) return;
    knowledge.known = true;
    knowledge.observedAt = observedTime;
    knowledge.averageSupplyPressure = averageValue(supplyPressure);
    knowledge.averageDemandPressure = averageValue(demandPressure);

    const size_t count = std::min(elementCount(), prices.size());
    for (size_t i = 0; i < count; ++i) {
        factionMarketPrices[factionMarketPriceIndex(*this, factionIndex, starIndex, int(i))] = prices[i];
        if (i < supplyPressure.size()) {
            factionMarketSupplyPressure[factionMarketPriceIndex(*this, factionIndex, starIndex, int(i))] = supplyPressure[i];
        }
        if (i < demandPressure.size()) {
            factionMarketDemandPressure[factionMarketPriceIndex(*this, factionIndex, starIndex, int(i))] = demandPressure[i];
        }
    }
}

void Game::queueOwnerSignal(int factionIndex, int subjectStar, int originStar) {
    if (!validFaction(*this, factionIndex) || !validStar(*this, subjectStar) || !validStar(*this, originStar)) return;

    const int owner = cluster.stars[subjectStar].ownerFaction;
    const int destination = nearestSignalRelay(*this, factionIndex, originStar);
    if (!validStar(*this, destination)) return;

    SignalPacket queued;
    queued.type = SignalType::OwnerReport;
    queued.observedTime = time;
    queued.subjectStar = subjectStar;
    queued.sourceFaction = owner;
    queued.recipientFaction = factionIndex;
    queued.ownerFaction = owner;
    if (!startSignalRoute(*this, queued, originStar, destination, time)) return;
    mergeSignalAtStar(*this, originStar, queued);
    if (queued.arrivalTime <= time + 0.000001) {
        if (factionIndex == playerFaction && playerAtStar(originStar)) {
            absorbLocalSignalsForFaction(factionIndex, originStar, true);
        }
        return;
    }

    enqueuePendingSignal(*this, queued);
}

void Game::queueMarketSignal(int factionIndex, int subjectStar, int originStar) {
    if (!validFaction(*this, factionIndex) || !validStar(*this, subjectStar) || !validStar(*this, originStar)) return;
    if (subjectStar >= int(markets.size())) return;

    const int destination = nearestSignalRelay(*this, factionIndex, originStar);
    if (!validStar(*this, destination)) return;

    const std::vector<double> supplyPressure = marketSupplyPressureSnapshot(markets[subjectStar]);
    const std::vector<double> demandPressure = marketDemandPressureSnapshot(markets[subjectStar]);

    SignalPacket queued;
    queued.type = SignalType::MarketReport;
    queued.observedTime = time;
    queued.subjectStar = subjectStar;
    queued.recipientFaction = factionIndex;
    queued.marketPrices = markets[subjectStar].prices;
    queued.marketSupplyPressure = supplyPressure;
    queued.marketDemandPressure = demandPressure;
    if (!startSignalRoute(*this, queued, originStar, destination, time)) return;
    mergeSignalAtStar(*this, originStar, queued);
    if (queued.arrivalTime <= time + 0.000001) {
        if (factionIndex == playerFaction && playerAtStar(originStar)) {
            absorbLocalSignalsForFaction(factionIndex, originStar, true);
        }
        return;
    }

    enqueuePendingSignal(*this, queued);
}

void Game::queueContractSignal(int factionIndex, int contractId, int originStar, int subjectStar) {
    if (!validFaction(*this, factionIndex) || !validStar(*this, originStar)) return;
    const int destination = nearestSignalRelay(*this, factionIndex, originStar);
    if (!validStar(*this, destination)) return;

    SignalPacket signal;
    signal.type = SignalType::ContractReport;
    signal.observedTime = time;
    signal.subjectStar = validStar(*this, subjectStar) ? subjectStar : originStar;
    signal.recipientFaction = factionIndex;
    signal.sourceFaction = factionIndex;
    signal.contractId = contractId;
    if (const Contract* contract = contractById(*this, contractId)) {
        signal.sourceFaction = contract->issuerFaction;
        fillSignalContractSnapshot(signal, *contract);
    }
    if (!startSignalRoute(*this, signal, originStar, destination, time)) return;
    mergeSignalAtStar(*this, originStar, signal);
    if (signal.arrivalTime <= time + 0.000001) return;
    enqueuePendingSignal(*this, signal);
}

void Game::queueCombatSignal(int factionIndex, int originStar, int sourceAgent, int targetAgent, double value, unsigned long long eventId) {
    if (!validFaction(*this, factionIndex) || !validStar(*this, originStar)) return;
    const int destination = nearestSignalRelay(*this, factionIndex, originStar);
    if (!validStar(*this, destination)) return;
    if (eventId == 0) eventId = allocateSignalEventId(*this);

    SignalPacket signal;
    signal.type = SignalType::CombatReport;
    signal.eventId = eventId;
    signal.observedTime = time;
    signal.subjectStar = originStar;
    signal.recipientFaction = factionIndex;
    signal.sourceAgent = sourceAgent;
    signal.targetAgent = targetAgent;
    signal.sourceFaction = sourceAgent >= 0 && sourceAgent < int(agents.size()) ? agents[sourceAgent].ship.ownerFaction : -1;
    signal.targetFaction = targetAgent >= 0 && targetAgent < int(agents.size()) ? agents[targetAgent].ship.ownerFaction : -1;
    signal.amount = value;
    if (targetAgent >= 0 && targetAgent < int(agents.size())) {
        signal.relationValue = cargoValueAt(*this, agents[targetAgent], originStar);
    }
    if (!startSignalRoute(*this, signal, originStar, destination, time)) return;
    mergeSignalAtStar(*this, originStar, signal);
    if (signal.arrivalTime <= time + 0.000001) return;
    enqueuePendingSignal(*this, signal);
}

void Game::queueSettlementSignal(int factionIndex, int originStar, double amount, unsigned long long eventId) {
    if (!validFaction(*this, factionIndex) || !validStar(*this, originStar)) return;
    const int destination = nearestSignalRelay(*this, factionIndex, originStar);
    if (!validStar(*this, destination)) return;
    if (eventId == 0) eventId = allocateSignalEventId(*this);

    SignalPacket signal;
    signal.type = SignalType::SettlementReport;
    signal.eventId = eventId;
    signal.observedTime = time;
    signal.subjectStar = originStar;
    signal.recipientFaction = factionIndex;
    signal.sourceFaction = factionIndex;
    signal.amount = amount;
    if (!startSignalRoute(*this, signal, originStar, destination, time)) return;
    mergeSignalAtStar(*this, originStar, signal);
    if (signal.arrivalTime <= time + 0.000001) return;
    enqueuePendingSignal(*this, signal);
}

void Game::queueDiplomacySignal(int factionIndex, int originStar, int targetFaction, int relationValue) {
    if (!validFaction(*this, factionIndex) || !validFaction(*this, targetFaction) || !validStar(*this, originStar)) return;
    const int destination = nearestSignalRelay(*this, factionIndex, originStar);
    if (!validStar(*this, destination)) return;

    SignalPacket signal;
    signal.type = SignalType::DiplomacyReport;
    signal.observedTime = time;
    signal.subjectStar = originStar;
    signal.recipientFaction = factionIndex;
    signal.sourceFaction = factionIndex;
    signal.targetFaction = targetFaction;
    signal.relationValue = relationValue;
    if (!startSignalRoute(*this, signal, originStar, destination, time)) return;
    mergeSignalAtStar(*this, originStar, signal);
    if (signal.arrivalTime <= time + 0.000001) return;
    enqueuePendingSignal(*this, signal);
}

static void recordLocalThreatSignal(Game& game, int factionIndex, int starIndex, int sourceAgent, int targetAgent, double threatValue, double cargoValue, bool piracy, double observedTime) {
    if (!validFaction(game, factionIndex) || !validStar(game, starIndex)) return;
    if (sourceAgent >= int(game.agents.size()) || targetAgent >= int(game.agents.size())) return;

    SignalPacket signal;
    signal.type = SignalType::CombatReport;
    signal.observedTime = observedTime;
    signal.sendTime = observedTime;
    signal.arrivalTime = observedTime;
    signal.originStar = starIndex;
    signal.destinationStar = starIndex;
    signal.hopStar = starIndex;
    signal.subjectStar = starIndex;
    signal.recipientFaction = factionIndex;
    signal.sourceAgent = sourceAgent;
    signal.targetAgent = targetAgent;
    signal.sourceFaction = sourceAgent >= 0 ? game.agents[sourceAgent].ship.ownerFaction : -1;
    signal.targetFaction = targetAgent >= 0 ? game.agents[targetAgent].ship.ownerFaction : factionIndex;
    signal.amount = std::max(0.0, threatValue);
    signal.relationValue = std::max(0.0, cargoValue);
    if (piracy && signal.sourceFaction < 0 && sourceAgent >= 0 && sourceAgent < int(game.agents.size())) {
        signal.sourceFaction = game.agents[sourceAgent].ship.ownerFaction;
    }
    mergeSignalAtStar(game, starIndex, signal);
}

void Game::observeLocalThreatsForFaction(int factionIndex, int starIndex) {
    if (!validFaction(*this, factionIndex) || !validStar(*this, starIndex)) return;

    int reports = 0;
    for (size_t i = 0; i < agents.size(); ++i) {
        const Agent& source = agents[i];
        if (source.ship.enRoute || source.currentStar != starIndex) continue;
        if (!agentIsPiracyThreat(source)) continue;
        if (source.ship.ownerFaction == factionIndex) continue;

        double bestCargo = 0.0;
        int bestTarget = -1;
        for (size_t j = 0; j < agents.size(); ++j) {
            if (i == j) continue;
            const Agent& target = agents[j];
            if (target.ship.enRoute || target.currentStar != starIndex) continue;
            if (target.ship.ownerFaction == source.ship.ownerFaction) continue;
            const double value = cargoValueAt(*this, target, starIndex) + target.money * 0.04;
            if (value > bestCargo) {
                bestCargo = value;
                bestTarget = int(j);
            }
        }
        const double threat = combatPower(*this, source) * (1.0 + source.piracyBias);
        recordLocalThreatSignal(*this, factionIndex, starIndex, int(i), bestTarget, threat, bestCargo, true, time);
        reports += 1;
        if (reports >= 8) break;
    }
}

void Game::seedPlayerKnowledge(int centerStar, double radiusLy) {
    seedFactionKnowledge(playerFaction, centerStar, radiusLy);
}

void Game::observeStar(int starIndex) {
    // (§34) Разведка — это ИССЛЕДОВАНИЕ, и платить она обязана исследованиями.
    //
    // ⚠️ До этой строки очки не давали ни торговля, ни разведка: они капали
    // только с аномалий, добычи и боя (14 точек по коду). То есть игрок,
    // играющий в то, ради чего игра и сделана, — возит грузы между системами —
    // не получал НИ ОДНОГО хромокора за всю партию, а прокачка доставалась
    // тому, кто стреляет. Новый рынок в модели — это новые данные, и цена у
    // них та же, что у обломка в поясе.
    // ⚠️ Спрашиваем ЗАПИСЬ ЗНАНИЯ напрямую, а не `playerKnowsMarket`: тот мерит
    // знание ОТ ТЕКУЩЕЙ ЗВЕЗДЫ ИГРОКА, а `observeStar` зовётся ровно для неё —
    // и `factionKnowsMarketAt(f, X, X)` возвращает true безусловно. Условие было
    // ложным всегда, и обещанные «+3 за новый рынок» не начислялись ни разу.
    const bool freshMarket = validStar(*this, starIndex) &&
                             !factionKnowsMarket(playerFaction, starIndex);
    observeStarForFaction(playerFaction, starIndex);
    observeMarketForFaction(playerFaction, starIndex);
    absorbLocalSignalsForFaction(playerFaction, starIndex, true);
    if (freshMarket) addResearch(3.0, TECH_INTELLECT);
}

void Game::absorbLocalSignalsForFaction(int factionIndex, int observerStar, bool updatePlayerMemory) {
    if (!validFaction(*this, factionIndex) || !validStar(*this, observerStar)) return;
    if (observerStar < 0 || observerStar >= int(signalMemory.size())) return;

    const std::vector<SignalMemoryRecord>& memory = signalMemory[size_t(observerStar)];
    for (const SignalMemoryRecord& record : memory) {
        if (record.recipientFaction != factionIndex || !validStar(*this, record.subjectStar)) continue;
        if (record.type == SignalType::OwnerReport) {
            applyOwnerKnowledge(factionIndex, record.subjectStar, record.ownerFaction, record.observedTime,
                record.subjectStar == observerStar);
        } else if (record.type == SignalType::MarketReport && !record.marketPrices.empty()) {
            applyMarketKnowledge(factionIndex, record.subjectStar, record.marketPrices,
                record.marketSupplyPressure, record.marketDemandPressure, record.observedTime);
        }
    }

    if (!updatePlayerMemory || factionIndex != playerFaction) return;
    for (const SignalMemoryRecord& record : memory) {
        if (record.recipientFaction != playerFaction || record.type != SignalType::OwnerReport || !validStar(*this, record.subjectStar)) continue;
        const size_t index = size_t(record.subjectStar);
        if (index >= playerKnowledge.size()) continue;
        if (!playerKnowledge[index].ownerKnown || playerKnowledge[index].ownerKnownAt <= record.observedTime) {
            playerKnowledge[index].ownerKnown = true;
            playerKnowledge[index].ownerFaction = record.ownerFaction;
            playerKnowledge[index].ownerKnownAt = record.observedTime;
            playerKnowledge[index].visited = playerKnowledge[index].visited || record.subjectStar == observerStar;
        }
    }
}

// --- Репутация игрока у фракций (§24) ---------------------------------------
//
// Одна величина на фракцию, в СДАННЫХ ЗАКАЗАХ. Всё остальное — размер груза,
// дальность, множитель платы, звание — чистые функции от неё, и живут здесь
// же, чтобы генератор, интерфейс и регрессии считали ОДНУ кривую, а не три
// похожие.
// (§47) Книга заводится на каждую фракцию; строка игрока — `playerFaction`.
void Game::resizeFactionLicence() {
    if (factionLicence.size() != factions.size()) {
        factionLicence.resize(factions.size(), FactionLicence());
    }
}

// ⚠️ Запасная строка — не «на всякий случай», а требование безопасности: книгу
// спрашивают из интерфейса и из харнесов до того, как мир засеян (фракций ещё
// нет), и вернуть ссылку в пустой вектор нельзя. Писать в неё бессмысленно, но
// не смертельно: это заведомо ничья лицензия.
static FactionLicence& fallbackLicence() {
    static FactionLicence orphan;
    return orphan;
}

FactionLicence& Game::licenceOf(int factionIndex) {
    resizeFactionLicence();
    if (factionIndex < 0 || factionIndex >= int(factionLicence.size())) return fallbackLicence();
    return factionLicence[size_t(factionIndex)];
}

const FactionLicence& Game::licenceOf(int factionIndex) const {
    if (factionIndex < 0 || factionIndex >= int(factionLicence.size())) return fallbackLicence();
    return factionLicence[size_t(factionIndex)];
}

FactionLicence& Game::licence() { return licenceOf(playerFaction); }
const FactionLicence& Game::licence() const { return licenceOf(playerFaction); }

void Game::resizeFactionReputation() {
    if (factionReputation.size() != factions.size()) {
        factionReputation.resize(factions.size(), 0.0);
    }
}

double Game::factionReputationOf(int factionIndex) const {
    if (factionIndex < 0 || factionIndex >= int(factionReputation.size())) return 0.0;
    return factionReputation[size_t(factionIndex)];
}

// Тир растёт как КОРЕНЬ из доли пройденного пути. Именно этот корень и есть
// «рано видно, верх далеко»: первые десятки заказов заметно двигают потолок,
// но последняя треть лестницы стоит сотен сдач.
double Game::factionJobTier(int factionIndex) const {
    const double share = factionReputationOf(factionIndex) / REPUTATION_CAP_JOBS;
    return std::max(0.0, std::min(1.0, std::sqrt(std::max(0.0, share))));
}

// Масса — экспонента от тира: равные шаги тира дают равные ПРОЦЕНТЫ роста,
// поэтому лестница корпусов проходится ровно, без ступенек и провалов.
double Game::jobCargoForTier(double tier) {
    const double t = std::max(0.0, std::min(1.0, tier));
    return JOB_CARGO_BASE * std::pow(JOB_CARGO_TOP / JOB_CARGO_BASE, t);
}

// Плата за рейс тоже растёт с тиром, и БЫСТРЕЕ инфляции размера: топовый заказ
// обязан быть выгоднее торговли, иначе тысяча сдач ничего не покупает.
double Game::jobPayMultiplierForTier(double tier) {
    const double t = std::max(0.0, std::min(1.0, tier));
    return std::pow(JOB_TIER_PAY_TOP, t);
}

const char* Game::jobRankName(double tier) {
    if (tier < 0.08) return "NOBODY";
    if (tier < 0.22) return "KNOWN";
    if (tier < 0.40) return "TRUSTED";
    if (tier < 0.60) return "CONTRACTOR";
    if (tier < 0.80) return "MASTER";
    if (tier < 0.95) return "LEGEND";
    return "PRIME CARRIER";
}

// --- Флот как ёмкость (§24) --------------------------------------------------
//
// Крупный заказ не влезает в один корпус, и это не препятствие, а содержание:
// «взять» его можно, только если СУММАРНЫЙ свободный трюм бортов игрока,
// СТОЯЩИХ В ЭТОЙ системе, покрывает груз. Борт в пути не считается — он не
// здесь. Отсюда и смысл лицензий: флот перестаёт быть украшением.
double Game::playerFleetCapacityAt(int starIndex) const {
    if (!validStar(*this, starIndex)) return 0.0;
    double free = 0.0;
    for (const Agent& agent : agents) {
        if (!agent.playerControlled || agent.ship.enRoute || agent.currentStar != starIndex) continue;
        free += std::max(0.0, agent.ship.cargoCapacity - shipCargoMass(agent.ship));
    }
    return free;
}

double Game::contractCargoMass(const Contract& contract) const {
    if (!contractUsesCargo(contract.type)) return 0.0;
    if (contract.resource < 0 || contract.resource >= int(elementCount())) return 0.0;
    return contract.amount * resourceUnitMassByIndex(contract.resource);
}

bool Game::playerFleetFitsContract(int contractId) const {
    const Contract* contract = contractById(*this, contractId);
    if (!contract) return false;
    const double needed = contractCargoMass(*contract);
    if (needed <= 0.0) return true;
    return playerFleetCapacityAt(contract->originStar) + 0.001 >= needed;
}

void Game::resizeFactionRelations() {
    const size_t count = factions.size();
    const size_t wanted = count * count;
    if (factionRelations.size() != wanted) {
        factionRelations.assign(wanted, 0);
    }
    for (size_t i = 0; i < count; ++i) {
        factions[i].relationRowOffset = int(i * count);
        factionRelations[i * count + i] = 128;
    }
}

int Game::factionRelation(int factionA, int factionB) const {
    if (!validFaction(*this, factionA) || !validFaction(*this, factionB)) return 0;
    const size_t index = factionRelationIndex(*this, factionA, factionB);
    return index < factionRelations.size() ? factionRelations[index] : 0;
}

void Game::setFactionRelation(int factionA, int factionB, int value) {
    if (!validFaction(*this, factionA) || !validFaction(*this, factionB)) return;
    resizeFactionRelations();
    factionRelations[factionRelationIndex(*this, factionA, factionB)] = clampRelation(value);
}

void Game::adjustFactionRelation(int factionA, int factionB, int delta) {
    if (!validFaction(*this, factionA) || !validFaction(*this, factionB) || factionA == factionB) return;
    setFactionRelation(factionA, factionB, factionRelation(factionA, factionB) + delta);
    setFactionRelation(factionB, factionA, factionRelation(factionB, factionA) + delta);
}

bool Game::factionKnowsOwner(int factionIndex, int starIndex) const {
    if (!validFaction(*this, factionIndex) || !validStar(*this, starIndex)) return false;
    const size_t index = factionKnowledgeIndex(*this, factionIndex, starIndex);
    return index < factionKnowledge.size() && factionKnowledge[index].ownerKnown;
}

int Game::factionKnownOwner(int factionIndex, int starIndex) const {
    if (!factionKnowsOwner(factionIndex, starIndex)) return -2;
    return factionKnowledge[factionKnowledgeIndex(*this, factionIndex, starIndex)].ownerFaction;
}

double Game::factionKnownOwnerAge(int factionIndex, int starIndex) const {
    if (!factionKnowsOwner(factionIndex, starIndex)) return -1.0;
    return std::max(0.0, time - factionKnowledge[factionKnowledgeIndex(*this, factionIndex, starIndex)].ownerKnownAt);
}

bool Game::factionKnowsMarket(int factionIndex, int starIndex) const {
    if (!validFaction(*this, factionIndex) || !validStar(*this, starIndex)) return false;
    const size_t index = factionKnowledgeIndex(*this, factionIndex, starIndex);
    return index < factionMarketKnowledge.size() && factionMarketKnowledge[index].known;
}

double Game::factionKnownPrice(int factionIndex, int starIndex, int elementIndex) const {
    if (!factionKnowsMarket(factionIndex, starIndex) || elementIndex < 0 || elementIndex >= int(elementCount())) return 0.0;
    const size_t index = factionMarketPriceIndex(*this, factionIndex, starIndex, elementIndex);
    return index < factionMarketPrices.size() ? factionMarketPrices[index] : 0.0;
}

double Game::factionKnownSupplyPressure(int factionIndex, int starIndex, int elementIndex) const {
    if (!factionKnowsMarket(factionIndex, starIndex) || elementIndex < 0 || elementIndex >= int(elementCount())) return 1.0;
    const size_t index = factionMarketPriceIndex(*this, factionIndex, starIndex, elementIndex);
    return index < factionMarketSupplyPressure.size() ? factionMarketSupplyPressure[index] : 1.0;
}

double Game::factionKnownDemandPressure(int factionIndex, int starIndex, int elementIndex) const {
    if (!factionKnowsMarket(factionIndex, starIndex) || elementIndex < 0 || elementIndex >= int(elementCount())) return 1.0;
    const size_t index = factionMarketPriceIndex(*this, factionIndex, starIndex, elementIndex);
    return index < factionMarketDemandPressure.size() ? factionMarketDemandPressure[index] : 1.0;
}

double Game::factionKnownMarketAge(int factionIndex, int starIndex) const {
    if (!factionKnowsMarket(factionIndex, starIndex)) return -1.0;
    return std::max(0.0, time - factionMarketKnowledge[factionKnowledgeIndex(*this, factionIndex, starIndex)].observedAt);
}

double Game::factionKnownMarketConfidence(int factionIndex, int starIndex, int elementIndex) const {
    if (!factionKnowsMarket(factionIndex, starIndex) || elementIndex < 0 || elementIndex >= int(elementCount())) return 0.0;
    const double age = factionKnownMarketAge(factionIndex, starIndex);
    if (age < 0.0) return 0.0;
    const double tau = marketMemoryTau(elementDefinitions()[elementIndex]);
    return std::max(0.0, std::min(1.0, std::exp(-age / tau)));
}

bool Game::factionKnowsOwnerAt(int factionIndex, int observerStar, int starIndex) const {
    if (!validFaction(*this, factionIndex) || !validStar(*this, observerStar) || !validStar(*this, starIndex)) return false;
    if (observerStar == starIndex) return true;
    if (latestSignalMemoryRecord(*this, observerStar, SignalType::OwnerReport, factionIndex, starIndex)) return true;
    return factionKnowsOwner(factionIndex, starIndex);
}

int Game::factionKnownOwnerAt(int factionIndex, int observerStar, int starIndex) const {
    if (!validFaction(*this, factionIndex) || !validStar(*this, observerStar) || !validStar(*this, starIndex)) return -2;
    if (observerStar == starIndex) return cluster.stars[starIndex].ownerFaction;
    if (const SignalMemoryRecord* record = latestSignalMemoryRecord(*this, observerStar, SignalType::OwnerReport, factionIndex, starIndex)) {
        return record->ownerFaction;
    }
    return factionKnownOwner(factionIndex, starIndex);
}

double Game::factionKnownOwnerAgeAt(int factionIndex, int observerStar, int starIndex) const {
    if (!validFaction(*this, factionIndex) || !validStar(*this, observerStar) || !validStar(*this, starIndex)) return -1.0;
    if (observerStar == starIndex) return 0.0;
    if (const SignalMemoryRecord* record = latestSignalMemoryRecord(*this, observerStar, SignalType::OwnerReport, factionIndex, starIndex)) {
        return std::max(0.0, time - record->observedTime);
    }
    return factionKnownOwnerAge(factionIndex, starIndex);
}

bool Game::factionKnowsMarketAt(int factionIndex, int observerStar, int starIndex) const {
    if (!validFaction(*this, factionIndex) || !validStar(*this, observerStar) || !validStar(*this, starIndex)) return false;
    if (observerStar == starIndex && starIndex < int(markets.size())) return true;
    const SignalMemoryRecord* record = latestSignalMemoryRecord(*this, observerStar, SignalType::MarketReport, factionIndex, starIndex);
    if (record && !record->marketPrices.empty()) return true;
    return factionKnowsMarket(factionIndex, starIndex);
}

double Game::factionKnownPriceAt(int factionIndex, int observerStar, int starIndex, int elementIndex) const {
    if (!validFaction(*this, factionIndex) || !validStar(*this, observerStar) || !validStar(*this, starIndex) ||
        elementIndex < 0 || elementIndex >= int(elementCount())) return 0.0;
    if (observerStar == starIndex && starIndex < int(markets.size()) && elementIndex < int(markets[starIndex].prices.size())) {
        return markets[starIndex].prices[elementIndex];
    }
    if (const SignalMemoryRecord* record = latestSignalMemoryRecord(*this, observerStar, SignalType::MarketReport, factionIndex, starIndex)) {
        if (elementIndex < int(record->marketPrices.size())) return record->marketPrices[elementIndex];
    }
    return factionKnownPrice(factionIndex, starIndex, elementIndex);
}

double Game::factionKnownSupplyPressureAt(int factionIndex, int observerStar, int starIndex, int elementIndex) const {
    if (!validFaction(*this, factionIndex) || !validStar(*this, observerStar) || !validStar(*this, starIndex) ||
        elementIndex < 0 || elementIndex >= int(elementCount())) return 1.0;
    if (observerStar == starIndex && starIndex < int(markets.size())) {
        const std::vector<double> pressure = marketSupplyPressureSnapshot(markets[starIndex]);
        return elementIndex < int(pressure.size()) ? pressure[elementIndex] : 1.0;
    }
    if (const SignalMemoryRecord* record = latestSignalMemoryRecord(*this, observerStar, SignalType::MarketReport, factionIndex, starIndex)) {
        if (elementIndex < int(record->marketSupplyPressure.size())) return record->marketSupplyPressure[elementIndex];
    }
    return factionKnownSupplyPressure(factionIndex, starIndex, elementIndex);
}

double Game::factionKnownDemandPressureAt(int factionIndex, int observerStar, int starIndex, int elementIndex) const {
    if (!validFaction(*this, factionIndex) || !validStar(*this, observerStar) || !validStar(*this, starIndex) ||
        elementIndex < 0 || elementIndex >= int(elementCount())) return 1.0;
    if (observerStar == starIndex && starIndex < int(markets.size())) {
        const std::vector<double> pressure = marketDemandPressureSnapshot(markets[starIndex]);
        return elementIndex < int(pressure.size()) ? pressure[elementIndex] : 1.0;
    }
    if (const SignalMemoryRecord* record = latestSignalMemoryRecord(*this, observerStar, SignalType::MarketReport, factionIndex, starIndex)) {
        if (elementIndex < int(record->marketDemandPressure.size())) return record->marketDemandPressure[elementIndex];
    }
    return factionKnownDemandPressure(factionIndex, starIndex, elementIndex);
}

double Game::factionKnownMarketAgeAt(int factionIndex, int observerStar, int starIndex) const {
    if (!validFaction(*this, factionIndex) || !validStar(*this, observerStar) || !validStar(*this, starIndex)) return -1.0;
    if (observerStar == starIndex && starIndex < int(markets.size())) return 0.0;
    if (const SignalMemoryRecord* record = latestSignalMemoryRecord(*this, observerStar, SignalType::MarketReport, factionIndex, starIndex)) {
        return std::max(0.0, time - record->observedTime);
    }
    return factionKnownMarketAge(factionIndex, starIndex);
}

double Game::factionKnownMarketConfidenceAt(int factionIndex, int observerStar, int starIndex, int elementIndex) const {
    if (!factionKnowsMarketAt(factionIndex, observerStar, starIndex) || elementIndex < 0 || elementIndex >= int(elementCount())) return 0.0;
    if (observerStar == starIndex) return 1.0;
    const double age = factionKnownMarketAgeAt(factionIndex, observerStar, starIndex);
    if (age < 0.0) return 0.0;
    const double tau = marketMemoryTau(elementDefinitions()[elementIndex]);
    return std::max(0.0, std::min(1.0, std::exp(-age / tau)));
}

bool Game::playerAtStar(int starIndex) const {
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return false;
    const Agent& player = agents[playerAgent];
    return !player.ship.enRoute && player.currentStar == starIndex;
}

bool Game::playerCanSeeAgent(int agentIndex) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    if (agentIndex == playerAgent) return true;
    const Agent& agent = agents[agentIndex];
    return !agent.ship.enRoute && playerAtStar(agent.currentStar);
}

int Game::playerVisibleAgentCount() const {
    int count = 0;
    for (size_t i = 0; i < agents.size(); ++i) {
        if (playerCanSeeAgent(int(i))) count += 1;
    }
    return count;
}

int Game::factionKnownThreatCount(int factionIndex, int starIndex) const {
    if (!validFaction(*this, factionIndex)) return 0;
    const int observerStar = factionObserverStar(*this, factionIndex);
    if (!validStar(*this, observerStar) || observerStar >= int(signalMemory.size())) return 0;

    int count = 0;
    const std::vector<SignalMemoryRecord>& memory = signalMemory[size_t(observerStar)];
    for (const SignalMemoryRecord& record : memory) {
        if (!usableThreatSignal(*this, factionIndex, record, false)) continue;
        if (starIndex >= 0 && record.subjectStar != starIndex) continue;
        count += 1;
    }
    return count;
}

double Game::factionKnownThreatAge(int factionIndex, int starIndex) const {
    if (!validFaction(*this, factionIndex)) return -1.0;
    const int observerStar = factionObserverStar(*this, factionIndex);
    if (!validStar(*this, observerStar) || observerStar >= int(signalMemory.size())) return -1.0;

    double bestAge = std::numeric_limits<double>::max();
    const std::vector<SignalMemoryRecord>& memory = signalMemory[size_t(observerStar)];
    for (const SignalMemoryRecord& record : memory) {
        if (!usableThreatSignal(*this, factionIndex, record, false)) continue;
        if (starIndex >= 0 && record.subjectStar != starIndex) continue;
        const double age = time - record.observedTime;
        if (age <= 24.0) bestAge = std::min(bestAge, std::max(0.0, age));
    }
    return bestAge == std::numeric_limits<double>::max() ? -1.0 : bestAge;
}

double Game::factionKnownThreatRisk(int factionIndex, int starIndex) const {
    if (!validFaction(*this, factionIndex) || !validStar(*this, starIndex)) return 0.0;
    const int observerStar = factionObserverStar(*this, factionIndex);
    if (!validStar(*this, observerStar) || observerStar >= int(signalMemory.size())) return 0.0;

    double risk = 0.0;
    const std::vector<SignalMemoryRecord>& memory = signalMemory[size_t(observerStar)];
    for (const SignalMemoryRecord& record : memory) {
        if (!usableThreatSignal(*this, factionIndex, record, false)) continue;
        const double distance = distanceBetween(cluster.stars[starIndex], cluster.stars[record.subjectStar]);
        if (distance > 18.0) continue;
        risk += signalThreatValue(*this, factionIndex, record) / (1.0 + distance * 0.28);
    }
    return std::min(3.0, std::max(0.0, risk));
}

double Game::factionRouteThreatRisk(int factionIndex, int originStar, int targetStar) const {
    if (!validFaction(*this, factionIndex) || !validStar(*this, originStar) || !validStar(*this, targetStar)) return 0.0;
    if (originStar >= int(signalMemory.size())) return 0.0;

    const ClusterStar& origin = cluster.stars[originStar];
    const ClusterStar& target = cluster.stars[targetStar];
    double risk = 0.0;
    const std::vector<SignalMemoryRecord>& memory = signalMemory[size_t(originStar)];
    for (const SignalMemoryRecord& record : memory) {
        if (!usableThreatSignal(*this, factionIndex, record, false)) continue;
        const double distance = distancePointToSegment(origin, target, cluster.stars[record.subjectStar]);
        if (distance > 26.0) continue;
        const double corridor = 1.0 / (1.0 + distance * 0.18);
        risk += signalThreatValue(*this, factionIndex, record) * corridor;
    }
    return std::min(3.0, std::max(0.0, risk));
}

bool Game::playerKnowsOwner(int starIndex) const {
    if (validFaction(*this, playerFaction) && playerAgent >= 0 && playerAgent < int(agents.size()) && !agents[playerAgent].ship.enRoute) {
        if (factionKnowsOwnerAt(playerFaction, agents[playerAgent].currentStar, starIndex)) return true;
    }
    if (validFaction(*this, playerFaction) && factionKnowsOwner(playerFaction, starIndex)) return true;
    return starIndex >= 0 && starIndex < int(playerKnowledge.size()) && playerKnowledge[starIndex].ownerKnown;
}

int Game::playerKnownOwner(int starIndex) const {
    if (validFaction(*this, playerFaction) && playerAgent >= 0 && playerAgent < int(agents.size()) && !agents[playerAgent].ship.enRoute &&
        factionKnowsOwnerAt(playerFaction, agents[playerAgent].currentStar, starIndex)) {
        return factionKnownOwnerAt(playerFaction, agents[playerAgent].currentStar, starIndex);
    }
    if (validFaction(*this, playerFaction) && factionKnowsOwner(playerFaction, starIndex)) {
        return factionKnownOwner(playerFaction, starIndex);
    }
    if (starIndex < 0 || starIndex >= int(playerKnowledge.size()) || !playerKnowledge[starIndex].ownerKnown) return -2;
    return playerKnowledge[starIndex].ownerFaction;
}

double Game::playerKnownOwnerAge(int starIndex) const {
    if (validFaction(*this, playerFaction) && playerAgent >= 0 && playerAgent < int(agents.size()) && !agents[playerAgent].ship.enRoute &&
        factionKnowsOwnerAt(playerFaction, agents[playerAgent].currentStar, starIndex)) {
        return factionKnownOwnerAgeAt(playerFaction, agents[playerAgent].currentStar, starIndex);
    }
    if (validFaction(*this, playerFaction) && factionKnowsOwner(playerFaction, starIndex)) {
        return factionKnownOwnerAge(playerFaction, starIndex);
    }
    if (starIndex < 0 || starIndex >= int(playerKnowledge.size()) || !playerKnowledge[starIndex].ownerKnown) return -1.0;
    return std::max(0.0, time - playerKnowledge[starIndex].ownerKnownAt);
}

bool Game::playerKnowsMarket(int starIndex) const {
    if (!validFaction(*this, playerFaction) || !validStar(*this, starIndex)) return false;
    if (playerAgent >= 0 && playerAgent < int(agents.size()) && !agents[playerAgent].ship.enRoute) {
        return factionKnowsMarketAt(playerFaction, agents[playerAgent].currentStar, starIndex);
    }
    return factionKnowsMarket(playerFaction, starIndex);
}

double Game::playerKnownPrice(int starIndex, int elementIndex) const {
    if (!validFaction(*this, playerFaction) || !validStar(*this, starIndex)) return 0.0;
    if (playerAgent >= 0 && playerAgent < int(agents.size()) && !agents[playerAgent].ship.enRoute) {
        return factionKnownPriceAt(playerFaction, agents[playerAgent].currentStar, starIndex, elementIndex);
    }
    return factionKnownPrice(playerFaction, starIndex, elementIndex);
}

double Game::playerKnownSupplyPressure(int starIndex, int elementIndex) const {
    if (!validFaction(*this, playerFaction) || !validStar(*this, starIndex)) return 1.0;
    if (playerAgent >= 0 && playerAgent < int(agents.size()) && !agents[playerAgent].ship.enRoute) {
        return factionKnownSupplyPressureAt(playerFaction, agents[playerAgent].currentStar, starIndex, elementIndex);
    }
    return factionKnownSupplyPressure(playerFaction, starIndex, elementIndex);
}

double Game::playerKnownDemandPressure(int starIndex, int elementIndex) const {
    if (!validFaction(*this, playerFaction) || !validStar(*this, starIndex)) return 1.0;
    if (playerAgent >= 0 && playerAgent < int(agents.size()) && !agents[playerAgent].ship.enRoute) {
        return factionKnownDemandPressureAt(playerFaction, agents[playerAgent].currentStar, starIndex, elementIndex);
    }
    return factionKnownDemandPressure(playerFaction, starIndex, elementIndex);
}

double Game::playerKnownMarketAge(int starIndex) const {
    if (!validFaction(*this, playerFaction) || !validStar(*this, starIndex)) return -1.0;
    if (playerAgent >= 0 && playerAgent < int(agents.size()) && !agents[playerAgent].ship.enRoute) {
        return factionKnownMarketAgeAt(playerFaction, agents[playerAgent].currentStar, starIndex);
    }
    return factionKnownMarketAge(playerFaction, starIndex);
}

double Game::playerKnownMarketConfidence(int starIndex, int elementIndex) const {
    if (!validFaction(*this, playerFaction) || !validStar(*this, starIndex)) return 0.0;
    if (playerAgent >= 0 && playerAgent < int(agents.size()) && !agents[playerAgent].ship.enRoute) {
        return factionKnownMarketConfidenceAt(playerFaction, agents[playerAgent].currentStar, starIndex, elementIndex);
    }
    return factionKnownMarketConfidence(playerFaction, starIndex, elementIndex);
}

void Game::render() {}
