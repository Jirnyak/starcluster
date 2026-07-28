// Временный soak-тест локального режима (НЕ часть игры; удаляется после проверки).
// Гоняет updateLocalScene тысячи кадров по нескольким системам и глубокому космосу,
// направляя игрока на радиоисточники и корабли, чтобы задеть все новые ветки
// (луны/орбиты, радио reveal+claim, бой/смерть игрока+аварийный прыжок, лут, частицы).
// Проверяет инварианты каждый кадр: конечность координат, ограниченность пулов fx/shots/loot.
#include "local.h"
#include "game.h"
#include "ship.h"
#include "resource.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

static bool finite3(double a, double b, double c) {
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(c);
}

int main() {
    Game game;
    game.init(1200);
    if (game.playerAgent < 0 || game.playerAgent >= (int)game.agents.size()) {
        std::printf("SOAK FAIL: no valid player agent\n");
        return 2;
    }
    // Поднять детектор до максимума, чтобы проявлялись радиоисточники всех тиров.
    game.tech.sensors = 1.25;

    const int stars[] = {-1, 0, 1, 2, 3, 5, 7, 11, 23, 99};
    long totalFrames = 0, invariantFails = 0, deaths = 0, claims = 0, kills = 0;
    long rocksSeen = 0, rocksShiny = 0;   // (§5.13.15) счётчик пород + блестящих (лёд/металл)
    const double dtReal = 0.05; // 50 мс/кадр

    for (int starIdx : stars) {
        if (starIdx >= (int)game.cluster.stars.size()) continue;

        LocalScene scene;
        buildLocalScene(game, starIdx, scene);
        scene.active = true;

        // (§5.13.15) Инвариант внешнего вида пород: spec ∈ [0,1] и конечен (r/g/b — байты
        // по типу). Заодно считаем блестящие (лёд/металл, spec>0.5) для сводки разнообразия.
        for (size_t k = 0; k < scene.rocks.size(); ++k) {
            const double sp = scene.rocks[k].spec;
            if (!std::isfinite(sp) || sp < 0.0 || sp > 1.0) {
                std::printf("INVARIANT FAIL: rock spec out of range star=%d k=%d spec=%f\n",
                            starIdx, (int)k, sp);
                ++invariantFails;
            } else {
                ++rocksSeen;
                if (sp > 0.5) ++rocksShiny;
            }
        }

        const int radioTotal = (int)scene.radio.size();
        const int craftStart = (int)scene.craft.size();
        int fxPeak = 0;
        bool broke = false;

        for (int f = 0; f < 12000 && !broke; ++f) {
            // Выбор цели: ближайший непрозведённый радиоисточник, иначе ближайший корабль, иначе тело.
            double bx = 0, by = 0, bz = 0; bool haveTarget = false; double best = 1e300;
            for (size_t k = 0; k < scene.radio.size(); ++k) {
                if (scene.radio[k].resolved) continue;
                double dx = scene.radio[k].x - scene.px, dy = scene.radio[k].y - scene.py, dz = scene.radio[k].z - scene.pz;
                double d = dx * dx + dy * dy + dz * dz;
                if (d < best) { best = d; bx = scene.radio[k].x; by = scene.radio[k].y; bz = scene.radio[k].z; haveTarget = true; }
            }
            if (!haveTarget) {
                for (size_t k = 0; k < scene.craft.size(); ++k) {
                    double dx = scene.craft[k].x - scene.px, dy = scene.craft[k].y - scene.py, dz = scene.craft[k].z - scene.pz;
                    double d = dx * dx + dy * dy + dz * dz;
                    if (d < best) { best = d; bx = scene.craft[k].x; by = scene.craft[k].y; bz = scene.craft[k].z; haveTarget = true; }
                }
            }
            if (!haveTarget && !scene.bodies.empty()) {
                const LocalBody& bd = scene.bodies[(size_t)f % scene.bodies.size()];
                bx = bd.x; by = bd.y; bz = bd.z; haveTarget = true;
            }

            // Прямое наведение носа на цель (soak напрямую задаёт ориентацию через
            // тело-относительный базис — как gen/UI; см. localSetForward в local.h).
            if (haveTarget) {
                localSetForward(scene, bx - scene.px, by - scene.py, bz - scene.pz);
            }

            LocalInput in;
            in.thrust = true;
            in.fire = true;
            in.warp = (f % 4 == 0);          // периодический варп: много орбит + дальние POI
            in.mineToggle = (f % 500 == 0);
            in.cycleTarget = (f % 137 == 0);

            updateLocalScene(game, scene, in, dtReal);
            ++totalFrames;
            fxPeak = std::max(fxPeak, (int)scene.fx.size());

            if (!finite3(scene.px, scene.py, scene.pz) || !finite3(scene.pvx, scene.pvy, scene.pvz)) {
                std::printf("INVARIANT FAIL: non-finite player state star=%d f=%d\n", starIdx, f);
                ++invariantFails; broke = true; break;
            }
            if ((int)scene.fx.size() > LocalCfg::FX_MAX) {
                std::printf("INVARIANT FAIL: fx overflow %d star=%d f=%d\n", (int)scene.fx.size(), starIdx, f);
                ++invariantFails; broke = true; break;
            }
            if (scene.shots.size() > 200000 || scene.loot.size() > 200000 || scene.craft.size() > 200000) {
                std::printf("INVARIANT FAIL: vector runaway star=%d f=%d shots=%zu loot=%zu craft=%zu\n",
                            starIdx, f, scene.shots.size(), scene.loot.size(), scene.craft.size());
                ++invariantFails; broke = true; break;
            }

            if (scene.playerDestroyed) {
                ++deaths;
                Ship& ps = game.agents[game.playerAgent].ship; // эмуляция аварийного прыжка (как main.cpp)
                ps.hullHP = std::max(1.0, ps.maxHullHP * 0.30);
                scene.playerDestroyed = false;
                scene.pShield = scene.pMaxShield;
            }
        }

        int resolved = 0;
        for (size_t k = 0; k < scene.radio.size(); ++k) if (scene.radio[k].resolved) ++resolved;
        claims += resolved;
        kills += std::max(0, craftStart - (int)scene.craft.size());
        std::printf("star %5d: bodies=%2d rocks=%3d craft=%2d->%2d radio=%d resolved=%d fxPeak=%d\n",
                    starIdx, (int)scene.bodies.size(), (int)scene.rocks.size(),
                    craftStart, (int)scene.craft.size(), radioTotal, resolved, fxPeak);
    }

    // --- Детерминированная проверка ветки смерти игрока + аварийного прыжка ---
    // Ставим враждебного пирата вплотную, обнуляем щит/корпус игрока и ждём попадания.
    bool deathPathOk = false;
    {
        LocalScene scene;
        buildLocalScene(game, 0, scene);
        scene.active = true;
        if (!scene.craft.empty()) {
            LocalCraft& p = scene.craft[0];
            p.hostile = true; p.kind = CK_PIRATE; p.aiState = 1;
            p.heavy = 30.0; p.light = 20.0; p.fireCooldown = 0.0;
            p.x = scene.px + 30.0; p.y = scene.py; p.z = scene.pz;
            p.vx = p.vy = p.vz = 0.0;
            game.agents[game.playerAgent].ship.hullHP = 1.0; // на грани
            scene.pShield = 0.0; scene.pMaxShield = 0.0; scene.pShieldTimer = 999.0;
            LocalInput in; // без движения/варпа: снаряд врага долетает за 1-2 кадра
            for (int f = 0; f < 6000; ++f) {
                updateLocalScene(game, scene, in, dtReal);
                if (!finite3(scene.px, scene.py, scene.pz)) { ++invariantFails; break; }
                if (scene.playerDestroyed) { deathPathOk = true; break; }
            }
        }
        std::printf("death-path probe: playerDestroyed=%s\n", deathPathOk ? "YES (ok)" : "NO");
    }

    // --- Детерминированная проверка write-back в макро (§5.13.14) ---
    // Привязываем локальное зеркало к реальному макро-агенту (не игроку), добиваем его
    // выстрелом игрока и проверяем, что агент в ПОСТОЯННОМ мире деградировал в спас-капсулу
    // (корабль=капсула, оружие=0, груз сброшен). Свежий Game — полная изоляция от soak-цикла.
    // Игроку временно поднимаем оружие, у цели обнуляем корпус/щит — один залп добивает.
    bool writeBackOk = false;
    {
        Game g2; g2.init(1200);
        const int ai = (g2.playerAgent == 0 ? 1 : 0);        // любой не-игрок агент
        if (ai < (int)g2.agents.size()) {
            g2.agents[g2.playerAgent].ship.heavyWeapons = 60.0; // гарантированный урон игрока
            g2.agents[ai].ship.name = "Test Cruiser";
            g2.agents[ai].ship.heavyWeapons = 20.0;
            g2.agents[ai].ship.cargo.clear();
            g2.agents[ai].ship.cargo.emplace_back("H", 5.0); // немного груза — проверим сброс
            for (int starIdx : stars) {
                if (starIdx < 0) continue;                   // нужна реальная звезда с трафиком
                LocalScene scene;
                buildLocalScene(g2, starIdx, scene);
                scene.active = true;
                if (scene.craft.empty()) continue;
                if (scene.craft.size() > 1) scene.craft.resize(1); // единственная мишень — выстрел не в чужого
                scene.craft[0].agentIndex = ai;              // зеркало → наш агент
                scene.craft[0].faction = -1;                 // без репрессии — изолируем write-back
                scene.craft[0].kind = CK_TRADER; scene.craft[0].hostile = false;
                scene.craft[0].armor = 800.0;                // большой радиус попадания (hr=5+armor*0.1) — без туннелирования
                // Снаряд летит PROJ_SPEED*dtHours = 520*0.05 = 26 LU/кадр; ставим цель ровно на этот шаг.
                for (int f = 0; f < 3000 && !writeBackOk; ++f) {
                    if (!scene.craft.empty()) {              // пиним цель как сидячую мишень
                        LocalCraft& cc = scene.craft[0];
                        cc.x = scene.px + 26.0; cc.y = scene.py; cc.z = scene.pz;
                        cc.vx = cc.vy = cc.vz = 0.0;
                        cc.armor = 800.0;
                        if (cc.hullHP > 1.0) cc.hullHP = 1.0;
                        cc.shield = 0.0; cc.shieldRegenTimer = 999.0;
                        localSetForward(scene, cc.x - scene.px, cc.y - scene.py, cc.z - scene.pz);
                    }
                    LocalInput in; in.fire = true;
                    updateLocalScene(g2, scene, in, dtReal);
                    if (g2.agents[ai].ship.name == "Escape Pod"
                        && g2.agents[ai].ship.heavyWeapons == 0.0
                        && g2.agents[ai].ship.cargo.empty()) writeBackOk = true;
                }
                break;                                        // проверили одну систему — достаточно
            }
        }
        std::printf("write-back probe: macroAgentDowngraded=%s\n", writeBackOk ? "YES (ok)" : "NO");
    }
    if (!writeBackOk) { std::printf("INVARIANT FAIL: §5.13.14 macro write-back did not fire\n"); ++invariantFails; }

    std::printf("SOAK DONE frames=%ld deaths=%ld radioClaims=%ld craftDestroyed=%ld invariantFails=%ld deathPath=%s writeBack=%s rocks=%ld shiny=%ld\n",
                totalFrames, deaths, claims, kills, invariantFails,
                deathPathOk ? "ok" : "UNTRIGGERED", writeBackOk ? "ok" : "FAILED",
                rocksSeen, rocksShiny);
    return invariantFails ? 1 : 0;
}
