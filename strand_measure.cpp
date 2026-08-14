// (§48, ВРЕМЕННЫЙ ЗАМЕР — не часть игры, удаляется после ответа)
//
// Вопрос один: как часто корабль доезжает до звезды, НЕ ИМЕЯ чем тормозить?
// Сегодня `moveShipToward` при пролёте мимо цели ставит борт в звезду и обнуляет
// скорость даром. Если убрать это прощение (чтобы борта ИИ могли застревать
// по-настоящему), каждое такое прибытие станет застреванием. Доля таких
// прибытий и есть будущая частота сигналов бедствия.

#include "game.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

int main(int argc, char** argv) {
    const int stars = argc > 1 ? std::atoi(argv[1]) : 1200;
    const int years = argc > 2 ? std::atoi(argv[2]) : 2000;
    const unsigned seed = argc > 3 ? unsigned(std::atoi(argv[3])) : 20260813u;
    // Шаг ИГРЫ, а не харнеса: main.cpp дробит время на MAX_SIM_STEP_YEARS = 0.01.
    // На шаге в год корабль пролетает мимо звезды просто от грубого интегрирования.
    const double dt = argc > 4 ? std::atof(argv[4]) : 0.01;

    Game g;
    g.seed = seed;
    g.init(stars);
    strandStatReset();
    strandStatTrace(true);

    const int steps = int(double(years) / dt + 0.5);
    // (§50) Заодно меряем ЦЕНУ ТАКТА: бюджет года симуляции — 160 мс на полное
    // скопление (README), и спасение трогает горячий путь `updateAgents`.
    const clock_t t0 = std::clock();
    for (int s = 0; s < steps; ++s) g.update(dt);
    const double msPerYear = 1000.0 * double(std::clock() - t0) / double(CLOCKS_PER_SEC) / double(years);

    const long long total = strandStatArrivals();
    const long long over = strandStatOvershoots();
    const long long dry = strandStatDryArrivals();
    int drifting = 0, enRoute = 0;
    for (size_t i = 0; i < g.agents.size(); ++i) {
        if (g.agents[i].ship.enRoute) ++enRoute;
        if (g.agents[i].ship.enRoute && g.agents[i].ship.targetStar == -2) ++drifting;
    }

    std::printf("сид %u, звёзд %d, лет %d, шаг %.3f года, бортов %d (в пути %d, дрейфует %d)\n",
                seed, stars, years, dt, int(g.agents.size()), enRoute, drifting);
    std::printf("ТАКТ ГОДА               %.1f мс  (бюджет 160 мс на 8192 звезды)\n", msPerYear);
    std::printf("вылетов впритык (нищета) %lld  (%.3f в год)\n",
                strandStatRiskyDepartures(), double(strandStatRiskyDepartures()) / years);
    std::printf("СПАСЕНИЙ (буксир)        %lld  (одно на %.1f лет)\n",
                strandStatTows(), strandStatTows() ? double(years) / double(strandStatTows()) : 0.0);
    std::printf("прибытий всего          %lld  (%.3f в год)\n", total, double(total) / years);
    std::printf("  с пролётом мимо цели  %lld  (%.2f%% прибытий)\n",
                over, total ? 100.0 * double(over) / double(total) : 0.0);
    std::printf("  из них СУХИХ          %lld  (%.3f%% прибытий, одно на %.0f лет)\n",
                dry, total ? 100.0 * double(dry) / double(total) : 0.0,
                dry ? double(years) / double(dry) : 0.0);
    std::printf("  с остатком >0.01c     %lld  (%.2f%% прибытий)\n",
                strandStatFastArrivals(),
                total ? 100.0 * double(strandStatFastArrivals()) / double(total) : 0.0);
    std::printf("расчёт против расхода: сожжено/обещано %.2f в среднем, %.2f худшее\n",
                strandStatBurnRatio(), strandStatBurnRatioMax());
    std::printf("сожжено / расход по фактической массе %.2f  (1.00 = полёт честен, врёт только оценка)\n",
                strandStatHonestRatio());
    std::printf("быстрота: выдано/заложено %.2f в среднем, %.2f худшее\n",
                strandStatRapidityRatio(), strandStatRapidityMax());
    std::printf("у сухих было залито %.2f от обещанного расчётом — и всё равно не хватило\n",
                strandStatDryReserve());
    std::printf("худший остаток: у сухих %.4f c, вообще %.4f c\n",
                strandStatWorstSpeed(), strandStatWorstAny());

    // (§50) Спасение как механика: из чего складывается ожидание и чем всё
    // кончается. `последний рубеж` — сколько раз сработал TOW_WAIT_YEARS, то
    // есть сколько раз физика НЕ справилась. Если он не ноль — сломана физика.
    int beaconsOpen = 0;
    double oldestBeacon = 0.0;
    for (size_t b = 0; b < g.distress.size(); ++b) {
        ++beaconsOpen;
        const double age = g.time - g.distress[b].raisedTime;
        if (age > oldestBeacon) oldestBeacon = age;
    }
    std::printf("\n--- СПАСЕНИЕ (§50) ---\n");
    std::printf("маяков поднято          %lld  (одно на %.1f лет)\n",
                strandStatBeacons(), strandStatBeacons() ? double(years) / double(strandStatBeacons()) : 0.0);
    std::printf("вытащено живым бортом   %lld\n", strandStatLiveRescues());
    std::printf("вытащено казной         %lld\n", strandStatStateRescues());
    std::printf("ПОСЛЕДНИЙ РУБЕЖ         %lld  (сработал TOW_WAIT_YEARS = %.0f лет)\n",
                strandStatLastResort(), TOW_WAIT_YEARS);
    std::printf("обобрано мародёрами     %lld\n", strandStatLoots());
    std::printf("ожидание: среднее %.2f года, худшее %.2f года; из него ход света %.2f года\n",
                strandStatWaitAverage(), strandStatWaitWorst(), strandStatLightAverage());
    std::printf("маяков в эфире сейчас   %d  (старейшему %.1f года)\n", beaconsOpen, oldestBeacon);

    // Разбор ЗАВИСШИХ маяков: без него видно только «не спасли», а надо знать
    // почему — некому лететь, не догоняет или спасатель сам умер по дороге.
    int shown = 0;
    for (size_t b = 0; b < g.distress.size() && shown < 8; ++b, ++shown) {
        const DistressBeacon& d = g.distress[b];
        const Ship& v = g.agents[size_t(d.agent)].ship;
        const double drift = std::sqrt(v.vx * v.vx + v.vy * v.vy + v.vz * v.vz);
        const double dx = v.x - d.x, dy = v.y - d.y, dz = v.z - d.z;
        const double gone = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d.responder >= 0) {
            const Agent& r = g.agents[size_t(d.responder)];
            const double rx = v.x - r.ship.x, ry = v.y - r.ship.y, rz = v.z - r.ship.z;
            std::printf("  маяк %2d: возраст %6.1f, дрейф %.3fc, ушёл %6.2f ly; спасатель #%d (%s) "
                        "потолок %.3fc, знает=%d, до цели %6.2f ly\n",
                        int(b), g.time - d.raisedTime, drift, gone, d.responder, r.type.c_str(),
                        r.ship.speed, r.rescueKnows ? 1 : 0, std::sqrt(rx * rx + ry * ry + rz * rz));
        } else {
            std::printf("  маяк %2d: возраст %6.1f, дрейф %.3fc, ушёл %6.2f ly; СПАСАТЕЛЯ НЕТ "
                        "(казна слала=%d, driftYears=%.1f, cooldown=%.1f, targetStar=%d)\n",
                        int(b), g.time - d.raisedTime, drift, gone, d.stateSent ? 1 : 0,
                        g.agents[size_t(d.agent)].driftYears, g.agents[size_t(d.agent)].missionCooldown,
                        v.targetStar);
        }
    }
    return 0;
}
