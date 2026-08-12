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
#include "local.h"
#include "exotic.h"
#include "i18n.h"
#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Определена в game.cpp; здесь нужна, чтобы проверить, что запечённые бонусы
// гибнут вместе с корпусом.
void downgradeAgentToEscapePod(Agent&);

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

    // КУШ РАЗРЕШЁН, НО ОБЯЗАН ВЫДЫХАТЬСЯ С РОСТОМ КАПИТАЛА.
    //
    // Решение пользователя (§17): мелкий торговец, нашедший выгодную сделку, —
    // молодец, множитель в разы это нормально, кредитов у него всё равно мало.
    // Плохо не «много за рейс», а «много и ДАЛЬШЕ много, когда денег уже гора».
    //
    // ⚠️ Прежняя версия этой проверки (и пробники к ней) делили прибыль на
    // кошелёк, НЕ ВЫДАВАЯ игроку этот кошелёк: он оставался со стартовыми
    // 100 Cr, и все числа были шумом. Здесь кошелёк выставляется явно перед
    // каждым замером — иначе проверка меряет пустоту.
    //
    // Инвариант безразмерный и переживает любой пересчёт масштаба (§17.5):
    // отдача падает по всей лестнице кошельков, на бедном конце куш есть, на
    // богатом его нет.
    const double purses[] = {400.0, 3600.0, 30000.0, 100000.0};
    double ratioAt[4] = {0, 0, 0, 0};
    double jackpotAt[4] = {0, 0, 0, 0};
    {
        Game g; buildWorld(g, 42u, 80);
        for (int p = 0; p < 4; ++p) {
            std::vector<double> v;
            for (int o = 0; o < 60; ++o) {
                const int origin = (o * 7919) % 80;
                g.agents[g.playerAgent].money = purses[p];
                const std::vector<ArbitrageDeal> board = g.playerArbitrageBoard(origin, 40, -1);
                double best = 0.0;
                for (size_t i = 0; i < board.size(); ++i) best = std::max(best, board[i].profit);
                v.push_back(best / purses[p]);
            }
            std::sort(v.begin(), v.end());
            ratioAt[p] = v[v.size() / 2];
            jackpotAt[p] = double(std::count_if(v.begin(), v.end(),
                                  [](double x) { return x > 5.0; })) / double(v.size());
        }
    }
    const bool falls = ratioAt[0] > ratioAt[1] && ratioAt[1] > ratioAt[2] && ratioAt[2] > ratioAt[3];
    const bool jackpotEarly = jackpotAt[0] > 0.10;      // на бедном конце куш ЕСТЬ
    const bool jackpotGoneLate = jackpotAt[3] < 0.02;   // на богатом его НЕТ
    const bool tamedLate = ratioAt[3] < 1.5;            // капитал сам себя не удваивает

    char buf[240];
    std::snprintf(buf, sizeof(buf),
                  "400Cr %.1Fx(%.0F%%) 3.6KCr %.1Fx(%.0F%%) 30KCr %.1Fx(%.0F%%) 100KCr %.2Fx(%.0F%%)",
                  ratioAt[0], jackpotAt[0] * 100.0, ratioAt[1], jackpotAt[1] * 100.0,
                  ratioAt[2], jackpotAt[2] * 100.0, ratioAt[3], jackpotAt[3] * 100.0);
    check(falls && jackpotEarly && jackpotGoneLate && tamedLate,
          "куш есть у бедного и выдыхается у богатого", buf);

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
// Квота — главный таймер игры, и у неё две стороны отказа. Слишком мала — берётся
// за пару рейсов и лежит мёртвым грузом три четверти тысячелетия. Слишком велика —
// первый период кончается заморозкой торговли при любой игре.
//
// Планка поднята до 10 000 (была 1 000). Замер по трём сидам при игре «как живой
// игрок», то есть возя в радиусе ~8 ly стартовым Hauler-ом: ~105 Cr тарифа за рейс
// при ~47 годах на рейс. Значит 1 000 закрывалась за ~440 лет — меньше половины
// периода, дальше игрок просто коптил небо. 10 000 в одиночку стартовым корпусом за
// период НЕ берутся: первое тысячелетие честно кончается доплатой кредитами
// (`playerSettleQuota`, 1.5× недобора) — к этому моменту капитал уже десятки тысяч,
// так что это ощутимый укус, а не стена. Со второго корпуса пропускная способность
// растёт на порядок, и квота снова берётся оборотом.
//
// Границы здесь широкие: ловим ПОЛОМКУ МЕХАНИЗМА (квота стала тривиальной либо
// недостижимой в принципе), а не точное число.
void testQuotaReachable() {
    Game g; buildWorld(g, 42, 120);
    ArbitrageDeal d; bool ok = false;
    const double before = g.licenceQuotaPaid;
    executeTopDeal(g, d, ok);
    const double tariff = g.licenceQuotaPaid - before;
    const double target = g.licenceQuotaTarget();
    const double runs = tariff > 0.0 ? target / tariff : 1e9;
    // Доплата кредитами обязана оставаться выходом: если она уходит в космос,
    // провал квоты превращается в непроходимую стену.
    g.licenceQuotaPaid = 0.0;
    const double settle = g.licenceSettleCost();
    char buf[200];
    std::snprintf(buf, sizeof(buf), "квота %.0f, тариф с рейса %.0f => рейсов %.1f, доплата %.0f Cr",
        target, tariff, runs, settle);
    // Доплата меряется НЕ в кредитах (они уплывают вместе с масштабом мира),
    // а в рейсах валовой выручки: столько рейсов надо было бы отторговать,
    // чтобы её покрыть. Величина безразмерная и переживает любой пересчёт §17.
    const double grossPerRun = tariff / std::max(1e-9, g.licenceTariffRate);
    const double settleRuns = settle / std::max(1e-9, grossPerRun);
    std::snprintf(buf, sizeof(buf), "квота %.0f, тариф с рейса %.0f => рейсов %.1f, доплата %.0f Cr (%.1f рейса выручки)",
        target, tariff, runs, settle, settleRuns);
    check(tariff > 0.0 && runs > 20.0 && runs < 250.0 && settleRuns < 50.0,
          "квота закрывается за разумное число рейсов", buf);
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

    // Ручка действует через перенастройку: cruiseExhaust — ЗАФИКСИРОВАННАЯ
    // рабочая точка, и менять throttle без shipTuneDrive бессмысленно. Именно
    // так это делает и игра (agentSetThrottle перенастраивает движок).
    ship.throttle = 0.0;
    shipTuneDrive(ship, 1.0, 1.0);
    const RouteCost bulk = shipRouteCost(ship, 0.4, 1.0, 1.0);
    ship.throttle = 0.5;
    shipTuneDrive(ship, 1.0, 1.0);
    const RouteCost mid = shipRouteCost(ship, 0.4, 1.0, 1.0);
    ship.throttle = 1.0;
    shipTuneDrive(ship, 1.0, 1.0);
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
// режимов на НОМИНАЛЬНОМ манёвре корабля (разгон до своего потолка скорости и
// торможение). Именно на нём определена рабочая точка двигателя: ve намеренно
// не подстраивается под длину конкретного рейса, иначе режим менялся бы прямо
// в полёте и расход не сходился бы с оценкой.
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

    const double nominal = ship.speed * 2.0;
    ship.throttle = 0.5;
    shipTuneDrive(ship, propPrice, fuelPrice);
    const RouteCost mid = shipRouteCost(ship, nominal, propPrice, fuelPrice);
    const double midCost = mid.propellantMass * propPrice + mid.fuelMass * fuelPrice;

    double best = midCost;
    double bestT = 0.5;
    int fitting = 0;
    for (int i = 0; i <= 40; ++i) {
        ship.throttle = double(i) / 40.0;
        shipTuneDrive(ship, propPrice, fuelPrice);
        const RouteCost r = shipRouteCost(ship, nominal, propPrice, fuelPrice);
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


// Прогноз маршрута обязан сходиться с ФАКТИЧЕСКИМ расходом в полёте. Именно
// отсутствие этой проверки скрыло два бага сразу:
//   1) Циолковский применялся по-тиковый от постоянной «сухой» массы, а не от
//      текущей; сумма sum(e^xi - 1) вместо e^(sum xi) - 1 занижала расход в 6 раз;
//   2) скорость истечения переоптимизировалась под dV КАЖДОГО тика, поэтому
//      режим двигателя менялся посреди рейса.
// Заливаем ровно расчётное количество и смотрим, что корабль долетает и
// сжигает примерно столько, сколько обещано.
void testRouteEstimateMatchesFlight() {
    Game g;
    buildWorld(g, 42, 12);
    const int pa = g.playerAgent;
    const int home = g.agents[pa].currentStar;

    int target = -1;
    double bestDist = 0.0;
    for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
        if (int(i) == home) continue;
        const double d = g.routeDistance(home, int(i));
        if (d > bestDist && d < 12.0) { bestDist = d; target = int(i); }
    }
    if (target < 0) { check(false, "прогноз маршрута сходится с полётом", "нет цели"); return; }

    Ship& ship = g.agents[pa].ship;
    const int fuelElem = shipDominantFuelElement(ship);
    const int propElem = shipDominantPropellantElement(ship);
    const RouteCost est = g.agentRouteCost(pa, target);
    if (!est.feasible) { check(false, "прогноз маршрута сходится с полётом", "маршрут недостижим"); return; }

    // Ёмкости под расчётную заправку, груз пуст (зола будет копиться в нём).
    ship.cargoCapacity = 1.0e5;
    ship.fuelVolume = 1.0e5;
    ship.propellantVolume = 1.0e5;
    ship.cargo.clear();
    ship.fuel.clear();
    ship.propellant.clear();
    // Запас сверх прогноза: вылет перенастраивает движок по ценам порта, а
    // залитое вещество само меняет массу и, значит, оптимум. Точной подгонки
    // «залить ровно по расчёту» тут быть не может — тест меряет ОТНОШЕНИЕ
    // факта к прогнозу, а не умение попасть в ноль.
    ship.fuel.push_back(Resource(elementDefinitions()[fuelElem].symbol,
                                 1.6 * est.fuelMass / elementUnitMass(fuelElem)));
    ship.propellant.push_back(Resource(elementDefinitions()[propElem].symbol,
                                       1.6 * est.propellantMass / elementUnitMass(propElem)));
    g.agents[pa].money = 0.0;   // чтобы не докупал по пути

    // Прогноз пересчитываем на ФАКТИЧЕСКОЕ состояние перед вылетом: первая
    // оценка делалась для другой загрузки и сравнивать с ней нечестно.
    RouteCost plan = g.agentRouteCost(pa, target);
    double fuel0 = shipFuelMix(ship).mass;
    double prop0 = shipPropellantMix(ship).mass;
    if (!g.commandAgentToStar(pa, target)) {
        // Первая попытка ФИКСИРУЕТ рабочую точку двигателя по ценам порта
        // (§12.4), и только после неё требуемое количество вещества становится
        // окончательным. Игрок в этой ситуации делает ровно то же: видит
        // честную цифру нехватки и доливает по ней. Дозаправляемся с тем же
        // запасом и пробуем ещё раз — если и теперь отказ, это уже дефект.
        const RouteCost after = g.agentRouteCost(pa, target);
        ship.fuel.clear();
        ship.propellant.clear();
        ship.fuel.push_back(Resource(elementDefinitions()[fuelElem].symbol,
                                     1.6 * after.fuelMass / elementUnitMass(fuelElem)));
        ship.propellant.push_back(Resource(elementDefinitions()[propElem].symbol,
                                           1.6 * after.propellantMass / elementUnitMass(propElem)));
        if (!g.commandAgentToStar(pa, target)) {
            char why[160];
            std::snprintf(why, sizeof(why), "вылет отклонён дважды: %s", g.lastEvent.c_str());
            check(false, "прогноз маршрута сходится с полётом", why);
            return;
        }
    }
    // Точка зафиксирована вылетом — перечитываем прогноз и стартовые запасы по ней.
    plan = g.agentRouteCost(pa, target);
    fuel0 = shipFuelMix(ship).mass;
    prop0 = shipPropellantMix(ship).mass;
    int steps = 0;
    while (g.agents[pa].ship.enRoute && steps < 400000) { g.update(0.02); ++steps; }

    const Ship& q = g.agents[pa].ship;
    const bool arrived = !q.enRoute;
    const double burnedProp = prop0 - shipPropellantMix(q).mass;
    const double burnedFuel = fuel0 - shipFuelMix(q).mass;
    const double propRatio = plan.propellantMass > 0.0 ? burnedProp / plan.propellantMass : 1.0;
    const double fuelRatio = plan.fuelMass > 0.0 ? burnedFuel / plan.fuelMass : 1.0;

    char buf[220];
    std::snprintf(buf, sizeof(buf), "%.1f ly: рабтело %.0f%% от прогноза, топливо %.0f%%, долетел %s",
        bestDist, propRatio * 100.0, fuelRatio * 100.0, arrived ? "да" : "НЕТ");
    // Полтора конца допуска: прогноз строится на массе в момент вылета, а по
    // дороге корабль тяжелеет от золы, поэтому точного равенства не бывает.
    check(arrived && propRatio > 0.5 && propRatio < 1.5 && fuelRatio > 0.5 && fuelRatio < 1.5,
          "прогноз маршрута сходится с полётом", buf);
}


// Лететь медленнее объективно дешевле: бюджет быстроты равен 2*artanh(peak),
// поэтому снижение крейсера прямо режет расход обоих расходников. Раньше этот
// размен игроку был недоступен — корабль всегда шёл на полном потолке.
void testSlowCruiseIsCheaper() {
    Game g;
    buildWorld(g, 42, 10);
    Ship ship = g.agents[g.playerAgent].ship;

    ship.cruiseFraction = 1.0;
    shipTuneDrive(ship, 1.0, 1.0);
    const RouteCost fast = shipEstimateRoute(ship, 8.0, 1.0, 1.0);
    ship.cruiseFraction = 0.4;
    shipTuneDrive(ship, 1.0, 1.0);
    const RouteCost slow = shipEstimateRoute(ship, 8.0, 1.0, 1.0);

    char buf[220];
    std::snprintf(buf, sizeof(buf), "100%%: W %.1f F %.1f | 40%%: W %.1f F %.1f",
        fast.propellantMass, fast.fuelMass, slow.propellantMass, slow.fuelMass);
    check(fast.feasible && slow.feasible &&
          slow.propellantMass < fast.propellantMass && slow.fuelMass < fast.fuelMass,
          "медленный крейсер дешевле быстрого", buf);
}

// Кнопка OPTIMAL обязана находить связку не хуже той, что стояла до неё,
// иначе она бесполезна.
void testOptimalBeatsDefaults() {
    Game g;
    buildWorld(g, 42, 12);
    const int pa = g.playerAgent;
    const int home = g.agents[pa].currentStar;
    int target = -1;
    double bestDist = 0.0;
    for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
        if (int(i) == home) continue;
        const double d = g.routeDistance(home, int(i));
        if (d > bestDist && d < 10.0) { bestDist = d; target = int(i); }
    }
    if (target < 0) { check(false, "OPTIMAL подбирает режим под цель", "нет цели"); return; }

    double propPrice = 1.0, fuelPrice = 1.0;
    Ship& ship = g.agents[pa].ship;
    ship.throttle = 0.9;
    ship.cruiseFraction = 1.0;
    g.agentSetThrottle(pa, 0.9);
    const RouteCost before = g.agentRouteCost(pa, target);
    const double costBefore = before.feasible
        ? before.propellantMass + before.fuelMass * 40.0 : 1e18;

    const bool ok = g.agentOptimiseForTarget(pa, target);
    const RouteCost after = g.agentRouteCost(pa, target);
    const double costAfter = after.feasible
        ? after.propellantMass + after.fuelMass * 40.0 : 1e18;
    (void)propPrice; (void)fuelPrice;

    char buf[220];
    std::snprintf(buf, sizeof(buf), "было t=0.90 cruise=100%% => %.0f, стало t=%.2f cruise=%.0f%% => %.0f",
        costBefore, ship.throttle, ship.cruiseFraction * 100.0, costAfter);
    check(ok && after.feasible && costAfter <= costBefore * 1.001,
          "OPTIMAL подбирает режим под цель", buf);
}

// Релятивизм: складывается БЫСТРОТА, а не скорость. На малых скоростях это
// одно и то же, у светового предела — расходится. Без этого бюджет «разгон до
// 0.5c плюс торможение» дал бы ровно 1.0c, величину несуществующую.
void testRapidityDivergesNearLight() {
    Game g;
    buildWorld(g, 42, 6);
    Ship ship = g.agents[g.playerAgent].ship;

    // Один и тот же корпус на разных потолках скорости: во сколько раз бюджет
    // быстроты обгоняет наивную сумму скоростей.
    ship.speed = 0.05;
    ship.cruiseFraction = 1.0;
    const RouteCost slow = shipEstimateRoute(ship, 500.0, 1.0, 1.0);
    ship.speed = 0.5;
    const RouteCost fast = shipEstimateRoute(ship, 500.0, 1.0, 1.0);

    // artanh(0.05)/0.05 = 1.0008, artanh(0.5)/0.5 = 1.0986
    const double lowFactor = std::atanh(0.05) / 0.05;
    const double highFactor = std::atanh(0.5) / 0.5;
    char buf[220];
    std::snprintf(buf, sizeof(buf), "надбавка быстроты: при 0.05c +%.2f%%, при 0.5c +%.1f%%",
        (lowFactor - 1.0) * 100.0, (highFactor - 1.0) * 100.0);
    check(slow.feasible && fast.feasible && lowFactor < 1.01 && highFactor > 1.05,
          "быстрота расходится у светового предела", buf);
}


// Локальный полёт (клавиша L) обязан быть БЕСПЛАТНЫМ: внутрисистемные манёвры
// топливо не жгут, оно тратится только на межзвёздные плечи. Сейчас это верно
// «по построению» — local*.cpp вообще не знает про баки, — но именно поэтому
// проверка и нужна: молча сломать такую гарантию проще всего.
void testDockedAndLocalFlightAreFree() {
    Game g;
    buildWorld(g, 42, 8);
    const int pa = g.playerAgent;
    Ship& ship = g.agents[pa].ship;
    ship.enRoute = false;
    ship.targetStar = -1;
    ship.vx = ship.vy = ship.vz = 0.0;

    const double fuel0 = shipFuelMix(ship).mass;
    const double prop0 = shipPropellantMix(ship).mass;
    for (int i = 0; i < 400; ++i) g.update(0.25);   // сто лет на стоянке
    const double fuel1 = shipFuelMix(g.agents[pa].ship).mass;
    const double prop1 = shipPropellantMix(g.agents[pa].ship).mass;

    char buf[200];
    std::snprintf(buf, sizeof(buf), "за 100 лет в системе: топливо %.3f -> %.3f, рабтело %.3f -> %.3f",
        fuel0, fuel1, prop0, prop1);
    check(std::fabs(fuel1 - fuel0) < 1e-9 && std::fabs(prop1 - prop0) < 1e-9,
          "полёт внутри системы бесплатен", buf);
}

// Лестница корпусов по скорости: стартовый около 0.12c, топовый около 0.5c,
// и она МОНОТОННА по ступени цены. Скорость выводится из полей самой таблицы
// (цена и тяговооружённость), поэтому разъехаться при правке класса не может.
void testSpeedLadderIsSmooth() {
    const std::vector<ShipClass>& classes = shipClasses();
    std::vector<double> speeds;
    double starter = 0.0;
    for (size_t i = 0; i < classes.size(); ++i) {
        const double v = shipClassMaxSpeed(classes[i]);
        speeds.push_back(v);
        if (classes[i].name == "Hauler") starter = v;
    }
    std::sort(speeds.begin(), speeds.end());

    // Дыра меряется по ОТСОРТИРОВАННОМУ спектру, а не по порядку в таблице:
    // таблица чередует роли, и «скачок» между грузовозом и линкором — это не
    // дыра, а разброс. Дыра — это когда расти по скорости приходится прыжком,
    // потому что корпуса на промежуточную скорость просто не существует.
    double worstGap = 0.0;
    double gapAt = 0.0;
    for (size_t i = 1; i < speeds.size(); ++i) {
        if (speeds[i] - speeds[i - 1] > worstGap) {
            worstGap = speeds[i] - speeds[i - 1];
            gapAt = speeds[i - 1];
        }
    }
    char buf[220];
    std::snprintf(buf, sizeof(buf), "стартовый %.3fc, топовый %.3fc, худшая дыра в спектре %.3fc (на %.3fc)",
        starter, speeds.back(), worstGap, gapAt);
    check(starter > 0.10 && starter < 0.15 && speeds.back() > 0.45 && speeds.back() <= 0.5 &&
          worstGap < 0.07,
          "лестница скоростей гладкая", buf);
}


// Экстренная остановка (STOP) — это ТОРМОЖЕНИЕ той же моделью, а не телепорт
// в ноль: корабль гасит скорость своей же тягой, честно жжёт оба расходника и
// по инерции пролетает заметное расстояние. А встав между систем, он больше
// НИ К ОДНОЙ не пристыкован — иначе маршруты считались бы от порта вылета
// (замер: 9.6 ly вместо реальных 3.6), а с рынком за световые годы можно было
// бы торговать прямо из пустоты.
void testEmergencyStopIsPhysical() {
    Game g;
    buildWorld(g, 42, 12);
    const int pa = g.playerAgent;
    const int home = g.agents[pa].currentStar;
    int target = -1;
    double bestDist = 0.0;
    for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
        if (int(i) == home) continue;
        const double d = g.routeDistance(home, int(i));
        if (d > bestDist && d < 10.0) { bestDist = d; target = int(i); }
    }
    if (target < 0) { check(false, "экстренная остановка физична", "нет цели"); return; }

    Ship& ship = g.agents[pa].ship;
    ship.fuelVolume *= 8.0;
    ship.propellantVolume *= 8.0;
    ship.fuel.clear();
    ship.propellant.clear();
    const int fe = shipDominantFuelElement(ship);
    const int pe = shipDominantPropellantElement(ship);
    ship.fuel.push_back(Resource(elementDefinitions()[fe].symbol,
                                 ship.fuelVolume / elementUnitVolume(fe)));
    ship.propellant.push_back(Resource(elementDefinitions()[pe].symbol,
                                       ship.propellantVolume / elementUnitVolume(pe)));

    g.commandAgentToStar(pa, target);
    for (int i = 0; i < 200 && g.agents[pa].ship.enRoute; ++i) g.update(0.25);

    const double fuel0 = shipFuelMix(ship).mass;
    const double prop0 = shipPropellantMix(ship).mass;
    const double x0 = ship.x, y0 = ship.y, z0 = ship.z;
    const double t0 = g.time;
    g.abortAgentRoute(pa);
    // Одного тика заведомо мало: если корабль встал сразу, торможения нет.
    g.update(0.25);
    const bool stillMoving = g.agents[pa].ship.enRoute;
    int ticks = 0;
    while (g.agents[pa].ship.enRoute && ticks < 20000) { g.update(0.05); ++ticks; }

    const double coasted = std::sqrt((ship.x - x0) * (ship.x - x0) +
                                     (ship.y - y0) * (ship.y - y0) +
                                     (ship.z - z0) * (ship.z - z0));
    const double burnedFuel = fuel0 - shipFuelMix(ship).mass;
    const double burnedProp = prop0 - shipPropellantMix(ship).mass;

    char buf[240];
    std::snprintf(buf, sizeof(buf), "остановка %.1f года, по инерции %.2f ly, сожжено %.1f топлива и %.1f рабтела",
        g.time - t0, coasted, burnedFuel, burnedProp);
    check(stillMoving && g.time - t0 > 1.0 && coasted > 0.05 &&
          burnedFuel > 0.0 && burnedProp > 0.0,
          "экстренная остановка физична", buf);
}

// После остановки между систем перемещение обязано считаться от КООРДИНАТ
// корабля, а не от звезды, к которой он формально приписан, и торговать из
// пустоты нельзя. Улететь при этом можно к любой системе.
void testAdriftShipRoutesByCoordinates() {
    Game g;
    buildWorld(g, 42, 12);
    const int pa = g.playerAgent;
    const int home = g.agents[pa].currentStar;
    int target = -1;
    double bestDist = 0.0;
    for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
        if (int(i) == home) continue;
        const double d = g.routeDistance(home, int(i));
        if (d > bestDist && d < 10.0) { bestDist = d; target = int(i); }
    }
    if (target < 0) { check(false, "дрейф считается по координатам", "нет цели"); return; }

    Ship& ship = g.agents[pa].ship;
    ship.fuelVolume *= 8.0;
    ship.propellantVolume *= 8.0;
    ship.fuel.clear();
    ship.propellant.clear();
    const int fe = shipDominantFuelElement(ship);
    const int pe = shipDominantPropellantElement(ship);
    ship.fuel.push_back(Resource(elementDefinitions()[fe].symbol,
                                 ship.fuelVolume / elementUnitVolume(fe)));
    ship.propellant.push_back(Resource(elementDefinitions()[pe].symbol,
                                       ship.propellantVolume / elementUnitVolume(pe)));

    g.commandAgentToStar(pa, target);
    for (int i = 0; i < 200 && g.agents[pa].ship.enRoute; ++i) g.update(0.25);
    g.abortAgentRoute(pa);
    int ticks = 0;
    while (g.agents[pa].ship.enRoute && ticks < 20000) { g.update(0.05); ++ticks; }

    const bool undocked = g.agents[pa].currentStar < 0;
    const bool tradeBlocked = !g.agentBuyElementAmount(pa, elementIndex("Fe"), 1.0);

    // Дистанция до цели: по факту от корабля, а не от порта вылета.
    const ClusterStar& T = g.cluster.stars[target];
    const ClusterStar& H = g.cluster.stars[home];
    const double fromShip = std::sqrt((ship.x - T.x) * (ship.x - T.x) +
                                      (ship.y - T.y) * (ship.y - T.y) +
                                      (ship.z - T.z) * (ship.z - T.z));
    const double fromHome = std::sqrt((H.x - T.x) * (H.x - T.x) +
                                      (H.y - T.y) * (H.y - T.y) +
                                      (H.z - T.z) * (H.z - T.z));
    const double reported = g.agentRouteDistance(pa, target);

    // И из пустоты можно улететь к произвольной системе, а не только к «своей».
    int other = -1;
    double nearest = 1e18;
    for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
        if (int(i) == target) continue;
        const ClusterStar& C = g.cluster.stars[i];
        const double d = std::sqrt((ship.x - C.x) * (ship.x - C.x) +
                                   (ship.y - C.y) * (ship.y - C.y) +
                                   (ship.z - C.z) * (ship.z - C.z));
        if (d < nearest) { nearest = d; other = int(i); }
    }
    const bool departed = other >= 0 && g.commandAgentToStar(pa, other);
    int t2 = 0;
    while (g.agents[pa].ship.enRoute && t2 < 40000) { g.update(0.05); ++t2; }
    const bool arrived = g.agents[pa].currentStar == other;

    char buf[240];
    std::snprintf(buf, sizeof(buf), "от корабля %.2f ly (от порта было бы %.2f), API даёт %.2f; вылет %s, стыковка %s",
        fromShip, fromHome, reported, departed ? "ок" : "ОТКАЗ", arrived ? "ок" : "НЕТ");
    check(undocked && tradeBlocked && std::fabs(reported - fromShip) < 0.05 &&
          departed && arrived,
          "дрейф считается по координатам", buf);
}

// --------------------------------------------------------- СОБСТВЕННОСТЬ --
// Цена системы — произведение шести множителей, и любая правка любого из них
// сдвигает ПОРЯДОК итога. Здесь заморожен именно порядок: медиана по скоплению
// должна остаться миллиардом (тир капитальных кораблей, §SYSTEM_PRICE_*), а не
// уехать в миллионы (система покупается между рейсами) или в триллионы
// (недостижимая цель). Пороги нарочно широкие — ловим поломку механизма.
void testSystemPriceOrder() {
    double medians[3] = {0.0, 0.0, 0.0};
    double lo = 1e300, hi = 0.0;
    bool monotone = true;
    for (int s = 0; s < 3; ++s) {
        Game g;
        buildWorld(g, unsigned(11 + s * 7), 0);
        std::vector<double> prices;
        for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
            const SystemPrice p = g.systemPrice(int(i));
            // Каждый множитель обязан быть >= 1: он отвечает «во сколько раз
            // система ЛУЧШЕ пустого камня», а не «во сколько раз хуже».
            if (p.population < 1.0 || p.industry < 1.0 || p.habitability < 1.0 ||
                p.resources < 1.0 || p.development < 1.0 || p.sovereignty < 1.0) monotone = false;
            prices.push_back(p.total);
        }
        std::sort(prices.begin(), prices.end());
        medians[s] = prices[prices.size() / 2];
        lo = std::min(lo, prices.front());
        hi = std::max(hi, prices.back());
    }
    const double median = (medians[0] + medians[1] + medians[2]) / 3.0;
    // Разброс обязан быть: если богатая система стоит как пустая, множители не работают.
    const bool spread = hi > lo * 8.0;
    char buf[240];
    std::snprintf(buf, sizeof(buf), "медиана %.3g Cr (нужно 3e8..4e9), разброс %.3g..%.3g",
        median, lo, hi);
    check(median > 3.0e8 && median < 4.0e9 && spread && monotone,
          "система стоит порядка миллиарда", buf);
}

// Покупка = смена владельца и перевод денег продавцу. Ни один кредит не должен
// ни исчезнуть, ни родиться: сумма «игрок + казна продавца» сохраняется.
void testBuySystemMovesMoneyAndOwner() {
    Game g;
    buildWorld(g, 42, 4);
    const int pa = g.playerAgent;
    const int home = g.agents[pa].currentStar;
    // Берём чужую систему: своя стартовая уже наша и продаваться не должна.
    int target = -1;
    for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
        if (g.cluster.stars[i].ownerFaction >= 0 && !g.playerOwnsStar(int(i))) { target = int(i); break; }
    }
    if (target < 0) { check(false, "покупка системы двигает деньги и владельца", "нет чужой системы"); return; }

    // Переставляем игрока в целевую систему: сделка требует стоянки на месте.
    g.agents[pa].currentStar = target;
    g.agents[pa].ship.enRoute = false;
    const int seller = g.cluster.stars[target].ownerFaction;
    const double price = g.systemPrice(target).total;

    g.agents[pa].money = price * 0.5;
    const bool poorRefused = !g.playerBuySystem();

    g.agents[pa].money = price * 1.5;
    const double sellerBefore = g.factions[seller].treasury;
    const double playerBefore = g.agents[pa].money;
    const bool bought = g.playerBuySystem();
    const double spent = playerBefore - g.agents[pa].money;
    const double gained = g.factions[seller].treasury - sellerBefore;

    const bool ownerChanged = g.playerOwnsStar(target);
    const bool hasColony = g.colonyLedgerAt(target) >= 0.0 && g.playerColonyCount() > 0;
    const bool conserved = std::fabs(spent - gained) < std::max(1.0, price * 1e-9);
    // Купленная система больше не продаётся.
    const bool notTwice = !g.playerBuySystem();

    char buf[240];
    std::snprintf(buf, sizeof(buf), "цена %.3g, списано %.3g, продавцу %.3g; %s%s%s%s",
        price, spent, gained,
        poorRefused ? "" : "БЕДНЫЙ КУПИЛ ", bought ? "" : "ОТКАЗ ",
        ownerChanged ? "" : "ВЛАДЕЛЕЦ НЕ СМЕНИЛСЯ ", notTwice ? "" : "КУПИЛ ДВАЖДЫ ");
    check(poorRefused && bought && ownerChanged && conserved && notTwice && hasColony,
          "покупка системы двигает деньги и владельца", buf);
}

// Свой рынок бесплатен, но НЕ бездонен: цена сделки 0, а запас склада тает и
// цена элемента растёт ровно как при обычной покупке. Без этого дармовой товар
// стал бы бесконечным станком денег через продажу в соседней системе.
void testOwnedMarketIsFreeButFinite() {
    Game g;
    buildWorld(g, 42, 4);
    const int pa = g.playerAgent;
    int target = -1;
    for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
        if (g.cluster.stars[i].ownerFaction >= 0 && !g.playerOwnsStar(int(i))) { target = int(i); break; }
    }
    if (target < 0) { check(false, "свой рынок даром, но конечен", "нет чужой системы"); return; }
    g.agents[pa].currentStar = target;
    g.agents[pa].ship.enRoute = false;
    g.agents[pa].money = g.systemPrice(target).total * 1.2;
    if (!g.playerBuySystem()) { check(false, "свой рынок даром, но конечен", "покупка не прошла"); return; }

    // Самый ходовой элемент этой системы.
    int element = -1;
    double bestSupply = 0.0;
    for (size_t e = 0; e < g.markets[target].supply.size(); ++e) {
        if (g.markets[target].supply[e].amount > bestSupply) {
            bestSupply = g.markets[target].supply[e].amount;
            element = int(e);
        }
    }
    if (element < 0) { check(false, "свой рынок даром, но конечен", "пустой рынок"); return; }

    g.agents[pa].ship.cargo.clear();
    const double moneyBefore = g.agents[pa].money;
    const double supplyBefore = g.markets[target].supply[element].amount;
    const double priceBefore = g.markets[target].prices[element];
    const bool took = g.agentBuyElementAmount(pa, element, 1.0e12);
    const double moneyAfter = g.agents[pa].money;
    const double supplyAfter = g.markets[target].supply[element].amount;
    const double priceAfter = g.markets[target].prices[element];

    const double cargoMass = shipCargoMass(g.agents[pa].ship);
    const bool freeOfCharge = std::fabs(moneyAfter - moneyBefore) < 0.001;
    const bool marketDrained = supplyAfter < supplyBefore - 0.01;
    const bool priceRose = priceAfter > priceBefore * 1.0000001;
    // Дармовой груз всё равно ограничен трюмом, а не размером склада.
    const bool holdRespected = cargoMass <= g.agents[pa].ship.cargoCapacity + 0.01;

    char buf[240];
    std::snprintf(buf, sizeof(buf), "деньги %+.4f, запас %.1f->%.1f, цена %.3f->%.3f, трюм %.1f/%.1f",
        moneyAfter - moneyBefore, supplyBefore, supplyAfter, priceBefore, priceAfter,
        cargoMass, g.agents[pa].ship.cargoCapacity);
    check(took && freeOfCharge && marketDrained && priceRose && holdRespected,
          "свой рынок даром, но конечен", buf);
}

// Касса колонии — перекладывание из кармана в карман: сколько ушло у игрока,
// столько пришло в колонию, и обратно. Ни один кредит не создаётся.
void testColonyVaultConserves() {
    Game g;
    buildWorld(g, 42, 4);
    const int pa = g.playerAgent;
    int target = -1;
    for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
        if (g.cluster.stars[i].ownerFaction >= 0 && !g.playerOwnsStar(int(i))) { target = int(i); break; }
    }
    if (target < 0) { check(false, "касса колонии сохраняет деньги", "нет чужой системы"); return; }
    g.agents[pa].currentStar = target;
    g.agents[pa].ship.enRoute = false;
    g.agents[pa].money = g.systemPrice(target).total * 1.2;
    if (!g.playerBuySystem()) { check(false, "касса колонии сохраняет деньги", "покупка не прошла"); return; }

    g.agents[pa].money = 10000.0;
    const double vaultBefore = g.colonyLedgerAt(target);
    const double put = g.playerColonyDeposit(4000.0);
    const bool deposited = std::fabs(put - 4000.0) < 0.001 &&
                           std::fabs(g.agents[pa].money - 6000.0) < 0.001 &&
                           std::fabs(g.colonyLedgerAt(target) - (vaultBefore + 4000.0)) < 0.001;
    // Забрать больше, чем лежит, нельзя — вернётся ровно остаток.
    const double vaultNow = g.colonyLedgerAt(target);
    const double got = g.playerColonyWithdraw(vaultNow * 10.0);
    const bool withdrawn = std::fabs(got - vaultNow) < 0.001 && g.colonyLedgerAt(target) < 0.001;

    // Вдали от своей системы касса недоступна: империю надо облетать.
    int elsewhere = -1;
    for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
        if (!g.playerOwnsStar(int(i))) { elsewhere = int(i); break; }
    }
    g.agents[pa].currentStar = elsewhere;
    const bool remoteBlocked = g.playerColonyWithdraw(1.0) <= 0.0 && g.playerColonyDeposit(1.0) <= 0.0;

    char buf[240];
    std::snprintf(buf, sizeof(buf), "положено %.1f, снято %.1f, удалённо %s",
        put, got, remoteBlocked ? "закрыто" : "ОТКРЫТО");
    check(deposited && withdrawn && remoteBlocked, "касса колонии сохраняет деньги", buf);
}

// --- Локальная добыча — гроши, а не капитал (§20.1) ------------------------
// Меряет ОДНУ безразмерную величину: во сколько трюмов ОБЫЧНОГО местного товара
// оценивается трюм, набранный в поясе и сданный НЕ СХОДЯ С МЕСТА. Замысел —
// пояс сделан из того же вещества, что и система, значит рынок им уже завален и
// берёт за гроши; чтобы заработать, намайненное надо ВЕЗТИ.
//
// Ловит три разных промаха, каждый из которых уже был: элемент камня выбирался
// равномерно по классу (четверть глыб — дефицит по 2.5 опорной цены); состав
// считался в ЕДИНИЦАХ, а не в массе (пояс из водорода — единиц больше всех, а
// весит единица в сотни раз меньше); буровая снимала одинаковое ЧИСЛО единиц
// независимо от элемента, хотя трюм меряется массой. Замер до правок — 7.37,
// после — 2.32. Порог 4.0 ловит возврат к прежнему режиму, оставляя запас на
// обычный дрейф баланса.
//
// Величина БЕЗРАЗМЕРНА (§17.5 п.2): пересчёт масштаба мира её не сдвинет.
void testBeltIsLocalDirt() {
    Game g;
    buildWorld(g, 42, 0);
    const int ec = int(elementCount());
    const double cap = g.agents[g.playerAgent].ship.cargoCapacity;

    std::vector<double> holdRatio;   // трюм добычи / трюм обычного местного товара
    for (int s = 0; s < int(g.cluster.stars.size()) && int(holdRatio.size()) < 40; s += 29) {
        if (g.cluster.stars[s].population <= 0.0) continue;
        LocalScene scene;
        buildLocalScene(g, s, scene);
        if (scene.rocks.empty()) continue;
        const Market& m = g.markets[s];

        double ore = 0.0;
        for (size_t i = 0; i < scene.rocks.size(); ++i) {
            const int e = scene.rocks[i].element;
            if (e >= 0 && e < ec) ore += scene.rocks[i].ore;
        }
        if (ore <= 0.0) continue;

        // Трюм добычи: берём пояс как он есть (доли по руде) до полной вместимости.
        double mined = 0.0;
        for (int e = 0; e < ec; ++e) {
            double share = 0.0;
            for (size_t i = 0; i < scene.rocks.size(); ++i) {
                if (scene.rocks[i].element == e) share += scene.rocks[i].ore;
            }
            if (share <= 0.0) continue;
            // `rock.ore` — тоннаж, поэтому доля идёт по МАССЕ, а трюм тоже меряется массой.
            const double units = cap * (share / ore) / std::max(0.001, resourceUnitMassByIndex(e));
            mined += units * m.executionPrice(e, units, true);
        }
        // Трюм обычного местного товара — медиана по тому, что реально лежит на складе.
        std::vector<double> perMass;
        for (int e = 0; e < ec; ++e) {
            if (m.supply[size_t(e)].amount < 200.0) continue;
            perMass.push_back(m.prices[e] / std::max(0.001, resourceUnitMassByIndex(e)));
        }
        if (perMass.empty()) continue;
        std::sort(perMass.begin(), perMass.end());
        const double typical = perMass[perMass.size() / 2] * cap;
        if (typical > 0.0) holdRatio.push_back(mined / typical);
    }

    if (holdRatio.empty()) {
        check(false, "локальная добыча — гроши", "выборка пуста");
        return;
    }
    std::sort(holdRatio.begin(), holdRatio.end());
    const double hold = holdRatio[holdRatio.size() / 2];

    char buf[200];
    std::snprintf(buf, sizeof(buf),
        "трюм добычи = %.2f трюма обычного местного товара (было 7.37, нужно <4.0)", hold);
    check(hold < 4.0, "локальная добыча — гроши", buf);
}

// --- Бур жжёт топливо реактора (§20.7) -------------------------------------
// Пояс возрождается полным при каждом входе в локальный полёт (§20.3B), значит
// руда бесконечна и единственная цена добычи — бункер. Проверка держит ДВА
// свойства сразу: с топливом бур даёт руду и уводит бункер вниз; без топлива
// не даёт НИЧЕГО и честно об этом говорит, а не крутит вхолостую. Второе важнее:
// если гейт отвалится, добыча снова станет бесплатной, и это не заметит ни одна
// другая проверка.
void testMiningBurnsReactorFuel() {
    Game g;
    buildWorld(g, 42, 0);
    const int home = g.agents[g.playerAgent].currentStar;

    // Ставим игрока вплотную к первому камню с рудой и включаем бур.
    LocalScene scene;
    buildLocalScene(g, home, scene);
    int rock = -1;
    for (size_t i = 0; i < scene.rocks.size(); ++i) {
        if (scene.rocks[i].ore > 0.0) { rock = int(i); break; }
    }
    if (rock < 0) { check(false, "бур жжёт топливо реактора", "в поясе нет руды"); return; }
    scene.px = scene.rocks[size_t(rock)].x;
    scene.py = scene.rocks[size_t(rock)].y;
    scene.pz = scene.rocks[size_t(rock)].z;
    scene.pvx = scene.pvy = scene.pvz = 0.0;
    scene.miningRock = rock;

    Ship& ps = g.agents[g.playerAgent].ship;
    ps.cargo.clear();
    const double fuelBefore = shipFuelMass(ps);
    LocalInput in;
    for (int f = 0; f < 40 && scene.miningRock >= 0; ++f) updateLocalScene(g, scene, in, 1.0);
    const double mined = shipCargoMass(ps);
    const double burned = fuelBefore - shipFuelMass(ps);

    // Тот же камень с ПУСТЫМ бункером: руды быть не должно вовсе.
    Game dry;
    buildWorld(dry, 42, 0);
    LocalScene dryScene;
    buildLocalScene(dry, home, dryScene);
    dryScene.px = dryScene.rocks[size_t(rock)].x;
    dryScene.py = dryScene.rocks[size_t(rock)].y;
    dryScene.pz = dryScene.rocks[size_t(rock)].z;
    dryScene.pvx = dryScene.pvy = dryScene.pvz = 0.0;
    dryScene.miningRock = rock;
    Ship& ds = dry.agents[dry.playerAgent].ship;
    ds.cargo.clear();
    ds.fuel.clear();
    for (int f = 0; f < 40 && dryScene.miningRock >= 0; ++f) updateLocalScene(dry, dryScene, in, 1.0);
    const double dryMined = shipCargoMass(ds);

    char buf[220];
    std::snprintf(buf, sizeof(buf),
        "с топливом добыто %.2f при расходе %.4f бункера (%.2f%%); с пустым бункером %.4f, бур %s",
        mined, burned, 100.0 * burned / std::max(1e-9, fuelBefore), dryMined,
        dryScene.miningRock < 0 ? "выключен" : "ВСЁ ЕЩЁ ВКЛЮЧЕН");
    check(mined > 0.01 && burned > 0.0 && dryMined <= 0.01 && dryScene.miningRock < 0,
          "бур жжёт топливо реактора", buf);
}

// --- Бур — это реактор, а реакторы разные (§20.9) --------------------------
// Ловит возврат к ПЛОСКОЙ скорости добычи. До §20.9 темп задавала одна
// константа (9 тонн/час), одинаковая для спас-капсулы и для «Фортресса»: ни
// корпус, ни движок на добычу не влияли вовсе. Теперь темп = мощность установки
// / энергия на тонну, поэтому крупный борт с дорогим движком обязан грызть
// камень заметно быстрее. Проверка безразмерна: сравнивает корпуса ДРУГ С
// ДРУГОМ, а не с абсолютным числом тонн.
void testDrillScalesWithReactor() {
    // Сколько намыто за 10 часов на ОДНОМ и том же камне разными корпусами.
    auto minedBy = [](const char* hull, int moduleIndex) {
        Game g;
        buildWorld(g, 42, 0);
        const int home = g.agents[g.playerAgent].currentStar;
        Ship& ps = g.agents[g.playerAgent].ship;
        for (size_t c = 0; c < shipClasses().size(); ++c) {
            if (shipClasses()[c].name == hull) { shipApplyClass(ps, shipClasses()[c]); break; }
        }
        if (moduleIndex >= 0 && moduleIndex < int(moduleDefs().size())) {
            applyModuleToShip(ps, moduleDefs()[size_t(moduleIndex)]);
        }
        ps.cargo.clear();
        LocalScene scene;
        buildLocalScene(g, home, scene);
        int rock = -1;
        for (size_t i = 0; i < scene.rocks.size(); ++i) {
            if (scene.rocks[i].ore > 0.0) { rock = int(i); break; }
        }
        if (rock < 0) return 0.0;
        scene.px = scene.rocks[size_t(rock)].x;
        scene.py = scene.rocks[size_t(rock)].y;
        scene.pz = scene.rocks[size_t(rock)].z;
        scene.pvx = scene.pvy = scene.pvz = 0.0;
        scene.miningRock = rock;
        LocalInput in;
        for (int f = 0; f < 10 && scene.miningRock >= 0; ++f) updateLocalScene(g, scene, in, 1.0);
        return shipCargoMass(ps);
    };

    const double starter = minedBy("Hauler", -1);
    const double heavy   = minedBy("Heavy Freighter", -1);
    const double pod     = minedBy("Escape Pod", -1);
    // Та же капсула с самой дешёвой буровой: установка обязана значить.
    int laser = -1;
    for (size_t i = 0; i < moduleDefs().size(); ++i) {
        if (moduleDefs()[i].name == "Mining Laser I") { laser = int(i); break; }
    }
    const double podRig = laser >= 0 ? minedBy("Escape Pod", laser) : 0.0;

    char buf[260];
    std::snprintf(buf, sizeof(buf),
        "за 10 ч: капсула %.2f (с буром %.2f) < Hauler %.2f < Heavy Freighter %.2f (x%.1f)",
        pod, podRig, starter, heavy, starter > 0.0 ? heavy / starter : 0.0);
    // Капсула грызёт САМОЕ слабое, но грызёт: без неё игрок, потерявший корабль,
    // остаётся без единого способа заработать. Тяжёлый борт заметно обгоняет её и
    // стартовый корпус, а буровая ускоряет даже капсулу.
    check(pod > 0.0 && pod < starter && starter < heavy && heavy > starter * 2.0 &&
          podRig > pod * 2.0,
          "бур масштабируется с реактором", buf);

    // (§20.11) Буровая — запечённый бонус, значит она обязана ГИБНУТЬ вместе с
    // корпусом (грабля §15.1B). Ловим ровно это: поставили на корабль, утопили
    // корабль — в капсуле установки быть не должно.
    {
        Game g;
        buildWorld(g, 42, 0);
        Agent& pa = g.agents[g.playerAgent];
        if (laser >= 0) applyModuleToShip(pa.ship, moduleDefs()[size_t(laser)]);
        pa.ship.modules.push_back(laser);
        const double fitted = pa.ship.miningRig;
        downgradeAgentToEscapePod(pa);
        char b2[200];
        std::snprintf(b2, sizeof(b2), "установлено %.1f, после гибели корпуса %.1f, модулей %d",
                      fitted, pa.ship.miningRig, int(pa.ship.modules.size()));
        check(fitted > 0.0 && pa.ship.miningRig == 0.0 && pa.ship.modules.empty(),
              "буровая гибнет вместе с корпусом", b2);
    }
}

// (§22.3) `STAR_COUNT` — ОДНО число: и размер мира, и знаменатель плотности. Значит цели
// населения (`AGENT_TARGET_FULL`, `CONTRACT_TARGET_FULL`) записаны абсолютом, а означают
// ПЛОТНОСТЬ, и живут с ним в паре: поправит кто-нибудь размер мира, не тронув цели, — мир
// молча станет гуще или пустее, и глазами это не видно. Пиньним ровно эту связку: полный
// мир добирает свою цель, а мир вчетверо меньше получает ту же плотность, а не ту же кучу.
void testWorldPopulationScalesWithSize() {
    Game full;  full.seed = 42u; full.init(size_t(STAR_COUNT));
    Game small; small.seed = 42u; small.init(1200);
    const double dFull  = double(full.agents.size())  / double(STAR_COUNT);
    const double dSmall = double(small.agents.size()) / 1200.0;
    const bool reachesTarget = (full.agents.size() > size_t(AGENT_TARGET_FULL) * 9 / 10 &&
                                full.agents.size() <= size_t(AGENT_TARGET_FULL));
    const bool sameDensity   = (dSmall > dFull * 0.85 && dSmall < dFull * 1.15);
    const bool boardScales   = (CONTRACT_TARGET_FULL * 1200 / STAR_COUNT >= 30);
    char buf[220];
    std::snprintf(buf, sizeof(buf), "мир %d: бортов %d (цель %d) = %.4f/систему; мир 1200: %d = %.4f/систему; доска %d->%d",
                  STAR_COUNT, int(full.agents.size()), AGENT_TARGET_FULL, dFull,
                  int(small.agents.size()), dSmall,
                  CONTRACT_TARGET_FULL, CONTRACT_TARGET_FULL * 1200 / STAR_COUNT);
    check(reachesTarget && sameDensity && boardScales,
          "население мира идёт от его размера", buf);
}

// --- Доска заказов (§23) ----------------------------------------------------
//
// До §23 доска была мёртвым содержимым, и обе поломки были невидимы глазами:
// заказ платил 0.5..1.7 Cr за год полёта против 7.6..23.2 Cr/год, которые тот
// же НИЩИЙ игрок снимал свободной торговлей (в 10..30 раз меньше, да ещё и с
// занятым трюмом), а 9 сроков из 15 нельзя было выдержать в принципе.
//
// ⚠️ Мера здесь — только Cr за ГОД ПОЛЁТА, и только против свободного рейса
// ТОГО ЖЕ кошелька (§17.5 п.2 + §19.4: величину, на которую делишь, надо
// выдать и сделку исполнить). В кредитах проверка бессмысленна и переживёт
// любую поломку.

// Фактическая прибыль лучшей сделки сводки и время рейса под неё.
double freeRunPerYear(Game& g, double wallet) {
    const int pa = g.playerAgent;
    const int home = g.agents[pa].currentStar;
    g.agents[pa].money = wallet;
    const std::vector<ArbitrageDeal> board = g.playerArbitrageBoard(home, 1, -1);
    if (board.empty()) return -1.0;
    const ArbitrageDeal& d = board[0];
    const double years = g.agentRouteTravelTime(pa, d.targetStar);
    if (years <= 0.0) return -1.0;
    const double before = g.agents[pa].money;
    if (!g.agentBuyElementAmount(pa, d.element, d.units)) return -1.0;
    g.agents[pa].currentStar = d.targetStar;
    g.agents[pa].ship.enRoute = false;
    g.agentSellCargoAmount(pa, 1e18, d.element);
    return (g.agents[pa].money - before) / years;
}

// 1. Заказ — ровня небогатому рейсу, а не милостыня и не золотая жила.
void testJobPaysLikeARun() {
    double worstRatio = 1e9;
    double bestRatio = 0.0;
    char detail[220] = "нет данных";
    for (unsigned seed : {42u, 7u, 1337u}) {
        Game g; buildWorld(g, seed, 200);
        const int pa = g.playerAgent;
        const int home = g.agents[pa].currentStar;

        double jobSum = 0.0;
        int jobs = 0;
        const std::vector<Contract> visible = g.playerVisibleContractsAt(home);
        for (const Contract& c : visible) {
            const double years = g.agentContractRouteTravelTime(pa, c.id);
            if (years <= 0.0) continue;
            jobSum += c.reward / years;
            ++jobs;
        }
        if (jobs == 0) continue;

        // Кошелёк выдаётся ОТДЕЛЬНОМУ миру: исполнение сделки двигает рынок,
        // и мерить по нему заказы того же мира уже нельзя.
        Game h; buildWorld(h, seed, 200);
        const double runRate = freeRunPerYear(h, 100.0);
        if (runRate <= 0.0) continue;

        const double ratio = (jobSum / jobs) / runRate;
        worstRatio = std::min(worstRatio, ratio);
        bestRatio = std::max(bestRatio, ratio);
        std::snprintf(detail, sizeof(detail),
            "seed %u: заказ %.1f Cr/год, свободный рейс (100 Cr) %.1f Cr/год => %.2fx",
            seed, jobSum / jobs, runRate, ratio);
    }
    // Полоса широкая нарочно: ловим не число, а МЕХАНИЗМ — заказ снова стал
    // платить за расстояние по линейке (уедет в 0.0x) или начал печатать
    // деньги (уедет за 3x). Внутри полосы баланс — дело вкуса.
    check(worstRatio > 0.25 && bestRatio < 3.0,
          "заказ платит соизмеримо свободному рейсу", detail);
}

// 2. Срок заказа выполним тем самым кораблём, которым его повезут.
void testJobDeadlinesAreReachable() {
    int total = 0;
    int reachable = 0;
    int rushTotal = 0;
    int rushReachable = 0;
    for (unsigned seed : {42u, 7u, 1337u}) {
        Game g; buildWorld(g, seed, 200);
        const int pa = g.playerAgent;
        const int home = g.agents[pa].currentStar;
        for (const Contract& c : g.playerVisibleContractsAt(home)) {
            const double eta = g.agentContractRouteTravelTime(pa, c.id);
            if (eta <= 0.0) continue;
            const bool fits = eta <= c.deadline - g.time;
            // СРОЧНЫЕ заказы считаются отдельно: им тугой срок положен по
            // замыслу (§24), и мешать их с обычными — значит мерить среднее по
            // больнице. Смешанный счёт как раз и провалил эту проверку, когда
            // срез срока стоял на 0.62.
            if (c.rushFactor > 1.01) {
                ++rushTotal;
                if (fits) ++rushReachable;
            } else {
                ++total;
                if (fits) ++reachable;
            }
        }
    }
    char buf[220];
    std::snprintf(buf, sizeof(buf), "обычные: %d из %d; срочные: %d из %d",
        reachable, total, rushReachable, rushTotal);
    // Замер до §23 давал 6 из 15 на обычных. Требуем подавляющее большинство,
    // но не все: тугой срок — законный заказ, он просто платит 45%.
    // У срочных планка ниже: они ставка, а не гарантия. Но НУЛЁМ она быть не
    // должна — недостижимый срочный заказ это ловушка, а не выбор.
    check(total >= 9 && reachable * 100 >= total * 80 &&
          (rushTotal == 0 || rushReachable > 0),
          "срок заказа выполним стартовым кораблём", buf);
}

// --- Репутация и тиры (§24) --------------------------------------------------

// Кривая обязана быть МОНОТОННОЙ и покрывать всю лестницу корпусов: от того,
// что влезает в стартовый трюм, до того, что не влезает ни в один корпус игры.
// Ломается это молча — числа остаются «похожими на правду».
void testJobTierCurveSpansTheHullLadder() {
    double prevCargo = -1.0;
    double prevPay = -1.0;
    bool monotone = true;
    for (int i = 0; i <= 100; ++i) {
        const double t = double(i) / 100.0;
        const double cargo = Game::jobCargoForTier(t);
        const double pay = Game::jobPayMultiplierForTier(t);
        if (cargo < prevCargo || pay < prevPay) monotone = false;
        prevCargo = cargo;
        prevPay = pay;
    }
    const double bottom = Game::jobCargoForTier(0.0);
    const double top = Game::jobCargoForTier(1.0);

    // ⚠️ Мерить надо против ДОСТИЖИМОЙ части лестницы корпусов, а не против
    // самого большого трюма в таблице. В ней есть Tera-Freighter (2 000 000 т)
    // за 8 ТРИЛЛИОНОВ и Fortress за 900 триллионов — заказами такое не
    // окупается никогда (топовый заказ платит порядка 6e8), это фантазийный
    // верх, живущий отдельно от экономики. Порог в 100 млрд отсекает его и
    // оставляет Giga-Freighter (150 000 т, 12 млрд) — реальный потолок
    // грузовика, до которого игрок может дойти.
    const double reachablePrice = 1e11;
    double reachableHull = 0.0;
    std::string reachableName = "-";
    for (const ShipClass& sc : shipClasses()) {
        if (sc.price > reachablePrice) continue;
        if (sc.cargoCapacity > reachableHull) {
            reachableHull = sc.cargoCapacity;
            reachableName = sc.name;
        }
    }

    char buf[240];
    std::snprintf(buf, sizeof(buf), "низ %.0f т (старт 110), верх %.0f т; крупнейший достижимый корпус %s %.0f т => нужно %.1f борта",
        bottom, top, reachableName.c_str(), reachableHull, top / std::max(1.0, reachableHull));
    check(monotone && bottom <= 110.0 && top > reachableHull * 2.0,
          "кривая тира кроет лестницу от старта до флота", buf);
}

// Репутация растёт на +1 за сдачу и падает ПО РАЗМЕРУ заказа. Асимметрия — это
// вся суть верха лестницы, и потерять её проще всего.
void testFailureCostsMoreThanSuccessGives() {
    const double base = REPUTATION_FAIL_FACTOR * std::sqrt(Game::jobCargoForTier(0.0) / JOB_CARGO_BASE);
    const double mid  = REPUTATION_FAIL_FACTOR * std::sqrt(Game::jobCargoForTier(0.707) / JOB_CARGO_BASE);
    const double top  = REPUTATION_FAIL_FACTOR * std::sqrt(Game::jobCargoForTier(1.0) / JOB_CARGO_BASE);
    char buf[220];
    std::snprintf(buf, sizeof(buf), "успех всегда +1; провал: базовый -%.0f, средний -%.0f, топовый -%.0f заказов",
        base, mid, top);
    check(base >= 1.0 && base <= 5.0 && mid > base * 5.0 && top > mid * 2.0 && top < REPUTATION_CAP_JOBS * 0.5,
          "провал бьёт по размеру, успех даёт единицу", buf);
}

// Репутация ОТКРЫВАЕТ работу: на нуле доска даёт мелочь, на потолке — то, что
// не влезает в один корпус. Проверяется на живом мире, а не на формуле.
void testReputationUnlocksBiggerJobs() {
    double lowBest = 0.0;
    double highBest = 0.0;
    double highMass = 0.0;
    for (int pass = 0; pass < 2; ++pass) {
        Game g; buildWorld(g, 42, 200);
        const int home = g.agents[g.playerAgent].currentStar;
        g.resizeFactionReputation();
        for (size_t f = 0; f < g.factionReputation.size(); ++f) {
            g.factionReputation[f] = pass == 0 ? 0.0 : REPUTATION_CAP_JOBS;
        }
        // ⚠️ Доску надо СНЕСТИ: она уже забита заказами, созданными до
        // выставления репутации, и новых просто не будет (потолок на систему).
        // Пробник без этого показывал тир 0.000 даже на максимуме.
        g.contracts.clear();
        for (int y = 0; y < 400; ++y) g.update(1.0);
        // Смотрим ВЕСЬ мир, а не одну доску. Тир каждого заказа кидается
        // кубически смещённым броском, поэтому попадание в самый верх редко:
        // на восьми строках одной системы его может не случиться вовсе, и
        // проверка ловила бы удачу броска, а не работу механики.
        (void)home;
        for (const Contract& c : g.contracts) {
            if (pass == 0) {
                lowBest = std::max(lowBest, c.reward);
            } else if (c.reward > highBest) {
                highBest = c.reward;
                highMass = g.contractCargoMass(c);
            }
        }
    }
    char buf[220];
    std::snprintf(buf, sizeof(buf), "на нуле лучший %.0f Cr; на потолке %.0f Cr за %.0f т (x%.0f)",
        lowBest, highBest, highMass, lowBest > 0.0 ? highBest / lowBest : 0.0);
    check(lowBest > 0.0 && highBest > lowBest * 1000.0 && highBest > 1e8,
          "репутация открывает заказы на порядки крупнее", buf);
}

// Крупный заказ не влезает в один корпус — и это ПРОВЕРЯЕТСЯ при взятии.
// Иначе весь смысл флота и лицензий пропадает.
void testFleetCapacityGatesBigJobs() {
    Game g; buildWorld(g, 42, 200);
    const int pa = g.playerAgent;
    const int home = g.agents[pa].currentStar;
    const double oneHold = g.agents[pa].ship.cargoCapacity;
    const double fleetAlone = g.playerFleetCapacityAt(home);

    // Заказ ровно вдвое больше одного трюма: одним бортом не взять.
    Contract big;
    big.id = g.nextContractId++;
    big.type = ContractType::Delivery;
    big.originStar = home;
    big.targetStar = home == 0 ? 1 : 0;
    big.resource = 0;
    big.amount = oneHold * 2.0 / std::max(0.001, resourceUnitMassByIndex(0));
    big.reward = 1000.0;
    big.postedTime = g.time;
    big.deadline = g.time + 500.0;
    g.contracts.push_back(big);

    const bool refusedAlone = !g.playerFleetFitsContract(big.id) && !g.agentAcceptContract(pa, big.id);

    // Добавляем второй борт в ТОЙ ЖЕ системе — теперь флот покрывает объём.
    Agent second = g.agents[pa];
    second.ship.cargo.clear();
    second.currentStar = home;
    second.ship.enRoute = false;
    second.playerControlled = true;
    g.agents.push_back(second);
    const double fleetWithTwo = g.playerFleetCapacityAt(home);
    const bool acceptedWithFleet = g.playerFleetFitsContract(big.id) && g.agentAcceptContract(pa, big.id);

    const Contract* stored = nullptr;
    for (const Contract& c : g.contracts) if (c.id == big.id) stored = &c;
    const size_t carriers = stored ? stored->carriers.size() : 0;

    char buf[220];
    std::snprintf(buf, sizeof(buf), "трюм борта %.0f, флот 1 борт %.0f -> отказ=%d; 2 борта %.0f -> взят=%d, носителей %d",
        oneHold, fleetAlone, int(refusedAlone), fleetWithTwo, int(acceptedWithFleet), int(carriers));
    check(refusedAlone && acceptedWithFleet && carriers == 2,
          "крупный заказ требует флота и делится по бортам", buf);
}

// Сейв обязан переживать репутацию, тир и журнал — иначе весь грайнд сгорает
// на первой же загрузке, а журнал (§23) снова становится одноразовым.
// §25. Имя системы — «Varen-417», а не «Star_417». Проверяется не красота, а
// три свойства, которые ломаются молча: имя выговариваемо (в каждом слоге есть
// гласная, буквы не утраиваются), оно детерминировано от зерна (иначе звезда в
// сейве и в новой партии с тем же зерном звалась бы по-разному) и НЕ совпадает
// со словом интерфейса (иначе перевод превратит систему Mine в «ШАХТУ-417»).
void testStarNamesAreNamesNotIndices() {
    int bad = 0, noVowel = 0, tripled = 0, collided = 0, unstable = 0;
    size_t longest = 0;
    std::string worst;
    for (unsigned seed = 1; seed <= 4; ++seed) {
        for (size_t i = 0; i < 2048; ++i) {
            const std::string name = starNameFor(seed, i);
            if (starNameFor(seed, i) != name) ++unstable;
            const size_t cut = name.find_last_of('-');
            if (cut == std::string::npos || name.substr(cut + 1) != std::to_string(i)) { ++bad; continue; }
            const std::string stem = name.substr(0, cut);
            if (stem.size() < 3) { ++bad; worst = stem; continue; }
            if (stem.size() > longest) { longest = stem.size(); worst = stem; }
            std::string upper;
            bool vowel = false;
            for (size_t k = 0; k < stem.size(); ++k) {
                const char c = stem[k];
                const char low = (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
                if (low == 'a' || low == 'e' || low == 'i' || low == 'o' || low == 'u') vowel = true;
                if (!((low >= 'a' && low <= 'z') || c == '\'')) ++bad;
                if (k >= 2 && stem[k] == stem[k - 1] && stem[k] == stem[k - 2]) ++tripled;
                upper += (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
            }
            if (!vowel) ++noVowel;
            if (I18N::isInterfaceWord(upper)) { ++collided; worst = stem; }
        }
    }
    char buf[240];
    std::snprintf(buf, sizeof(buf), "8192 имён: битых %d, без гласной %d, с утроенной буквой %d, "
                  "совпало со словом интерфейса %d, недетерминированных %d; длиннейшая основа %d (%s)",
                  bad, noVowel, tripled, collided, unstable, int(longest), worst.c_str());
    check(bad == 0 && noVowel == 0 && tripled == 0 && collided == 0 && unstable == 0 && longest <= 12,
          "имя системы выговаривается и не спорит с интерфейсом", buf);
}

// §25. Имя должно говорить по-русски ВЕЗДЕ и переживать сейв. Реестр имён
// собственных наполняется при генерации скопления, но сейв несёт имена строкой
// — поэтому после загрузки реестр обязан собраться заново.
void testStarNamesSpeakRussianAfterLoad() {
    Game g; buildWorld(g, 7, 8);
    const std::string path = "balance_name_probe.sav";
    if (!g.saveToFile(path)) { check(false, "сейв имён: запись", "saveToFile вернул false"); return; }
    Game loaded;
    if (!loaded.loadFromFile(path)) { check(false, "сейв имён: чтение", "loadFromFile вернул false"); return; }
    std::remove(path.c_str());

    int mismatched = 0, latinLeft = 0;
    const I18N::Lang before = I18N::lang();
    I18N::setLang(I18N::LANG_RU);
    std::string sample, sampleRu;
    for (size_t i = 0; i < loaded.cluster.stars.size() && i < 512; ++i) {
        const std::string& name = loaded.cluster.stars[i].name;
        if (name != g.cluster.stars[i].name) ++mismatched;
        // Перевод целой строки, а не голого имени: имя обязано находиться и
        // внутри фразы журнала, иначе §14 его не увидит.
        const std::string line = I18N::tr("DESTINATION: " + name);
        for (size_t k = 0; k < line.size(); ++k) {
            const char c = line[k];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { ++latinLeft; break; }
        }
        if (i == 3) { sample = name; sampleRu = line; }
    }
    I18N::setLang(before);

    char buf[240];
    std::snprintf(buf, sizeof(buf), "512 имён после загрузки: разошлось %d, с латинским огрызком в RU %d; «%s» -> «%s»",
                  mismatched, latinLeft, sample.c_str(), sampleRu.c_str());
    check(mismatched == 0 && latinLeft == 0, "имя системы переживает сейв и говорит по-русски", buf);
}

// §25. Своё имя системе. Проверяется не только успех, но и три отказа: чужую
// систему не переименовать, из другой системы — тоже, пустое имя не проходит.
void testPlayerRenamesOwnSystem() {
    Game g; buildWorld(g, 11, 8);
    const int here = g.agents[g.playerAgent].currentStar;
    const int elsewhere = here == 0 ? 1 : 0;

    const bool deniedForeign = !g.playerRenameSystem(here, "Perekat");
    g.agents[g.playerAgent].money = 1.0e15;
    const bool bought = g.playerBuySystem();
    const std::string before = g.cluster.stars[here].name;
    const bool renamed = g.playerRenameSystem(here, "Perekat");
    const bool numberKept = g.cluster.stars[here].name == "Perekat-" + std::to_string(here);
    const bool deniedEmpty = !g.playerRenameSystem(here, "   ");
    const bool deniedRemote = !g.playerRenameSystem(elsewhere, "Perekat");

    // Новое имя обязано сразу зазвучать по-русски: реестр §25 пополняется тем же
    // вызовом, что и переименование, иначе имя останется латиницей в кириллице.
    const I18N::Lang lang = I18N::lang();
    I18N::setLang(I18N::LANG_RU);
    const std::string ru = I18N::tr("DESTINATION: " + g.cluster.stars[here].name);
    I18N::setLang(lang);
    const bool speaksRussian = ru.find("Перекат") != std::string::npos;

    char buf[240];
    std::snprintf(buf, sizeof(buf), "«%s» -> «%s»; RU «%s»; отказы: чужая=%d, пустое=%d, издалека=%d",
                  before.c_str(), g.cluster.stars[here].name.c_str(), ru.c_str(),
                  int(deniedForeign), int(deniedEmpty), int(deniedRemote));
    check(bought && renamed && numberKept && deniedForeign && deniedEmpty && deniedRemote && speaksRussian,
          "своя система переименовывается, номер цел", buf);
}

void testSaveKeepsReputationAndJournal() {
    Game g; buildWorld(g, 42, 60);
    g.resizeFactionReputation();
    for (size_t f = 0; f < g.factionReputation.size(); ++f) g.factionReputation[f] = 40.0 + double(f) * 7.0;
    g.pushJournal(JournalKind::JobAccepted, "TOOK #7 DELIVERY 120 Fe > STAR_1", 0.0, 3);
    g.pushJournal(JournalKind::JobCompleted, "#7 DELIVERY +9000 Cr", 9000.0, 3);
    if (!g.contracts.empty()) {
        g.contracts[0].tier = 0.63;
        g.contracts[0].rushFactor = JOB_RUSH_PAY;
        g.contracts[0].carriers.clear();
        g.contracts[0].carriers.push_back(0);
        g.contracts[0].carriers.push_back(1);
    }
    const int probeId = g.contracts.empty() ? -1 : g.contracts[0].id;
    const double repBefore = g.factionReputationOf(1);
    const size_t linesBefore = g.transactions.size();

    const std::string path = "balance_save_probe.sav";
    if (!g.saveToFile(path)) { check(false, "сейв: запись", "saveToFile вернул false"); return; }
    Game loaded;
    if (!loaded.loadFromFile(path)) { check(false, "сейв: чтение", "loadFromFile вернул false"); return; }
    std::remove(path.c_str());

    const Contract* restored = nullptr;
    for (const Contract& c : loaded.contracts) if (c.id == probeId) restored = &c;
    int journalKinds = 0;
    for (const Transaction& t : loaded.transactions) {
        if (t.kind == JournalKind::JobAccepted || t.kind == JournalKind::JobCompleted) ++journalKinds;
    }
    const bool textKept = !loaded.transactions.empty() &&
        loaded.transactions.front().text.find("DELIVERY") != std::string::npos;

    char buf[240];
    std::snprintf(buf, sizeof(buf), "репутация %.0f->%.0f, строк журнала %d->%d (видов %d, текст цел=%d), тир %.2f, носителей %d",
        repBefore, loaded.factionReputationOf(1), int(linesBefore), int(loaded.transactions.size()),
        journalKinds, int(textKept), restored ? restored->tier : -1.0,
        restored ? int(restored->carriers.size()) : -1);
    check(std::abs(loaded.factionReputationOf(1) - repBefore) < 0.001 &&
          loaded.transactions.size() == linesBefore && journalKinds == 2 && textKept &&
          restored && std::abs(restored->tier - 0.63) < 0.001 && restored->carriers.size() == 2,
          "сейв переживает репутацию, журнал и тир заказа", buf);
}

// 3. Срок обязателен: взятый заказ можно ПРОВАЛИТЬ.
// До §23 взятый заказ не сгорал никогда — 443 года сверх срока, и он всё ещё
// висел на руках, готовый заплатить 45%. Срок был ценником, а не обязательством.
void testTakenJobCanExpire() {
    Game g; buildWorld(g, 42, 200);
    const int pa = g.playerAgent;
    const int home = g.agents[pa].currentStar;

    int taken = -1;
    double deadline = 0.0;
    for (const Contract& c : g.playerVisibleContractsAt(home)) {
        if (c.acceptedByAgent >= 0 || !g.agentContractCargoFits(pa, c.id)) continue;
        if (!g.agentAcceptContract(pa, c.id)) continue;
        taken = c.id;
        deadline = c.deadline;
        break;
    }
    if (taken < 0) { check(false, "провал заказа: заказ удалось взять", "ни один не взялся"); return; }

    // Сидим на месте и не везём. Заказ обязан сгореть, а не ждать вечно.
    //
    // Ищем след в ЖУРНАЛЕ, а не флаг в списке: сгоревшие заказы через 18 лет
    // выметаются чисткой `updateContracts`, и проверка по `contract.failed`
    // ловила бы гонку с уборщиком, а не саму механику.
    const std::string mark = "#" + std::to_string(taken) + " ";
    bool journalled = false;
    double failedAt = 0.0;
    for (int y = 0; y < 4000 && !journalled; ++y) {
        g.update(1.0);
        for (const Transaction& t : g.transactions) {
            if (t.kind != JournalKind::JobFailed || t.text.find(mark) == std::string::npos) continue;
            journalled = true;
            failedAt = t.time;
        }
    }
    char buf[220];
    std::snprintf(buf, sizeof(buf), "заказ #%d, срок %.0fY, сгорел на %.0fY (прокручено %.0fY)",
        taken, deadline, failedAt, g.time);
    check(journalled, "просроченный заказ сгорает и попадает в журнал", buf);
}

// 4. Журнал не считает одну выплату дважды.
// Награда пишется отдельной ЗЕЛЁНОЙ строкой, а безымянная кассовая лента
// обязана на эту же сумму уменьшиться — иначе игрок видит свой заработок
// дважды и не понимает, сколько на самом деле заработал.
void testJournalCountsPayoutOnce() {
    Game g; buildWorld(g, 42, 200);
    const int pa = g.playerAgent;
    const int home = g.agents[pa].currentStar;

    // Нужен именно ГРУЗОВОЙ заказ: разведка на сдаче сначала лишь отправляет
    // отчёт (возвращает true, но не платит), и мерить выплату по ней нельзя.
    int taken = -1;
    for (const Contract& c : g.playerVisibleContractsAt(home)) {
        if (c.acceptedByAgent >= 0 || c.resource < 0) continue;
        if (!g.agentContractCargoFits(pa, c.id)) continue;
        if (!g.agentAcceptContract(pa, c.id)) continue;
        taken = c.id;
        break;
    }
    if (taken < 0) { check(false, "журнал: грузовой заказ удалось взять", "ни один не взялся"); return; }

    const Contract* c = nullptr;
    for (const Contract& x : g.contracts) if (x.id == taken) c = &x;
    if (!c) { check(false, "журнал: заказ на месте", "исчез"); return; }

    const int target = c->targetStar;
    const double moneyBefore = g.agents[pa].money;
    g.transactions.clear();
    g.agents[pa].currentStar = target;
    g.agents[pa].ship.enRoute = false;
    if (!g.agentCompleteContract(pa, taken)) { check(false, "журнал: заказ сдался", "сдать не вышло"); return; }
    g.update(1.0);   // прокрутка, на которой пишется кассовая строка

    const double earned = g.agents[pa].money - moneyBefore;
    double logged = 0.0;
    int green = 0;
    for (const Transaction& t : g.transactions) {
        logged += t.amount;
        if (t.kind == JournalKind::JobCompleted) ++green;
    }
    char buf[220];
    std::snprintf(buf, sizeof(buf), "кошелёк +%.0f, журнал суммарно +%.0f, зелёных строк %d, всего строк %d",
        earned, logged, green, int(g.transactions.size()));
    check(green == 1 && std::abs(logged - earned) < 1.0,
          "выплата за заказ попадает в журнал ровно один раз", buf);
}

// --- Совет Тимертии (§27) ---------------------------------------------------
// Реплики новеллы ранжировали рынки МАССОЙ запаса и МАССОЙ спроса. У водорода
// её на порядки больше всех, поэтому «покупай» всегда указывало на водород, а
// «продавай» — на самую населённую систему в 15 ly, у которой и потребление
// максимально: обе половины сводки сходились в ОДНУ систему, и на скриншоте
// игрока Тимертия предлагала купить и продать водород в одном и том же порту.
// Хуже того, цена в «лучшей» цели бывала НИЖЕ домашней (замер 2.38 -> 1.74) —
// совет отправлял торговать в убыток.
void testAdviceIsARealRun() {
    const unsigned seeds[3] = {42u, 1337u, 7u};
    int good = 0;
    char buf[260] = "";
    for (int s = 0; s < 3; ++s) {
        Game g; buildWorld(g, seeds[s], 0);
        const int home = g.agents[g.playerAgent].currentStar;
        const TradeRun run = g.playerBestRun(home, 128, false);
        const bool ok = run.valid && run.targetStar != home && run.profit > 0.0 &&
                        run.sellPrice > run.buyPrice && run.years > 0.0;
        if (ok) ++good;
        if (s == 0) {
            std::snprintf(buf, sizeof(buf), "seed 42: %s %.2f -> %.2f в %s, %.1f ly, %.0f лет, %.0f Cr = %.1f Cr/год",
                run.valid ? elementDefinitions()[size_t(run.element)].symbol : "-",
                run.buyPrice, run.sellPrice,
                run.valid ? g.cluster.stars[size_t(run.targetStar)].name.c_str() : "-",
                run.distanceLy, run.years, run.profit, run.perYear);
        }
    }
    check(good == 3, "совет ведёт в ДРУГУЮ систему и в плюс", buf);
}

// Мерка совета — Cr за ГОД полёта, а не абсолютная прибыль: 3929 Cr за 313 лет
// хуже, чем 3819 Cr за 33. Проверка живая: сравниваем выбранный рейс с самым
// прибыльным по абсолюту среди тех же кандидатов.
void testAdviceMeasuresPerYear() {
    Game g; buildWorld(g, 1337, 0);
    const int pa = g.playerAgent;
    const int home = g.agents[pa].currentStar;
    const TradeRun run = g.playerBestRun(home, 128, false);
    if (!run.valid) { check(false, "совет мерит Cr/год", "рейса не нашлось"); return; }

    // Самая жирная по абсолюту сделка среди разведанного — та же, что показала бы
    // биржа. Она НЕ обязана быть советом, но её Cr/год не должно быть выше.
    for (int i = 0; i < int(g.cluster.stars.size()); ++i) g.observeMarketForFaction(g.playerFaction, i);
    const std::vector<ArbitrageDeal> board = g.playerArbitrageBoard(home, 1, -1);
    if (board.empty()) { check(false, "совет мерит Cr/год", "биржа пуста"); return; }
    const double fatYears = g.agentRouteTravelTime(pa, board[0].targetStar);
    const double fatPerYear = fatYears > 0.0 ? board[0].profit / fatYears : 0.0;

    char buf[220];
    std::snprintf(buf, sizeof(buf), "совет %.0f Cr за %.0f лет = %.1f Cr/год; жирнейшая сделка %.0f Cr за %.0f лет = %.1f Cr/год",
        run.profit, run.years, run.perYear, board[0].profit, fatYears, fatPerYear);
    check(run.perYear >= fatPerYear * 0.999, "совет мерит Cr/ГОД, а не общую сумму", buf);
}

// Сводка (шаг 100) обязана уважать разведку: советовать можно только про то, что
// игрок видел сам. Прежний код читал `game.markets` напрямую и называл системы,
// где игрок не был, — в радиусе 15 ly их сотни, а разведана ровно одна.
void testAdviceRespectsSurvey() {
    Game fresh; buildWorld(fresh, 42, 0);
    const int homeFresh = fresh.agents[fresh.playerAgent].currentStar;
    const TradeRun blind = fresh.playerBestRun(homeFresh, 128, true);

    Game seen; buildWorld(seen, 42, 400);
    const int homeSeen = seen.agents[seen.playerAgent].currentStar;
    const TradeRun known = seen.playerBestRun(homeSeen, 128, true);

    char buf[220];
    std::snprintf(buf, sizeof(buf), "разведано %d -> совета нет (%s); разведано %d -> совет есть (%s)",
        fresh.playerSurveyedMarketCount(), blind.valid ? "ЕСТЬ" : "нет",
        seen.playerSurveyedMarketCount(), known.valid ? "есть" : "НЕТ");
    check(!blind.valid && known.valid, "сводка молчит про неразведанное", buf);
}

// Один seed — один мир, СКОЛЬКО БЫ миров ни построил процесс. Уровень цен
// скопления кэшируется в market.cpp, а проталкивается туда уже после засева
// рынков: второй мир релаксировал цены под остаток первого. Замер до правки —
// уровень до init 1.0000 / 0.2652 / 0.4641 на трёх подряд мирах seed 42.
void testSameSeedSameWorld() {
    Game a; buildWorld(a, 42, 0);
    Game b; buildWorld(b, 42, 0);
    const int ha = a.agents[a.playerAgent].currentStar;
    const int hb = b.agents[b.playerAgent].currentStar;

    double worst = 0.0;
    int worstElement = -1;
    const int elems = int(std::min(elementCount(), a.markets[size_t(ha)].prices.size()));
    for (int e = 0; e < elems; ++e) {
        const double pa = a.markets[size_t(ha)].prices[size_t(e)];
        const double pb = b.markets[size_t(hb)].prices[size_t(e)];
        const double diff = std::abs(pa - pb) / std::max(1e-6, std::abs(pa));
        if (diff > worst) { worst = diff; worstElement = e; }
    }
    char buf[220];
    std::snprintf(buf, sizeof(buf), "дом %s против %s, худшее расхождение цены %.4f%% (%s)",
        a.cluster.stars[size_t(ha)].name.c_str(), b.cluster.stars[size_t(hb)].name.c_str(),
        worst * 100.0, worstElement >= 0 ? elementDefinitions()[size_t(worstElement)].symbol : "-");
    check(ha == hb && worst < 1e-9, "один seed даёт один мир и во второй раз", buf);
}

// Клавиша V пересчитывает сводку и ПЛАТИТ за это реактором (§28). Просчёт — это
// работа, а не подсказка интерфейса: на сухом бункере Тимертия считать
// отказывается, ровно как выключается буровая (§20.7).
void testInsightBurnsReactorFuel() {
    Game g; buildWorld(g, 42, 0);
    Ship& ps = g.agents[g.playerAgent].ship;

    UI::WindowState ui;
    ui.vnState.tutorialCompleted = true;
    ui.vnState.active = false;

    const double bunker = shipFuelMass(ps);
    UI::toggleVisualNovel(ui, g);
    const double burned = bunker - shipFuelMass(ps);
    const int paidStep = ui.vnState.tutorialStep;
    const bool spoke = !ui.vnState.targetText.empty();

    // Второе нажатие ГАСИТ коробку и не берёт платы: деньги (топливо) идут за
    // просчёт, а не за нажатие клавиши.
    const double afterFirst = shipFuelMass(ps);
    UI::toggleVisualNovel(ui, g);
    const bool hidden = !ui.vnState.active;
    const double freeToggle = afterFirst - shipFuelMass(ps);

    // Сухой бункер: считать не на чем.
    ps.fuel.clear();
    UI::toggleVisualNovel(ui, g);
    const int dryStep = ui.vnState.tutorialStep;

    // Плата — ровно процент ПОЛНОГО бака (§28.2). Меряем на полном бункере,
    // поэтому доля от остатка и доля от ёмкости здесь совпадают; окно узкое
    // нарочно: подмена «процента бака» на «процент остатка» сделала бы совет тем
    // дешевле, чем хуже дела у игрока, и такую подмену обязано ловить число.
    const double share = bunker > 0.0 ? burned / bunker : 0.0;
    char buf[240];
    std::snprintf(buf, sizeof(buf), "сожжено %.4f из %.1f бункера (%.2f%%, надо 1.00%%), шаг %d; выключение бесплатно (%.4f); на сухом шаг %d",
        burned, bunker, share * 100.0, paidStep, freeToggle, dryStep);
    check(share > 0.009 && share < 0.011 && paidStep == 101 && spoke &&
          hidden && freeToggle == 0.0 && dryStep == 102,
          "просчёт по V стоит процент бака, на сухом молчит", buf);
}

// --- Хайтек-этаж: экзотическая материя (§31) --------------------------------

// Рынок экзотики обязан быть РЕДКИМ. В этом весь смысл: у каждого вещества свой
// тип звезды, и источники надо искать. Если рынок окажется на каждом углу,
// хайтек превратится в ещё один ряд цифр, а разведка снова потеряет поздний
// смысл. Порог широкий — ловим ПОЛОМКУ ГЕЙТА (кто-то ослабил условие), а не
// точное число.
void testExoticMarketsAreRare() {
    Game g;
    g.seed = 42;
    g.init(4096);
    int markets[EX_COUNT] = {0, 0, 0};
    int sources[EX_COUNT] = {0, 0, 0};
    int any = 0;
    for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
        bool has = false;
        for (int k = 0; k < EX_COUNT; ++k) {
            const double t = exoticTargetStock(g.cluster.stars[i], k);
            if (t <= 0.0) continue;
            markets[k]++;
            has = true;
            if (exoticStockSourceStrength(g.cluster.stars[i], k) > 0.0) sources[k]++;
        }
        if (has) ++any;
    }
    const double n = double(g.cluster.stars.size());
    char buf[240];
    std::snprintf(buf, sizeof(buf),
        "любой рынок %.1f%%; AM %.1f%% (источников %d), NM %.1f%% (%d), QC %.1f%% (%d)",
        100.0 * any / n, 100.0 * markets[0] / n, sources[0],
        100.0 * markets[1] / n, sources[1], 100.0 * markets[2] / n, sources[2]);
    bool ok = any > 0 && (100.0 * any / n) < 25.0;
    for (int k = 0; k < EX_COUNT; ++k) {
        const double share = 100.0 * markets[k] / n;
        if (!(share > 0.3 && share < 15.0)) ok = false;
        if (sources[k] <= 0) ok = false;   // без источника вещество взять негде вовсе
    }
    check(ok, "рынки экзотики редки и у каждого свой источник", buf);
}

// Рейс с экзотикой — это ДРУГОЙ ЭТАЖ денег, и при этом не бесконечный кран.
// Проверяем оба свойства сразу: выручка за отсек должна мериться миллионами
// (иначе хайтек не выводит к покупке систем за 1e9), а вторая такая же закупка
// подряд обязана обойтись дороже — источник вырабатывается.
void testExoticRunIsAnotherFloor() {
    Game g;
    buildWorld(g, 2026, 40);
    const int pa = g.playerAgent;

    // Ищем источник и потребителя одного вещества.
    int source = -1, sink = -1, kind = -1;
    for (int k = 0; k < EX_COUNT && source < 0; ++k) {
        double bestLow = 1e18, bestHigh = 0.0;
        int lowAt = -1, highAt = -1;
        for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
            const double p = g.exoticPriceAt(int(i), k);
            if (p <= 0.0) continue;
            if (p < bestLow) { bestLow = p; lowAt = int(i); }
            if (p > bestHigh) { bestHigh = p; highAt = int(i); }
        }
        if (lowAt >= 0 && highAt >= 0 && lowAt != highAt && bestHigh > bestLow * 1.5) {
            source = lowAt; sink = highAt; kind = k;
        }
    }
    if (source < 0) {
        check(false, "рейс с экзотикой — другой этаж денег", "не нашлось пары источник/потребитель");
        return;
    }

    // Ставим игрока на источник с ячейкой и деньгами.
    g.containmentLevel = CONTAINMENT_MAX_LEVEL;
    g.rebakePlayerBakedBonuses();
    g.agents[pa].currentStar = source;
    g.agents[pa].ship.enRoute = false;
    g.agents[pa].money = 3.0e8;

    const double firstPrice = g.exoticPriceAt(source, kind);
    const double bought = g.playerBuyExotic(kind, 1.0e9);
    const double spent = 3.0e8 - g.agents[pa].money;
    const double secondPrice = g.exoticPriceAt(source, kind);

    // Везём и продаём.
    g.agents[pa].currentStar = sink;
    const double before = g.agents[pa].money;
    const double sold = g.playerSellExotic(kind, 1.0e9);
    const double gained = g.agents[pa].money - before;

    char buf[260];
    std::snprintf(buf, sizeof(buf),
        "%s: взято %.0f по %.0f (потрачено %.3g), продано %.0f за %.3g => %.2fx; источник подорожал %.0f -> %.0f",
        exoticDefs()[size_t(kind)].symbol, bought, firstPrice, spent, sold, gained,
        spent > 0.0 ? gained / spent : 0.0, firstPrice, secondPrice);
    check(bought > 1.0 && sold > 1.0 && gained > 1.0e6 && gained > spent &&
          secondPrice > firstPrice * 1.05,
          "рейс с экзотикой — другой этаж денег", buf);
}

// Антивещество обязано и ДАВАТЬ энергию, и КОНЧАТЬСЯ. Это ровно то, что делает
// его расходником, а не постоянным апгрейдом: доля в тысячную по массе удваивает
// энергию смеси, но улетает вместе с рейсом.
void testAntimatterBurnsWithTheFuel() {
    Game g;
    buildWorld(g, 77, 10);
    Ship& ship = g.agents[g.playerAgent].ship;
    const double plain = shipFuelMix(ship).specificEnergy;
    ship.containment = 200.0;
    ship.exotic[EX_ANTIMATTER] = 12.0;
    const double boosted = shipFuelMix(ship).specificEnergy;
    const double amBefore = ship.exotic[EX_ANTIMATTER];
    shipConsumeForDeltaV(ship, 0.02, NULL);
    const double amAfter = ship.exotic[EX_ANTIMATTER];

    char buf[200];
    std::snprintf(buf, sizeof(buf),
        "энергия смеси %.4f -> %.4f МэВ/нуклон (x%.2f); антивещество %.3f -> %.3f",
        plain, boosted, plain > 0.0 ? boosted / plain : 0.0, amBefore, amAfter);
    check(boosted > plain * 1.5 && amAfter < amBefore && amAfter >= 0.0,
          "антивещество греет смесь и сгорает вместе с ней", buf);
}

// Кузница — это ВЫБОР, в отличие от ядра из исследований. Проверяем, что растёт
// именно названный стат и что за это списывают конденсат.
void testForgePicksTheStat() {
    Game g;
    buildWorld(g, 313, 20);
    const int pa = g.playerAgent;
    // Ставим игрока туда, где есть верфь: кузница — не полевая работа.
    int yard = -1;
    for (size_t i = 0; i < g.cluster.stars.size(); ++i) {
        if (g.shipyardLevelAtStar(int(i)) >= 2) { yard = int(i); break; }
    }
    if (yard < 0) {
        check(false, "кузница даёт ВЫБРАННОЕ ядро", "в мире не нашлось верфи 2 уровня");
        return;
    }
    g.agents[pa].currentStar = yard;
    g.agents[pa].ship.enRoute = false;
    g.agents[pa].money = 1.0e9;
    g.agents[pa].ship.containment = 400.0;
    g.agents[pa].ship.exotic[EX_CONDENSATE] = 300.0;

    const double sensorsBefore = g.tech.sensors;
    const double luckBefore = g.tech.luck;
    const double condBefore = g.agents[pa].ship.exotic[EX_CONDENSATE];
    const bool forged = g.playerForgeChromocore(TECH_SENSORS);
    const double condAfter = g.agents[pa].ship.exotic[EX_CONDENSATE];

    char buf[220];
    std::snprintf(buf, sizeof(buf),
        "сенсоры %.3f -> %.3f, удача %.3f -> %.3f (не тронута), конденсат %.0f -> %.0f, ядер %d",
        sensorsBefore, g.tech.sensors, luckBefore, g.tech.luck, condBefore, condAfter, g.tech.cores);
    check(forged && g.tech.sensors > sensorsBefore * 1.01 &&
          std::fabs(g.tech.luck - luckBefore) < 1e-9 && condAfter < condBefore,
          "кузница даёт ВЫБРАННОЕ ядро", buf);
}

// Переоснастка запекается в поля корпуса, значит обязана пережить и покупку
// нового корпуса, и сейв. Ровно та же грабля, что §32.2 с хромокорами: ячейка
// и броня не хранятся больше нигде, кроме `containmentLevel`/`hullPlating`.
void testRefitSurvivesHullAndSave() {
    Game g;
    buildWorld(g, 5150, 20);
    const int pa = g.playerAgent;
    g.containmentLevel = 2;
    g.hullPlating = 3;
    g.rebakePlayerBakedBonuses();
    const double armorWithPlating = g.agents[pa].ship.armor;
    const double bay = g.agents[pa].ship.containment;

    // Покупаем корпус побольше — shipApplyClass обнулит поля.
    int classId = -1;
    for (size_t c = 0; c < shipClasses().size(); ++c) {
        if (shipClasses()[c].cargoCapacity > g.agents[pa].ship.cargoCapacity * 1.5) { classId = int(c); break; }
    }
    g.agents[pa].money = 1.0e12;
    const bool bought = classId >= 0 && g.buyShip(pa, g.agents[pa].currentStar, classId);
    const double bayAfterHull = g.agents[pa].ship.containment;
    const double platedArmor = g.agents[pa].ship.armor;

    // И через сейв.
    const std::string path = "balance_refit_save.txt";
    const bool saved = g.saveToFile(path);
    Game b;
    b.init(1200);
    const bool loaded = saved && b.loadFromFile(path);
    std::remove(path.c_str());
    const double bayAfterLoad = loaded && b.playerAgent >= 0 ? b.agents[b.playerAgent].ship.containment : -1.0;
    const double armorAfterLoad = loaded && b.playerAgent >= 0 ? b.agents[b.playerAgent].ship.armor : -1.0;

    char buf[240];
    std::snprintf(buf, sizeof(buf),
        "ячейка %.0f -> %.0f (корпус) -> %.0f (сейв); броня со слоями %.0f -> %.0f -> %.0f",
        bay, bayAfterHull, bayAfterLoad, armorWithPlating, platedArmor, armorAfterLoad);
    check(bought && loaded && bay > 0.0 &&
          std::fabs(bayAfterHull - bay) < 0.5 && std::fabs(bayAfterLoad - bay) < 0.5 &&
          platedArmor > PLATING_ARMOR_PER_LAYER * 2.5 &&
          std::fabs(armorAfterLoad - platedArmor) < 0.5,
          "переоснастка переживает корпус и сейв", buf);
}

// Загрузка сейва обязана вернуть ЖИВОЙ рынок, а не только его цифры.
//
// В файле лежат «движущиеся части» — запас, цены, темпы. Модель НУЖДЫ (`needs`,
// `rationing`, `pref`, `tradeAccess`) в сейв не пишется, потому что она чистая
// функция звезды, — но раньше её никто и не восстанавливал. `Market::update` на
// первом же такте видел пустой `needs` и заполнял его НУЛЯМИ: спрос исчезал
// навсегда, цены обваливались к полу импортной полосы, склады пухли, `strain`
// становился вечным нулём, а `measureClusterServiceCost()` возвращал 0 — то есть
// замирали и тысячелетняя сверка, и ставка тарифа. Партия после первой загрузки
// была ДРУГОЙ игрой, и об этом не было ни строчки в интерфейсе.
//
// Меряем не «поля на месте», а СЛЕДСТВИЕ: прокручиваем оба мира одинаково и
// сравниваем спрос, цену и оборот.
void testSaveKeepsTheMarketAlive() {
    Game a;
    buildWorld(a, 909, 30);
    const int star = a.agents[a.playerAgent].currentStar;

    const std::string path = "balance_market_save.txt";
    if (!a.saveToFile(path)) {
        check(false, "загрузка сохраняет модель спроса", "сохранить не удалось");
        return;
    }
    Game b;
    b.init(1200);
    if (!b.loadFromFile(path)) {
        check(false, "загрузка сохраняет модель спроса", "загрузить не удалось");
        std::remove(path.c_str());
        return;
    }
    std::remove(path.c_str());

    // Обоим мирам даём одинаковый ход: разница может прийти только от загрузки.
    for (int y = 0; y < 60; ++y) { a.update(1.0); b.update(1.0); }

    double needsA = 0.0, needsB = 0.0, rateA = 0.0, rateB = 0.0;
    for (size_t f = 0; f < a.markets[star].needs.size(); ++f) needsA += a.markets[star].needs[f];
    for (size_t f = 0; f < b.markets[star].needs.size(); ++f) needsB += b.markets[star].needs[f];
    for (size_t i = 0; i < a.markets[star].demandRate.size(); ++i) rateA += a.markets[star].demandRate[i];
    for (size_t i = 0; i < b.markets[star].demandRate.size(); ++i) rateB += b.markets[star].demandRate[i];
    const double turnA = a.systemTurnover(star), turnB = b.systemTurnover(star);
    const double svcA = a.measureClusterServiceCost(), svcB = b.measureClusterServiceCost();

    // Пороги широкие: цель — поймать ИСЧЕЗНОВЕНИЕ модели, а не заморозить числа.
    const bool ok = needsA > 1.0 && needsB > needsA * 0.5 &&
                    rateA > 1.0 && rateB > rateA * 0.5 &&
                    turnB > turnA * 0.4 && svcB > svcA * 0.4;
    char buf[240];
    std::snprintf(buf, sizeof(buf),
        "нужда %.0f -> %.0f, спрос %.0f -> %.0f, оборот %.3g -> %.3g, услуга %.3g -> %.3g",
        needsA, needsB, rateA, rateB, turnA, turnB, svcA, svcB);
    check(ok, "загрузка сохраняет модель спроса", buf);
}

// Хромокоры обязаны пережить покупку корпуса.
//
// Три ветки прокачки из семи (материалы, тактика, кинематика) не хранятся
// нигде, кроме ПОЛЕЙ корабля, — а `shipApplyClass` при покупке переписывает эти
// поля табличными значениями нового корпуса. Модули после покупки перезапекались
// (§15.1B), хромокоры — нет: с первой же пересадки 43% прокачки умирало молча,
// продолжая показывать «>1.00» в интерфейсе. Меряем ОТНОШЕНИЕ к чистому
// корпусу того же класса, а не абсолютные числа: лестница классов может
// двигаться, закон запекания — нет.
void testChromocoresSurviveHullChange() {
    Game g;
    buildWorld(g, 4242, 40);
    const int pa = g.playerAgent;

    // Десять ядер в материалы, пять в тактику, пять в кинематику.
    for (int i = 0; i < 10; ++i) g.grantChromocore(TECH_MATERIALS);
    for (int i = 0; i < 5; ++i) g.grantChromocore(TECH_TACTICS);
    for (int i = 0; i < 5; ++i) g.grantChromocore(TECH_KINEMATICS);

    // Ищем корпус выше стартового и покупаем его, не считаясь с ценой.
    int classId = -1;
    for (size_t c = 0; c < shipClasses().size(); ++c) {
        if (shipClasses()[c].cargoCapacity > g.agents[pa].ship.cargoCapacity * 1.5) { classId = int(c); break; }
    }
    const ShipClass sc = shipClasses()[size_t(classId < 0 ? 0 : classId)];
    g.agents[pa].money = sc.price * 4.0 + 1.0e9;
    const bool bought = classId >= 0 && g.buyShip(pa, g.agents[pa].currentStar, classId);

    // Чистый корпус того же класса — точка отсчёта.
    Ship bare("bare", 0, 0, 0, 0, -1);
    shipApplyClass(bare, sc);

    const Ship& ps = g.agents[pa].ship;
    const double cargoRatio = bare.cargoCapacity > 0.0 ? ps.cargoCapacity / bare.cargoCapacity : 0.0;
    const double hullRatio = bare.maxHullHP > 0.0 ? ps.maxHullHP / bare.maxHullHP : 0.0;
    // Тактика умножает ПУШКИ, а у грузового корпуса их может не быть вовсе:
    // ноль, умноженный на что угодно, — ноль, и это не потеря прокачки
    // (множитель цел в `tech` и оживёт на боевом корпусе). Поэтому пушки
    // проверяем только там, где им есть чем стрелять.
    const bool armed = bare.heavyWeapons > 0.0;
    const double gunRatio = armed ? ps.heavyWeapons / bare.heavyWeapons : g.tech.tactics;

    char buf[220];
    std::snprintf(buf, sizeof(buf),
        "куплен %s: трюм x%.3f (стат %.3f), пушки x%.3f (стат %.3f%s), корпус x%.3f",
        sc.name.c_str(), cargoRatio, g.tech.materials, gunRatio, g.tech.tactics,
        armed ? "" : ", корпус безоружен", hullRatio);
    check(bought &&
          std::fabs(cargoRatio - g.tech.materials) < 0.02 &&
          std::fabs(gunRatio - g.tech.tactics) < 0.02 &&
          std::fabs(hullRatio - g.tech.materials) < 0.02,
          "хромокоры переживают покупку корпуса", buf);
}

// Перевод обязан сохранять порядок %s и чисел: snprintf берёт аргументы по
// счёту. Перестановка роняет игру внутри реплики — и только на русском.
void testTranslationsKeepFormatOrder() {
    std::string bad;
    const bool ok = I18N::formatSpecsConsistent(&bad);
    check(ok, "перевод не переставляет %s и числа",
          ok ? "все точные строки сходятся" : ("разошлось: " + bad.substr(0, 60)));
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
    testRouteEstimateMatchesFlight();
    testSlowCruiseIsCheaper();
    testOptimalBeatsDefaults();
    testRapidityDivergesNearLight();
    testDockedAndLocalFlightAreFree();
    testSpeedLadderIsSmooth();
    testEmergencyStopIsPhysical();
    testAdriftShipRoutesByCoordinates();
    testDriveSlotExclusive();
    testSystemPriceOrder();
    testBuySystemMovesMoneyAndOwner();
    testOwnedMarketIsFreeButFinite();
    testColonyVaultConserves();
    testBeltIsLocalDirt();
    testMiningBurnsReactorFuel();
    testDrillScalesWithReactor();
    testWorldPopulationScalesWithSize();
    testJobPaysLikeARun();
    testJobDeadlinesAreReachable();
    testTakenJobCanExpire();
    testJournalCountsPayoutOnce();
    testJobTierCurveSpansTheHullLadder();
    testFailureCostsMoreThanSuccessGives();
    testReputationUnlocksBiggerJobs();
    testFleetCapacityGatesBigJobs();
    testSaveKeepsReputationAndJournal();
    testPlayerRenamesOwnSystem();
    testStarNamesAreNamesNotIndices();
    testStarNamesSpeakRussianAfterLoad();
    testAdviceIsARealRun();
    testAdviceMeasuresPerYear();
    testAdviceRespectsSurvey();
    testSameSeedSameWorld();
    testInsightBurnsReactorFuel();
    testSaveKeepsTheMarketAlive();
    testChromocoresSurviveHullChange();
    testExoticMarketsAreRare();
    testExoticRunIsAnotherFloor();
    testAntimatterBurnsWithTheFuel();
    testForgePicksTheStat();
    testRefitSurvivesHullAndSave();
    testTranslationsKeepFormatOrder();
    std::printf("\n%s (%d failures)\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures == 0 ? 0 : 1;
}
