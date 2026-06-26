#include "game.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>

std::mt19937 rng(42);

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

struct RouteEdge {
    int star = -1;
    double distance = 0.0;

    RouteEdge() {}
    RouteEdge(int star_, double distance_) : star(star_), distance(distance_) {}
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
const int ROUTE_NEIGHBORS = 14;
const double ROUTE_REBUILD_INTERVAL_YEARS = 1000.0;
const double MARKET_UPDATE_INTERVAL_YEARS = 1.0;
const int SIGNAL_MEMORY_PER_STAR = 24;

bool contractUsesCargo(ContractType type) {
    return type == ContractType::Delivery || type == ContractType::ColonySupply;
}

bool contractNeedsTargetAgent(ContractType type) {
    return type == ContractType::Bounty || type == ContractType::Escort;
}

enum class MaterialNeed {
    Structure,
    Electronics,
    Energy
};

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

double materialNeedTrait(MaterialNeed need, const ElementDefinition& element) {
    if (need == MaterialNeed::Structure) {
        return element.structuralTrait * 0.70 + element.metallicTrait * 0.25 + element.nuclearStability * 0.10;
    }
    if (need == MaterialNeed::Electronics) {
        return element.conductorTrait * 0.70 + element.catalystTrait * 0.22 + element.nobleStability * 0.18;
    }
    const double fuelTrait = std::max(element.fusionFuelTrait, element.fissionFuelTrait);
    return fuelTrait * 0.80 + element.activationCost * 0.05 + (1.0 - element.handlingRisk) * 0.12;
}

struct MaterialRequirement {
    int element = -1;
    double amount = 0.0;

    MaterialRequirement() {}
    MaterialRequirement(int element_, double amount_) : element(element_), amount(amount_) {}
};

int pickLocalMaterialElement(const Market& market, MaterialNeed need) {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    int best = -1;
    double bestScore = 0.0;
    const size_t count = std::min(elements.size(), market.supply.size());
    for (size_t i = 0; i < count; ++i) {
        const double trait = materialNeedTrait(need, elements[i]);
        if (trait <= 0.04) continue;
        const double availableMass = market.supply[i].amount * resourceUnitMassByIndex(int(i));
        if (availableMass <= 0.05) continue;
        const double pressure = i < market.prices.size() && elements[i].basePrice > 0.0 ?
            market.prices[i] / elements[i].basePrice : 1.0;
        const double score = trait * (0.5 + std::sqrt(availableMass)) / std::sqrt(std::max(0.2, pressure));
        if (score > bestScore) {
            bestScore = score;
            best = int(i);
        }
    }
    return best;
}

void addMaterialRequirement(std::vector<MaterialRequirement>& requirements, int elementIndex, double mass) {
    if (elementIndex < 0 || mass <= 0.0) return;
    const double amount = mass / resourceUnitMassByIndex(elementIndex);
    if (amount <= 0.0) return;
    for (MaterialRequirement& requirement : requirements) {
        if (requirement.element == elementIndex) {
            requirement.amount += amount;
            return;
        }
    }
    requirements.push_back(MaterialRequirement(elementIndex, amount));
}

std::vector<MaterialRequirement> colonyFoundingRequirements(const Game& game, int starIndex) {
    std::vector<MaterialRequirement> requirements;
    if (!validStar(game, starIndex) || starIndex >= int(game.markets.size())) return requirements;
    const ClusterStar& star = game.cluster.stars[starIndex];
    const Market& market = game.markets[starIndex];
    const double scale = 0.75 + std::max(0.0, 1.0 - star.habitability) * 0.55 + std::max(0.0, 1.0 - star.industry) * 0.10;
    addMaterialRequirement(requirements, pickLocalMaterialElement(market, MaterialNeed::Structure), 26.0 * scale);
    addMaterialRequirement(requirements, pickLocalMaterialElement(market, MaterialNeed::Electronics), 7.0 * scale);
    addMaterialRequirement(requirements, pickLocalMaterialElement(market, MaterialNeed::Energy), 10.0 * scale);
    return requirements;
}

std::vector<MaterialRequirement> shipBuildRequirements(const Game& game, int starIndex, const Colony& colony) {
    std::vector<MaterialRequirement> requirements;
    if (!validStar(game, starIndex) || starIndex >= int(game.markets.size())) return requirements;
    const Market& market = game.markets[starIndex];
    const double shipyardDiscount = colony.shipyardLevel > 0 ? 0.72 : 1.0;
    addMaterialRequirement(requirements, pickLocalMaterialElement(market, MaterialNeed::Structure), 18.0 * shipyardDiscount);
    addMaterialRequirement(requirements, pickLocalMaterialElement(market, MaterialNeed::Electronics), 5.5 * shipyardDiscount);
    addMaterialRequirement(requirements, pickLocalMaterialElement(market, MaterialNeed::Energy), 6.0 * shipyardDiscount);
    return requirements;
}

bool consumeMaterialRequirements(Game& game, int starIndex, const std::vector<MaterialRequirement>& requirements, std::string& missing) {
    if (!validStar(game, starIndex) || starIndex >= int(game.markets.size())) return false;
    Market& market = game.markets[starIndex];
    for (const MaterialRequirement& requirement : requirements) {
        if (requirement.element < 0 || requirement.element >= int(market.supply.size())) return false;
        const double available = market.supply[requirement.element].amount;
        if (available + 0.001 < requirement.amount) {
            const ElementDefinition& element = elementDefinitions()[requirement.element];
            missing = std::string("need ") + element.symbol + " " +
                std::to_string(int(std::ceil(requirement.amount - available)));
            return false;
        }
    }
    for (const MaterialRequirement& requirement : requirements) {
        market.supply[requirement.element].amount = std::max(0.0, market.supply[requirement.element].amount - requirement.amount);
    }
    return true;
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

        const double score = (star.population * 0.0005 + star.industry * 14.0 + star.habitability * 18.0) * (0.4 + spacing);
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
            const double score = star.habitability * 10.0 + star.industry * 2.5 + star.population * 0.00012 - distance * 0.18;
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

double cachedRouteDistanceFromShip(const Game& game, const Agent& agent, int targetStar) {
    if (!validStar(game, targetStar)) return -1.0;
    if (agent.ship.enRoute && validStar(game, agent.ship.targetStar)) {
        const double leg = distanceShipToStar(agent.ship, game.cluster.stars[agent.ship.targetStar]);
        const double rest = agent.ship.targetStar == targetStar ? 0.0 : cachedRouteDistance(game, agent.ship.targetStar, targetStar);
        return leg + std::max(0.0, rest);
    }
    if (!validStar(game, agent.currentStar)) return distanceShipToStar(agent.ship, game.cluster.stars[targetStar]);
    return cachedRouteDistance(game, agent.currentStar, targetStar);
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

double cachedRouteFuelNeeded(const Game& game, const Ship& ship, int originStar, int targetStar) {
    if (!validStar(game, originStar) || !validStar(game, targetStar)) return -1.0;
    if (originStar == targetStar) return 0.0;

    double fuel = 0.0;
    int current = originStar;
    const int guardLimit = std::max(1, int(game.cluster.stars.size()));
    for (int guard = 0; guard < guardLimit && current != targetStar; ++guard) {
        const int next = game.routeNextStar(current, targetStar);
        if (!validStar(game, next) || next == current) {
            return shipEstimateRouteFuel(ship, distanceBetween(game.cluster.stars[originStar], game.cluster.stars[targetStar])) * 1.08;
        }
        fuel += shipEstimateRouteFuel(ship, distanceBetween(game.cluster.stars[current], game.cluster.stars[next])) * 1.08;
        current = next;
    }
    return current == targetStar ? fuel : shipEstimateRouteFuel(ship, distanceBetween(game.cluster.stars[originStar], game.cluster.stars[targetStar])) * 1.08;
}

bool shipCanFlyDirect(const Ship& ship, double distance) {
    return distance >= 0.0 && ship.fuel + 0.001 >= shipEstimateRouteFuel(ship, distance) * 1.08;
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

double plannedRouteFuelNeeded(const Game& game, const Ship& ship, int originStar, int targetStar) {
    if (!validStar(game, originStar) || !validStar(game, targetStar)) return -1.0;
    const double direct = distanceBetween(game.cluster.stars[originStar], game.cluster.stars[targetStar]);
    if (shipCanFlyDirect(ship, direct)) return shipEstimateRouteFuel(ship, direct) * 1.08;
    return cachedRouteFuelNeeded(game, ship, originStar, targetStar);
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
        routeShip.cargo.emplace_back(elementDefinitions()[contract.resource].symbol, contract.amount);
    }
    return routeShip;
}

Contract* activeContractForAgent(Game& game, int agentIndex) {
    for (Contract& contract : game.contracts) {
        if (activeContract(contract) && contract.acceptedByAgent == agentIndex) return &contract;
    }
    return nullptr;
}

const Contract* activeContractForAgent(const Game& game, int agentIndex) {
    for (const Contract& contract : game.contracts) {
        if (activeContract(contract) && contract.acceptedByAgent == agentIndex) return &contract;
    }
    return nullptr;
}

bool payContractReward(Game& game, Contract& contract, Agent& agent, bool emitSignals) {
    if (!activeContract(contract)) return false;
    const double lateFactor = game.time > contract.deadline ? 0.45 : 1.0;
    const double payout = contract.reward * lateFactor;
    agent.money += payout;
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

int pickSurplusResource(const Game& game, int originStar) {
    if (!validStar(game, originStar) || originStar >= int(game.markets.size())) return -1;

    const Market& market = game.markets[originStar];
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    int best = -1;
    double bestScore = -std::numeric_limits<double>::max();
    for (size_t i = 0; i < market.prices.size() && i < elements.size(); ++i) {
        if (market.supply[i].amount <= 8.0 || elements[i].basePrice <= 0.0) continue;
        const double pressure = market.prices[i] / elements[i].basePrice;
        const double unitMass = std::max(0.001, resourceUnitMassByIndex(int(i)));
        const double score = (1.18 - pressure) * std::sqrt(market.supply[i].amount) / unitMass;
        if (score > bestScore) {
            bestScore = score;
            best = int(i);
        }
    }
    return best;
}

int pickDeliveryTarget(const Game& game, int originStar, int resourceIndex) {
    if (!validStar(game, originStar) || resourceIndex < 0 || resourceIndex >= int(elementCount())) return -1;

    const Market& origin = game.markets[originStar];
    int best = -1;
    double bestScore = 0.0;
    const int samples = std::min(72, std::max(12, int(game.cluster.stars.size())));
    for (int i = 0; i < samples; ++i) {
        const int target = randomer(rng, int(game.cluster.stars.size()) - 1);
        if (target == originStar || target < 0 || target >= int(game.markets.size())) continue;

        const Market& market = game.markets[target];
        const double base = elementDefinitions()[resourceIndex].basePrice;
        const double pressure = base > 0.0 ? market.prices[resourceIndex] / base : 1.0;
        const double spread = market.prices[resourceIndex] - origin.prices[resourceIndex];
        if (pressure < 1.08 || spread <= 0.0) continue;

        const double distance = cachedRouteDistance(game, originStar, target);
        const double score = (spread * 0.75 + market.demand[resourceIndex].amount * 0.012) * pressure / (std::sqrt(distance) + 1.0);
        if (score > bestScore) {
            bestScore = score;
            best = target;
        }
    }
    return best;
}

bool tryCreateDeliveryContract(Game& game, int originStar) {
    if (!validStar(game, originStar) || originStar >= int(game.markets.size())) return false;
    if (activeContractsAtOrigin(game, originStar) >= 4) return false;

    const int resourceIndex = pickSurplusResource(game, originStar);
    const int targetStar = pickDeliveryTarget(game, originStar, resourceIndex);
    if (!validStar(game, targetStar)) return false;

    Market& origin = game.markets[originStar];
    const Market& target = game.markets[targetStar];
    const double unitMass = std::max(0.001, resourceUnitMassByIndex(resourceIndex));
    const double cargoMass = 18.0 + double(randomer(rng, 42));
    const double amount = std::min(origin.supply[resourceIndex].amount * 0.18, std::min(160.0, cargoMass / unitMass));
    if (amount <= 0.25) return false;

    const double distance = cachedRouteDistance(game, originStar, targetStar);
    const double spread = std::max(0.0, target.prices[resourceIndex] - origin.prices[resourceIndex]);
    const double scarcityPay = amount * (spread * 0.055 + target.prices[resourceIndex] * 0.012);
    const double distancePay = distance * (3.5 + 0.012 * cargoMass);

    Contract contract;
    contract.id = game.nextContractId++;
    contract.type = ContractType::Delivery;
    contract.originStar = originStar;
    contract.targetStar = targetStar;
    contract.resource = resourceIndex;
    contract.amount = amount;
    contract.reward = std::max(80.0, scarcityPay + distancePay);
    contract.deposit = 0.0;
    contract.postedTime = game.time;
    contract.deadline = game.time + std::max(4.0, distance / 0.11 + 3.0);
    contract.risk = std::min(1.0, distance / 95.0);
    contract.issuerFaction = game.cluster.stars[targetStar].ownerFaction >= 0 ?
        game.cluster.stars[targetStar].ownerFaction : game.cluster.stars[originStar].ownerFaction;
    contract.risk = std::min(1.0, contract.risk + game.factionRouteThreatRisk(contract.issuerFaction, originStar, targetStar) * 0.12);
    game.contracts.push_back(contract);
    publishContractPosting(game, game.contracts.back());
    return true;
}

bool tryCreateCourierContract(Game& game, int originStar) {
    if (!validStar(game, originStar)) return false;
    if (activeContractsAtOrigin(game, originStar) >= 5) return false;

    int best = -1;
    double bestScore = -std::numeric_limits<double>::max();
    const int samples = std::min(64, std::max(12, int(game.cluster.stars.size())));
    for (int i = 0; i < samples; ++i) {
        const int target = randomer(rng, int(game.cluster.stars.size()) - 1);
        if (target == originStar || !validStar(game, target)) continue;
        const double distance = cachedRouteDistance(game, originStar, target);
        const double ownerValue = game.cluster.stars[target].ownerFaction >= 0 ? 2.0 : 0.0;
        const double score = ownerValue + game.cluster.stars[target].industry * 0.8 + game.cluster.stars[target].population * 0.00005 - distance * 0.025;
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
    contract.originStar = originStar;
    contract.targetStar = best;
    contract.reward = 65.0 + distance * 5.6;
    contract.deposit = 0.0;
    contract.postedTime = game.time;
    contract.deadline = game.time + std::max(2.5, distance / 0.16 + 2.0);
    contract.risk = std::min(1.0, distance / 120.0);
    contract.issuerFaction = game.cluster.stars[originStar].ownerFaction;
    contract.risk = std::min(1.0, contract.risk + game.factionRouteThreatRisk(contract.issuerFaction, originStar, best) * 0.10);
    game.contracts.push_back(contract);
    publishContractPosting(game, game.contracts.back());
    return true;
}

bool tryCreateScoutContract(Game& game, int originStar) {
    if (!validStar(game, originStar)) return false;
    if (activeContractsAtOrigin(game, originStar) >= 5) return false;

    const int issuer = game.cluster.stars[originStar].ownerFaction;
    if (!validFaction(game, issuer)) return false;

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
        const double score = unknownValue + star.industry * 1.2 + star.habitability * 3.0 - distance * 0.12;
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
    contract.reward = 90.0 + bestScore * 2.2 + distance * 3.1;
    contract.deposit = 0.0;
    contract.postedTime = game.time;
    contract.deadline = game.time + std::max(4.0, distance / 0.22 + 4.0);
    contract.risk = std::min(1.0, 0.16 + distance / 135.0);
    contract.risk = std::min(1.0, contract.risk + game.factionRouteThreatRisk(issuer, originStar, best) * 0.10);
    game.contracts.push_back(contract);
    publishContractPosting(game, game.contracts.back());
    return true;
}

bool tryCreateColonySupplyContract(Game& game, int originStar) {
    if (!validStar(game, originStar) || originStar >= int(game.markets.size())) return false;
    if (activeContractsAtOrigin(game, originStar) >= 5) return false;

    const int resourceIndex = pickSurplusResource(game, originStar);
    if (resourceIndex < 0) return false;

    int best = -1;
    double bestScore = 0.0;
    const int samples = std::min(96, std::max(18, int(game.cluster.stars.size())));
    for (int i = 0; i < samples; ++i) {
        const int target = randomer(rng, int(game.cluster.stars.size()) - 1);
        if (target == originStar || !validStar(game, target) || target >= int(game.markets.size())) continue;
        if (colonyIndexAt(game, target) < 0) continue;

        const Market& targetMarket = game.markets[target];
        const double base = elementDefinitions()[resourceIndex].basePrice;
        const double pressure = base > 0.0 ? targetMarket.prices[resourceIndex] / base : 1.0;
        if (pressure < 1.16) continue;

        const double distance = cachedRouteDistance(game, originStar, target);
        const double score = pressure * 18.0 + game.cluster.stars[target].population * 0.00004 + game.cluster.stars[target].industry * 1.4 - distance * 0.11;
        if (score > bestScore) {
            bestScore = score;
            best = target;
        }
    }
    if (!validStar(game, best)) return false;

    Market& origin = game.markets[originStar];
    const Market& target = game.markets[best];
    const double unitMass = std::max(0.001, resourceUnitMassByIndex(resourceIndex));
    const double cargoMass = 22.0 + double(randomer(rng, 58));
    const double amount = std::min(origin.supply[resourceIndex].amount * 0.16, std::min(180.0, cargoMass / unitMass));
    if (amount <= 0.25) return false;

    const double distance = cachedRouteDistance(game, originStar, best);
    const double scarcity = std::max(0.0, target.prices[resourceIndex] / elementDefinitions()[resourceIndex].basePrice - 1.0);

    Contract contract;
    contract.id = game.nextContractId++;
    contract.type = ContractType::ColonySupply;
    contract.issuerFaction = game.cluster.stars[best].ownerFaction;
    contract.originStar = originStar;
    contract.targetStar = best;
    contract.resource = resourceIndex;
    contract.amount = amount;
    contract.reward = std::max(100.0, amount * target.prices[resourceIndex] * 0.035 + scarcity * 120.0 + distance * 4.2);
    contract.deposit = 0.0;
    contract.postedTime = game.time;
    contract.deadline = game.time + std::max(4.0, distance / 0.12 + 3.0);
    contract.risk = std::min(1.0, 0.12 + distance / 110.0);
    contract.risk = std::min(1.0, contract.risk + game.factionRouteThreatRisk(contract.issuerFaction, originStar, best) * 0.12);
    game.contracts.push_back(contract);
    publishContractPosting(game, game.contracts.back());
    return true;
}

bool tryCreateBountyContract(Game& game, int originStar) {
    if (!validStar(game, originStar)) return false;
    if (activeContractsAtOrigin(game, originStar) >= 5) return false;
    const int issuer = game.cluster.stars[originStar].ownerFaction;
    if (!validFaction(game, issuer)) return false;

    const ThreatCandidate report = bestThreatReportAt(game, issuer, originStar, true);
    if (!report.valid) return false;

    const double distance = cachedRouteDistance(game, originStar, report.starIndex);
    Contract contract;
    contract.id = game.nextContractId++;
    contract.type = ContractType::Bounty;
    contract.issuerFaction = issuer;
    contract.originStar = originStar;
    contract.targetStar = report.starIndex;
    contract.targetAgent = report.sourceAgent;
    contract.reward = 180.0 + report.threatValue * 64.0 + report.cargoValue * 0.018 + distance * 4.0;
    contract.deposit = 0.0;
    contract.postedTime = game.time;
    contract.deadline = game.time + std::max(4.0, distance / 0.22 + 6.0);
    contract.risk = std::min(1.0, 0.28 + report.threatValue / 24.0 + std::min(0.25, (game.time - report.observedAt) * 0.018));
    game.contracts.push_back(contract);
    publishContractPosting(game, game.contracts.back());
    return true;
}

bool tryCreateRaidContract(Game& game, int originStar) {
    if (!validStar(game, originStar)) return false;
    if (activeContractsAtOrigin(game, originStar) >= 5) return false;
    const int issuer = game.cluster.stars[originStar].ownerFaction;
    if (!validFaction(game, issuer)) return false;

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
            star.population * 0.00007 +
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
    contract.reward = 150.0 + bestScore * 12.0 + target.industry * 42.0 + hostility * 180.0 + distance * 4.4;
    contract.deposit = 0.0;
    contract.postedTime = game.time;
    contract.deadline = game.time + std::max(4.0, distance / 0.20 + 5.0);
    contract.risk = std::min(1.0, 0.24 + target.defense * 0.018 + distance / 125.0 + hostility * 0.20);
    game.contracts.push_back(contract);
    publishContractPosting(game, game.contracts.back());
    return true;
}

bool tryCreateEscortContract(Game& game, int originStar) {
    if (!validStar(game, originStar)) return false;
    if (activeContractsAtOrigin(game, originStar) >= 5) return false;
    const int issuer = game.cluster.stars[originStar].ownerFaction;
    if (!validFaction(game, issuer)) return false;
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
        const double score = game.cluster.stars[target].industry * 1.3 + game.cluster.stars[target].population * 0.00006 - distance * 0.04;
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
    contract.reward = 120.0 + bestNeed * 12.0 + threatRisk * 80.0 + distance * 3.8;
    contract.deposit = 0.0;
    contract.postedTime = game.time;
    contract.deadline = game.time + std::max(4.0, distance / 0.16 + 4.0);
    contract.risk = std::min(1.0, 0.18 + distance / 140.0 + bestNeed * 0.01 + threatRisk * 0.12);
    game.contracts.push_back(contract);
    publishContractPosting(game, game.contracts.back());
    return true;
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
        const double fuelNeeded = std::max(0.0, plannedRouteFuelNeeded(game, routeShip, contract.originStar, contract.targetStar) - routeShip.fuel);
        double fuelCost = 0.0;
        if (routeShip.fuelElement >= 0 && routeShip.fuelElement < int(game.markets[contract.originStar].prices.size())) {
            fuelCost = fuelNeeded * game.markets[contract.originStar].prices[routeShip.fuelElement];
        }
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
    for (size_t element = 0; element < source.prices.size(); ++element) {
        const double buyPrice = source.prices[element];
        const double available = source.supply[element].amount;
        if (buyPrice <= 0.0 || available <= 0.01) continue;

        const double cargoSpace = std::max(0.0, agent.ship.cargoCapacity - shipCargoMass(agent.ship));
        const double massLimitedAmount = cargoSpace / resourceUnitMassByIndex(int(element));
        const double amount = std::min(massLimitedAmount, std::min(available, agent.money / buyPrice));
        if (amount <= 0.01) continue;

        const int destinationSamples = sampledStarCount(game, 96, 96);
        for (int sample = 0; sample < destinationSamples; ++sample) {
            const int dest = sampledStarAt(game, sample, destinationSamples);
            if (dest == current || dest < 0 || dest >= int(game.markets.size())) continue;
            if (!game.factionKnowsOwnerAt(agent.ship.ownerFaction, current, dest)) continue;
            if (!game.factionKnowsMarketAt(agent.ship.ownerFaction, current, dest)) continue;

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
            double fuelCost = 0.0;
            if (routeShip.fuelElement >= 0 && routeShip.fuelElement < int(source.prices.size())) {
                const double fuelNeeded = std::max(0.0, plannedRouteFuelNeeded(game, routeShip, current, dest) - routeShip.fuel);
                fuelCost = fuelNeeded * source.prices[routeShip.fuelElement] * (1.0 + tariffFor(game, current, agent.ship.ownerFaction, 0.014));
            }
            const double cargoCost = amount * buyPrice * (1.0 + tariffFor(game, current, agent.ship.ownerFaction, 0.014));
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

    if (stoppingDistance + speed * dt * 0.5 >= dist && speed > 0.0) {
        const double brake = shipConsumeFuelForDeltaV(ship, std::min(deltaV, speed));
        ship.vx -= ship.vx / speed * brake;
        ship.vy -= ship.vy / speed * brake;
        ship.vz -= ship.vz / speed * brake;
    } else if (accel > 0.0) {
        const double thrust = shipConsumeFuelForDeltaV(ship, deltaV);
        ship.vx += dirX * thrust;
        ship.vy += dirY * thrust;
        ship.vz += dirZ * thrust;

        const double newSpeed = std::sqrt(ship.vx * ship.vx + ship.vy * ship.vy + ship.vz * ship.vz);
        if (newSpeed > ship.speed) {
            const double k = ship.speed / newSpeed;
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

bool buyFuel(Game& game, Agent& agent, int starIndex, double targetFuel) {
    if (!validStar(game, starIndex)) return false;
    if (agent.ship.fuelElement < 0 || agent.ship.fuelElement >= int(elementCount())) return false;
    if (agent.ship.fuel >= targetFuel || agent.ship.fuel >= agent.ship.fuelCapacity) return false;

    Market& market = game.markets[starIndex];
    const int fuelIndex = agent.ship.fuelElement;
    if (fuelIndex < 0 || fuelIndex >= int(market.supply.size()) || fuelIndex >= int(market.prices.size())) return false;

    const double tariff = tariffFor(game, starIndex, agent.ship.ownerFaction, 0.014);
    const double unitCost = market.prices[fuelIndex] * (1.0 + tariff);
    if (unitCost <= 0.0) return false;

    const double wanted = std::max(0.0, std::min(agent.ship.fuelCapacity, targetFuel) - agent.ship.fuel);
    const double amount = std::min(wanted, std::min(market.supply[fuelIndex].amount, agent.money / unitCost));
    if (amount <= 0.01) return false;

    const double baseCost = amount * market.prices[fuelIndex];
    const double fee = baseCost * tariff;
    const int owner = game.cluster.stars[starIndex].ownerFaction;
    market.supply[fuelIndex].amount -= amount;
    agent.money -= baseCost + fee;
    if (validFaction(game, owner)) {
        game.factions[owner].treasury += fee;
        if (fee > 0.01) game.queueSettlementSignal(owner, starIndex, fee);
    }
    agent.ship.fuel += amount;
    agent.lastAction = "refueled";
    return true;
}

bool sellCargo(Game& game, Agent& agent, int starIndex, double requestedAmount = std::numeric_limits<double>::max(), const std::string& elementSymbol = "") {
    if (!validStar(game, starIndex) || agent.ship.cargo.empty() || requestedAmount <= 0.0) return false;

    int cargoIndex = -1;
    if (!elementSymbol.empty()) {
        for (size_t i = 0; i < agent.ship.cargo.size(); ++i) {
            if (agent.ship.cargo[i].element == elementSymbol) { cargoIndex = int(i); break; }
        }
    } else {
        cargoIndex = 0;
    }
    if (cargoIndex < 0 || agent.ship.cargo[cargoIndex].amount <= 0.0) return false;

    Market& market = game.markets[starIndex];
    const int resourceIndex = elementIndex(agent.ship.cargo[cargoIndex].element);
    if (resourceIndex < 0 || resourceIndex >= int(market.prices.size())) return false;

    const double cargoAmount = agent.ship.cargo[cargoIndex].amount;
    const double amount = std::min(cargoAmount, requestedAmount);
    if (amount <= 0.01) return false;
    const double gross = amount * market.prices[resourceIndex];
    const double tariff = tariffFor(game, starIndex, agent.ship.ownerFaction, 0.026);
    const double fee = gross * tariff;
    const int owner = game.cluster.stars[starIndex].ownerFaction;
    const double costShare = agent.cargoCost * (amount / std::max(0.001, cargoAmount));

    market.supply[resourceIndex].amount += amount;
    market.demand[resourceIndex].amount = std::max(0.0, market.demand[resourceIndex].amount - amount);
    agent.money += gross - fee;
    if (validFaction(game, owner)) {
        game.factions[owner].treasury += fee;
        if (fee > 0.01) game.queueSettlementSignal(owner, starIndex, fee);
    }
    agent.lastProfit = gross - fee - costShare;
    agent.cargoCost = std::max(0.0, agent.cargoCost - costShare);
    agent.trades += 1;
    agent.lastAction = "sold " + agent.ship.cargo[cargoIndex].element;
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

    const double unitCost = market.prices[plan.elementIndex];
    const double amount = std::min(plan.amount, std::min(market.supply[plan.elementIndex].amount, agent.money / unitCost));
    if (amount <= 0.01) return;

    const double baseCost = amount * unitCost;
    const double tariff = tariffFor(game, starIndex, agent.ship.ownerFaction, 0.014);
    const double fee = baseCost * tariff;
    const int owner = game.cluster.stars[starIndex].ownerFaction;

    market.supply[plan.elementIndex].amount -= amount;
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
    agent.lastAction = "bought " + std::string(element.symbol);
}

bool startJourney(Game& game, Agent& agent, int destStar) {
    if (destStar < 0 || destStar == agent.currentStar) return false;
    if (!validStar(game, agent.currentStar) || !validStar(game, destStar)) return false;
    const double directDistance = distanceBetween(game.cluster.stars[agent.currentStar], game.cluster.stars[destStar]);
    const double directFuel = shipEstimateRouteFuel(agent.ship, directDistance) * 1.08;
    if (agent.ship.fuel < directFuel && !agent.playerControlled) {
        buyFuel(game, agent, agent.currentStar, std::min(agent.ship.fuelCapacity, directFuel * 1.18));
    }

    const bool direct = agent.ship.fuel >= directFuel;
    const int legStar = direct ? destStar : game.routeNextStar(agent.currentStar, destStar);
    if (!validStar(game, legStar) || legStar == agent.currentStar) return false;
    const double distance = distanceBetween(game.cluster.stars[agent.currentStar], game.cluster.stars[legStar]);
    const double neededFuel = shipEstimateRouteFuel(agent.ship, distance) * 1.08;
    if (agent.ship.fuel < neededFuel && !agent.playerControlled) {
        buyFuel(game, agent, agent.currentStar, std::min(agent.ship.fuelCapacity, neededFuel * 1.18));
    }
    if (agent.ship.fuel < neededFuel) {
        agent.lastAction = "need fuel";
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
        const double score = star.habitability * 15.0 + star.industry * 3.0 + star.population * 0.00014 - distance * 0.32 - uncertainty - stalePenalty;
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
        game.foundedColonies += 1;
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
        const double score = enemyValue + star.industry * 2.4 + star.population * 0.00008 - distance * 0.28 - star.defense * 0.18 - stalePenalty;
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
        const double score = star.population * 0.00012 + star.industry * 2.0 + hostility * 10.0 - distance * 0.09;
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

Game::Game() : time(0.0) {}

bool Game::saveToFile(const std::string& path) {
    std::ofstream out(path.c_str());
    if (!out) {
        lastEvent = "save failed";
        return false;
    }
    out << std::setprecision(17);
    out << "STARCLUSTER_SAVE 5 " << cluster.stars.size() << '\n';
    out << "RNG " << rng << '\n';
    out << "TIME " << time << ' ' << contractUpdateTimer << ' ' << factionUpdateTimer << ' '
        << nextContractId << ' ' << playerAgent << ' ' << playerFaction << ' '
        << foundedColonies << ' ' << capturedSystems << ' ' << nextSignalEventId << ' ' << saveToken(lastEvent) << '\n';

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
            << ship.driveEfficiency << ' ' << ship.fuelElement << ' ' << ship.fuel << ' '
            << ship.fuelCapacity << ' ' << ship.cargoCapacity << ' ' << ship.ownerFaction << ' '
            << ship.targetStar << ' ' << int(ship.enRoute) << '\n';
        out << "CARGO ";
        writeResourceList(out, ship.cargo);
        out << '\n';
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
    if (!(in >> tag >> version >> starCount) || tag != "STARCLUSTER_SAVE" || version != 5) {
        lastEvent = "load failed: version";
        return false;
    }

    Game loaded;
    loaded.cluster.generate(starCount);
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
            loaded.foundedColonies >> loaded.capturedSystems >> loaded.nextSignalEventId >> eventToken)) {
        lastEvent = "load failed: time";
        return false;
    }
    loaded.lastEvent = loadToken(eventToken);
    if (loaded.nextSignalEventId == 0) loaded.nextSignalEventId = 1;

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
        Market market;
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
                agent.ship.driveEfficiency >> agent.ship.fuelElement >> agent.ship.fuel >>
                agent.ship.fuelCapacity >> agent.ship.cargoCapacity >> agent.ship.ownerFaction >>
                agent.ship.targetStar >> enRoute)) {
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

    const size_t countSize = size_t(count);
    routeNextHop.assign(countSize * countSize, ROUTE_NO_HOP);
    for (int source = 0; source < count; ++source) {
        const size_t row = size_t(source) * size_t(count);
        routeNextHop[row + size_t(source)] = static_cast<unsigned short>(source);
        for (int target = 0; target < count; ++target) {
            if (target == source) continue;
            const double direct2 = distanceSquaredStarToStar(cluster.stars[source], cluster.stars[target]);
            double best2 = direct2;
            int best = -1;
            const std::vector<RouteEdge>& edges = graph[source];
            for (size_t e = 0; e < edges.size(); ++e) {
                const int neighbor = edges[e].star;
                if (neighbor < 0 || neighbor >= count) continue;
                const double neighbor2 = distanceSquaredStarToStar(cluster.stars[neighbor], cluster.stars[target]);
                if (neighbor2 < best2) {
                    best2 = neighbor2;
                    best = neighbor;
                }
            }
            routeNextHop[row + size_t(target)] = best >= 0 ? static_cast<unsigned short>(best) : static_cast<unsigned short>(target);
        }
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

void Game::init(size_t num_stars) {
    time = 0.0;
    cluster.generate(num_stars);
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
    foundedColonies = 0;
    capturedSystems = 0;
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

    const int playerStart = !factions.empty() && !factions[0].controlledStars.empty() ? factions[0].homeStar : 0;
    const ClusterStar& playerHome = cluster.stars[playerStart];
    Ship playerShip("Player", playerHome.x, playerHome.y, playerHome.z, 0.28, playerFaction);
    playerShip.cargoCapacity = 110.0;
    playerShip.acceleration = 0.22;
    shipAutofit(playerShip);
    Agent player("player", playerShip);
    player.playerControlled = true;
    player.currentStar = playerStart;
    player.homeStar = playerStart;
    player.destStar = playerStart;
    player.money = 3600.0;
    player.lastAction = "ready";
    playerAgent = int(agents.size());
    agents.push_back(player);
    registerFactionAgent(*this, playerAgent);

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
    seedPlayerKnowledge(playerStart, 10.0);
    observeStar(playerStart);
    for (int i = 0; i < 4; ++i) {
        tryCreateDeliveryContract(*this, playerStart);
    }

    const int traderCount = std::min<int>(64, std::max<int>(12, int(num_stars / 24)));
    for (int i = 0; i < traderCount; ++i) {
        const int owner = npcFactionCount > 0 ? i % npcFactionCount : -1;
        int start = (i * 37) % int(num_stars);
        if (validFaction(*this, owner) && !factions[owner].controlledStars.empty()) {
            start = factions[owner].controlledStars[i % int(factions[owner].controlledStars.size())];
        }
        const ClusterStar& star = cluster.stars[start];
        const double maxSpeed = 0.1 + 0.4 * double((i * 17) % 100) / 99.0;
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
        for (int i = 0; i < 3; ++i) {
            const int start = factions[f].controlledStars[i % int(factions[f].controlledStars.size())];
            const ClusterStar& star = cluster.stars[start];
            Ship ship(factions[f].name + "_Patrol_" + std::to_string(i + 1), star.x, star.y, star.z, 0.18 + 0.05 * i, f);
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

        for (int i = 0; i < 2; ++i) {
            const int start = factions[f].controlledStars[(i + 1) % int(factions[f].controlledStars.size())];
            const ClusterStar& star = cluster.stars[start];
            Ship ship(factions[f].name + "_Charter_" + std::to_string(i + 1), star.x, star.y, star.z, 0.12 + 0.025 * i, f);
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

        const int scoutStart = factions[f].controlledStars[0];
        const ClusterStar& scoutStar = cluster.stars[scoutStart];
        Ship scoutShip(factions[f].name + "_Scout", scoutStar.x, scoutStar.y, scoutStar.z, 0.32, f);
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

        const int pirateStart = factions[f].controlledStars[(f + 2) % int(factions[f].controlledStars.size())];
        const ClusterStar& pirateStar = cluster.stars[pirateStart];
        Ship pirateShip(factions[f].name + "_Raider", pirateStar.x, pirateStar.y, pirateStar.z, 0.26, f);
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

    const int adventurerCount = std::min<int>(28, std::max<int>(8, int(num_stars / 80)));
    for (int i = 0; i < adventurerCount; ++i) {
        const int owner = npcFactionCount > 0 ? (i * 5 + 1) % npcFactionCount : -1;
        const int start = validFaction(*this, owner) && !factions[owner].controlledStars.empty() ?
            factions[owner].controlledStars[(i + 2) % int(factions[owner].controlledStars.size())] :
            randomer(rng, int(num_stars) - 1);
        const ClusterStar& star = cluster.stars[start];
        Ship ship("Adventurer_" + std::to_string(i + 1), star.x, star.y, star.z, 0.18 + 0.16 * double((i * 19) % 100) / 99.0, owner);
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
    for (int i = 0; i < 24; ++i) {
        tryCreateDeliveryContract(*this, randomer(rng, int(num_stars) - 1));
    }
}

void Game::update(double dt) {
    time += dt;
    if (routeCacheBuiltAt < 0.0 || time - routeCacheBuiltAt >= ROUTE_REBUILD_INTERVAL_YEARS) {
        rebuildRouteCache();
    }
    updateMarkets(dt);
    updateColonies(dt);
    updateFactions(dt);
    updateContracts(dt);
    updateAgents(dt);
    processSignals();
    if (playerAgent >= 0 && playerAgent < int(agents.size()) && !agents[playerAgent].ship.enRoute) {
        observeStar(agents[playerAgent].currentStar);
        agentCompleteContracts(playerAgent);
    }
}

void Game::updateMarkets(double dt) {
    const int count = int(markets.size());
    if (count <= 0) return;
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
        markets[size_t(starIndex)].update(elapsed);
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
        const double shortage = std::max(0.0, market.pricePressure() - 1.0);
        const double supplySatisfaction = std::max(0.2, 1.0 - shortage * 0.18);
        const double damageFactor = std::max(0.1, 1.0 - colony.damage);
        const double growthRate = (0.0012 + star.habitability * 0.0035 + colony.infrastructure * 0.00035) * supplySatisfaction * damageFactor;
        const double gained = std::min(1200.0, double(colony.population) * growthRate * dt);
        const size_t gainedPopulation = size_t(std::max(0.0, gained));
        colony.population += gainedPopulation;
        star.population += double(gainedPopulation);
        star.industry += colony.infrastructure * (0.00045 + colony.automation * 0.00025) * supplySatisfaction * dt;
        colony.energyCapacity = std::max(colony.energyCapacity, colony.infrastructure * (0.6 + colony.automation * 0.4));
        colony.marketAccess = std::max(0.15, supplySatisfaction);
        colony.defense += (0.002 + colony.infrastructure * 0.0011 + colony.energyCapacity * 0.0003) * damageFactor * dt;
        star.defense = std::max(star.defense, colony.defense);
        colony.infrastructure += star.industry * 0.000035 * supplySatisfaction * dt;

        const double income = (star.industry * 0.55 + double(colony.population) * 0.00002) * colony.marketAccess * dt;
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

        Faction& faction = factions[colony.ownerFaction];
        faction.treasury += income * 0.42;
        faction.strength += 0.00015 * dt;
    }
}

void Game::updateContracts(double dt) {
    for (Contract& contract : contracts) {
        if (activeContract(contract) && contract.acceptedByAgent < 0 && contract.deadline < time) {
            contract.failed = true;
        }
    }

    contractUpdateTimer -= dt;
    if (contractUpdateTimer > 0.0 || cluster.stars.empty()) return;
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
                    agent.lastAction = "stopped in deep space";
                } else {
                    const double accel = shipCurrentAcceleration(agent.ship);
                    const double deltaV = accel * dt;
                    const double brake = shipConsumeFuelForDeltaV(agent.ship, std::min(deltaV, speed));
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

        if (agent.playerControlled || int(i) == playerAgent) continue;

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
            
            // Downgrade to Escape Pod
            const auto& classes = shipClasses();
            const ShipClass& pod = classes[0];
            victim.ship.name = "Escape Pod";
            victim.ship.dryMass = pod.dryMass;
            victim.ship.driveThrust = pod.driveThrust;
            victim.ship.driveEfficiency = pod.driveEfficiency;
            victim.ship.cargoCapacity = pod.cargoCapacity;
            victim.ship.fuelCapacity = pod.fuelCapacity;
            victim.ship.heavyWeapons = pod.heavyWeapons;
            victim.ship.lightWeapons = pod.lightWeapons;
            victim.ship.armor = pod.armor;
            victim.ship.utility = pod.utility;
            shipAutofit(victim.ship);
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
            
            // Downgrade attacker to Escape Pod
            const auto& classes = shipClasses();
            const ShipClass& pod = classes[0];
            attacker.ship.name = "Escape Pod";
            attacker.ship.dryMass = pod.dryMass;
            attacker.ship.driveThrust = pod.driveThrust;
            attacker.ship.driveEfficiency = pod.driveEfficiency;
            attacker.ship.cargoCapacity = pod.cargoCapacity;
            attacker.ship.fuelCapacity = pod.fuelCapacity;
            attacker.ship.heavyWeapons = pod.heavyWeapons;
            attacker.ship.lightWeapons = pod.lightWeapons;
            attacker.ship.armor = pod.armor;
            attacker.ship.utility = pod.utility;
            shipAutofit(attacker.ship);
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
    const Colony& colony = colonies[starIndex];
    if (colony.shipyardLevel <= 0 && colony.infrastructure < 1.0) return false;
    
    const auto& classes = shipClasses();
    if (classId < 0 || classId >= int(classes.size())) return false;
    const ShipClass& sc = classes[classId];
    
    if (agent.money < sc.price) return false;
    
    agent.money -= sc.price;
    agent.ship.name = sc.name;
    agent.ship.dryMass = sc.dryMass;
    agent.ship.driveThrust = sc.driveThrust;
    agent.ship.driveEfficiency = sc.driveEfficiency;
    agent.ship.cargoCapacity = sc.cargoCapacity;
    agent.ship.fuelCapacity = sc.fuelCapacity;
    agent.ship.heavyWeapons = sc.heavyWeapons;
    agent.ship.lightWeapons = sc.lightWeapons;
    agent.ship.armor = sc.armor;
    agent.ship.utility = sc.utility;
    shipAutofit(agent.ship);
    agent.lastAction = "bought " + sc.name;
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

    const double years = agentRouteTravelTime(agentIndex, starIndex);
    const double fuelNeeded = agentRouteFuelNeeded(agentIndex, starIndex);
    const double shortfall = agentRouteFuelShortfall(agentIndex, starIndex);
    const double risk = agentRouteThreatRisk(agentIndex, starIndex);
    const bool departed = startJourney(*this, agent, starIndex);
    if (!agent.playerControlled) return departed;

    if (!departed) {
        lastEvent = "route blocked: fuel " + std::to_string(int(agent.ship.fuel)) +
            "/" + std::to_string(int(std::ceil(fuelNeeded))) +
            " short " + std::to_string(int(std::ceil(shortfall)));
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
    const Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute && validStar(*this, agent.ship.targetStar)) {
        const double leg = shipEstimateRouteFuel(agent.ship, distanceShipToStar(agent.ship, cluster.stars[agent.ship.targetStar])) * 1.08;
        const double rest = agent.ship.targetStar == targetStar ? 0.0 : plannedRouteFuelNeeded(*this, agent.ship, agent.ship.targetStar, targetStar);
        return leg + std::max(0.0, rest);
    }
    if (!validStar(*this, agent.currentStar)) {
        return shipEstimateRouteFuel(agent.ship, distanceShipToStar(agent.ship, cluster.stars[targetStar])) * 1.08;
    }
    return plannedRouteFuelNeeded(*this, agent.ship, agent.currentStar, targetStar);
}

double Game::agentRouteFuelShortfall(int agentIndex, int targetStar) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    const double fuelNeeded = agentRouteFuelNeeded(agentIndex, targetStar);
    if (fuelNeeded < 0.0) return 0.0;
    return std::max(0.0, fuelNeeded - agents[agentIndex].ship.fuel);
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
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return -1.0;
    const Contract* contract = contractById(*this, contractId);
    if (!contract || !activeContract(*contract)) return -1.0;
    const double distance = agentContractRouteDistance(agentIndex, contractId);
    if (distance < 0.0) return -1.0;
    const Ship routeShip = contractRouteShip(agents[agentIndex], *contract);
    const Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute && validStar(*this, agent.ship.targetStar)) {
        const double leg = shipEstimateRouteFuel(routeShip, distanceShipToStar(agent.ship, cluster.stars[agent.ship.targetStar])) * 1.08;
        const double rest = agent.ship.targetStar == contract->targetStar ? 0.0 :
            plannedRouteFuelNeeded(*this, routeShip, agent.ship.targetStar, contract->targetStar);
        return leg + std::max(0.0, rest);
    }
    if (!validStar(*this, agent.currentStar)) return shipEstimateRouteFuel(routeShip, distance) * 1.08;
    return plannedRouteFuelNeeded(*this, routeShip, agent.currentStar, contract->targetStar);
}

double Game::agentContractRouteFuelShortfall(int agentIndex, int contractId) const {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return 0.0;
    const double fuelNeeded = agentContractRouteFuelNeeded(agentIndex, contractId);
    if (fuelNeeded < 0.0) return 0.0;
    return std::max(0.0, fuelNeeded - agents[agentIndex].ship.fuel);
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
    return cargoMass <= agent.ship.cargoCapacity - shipCargoMass(agent.ship) + 0.001;
}

bool Game::agentBuyElement(int agentIndex, int elementIndex) {
    return agentBuyElementAmount(agentIndex, elementIndex, std::numeric_limits<double>::max());
}

bool Game::agentBuyElementAmount(int agentIndex, int elementIndex, double amount) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Agent& agent = agents[agentIndex];
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
    const double targetFuel = agent.ship.fuelCapacity;
    return buyFuel(*this, agent, agent.currentStar, targetFuel);
}

bool Game::agentSellCargo(int agentIndex) {
    return agentSellCargoAmount(agentIndex, std::numeric_limits<double>::max(), -1);
}

bool Game::agentSellCargoAmount(int agentIndex, double amount, int elementIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute || !validStar(*this, agent.currentStar) || agent.ship.cargo.empty() || amount <= 0.0) return false;
    std::string elementSymbol = "";
    if (elementIndex >= 0 && elementIndex < int(elementCount())) {
        elementSymbol = elementDefinitions()[elementIndex].symbol;
    }
    return sellCargo(*this, agent, agent.currentStar, amount, elementSymbol);
}

bool Game::agentAcceptContract(int agentIndex, int contractId) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Contract* contract = contractById(*this, contractId);
    if (!contract || !activeContract(*contract) || contract->acceptedByAgent >= 0) return false;
    if (!validStar(*this, contract->originStar) || !validStar(*this, contract->targetStar)) return false;

    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute || agent.currentStar != contract->originStar) return false;

    if (contractUsesCargo(contract->type)) {
        if (contract->resource < 0 || contract->resource >= int(elementCount()) || contract->amount <= 0.0) return false;
        Market& origin = markets[contract->originStar];
        if (contract->resource >= int(origin.supply.size()) || origin.supply[contract->resource].amount < contract->amount) return false;

        const double cargoMass = contract->amount * resourceUnitMassByIndex(contract->resource);
        if (cargoMass > agent.ship.cargoCapacity - shipCargoMass(agent.ship) + 0.001) {
            agent.lastAction = "contract too heavy";
            return false;
        }

        origin.supply[contract->resource].amount -= contract->amount;
        agent.ship.cargo.emplace_back(elementDefinitions()[contract->resource].symbol, contract->amount);
    }
    agent.cargoCost = 0.0;
    contract->acceptedByAgent = agentIndex;
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
        if (contract->resource < 0 || contract->resource >= int(elementCount()) || agent.ship.cargo.empty()) return false;
        
        double totalFound = 0.0;
        const std::string& targetSymbol = elementDefinitions()[contract->resource].symbol;
        for (const Resource& res : agent.ship.cargo) {
            if (res.element == targetSymbol) totalFound += res.amount;
        }
        
        if (totalFound + 0.001 < contract->amount) return false;
        
        double remainingToTake = contract->amount;
        for (auto it = agent.ship.cargo.begin(); it != agent.ship.cargo.end() && remainingToTake > 0.001; ) {
            if (it->element == targetSymbol) {
                if (it->amount > remainingToTake) {
                    it->amount -= remainingToTake;
                    remainingToTake = 0.0;
                    ++it;
                } else {
                    remainingToTake -= it->amount;
                    it = agent.ship.cargo.erase(it);
                }
            } else {
                ++it;
            }
        }
        Market& target = markets[contract->targetStar];
        target.supply[contract->resource].amount += contract->amount;
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

bool Game::playerFoundColony() {
    if (playerAgent < 0 || playerAgent >= int(agents.size()) || !validFaction(*this, playerFaction)) return false;
    Agent& player = agents[playerAgent];
    if (player.ship.enRoute || !validStar(*this, player.currentStar)) return false;

    ClusterStar& star = cluster.stars[player.currentStar];
    if (star.ownerFaction >= 0 && star.ownerFaction != playerFaction) {
        player.lastAction = "foreign claim";
        return false;
    }

    const int existingColony = colonyIndexAt(*this, player.currentStar);
    if (existingColony < 0) {
        const double cost = 7600.0 + std::max(0.0, 1.0 - star.habitability) * 1800.0;
        if (player.money < cost) {
            player.lastAction = "need " + std::to_string(int(std::ceil(cost)));
            return false;
        }
        std::string missing;
        const std::vector<MaterialRequirement> requirements = colonyFoundingRequirements(*this, player.currentStar);
        if (!consumeMaterialRequirements(*this, player.currentStar, requirements, missing)) {
            player.lastAction = missing.empty() ? "need materials" : missing;
            lastEvent = "colony blocked: " + player.lastAction;
            return false;
        }
        setStarOwner(*this, player.currentStar, playerFaction);
        addColony(*this, player.currentStar, playerFaction, false);
        observeStar(player.currentStar);
        player.money -= cost;
        foundedColonies += 1;
        lastEvent = "player founded " + star.name;
        player.lastAction = "founded colony";
        return true;
    }

    Colony& colony = colonies[existingColony];
    if (colony.ownerFaction != playerFaction) {
        player.lastAction = "foreign colony";
        return false;
    }

    const double cost = std::min(2500.0, player.money);
    if (cost < 500.0) {
        player.lastAction = "need credits";
        return false;
    }
    player.money -= cost;
    colony.population += size_t(cost * 0.15);
    colony.infrastructure += cost * 0.00016;
    star.population += cost * 0.15;
    star.industry += cost * 0.000012;
    star.defense += cost * 0.00008;
    observeStar(player.currentStar);
    lastEvent = "player reinforced " + star.name;
    player.lastAction = "reinforced colony";
    return true;
}

bool Game::playerHireShip() {
    if (playerAgent < 0 || playerAgent >= int(agents.size()) || !validFaction(*this, playerFaction)) return false;
    Agent& player = agents[playerAgent];
    if (player.ship.enRoute || !validStar(*this, player.currentStar)) return false;

    const int colonyIndex = colonyIndexAt(*this, player.currentStar);
    if (colonyIndex < 0) {
        player.lastAction = "need colony";
        lastEvent = "hire blocked: no local colony";
        return false;
    }

    Colony& colony = colonies[colonyIndex];
    if (colony.ownerFaction != playerFaction) {
        player.lastAction = "foreign colony";
        lastEvent = "hire blocked: foreign colony";
        return false;
    }

    const bool shipyard = colonyHasShipyardCapacity(colony);
    if (!shipyard && colony.infrastructure < 1.15) {
        player.lastAction = "need shipyard";
        lastEvent = "hire blocked: colony needs shipyard";
        return false;
    }

    const double baseCost = shipyard ? 4200.0 : 6100.0;
    const double ledgerSpend = std::min(colony.localLedger, baseCost * (shipyard ? 0.42 : 0.24));
    const double cashCost = baseCost - ledgerSpend;
    if (player.money < cashCost) {
        player.lastAction = "need " + std::to_string(int(std::ceil(cashCost)));
        return false;
    }

    std::string missing;
    const std::vector<MaterialRequirement> requirements = shipBuildRequirements(*this, player.currentStar, colony);
    if (!consumeMaterialRequirements(*this, player.currentStar, requirements, missing)) {
        player.lastAction = missing.empty() ? "need materials" : missing;
        lastEvent = "ship blocked: " + player.lastAction;
        return false;
    }

    ClusterStar& star = cluster.stars[player.currentStar];
    const int fleetNumber = int(factions[playerFaction].fleetAgents.size()) + 1;
    Ship ship("Freehold_Trader_" + std::to_string(fleetNumber), star.x, star.y, star.z, shipyard ? 0.24 : 0.19, playerFaction);
    ship.cargoCapacity = shipyard ? 95.0 + colonyShipHiringCapacity(colony) * 8.0 : 72.0;
    ship.acceleration = shipyard ? 0.19 + colonyShipHiringCapacity(colony) * 0.004 : 0.15;
    shipAutofit(ship);

    Agent hired(shipyard ? "trader" : "adventurer", ship);
    hired.currentStar = player.currentStar;
    hired.homeStar = player.currentStar;
    hired.destStar = player.currentStar;
    hired.money = shipyard ? 1300.0 : 900.0;
    hired.tradeBias = 0.85;
    hired.questBias = shipyard ? 0.28 : 0.45;
    hired.scoutBias = shipyard ? 0.12 : 0.32;
    hired.riskTolerance = 0.42;
    hired.lastAction = "hired";
    agents.push_back(hired);
    registerFactionAgent(*this, int(agents.size()) - 1);

    player.money -= cashCost;
    colony.localLedger = std::max(0.0, colony.localLedger - ledgerSpend);
    colony.infrastructure += shipyard ? 0.015 : 0.006;
    factions[playerFaction].treasury += ledgerSpend;
    factions[playerFaction].estimatedTreasury = std::max(factions[playerFaction].estimatedTreasury, factions[playerFaction].treasury);
    observeStar(player.currentStar);

    player.lastAction = "hired ship";
    lastEvent = "hired " + agents.back().ship.name + " at " + star.name;
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
    observeStarForFaction(playerFaction, starIndex);
    observeMarketForFaction(playerFaction, starIndex);
    absorbLocalSignalsForFaction(playerFaction, starIndex, true);
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
