// (§51) ПУТЬ НОВИЧКА, сыгранный харнесом.
//
// Урок §42 записан кровью: механики здоровы по отдельности, а вместе не дают
// играть. Значит проверять надо ИГРУ, а не части. Этот пробник проходит путь от
// нищего старта до поздней игры ТЕМИ ЖЕ вызовами, что доступны живому игроку, и
// на каждой вехе печатает три числа: на каком году она взята, каким капиталом и
// что к этому моменту открылось.
//
// ⚠️ Порядок действий тот же, в каком игрока ведёт интерфейс и новелла: именно
// неверный порядок однажды спрятал поломку (§46). Ничего «служебного» пробник не
// делает — ни бесплатных денег, ни телепортов.
//
// В `Makefile` не входит, собирается вручную (строка сборки — night_release_prompt §9).
//
//   ./journey_probe [сид] [звёзд] [лет]

#include "game.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// Веха берётся ОДИН РАЗ и запоминает, чем игрок располагал в этот момент.
struct Milestone {
    const char* name;
    bool taken;
    double year;
    double worth;
    int trips;
    // ⚠️ Явный конструктор, а не агрегат с умолчаниями полей: код на C++11, и
    // фигурная инициализация класса с инициализаторами членов там не аггрегат.
    explicit Milestone(const char* n) : name(n), taken(false), year(-1.0), worth(0.0), trips(0) {}
};

void take(Milestone& m, const Game& g, int trips) {
    if (m.taken) return;
    m.taken = true;
    m.year = g.time;
    m.worth = g.playerNetWorth().total;
    m.trips = trips;
}

void report(const Milestone& m) {
    if (!m.taken) { std::printf("  %-34s НЕ ВЗЯТА\n", m.name); return; }
    std::printf("  %-34s год %8.1f, состояние %10.4g, рейсов %3d\n",
                m.name, m.year, m.worth, m.trips);
}

}  // namespace

int main(int argc, char** argv) {
    const unsigned seed = argc > 1 ? unsigned(std::atoi(argv[1])) : 1234u;
    const int stars = argc > 2 ? std::atoi(argv[2]) : 1200;
    const int maxTrips = argc > 3 ? std::atoi(argv[3]) : 60;

    Game g;
    g.seed = seed;
    g.init(stars);
    // Двадцать лет прогрева: мир должен ожить до того, как игрок в него войдёт,
    // иначе первые рейсы меряют пустые рынки, а не игру.
    for (int y = 0; y < 20; ++y) g.update(1.0);
    const int pa = g.playerAgent;
    strandStatReset();

    const double startYear = g.time;
    const double startMoney = g.agents[size_t(pa)].money;
    std::printf("=== ПУТЬ НОВИЧКА === сид %u, звёзд %d, старт: %.0f Cr, корпус %s\n",
                seed, stars, startMoney, g.agents[size_t(pa)].ship.name.c_str());

    Milestone mFirstProfit("первая прибыль с рейса");
    Milestone mThousand("1 000 Cr (первый оборот)");
    Milestone mLicence("10 000 Cr (цена лицензии)");
    Milestone mSecondHull("второй корпус куплен");
    Milestone mContract("первый заказ взят");
    Milestone mCore("первое хромоядро");
    Milestone mQuota("квота закрыта хоть раз");
    Milestone mExotic("экзотика продана");
    Milestone mSystem("система куплена");

    int trips = 0, refusedDepartures = 0, silentAdvice = 0, adrift = 0;
    int jobsTaken = 0, licences = 0, hulls = 0, systems = 0;
    // (§53) Сколько заказов ОТВЕРГНУТО по мерке §23 (Cr на год полёта против
    // свободного рейса). Число важнее взятых: если отвергнуты все, значит
    // рациональный игрок репутацию не наберёт вообще.
    int jobsRejected = 0;
    double worstWallet = startMoney, peakWorth = 0.0;

    for (; trips < maxTrips; ++trips) {
        const int here = g.agents[size_t(pa)].currentStar;
        if (here < 0) { ++adrift; break; }

        // Шаг игрока 1: осмотреться и заправиться — ровно то, чему учит новелла.
        g.observeStar(here);
        g.agentBuyFuel(pa);

        // Шаг 1б: ЧТО ЕЩЁ ПРЕДЛАГАЕТ ПОРТ. Новелла зовёт игрока в четыре двери
        // помимо торговли — заказы, лицензия с корпусом, экзотика, покупка
        // системы, — и пробник обязан в них стучаться. Иначе он замерит один
        // торговый цикл и объявит игру целой (урок §42).
        {
            // Заказ: берём первый, который здесь висит, влезает в трюм И
            // ОКУПАЕТ ДОРОГУ.
            //
            // ⚠️ (§53) Отбора тут не было вовсе, и это выяснилось, как только
            // пробник начал заказы ВОЗИТЬ, а не только брать: он хватал первый
            // подходящий по трюму — включая цель на другом конце скопления — и
            // упирался в потолок прогона (один рейс за 979 лет).
            //
            // Мерка взята каноническая (§23): плата и срок заказа сравниваются
            // ТОЛЬКО в Cr на ГОД ПОЛЁТА и ТОЛЬКО против свободного рейса,
            // который советчик предлагает отсюда же. Это ровно тот выбор, перед
            // которым стоит живой игрок, и никакого нового числа он не требует.
            const TradeRun freeRun = g.playerBestRun(here, 40, false);
            const double freePerYear = freeRun.valid ? freeRun.perYear : 0.0;
            for (size_t c = 0; c < g.contracts.size(); ++c) {
                const Contract& job = g.contracts[c];
                if (job.originStar != here || job.acceptedByAgent >= 0) continue;
                if (!g.agentContractCargoFits(pa, job.id)) continue;
                const double years = g.agentContractRouteTravelTime(pa, job.id);
                if (!(years > 0.0)) continue;
                const double net = job.reward - g.agentContractRoadCost(pa, job.id);
                if (net / years <= freePerYear) { ++jobsRejected; continue; }
                if (g.agentAcceptContract(pa, job.id)) { ++jobsTaken; take(mContract, g, trips); }
                break;
            }
            // Лицензия и второй корпус: как только кошелёк позволяет. Лицензия
            // расширяет и квоту, и разрешённое число бортов (§21, §24).
            //
            // ⚠️ (§52) ПОРОГ ВЫВЕДЕН, А НЕ ВЗЯТ ИЗ ВОЗДУХА. Раньше здесь стоял
            // «тройной запас», и он же и был ответом пробника: на выходе
            // кошелёк 1.438e6 против цены лицензии 1.02e6 — то есть игрок купить
            // МОГ, а пробник не давал и рапортовал «второй корпус НЕ ВЗЯТ».
            // Правило теперь из самой игры: лицензия без корпуса бесполезна,
            // значит копим на пару «лицензия + самый дешёвый корпус».
            double cheapestHull = 0.0;
            {
                const std::vector<ShipClass>& classes = shipClasses();
                for (size_t c = 0; c < classes.size(); ++c) {
                    if (classes[c].price <= 0.0) continue;
                    if (cheapestHull <= 0.0 || classes[c].price < cheapestHull) cheapestHull = classes[c].price;
                }
            }
            if (g.agents[size_t(pa)].money >= g.licencePrice() + cheapestHull && g.playerBuyLicence()) ++licences;
            if (g.playerShipCount() < g.licence().count) {
                const std::vector<ShipClass>& classes = shipClasses();
                for (size_t c = classes.size(); c-- > 0; ) {
                    if (classes[c].price <= 0.0 || classes[c].price * 3.0 > g.agents[size_t(pa)].money) continue;
                    if (g.buyAdditionalShip(pa, here, int(c))) ++hulls;
                    break;
                }
            }
            // Своя система — поздняя игра (§13, §22): цена квадратична.
            if (g.playerBuySystem()) ++systems;
        }

        // Шаг 2: КУДА ЛЕТИМ. ⚠️ (§53) Раньше здесь всегда стоял торговый
        // советчик, и принятый заказ оставался лежать мёртвым грузом: борт летел
        // не туда, куда заказ указывает, и не сдавал НИ ОДНОГО из 27. Отсюда шла
        // «репутация суммой 0.0», по которой §51 похоронил всю ветку репутации.
        //
        // Порядок теперь тот же, что у живого игрока: взял заказ — вези заказ, а
        // торговлю пристраивай ПОД этот маршрут. Свободен — слушай советчика.
        int haulContract = -1;
        int destStar = -1;
        for (size_t c = 0; c < g.contracts.size(); ++c) {
            const Contract& job = g.contracts[c];
            if (job.acceptedByAgent != pa || job.completed || job.failed) continue;
            if (job.targetStar < 0 || job.targetStar >= int(g.cluster.stars.size())) continue;
            haulContract = job.id;
            destStar = job.targetStar;
            break;
        }

        TradeRun run = g.playerBestRun(here, 40, false);
        if (destStar < 0) {
            if (!run.valid || run.element < 0 || run.targetStar < 0) { ++silentAdvice; break; }
            destStar = run.targetStar;
        }

        const double before = g.playerNetWorth().total;
        g.setAgentDestination(pa, destStar);
        // Груз заказа уже в трюме (его кладёт `agentAcceptContract`), поэтому
        // докупаем только если советчик указывает В ТУ ЖЕ систему — иначе на
        // сдаче не хватит места под сам заказ.
        if (run.valid && run.element >= 0 && run.targetStar == destStar) {
            g.agentBuyElementAmount(pa, run.element, 1.0e12);
        }
        if (!g.commandAgentToStar(pa, destStar)) { ++refusedDepartures; break; }
        (void)haulContract;

        // Шаг игры 0.1: мельче годового, чтобы расход сходился с оценкой (§48.9).
        for (int s = 0; s < 8000 && g.agents[size_t(pa)].ship.enRoute; ++s) g.update(0.1);
        if (g.agents[size_t(pa)].ship.enRoute) break;

        g.agentSellAllCargo(pa);
        // ⚠️ (§52) СДАЧА ЗАКАЗА, КОТОРОЙ ЗДЕСЬ НЕ БЫЛО. Пробник брал заказы (28
        // за прогон) и не сдавал НИ ОДНОГО — репутация выходила ровно 0.0, а по
        // ней в игре считается тир, масса и ставка заказа. Из-за этого пробник
        // объявлял мёртвой всю ветку репутации, хотя молчал его собственный
        // круг: взял — довёз — СДАЛ. Сдаём всё, что довезли до этой звезды.
        g.agentCompleteContracts(pa);

        const double after = g.playerNetWorth().total;
        const double wallet = g.agents[size_t(pa)].money;
        if (wallet < worstWallet) worstWallet = wallet;
        if (after > peakWorth) peakWorth = after;

        if (after > before) take(mFirstProfit, g, trips + 1);
        if (wallet >= 1000.0) take(mThousand, g, trips + 1);
        if (wallet >= 10000.0) take(mLicence, g, trips + 1);
        if (g.playerShipCount() > 1) take(mSecondHull, g, trips + 1);
        if (g.tech.cores > 0) take(mCore, g, trips + 1);
        if (g.licence().periodsMet > 0) take(mQuota, g, trips + 1);
        if (g.exoticUnitsSold > 0.0) take(mExotic, g, trips + 1);
        if (g.boughtSystems > 0) take(mSystem, g, trips + 1);
    }

    std::printf("\nрейсов сделано %d за %.1f года; состояние %.4g (пик %.4g), худший кошелёк %.0f Cr\n",
                trips, g.time - startYear, g.playerNetWorth().total, peakWorth, worstWallet);
    std::printf("сорвалось: вылет отказан %d, советчик молчал %d, ушёл в дрейф %d\n",
                refusedDepartures, silentAdvice, adrift);
    std::printf("двери, в которые стучались: заказов взято %d (отвергнуто по Cr/год %d), лицензий %d, корпусов %d, систем %d\n",
                jobsTaken, jobsRejected, licences, hulls, systems);
    // Почему дверь не открылась: цена против кошелька на последнем шаге.
    std::printf("на выходе: кошелёк %.4g Cr; лицензия стоит %.4g; лицензий у игрока %d, бортов %d\n",
                g.agents[size_t(pa)].money, g.licencePrice(), g.licence().count, g.playerShipCount());
    double repTotal = 0.0;
    for (size_t f = 0; f < g.factionReputation.size(); ++f) repTotal += g.factionReputation[f];
    std::printf("репутация суммой %.1f; ядер %d; квота закрыта раз %d; систем %d; рынков разведано %d\n",
                repTotal, g.tech.cores, g.licence().periodsMet, g.boughtSystems,
                g.playerSurveyedMarketCount());

    std::printf("\n--- ВЕХИ ---\n");
    report(mFirstProfit);
    report(mThousand);
    report(mLicence);
    report(mContract);
    report(mSecondHull);
    report(mCore);
    report(mQuota);
    report(mExotic);
    report(mSystem);
    return 0;
}
