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
    for (int s = 0; s < steps; ++s) g.update(dt);

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
    return 0;
}
