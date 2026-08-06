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
//
// `sellPrice` — НЕ замороженный снимок, а МОДЕЛЬ на текущий момент. Биржа здесь не
// биржа в привычном смысле: живого канала с чужой системой не существует (свет идёт
// туда десятилетиями), есть только «что я там видел» плюс экстраполяция. Модель
// строится по тому же закону, которому подчиняется настоящий рынок: цена медленно
// тянется к опорной цене скопления, поэтому чем старше наблюдение, тем сильнее
// оценка сползает от увиденного к опорной. Вес наблюдения — это ровно
// `confidence = exp(-age/tau)`, уже существующий в системе знаний.
// `observedPrice` хранит исходное наблюдение, чтобы игрок видел и то, и другое.
struct ArbitrageDeal {
    int element = -1;
    int targetStar = -1;
    double buyPrice = 0.0;     // цена в системе отправления (живая — мы в ней стоим)
    double sellPrice = 0.0;    // МОДЕЛЬНАЯ цена назначения на сейчас (не снимок, см. ниже)
    double observedPrice = 0.0;// что игрок видел своими глазами в момент посещения
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
//
// Планка первого тысячелетия — 40 000. Это НЕ круглое число из головы, а
// пересчёт: смысл квоты («держит игрока в маршруте весь период, но берётся
// одним бортом») задаётся числом РЕЙСОВ, а не суммой в кредитах. Держи это
// соотношение: меняешь масштаб экономики или ставку — пересчитывай планку по
// рейсам, а не по кредитам.
//   было 10 000 при деревенской экономике        => ~123 рейса
//   65 000 после пересчёта масштаба §17          => ~122 рейса (тариф 533/рейс)
//   40 000 после оживления ставки §19            => ~120 рейсов (тариф 332/рейс)
// Ставка выросла 0.05 -> 0.08, но валовая выручка с рейса упала — поэтому
// планку пришлось не поднять, а опустить. Считай по замеру, а не по интуиции.
const double LICENCE_PERIOD_YEARS = 1000.0;      // релятивистская корректировка рынка
const double LICENCE_QUOTA_BASE = 40000.0;       // квота первого тысячелетия (~120 рейсов)
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
// Цель — МИЛЛИОН за вторую лицензию. Это намеренно: второй борт — не апгрейд, а
// смена масштаба игры, и стоить он должен как цель на сотни рейсов, а не как пара
// удачных сделок. Прежние 5000 закрывались за три рейса и обесценивали всю ветку
// роста флота.
//
// ⚠️ Цена ВЫВОДИТСЯ из квоты (`licenceQuotaBase * K * licenceCount`), поэтому
// при каждом движении квоты множитель двигается обратно, чтобы вторая лицензия
// осталась МИЛЛИОНОМ: 1000 при квоте 1 000, затем 100 при 10 000, 15 при 65 000,
// 25 при 40 000. Миллион за второй борт — design point, а не побочный продукт.
const double LICENCE_PRICE_K = 25.0;
// Досрочное погашение квоты кредитами (для тех, кто не хочет ждать тарифов с
// продаж). С наценкой: иначе богатый игрок просто откупался бы каждое
// тысячелетие и торговля — смысл квоты — перестала бы быть нужна.
const double LICENCE_SETTLE_K = 1.5;

// Доля годового выпуска системы, которая достаётся ВЛАДЕЛЬЦУ — суверенный сбор
// с местного хозяйства, брат-близнец пошлины с заходящих кораблей
// (`tariffFor`). Доход колонии считается ОТ ЭТОЙ ДОЛИ, а не формулой по
// `star.industry`, поэтому он живёт вместе с ценами, растёт от построенного и
// падает, когда склад вычерпан (`strain`) — §13.5 наконец выполняется буквально.
//
// Величина ОТБАЛАНСИРОВАНА, а не выведена: цена системы остаётся произведением
// контекстных множителей (люди, заводы, обитаемость, недра, построенное,
// суверенитет — §13.3), и окупаемость получается их СЛЕДСТВИЕМ, а не входом.
// Замер по 1200 системам: при этой доле медиана ложится на ~10 000 лет
// (десять лицензионных периодов).
//
// ⚠️ Разброс окупаемости по системам ОСТАЁТСЯ и это фича, а не недоделка:
// богатая людьми и заводами система дороже, чем даёт её выпуск, а тихая
// рудная — наоборот. Именно поэтому «какую систему купить» — вопрос, а не
// таблица. Гарантии окупиться нет ни у одной.
const double COLONY_OWNER_DUTY = 0.029;

// --- Покупка системы -----------------------------------------------------
// Колония — это не отдельная механика, а ПРАВО СОБСТВЕННОСТИ на систему.
// Игрок не «основывает» ничего: он покупает уже живущее хозяйство целиком,
// со всем населением, заводами и недрами. Дальше система живёт ровно так же,
// как жила до сделки (Game::updateColonies), просто прибыль капает в кассу,
// из которой владелец берёт по желанию, а местный рынок для него бесплатен.
//
// Цена — взвешенное произведение характеристик: каждый множитель отвечает на
// вопрос «во сколько раз эта система ценнее пустого камня». Порядок — МИЛЛИАРД,
// то есть тир капитальных кораблей (Battlecruiser 5e9): владение системой —
// цель на всю партию, а не покупка между рейсами.
// Опорные величины откалиброваны замером по сгенерированному скоплению (1200
// систем, три сида): медиана ложится на ~1e9, дешёвый фронтир — на сотни
// миллионов, богатейшая система — на десятки миллиардов. Проверяется в
// `make balance` (SYSTEM PRICE), чтобы правка любого множителя не увела порядок.
//
// ⚠️ POP_REF и TURNOVER_REF пересчитаны в §17 вместе с масштабом мира: население
// системы выросло с тысяч до десятков миллионов, годовой выпуск — с тысяч Cr до
// десятков миллионов. Оба опорных числа — МЕДИАНА скопления, то есть типичная
// система даёт по каждому множителю ровно ×2. Тронешь генератор населения —
// перемеряй эти два числа, иначе цена уедет на порядки.
const double SYSTEM_PRICE_BASE = 9.0e7;          // цена «пустого камня» без людей и недр
const double SYSTEM_PRICE_POP_REF = 5.0e7;       // население, удваивающее цену (медиана скопления)
const double SYSTEM_PRICE_IND_REF = 1.5;         // индустрия, удваивающая цену
const double SYSTEM_PRICE_HAB_W = 1.0;           // вклад обитаемости (потенциал роста)
const double SYSTEM_PRICE_TURNOVER_REF = 1.1e7;  // годовой выпуск недр (Cr/год), удваивающий цену
const double SYSTEM_PRICE_INFRA_W = 0.22;       // вклад уже построенной инфраструктуры
const double SYSTEM_PRICE_SHIPYARD_W = 0.30;    // вклад каждого уровня верфи
// Суверенная надбавка: чужая фракция расстаётся с территорией неохотно, и чем
// она сильнее, тем дороже её уговорить. Ничейный камень надбавки не несёт.
const double SYSTEM_PRICE_FOREIGN_W = 0.55;

// Разбор цены по множителям — чтобы окно колонии показывало не голое число,
// а ИЗ ЧЕГО оно собрано. Ровно те же поля, по которым считается price.
struct SystemPrice {
    double population = 1.0;
    double industry = 1.0;
    double habitability = 1.0;
    double resources = 1.0;   // недра: годовой выпуск в кредитах
    double development = 1.0; // инфраструктура и верфь уже стоящей колонии
    double sovereignty = 1.0; // надбавка за выкуп у чужой фракции
    double total = 0.0;       // итог в кредитах
};

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

// Кредиты, положенные на счёт фракции, о которых ЗНАЕТ ЕЩЁ НЕ ВСЁ скопление.
//
// Счёт фракции — не сейф, а СВЕДЕНИЯ, и они идут со скоростью света, как и всё
// остальное в этом мире. Внёс на счёт в одном конце скопления — сумма на счету
// появилась мгновенно, но потратить её на другом конце нельзя, пока туда не
// дошло известие: иначе один и тот же кредит был бы снят дважды в двух местах
// одновременно. Отсюда правило: тратить можно ровно то, что УЖЕ покрыто светом
// по всему скоплению.
//
// Снятие в поплавок НЕ попадает: оно вычитается из счёта немедленно и потому
// консервативно (нельзя снять то, что уже снято за световые годы отсюда).
// Плавает только приход — та половина, где спешка была бы жульничеством.
struct CreditFloat {
    int faction = -1;
    double amount = 0.0;    // сколько ещё не подтверждено
    double clearsAt = 0.0;  // game.time, когда известие покроет всё скопление
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
    // Клиринговая палата скопления — межзвёздный банк и биржа. Не игрок и не
    // держава: она держит богатые системы в ядре, собирает ВСЕ лицензионные
    // деньги (тариф, выкуп, цена лицензии, досрочное погашение) и тратит их
    // как государство — субсидирует чахнущие колонии и развивает себя. Раньше
    // всё это капало в казну ФРАКЦИИ ИГРОКА, то есть игрок платил тариф сам
    // себе; после §16, давшего доступ к казне, тариф стало ещё и можно снять
    // обратно — квота переставала стоить хоть что-то.
    int clearingFaction = -1;
    int boughtSystems = 0;   // сколько систем игрок выкупил в собственность
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
    // Денежный уровень скопления: во сколько раз услуга стоит дороже базовой
    // (`ECON_CREDITS_PER_SERVICE`). Входит в ценовой дрейф КАЖДОГО рынка и
    // держит ставку лицензионного тарифа.
    //
    // Меняется НЕ каждый такт, а СТУПЕНЬКОЙ — на той же тысячелетней сверке,
    // что пересматривает квоты. Это не оптимизация, а модель: свести цены
    // разных концов скопления можно только релятивистской корректировкой, и
    // другого момента, когда «скопление знает, сколько стоит само», нет.
    // 1.0 = услуга стоит ровно якорь `ECON_CREDITS_PER_SERVICE`. На этом же
    // числе висит ценовой привод `gLevelDrive` — единственное, что закрепляет
    // НОМИНАЛЬНЫЙ уровень цен скопления, поэтому мерить его «от начала партии»
    // нельзя: привод обнулился бы и якорь перестал держать.
    double clusterPriceLevel = 1.0;
    // Уровень В МОМЕНТ ОСНОВАНИЯ партии. Нужен потому, что одно число делало
    // ДВЕ работы с противоположной нормировкой:
    //   привод цен спрашивает «где мы относительно ЯКОРЯ» — это контур
    //     управления, он и должен мерить от ECON_CREDITS_PER_SERVICE;
    //   ставка тарифа спрашивает «насколько скопление подорожало С НАЧАЛА» —
    //     это про деньги игрока, и мерить надо от старта партии.
    // Пока их считало одно число, `LICENCE_TARIFF_BASE` не действовал НИКОГДА:
    // рынок втрое дешевле якоря, значит ставка вечно лежала на полу 0.05, а
    // BASE 0.08 и потолок 0.14 были мёртвыми константами.
    double clusterPriceBase = 1.0;


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

    std::vector<CreditFloat> creditFloat;   // приход, ещё не покрытый светом
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
    // Расход на маршрут: ОБА расходника сразу — топливо и рабочее тело.
    // feasible=false означает, что связка движок/рабочее тело физически
    // не тянет это плечо, сколько ни заливай.
    RouteCost agentRouteCost(int agentIndex, int targetStar) const;
    double agentRouteFuelNeeded(int agentIndex, int targetStar) const;
    double agentRouteFuelShortfall(int agentIndex, int targetStar) const;
    double agentRoutePropellantShortfall(int agentIndex, int targetStar) const;
    double agentRouteThreatRisk(int agentIndex, int targetStar) const;
    double playerRouteMarketConfidence(int targetStar, int elementIndex) const;
    double agentContractRouteDistance(int agentIndex, int contractId) const;
    double agentContractRouteTravelTime(int agentIndex, int contractId) const;
    RouteCost agentContractRouteCost(int agentIndex, int contractId) const;
    double agentContractRouteFuelNeeded(int agentIndex, int contractId) const;
    double agentContractRouteFuelShortfall(int agentIndex, int contractId) const;
    double agentContractRoutePropellantShortfall(int agentIndex, int contractId) const;
    double agentContractRouteThreatRisk(int agentIndex, int contractId) const;
    bool agentContractCargoFits(int agentIndex, int contractId) const;
    bool agentBuyElement(int agentIndex, int elementIndex);
    bool agentBuyElementAmount(int agentIndex, int elementIndex, double amount);
    // Станционный макрос «купи и залей» с наценкой за очистку.
    bool agentBuyFuel(int agentIndex);
    // Ручной перелив из трюма: бесплатно, но возить элемент надо самому.
    double agentLoadFuelFromCargo(int agentIndex, int elementIdx, double units);
    double agentLoadPropellantFromCargo(int agentIndex, int elementIdx, double units);
    // Назначение маршрута БЕЗ вылета: кнопка DESTINATION в окне системы.
    // Сам вылет — отдельным действием GO, чтобы между выбором цели и стартом
    // можно было настроить двигатель под неё.
    bool setAgentDestination(int agentIndex, int starIndex);
    // Крейсерская скорость как доля от потолка корпуса, 0.2..1.
    void agentSetCruiseFraction(int agentIndex, double fraction);
    // Подбирает режим двигателя и крейсер под КОНКРЕТНУЮ цель: перебирает пару
    // (throttle, cruise) и берёт самую дешёвую связку, которая долетает и
    // влезает в баки. Возвращает false, если цель недостижима ни в каком режиме.
    bool agentOptimiseForTarget(int agentIndex, int targetStar);
    // Режим двигателя 0..1: 0 — платим рабочим телом, 0.5 — ценовой оптимум,
    // 1 — платим топливом (максимальная скорость истечения).
    void agentSetThrottle(int agentIndex, double throttle);
    // Выбросить груз за борт: единственный выход, если перегрузился вдали от рынка.
    double agentJettisonCargo(int agentIndex, int elementIdx, double units);
    double agentDrainFuelToCargo(int agentIndex, int elementIdx, double units);
    double agentDrainPropellantToCargo(int agentIndex, int elementIdx, double units);
    bool agentSellCargo(int agentIndex);
    bool agentSellCargoAmount(int agentIndex, double amount, int elementIndex = -1);
    bool agentAcceptContract(int agentIndex, int contractId);
    bool agentCompleteContract(int agentIndex, int contractId);
    int agentCompleteContracts(int agentIndex);
    bool robAgent(int attackerIndex, int victimIndex);
    bool agentAutoTrade(int agentIndex);
    // --- Собственность на систему (см. SYSTEM_PRICE_* выше) ---
    // Цена системы с разбором по множителям. Работает для ЛЮБОЙ звезды, в том
    // числе неразведанной — окно колонии само решает, показывать её или нет.
    SystemPrice systemPrice(int starIndex) const;
    bool playerOwnsStar(int starIndex) const;   // система принадлежит игроку
    bool playerBuySystem();                     // выкупить систему, в которой стоим
    // Касса колонии. Доступна ТОЛЬКО на месте: прибыль копится сама, но забрать
    // её надо прилететь — так империя остаётся маршрутом, а не строкой в меню.
    // amount <= 0 или больше доступного — переводит всё, что есть.
    double playerColonyDeposit(double amount);  // положить в кассу, вернёт переведённое
    double playerColonyWithdraw(double amount); // забрать из кассы, вернёт полученное
    // --- Деньги между своими бортами --------------------------------------
    // Кошелёк принадлежит КАПИТАНУ и лежит на борту: фракция — абстракция без
    // координат, её казна и обязана быть общей, а корабль — предмет, и деньги
    // его обязаны быть там же, где он. Поэтому перевод возможен только когда
    // оба борта стоят В ОДНОЙ системе — тот же гейт, что у кассы колонии.
    // Второй борт из-за этого обязан ВЕРНУТЬСЯ, чтобы отдать выручку.
    int playerOtherShipHere() const;                     // другой свой борт рядом, -1 если нет
    double playerTransferCredits(double amount, bool give); // give=true — отдать соседу
    // --- Счёт фракции: деньги, которые ходят СВЕТОМ, а не с кораблём ---------
    // За сколько лет свет от этой звезды покроет ВСЁ скопление. Верхняя оценка
    // по геометрии (|звезда − центр| + радиус), поэтому сверка честно
    // консервативна и не требует перебора всех звёзд на каждую транзакцию.
    double creditClearYears(int starIndex) const;
    // Сколько на счету фракции РЕАЛЬНО (мгновенно, с точки зрения мира).
    double factionTreasuryAt(int factionIndex) const;
    // Сколько из этого уже покрыто светом, то есть доступно к трате где угодно.
    double factionClearedTreasury(int factionIndex) const;
    // Сколько ещё в пути (разница между двумя величинами выше).
    double factionCreditsInFlight(int factionIndex) const;
    // Зачислить приход на счёт фракции так, чтобы он стал доступен к трате
    // только после того, как известие о нём покроет скопление. Записи
    // склеиваются по году сверки, иначе колонии, телеграфирующие прибыль
    // каждый тик, наплодили бы их без счёта.
    void addCreditFloat(int factionIndex, double amount, int originStar);
    double playerAccountDeposit(double amount);   // кошелёк -> счёт (уйдёт в сверку)
    double playerAccountWithdraw(double amount);  // счёт -> кошелёк (только покрытое)
    double colonyLedgerAt(int starIndex) const; // касса колонии на звезде (0, если нет)
    double colonyIncomeAt(int starIndex) const; // доход колонии, Cr/год (0, если нет)
    // --- Лицензионная квота ---
    // Годовой выпуск недр системы В КРЕДИТАХ: Σ productionRate[i] * prices[i].
    // Один источник и для цены системы, и для дохода её владельца.
    double systemTurnover(int starIndex) const;
    double licenceQuotaTarget() const;   // сколько тарифов нужно за период
    void updateLicence(double dt);       // начисление ставки и смена периода
    // Медиана стоимости единицы услуги по всем рынкам скопления «сейчас».
    double measureClusterServiceCost() const;
    // Она же, отнесённая к уровню при основании партии. Чистая функция
    // состояния, без RNG: воспроизводится вместе с seed.
    double measureClusterPriceLevel() const;
    bool playerBuybackLicence();         // выкупить отозванную лицензию
    bool playerTradingBlocked();         // общий гейт BUY/SELL + объяснение в lastEvent
    double licencePrice() const;         // цена следующей лицензии
    double licenceSettleCost() const;    // цена досрочного погашения остатка квоты
    int playerShipCount() const;         // сколько бортов у игрока (= занятых лицензий)
    int playerFreeLicences() const;      // лицензий сверх имеющихся бортов
    bool playerBuyLicence();             // купить лицензию (+1 борт разрешён, +квота)
    bool playerSettleQuota();            // закрыть остаток квоты кредитами
    // elementFilter >= 0 — только этот элемент (тогда список = все разведанные
    // системы по нему), -1 — все элементы разом.
    std::vector<ArbitrageDeal> playerArbitrageBoard(int originStar, int maxDeals, int elementFilter = -1) const;
    // МОДЕЛЬ цены на СЕЙЧАС по устаревшему наблюдению (см. ArbitrageDeal).
    double playerProjectedPrice(int starIndex, int elementIndex) const;
    int playerSurveyedMarketCount() const;   // сколько рынков игрок обследовал
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
