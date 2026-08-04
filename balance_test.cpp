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
    std::printf("\n%s (%d failures)\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures == 0 ? 0 : 1;
}
