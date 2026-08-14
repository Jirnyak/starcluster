// (§52) РАЗБРОС МЕСТНОЙ ЦЕНЫ УСЛУГИ ПО СКОПЛЕНИЮ.
//
// Ремонт привязывается к `market.serviceCostAvg`, нормированной на денежный
// уровень партии (`ECON_CREDITS_PER_SERVICE * clusterPriceLevel`). Прежде чем
// ставить на это цену, надо знать РАЗБРОС: если у части портов отношение
// уходит в сотые или в десятки, ремонт станет то дармовым, то неподъёмным — а
// неподъёмный ремонт запирает игру (§42).
//
// Печатает перцентили отношения по обитаемым системам. В `Makefile` не входит.
//
//   ./repair_probe [звёзд] [лет] [сид]

#include "game.h"
#include "econ.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    const int stars = argc > 1 ? std::atoi(argv[1]) : 8192;
    const int years = argc > 2 ? std::atoi(argv[2]) : 150;
    const unsigned seed = argc > 3 ? unsigned(std::atoi(argv[3])) : 20260814u;

    Game g;
    g.seed = seed;
    g.init(size_t(stars));
    for (int s = 0; s < years * 100; ++s) g.update(0.01);

    const double anchor = ECON_CREDITS_PER_SERVICE * g.clusterPriceLevel;
    std::vector<double> ratios;
    int silent = 0;
    for (size_t i = 0; i < g.markets.size(); ++i) {
        if (i >= g.cluster.stars.size() || g.cluster.stars[i].population <= 0.0) continue;
        const double cost = g.markets[i].serviceCostAvg;
        if (cost <= 0.0) { ++silent; continue; }
        ratios.push_back(cost / anchor);
    }
    std::sort(ratios.begin(), ratios.end());

    std::printf("=== %d звёзд, %d лет, сид %u ===\n", stars, years, seed);
    std::printf("уровень цен партии %.4f, якорь услуги %.4f Cr\n", g.clusterPriceLevel, anchor);
    std::printf("обитаемых с рынком %d, из них без цены услуги %d\n", int(ratios.size()), silent);
    if (ratios.empty()) return 1;

    const double pct[] = {0.0, 0.01, 0.05, 0.25, 0.5, 0.75, 0.95, 0.99, 1.0};
    for (int k = 0; k < 9; ++k) {
        size_t idx = size_t(pct[k] * double(ratios.size() - 1));
        std::printf("  %4.0f%%  %.4f\n", pct[k] * 100.0, ratios[idx]);
    }
    return 0;
}
