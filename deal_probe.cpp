// (§48, ВРЕМЕННЫЙ ЗАМЕР) Из чего складывается рейс бедного торговца.
// Печатает лучшую сделку сводки в кредитах: валовую разницу, цену дороги и что
// остаётся. Нужен, чтобы решать про баланс числами, а не множителями.

#include "game.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>

int main(int argc, char** argv) {
    const double purse = argc > 1 ? std::atof(argv[1]) : 400.0;
    const double cruise = argc > 2 ? std::atof(argv[2]) : 1.0;

    Game g;
    g.seed = 42u;
    g.init(1200);
    for (int y = 0; y < 20; ++y) g.update(1.0);
    for (int i = 0; i < int(g.cluster.stars.size()) && i < 80; ++i) {
        g.observeMarketForFaction(g.playerFaction, i);
    }

    {   // Крейсерская доля борта игрока: лететь медленнее объективно дешевле (§12).
        Ship& ps = g.agents[size_t(g.playerAgent)].ship;
        ps.cruiseFraction = cruise;
        shipTuneDrive(ps, 1.0, 1.0);
    }
    std::printf("кошелёк %.0f Cr, крейсер %.2f\n", purse, cruise);
    std::printf("%-8s %12s %12s %12s %10s\n", "старт", "валом", "дорога", "чистыми", "доля дороги");
    int empty = 0, total = 0;
    double sumGross = 0.0, sumRoad = 0.0;
    for (int o = 0; o < 12; ++o) {
        const int origin = (o * 7919) % 80;
        g.agents[size_t(g.playerAgent)].money = purse;
        const std::vector<ArbitrageDeal> board = g.playerArbitrageBoard(origin, 40, -1);
        if (board.empty()) { ++empty; ++total; continue;}
        const ArbitrageDeal* best = &board[0];
        for (size_t i = 1; i < board.size(); ++i) if (board[i].profit > best->profit) best = &board[i];
        const double gross = best->profit + best->fuelCost;
        ++total;
        sumGross += gross; sumRoad += best->fuelCost;
        if (best->profit <= 0.0) ++empty;
        std::printf("%-8d %12.1f %12.1f %12.1f %9.0f%%\n",
                    origin, gross, best->fuelCost, best->profit,
                    gross > 0.0 ? 100.0 * best->fuelCost / gross : 0.0);
    }
    std::printf("\nбез прибыли %d из %d; валом в среднем %.0f, дорога %.0f (%.0f%%)\n",
                empty, total, sumGross / total, sumRoad / total,
                sumGross > 0.0 ? 100.0 * sumRoad / sumGross : 0.0);
    return 0;
}
