// (§52) ГДЕ ИМЕННО СЕЙВ ТЕРЯЕТ МИР.
//
// `save_probe` отвечает «сошлось / разошлось», но не говорит где. Этот пробник
// половинит за нас двумя приёмами сразу:
//
//  1. Сохранить мир -> загрузить -> сохранить загруженную копию. Два текста
//     обязаны совпасть побайтно; любая разошедшаяся строка — поле, которое
//     круг не донёс. Это ловит всё СОХРАНЯЕМОЕ и стоит одну загрузку.
//  2. Покрутить обе копии на несколько лет и сравнить их сейвы снова. Это
//     ловит НЕСОХРАНЯЕМОЕ: состояние, которого в файле нет вовсе, проявляется
//     только в будущем (§51.5).
//
// В `Makefile` не входит, собирается вручную.
//
//   ./save_diff [звёзд] [лет до сейва] [лет после] [сид]

#include "game.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path.c_str());
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    return lines;
}

// Первые несколько расхождений с номерами строк: дальше первого обычно всё
// съезжает следом, поэтому важна именно ГОЛОВА списка.
int diff(const char* tag, const std::string& a, const std::string& b, int show) {
    const std::vector<std::string> la = readLines(a);
    const std::vector<std::string> lb = readLines(b);
    int shown = 0, total = 0;
    const size_t n = la.size() < lb.size() ? la.size() : lb.size();
    for (size_t i = 0; i < n; ++i) {
        if (la[i] == lb[i]) continue;
        ++total;
        if (shown < show) {
            std::printf("  строка %zu\n    A: %.150s\n    B: %.150s\n", i + 1,
                        la[i].c_str(), lb[i].c_str());
            ++shown;
        }
    }
    if (la.size() != lb.size()) {
        std::printf("  ⚠️  разная длина: %zu против %zu строк\n", la.size(), lb.size());
    }
    std::printf("%s: расхождений %d\n", tag, total);
    return total;
}

// Сравнение В ПАМЯТИ: файл показывает только сохраняемое, а расходится как раз
// то, чего в файле нет. Считаем расхождения по каждому массиву отдельно.
int reportVec(const char* name, const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) {
        std::printf("  %-22s размеры %zu против %zu\n", name, a.size(), b.size());
        return 1;
    }
    int bad = 0;
    size_t first = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) { if (!bad) first = i; ++bad; }
    }
    if (bad) {
        std::printf("  %-22s разошлось %d из %zu, первое [%zu]: %.17g против %.17g\n",
                    name, bad, a.size(), first, a[first], b[first]);
    }
    return bad;
}

int compareMemory(Game& a, Game& b) {
    int bad = 0;

    if (a.routeNextHop != b.routeNextHop) { std::printf("  кэш маршрутов разошёлся\n"); ++bad; }

    // Рынки: то, что сейв не пишет вовсе.
    int needsBad = 0, prefBad = 0, rationBad = 0, accessBad = 0, scaleBad = 0;
    int strainBad = 0, serviceBad = 0, eventBad = 0, noiseBad = 0;
    for (size_t i = 0; i < a.markets.size() && i < b.markets.size(); ++i) {
        const Market& x = a.markets[i];
        const Market& y = b.markets[i];
        if (x.needs != y.needs) ++needsBad;
        if (x.pref != y.pref) ++prefBad;
        if (x.rationing != y.rationing) ++rationBad;
        if (x.tradeAccess != y.tradeAccess) ++accessBad;
        if (x.seededScale != y.seededScale) ++scaleBad;
        if (x.strain != y.strain) ++strainBad;
        if (x.serviceCostAvg != y.serviceCostAvg) ++serviceBad;
        if (x.eventMul != y.eventMul) ++eventBad;
        if (x.demandNoise != y.demandNoise) ++noiseBad;
    }
    const char* names[] = {"needs", "pref", "rationing", "tradeAccess", "seededScale",
                           "strain", "serviceCostAvg", "eventMul", "demandNoise"};
    const int counts[] = {needsBad, prefBad, rationBad, accessBad, scaleBad,
                          strainBad, serviceBad, eventBad, noiseBad};
    for (int k = 0; k < 9; ++k) {
        if (counts[k]) { std::printf("  рынок.%-16s разошёлся на %d рынках\n", names[k], counts[k]); bad += counts[k]; }
    }

    // Знание держав: в файл идёт РАЗРЕЖЕННО, поэтому в памяти его и проверяем.
    if (a.factionKnowledge.size() == b.factionKnowledge.size()) {
        int owners = 0;
        for (size_t i = 0; i < a.factionKnowledge.size(); ++i) {
            const FactionStarKnowledge& x = a.factionKnowledge[i];
            const FactionStarKnowledge& y = b.factionKnowledge[i];
            if (x.ownerKnown != y.ownerKnown || x.ownerFaction != y.ownerFaction ||
                x.ownerKnownAt != y.ownerKnownAt || x.visited != y.visited) ++owners;
        }
        if (owners) { std::printf("  знание владельца разошлось в %d записях\n", owners); bad += owners; }
    } else { std::printf("  знание владельца: разные размеры\n"); ++bad; }

    if (a.factionMarketKnowledge.size() == b.factionMarketKnowledge.size()) {
        int mk = 0;
        for (size_t i = 0; i < a.factionMarketKnowledge.size(); ++i) {
            const FactionMarketKnowledge& x = a.factionMarketKnowledge[i];
            const FactionMarketKnowledge& y = b.factionMarketKnowledge[i];
            if (x.known != y.known || x.observedAt != y.observedAt ||
                x.averageSupplyPressure != y.averageSupplyPressure ||
                x.averageDemandPressure != y.averageDemandPressure) ++mk;
        }
        if (mk) { std::printf("  знание рынка разошлось в %d записях\n", mk); bad += mk; }
    } else { std::printf("  знание рынка: разные размеры\n"); ++bad; }

    bad += reportVec("знание.цены", a.factionMarketPrices, b.factionMarketPrices);
    bad += reportVec("знание.предложение", a.factionMarketSupplyPressure, b.factionMarketSupplyPressure);
    bad += reportVec("знание.спрос", a.factionMarketDemandPressure, b.factionMarketDemandPressure);

    if (a.agents.size() == b.agents.size()) {
        int mission = 0, cargoCost = 0;
        for (size_t i = 0; i < a.agents.size(); ++i) {
            if (a.agents[i].missionCooldown != b.agents[i].missionCooldown) ++mission;
            if (a.agents[i].cargoCost != b.agents[i].cargoCost) ++cargoCost;
        }
        if (mission) { std::printf("  борт.missionCooldown разошёлся у %d\n", mission); bad += mission; }
        if (cargoCost) { std::printf("  борт.cargoCost разошёлся у %d\n", cargoCost); bad += cargoCost; }
    }

    std::printf("в памяти: расхождений %d\n", bad);
    return bad;
}

}  // namespace

int main(int argc, char** argv) {
    const int stars = argc > 1 ? std::atoi(argv[1]) : 600;
    const int before = argc > 2 ? std::atoi(argv[2]) : 20;
    // Горизонт дробный: первое расхождение бывает и через сотую года, а
    // целыми годами его не поймать — к тому времени разошлось уже всё.
    const double after = argc > 3 ? std::atof(argv[3]) : 10.0;
    const unsigned seed = argc > 4 ? unsigned(std::atoi(argv[4])) : 20260814u;

    Game g;
    g.seed = seed;
    g.init(size_t(stars));
    for (int s = 0; s < before * 100; ++s) g.update(0.01);

    const std::string a = "/tmp/sc_diff_a.sav";
    const std::string b = "/tmp/sc_diff_b.sav";
    const std::string a2 = "/tmp/sc_diff_a2.sav";
    const std::string b2 = "/tmp/sc_diff_b2.sav";

    if (!g.saveToFile(a)) { std::printf("сейв не записался\n"); return 1; }
    Game loaded;
    if (!loaded.loadFromFile(a)) { std::printf("загрузка не удалась\n"); return 1; }
    if (!loaded.saveToFile(b)) { std::printf("второй сейв не записался\n"); return 1; }

    std::printf("=== круг сохранение->загрузка (%d звёзд, год %.2f) ===\n", stars, g.time);
    const int atLoad = diff("в момент загрузки", a, b, 8) + compareMemory(g, loaded);

    // ⚠️ (§52) Копии крутятся ПООЧЕРЁДНО с возвратом генератора: `rng` — глобал
    // процесса, один на все миры. Вперемешку они расходятся всегда (см.
    // разбор в `save_probe.cpp`).
    const int afterSteps = int(after * 100.0 + 0.5);
    const std::mt19937 rngAfterLoad = rng;
    for (int s = 0; s < afterSteps; ++s) g.update(0.01);
    rng = rngAfterLoad;
    for (int s = 0; s < afterSteps; ++s) loaded.update(0.01);
    if (!g.saveToFile(a2) || !loaded.saveToFile(b2)) { std::printf("сейв после прокрутки не записался\n"); return 1; }
    std::printf("=== через %.2f лет ===\n", after);
    const int later = diff("после прокрутки", a2, b2, 8);

    return (atLoad == 0 && later == 0) ? 0 : 1;
}
