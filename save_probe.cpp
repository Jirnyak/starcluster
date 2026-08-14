// (§51) ПОЛНЫЙ МИР РУКАМИ: старт -> игра -> сейв -> загрузка -> продолжение.
//
// Релизный чеклист требует пройти этот круг на НАСТОЯЩЕМ мире (8192 звезды), а
// не на харнесном огрызке: сейв версии 20 несёт маяки бедствия, состояние
// спасателей и годы дрейфа, и все они появляются только там, где кто-то реально
// бедствует. В `Makefile` не входит, собирается вручную.
//
//   ./save_probe [звёзд] [лет до сейва] [лет после] [сид]

#include "game.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// Отпечаток мира: если после круга «сохранить -> загрузить» он совпал, значит
// сейв донёс всё, что игра потом читает.
struct Fingerprint {
    double time;
    size_t agents;
    size_t beacons;
    double playerMoney;
    double treasury;
    double driftSum;
    int rescuers;
};

Fingerprint take(const Game& g) {
    Fingerprint f;
    f.time = g.time;
    f.agents = g.agents.size();
    f.beacons = g.distress.size();
    f.playerMoney = g.playerAgent >= 0 ? g.agents[size_t(g.playerAgent)].money : 0.0;
    f.treasury = g.clearingFaction >= 0 && g.clearingFaction < int(g.factions.size())
        ? g.factions[size_t(g.clearingFaction)].treasury : 0.0;
    f.driftSum = 0.0;
    f.rescuers = 0;
    for (size_t i = 0; i < g.agents.size(); ++i) {
        f.driftSum += g.agents[i].driftYears;
        if (g.agents[i].rescueTarget >= 0) ++f.rescuers;
    }
    return f;
}

void show(const char* tag, const Fingerprint& f) {
    std::printf("%-10s год %9.2f, бортов %4d, маяков %2d, кошелёк %.6g, казна %.6g, "
                "дрейфа суммой %.4f, спасателей в пути %d\n",
                tag, f.time, int(f.agents), int(f.beacons), f.playerMoney, f.treasury,
                f.driftSum, f.rescuers);
}

bool same(const Fingerprint& a, const Fingerprint& b) {
    return a.time == b.time && a.agents == b.agents && a.beacons == b.beacons &&
           a.playerMoney == b.playerMoney && a.treasury == b.treasury &&
           std::fabs(a.driftSum - b.driftSum) < 1e-9 && a.rescuers == b.rescuers;
}

}  // namespace

int main(int argc, char** argv) {
    const int stars = argc > 1 ? std::atoi(argv[1]) : 8192;
    const int before = argc > 2 ? std::atoi(argv[2]) : 120;
    const int after = argc > 3 ? std::atoi(argv[3]) : 40;
    const unsigned seed = argc > 4 ? unsigned(std::atoi(argv[4])) : 20260814u;
    const std::string path = "/tmp/starcluster_release_check.sav";

    Game g;
    g.seed = seed;
    g.init(stars);
    std::printf("=== ПОЛНЫЙ МИР === %d звёзд, сид %u\n", stars, seed);

    // Играем до сейва шагом ИГРЫ: на грубом шаге беды не случается вовсе, а
    // именно её и надо провести через сейв.
    for (int s = 0; s < before * 100; ++s) g.update(0.01);
    const Fingerprint atSave = take(g);
    show("до сейва", atSave);
    if (atSave.beacons == 0) {
        std::printf("⚠️  за %d лет никто не забедствовал — круг проверит сейв, но не маяки\n", before);
    }

    if (!g.saveToFile(path)) { std::printf("СЕЙВ НЕ ЗАПИСАЛСЯ: %s\n", g.lastEvent.c_str()); return 1; }

    Game loaded;
    if (!loaded.loadFromFile(path)) { std::printf("ЗАГРУЗКА НЕ УДАЛАСЬ: %s\n", loaded.lastEvent.c_str()); return 1; }
    const Fingerprint atLoad = take(loaded);
    show("загружен", atLoad);
    const bool roundTrip = same(atSave, atLoad);

    // Продолжение: обе копии крутим одинаково и сверяем. Мир детерминирован
    // (§2.3), поэтому расхождение означало бы, что сейв донёс не всё.
    for (int s = 0; s < after * 100; ++s) { g.update(0.01); loaded.update(0.01); }
    const Fingerprint contOriginal = take(g);
    const Fingerprint contLoaded = take(loaded);
    show("продолж.", contOriginal);
    show("из сейва", contLoaded);
    const bool sameFuture = same(contOriginal, contLoaded);

    std::printf("\nкруг сохранение->загрузка: %s\n", roundTrip ? "СОШЁЛСЯ" : "РАЗОШЁЛСЯ");
    std::printf("%d лет после загрузки идут одинаково: %s\n", after, sameFuture ? "ДА" : "НЕТ");
    return (roundTrip && sameFuture) ? 0 : 1;
}
