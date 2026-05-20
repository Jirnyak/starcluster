#pragma once
#include "agent.h"
#include "cluster.h"
#include "colony.h"
#include "contract.h"
#include "faction.h"
#include "market.h"
#include <random>
#include <string>
#include <vector>

const int STAR_COUNT = 10000;
const int CIV_COUNT = 100;
const int RESOURCE_TYPES = 118;

extern std::mt19937 rng;
int randomer(std::mt19937& rng, int max);

struct FactionStarKnowledge {
    bool ownerKnown = false;
    int ownerFaction = -1;
    double ownerKnownAt = -1.0;
    bool visited = false;
};

using PlayerStarKnowledge = FactionStarKnowledge;

struct FactionMarketKnowledge {
    bool known = false;
    double observedAt = -1.0;
    double averageSupplyPressure = 1.0;
    double averageDemandPressure = 1.0;
};

enum class SignalType {
    OwnerReport,
    MarketReport,
    ContractReport,
    CombatReport,
    SettlementReport,
    DiplomacyReport
};

struct SignalPacket {
    SignalType type = SignalType::OwnerReport;
    unsigned long long eventId = 0;
    double observedTime = 0.0;
    double sendTime = 0.0;
    double arrivalTime = 0.0;
    int originStar = -1;
    int destinationStar = -1;
    int hopStar = -1;
    int subjectStar = -1;
    int sourceAgent = -1;
    int targetAgent = -1;
    int sourceFaction = -1;
    int targetFaction = -1;
    int recipientFaction = -1;
    int ownerFaction = -1;
    int contractId = -1;
    double amount = 0.0;
    double relationValue = 0.0;
    ContractType contractType = ContractType::Delivery;
    int contractOriginStar = -1;
    int contractTargetStar = -1;
    int contractTargetAgent = -1;
    int contractResource = -1;
    int contractAcceptedByAgent = -1;
    double contractAmount = 0.0;
    double contractReward = 0.0;
    double contractDeposit = 0.0;
    double contractPostedTime = 0.0;
    double contractDeadline = 0.0;
    double contractRisk = 0.0;
    double contractProgress = 0.0;
    bool contractCompleted = false;
    bool contractFailed = false;
    std::vector<double> marketPrices;
    std::vector<double> marketSupplyPressure;
    std::vector<double> marketDemandPressure;
};

struct SignalMemoryRecord {
    SignalType type = SignalType::OwnerReport;
    unsigned long long eventId = 0;
    int recipientFaction = -1;
    int subjectStar = -1;
    int destinationStar = -1;
    int sourceAgent = -1;
    int targetAgent = -1;
    int sourceFaction = -1;
    int targetFaction = -1;
    int ownerFaction = -1;
    int contractId = -1;
    double observedTime = -1.0;
    double amount = 0.0;
    double relationValue = 0.0;
    ContractType contractType = ContractType::Delivery;
    int contractOriginStar = -1;
    int contractTargetStar = -1;
    int contractTargetAgent = -1;
    int contractResource = -1;
    int contractAcceptedByAgent = -1;
    double contractAmount = 0.0;
    double contractReward = 0.0;
    double contractDeposit = 0.0;
    double contractPostedTime = 0.0;
    double contractDeadline = 0.0;
    double contractRisk = 0.0;
    double contractProgress = 0.0;
    bool contractCompleted = false;
    bool contractFailed = false;
    double averageSupplyPressure = 1.0;
    double averageDemandPressure = 1.0;
    bool absorbed = false;
    std::vector<double> marketPrices;
    std::vector<double> marketSupplyPressure;
    std::vector<double> marketDemandPressure;
};

// Главный игровой класс
class Game {
public:
    Cluster cluster;
    std::vector<Market> markets; // Локальные рынки по звёздам
    std::vector<Faction> factions;
    std::vector<Colony> colonies;
    std::vector<Contract> contracts;
    std::vector<Agent> agents;   // Агенты (торговцы, военные и т.д.)
    std::vector<FactionStarKnowledge> factionKnowledge;
    std::vector<FactionMarketKnowledge> factionMarketKnowledge;
    std::vector<double> factionMarketPrices;
    std::vector<double> factionMarketSupplyPressure;
    std::vector<double> factionMarketDemandPressure;
    std::vector<int> factionRelations;
    std::vector<PlayerStarKnowledge> playerKnowledge;
    std::vector<SignalPacket> pendingSignals;
    std::vector<std::vector<SignalMemoryRecord> > signalMemory;
    std::vector<unsigned short> routeNextHop;
    std::vector<double> marketUpdatedAt;
    double routeCacheBuiltAt = -1.0;
    int marketUpdateCursor = 0;
    double marketUpdateBudget = 0.0;
    double time; // Время симуляции (годы)
    double contractUpdateTimer = 0.0;
    double factionUpdateTimer = 0.0;
    int nextContractId = 1;
    unsigned long long nextSignalEventId = 1;
    int playerAgent = -1;
    int playerFaction = -1;
    int foundedColonies = 0;
    int capturedSystems = 0;
    std::string lastEvent;

    Game();
    void init(size_t num_stars);
    void update(double dt);
    bool saveToFile(const std::string& path);
    bool loadFromFile(const std::string& path);
    void updateMarkets(double dt);
    void updateColonies(double dt);
    void updateAgents(double dt);
    void updateContracts(double dt);
    void updateFactions(double dt);
    void processSignals();
    void rebuildRouteCache();
    int routeNextStar(int originStar, int targetStar) const;
    bool commandAgentToStar(int agentIndex, int starIndex);
    double routeDistance(int originStar, int targetStar) const;
    double agentRouteDistance(int agentIndex, int targetStar) const;
    double agentRouteTravelTime(int agentIndex, int targetStar) const;
    double agentRouteFuelNeeded(int agentIndex, int targetStar) const;
    double agentRouteFuelShortfall(int agentIndex, int targetStar) const;
    double agentRouteThreatRisk(int agentIndex, int targetStar) const;
    double playerRouteMarketConfidence(int targetStar, int elementIndex) const;
    double agentContractRouteDistance(int agentIndex, int contractId) const;
    double agentContractRouteTravelTime(int agentIndex, int contractId) const;
    double agentContractRouteFuelNeeded(int agentIndex, int contractId) const;
    double agentContractRouteFuelShortfall(int agentIndex, int contractId) const;
    double agentContractRouteThreatRisk(int agentIndex, int contractId) const;
    bool agentContractCargoFits(int agentIndex, int contractId) const;
    bool agentBuyElement(int agentIndex, int elementIndex);
    bool agentBuyElementAmount(int agentIndex, int elementIndex, double amount);
    bool agentBuyFuel(int agentIndex);
    bool agentSellCargo(int agentIndex);
    bool agentSellCargoAmount(int agentIndex, double amount);
    bool agentAcceptContract(int agentIndex, int contractId);
    bool agentCompleteContract(int agentIndex, int contractId);
    int agentCompleteContracts(int agentIndex);
    bool agentAutoTrade(int agentIndex);
    bool playerFoundColony();
    bool playerHireShip();
    int playerColonyCount() const;
    bool playerCanOpenContractsAt(int starIndex) const;
    std::vector<Contract> playerVisibleContractsAt(int starIndex) const;
    void resizeFactionKnowledge();
    void seedFactionKnowledge(int factionIndex, int centerStar, double radiusLy);
    void observeStarForFaction(int factionIndex, int starIndex);
    void observeMarketForFaction(int factionIndex, int starIndex);
    void applyOwnerKnowledge(int factionIndex, int starIndex, int ownerFaction, double observedTime, bool visited);
    void applyMarketKnowledge(int factionIndex, int starIndex, const std::vector<double>& prices, const std::vector<double>& supplyPressure, const std::vector<double>& demandPressure, double observedTime);
    void queueOwnerSignal(int factionIndex, int subjectStar, int originStar);
    void queueMarketSignal(int factionIndex, int subjectStar, int originStar);
    void queueContractSignal(int factionIndex, int contractId, int originStar, int subjectStar);
    void queueCombatSignal(int factionIndex, int originStar, int sourceAgent, int targetAgent, double value, unsigned long long eventId = 0);
    void queueSettlementSignal(int factionIndex, int originStar, double amount, unsigned long long eventId = 0);
    void queueDiplomacySignal(int factionIndex, int originStar, int targetFaction, int relationValue);
    void observeLocalThreatsForFaction(int factionIndex, int starIndex);
    void seedPlayerKnowledge(int centerStar, double radiusLy);
    void observeStar(int starIndex);
    void absorbLocalSignalsForFaction(int factionIndex, int observerStar, bool updatePlayerMemory);
    void resizeFactionRelations();
    int factionRelation(int factionA, int factionB) const;
    void setFactionRelation(int factionA, int factionB, int value);
    void adjustFactionRelation(int factionA, int factionB, int delta);
    bool factionKnowsOwner(int factionIndex, int starIndex) const;
    int factionKnownOwner(int factionIndex, int starIndex) const;
    double factionKnownOwnerAge(int factionIndex, int starIndex) const;
    bool factionKnowsMarket(int factionIndex, int starIndex) const;
    double factionKnownPrice(int factionIndex, int starIndex, int elementIndex) const;
    double factionKnownSupplyPressure(int factionIndex, int starIndex, int elementIndex) const;
    double factionKnownDemandPressure(int factionIndex, int starIndex, int elementIndex) const;
    double factionKnownMarketAge(int factionIndex, int starIndex) const;
    double factionKnownMarketConfidence(int factionIndex, int starIndex, int elementIndex) const;
    bool factionKnowsOwnerAt(int factionIndex, int observerStar, int starIndex) const;
    int factionKnownOwnerAt(int factionIndex, int observerStar, int starIndex) const;
    double factionKnownOwnerAgeAt(int factionIndex, int observerStar, int starIndex) const;
    bool factionKnowsMarketAt(int factionIndex, int observerStar, int starIndex) const;
    double factionKnownPriceAt(int factionIndex, int observerStar, int starIndex, int elementIndex) const;
    double factionKnownSupplyPressureAt(int factionIndex, int observerStar, int starIndex, int elementIndex) const;
    double factionKnownDemandPressureAt(int factionIndex, int observerStar, int starIndex, int elementIndex) const;
    double factionKnownMarketAgeAt(int factionIndex, int observerStar, int starIndex) const;
    double factionKnownMarketConfidenceAt(int factionIndex, int observerStar, int starIndex, int elementIndex) const;
    bool playerAtStar(int starIndex) const;
    bool playerCanSeeAgent(int agentIndex) const;
    int playerVisibleAgentCount() const;
    int factionKnownThreatCount(int factionIndex, int starIndex) const;
    double factionKnownThreatAge(int factionIndex, int starIndex) const;
    double factionKnownThreatRisk(int factionIndex, int starIndex) const;
    double factionRouteThreatRisk(int factionIndex, int originStar, int targetStar) const;
    bool playerKnowsOwner(int starIndex) const;
    int playerKnownOwner(int starIndex) const;
    double playerKnownOwnerAge(int starIndex) const;
    bool playerKnowsMarket(int starIndex) const;
    double playerKnownPrice(int starIndex, int elementIndex) const;
    double playerKnownSupplyPressure(int starIndex, int elementIndex) const;
    double playerKnownDemandPressure(int starIndex, int elementIndex) const;
    double playerKnownMarketAge(int starIndex) const;
    double playerKnownMarketConfidence(int starIndex, int elementIndex) const;
    void render(); // TODO: добавить SDL2
};
