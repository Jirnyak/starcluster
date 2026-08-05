// Регрессия БАЛАНСА. Лестница проверок проекта покрывала память (ASan) и инварианты
// локального режима (soak), но экономику — ничем. При этом именно там оказались все
// крупные ошибки: сделка исполнялась без проскальзывания и давала x44 капитала за
// рейс; биржевая сводка обещала +7869 там, где реальность давала +285; стартовая
// система на 2 сидах из 5 не имела ни одного прибыльного маршрута; первые два
// покупаемых корпуса были по трюму МЕНЬШЕ стартового.
//
// Все эти баги были найдены одноразовыми стендами, которые умерли вместе с сессией.
// Здесь они превращены в постоянные проверки: каждая ловит СВОЙ класс регрессии и
// объясняет, что именно сломалось. Числа-пороги намеренно широкие — цель не заморозить
// баланс, а поймать поломку МЕХАНИЗМА (исчезло проскальзывание, сводка начала врать,
// в лестнице цен появилась дыра).
//
// Запуск: `make balance`.
#include "game.h"
#include "market.h"
#include "ship.h"
#include "modules.h"
#include "drive.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void check(bool ok, const std::string& what, const std::string& detail) {
    std::printf("%-4s %-46s %s\n", ok ? "ok" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) ++gFailures;
}

// Мир с заданным seed, прокрученный до устоявшихся цен, с разведанными соседями.
void buildWorld(Game& g, unsigned seed, int surveyed) {
    g.seed = seed;
    g.init(1200);
    for (int y = 0; y < 20; ++y) g.update(1.0);
    for (int i = 0; i < int(g.cluster.stars.size()) && i < surveyed; ++i) {
        g.observeMarketForFaction(g.playerFaction, i);
    }
}

// Исполняет лучшую сделку сводки по-настоящему и возвращает фактическую прибыль.
double executeTopDeal(Game& g, ArbitrageDeal& outDeal, bool& ok) {
    ok = false;
    const int pa = g.playerAgent;
    const int home = g.agents[pa].currentStar;
    const std::vector<ArbitrageDeal> board = g.playerArbitrageBoard(home, 1, -1);
    if (board.empty()) return 0.0;
    outDeal = board[0];
    const double before = g.agents[pa].money;
    if (!g.agentBuyElementAmount(pa, outDeal.element, outDeal.units)) return 0.0;
    g.agents[pa].currentStar = outDeal.targetStar;
    g.agents[pa].ship.enRoute = false;
    g.agentSellCargoAmount(pa, 1e18, outDeal.element);
    ok = true;
    return g.agents[pa].money - before;
}

// --- 1. Проскальзывание существует -----------------------------------------
// Ловит возврат к исполнению всего объёма по цене ДО сделки. Признак: продажа
// крупной партии в тонкий рынок должна давать за единицу заметно МЕНЬШЕ котировки.
void testSlippageExists() {
    Game g; buildWorld(g, 42, 80);
    const int home = g.agents[g.playerAgent].currentStar;
    const Market& m = g.markets[home];
    int probe = -1;
    for (int e = 0; e < int(elementCount()); ++e) {
        if (m.prices[e] > 0.01 && m.depthOf(e) > 0.01) { probe = e; break; }
    }
    if (probe < 0) { check(false, "проскальзывание: есть подходящий элемент", "не нашёлся"); return; }

    const double depth = m.depthOf(probe);
    const double quote = m.prices[probe];
    const double small = m.executionPrice(probe, depth * 0.01, true);   // сделка в 1% глубины
    const double huge  = m.executionPrice(probe, depth * 20.0, true);   // в 20 глубин

    char buf[160];
    std::snprintf(buf, sizeof(buf), "котировка %.3f, мелкая %.3f, крупная %.3f", quote, small, huge);
    check(small > quote * 0.95 && huge < quote * 0.35,
          "мелкая сделка ~по котировке, крупная обваливает", buf);

    const double up = m.executionPrice(probe, depth * 20.0, false);
    std::snprintf(buf, sizeof(buf), "покупка 20 глубин: %.3f против котировки %.3f", up, quote);
    check(up > quote * 1.5, "крупная закупка сама разгоняет цену", buf);
}

// --- 2. Один рейс не удваивает капитал --------------------------------------
// Ловит любую поломку, от которой торговля снова становится печатным станком
// (замер до введения проскальзывания: x44 за рейс).
//
// ⚠️ Мерить это от СТАРТОВОГО кошелька нельзя. Стартовый капитал — 100 Cr
// (решение пользователя), а на объёме, который не двигает рынок, отношение
// прибыли к капиталу равно голому спреду скопления (30x by design, §10.3).
// То есть «x14.8 за рейс» на нищем старте — не поломка, а арифметика: игрок
// беден, а не механизм сломан. Поэтому проверяем ДВА разных утверждения:
//   а) при рабочем капитале (тысячи) отношение мало — проскальзывание кусается;
//   б) отдача монотонно падает с ростом капитала — механизм вообще существует.
// Это тот же урок, что §10.6: сперва убедись, что сценарий пробника осмыслен.
void testRunProfitSane() {
    const unsigned seeds[] = {42u, 7u, 2024u, 999u};
    const double REFERENCE_CAPITAL = 3600.0;

    double worstRatio = 0.0;
    unsigned worstSeed = 0;
    int monotone = 0, monotoneTotal = 0;
    for (unsigned seed : seeds) {
        {
            Game g; buildWorld(g, seed, 80);
            g.agents[g.playerAgent].money = REFERENCE_CAPITAL;
            ArbitrageDeal d; bool ok = false;
            const double profit = executeTopDeal(g, d, ok);
            if (ok) {
                const double ratio = profit / REFERENCE_CAPITAL;
                if (ratio > worstRatio) { worstRatio = ratio; worstSeed = seed; }
            }
        }
        // Кривая отдачи: бедный рейс окупается в разы лучше богатого — именно
        // это и делает проскальзывание. Если механизм отключат, кривая станет
        // плоской (отношение перестанет зависеть от капитала).
        const double purses[] = {100.0, 3600.0, 120000.0};
        double prev = -1.0;
        bool falling = true, measured = false;
        for (double purse : purses) {
            Game g; buildWorld(g, seed, 80);
            g.agents[g.playerAgent].money = purse;
            ArbitrageDeal d; bool ok = false;
            const double profit = executeTopDeal(g, d, ok);
            if (!ok) continue;
            const double ratio = profit / purse;
            if (prev >= 0.0 && ratio > prev * 1.02) falling = false;
            prev = ratio;
            measured = true;
        }
        if (measured) { ++monotoneTotal; if (falling) ++monotone; }
    }

    char buf[160];
    std::snprintf(buf, sizeof(buf), "при рабочих %.0F Cr максимум %.2Fx (seed %u)",
                  REFERENCE_CAPITAL, worstRatio, worstSeed);
    check(worstRatio < 3.0, "один рейс не печатает капитал", buf);

    std::snprintf(buf, sizeof(buf), "отдача падает с ростом кошелька на %d из %d сидов",
                  monotone, monotoneTotal);
    check(monotoneTotal > 0 && monotone == monotoneTotal,
          "проскальзывание наказывает объём", buf);
}

// --- 3. Сводка не врёт на свежих данных -------------------------------------
// Ловит рассогласование прогноза и исполнения: сводка считала выручку по голой
// котировке и обещала в 27 раз больше реального.
void testBoardHonest() {
    const unsigned seeds[] = {42u, 7u, 2024u, 999u};
    double worstErr = 0.0;
    unsigned worstSeed = 0;
    int checked = 0;
    for (unsigned seed : seeds) {
        Game g; buildWorld(g, seed, 80);
        ArbitrageDeal d; bool ok = false;
        const double actual = executeTopDeal(g, d, ok);
        if (!ok || d.profit <= 0.0 || d.confidence < 0.5) continue;   // спрашиваем только со свежих данных
        ++checked;
        const double err = std::fabs(actual - d.profit) / d.profit;
        if (err > worstErr) { worstErr = err; worstSeed = seed; }
    }
    char buf[160];
    std::snprintf(buf, sizeof(buf), "худшая ошибка %.0f%% на %d проверках (seed %u)",
                  worstErr * 100.0, checked, worstSeed);
    check(checked > 0 && worstErr < 0.5, "прогноз сводки сходится с исполнением", buf);
}

// --- 4. У старта всегда есть чем торговать ----------------------------------
// Ловит регресс pickStarterSystem: до него 2 сида из 5 давали старт без единого
// прибыльного маршрута, и первые пять минут были лотереей.
void testStarterAlwaysTradeable() {
    const unsigned seeds[] = {42u, 7u, 12345u, 999u, 2024u, 31337u};
    int barren = 0;
    std::string names;
    for (unsigned seed : seeds) {
        Game g; buildWorld(g, seed, 60);
        const int home = g.agents[g.playerAgent].currentStar;
        if (g.playerArbitrageBoard(home, 1, -1).empty()) {
            ++barren;
            names += " " + std::to_string(seed);
        }
    }
    char buf[160];
    std::snprintf(buf, sizeof(buf), "пустых стартов %d из %d%s", barren,
                  int(sizeof(seeds) / sizeof(seeds[0])), names.c_str());
    check(barren == 0, "у каждого старта есть прибыльный маршрут", buf);
}

// --- 5. Вместимость трюма окупается -----------------------------------------
// Ловит вырождение, при котором большой трюм перестаёт значить (проверяем на
// СМЕШАННОМ грузе, как игра и позволяет: много разных элементов за рейс).
double basketProfit(unsigned seed, double cargo) {
    Game g; buildWorld(g, seed, 120);
    const int pa = g.playerAgent;
    const int home = g.agents[pa].currentStar;
    g.agents[pa].ship.cargoCapacity = cargo;
    g.agents[pa].money = 1e11;                       // деньги не должны быть узким местом
    const double before = g.agents[pa].money;
    std::vector<std::pair<int, int> > plan;
    for (int e = 0; e < int(elementCount()); ++e) {
        if (shipCargoMass(g.agents[pa].ship) >= cargo * 0.999) break;
        const std::vector<ArbitrageDeal> one = g.playerArbitrageBoard(home, 1, e);
        if (one.empty() || one[0].profit <= 0.0) continue;
        if (!g.agentBuyElementAmount(pa, e, one[0].units)) continue;
        plan.push_back(std::make_pair(e, one[0].targetStar));
    }
    for (size_t i = 0; i < plan.size(); ++i) {
        g.agents[pa].currentStar = plan[i].second;
        g.agents[pa].ship.enRoute = false;
        g.agentSellCargoAmount(pa, 1e18, plan[i].first);
    }
    return g.agents[pa].money - before;
}

void testCargoPaysOff() {
    const double small = basketProfit(42u, 110.0);
    const double big = basketProfit(42u, 4000.0);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "трюм 110 -> %.0f, трюм 4000 -> %.0f (x%.1f)",
                  small, big, small > 0.0 ? big / small : 0.0);
    check(small > 0.0 && big > small * 3.0, "больший трюм заметно прибыльнее", buf);
}

// --- 6. Разведка окупается ---------------------------------------------------
// Ловит обесценивание главной петли мотивации: знать больше рынков должно быть выгодно.
void testSurveyPaysOff() {
    Game a; buildWorld(a, 42, 30);
    Game b; buildWorld(b, 42, 300);
    const int homeA = a.agents[a.playerAgent].currentStar;
    const int homeB = b.agents[b.playerAgent].currentStar;
    const std::vector<ArbitrageDeal> ba = a.playerArbitrageBoard(homeA, 1, -1);
    const std::vector<ArbitrageDeal> bb = b.playerArbitrageBoard(homeB, 1, -1);
    const double pa = ba.empty() ? 0.0 : ba[0].profit;
    const double pb = bb.empty() ? 0.0 : bb[0].profit;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "30 рынков -> %.0f, 300 рынков -> %.0f", pa, pb);
    check(pb >= pa, "больше разведки — не хуже сделки", buf);
}

// --- 7. Лестница цен без дыр -------------------------------------------------
// Ловит появление разрыва, через который игроку нечего покупать: между соседними
// ступенями (модули + корпуса) не должно быть скачка больше чем в 5 раз.
void testPriceLadderContinuous() {
    std::vector<double> rungs;
    const std::vector<ShipClass>& ships = shipClasses();
    for (size_t i = 0; i < ships.size(); ++i) {
        if (ships[i].price > 0.0) rungs.push_back(ships[i].price);
    }
    const std::vector<ModuleDef>& mods = moduleDefs();
    for (size_t i = 0; i < mods.size(); ++i) {
        if (mods[i].price > 0.0) rungs.push_back(mods[i].price);
    }
    std::sort(rungs.begin(), rungs.end());

    // Проверяем только ДОСТИЖИМУЮ часть лестницы: то, что игрок реально может
    // купить за обозримое число рейсов. Дальний хайэнд — намеренно эндгейм.
    const double REACHABLE = 1.0e6;
    double worstJump = 1.0, at = 0.0;
    for (size_t i = 1; i < rungs.size(); ++i) {
        if (rungs[i] > REACHABLE) break;
        if (rungs[i - 1] <= 0.0) continue;
        const double jump = rungs[i] / rungs[i - 1];
        if (jump > worstJump) { worstJump = jump; at = rungs[i]; }
    }
    char buf[160];
    std::snprintf(buf, sizeof(buf), "худший скачок x%.1f (на цене %.0f)", worstJump, at);
    check(worstJump <= 5.0, "в достижимой части лестницы нет дыр", buf);
}

// --- 8. Стартовый корпус числится в классах ---------------------------------
// Ловит возврат «безымянного» стартового корабля: тогда buyShip не находит текущий
// корпус и не засчитывает его цену — игрок платит полную стоимость первой замены.
void testStarterHullIsAClass() {
    Game g; buildWorld(g, 42, 10);
    const std::string mine = g.agents[g.playerAgent].ship.name;
    const std::vector<ShipClass>& ships = shipClasses();
    bool found = false;
    double minePrice = 0.0, mineCargo = 0.0;
    for (size_t i = 0; i < ships.size(); ++i) {
        if (ships[i].name == mine) { found = true; minePrice = ships[i].price; mineCargo = ships[i].cargoCapacity; }
    }
    char buf[160];
    std::snprintf(buf, sizeof(buf), "корпус '%s' в списке классов: %s", mine.c_str(), found ? "да" : "НЕТ");
    check(found, "стартовый корпус — настоящий класс (зачёт цены)", buf);
    if (!found) return;

    // И он не должен быть дороже первого апгрейда: иначе зачёт уводит цену в ноль.
    double cheapestAbove = 0.0;
    for (size_t i = 0; i < ships.size(); ++i) {
        if (ships[i].price > minePrice && (cheapestAbove <= 0.0 || ships[i].price < cheapestAbove)) {
            cheapestAbove = ships[i].price;
        }
    }
    std::snprintf(buf, sizeof(buf), "старт %.0f Cr / трюм %.0f, следующая ступень %.0f Cr",
                  minePrice, mineCargo, cheapestAbove);
    check(cheapestAbove > minePrice, "есть куда расти с первого корпуса", buf);
}

// --- 9. Лицензионная квота проходима, но не даром ---------------------------
// Ловит развал главного таймера: квоту должно быть реально закрыть торговлей за
// период, но не с одного рейса (иначе она перестаёт давить вовсе).
void testQuotaReachable() {
    Game g; buildWorld(g, 42, 120);
    ArbitrageDeal d; bool ok = false;
    const double before = g.licenceQuotaPaid;
    executeTopDeal(g, d, ok);
    const double tariff = g.licenceQuotaPaid - before;
    const double target = g.licenceQuotaTarget();
    const double runs = tariff > 0.0 ? target / tariff : 1e9;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "квота %.0f, тариф с рейса %.0f => рейсов %.1f", target, tariff, runs);
    check(tariff > 0.0 && runs > 1.0 && runs < 60.0, "квота закрывается за разумное число рейсов", buf);
}


// --- ДВИГАТЕЛЬНАЯ УСТАНОВКА -------------------------------------------------
// Топливо перестало быть абстрактным счётчиком: это ЭЛЕМЕНТ, который покупается
// в трюм и переливается в бункер. Проверки ловят обрыв любого звена цепочки.

// Полный цикл, ради которого всё затевалось: купил элемент -> он в трюме ->
// перелил в бак -> улетел. Если ломается любое звено, ломается вся игра.
void testCargoToTankLoop() {
    Game g;
    buildWorld(g, 42, 40);
    Ship& ship = g.agents[g.playerAgent].ship;
    const int propellant = shipDominantPropellantElement(ship);

    // Опустошаем бак и покупаем рабочее тело как ОБЫЧНЫЙ груз.
    ship.propellant.clear();
    const double cargoBefore = shipCargoMass(ship);
    const bool bought = g.agentBuyElementAmount(g.playerAgent, propellant, 40.0);
    const double cargoAfterBuy = shipCargoMass(ship);

    const double moved = g.agentLoadPropellantFromCargo(g.playerAgent, propellant, 40.0);
    const double cargoAfterMove = shipCargoMass(ship);

    char buf[200];
    std::snprintf(buf, sizeof(buf), "куплено в трюм %+.1f массы, перелито %.1f ед., трюм освободился на %+.1f",
        cargoAfterBuy - cargoBefore, moved, cargoAfterMove - cargoAfterBuy);
    check(bought && moved > 0.0 && cargoAfterBuy > cargoBefore + 1e-6 &&
          cargoAfterMove < cargoAfterBuy - 1e-6 && shipPropellantMix(ship).mass > 0.0,
          "цикл трюм -> бак работает", buf);
}

// Любой элемент грузится в любую ёмкость: запретов нет вообще. Раньше кнопки
// были серыми, потому что ёмкость держала один сорт — это и чиним.
void testAnyElementLoads() {
    Game g;
    buildWorld(g, 42, 10);
    const int pa = g.playerAgent;
    Ship& ship = g.agents[pa].ship;
    ship.fuel.clear();
    ship.propellant.clear();
    ship.cargoCapacity = 1e6;

    const char* probe[] = {"H", "He", "Fe", "Xe", "U", "Pb"};
    int loadedFuel = 0;
    int loadedTank = 0;
    for (size_t k = 0; k < sizeof(probe) / sizeof(probe[0]); ++k) {
        const int e = elementIndex(probe[k]);
        if (e < 0) continue;
        ship.cargo.push_back(Resource(probe[k], 4.0));
        if (g.agentLoadFuelFromCargo(pa, e, 1.0) > 0.0) ++loadedFuel;
        if (g.agentLoadPropellantFromCargo(pa, e, 1.0) > 0.0) ++loadedTank;
    }
    char buf[200];
    std::snprintf(buf, sizeof(buf), "из %d сортов в бункер легло %d, в бак %d (смесь из %zu и %zu)",
        int(sizeof(probe) / sizeof(probe[0])), loadedFuel, loadedTank,
        ship.fuel.size(), ship.propellant.size());
    check(loadedFuel == 6 && loadedTank == 6 && ship.fuel.size() > 1 && ship.propellant.size() > 1,
          "любой элемент грузится в любую ёмкость", buf);
}

// Мёртвый балласт: железо зажечь нельзя, поэтому смесь с ним слабеет.
// Но это ГРАДИЕНТ, а не запрет — залить его никто не мешает.
void testBallastWeakensMix() {
    Game g;
    buildWorld(g, 42, 10);
    Ship& ship = g.agents[g.playerAgent].ship;
    const int th = elementIndex("Th");
    const int fe = elementIndex("Fe");

    ship.fuel.clear();
    ship.fuel.push_back(Resource("Th", 10.0));
    const double pure = shipFuelMix(ship).specificEnergy;

    // Доливаем железа столько же по МАССЕ, сколько тория.
    const double thMass = 10.0 * elementUnitMass(th);
    ship.fuel.push_back(Resource("Fe", thMass / elementUnitMass(fe)));
    const double diluted = shipFuelMix(ship).specificEnergy;

    char buf[200];
    std::snprintf(buf, sizeof(buf), "чистый торий %.3f -> пополам с железом %.3f МэВ/нуклон",
        pure, diluted);
    check(pure > 0.0 && diluted < pure * 0.75 && diluted > 0.0,
          "балласт в смеси снижает энергию", buf);
}

// Сгоревшее топливо не исчезает: оно перегорает в золу и падает обратно в трюм.
// Лёгкие ядра сливаются вверх к железу, тяжёлые делятся вниз к нему же.
void testBurnedFuelBecomesAsh() {
    Game g;
    buildWorld(g, 42, 20);
    Ship& ship = g.agents[g.playerAgent].ship;
    ship.cargo.clear();

    const int fuelElem = shipDominantFuelElement(ship);
    const int ash = elementAshProduct(fuelElem);
    const int fuelZ = elementDefinitions()[fuelElem].atomicNumber;
    const int ashZ = ash >= 0 ? elementDefinitions()[ash].atomicNumber : -1;

    std::vector<Resource> produced;
    const double fuelBefore = shipFuelMix(ship).mass;
    shipConsumeForDeltaV(ship, 0.05, &produced);
    const double fuelAfter = shipFuelMix(ship).mass;

    char buf[200];
    std::snprintf(buf, sizeof(buf), "%s(Z=%d) -> %s(Z=%d), сожжено %.2f массы, золы %zu",
        elementDefinitions()[fuelElem].symbol, fuelZ,
        ash >= 0 ? elementDefinitions()[ash].symbol : "-", ashZ,
        fuelBefore - fuelAfter, produced.size());
    // Зола обязана лежать БЛИЖЕ к железу, чем топливо: это и есть источник энергии.
    const bool towardIron = ash >= 0 && std::abs(ashZ - 26) < std::abs(fuelZ - 26);
    check(fuelAfter < fuelBefore && !produced.empty() && towardIron,
          "сгоревшее топливо возвращается золой", buf);
}

// Размен, ради которого введён объём: по МАССЕ синтез бьёт деление, по ОБЪЁМУ
// наоборот. Если оба выиграет один и тот же элемент, выбора топлива нет.
void testFusionFissionTradeoff() {
    const int h = elementIndex("H");
    const int u = elementIndex("U");
    const std::vector<ElementDefinition>& e = elementDefinitions();
    const double hPerMass = e[h].specificEnergy;
    const double uPerMass = e[u].specificEnergy;
    const double hPerVolume = e[h].specificEnergy * e[h].density;
    const double uPerVolume = e[u].specificEnergy * e[u].density;

    char buf[200];
    std::snprintf(buf, sizeof(buf), "на массу H/U = %.1fx, на объём U/H = %.1fx",
        hPerMass / uPerMass, uPerVolume / hPerVolume);
    check(hPerMass > uPerMass * 2.0 && uPerVolume > hPerVolume * 2.0,
          "синтез силён на массу, деление на объём", buf);
}

// Железо — дно кривой связи: жечь там нечего. Проверяем, что континуум не
// выродился в «любой элемент — топливо»: доля поджига у железа должна быть 0.
void testIronIsDead() {
    const int fe = elementIndex("Fe");
    const std::vector<ElementDefinition>& e = elementDefinitions();
    double worstIgnition = 0.0;
    for (size_t d = 0; d < driveDefs().size(); ++d) {
        worstIgnition = std::max(worstIgnition, driveIgnitionFraction(int(d), fe));
    }
    const double h = e[elementIndex("H")].specificEnergy;
    const double u = e[elementIndex("U")].specificEnergy;
    char buf[200];
    std::snprintf(buf, sizeof(buf), "Fe %.3f = %.1f%% от H и %.1f%% от U, лучшая доля поджига %.3f",
        e[fe].specificEnergy, e[fe].specificEnergy / h * 100.0, e[fe].specificEnergy / u * 100.0,
        worstIgnition);
    check(e[fe].specificEnergy < h * 0.05 && e[fe].specificEnergy < u * 0.25 && worstIgnition < 0.01,
          "железо не топливо", buf);
}

// Своё топливо должно быть заметно выгоднее станционного, иначе весь смысл
// возить элементы самому пропадает и игрок просто жмёт одну кнопку.
void testSelfFuelBeatsStation() {
    Game g;
    buildWorld(g, 42, 20);
    const int pa = g.playerAgent;
    Ship& ship = g.agents[pa].ship;
    const int fuel = shipDominantFuelElement(ship);
    const int star = g.agents[pa].currentStar;
    if (fuel >= int(g.markets[star].prices.size())) { check(false, "своё топливо выгоднее станционного", "нет цены"); return; }

    const double raw = g.markets[star].prices[fuel] / elementUnitMass(fuel);

    // Станционная заправка: сколько кредитов ушло за единицу МАССЫ в бункере.
    ship.fuel.clear();
    const double moneyBefore = g.agents[pa].money;
    g.agentBuyFuel(pa);
    const double gotMass = shipFuelMix(ship).mass;
    const double stationPerMass = gotMass > 0.0 ? (moneyBefore - g.agents[pa].money) / gotMass : 0.0;

    char buf[200];
    std::snprintf(buf, sizeof(buf), "сырьё %.2f/масса, станция %.2f/масса (наценка %.0f%%)",
        raw, stationPerMass, stationPerMass > 0.0 ? (stationPerMass / raw - 1.0) * 100.0 : 0.0);
    check(gotMass > 0.0 && stationPerMass > raw * 1.5, "своё топливо выгоднее станционного", buf);
}

void testDriveSlotExclusive() {
    Game g;
    buildWorld(g, 42, 10);
    const int pa = g.playerAgent;
    Ship& ship = g.agents[pa].ship;
    ship.maxModules = 8;
    g.agents[pa].money = 1e9;
    // Верфь высшего уровня, иначе движки просто не поставятся и тест пройдёт
    // вхолостую, ничего не проверив.
    const int star = g.agents[pa].currentStar;
    // Верфь высшего уровня даёт роль системы (shipyardLevelAtStar ищет колонию
    // по полю starIndex, а не по индексу массива).
    if (star >= 0 && star < int(g.cluster.stars.size())) g.cluster.stars[star].economyRole = "shipyard";

    const std::vector<ModuleDef>& defs = moduleDefs();
    int installed = 0;
    int attempted = 0;
    for (size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].slot != ModuleSlot::Drive) continue;
        ++attempted;
        ship.cargo.push_back(Resource("Module: " + defs[i].name, 1.0));
        g.equipModule(pa, int(i));
    }
    for (size_t i = 0; i < ship.modules.size(); ++i) {
        if (defs[ship.modules[i]].slot == ModuleSlot::Drive) ++installed;
    }
    char buf[160];
    std::snprintf(buf, sizeof(buf), "поставлено %d движков подряд, на борту осталось %d",
        attempted, installed);
    // installed == 1, а не <= 1: иначе тест проходит и когда НИЧЕГО не встало.
    check(attempted > 1 && installed == 1, "слот двигателя эксклюзивен", buf);
}

// Перегруз РАЗРЕШЁН: cargoCapacity — паспортная норма, а не физическая стена.
// Залить в трюм можно что угодно и сколько угодно; цена — невозможность взлёта.
// Без этого игрок не мог вылить бак, чтобы сменить топливную схему, и застревал.
void testOverloadAllowedButGrounds() {
    Game g;
    buildWorld(g, 42, 20);
    const int pa = g.playerAgent;
    Ship& ship = g.agents[pa].ship;
    ship.cargoCapacity = 5.0;
    ship.cargo.clear();

    // Сливаем весь бак в заведомо маленький трюм.
    const int prop = shipDominantPropellantElement(ship);
    const double drained = g.agentDrainPropellantToCargo(pa, prop, 1.0e9);
    const double loaded = shipCargoMass(ship);
    const double over = shipCargoOverload(ship);

    // Взлёт при перегрузе запрещён.
    int target = -1;
    for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
        if (int(i) != g.agents[pa].currentStar) { target = int(i); break; }
    }
    const bool blocked = !g.commandAgentToStar(pa, target);

    // Выброс за борт снимает перегруз и возвращает возможность лететь.
    g.agentJettisonCargo(pa, prop, 1.0e9);
    const double afterJettison = shipCargoOverload(ship);

    char buf[220];
    std::snprintf(buf, sizeof(buf), "слито %.0f ед. => трюм %.0f/%.0f (перегруз +%.0f), взлёт %s, после сброса %.0f",
        drained, loaded, ship.cargoCapacity, over,
        blocked ? "заблокирован" : "РАЗРЕШЁН", afterJettison);
    check(drained > 0.0 && over > 0.0 && blocked && afterJettison <= 0.001,
          "перегруз разрешён, но не даёт взлететь", buf);
}

// Зола ложится в трюм ЦЕЛИКОМ, даже сверх нормы: она заменяет сгоревшее
// топливо, поэтому масса корабля не растёт и перегруз в полёте не запирает.
void testAshIgnoresHoldLimit() {
    Game g;
    buildWorld(g, 42, 10);
    Ship& ship = g.agents[g.playerAgent].ship;
    ship.cargo.clear();
    ship.cargoCapacity = 0.5;   // трюм заведомо меньше будущей золы

    std::vector<Resource> ash;
    const double burned = shipFuelMix(ship).mass;
    shipConsumeForDeltaV(ship, 0.05, &ash);
    double landed = 0.0;
    for (size_t i = 0; i < ash.size(); ++i) landed += ash[i].amount * resourceUnitMass(ash[i].element);

    char buf[200];
    std::snprintf(buf, sizeof(buf), "трюм %.1f, сожжено %.1f массы, золы получено %.1f",
        ship.cargoCapacity, burned - shipFuelMix(ship).mass, landed);
    check(landed > ship.cargoCapacity, "зола не режется вместимостью трюма", buf);
}


// Ручка режима двигателя: 0 — платим рабочим телом, 0.5 — ценовой оптимум,
// 1 — платим топливом. Проверяем, что размен монотонный в обе стороны, иначе
// ручка декоративна.
void testThrottleTradesPropellantForFuel() {
    Game g;
    buildWorld(g, 42, 10);
    Ship ship = g.agents[g.playerAgent].ship;

    ship.throttle = 0.0;
    const RouteCost bulk = shipRouteCost(ship, 0.4, 1.0, 1.0);
    ship.throttle = 0.5;
    const RouteCost mid = shipRouteCost(ship, 0.4, 1.0, 1.0);
    ship.throttle = 1.0;
    const RouteCost burn = shipRouteCost(ship, 0.4, 1.0, 1.0);

    char buf[240];
    std::snprintf(buf, sizeof(buf), "0.0: ve %.4f prop %.0f | 0.5: ve %.4f prop %.0f fuel %.0f | 1.0: ve %.4f fuel %.0f",
        bulk.exhaustVelocity, bulk.propellantMass,
        mid.exhaustVelocity, mid.propellantMass, mid.fuelMass,
        burn.exhaustVelocity, burn.fuelMass);
    // Скорость истечения растёт слева направо; рабочего тела становится меньше,
    // топлива на форсаже — больше, чем в оптимуме.
    const bool ok = bulk.feasible && mid.feasible && burn.feasible &&
                    bulk.exhaustVelocity < mid.exhaustVelocity &&
                    mid.exhaustVelocity < burn.exhaustVelocity &&
                    bulk.propellantMass > mid.propellantMass &&
                    mid.propellantMass > burn.propellantMass &&
                    burn.fuelMass > mid.fuelMass;
    check(ok, "ручка меняет рабочее тело на топливо", buf);
}

// Середина ручки обязана быть ценовым оптимумом СРЕДИ ВЛЕЗАЮЩИХ В БАКИ
// режимов — это её точка отсчёта. Глобально дешевле бывает, но если под режим
// негде возить рабочее тело, он бесполезен, и оптимизатор его не выбирает.
// Если середина съедет, игрок по умолчанию полетит не оптимально и не узнает.
void testThrottleMidpointIsCostOptimum() {
    Game g;
    buildWorld(g, 42, 10);
    Ship ship = g.agents[g.playerAgent].ship;

    const double propPrice = 3.0;
    const double fuelPrice = 40.0;
    const MixSummary pm = shipPropellantMix(ship);
    const MixSummary fm = shipFuelMix(ship);
    const double propVolPerMass = pm.mass > 0.0 ? pm.volume / pm.mass : 1.0;
    const double fuelVolPerMass = fm.mass > 0.0 ? fm.volume / fm.mass : 1.0;

    ship.throttle = 0.5;
    const RouteCost mid = shipRouteCost(ship, 0.4, propPrice, fuelPrice);
    const double midCost = mid.propellantMass * propPrice + mid.fuelMass * fuelPrice;

    double best = midCost;
    double bestT = 0.5;
    int fitting = 0;
    for (int i = 0; i <= 40; ++i) {
        ship.throttle = double(i) / 40.0;
        const RouteCost r = shipRouteCost(ship, 0.4, propPrice, fuelPrice);
        if (!r.feasible) continue;
        // Считаем только те режимы, под которые реально есть ёмкости.
        if (r.propellantMass * propVolPerMass > ship.propellantVolume) continue;
        if (r.fuelMass * fuelVolPerMass > shipFuelTankVolume(ship)) continue;
        ++fitting;
        const double c = r.propellantMass * propPrice + r.fuelMass * fuelPrice;
        if (c < best) { best = c; bestT = ship.throttle; }
    }
    char buf[220];
    std::snprintf(buf, sizeof(buf), "середина %.0f Cr, лучшее из %d влезающих %.0f Cr при t=%.2f",
        midCost, fitting, best, bestT);
    check(fitting > 1 && best >= midCost * 0.97, "середина ручки — оптимум среди влезающих", buf);
}

} // namespace

int main() {
    std::printf("=== РЕГРЕССИЯ БАЛАНСА ===\n\n");
    testSlippageExists();
    testRunProfitSane();
    testBoardHonest();
    testStarterAlwaysTradeable();
    testCargoPaysOff();
    testSurveyPaysOff();
    testPriceLadderContinuous();
    testStarterHullIsAClass();
    testQuotaReachable();
    testCargoToTankLoop();
    testAnyElementLoads();
    testBallastWeakensMix();
    testBurnedFuelBecomesAsh();
    testFusionFissionTradeoff();
    testIronIsDead();
    testSelfFuelBeatsStation();
    testOverloadAllowedButGrounds();
    testAshIgnoresHoldLimit();
    testThrottleTradesPropellantForFuel();
    testThrottleMidpointIsCostOptimum();
    testDriveSlotExclusive();
    std::printf("\n%s (%d failures)\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures == 0 ? 0 : 1;
}
