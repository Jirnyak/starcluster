// (§48, ВРЕМЕННЫЙ ЗАМЕР) Повторяет testBoardHonest и печатает, ПОЧЕМУ у него
// ноль проверок: сводка на стартовом кошельке в одном прыжке.

#include "game.h"

#include <cstdio>

int main() {
    const unsigned seeds[] = {42u, 7u, 2024u, 999u};
    for (int s = 0; s < 4; ++s) {
        Game g;
        g.seed = seeds[s];
        g.init(1200);
        for (int y = 0; y < 20; ++y) g.update(1.0);
        for (int i = 0; i < int(g.cluster.stars.size()) && i < 80; ++i) {
            g.observeMarketForFaction(g.playerFaction, i);
        }
        const int pa = g.playerAgent;
        const int home = g.agents[size_t(pa)].currentStar;
        const double purses[] = {100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0};
        std::printf("сид %4u:", seeds[s]);
        for (int w = 0; w < 6; ++w) {
            g.agents[size_t(pa)].money = purses[w];
            const std::vector<ArbitrageDeal> b = g.playerArbitrageBoard(home, 40, -1);
            double bp = -1e300; int pos = 0;
            for (size_t i = 0; i < b.size(); ++i) {
                if (b[i].profit > bp) bp = b[i].profit;
                if (b[i].profit > 0.0) ++pos;
            }
            std::printf("  %.0fCr: %+.0f (%d)", purses[w], bp, pos);
        }
        std::printf("\n");
        g.agents[size_t(pa)].money = 100.0;
        continue;
        const std::vector<ArbitrageDeal> board = g.playerArbitrageBoard(home, 40, -1);
        std::printf("сид %4u: кошелёк %.0f, крейсер %.2f, сделок %d",
                    seeds[s], g.agents[size_t(pa)].money,
                    g.agents[size_t(pa)].ship.cruiseFraction, int(board.size()));
        if (!board.empty()) {
            const ArbitrageDeal& d = board[0];
            std::printf("; лучшая: чистыми %.1f, дорога %.1f, валом %.1f, доверие %.2f",
                        d.profit, d.fuelCost, d.profit + d.fuelCost, d.confidence);
        }
        double bestProfit = -1e300; int positives = 0;
        for (size_t i = 0; i < board.size(); ++i) {
            if (board[i].profit > bestProfit) bestProfit = board[i].profit;
            if (board[i].profit > 0.0 && board[i].confidence >= 0.5) ++positives;
        }
        if (!board.empty()) std::printf("; лучшая ПО ПРИБЫЛИ %.1f, прибыльных %d из %d",
                                        bestProfit, positives, int(board.size()));
        std::printf("\n");
    }
    return 0;
}
