// (§48.8, ВРЕМЕННЫЙ ЗАМЕР) Повторяет testAdvisorRunsCompound и печатает, на чём
// именно встаёт партия: отказ вылета, застревание, буксир.

#include "game.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const unsigned seed = argc > 1 ? unsigned(std::atoi(argv[1])) : 1234u;
    const double dt = argc > 2 ? std::atof(argv[2]) : 1.0;

    Game g;
    g.seed = seed;
    g.init(1200);
    for (int y = 0; y < 20; ++y) g.update(1.0);
    const int pa = g.playerAgent;
    strandStatReset();

    for (int trip = 0; trip < 25; ++trip) {
        const int here = g.agents[size_t(pa)].currentStar;
        if (here < 0) { std::printf("рейс %d: борт НЕ В СИСТЕМЕ (дрейф)\n", trip); break; }
        g.observeStar(here);
        g.agentBuyFuel(pa);
        const TradeRun r = g.playerBestRun(here, 40, false);
        if (!r.valid || r.element < 0 || r.targetStar < 0) { std::printf("рейс %d: совет молчит\n", trip); break; }
        g.setAgentDestination(pa, r.targetStar);
        g.agentBuyElementAmount(pa, r.element, 1.0e12);
        if (!g.commandAgentToStar(pa, r.targetStar)) {
            std::printf("рейс %d: ВЫЛЕТ ОТКАЗАН (%s), кошелёк %.0f, топливо %.2f, РТ %.2f\n",
                        trip, g.agents[size_t(pa)].lastAction.c_str(), g.agents[size_t(pa)].money,
                        shipFuelMix(g.agents[size_t(pa)].ship).mass,
                        shipPropellantMix(g.agents[size_t(pa)].ship).mass);
            break;
        }
        const long long towsBefore = strandStatTows();
        int steps = 0;
        for (; steps < int(800.0 / dt) && g.agents[size_t(pa)].ship.enRoute; ++steps) g.update(dt);
        const bool towed = strandStatTows() > towsBefore;
        g.agentSellAllCargo(pa);
        std::printf("рейс %2d: %s%s, состояние %.4g\n", trip,
                    g.agents[size_t(pa)].ship.enRoute ? "НЕ ДОЛЕТЕЛ за 800 лет" : "дошёл",
                    towed ? " (ВЫТАЩЕН БУКСИРОМ)" : "", g.playerNetWorth().total);
        if (g.agents[size_t(pa)].ship.enRoute) break;
    }
    std::printf("шаг %.2f года: буксиров %lld, вылетов впритык %lld\n",
                dt, strandStatTows(), strandStatRiskyDepartures());
    return 0;
}
