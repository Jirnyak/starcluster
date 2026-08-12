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

int sampledStarAt(const Game& game, int sampleIndex, int sampleCount) {
    const int count = int(game.cluster.stars.size());
    if (count <= sampleCount) return sampleIndex;
    return randomer(rng, count - 1);
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

// Во сколько кредитов обойдётся долить недостающее в этой системе. Считает
// ОБА расходника: и топливо, и рабочее тело, каждое по своей локальной цене.
double refillCost(const Game& game, const Ship& ship, int starIndex, const RouteCost& need) {
    if (!need.feasible || !validStar(game, starIndex)) return 0.0;
    double propellantPrice = 1.0;
    double fuelPrice = 1.0;
    routePrices(game, ship, starIndex, propellantPrice, fuelPrice);
    double cost = std::max(0.0, need.fuelMass - shipFuelMix(ship).mass) * fuelPrice;
    if (!driveUsesFuelAsPropellant(ship.driveIndex)) {
        cost += std::max(0.0, need.propellantMass - shipPropellantMix(ship).mass) * propellantPrice;
    }
    return cost;
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
    const double payout = contract.reward * lateFactor * (1.0 + earlyBonus);
    agent.money += payout;
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
    if (validFaction(game, contract.issuerFaction)) {
        game.factions[contract.issuerFaction].treasury = std::max(0.0, game.factions[contract.issuerFaction].treasury - payout);
        if (emitSignals) game.queueSettlementSignal(contract.issuerFaction, contract.targetStar, -payout);
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
    const int issuer = validFaction(game, game.cluster.stars[originStar].ownerFaction) ?
        game.cluster.stars[originStar].ownerFaction : -1;
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
    contract.issuerFaction = validFaction(game, issuer) ? issuer : game.cluster.stars[targetStar].ownerFaction;
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
    contract.issuerFaction = game.cluster.stars[originStar].ownerFaction;
    contract.risk = std::min(1.0, contract.risk + game.factionRouteThreatRisk(contract.issuerFaction, originStar, best) * 0.10);
    return finishContract(game, contract);
}

bool tryCreateScoutContract(Game& game, int originStar) {
    if (!validStar(game, originStar)) return false;
    if (activeContractsAtOrigin(game, originStar) >= CONTRACTS_PER_SYSTEM) return false;

    const int issuer = game.cluster.stars[originStar].ownerFaction;
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
    const int issuer = validFaction(game, game.cluster.stars[originStar].ownerFaction) ?
        game.cluster.stars[originStar].ownerFaction : -1;
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
    contract.issuerFaction = validFaction(game, issuer) ? issuer : game.cluster.stars[best].ownerFaction;
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
    const int issuer = game.cluster.stars[originStar].ownerFaction;
    if (!validFaction(game, issuer)) return false;
    const double tier = rollContractTier(game, issuer);

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
    const int issuer = game.cluster.stars[originStar].ownerFaction;
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

TradePlan findBestTrade(const Game& game, const Agent& agent) {
    TradePlan best;
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
        const int dest = sampledStarAt(game, sample, destinationSamples);
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

    if (stoppingDistance + speed * dt * 0.5 >= dist && speed > 0.0) {
        const double brake = consumeAndStoreAsh(ship, std::min(deltaV, speed) * rapidityCost) / rapidityCost;
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
const double REFINERY_MARKUP = 0.90;

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

    // Лицензионный тариф: удерживается ТОЛЬКО с игрока и только с продажи —
    // это и есть то, что зачитывается в квоту периода. NPC живут по прежним
    // правилам (их баланс не трогаем), поэтому для них ставка нулевая.
    const double licenceFee = agent.playerControlled && !game.licenceRevoked
                                  ? gross * game.licenceTariffRate
                                  : 0.0;

    market.applyTrade(resourceIndex, amount);
    market.demand[resourceIndex].amount = std::max(0.0, market.demand[resourceIndex].amount - amount);
    agent.money += gross - fee - licenceFee;
    if (validFaction(game, owner)) {
        game.factions[owner].treasury += fee;
        if (fee > 0.01) game.queueSettlementSignal(owner, starIndex, fee);
    }
    if (licenceFee > 0.0) {
        game.licenceQuotaPaid += licenceFee;
        // Лицензиями и квотами ведает КЛИРИНГОВАЯ ПАЛАТА, а не своя же фракция.
        // Иначе игрок платил тариф сам себе, а после §16 (доступ к казне) ещё и
        // снимал его обратно — квота не стоила ничего (замер: удержано 33.6 Cr,
        // снято обратно 33.6 Cr).
        if (validFaction(game, game.clearingFaction)) game.factions[game.clearingFaction].treasury += licenceFee;
    }
    agent.lastProfit = gross - fee - licenceFee - costShare;
    // (§34) Удачная сделка — тоже знание: рейс проверил модель рынка на деле.
    // Логарифм намеренно: рост капитала на порядки не должен превращать
    // прокачку в формальность, поэтому миллионный куш даёт вдвое больше
    // тысячного, а не в тысячу раз.
    if (agent.playerControlled && agent.lastProfit > 0.0) {
        game.addResearch(1.0 + 2.0 * std::log10(1.0 + agent.lastProfit / 1000.0));
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
        amount = std::min(capAmount, budget / std::max(1e-9, market.prices[plan.elementIndex]));
        for (int pass = 0; pass < 2 && amount > 0.0; ++pass) {
            const double avg = market.executionPrice(plan.elementIndex, amount, false);
            amount = std::min(capAmount, budget / std::max(1e-9, avg));
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
    if (shipFuelMix(agent.ship).mass < need.fuelMass) {
        agent.lastAction = "need fuel";
        return false;
    }
    if (!driveUsesFuelAsPropellant(agent.ship.driveIndex) &&
        shipPropellantMix(agent.ship).mass < need.propellantMass) {
        agent.lastAction = "need propellant";
        return false;
    }
    agent.destStar = destStar;
    agent.ship.targetStar = legStar;
    agent.ship.enRoute = true;
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
    if (game.licenceRevoked) { agent.missionCooldown = 1.0; return; }
    if (!validStar(game, agent.currentStar)) return;
    // В собственной системе всё даром (§13), а `findBestTrade` считает по
    // рыночным ценам и потому оценивает такой рейс неверно. Автопилот из своей
    // колонии не торгует — насос из собственного склада это не торговля.
    if (freeMarketFor(game, agent, agent.currentStar)) { agent.missionCooldown = 1.0; return; }

    sellCargo(game, agent, agent.currentStar);

    const TradePlan plan = findBestTrade(game, agent);
    if (plan.destStar >= 0 && plan.elementIndex >= 0) {
        buyCargo(game, agent, agent.currentStar, plan);
        if (!agent.ship.cargo.empty()) startJourney(game, agent, plan.destStar);
        agent.lastAction = "auto: hauling";
    } else {
        agent.missionCooldown = 0.25;   // константа: ни бита из глобального rng
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
    out << "STARCLUSTER_SAVE 17 " << cluster.stars.size() << '\n';
    out << "SEED " << seed << '\n';
    out << "RNG " << rng << '\n';
    out << "TIME " << time << ' ' << contractUpdateTimer << ' ' << factionUpdateTimer << ' '
        << nextContractId << ' ' << playerAgent << ' ' << playerFaction << ' '
        << boughtSystems << ' ' << capturedSystems << ' ' << nextSignalEventId << ' ' << saveToken(lastEvent) << '\n';
    // clusterPriceLevel идёт в той же строке — он тоже про деньги скопления.
    // До версии 13 он жил в файловом глобале `market.cpp` и в сейв не попадал
    // вовсе, поэтому ставка тарифа зависела от того, сколько времени прожил
    // ПРОЦЕСС, а не партия.
    out << "LICENCE " << licenceQuotaPaid << ' ' << licencePeriodEnd << ' ' << licenceTariffRate << ' '
        << licenceBuyback << ' ' << licenceCount << ' ' << (licenceRevoked ? 1 : 0) << ' '
        << licencePeriodsMet << ' ' << licenceQuotaBase << ' '
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
        out << ' ' << int(contract.targetDown) << '\n';
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
        out << "LC " << c.starIndex << ' ' << c.radioMask << ' ' << c.bountyPaid << ' '
            << c.bountyAt << '\n';
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

    out << "EXOTIC " << exoticStocks.size() << ' ' << coresForged << '\n';
    for (const ExoticStock& e : exoticStocks) {
        out << "EX " << e.starIndex << ' ' << e.updatedAt;
        for (int k = 0; k < EX_COUNT; ++k) out << ' ' << e.stock[k];
        out << '\n';
    }

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
    if (!(in >> tag >> version >> starCount) || tag != "STARCLUSTER_SAVE" ||
        version < 14 || version > 17) {
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

    if (version >= 8) {
        int revoked = 0;
        if (!expectTag(in, "LICENCE") ||
            !(in >> loaded.licenceQuotaPaid >> loaded.licencePeriodEnd >> loaded.licenceTariffRate >>
                loaded.licenceBuyback >> loaded.licenceCount >> revoked >> loaded.licencePeriodsMet >>
                loaded.licenceQuotaBase >> loaded.clusterPriceLevel >> loaded.clusterPriceBase >> loaded.clearingFaction)) {
            lastEvent = "load failed: licence";
            return false;
        }
        loaded.licenceRevoked = revoked != 0;
    } else {
        // Сейв до введения квоты: начинаем новый отчётный период с текущего момента.
        loaded.licencePeriodEnd = loaded.time + LICENCE_PERIOD_YEARS;
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
            for (Contract& contract : loaded.contracts) {
                if (contract.id != id) continue;
                contract.targetDown = targetDown != 0;
                contract.tier = tier;
                contract.rushFactor = rush;
                contract.carriers = carrierList;
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

        if (!expectTag(in, "EXOTIC") ||
            !(in >> count >> loaded.coresForged)) {
            lastEvent = "load failed: exotics";
            return false;
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

    if (!in) {
        lastEvent = "load failed";
        return false;
    }

    *this = loaded;
    rng = loadedRng;
    if (signalMemory.size() != cluster.stars.size()) {
        signalMemory.assign(cluster.stars.size(), std::vector<SignalMemoryRecord>());
    }
    marketUpdatedAt.assign(markets.size(), time - MARKET_UPDATE_INTERVAL_YEARS);
    marketUpdateCursor = 0;
    marketUpdateBudget = 0.0;
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
    licenceQuotaPaid = 0.0;
    licenceQuotaBase = LICENCE_QUOTA_BASE;
    licencePeriodEnd = LICENCE_PERIOD_YEARS;
    licenceTariffRate = LICENCE_TARIFF_BASE;
    licenceBuyback = 0.0;
    licenceCount = 1;
    licenceRevoked = false;
    licencePeriodsMet = 0;
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
    factionBook.clear();
    factionIncome.clear();
    factionBookAt.clear();
    playerShares.clear();
    shareCostBasis.clear();
    factionBookCursor = 0;
    lastEvent = "cluster seeded";
    if (num_stars == 0) return;

    for (size_t i = 0; i < num_stars; ++i) {
        const ClusterStar& star = cluster.stars[i];
        markets[i].seed(star.resources, star.demandBias, star.economyRole, star.population, star.industry);
    }
    marketUpdatedAt.assign(num_stars, time - MARKET_UPDATE_INTERVAL_YEARS);
    rebuildRouteCache();

    const FactionSeed seeds[] = {
        {"Aster Compact", 230, 76, 82, 0.68},
        {"Helion League", 244, 178, 70, 0.42},
        {"Cobalt Mandate", 80, 156, 255, 0.58},
        {"Green Arcology", 95, 210, 128, 0.34},
        {"Violet Synod", 190, 112, 240, 0.72},
        {"White Foundry", 220, 226, 214, 0.50}
    };
    const int factionCount = std::min<int>(6, std::max<int>(2, int(num_stars / 180)));
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
    licenceTariffRate = LICENCE_TARIFF_BASE;
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
    processSignals();
    updateAnomalies(dt);
    updateLicence(dt);
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
        if (market.rescale(star.population, star.industry)) {
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
            item.progress += spend;
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
        publishFactionBook(factionBookCursor);
        factionBookCursor = (factionBookCursor + 1) % int(factions.size());
        // (§37.1) РЕПУТАЦИЯ ОТКРЫВАЕТ КНИГИ. У доверенного возчика отчёт всегда
        // свежий: он и есть тот, кто возит их грузы и видит их склады изнутри.
        //
        // Это единственный выход у репутации, кроме размера заказов: до сих пор
        // она копилась до потолка 1000 и упиралась в звание. А отставание
        // котировки от жизни (§33) — главное, чем биржа вообще интересна:
        // знать раньше рынка и есть заработок.
        for (size_t f = 0; f < factions.size(); ++f) {
            if (int(f) == factionBookCursor) continue;   // эту уже обновили выше
            if (factionJobTier(int(f)) < SHARE_INSIDER_TIER) continue;
            publishFactionBook(int(f));
        }
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

void Game::updateAgents(double dt) {
    for (size_t i = 0; i < agents.size(); ++i) {
        Agent& agent = agents[i];
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
                    }
                    agent.ship.x += agent.ship.vx * dt;
                    agent.ship.y += agent.ship.vy * dt;
                    agent.ship.z += agent.ship.vz * dt;
                }
            } else if (agent.ship.targetStar >= 0 && agent.ship.targetStar < int(cluster.stars.size())) {
                const bool arrived = moveShipToward(agent.ship, cluster.stars[agent.ship.targetStar], dt);
                if (arrived) {
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
            if (victimIndex == playerAgent) rebakePlayerBakedBonuses();
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
            if (attackerIndex == playerAgent) rebakePlayerBakedBonuses();
        } else {
            // Attacker repelled
            attacker.lastAction = "repelled by " + victim.type;
            victim.lastAction = "repelled pirate " + attacker.type;
        }
        return false;
    }
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
    if (agentIndex == playerAgent) rebakePlayerBakedBonuses();
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
    if (agentIndex == playerAgent) {
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

    TradePlan plan;
    plan.destStar = agent.currentStar;
    plan.elementIndex = elementIndex;
    plan.amount = amount;
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

bool Game::agentBuyFuel(int agentIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute || !validStar(*this, agent.currentStar)) return false;
    // Заливаем обе ёмкости под пробку по станционной цене (с наценкой за очистку).
    // Цель задаём в массе: сколько влезет по объёму на текущем составе.
    const int fuelElem = shipDominantFuelElement(agent.ship);
    const double fuelTarget = shipFuelTankVolume(agent.ship) /
        std::max(1e-9, elementUnitVolume(fuelElem)) * elementUnitMass(fuelElem);
    bool any = buyConsumable(*this, agent, agent.currentStar, true, fuelTarget);
    if (!driveUsesFuelAsPropellant(agent.ship.driveIndex)) {
        const int propElem = shipDominantPropellantElement(agent.ship);
        const double propTarget = agent.ship.propellantVolume /
            std::max(1e-9, elementUnitVolume(propElem)) * elementUnitMass(propElem);
        any = buyConsumable(*this, agent, agent.currentStar, false, propTarget) || any;
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
    if (agent.playerControlled && playerTradingBlocked()) return false;
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
    if (agent.playerControlled && playerTradingBlocked()) return 0;
    if (agent.ship.enRoute || !validStar(*this, agent.currentStar)) return 0;

    // Партии сдаются по одной с головы вектора: sellCargo сам стирает опустевшую,
    // поэтому индекс не нужен. Ограничитель по числу партий — от зацикливания,
    // если очередная партия окажется меньше порога 0.01 и sellCargo вернёт false
    // (тогда вектор не уменьшится и цикл обязан прерваться).
    int lots = 0;
    size_t guard = agent.ship.cargo.size();
    while (!agent.ship.cargo.empty() && guard-- > 0) {
        const size_t before = agent.ship.cargo.size();
        if (!sellCargo(*this, agent, agent.currentStar)) break;
        if (agent.ship.cargo.size() >= before) break;
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

    const TradePlan plan = findBestTrade(*this, agent);
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

    const int seller = star.ownerFaction;
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
// формула сводится к `licenceQuotaBase * licenceCount`, и планка считается в
// уме — три борта, тридцать тысяч (§21: читаемость шкалы важнее точности).
double Game::licenceQuotaTarget() const {
    return licenceQuotaBase * (1.0 + LICENCE_QUOTA_PER_EXTRA * double(std::max(0, licenceCount - 1)));
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
    licenceTariffRate = std::max(LICENCE_TARIFF_MIN,
                                 std::min(LICENCE_TARIFF_MAX, LICENCE_TARIFF_BASE * drift));

    if (time < licencePeriodEnd) return;

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
    licenceTariffRate = std::max(LICENCE_TARIFF_MIN,
                                 std::min(LICENCE_TARIFF_MAX, LICENCE_TARIFF_BASE * reconciledDrift));
    pushNews("Millennial relativistic market correction: cluster prices at " +
             std::to_string(int(reconciledDrift * 100.0 + 0.5)) + "% of founding, tariff now " +
             std::to_string(int(licenceTariffRate * 1000.0 + 0.5)) + " per mille.", 1);
    if (licenceQuotaPaid + 1e-6 >= target) {
        licencePeriodsMet += 1;
        pushNews("Licence renewed: quota met (" + std::to_string(int(licenceQuotaPaid)) +
                 "/" + std::to_string(int(target)) + " Cr).", 4);
        lastEvent = "licence renewed";
    } else if (!licenceRevoked) {
        // Отзыв: торговля замерзает, пока игрок не выкупит лицензию. Добыча (M),
        // контракты и локальный полёт продолжают работать — есть чем откопаться.
        const double shortfall = target - licenceQuotaPaid;
        licenceRevoked = true;
        licenceBuyback = std::max(LICENCE_BUYBACK_MIN, shortfall * LICENCE_BUYBACK_K);
        pushNews("LICENCE REVOKED: quota short by " + std::to_string(int(std::ceil(shortfall))) +
                 " Cr. Trading frozen until bought back (" + std::to_string(int(licenceBuyback)) + " Cr).", 2);
        lastEvent = "licence revoked - trading frozen";
    }
    // Планка ползёт вверх независимо от исхода: скопление богатеет, и вечно жить
    // на одном отработанном маршруте не выйдет — геймплей обязан двигаться.
    licenceQuotaBase *= LICENCE_QUOTA_GROWTH;
    licenceQuotaPaid = 0.0;
    licencePeriodEnd = time + LICENCE_PERIOD_YEARS;
}

bool Game::playerBuybackLicence() {
    if (!licenceRevoked) {
        lastEvent = "licence is valid";
        return false;
    }
    if (playerAgent < 0 || playerAgent >= int(agents.size())) return false;
    Agent& player = agents[playerAgent];
    if (player.money < licenceBuyback) {
        lastEvent = "buyback needs " + std::to_string(int(std::ceil(licenceBuyback))) + " Cr";
        return false;
    }
    player.money -= licenceBuyback;
    // Выкуп идёт в казну лицензиара — деньги не исчезают из экономики.
    if (validFaction(*this, clearingFaction)) factions[clearingFaction].treasury += licenceBuyback;
    licenceRevoked = false;
    licenceBuyback = 0.0;
    licenceQuotaPaid = 0.0;
    licencePeriodEnd = time + LICENCE_PERIOD_YEARS;
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

    const double sellTariff = licenceTariffRate;    // лицензионный тариф удержат с продажи
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
            if (elementFilter < 0 && profit <= worst) continue;

            ArbitrageDeal deal;
            deal.element = e;
            deal.targetStar = target;
            deal.buyPrice = cost / units;
            deal.sellPrice = sellPrice;
            deal.observedPrice = playerKnownPrice(target, e);
            deal.units = units;
            deal.profit = profit;
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

    const double sellTariff = licenceTariffRate;
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

    for (size_t k = 0; k < near.size(); ++k) {
        const int target = near[k].second;
        // Цель, до которой маршрут не строится, — не совет, а ловушка (§12.5):
        // корабль с такими баками туда просто не полетит.
        if (!agentRouteCost(playerAgent, target).feasible) continue;
        const double years = agentRouteTravelTime(playerAgent, target);
        if (years <= 0.0) continue;

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
            for (int step = 1; step <= 10; ++step) {
                const double u = leg.maxUnits * double(step) / 10.0;
                if (u <= 0.01) continue;
                const double avgBuy = home.executionPrice(e, u, false);
                const double cost = u * avgBuy;
                const double rev = u * sellPrice * marketExecutionFactor(u, targetDepth, true) * (1.0 - sellTariff);
                const double perYear = (rev - cost) / years;
                if (rev - cost <= 0.0 || perYear <= best.perYear) continue;
                best.element = e;
                best.targetStar = target;
                best.units = u;
                best.buyPrice = avgBuy;
                best.sellPrice = sellPrice;
                best.profit = rev - cost;
                best.years = years;
                best.perYear = perYear;
                best.distanceLy = near[k].first;
                best.valid = true;
            }
        }
    }

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

double Game::licencePrice() const {
    return licenceQuotaBase * LICENCE_PRICE_K * double(std::max(1, licenceCount));
}

double Game::licenceSettleCost() const {
    const double remaining = std::max(0.0, licenceQuotaTarget() - licenceQuotaPaid);
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
    return std::max(0, licenceCount - playerShipCount());
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
    licenceCount += 1;
    pushNews("Trading licence #" + std::to_string(licenceCount) + " acquired. Quota is now " +
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
    if (licenceRevoked) {
        lastEvent = "licence revoked: buy it back first (F2)";
        return false;
    }
    const double remaining = std::max(0.0, licenceQuotaTarget() - licenceQuotaPaid);
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
    licenceQuotaPaid += remaining;
    pushNews("Quota settled in cash for " + std::to_string(int(std::ceil(cost))) + " Cr.", 4);
    lastEvent = "quota settled";
    return true;
}

// --- Акции держав (§33) -----------------------------------------------------

void Game::resizeShareBooks() {
    const size_t n = factions.size();
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
    factionBook[size_t(factionIndex)] = book;
    factionIncome[size_t(factionIndex)] = income;
    factionBookAt[size_t(factionIndex)] = time;
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
    if (playerTradingBlocked()) return 0.0;
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
    player.money += amount * price;
    playerShares[size_t(factionIndex)] -= amount;
    shareCostBasis[size_t(factionIndex)] = std::max(0.0, shareCostBasis[size_t(factionIndex)] - basisShare);
    lastEvent = "sold shares in " + factions[size_t(factionIndex)].name;
    return amount;
}

void Game::payShareDividends(double years) {
    if (!(years > 0.0) || playerFaction < 0) return;
    resizeShareBooks();
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
    const double licenceFee = licenceRevoked ? 0.0 : gross * licenceTariffRate;
    player.money += gross - fee - licenceFee;
    const int owner = star.ownerFaction;
    if (validFaction(*this, owner)) factions[size_t(owner)].treasury += fee;
    if (licenceFee > 0.0) {
        licenceQuotaPaid += licenceFee;
        if (validFaction(*this, clearingFaction)) factions[size_t(clearingFaction)].treasury += licenceFee;
    }
    player.trades += 1;
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
    for (size_t i = 0; i < contracts.size(); ++i) {
        Contract& c = contracts[i];
        if (c.completed || c.failed) continue;
        if (c.type != ContractType::Bounty) continue;
        if (c.targetAgent != agentIndex) continue;
        // Ничей заказ или ВЗЯТЫЙ ИГРОКОМ. Если игрок сбил пирата раньше, чем
        // взял заказ, доска обязана это признать — иначе награда зависела бы от
        // порядка нажатий. Но заказ, который везёт конкурент, чужой: закрывать
        // его чужой работой значило бы платить NPC за то, чего он не делал.
        if (c.acceptedByAgent >= 0 && c.acceptedByAgent != playerAgent) continue;
        c.targetDown = true;
        c.progress = std::max(c.progress, 0.9);
        marked = true;
    }
    if (marked) pushNews("Bounty target destroyed - claim it at the board", 2);
    return marked;
}

bool Game::playerTradingBlocked() {
    if (!licenceRevoked) return false;
    lastEvent = "trading frozen: licence revoked (buy back for " +
                std::to_string(int(std::ceil(licenceBuyback))) + " Cr)";
    return true;
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
    const bool freshMarket = validStar(*this, starIndex) && !playerKnowsMarket(starIndex);
    observeStarForFaction(playerFaction, starIndex);
    observeMarketForFaction(playerFaction, starIndex);
    absorbLocalSignalsForFaction(playerFaction, starIndex, true);
    if (freshMarket) addResearch(3.0);
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
