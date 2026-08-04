#pragma once
#include "agent.h"
#include "cluster.h"
#include "colony.h"
#include "contract.h"
#include "faction.h"
#include "features.h"
#include "market.h"
#include <random>
#include <string>
#include <vector>
#include <unordered_map>

// Одна строка биржевой сводки: «купить здесь — продать там». Считается ТОЛЬКО по
// тем рынкам, которые игрок уже знает (§ система знаний/сигналов), поэтому данные
// бывают устаревшими — возраст и уверенность идут в той же строке. Разведка чужих
// систем становится источником дохода, а не декоративным флагом на карте.
struct ArbitrageDeal {
    int element = -1;
    int targetStar = -1;
    double buyPrice = 0.0;     // цена в системе отправления (живая — мы в ней стоим)
    double sellPrice = 0.0;    // известная цена назначения (может быть устаревшей)
    double units = 0.0;        // сколько влезает в трюм и по карману
    double profit = 0.0;       // ожидаемая прибыль с учётом проскальзывания и тарифов
    double distanceLy = 0.0;
    double ageYears = -1.0;    // сколько лет сведениям о рынке назначения
    double confidence = 0.0;   // 0..1 — насколько им можно верить
};

struct RouteEdge {
    int star = 0;
    double distance = 0.0;
    RouteEdge() {}
    RouteEdge(int star_, double distance_) : star(star_), distance(distance_) {}
};

const int STAR_COUNT = 10000;
const int CIV_COUNT = 100;
const int RESOURCE_TYPES = 118;

// Целевое население скопления в STAR_COUNT звёзд (для меньших миров — доля).
// Миры меньшего размера получают пропорционально меньше бортов, поэтому
// тестовые сцены остаются лёгкими.
const int AGENT_TARGET_FULL = 1024;
// Открытых контрактов на скопление той же полноты: доска заданий в 24 записи на
// 10 000 систем означала, что игрок не видит ни одного задания нигде, кроме дома.
const int CONTRACT_TARGET_FULL = 320;

// --- Лицензионная квота оборота ------------------------------------------
// Лицензия торговца не бесплатна: с каждой продажи удерживается тариф, и за
// отчётный период игрок обязан наторговать столько, чтобы уплаченных тарифов
// набралось не меньше квоты. Не набрал — лицензию отзывают, торговля
// замораживается до выкупа.
//
// Период — ТЫСЯЧЕЛЕТИЕ, и это не произвол: скопление ~100 световых лет в
// поперечнике, поэтому цены в разных его концах расходятся на века, и сверить
// их можно лишь релятивистской корректировкой раз в тысячу лет. Тогда же
// пересматривают и квоты. При «1 игровой год = 1 реальная секунда» это около
// 17 минут игры на скорости x1 — дедлайн ощутим, но не гонит.
//
// Квота растёт на 1% за период: скопление богатеет, планка ползёт вверх, и
// сидеть на одном отработанном маршруте вечно не выйдет.
const double LICENCE_PERIOD_YEARS = 1000.0;      // релятивистская корректировка рынка
const double LICENCE_QUOTA_BASE = 1000.0;        // квота первого тысячелетия
const double LICENCE_QUOTA_GROWTH = 1.01;        // +1% за каждый прожитый период
const double LICENCE_QUOTA_PER_EXTRA = 0.8;      // доля квоты за каждую лицензию сверх первой
const double LICENCE_TARIFF_BASE = 0.08;         // базовая ставка тарифа с продажи
const double LICENCE_TARIFF_MIN = 0.05;
const double LICENCE_TARIFF_MAX = 0.14;
const double LICENCE_BUYBACK_K = 2.0;            // выкуп = столько недобора
const double LICENCE_BUYBACK_MIN = 400.0;
// Цена НОВОЙ лицензии — кратно текущей квоте, и каждая следующая дороже: лицензия
// разрешает ещё один борт, но и поднимает планку, так что расширение всегда
// сознательная ставка, а не автоматический апгрейд.
//
// Множитель 1000 к квоте в 1000 Cr даёт МИЛЛИОН за вторую лицензию. Это намеренно:
// второй борт — не апгрейд, а смена масштаба игры, и стоить он должен как цель на
// сотни рейсов, а не как пара удачных сделок. Прежние 5000 закрывались за три рейса
// и обесценивали всю ветку роста флота.
const double LICENCE_PRICE_K = 1000.0;
// Досрочное погашение квоты кредитами (для тех, кто не хочет ждать тарифов с
// продаж). С наценкой: иначе богатый игрок просто откупался бы каждое
// тысячелетие и торговля — смысл квоты — перестала бы быть нужна.
const double LICENCE_SETTLE_K = 1.5;

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

struct Transaction {
    double time = 0.0;
    int starIndex = -1;
    double amount = 0.0;
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
    unsigned int seed = 42;
    std::string lastEvent;
    
    // --- Лицензионная квота оборота (см. константы LICENCE_* выше) ---
    double licenceQuotaPaid = 0.0;                   // уплачено тарифов в текущем периоде
    double licenceQuotaBase = LICENCE_QUOTA_BASE;    // планка на одну лицензию, растёт на 1% за период
    double licencePeriodEnd = LICENCE_PERIOD_YEARS;  // game.time окончания периода
    double licenceTariffRate = LICENCE_TARIFF_BASE;  // ставка, медленно плывёт от оборота скопления
    double licenceBuyback = 0.0;                     // цена выкупа после отзыва
    int licenceCount = 1;                            // сколько лицензий у игрока
    bool licenceRevoked = false;                     // торговля заморожена
    int licencePeriodsMet = 0;                       // сколько периодов закрыто успешно


    // --- Расширения геймплея (вертикальный срез) ---
    TechState tech;                     // хромокоры игрока
    std::vector<MarketEvent> marketEvents;
    std::vector<Anomaly> anomalies;
    std::vector<NewsItem> news;         // скользящая лента новостей
    double marketEventTimer = 0.0;
    double anomalyTimer = 0.0;
    bool playerMining = false;
    double miningTimer = 0.0;
    int miningStar = -1;
    double miningYieldAccum = 0.0;

    std::vector<Transaction> transactions;
    double lastPlayerMoney = -1.0;
    bool everEnteredLocal = false;      // заходил ли игрок в локальный полёт (для панели целей)

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
    bool abortAgentRoute(int agentIndex);
    bool commandAgentToStar(int agentIndex, int starIndex);
    bool buyShip(int agentIndex, int starIndex, int classId);
    bool buyAdditionalShip(int agentIndex, int starIndex, int classId);
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
    bool agentSellCargoAmount(int agentIndex, double amount, int elementIndex = -1);
    bool agentAcceptContract(int agentIndex, int contractId);
    bool agentCompleteContract(int agentIndex, int contractId);
    int agentCompleteContracts(int agentIndex);
    bool robAgent(int attackerIndex, int victimIndex);
    bool agentAutoTrade(int agentIndex);
    bool playerFoundColony();
    bool playerHireShip();
    // --- Лицензионная квота ---
    double licenceQuotaTarget() const;   // сколько тарифов нужно за период
    void updateLicence(double dt);       // начисление ставки и смена периода
    bool playerBuybackLicence();         // выкупить отозванную лицензию
    bool playerTradingBlocked();         // общий гейт BUY/SELL + объяснение в lastEvent
    double licencePrice() const;         // цена следующей лицензии
    double licenceSettleCost() const;    // цена досрочного погашения остатка квоты
    int playerShipCount() const;         // сколько бортов у игрока (= занятых лицензий)
    int playerFreeLicences() const;      // лицензий сверх имеющихся бортов
    bool playerBuyLicence();             // купить лицензию (+1 борт разрешён, +квота)
    bool playerSettleQuota();            // закрыть остаток квоты кредитами
    std::vector<ArbitrageDeal> playerArbitrageBoard(int originStar, int maxDeals) const;
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

    // --- Срез: новые системы (реализованы в отдельных .cpp) ---
    void pushNews(const std::string& text, int kind = 0);   // game.cpp
    void updateMining(double dt);                           // mining.cpp
    bool playerToggleMining();                              // mining.cpp
    void updateMarketEvents(double dt);                     // spaceevents.cpp
    void spawnMarketEvent();                                // spaceevents.cpp
    void seedAnomalies();                                   // anomaly.cpp
    void updateAnomalies(double dt);                        // anomaly.cpp
    bool playerScanAnomaly();                               // anomaly.cpp

    bool playerRepairHull();                                // combat.cpp
    void grantChromocore(int stat);                         // chromo.cpp
    void addResearch(double amount);                        // chromo.cpp
    bool buyModule(int agentIndex, int defIndex);           // modules.cpp
    bool equipModule(int agentIndex, int defIndex);         // modules.cpp
    bool unequipModule(int agentIndex, int moduleListIndex);// modules.cpp
    int shipyardLevelAtStar(int starIndex) const;           // modules.cpp

    void render(); // TODO: добавить SDL2
};

const char* chromocoreStatLabel(int stat); // chromo.cpp
void downgradeAgentToEscapePod(Agent& a);   // game.cpp — «смерть» макро-агента: спас-капсула + сброс груза
bool localDockSellCargo(Game& game, int agentIndex, int starIndex); // game.cpp — торговец-зеркало продаёт груз на местном рынке при стыковке (§5.13.18)
