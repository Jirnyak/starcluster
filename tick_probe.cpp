// (§50) ЦЕНА ТАКТА — отдельным пробником, потому что мерить её было нечем.
//
// Бюджет README — 160 мс на год симуляции при 8192 звёздах, и до этого раздела
// число просто передавалось из захода в заход. В `Makefile` пробник НЕ входит,
// собирается вручную (строка сборки — в night_release_prompt.md §9).
//
// ⚠️ Число МАШИННО-ЗАВИСИМОЕ, и сравнивать можно только «до и после» на одной
// машине и на одинаковом горизонте лет. Замер §50: тот же код на 100 годах даёт
// 169.6 мс, на 200 — 158.3 мс, потому что мир со временем меняет состав (флот
// держав растёт, дороги удлиняются). Повторы стабильны в пределах 1 мс, так что
// разница между прогонами — это мир, а не шум.
//
//   ./tick_probe [звёзд] [лет] [сид] [шаг]

#include "game.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

int main(int argc, char** argv) {
    const int stars = argc > 1 ? std::atoi(argv[1]) : 8192;
    const int years = argc > 2 ? std::atoi(argv[2]) : 200;
    const unsigned seed = argc > 3 ? unsigned(std::atoi(argv[3])) : 20260814u;
    // Шаг ИГРЫ: main.cpp дробит время на MAX_SIM_STEP_YEARS = 0.01.
    const double dt = argc > 4 ? std::atof(argv[4]) : 0.01;

    Game g;
    g.seed = seed;
    g.init(stars);

    const int steps = int(double(years) / dt + 0.5);
    const clock_t t0 = std::clock();
    for (int s = 0; s < steps; ++s) g.update(dt);
    const double ms = 1000.0 * double(std::clock() - t0) / double(CLOCKS_PER_SEC) / double(years);

    std::printf("звёзд %d, лет %d, сид %u, шаг %.3f\n", stars, years, seed, dt);
    std::printf("ТАКТ ГОДА %.1f мс  (бюджет README 160 мс), бортов %d\n", ms, int(g.agents.size()));
    return 0;
}
