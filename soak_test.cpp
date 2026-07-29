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
    long tradesTotal = 0;                 // (§5.13.18) сделки продажи груза зеркалом-торговцем за весь soak
    const double dtReal = 0.05; // 50 мс/кадр

    for (int starIdx : stars) {
        if (starIdx >= (int)game.cluster.stars.size()) continue;

        LocalScene scene;
        buildLocalScene(game, starIdx, scene);
        scene.active = true;

        // (§5.13.15) Инвариант внешнего вида пород: spec ∈ [0,1] и конечен (r/g/b — байты
        // по типу). Заодно считаем блестящие (лёд/металл, spec>0.5) для сводки разнообразия.
        // (§5.13.16) + класс породы валиден (RockClass) и даёт конечный положительный множитель
        // добычи — гейт на новую классификацию/выход (rockClass/rockYieldMult).
        for (size_t k = 0; k < scene.rocks.size(); ++k) {
            const double sp = scene.rocks[k].spec;
            const int    rc = rockClass(scene.rocks[k].element);
            const double ym = rockYieldMult(rc);
            if (!std::isfinite(sp) || sp < 0.0 || sp > 1.0) {
                std::printf("INVARIANT FAIL: rock spec out of range star=%d k=%d spec=%f\n",
                            starIdx, (int)k, sp);
                ++invariantFails;
            } else if (rc < ROCK_SILICATE || rc > ROCK_METAL || !std::isfinite(ym) || ym <= 0.0) {
                std::printf("INVARIANT FAIL: rock class/yield star=%d k=%d class=%d yield=%f\n",
                            starIdx, (int)k, rc, ym);
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
        tradesTotal += scene.tradesExecuted;   // (§5.13.18) продажи зеркал-торговцев в этой сцене
        std::printf("star %5d: bodies=%2d rocks=%3d craft=%2d->%2d radio=%d resolved=%d fxPeak=%d\n",
                    starIdx, (int)scene.bodies.size(), (int)scene.rocks.size(),
                    craftStart, (int)scene.craft.size(), radioTotal, resolved, fxPeak);
    }

    // (§5.13.18) Инвариант: деньги всех макро-агентов конечны после локальных сделок продажи —
    // зеркала-торговцы писали в game.agents[].money через детерминированный sellCargo (тот же
    // персистентный мир). Ловим NaN/Inf, если write-back где-то повредил экономику.
    for (size_t a = 0; a < game.agents.size(); ++a) {
        if (!std::isfinite(game.agents[a].money)) {
            std::printf("INVARIANT FAIL: non-finite agent money a=%zu\n", a);
            ++invariantFails; break;
        }
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

    // --- Детерминированная проверка write-back ПРОДАЖИ груза (§5.13.18) ---
    // Зеркало-торговец (co-located макро-агент) при швартовке в локальном полёте продаёт передний
    // стак груза на местном рынке через детерминированный sellCargo — тот же путь, что макро-updateTrader
    // зовёт на прибытии. Свежий Game — полная изоляция. Берём элемент с макс. рыночной ценой (гарантия,
    // что выручка > 0), кладём макро-агенту в трюм, пиним зеркало-торговца на теле-цели → срабатывает
    // переход швартовки (errand 0→1) → продажа. Проверяем в ПОСТОЯННОМ мире: груз уменьшился, деньги
    // выросли, счётчик сделок сцены > 0. Это доказывает живую экономику без двойного счёта.
    bool sellWriteBackOk = false;
    {
        Game g3; g3.init(1200);
        const int ai = (g3.playerAgent == 0 ? 1 : 0);          // любой не-игрок агент
        if (ai < (int)g3.agents.size()) {
            for (int starIdx : stars) {
                if (starIdx < 0 || starIdx >= (int)g3.cluster.stars.size()) continue;
                if (starIdx >= (int)g3.markets.size()) continue; // нужен реальный рынок
                LocalScene scene;
                buildLocalScene(g3, starIdx, scene);
                scene.active = true;
                if (scene.craft.empty() || scene.bodies.empty()) continue;
                // Элемент с макс. рыночной ценой — гарантированно положительная выручка при продаже.
                const Market& mk = g3.markets[starIdx];
                int bestEl = -1; double bestPrice = 0.0;
                for (size_t e = 0; e < mk.prices.size(); ++e)
                    if (mk.prices[e] > bestPrice) { bestPrice = mk.prices[e]; bestEl = (int)e; }
                if (bestEl < 0) continue;                        // рынок без цен — к следующей звезде
                const std::string sym = elementDefinitions()[bestEl].symbol;
                // Готовим макро-агента: известный груз, запоминаем деньги.
                g3.agents[ai].ship.cargo.clear();
                g3.agents[ai].ship.cargo.emplace_back(sym, 8.0);
                const double moneyBefore = g3.agents[ai].money;
                // Единственный craft — зеркало нашего агента, торговец, круиз к телу 0.
                scene.craft.resize(1);
                scene.craft[0].agentIndex = ai;
                scene.craft[0].kind = CK_TRADER;
                scene.craft[0].hostile = false;
                scene.craft[0].faction = -1;
                scene.craft[0].errand = 0;
                scene.craft[0].errandBody = 0;
                for (int f = 0; f < 2000 && !sellWriteBackOk; ++f) {
                    LocalCraft& cc = scene.craft[0];
                    if (cc.errand == 0) {                        // пиним торговца ровно на теле-цели
                        const LocalBody& tb = scene.bodies[0];
                        cc.x = tb.x; cc.y = tb.y; cc.z = tb.z;
                        cc.vx = cc.vy = cc.vz = 0.0;
                        cc.errandBody = 0;
                    }
                    LocalInput in;
                    updateLocalScene(g3, scene, in, dtReal);
                    const bool cargoShrank = g3.agents[ai].ship.cargo.empty()
                        || g3.agents[ai].ship.cargo[0].amount < 8.0 - 0.01;
                    if (scene.tradesExecuted > 0 && cargoShrank
                        && g3.agents[ai].money > moneyBefore) sellWriteBackOk = true;
                }
                if (sellWriteBackOk) break;                      // одной системы достаточно
            }
        }
        std::printf("sell-write-back probe: traderSold=%s\n", sellWriteBackOk ? "YES (ok)" : "NO");
    }
    if (!sellWriteBackOk) { std::printf("INVARIANT FAIL: §5.13.18 trader-sell write-back did not fire\n"); ++invariantFails; }

    // --- Детерминированная проверка write-back смерти NPC-vs-NPC (§5.13.26, часть A) ---
    // Симметрична player-kill write-back (§5.13.14), но добивает НЕ игрок, а другой NPC: пират
    // расстреливает борт-зеркало макро-агента. Проверяем, что и в этом случае агент в ПОСТОЯННОМ
    // мире деградирует в спас-капсулу (раньше смерть NPC-vs-NPC была для макро инертной — единственная
    // точка мира без последствий). Свежий Game — полная изоляция. Пират и жертва пиньятся в упор;
    // игрок далеко (пират целит в NPC, а не в игрока) и НЕ стреляет — смерть чисто NPC-vs-NPC.
    // Жертве держим щит=0 и корпус=1 → первый же залп пирата добивает.
    bool npcWriteBackOk = false;
    {
        Game g4; g4.init(1200);
        const int ai = (g4.playerAgent == 0 ? 1 : 0);          // жертва — любой не-игрок агент
        if (ai < (int)g4.agents.size()) {
            g4.agents[ai].ship.name = "Test Freighter";
            g4.agents[ai].ship.heavyWeapons = 15.0;            // станет 0 после даунгрейда
            g4.agents[ai].ship.cargo.clear();
            g4.agents[ai].ship.cargo.emplace_back("H", 5.0);   // немного груза — проверим сброс
            for (int starIdx : stars) {
                if (starIdx < 0) continue;                     // нужна реальная звезда с трафиком
                LocalScene scene;
                buildLocalScene(g4, starIdx, scene);
                scene.active = true;
                while (scene.craft.size() < 2) { LocalCraft nc; scene.craft.push_back(nc); }
                scene.craft.resize(2);                         // ровно два борта: пират[0] + жертва[1]
                {
                    LocalCraft& pir = scene.craft[0];          // ПИРАТ-налётчик (без макро-зеркала)
                    pir.kind = CK_PIRATE; pir.hostile = true; pir.faction = -1; pir.agentIndex = -1;
                    pir.heavy = 60.0; pir.light = 0.0;         // гарантированный урон
                    pir.maxHullHP = 60.0; pir.hullHP = 60.0;   // здоров → не бежит (aiState=1)
                    pir.maxShield = 0.0; pir.shield = 0.0; pir.fireCooldown = 0.0;
                    LocalCraft& vic = scene.craft[1];          // ЖЕРТВА — зеркало нашего агента
                    vic.kind = CK_TRADER; vic.hostile = false; vic.faction = -1; vic.agentIndex = ai;
                    vic.armor = 800.0;
                }
                scene.px = 100000.0; scene.py = 0.0; scene.pz = 0.0; // игрок вне радиуса осведомлённости
                scene.pvx = scene.pvy = scene.pvz = 0.0;
                for (int f = 0; f < 3000 && !npcWriteBackOk; ++f) {
                    if (scene.craft.size() >= 2) {             // пиньятся в упор; жертва — сидячая мишень
                        LocalCraft& p0 = scene.craft[0];
                        LocalCraft& v1 = scene.craft[1];
                        p0.x = p0.y = p0.z = 0.0; p0.vx = p0.vy = p0.vz = 0.0;
                        p0.hullHP = p0.maxHullHP; p0.shield = 0.0; // держим пирата здоровым (не бежит)
                        v1.x = 40.0; v1.y = 0.0; v1.z = 0.0; v1.vx = v1.vy = v1.vz = 0.0;
                        if (v1.hullHP > 1.0) v1.hullHP = 1.0;
                        v1.shield = 0.0; v1.shieldRegenTimer = 999.0;
                    }
                    LocalInput in;                             // игрок НЕ стреляет
                    updateLocalScene(g4, scene, in, dtReal);
                    if (g4.agents[ai].ship.name == "Escape Pod"
                        && g4.agents[ai].ship.heavyWeapons == 0.0
                        && g4.agents[ai].ship.cargo.empty()) npcWriteBackOk = true;
                }
                break;                                          // проверили одну систему — достаточно
            }
        }
        std::printf("npc-write-back probe: macroAgentDowngraded=%s\n", npcWriteBackOk ? "YES (ok)" : "NO");
    }
    if (!npcWriteBackOk) { std::printf("INVARIANT FAIL: §5.13.26 NPC-vs-NPC macro write-back did not fire\n"); ++invariantFails; }

    // --- Детерминированная проверка §5.13.28: патруль ОХОТИТСЯ на РОЗЫСКНОГО пирата ---
    // Гейт этого слайса. Ветку CK_PATROL headless-мир НЕ исполняет (патрулей в замороженном кластере
    //   нет — оттого 120k-бейзлайн выше побитово неизменен), поэтому изолированный пробник — ЕДИНСТВЕННОЕ
    //   headless-покрытие ИИ патруля. Свежий Game. Две подпроверки, обе с пиньнутыми позициями (ноль RNG,
    //   ноль движения): (A) розыскной пират ДАЛЬШЕ, простой БЛИЖЕ ⇒ патруль обязан бить ИМЕННО розыскного
    //   (приоритет-0 перебивает дистанцию); (B) розыскного нет ⇒ патруль бьёт БЛИЖАЙШЕГО (фолбэк §5.13.12
    //   цел). Игрок далеко и не стреляет — бой чисто патруль-против-пиратов. Пиратам даём огромный корпус
    //   (не гибнут за пробу ⇒ индексы стабильны), щит=0 (урон сразу в корпус, измерим); патруль пиньним
    //   здоровым каждый кадр (никогда не бежит ⇒ aiState=1, стреляет с первого кадра, цель — в WEAPON_RANGE).
    bool patrolHuntsWantedOk = false;
    {
        Game g5; g5.init(1200);
        int star = -1;
        for (int s : stars) { if (s >= 0) { star = s; break; } }
        bool aOk = false, bOk = false;
        if (star >= 0) {
            // (A) розыскной ДАЛЬШЕ (120 LU) vs простой БЛИЖЕ (60 LU) → патруль должен бить РОЗЫСКНОГО
            LocalScene sa;
            buildLocalScene(g5, star, sa);
            sa.active = true;
            while (sa.craft.size() < 3) { LocalCraft nc; sa.craft.push_back(nc); }
            sa.craft.resize(3);
            {
                LocalCraft& pat = sa.craft[0];                 // ПАТРУЛЬ-охотник (чисто локальный)
                pat.kind = CK_PATROL; pat.hostile = false; pat.faction = -1; pat.agentIndex = -1;
                pat.heavy = 60.0; pat.light = 0.0;             // гарантированный урон
                pat.maxHullHP = 100000.0; pat.hullHP = 100000.0;
                pat.maxShield = 0.0; pat.shield = 0.0; pat.fireCooldown = 0.0; pat.wanted = false;
                LocalCraft& farW = sa.craft[1];                // РОЗЫСКНОЙ пират — ДАЛЬШЕ
                farW.kind = CK_PIRATE; farW.hostile = true; farW.faction = -1; farW.agentIndex = -1;
                farW.wanted = true; farW.wantedBounty = 650.0;
                farW.maxHullHP = 100000.0; farW.hullHP = 100000.0;
                farW.maxShield = 0.0; farW.shield = 0.0; farW.shieldRegenTimer = 999.0;
                LocalCraft& nearP = sa.craft[2];               // ПРОСТОЙ пират — БЛИЖЕ
                nearP.kind = CK_PIRATE; nearP.hostile = true; nearP.faction = -1; nearP.agentIndex = -1;
                nearP.wanted = false;
                nearP.maxHullHP = 100000.0; nearP.hullHP = 100000.0;
                nearP.maxShield = 0.0; nearP.shield = 0.0; nearP.shieldRegenTimer = 999.0;
            }
            sa.px = 100000.0; sa.py = 0.0; sa.pz = 0.0; sa.pvx = sa.pvy = sa.pvz = 0.0;
            for (int f = 0; f < 400 && !aOk; ++f) {
                LocalCraft& p0 = sa.craft[0]; LocalCraft& w1 = sa.craft[1]; LocalCraft& n2 = sa.craft[2];
                p0.x = p0.y = p0.z = 0.0; p0.vx = p0.vy = p0.vz = 0.0;
                p0.hullHP = p0.maxHullHP; p0.shield = 0.0;     // держим патруль здоровым
                w1.x = 120.0; w1.y = 0.0; w1.z = 0.0; w1.vx = w1.vy = w1.vz = 0.0;  // розыскной ДАЛЬШЕ
                w1.shield = 0.0; w1.shieldRegenTimer = 999.0;
                n2.x =  60.0; n2.y = 0.0; n2.z = 0.0; n2.vx = n2.vy = n2.vz = 0.0;  // простой БЛИЖЕ
                n2.shield = 0.0; n2.shieldRegenTimer = 999.0;
                LocalInput in;
                updateLocalScene(g5, sa, in, dtReal);
                if (sa.craft.size() >= 3
                    && sa.craft[1].hullHP <  sa.craft[1].maxHullHP    // розыскной (дальний) под огнём
                    && sa.craft[2].hullHP >= sa.craft[2].maxHullHP)   // простой (ближний) НЕ тронут
                    aOk = true;
            }
            // (B) розыскного НЕТ: два простых пирата, дальний (120) и ближний (60) → бьёт БЛИЖНЕГО (фолбэк)
            LocalScene sb;
            buildLocalScene(g5, star, sb);
            sb.active = true;
            while (sb.craft.size() < 3) { LocalCraft nc; sb.craft.push_back(nc); }
            sb.craft.resize(3);
            {
                LocalCraft& pat = sb.craft[0];
                pat.kind = CK_PATROL; pat.hostile = false; pat.faction = -1; pat.agentIndex = -1;
                pat.heavy = 60.0; pat.light = 0.0;
                pat.maxHullHP = 100000.0; pat.hullHP = 100000.0;
                pat.maxShield = 0.0; pat.shield = 0.0; pat.fireCooldown = 0.0; pat.wanted = false;
                for (int idx = 1; idx <= 2; ++idx) {           // оба — ПРОСТЫЕ пираты (не в розыске)
                    LocalCraft& pr = sb.craft[idx];
                    pr.kind = CK_PIRATE; pr.hostile = true; pr.faction = -1; pr.agentIndex = -1;
                    pr.wanted = false;
                    pr.maxHullHP = 100000.0; pr.hullHP = 100000.0;
                    pr.maxShield = 0.0; pr.shield = 0.0; pr.shieldRegenTimer = 999.0;
                }
            }
            sb.px = 100000.0; sb.py = 0.0; sb.pz = 0.0; sb.pvx = sb.pvy = sb.pvz = 0.0;
            for (int f = 0; f < 400 && !bOk; ++f) {
                LocalCraft& p0 = sb.craft[0]; LocalCraft& far1 = sb.craft[1]; LocalCraft& near2 = sb.craft[2];
                p0.x = p0.y = p0.z = 0.0; p0.vx = p0.vy = p0.vz = 0.0;
                p0.hullHP = p0.maxHullHP; p0.shield = 0.0;
                far1.x = 120.0; far1.y = 0.0; far1.z = 0.0; far1.vx = far1.vy = far1.vz = 0.0;  // ДАЛЬНИЙ
                far1.shield = 0.0; far1.shieldRegenTimer = 999.0;
                near2.x = 60.0; near2.y = 0.0; near2.z = 0.0; near2.vx = near2.vy = near2.vz = 0.0; // БЛИЖНИЙ
                near2.shield = 0.0; near2.shieldRegenTimer = 999.0;
                LocalInput in;
                updateLocalScene(g5, sb, in, dtReal);
                if (sb.craft.size() >= 3
                    && sb.craft[2].hullHP <  sb.craft[2].maxHullHP    // ближний под огнём
                    && sb.craft[1].hullHP >= sb.craft[1].maxHullHP)   // дальний НЕ тронут (фолбэк = ближайший)
                    bOk = true;
            }
        }
        patrolHuntsWantedOk = aOk && bOk;
        std::printf("patrol-hunts-wanted probe: wantedOverDistance=%s fallbackNearest=%s\n",
                    aOk ? "YES (ok)" : "NO", bOk ? "YES (ok)" : "NO");
    }
    if (!patrolHuntsWantedOk) { std::printf("INVARIANT FAIL: §5.13.28 patrol wanted-hunt did not fire\n"); ++invariantFails; }

    // --- Детерминированная проверка §5.13.30: СТАЯ защищает загнанного изгоя (pack-rally) ---
    // Правило добавлено в ветку CK_PIRATE (она в соаке ИСПОЛНЯЕТСЯ), но срабатывает лишь при наличии
    //   патруля-охотника рядом; патрулей в замороженном мире НЕТ ⇒ в 120k-прогоне блок no-op ПО
    //   ПОСТРОЕНИЮ (бейзлайн выше побитово цел), а этот изолированный пробник — единственное headless-
    //   покрытие правила. Свежий Game, позиции пиньнутся каждый кадр (ноль дрейфа/RNG), корпуса огромны
    //   (никто не гибнет ⇒ индексы стабильны). Три борта: патруль в 0; розыскная приманка в радиусе
    //   охоты патруля (<1400) — делает патруль «охотником»; ПРОСТАИВАЮЩИЙ пират-стайник ВНЕ своей
    //   осведомлённости (1100 > 750 ⇒ сам цели не видит), но В радиусе набата (<1400). Проверяем aiState
    //   (прямой выход решения о цели), а не урон: стайник на 1100 LU > WEAPON_RANGE, стрелять не может,
    //   но захват цели виден по aiState. (A) приманка РОЗЫСКНАЯ ⇒ стайник обязан взять патруль целью
    //   (aiState=1 — иначе он простаивал бы). (B) приманка НЕ розыскная ⇒ патруль не охотник ⇒ набат
    //   заперт гейтом ⇒ стайник простаивает (aiState=0).
    bool packRalliesOutlawOk = false;
    {
        Game g6; g6.init(1200);
        int star = -1;
        for (int s : stars) { if (s >= 0) { star = s; break; } }
        bool aOk = false, bReached = false, bViolated = false;
        if (star >= 0) {
            for (int phase = 0; phase < 2; ++phase) {          // phase 0 = (A) розыскная приманка; 1 = (B) простая
                LocalScene s;
                buildLocalScene(g6, star, s);
                s.active = true;
                while (s.craft.size() < 3) { LocalCraft nc; s.craft.push_back(nc); }
                s.craft.resize(3);
                {
                    LocalCraft& pat = s.craft[0];              // ПАТРУЛЬ (в соаке таких нет — тут вручную)
                    pat.kind = CK_PATROL; pat.hostile = false; pat.faction = -1; pat.agentIndex = -1;
                    pat.heavy = 60.0; pat.light = 0.0;
                    pat.maxHullHP = 100000.0; pat.hullHP = 100000.0;
                    pat.maxShield = 0.0; pat.shield = 0.0; pat.fireCooldown = 0.0; pat.wanted = false;
                    LocalCraft& bait = s.craft[1];             // приманка: делает патруль ОХОТНИКОМ, если розыскная
                    bait.kind = CK_PIRATE; bait.hostile = true; bait.faction = -1; bait.agentIndex = -1;
                    bait.wanted = (phase == 0); bait.wantedBounty = 650.0;
                    bait.maxHullHP = 100000.0; bait.hullHP = 100000.0;
                    bait.maxShield = 0.0; bait.shield = 0.0; bait.shieldRegenTimer = 999.0;
                    LocalCraft& pack = s.craft[2];             // ПРОСТАИВАЮЩИЙ пират-стайник (набатчик)
                    pack.kind = CK_PIRATE; pack.hostile = true; pack.faction = -1; pack.agentIndex = -1;
                    pack.wanted = false;
                    pack.maxHullHP = 100000.0; pack.hullHP = 100000.0;
                    pack.maxShield = 0.0; pack.shield = 0.0; pack.shieldRegenTimer = 999.0;
                }
                s.px = 100000.0; s.py = 0.0; s.pz = 0.0; s.pvx = s.pvy = s.pvz = 0.0;
                for (int f = 0; f < 60; ++f) {
                    LocalCraft& p0 = s.craft[0]; LocalCraft& b1 = s.craft[1]; LocalCraft& k2 = s.craft[2];
                    p0.x = 0.0;    p0.y = 0.0;    p0.z = 0.0; p0.vx = p0.vy = p0.vz = 0.0;
                    p0.hullHP = p0.maxHullHP; p0.shield = 0.0;
                    b1.x = 1000.0; b1.y = 0.0;    b1.z = 0.0; b1.vx = b1.vy = b1.vz = 0.0;  // в радиусе охоты патруля (<1400)
                    b1.hullHP = b1.maxHullHP; b1.shield = 0.0; b1.shieldRegenTimer = 999.0;
                    k2.x = 0.0;    k2.y = 1100.0; k2.z = 0.0; k2.vx = k2.vy = k2.vz = 0.0;  // вне осведомлённости (>750), в набате (<1400)
                    k2.hullHP = k2.maxHullHP; k2.shield = 0.0; k2.shieldRegenTimer = 999.0;
                    LocalInput in;
                    updateLocalScene(g6, s, in, dtReal);
                    if (s.craft.size() >= 3) {
                        if (phase == 0 && s.craft[2].aiState == 1) aOk = true;            // (A) стайник взял патруль целью
                        if (phase == 1) { bReached = true; if (s.craft[2].aiState == 1) bViolated = true; }  // (B) не должен
                    }
                }
            }
        }
        bool bOk = bReached && !bViolated;
        packRalliesOutlawOk = aOk && bOk;
        std::printf("pack-rallies-outlaw probe: rallyFires=%s gatedOnHunt=%s\n",
                    aOk ? "YES (ok)" : "NO", bOk ? "YES (ok)" : "NO");
    }
    if (!packRalliesOutlawOk) { std::printf("INVARIANT FAIL: §5.13.30 pack-rally did not fire or was not gated\n"); ++invariantFails; }

    // --- Детерминированная проверка §5.13.32: ПОДМОГА загнанному охотнику (patrol backup) ---
    // Правило добавлено в ветку CK_PATROL (в соаке НЕ исполняется — патрулей в замороженном мире нет) и
    //   вдобавок срабатывает ЛИШЬ при ≥2 патрулях (осаждённый охотник + свободный подмога); в обоих прежних
    //   пробниках патруль РОВНО один ⇒ бейзлайн и оба пробника no-op ПО ПОСТРОЕНИЮ (числа выше побитово целы).
    //   Этот изолированный пробник — единственное headless-покрытие. Свежий Game, позиции пиньнутся каждый
    //   кадр (ноль дрейфа/RNG), корпуса огромны (индексы стабильны), игрок отставлен на 100000 LU. Четыре
    //   борта: охотник A в 0; розыскная приманка на 300 LU от A (<1400 ⇒ A становится ОХОТНИКОМ, если wanted)
    //   и >1400 от B; ПИРАТ-стайник на 1000 LU от A (в его бедствии) и ~1100 LU от B (750<d<1400 ⇒ B достанет
    //   его ТОЛЬКО подмогой, не фолбэком AW2=750); свободный патруль B на 2100 LU по Y (до розыскного
    //   ~2121>1400 ⇒ P0 у B пуст). Проверяем aiState B (прямой выход решения о цели): B на 1100 LU >
    //   WEAPON_RANGE ⇒ урона нет, но захват цели виден. (A) приманка РОЗЫСКНАЯ ⇒ A охотник ⇒ стайник «в
    //   бедствии охотника» ⇒ B берёт его целью (aiState=1 — только подмога дотянется дальше 750). (B) приманка
    //   НЕ розыскная ⇒ A не охотник ⇒ подмога заперта, рейдера нет (нет торговцев), фолбэк капнут 750 (стайник
    //   на 1100) ⇒ B простаивает (aiState=0).
    bool patrolBacksUpSwarmedOk = false;
    {
        Game g7; g7.init(1200);
        int star = -1;
        for (int s : stars) { if (s >= 0) { star = s; break; } }
        bool aOk = false, bReached = false, bViolated = false;
        if (star >= 0) {
            for (int phase = 0; phase < 2; ++phase) {          // phase 0 = (A) розыскная приманка ⇒ A охотник; 1 = (B) простая
                LocalScene s;
                buildLocalScene(g7, star, s);
                s.active = true;
                while (s.craft.size() < 4) { LocalCraft nc; s.craft.push_back(nc); }
                s.craft.resize(4);
                {
                    LocalCraft& hunterA = s.craft[0];          // ПАТРУЛЬ-ОХОТНИК (его облепит стая)
                    hunterA.kind = CK_PATROL; hunterA.hostile = false; hunterA.faction = -1; hunterA.agentIndex = -1;
                    hunterA.heavy = 60.0; hunterA.light = 0.0;
                    hunterA.maxHullHP = 100000.0; hunterA.hullHP = 100000.0;
                    hunterA.maxShield = 0.0; hunterA.shield = 0.0; hunterA.fireCooldown = 0.0; hunterA.wanted = false;
                    LocalCraft& outlaw = s.craft[1];           // розыскная приманка: делает A охотником, если wanted
                    outlaw.kind = CK_PIRATE; outlaw.hostile = true; outlaw.faction = -1; outlaw.agentIndex = -1;
                    outlaw.wanted = (phase == 0); outlaw.wantedBounty = 650.0;
                    outlaw.maxHullHP = 100000.0; outlaw.hullHP = 100000.0;
                    outlaw.maxShield = 0.0; outlaw.shield = 0.0; outlaw.shieldRegenTimer = 999.0;
                    LocalCraft& pack = s.craft[2];             // ПИРАТ-стайник в бедствии охотника A (цель подмоги)
                    pack.kind = CK_PIRATE; pack.hostile = true; pack.faction = -1; pack.agentIndex = -1;
                    pack.wanted = false;
                    pack.maxHullHP = 100000.0; pack.hullHP = 100000.0;
                    pack.maxShield = 0.0; pack.shield = 0.0; pack.shieldRegenTimer = 999.0;
                    LocalCraft& respB = s.craft[3];            // СВОБОДНЫЙ патруль-подмога (своего розыскного нет)
                    respB.kind = CK_PATROL; respB.hostile = false; respB.faction = -1; respB.agentIndex = -1;
                    respB.heavy = 60.0; respB.light = 0.0;
                    respB.maxHullHP = 100000.0; respB.hullHP = 100000.0;
                    respB.maxShield = 0.0; respB.shield = 0.0; respB.fireCooldown = 0.0; respB.wanted = false;
                }
                s.px = 100000.0; s.py = 0.0; s.pz = 0.0; s.pvx = s.pvy = s.pvz = 0.0;
                for (int f = 0; f < 60; ++f) {
                    LocalCraft& a0 = s.craft[0]; LocalCraft& o1 = s.craft[1];
                    LocalCraft& k2 = s.craft[2]; LocalCraft& b3 = s.craft[3];
                    a0.x = 0.0;    a0.y = 0.0;    a0.z = 0.0; a0.vx = a0.vy = a0.vz = 0.0;   // осаждаемый охотник
                    a0.hullHP = a0.maxHullHP; a0.shield = 0.0;
                    o1.x = 300.0;  o1.y = 0.0;    o1.z = 0.0; o1.vx = o1.vy = o1.vz = 0.0;   // <1400 от A (A охотник), >1400 от B
                    o1.hullHP = o1.maxHullHP; o1.shield = 0.0; o1.shieldRegenTimer = 999.0;
                    k2.x = 0.0;    k2.y = 1000.0; k2.z = 0.0; k2.vx = k2.vy = k2.vz = 0.0;   // <1400 от A (в бедствии) и от B, >750 от B
                    k2.hullHP = k2.maxHullHP; k2.shield = 0.0; k2.shieldRegenTimer = 999.0;
                    b3.x = 0.0;    b3.y = 2100.0; b3.z = 0.0; b3.vx = b3.vy = b3.vz = 0.0;   // до k2 ≈1100 (750<d<1400), до o1 ≈2121 (>1400)
                    b3.hullHP = b3.maxHullHP; b3.shield = 0.0;
                    LocalInput in;
                    updateLocalScene(g7, s, in, dtReal);
                    if (s.craft.size() >= 4) {
                        if (phase == 0 && s.craft[3].aiState == 1) aOk = true;            // (A) свободный патруль пришёл на помощь
                        if (phase == 1) { bReached = true; if (s.craft[3].aiState == 1) bViolated = true; }  // (B) не должен
                    }
                }
            }
        }
        bool bOk = bReached && !bViolated;
        patrolBacksUpSwarmedOk = aOk && bOk;
        std::printf("patrol-backs-up-swarmed probe: respondsToSwarm=%s gatedOnHunt=%s\n",
                    aOk ? "YES (ok)" : "NO", bOk ? "YES (ok)" : "NO");
    }
    if (!patrolBacksUpSwarmedOk) { std::printf("INVARIANT FAIL: §5.13.32 patrol backup did not fire or was not gated\n"); ++invariantFails; }

    // --- Детерминированная проверка §5.13.34: ОБЛАВА — закон гонится за одиозной дичью быстрее ---
    // manhuntSpeedMult поднимает потолок скорости патруля, ведущего P0-охоту на живого `wanted`-пирата,
    //   пропорционально награде за голову (250→1500 CR ⇒ ×1.0→×(1+GAIN)). Гейт CK_PATROL — в замороженном
    //   мире патрулей нет ⇒ 120k-бейзлайн выше побитово цел; этот пробник — единственное headless-покрытие.
    //   Свежий Game. Две ФАЗЫ, идентичные во всём КРОМЕ награды: патруль в 0, розыскной пират на 1200 LU
    //   (<1400 ⇒ P0-охота; >95 ⇒ прямая погоня, не страйф; >150 ⇒ никто не стреляет, индексы стабильны;
    //   патруль полнокорпусный ⇒ не бежит, threatScan только для торговцев). Позицию патруля пиним в 0
    //   каждый кадр (цель всегда на +X ⇒ скорость копится в одну сторону, насыщаясь на effMax), но
    //   СКОРОСТЬ НЕ трогаем (в отличие от прочих пробников — тут нужен разгон). Меряем max|v|. (A) награда
    //   1500 ⇒ терминальная скорость ≈ maxSpeed·(1+GAIN). (B) награда 250 ⇒ ровно maxSpeed (нулевой регресс
    //   = «без облавы»). Ядро: A строго быстрее B. Эталоны берём из LocalCfg::MANHUNT_SPEED_GAIN (устойчиво
    //   к тюнингу). Пират/не-патруль НЕ ускоряется — это доказывает побитовый 120k-бейзлайн (пираты в нём
    //   движутся; изменись их скорость — числа поплыли бы).
    bool manhuntScalesWithBountyOk = false;
    {
        Game g8; g8.init(1200);
        int star = -1;
        for (int s : stars) { if (s >= 0) { star = s; break; } }
        double vmax[2] = {0.0, 0.0};
        const double MAXS = 60.0;
        if (star >= 0) {
            for (int phase = 0; phase < 2; ++phase) {          // 0 = высокая награда (1500); 1 = минимальная (250)
                LocalScene s;
                buildLocalScene(g8, star, s);
                s.active = true;
                while (s.craft.size() < 2) { LocalCraft nc; s.craft.push_back(nc); }
                s.craft.resize(2);
                {
                    LocalCraft& pat = s.craft[0];              // патруль-охотник (в соаке таких нет — тут вручную)
                    pat.kind = CK_PATROL; pat.hostile = false; pat.faction = -1; pat.agentIndex = -1;
                    pat.heavy = 60.0; pat.light = 0.0;
                    pat.maxSpeed = MAXS; pat.accel = 300.0;    // быстрое насыщение до effMax за единицы кадров
                    pat.maxHullHP = 100000.0; pat.hullHP = 100000.0;
                    pat.maxShield = 0.0; pat.shield = 0.0; pat.fireCooldown = 0.0; pat.wanted = false; pat.boost = 0.0;
                    LocalCraft& outlaw = s.craft[1];           // розыскной — задаёт «жар» облавы наградой
                    outlaw.kind = CK_PIRATE; outlaw.hostile = true; outlaw.faction = -1; outlaw.agentIndex = -1;
                    outlaw.wanted = true; outlaw.wantedBounty = (phase == 0) ? 1500.0 : 250.0;
                    outlaw.maxHullHP = 100000.0; outlaw.hullHP = 100000.0;
                    outlaw.maxShield = 0.0; outlaw.shield = 0.0; outlaw.shieldRegenTimer = 999.0;
                }
                s.px = 100000.0; s.py = 0.0; s.pz = 0.0; s.pvx = s.pvy = s.pvz = 0.0;
                for (int f = 0; f < 120; ++f) {
                    LocalCraft& p0 = s.craft[0]; LocalCraft& o1 = s.craft[1];
                    p0.x = 0.0; p0.y = 0.0; p0.z = 0.0;        // пин ПОЗИЦИИ (но НЕ скорости — нужен разгон)
                    p0.hullHP = p0.maxHullHP; p0.shield = 0.0; p0.boost = 0.0;
                    o1.x = 1200.0; o1.y = 0.0; o1.z = 0.0; o1.vx = o1.vy = o1.vz = 0.0;  // <1400 ⇒ P0; >150 ⇒ без стрельбы
                    o1.hullHP = o1.maxHullHP; o1.shield = 0.0; o1.shieldRegenTimer = 999.0;
                    LocalInput in;
                    updateLocalScene(g8, s, in, dtReal);
                    if (!s.craft.empty()) {
                        LocalCraft& q = s.craft[0];
                        double cs = std::sqrt(q.vx*q.vx + q.vy*q.vy + q.vz*q.vz);
                        if (cs > vmax[phase]) vmax[phase] = cs;
                    }
                }
            }
        }
        double expHigh = MAXS * (1.0 + LocalCfg::MANHUNT_SPEED_GAIN);
        double expLow  = MAXS;                                  // heat=0 при награде 250 ⇒ множитель 1.0
        bool highOk  = std::fabs(vmax[0] - expHigh) < 1.5;      // ускорен пропорционально награде
        bool lowOk   = std::fabs(vmax[1] - expLow)  < 1.0;      // нулевой регресс: ровно maxSpeed
        bool orderOk = vmax[0] > vmax[1] + 1.0;                 // ядро: облава реально ускоряет закон
        manhuntScalesWithBountyOk = highOk && lowOk && orderOk;
        std::printf("manhunt-scales-with-bounty probe: highBounty=%.2f (exp %.2f) lowBounty=%.2f (exp %.2f) faster=%s\n",
                    vmax[0], expHigh, vmax[1], expLow, orderOk ? "yes (ok)" : "no");
    }
    if (!manhuntScalesWithBountyOk) { std::printf("INVARIANT FAIL: §5.13.34 manhunt speed scaling did not fire or regressed baseline\n"); ++invariantFails; }

    // --- Детерминированная проверка §5.13.36: БЛАГОДАРНОСТЬ ЗАКОНА (law gratitude) ---
    // Блок добавлен в player-kill (E) — он В СОАКЕ ИСПОЛНЯЕТСЯ (игрок палит каждый кадр), НО награда требует
    //   рядом патруля-охотника, а патрулей в замороженном мире НЕТ ⇒ в 120k-прогоне gratPatrol==-1,
    //   adjustFactionRelation из блока не зовётся ⇒ factionRelations и все счётчики выше побитово целы
    //   (no-op ПО ПОСТРОЕНИЮ, как §5.13.28/30/32/34). Этот изолированный пробник — единственное headless-
    //   покрытие правила. Один свежий Game, две сцены (полная изоляция от soak-цикла; rel0 берём перед
    //   каждой): (A) РОЗЫСКНОГО, которого гонит патруль в PATROL_DISTRESS_R (300 LU), ДОБИВАЕТ игрок ⇒ rep
    //   с фракцией патруля вырос ровно на +6; (B) тот же убой, но патруль ВНЕ радиуса (2000 LU) ⇒ rep НЕ
    //   меняется (гейт дистанции). Игроку поднимаем оружие; цель пиньним сидячей мишенью (hull→1, armor=800 —
    //   большой радиус попадания, как write-back §5.13.14); патруль пиньним здоровым и неподвижным в 300/2000
    //   LU от цели (никогда не добьёт её раньше игрока — вне WEAPON_RANGE — и сам не гибнет). Ноль RNG/дрейфа.
    bool lawGratitudeOk = false;
    {
        bool aOk = false, bOk = false;
        Game glg; glg.init(1200);
        int pf = glg.playerFaction;
        int F = -1;
        for (int f = 0; f < (int)glg.factions.size(); ++f) if (f != pf) { F = f; break; }
        int star = -1;
        for (int s : stars) { if (s >= 0) { star = s; break; } }
        if (pf >= 0 && F >= 0 && star >= 0) {
            glg.agents[glg.playerAgent].ship.heavyWeapons = 60.0;   // гарантированный урон игрока
            // (A) патруль В радиусе (300 LU от цели) ⇒ +6 rep фракции патруля
            {
                LocalScene sa;
                buildLocalScene(glg, star, sa);
                sa.active = true;
                while (sa.craft.size() < 2) { LocalCraft nc; sa.craft.push_back(nc); }
                sa.craft.resize(2);
                LocalCraft& W = sa.craft[0];                 // РОЗЫСКНОЙ пират — мишень игрока
                W.kind = CK_PIRATE; W.hostile = true; W.wanted = true; W.wantedBounty = 650.0;
                W.faction = -1; W.agentIndex = -1; W.threatConvoy = false;
                W.maxHullHP = 100.0; W.armor = 800.0;
                W.maxShield = 0.0; W.shield = 0.0; W.shieldRegenTimer = 999.0;
                LocalCraft& P = sa.craft[1];                 // ПАТРУЛЬ-охотник (фракция F)
                P.kind = CK_PATROL; P.hostile = false; P.faction = F; P.agentIndex = -1;
                P.heavy = 60.0; P.light = 0.0; P.wanted = false;
                P.maxHullHP = 100000.0; P.hullHP = 100000.0;
                P.maxShield = 0.0; P.shield = 0.0; P.fireCooldown = 0.0;
                int rel0 = glg.factionRelation(pf, F);
                for (int f = 0; f < 3000; ++f) {
                    if (!sa.craft.empty() && sa.craft[0].kind == CK_PIRATE && sa.craft[0].hullHP > 0.0) {
                        LocalCraft& w = sa.craft[0];
                        w.x = sa.px + 26.0; w.y = sa.py; w.z = sa.pz;      // сидячая мишень по носу
                        w.vx = w.vy = w.vz = 0.0;
                        if (w.hullHP > 1.0) w.hullHP = 1.0;
                        w.shield = 0.0; w.armor = 800.0;
                        localSetForward(sa, w.x - sa.px, w.y - sa.py, w.z - sa.pz);
                    }
                    if (sa.craft.size() >= 2 && sa.craft[1].kind == CK_PATROL) {   // патруль: неподвижен, здоров, 300 LU от цели
                        LocalCraft& p = sa.craft[1];
                        p.x = sa.px + 26.0; p.y = sa.py + 300.0; p.z = sa.pz;
                        p.vx = p.vy = p.vz = 0.0;
                        p.hullHP = p.maxHullHP; p.shield = 0.0;
                    }
                    LocalInput in; in.fire = true;
                    updateLocalScene(glg, sa, in, dtReal);
                    if (glg.factionRelation(pf, F) != rel0) { aOk = (glg.factionRelation(pf, F) == rel0 + 6); break; }
                }
            }
            // (B) патруль ВНЕ радиуса (2000 LU > PATROL_DISTRESS_R) ⇒ rep НЕ меняется, хотя убой был
            {
                LocalScene sb;
                buildLocalScene(glg, star, sb);
                sb.active = true;
                while (sb.craft.size() < 2) { LocalCraft nc; sb.craft.push_back(nc); }
                sb.craft.resize(2);
                LocalCraft& W = sb.craft[0];
                W.kind = CK_PIRATE; W.hostile = true; W.wanted = true; W.wantedBounty = 650.0;
                W.faction = -1; W.agentIndex = -1; W.threatConvoy = false;
                W.maxHullHP = 100.0; W.armor = 800.0;
                W.maxShield = 0.0; W.shield = 0.0; W.shieldRegenTimer = 999.0;
                LocalCraft& P = sb.craft[1];
                P.kind = CK_PATROL; P.hostile = false; P.faction = F; P.agentIndex = -1;
                P.heavy = 60.0; P.light = 0.0; P.wanted = false;
                P.maxHullHP = 100000.0; P.hullHP = 100000.0;
                P.maxShield = 0.0; P.shield = 0.0; P.fireCooldown = 0.0;
                int rel0 = glg.factionRelation(pf, F);
                bool killed = false;
                for (int f = 0; f < 3000 && !killed; ++f) {
                    if (!sb.craft.empty() && sb.craft[0].kind == CK_PIRATE && sb.craft[0].hullHP > 0.0) {
                        LocalCraft& w = sb.craft[0];
                        w.x = sb.px + 26.0; w.y = sb.py; w.z = sb.pz;
                        w.vx = w.vy = w.vz = 0.0;
                        if (w.hullHP > 1.0) w.hullHP = 1.0;
                        w.shield = 0.0; w.armor = 800.0;
                        localSetForward(sb, w.x - sb.px, w.y - sb.py, w.z - sb.pz);
                    }
                    if (sb.craft.size() >= 2 && sb.craft[1].kind == CK_PATROL) {   // патруль ДАЛЕКО (2000 LU) — не охотник для c
                        LocalCraft& p = sb.craft[1];
                        p.x = sb.px + 2000.0; p.y = sb.py; p.z = sb.pz;
                        p.vx = p.vy = p.vz = 0.0;
                        p.hullHP = p.maxHullHP; p.shield = 0.0;
                    }
                    LocalInput in; in.fire = true;
                    updateLocalScene(glg, sb, in, dtReal);
                    if (sb.craft.empty() || sb.craft[0].kind != CK_PIRATE || sb.craft[0].hullHP <= 0.0) killed = true;
                }
                bOk = killed && (glg.factionRelation(pf, F) == rel0);   // убой состоялся, но rep не изменился
            }
        }
        lawGratitudeOk = aOk && bOk;
        std::printf("law-gratitude probe: repBumpOnCoopKill=%s noBumpOutOfRange=%s\n",
                    aOk ? "YES (ok)" : "NO", bOk ? "YES (ok)" : "NO");
    }
    if (!lawGratitudeOk) { std::printf("INVARIANT FAIL: §5.13.36 law gratitude did not fire or leaked out of range\n"); ++invariantFails; }

    // --- Детерминированная проверка §5.13.40: МЕСТЬ ФРАКЦИИ (faction reprisal) ---
    // §5.13.36 всегда давал +6 за co-op-добивание розыскного, которого гнал патруль. §5.13.40 берёт ЗНАК из
    //   того, где игрок УЖЕ стоит с фракцией патруля (тот же классификатор порогов, что панель §5.13.37 и
    //   прицел §5.13.39): дружеств./нейтр. — благодарность (+6), ВРАЖДЕБНАЯ (тир Enemy/Hostile) — вооружённое
    //   вмешательство в их операцию ⇒ −6. Как и §5.13.36, в замороженном soak-мире патрулей нет ⇒ правило
    //   no-op ПО ПОСТРОЕНИЮ в 120k-цикле (числовой бейзлайн цел); этот изолированный пробник — единственное
    //   headless-покрытие ЗНАКА. Свежий Game, ТРИ сцены, идентичные Scene A из §5.13.36 (сидячий розыскной по
    //   носу, здоровый неподвижный патруль в 300 LU), но с ПРЕДварительно посеянной репутацией: (C) реп −60
    //   (тир Hostile) ⇒ −6 (итог −66); (D) реп +40 (тир Friendly) ⇒ +6 (итог +46); (E) реп −30 (тир Tense —
    //   ОТРИЦАТЕЛЬНЫЙ, но выше порога Hostile −48) ⇒ ВСЁ РАВНО +6 (итог −24). Пробник трёхсторонний: застрявший
    //   «всегда +6» валит C; «всегда −6» валит D; наивный «rep<0 ⇒ −6» (флип по нулю, а не по классификатору
    //   §5.13.37/§5.13.39) валит E ⇒ пробник пиньнит именно ТИР-семантику, а не знак числа. Посев и все три
    //   итога — в clamp [−128,128]. Посев симметричным adjustFactionRelation (pf≠F); ноль RNG/дрейфа.
    bool factionReprisalOk = false;
    {
        bool res[3] = { false, false, false };       // (C) Hostile −60 ; (D) Friendly +40 ; (E) Tense −30
        Game glg; glg.init(1200);
        int pf = glg.playerFaction;
        int F = -1;
        for (int f = 0; f < (int)glg.factions.size(); ++f) if (f != pf) { F = f; break; }
        int star = -1;
        for (int s : stars) { if (s >= 0) { star = s; break; } }
        if (pf >= 0 && F >= 0 && star >= 0) {
            glg.agents[glg.playerAgent].ship.heavyWeapons = 60.0;   // гарантированный урон игрока
            const int SEEDS[3] = { -60, 40, -30 };     // (C) Hostile ; (D) Friendly ; (E) Tense (neg, но > −48)
            const int WANTS[3] = {  -6,  6,   6 };     // §5.13.40 по тир-классификатору: Hostile⇒−6, Friendly/Tense⇒+6
            for (int scn = 0; scn < 3; ++scn) {
                int seed = SEEDS[scn];
                int want = WANTS[scn];
                LocalScene ss;
                buildLocalScene(glg, star, ss);
                ss.active = true;
                while (ss.craft.size() < 2) { LocalCraft nc; ss.craft.push_back(nc); }
                ss.craft.resize(2);
                LocalCraft& W = ss.craft[0];                 // РОЗЫСКНОЙ пират — мишень игрока
                W.kind = CK_PIRATE; W.hostile = true; W.wanted = true; W.wantedBounty = 650.0;
                W.faction = -1; W.agentIndex = -1; W.threatConvoy = false;
                W.maxHullHP = 100.0; W.armor = 800.0;
                W.maxShield = 0.0; W.shield = 0.0; W.shieldRegenTimer = 999.0;
                LocalCraft& P = ss.craft[1];                 // ПАТРУЛЬ-охотник (фракция F), 300 LU от цели
                P.kind = CK_PATROL; P.hostile = false; P.faction = F; P.agentIndex = -1;
                P.heavy = 60.0; P.light = 0.0; P.wanted = false;
                P.maxHullHP = 100000.0; P.hullHP = 100000.0;
                P.maxShield = 0.0; P.shield = 0.0; P.fireCooldown = 0.0;
                int cur = glg.factionRelation(pf, F);
                glg.adjustFactionRelation(pf, F, seed - cur);        // посев: rep(pf,F) = seed
                int rel0 = glg.factionRelation(pf, F);
                bool moved = false;
                for (int f = 0; f < 3000; ++f) {
                    if (!ss.craft.empty() && ss.craft[0].kind == CK_PIRATE && ss.craft[0].hullHP > 0.0) {
                        LocalCraft& w = ss.craft[0];
                        w.x = ss.px + 26.0; w.y = ss.py; w.z = ss.pz;      // сидячая мишень по носу
                        w.vx = w.vy = w.vz = 0.0;
                        if (w.hullHP > 1.0) w.hullHP = 1.0;
                        w.shield = 0.0; w.armor = 800.0;
                        localSetForward(ss, w.x - ss.px, w.y - ss.py, w.z - ss.pz);
                    }
                    if (ss.craft.size() >= 2 && ss.craft[1].kind == CK_PATROL) {   // патруль: неподвижен, здоров, 300 LU
                        LocalCraft& p = ss.craft[1];
                        p.x = ss.px + 26.0; p.y = ss.py + 300.0; p.z = ss.pz;
                        p.vx = p.vy = p.vz = 0.0;
                        p.hullHP = p.maxHullHP; p.shield = 0.0;
                    }
                    LocalInput in; in.fire = true;
                    updateLocalScene(glg, ss, in, dtReal);
                    if (glg.factionRelation(pf, F) != rel0) { moved = (glg.factionRelation(pf, F) == rel0 + want); break; }
                }
                res[scn] = moved;
            }
        }
        factionReprisalOk = res[0] && res[1] && res[2];
        std::printf("faction-reprisal probe: hostile[-60]->-6=%s friendly[+40]->+6=%s tense[-30]->+6=%s\n",
                    res[0] ? "YES (ok)" : "NO", res[1] ? "YES (ok)" : "NO", res[2] ? "YES (ok)" : "NO");
    }
    if (!factionReprisalOk) { std::printf("INVARIANT FAIL: §5.13.40 faction reprisal sign did not flip by standing tier\n"); ++invariantFails; }

    // (§5.13.38) КОП-КИЛЛ: убийство патруля-ОХОТНИКА его же дичью поднимает награду СИЛЬНЕЕ базового +200.
    //   Как и §5.13.28/30/32/34/36 — в замороженном soak-мире патрулей нет (их родит лишь со-локальный
    //   «military»/«patrol» макро-агент, localgen.cpp) ⇒ правило no-op ПО ПОСТРОЕНИЮ в 120k-цикле выше (числовой
    //   бейзлайн цел), а единственное headless-покрытие — этот изолированный пробник. Свежий Game, две сцены,
    //   обе с розыскным пиратом-УБИЙЦЕЙ (heavy=60, wantedBounty0=650), что расстреливает пиньнутый патруль
    //   (корпус→1) с 30 LU (< WEAPON_RANGE=150): (A) убийца — ЕДИНСТВЕННЫЙ wanted у патруля ⇒ он «дичь» ⇒
    //   награда +200 (§5.13.26G) +COP_KILL_BOUNTY = ровно 650+700=1350; (B) есть ДРУГОЙ wanted-пират (безоружная
    //   приманка) ВПЛОТНУЮ к патрулю (10 LU < 30) ⇒ убийца НЕ ближайший wanted ⇒ cWasQuarry ложь ⇒ эскалация
    //   НЕ срабатывает, награда растёт лишь на обычные +200 = 850. Пробник двусторонний: застрявшее «вкл»
    //   (всегда +700) валит B, застрявшее «выкл» валит A. Игрок в 5000 LU и НЕ стреляет; позиции/корпуса
    //   пиньним каждый кадр (ноль RNG/дрейфа); wantedBounty НЕ пиньним — это измеряемое.
    bool copKillRaisesBountyOk = false;
    {
        bool aOk = false, bOk = false;
        Game gck; gck.init(1200);
        int pf = gck.playerFaction;
        int F = -1;
        for (int f = 0; f < (int)gck.factions.size(); ++f) if (f != pf) { F = f; break; }
        int star = -1;
        for (int s : stars) { if (s >= 0) { star = s; break; } }
        const double B0 = 650.0;
        if (pf >= 0 && F >= 0 && star >= 0) {
            // (A) убийца — единственный wanted ⇒ он дичь ⇒ награда 650 +200 +COP_KILL_BOUNTY
            {
                LocalScene sa;
                buildLocalScene(gck, star, sa);
                sa.active = true;
                while (sa.craft.size() < 2) { LocalCraft nc; sa.craft.push_back(nc); }
                sa.craft.resize(2);
                double cx = sa.px + 5000.0, cy = sa.py, cz = sa.pz;   // бой ДАЛЕКО от игрока (не цель пирата)
                LocalCraft& P = sa.craft[0];                          // ПАТРУЛЬ — жертва
                P.kind = CK_PATROL; P.hostile = false; P.faction = F; P.agentIndex = -1;
                P.heavy = 0.0; P.light = 0.0; P.wanted = false; P.threatConvoy = false;
                P.maxHullHP = 100.0; P.hullHP = 1.0; P.maxShield = 0.0; P.shield = 0.0; P.shieldRegenTimer = 999.0;
                LocalCraft& K = sa.craft[1];                          // ПИРАТ-убийца (розыскной)
                K.kind = CK_PIRATE; K.hostile = true; K.faction = -1; K.agentIndex = -1;
                K.wanted = true; K.wantedBounty = B0; K.threatConvoy = false;
                K.heavy = 60.0; K.light = 0.0; K.fireCooldown = 0.0;
                K.maxHullHP = 100000.0; K.hullHP = 100000.0; K.maxShield = 0.0; K.shield = 0.0;
                bool killed = false;
                for (int f = 0; f < 3000 && !killed; ++f) {
                    int pi = -1, ki = -1;
                    for (size_t si = 0; si < sa.craft.size(); ++si) {
                        if (sa.craft[si].kind == CK_PATROL && sa.craft[si].hullHP > 0.0) pi = (int)si;
                        else if (sa.craft[si].kind == CK_PIRATE) ki = (int)si;
                    }
                    if (pi < 0) { killed = true; break; }              // патруль пал — эскалация уже применена
                    LocalCraft& pv = sa.craft[pi];
                    pv.x = cx; pv.y = cy; pv.z = cz; pv.vx = pv.vy = pv.vz = 0.0;
                    if (pv.hullHP > 1.0) pv.hullHP = 1.0;
                    pv.shield = 0.0;
                    if (ki >= 0) {
                        LocalCraft& kp = sa.craft[ki];
                        kp.x = cx + 30.0; kp.y = cy; kp.z = cz; kp.vx = kp.vy = kp.vz = 0.0;
                        kp.hullHP = kp.maxHullHP; kp.shield = 0.0;
                    }
                    LocalInput in; in.fire = false;
                    updateLocalScene(gck, sa, in, dtReal);
                }
                double finalB = -1.0;
                for (size_t si = 0; si < sa.craft.size(); ++si)
                    if (sa.craft[si].kind == CK_PIRATE && sa.craft[si].heavy > 50.0) finalB = sa.craft[si].wantedBounty;
                aOk = killed && (finalB == B0 + 200.0 + LocalCfg::COP_KILL_BOUNTY);
            }
            // (B) есть ДРУГОЙ wanted ВПЛОТНУЮ к патрулю ⇒ убийца не дичь ⇒ лишь обычные +200
            {
                LocalScene sb;
                buildLocalScene(gck, star, sb);
                sb.active = true;
                while (sb.craft.size() < 3) { LocalCraft nc; sb.craft.push_back(nc); }
                sb.craft.resize(3);
                double cx = sb.px + 5000.0, cy = sb.py, cz = sb.pz;
                LocalCraft& P = sb.craft[0];                          // ПАТРУЛЬ — жертва
                P.kind = CK_PATROL; P.hostile = false; P.faction = F; P.agentIndex = -1;
                P.heavy = 0.0; P.light = 0.0; P.wanted = false; P.threatConvoy = false;
                P.maxHullHP = 100.0; P.hullHP = 1.0; P.maxShield = 0.0; P.shield = 0.0; P.shieldRegenTimer = 999.0;
                LocalCraft& K = sb.craft[1];                          // ПИРАТ-убийца (heavy=60)
                K.kind = CK_PIRATE; K.hostile = true; K.faction = -1; K.agentIndex = -1;
                K.wanted = true; K.wantedBounty = B0; K.threatConvoy = false;
                K.heavy = 60.0; K.light = 0.0; K.fireCooldown = 0.0;
                K.maxHullHP = 100000.0; K.hullHP = 100000.0; K.maxShield = 0.0; K.shield = 0.0;
                LocalCraft& D = sb.craft[2];                          // ПРИМАНКА — безоружный wanted ВПЛОТНУЮ к патрулю
                D.kind = CK_PIRATE; D.hostile = false; D.faction = -1; D.agentIndex = -1;
                D.wanted = true; D.wantedBounty = 250.0; D.threatConvoy = false;
                D.heavy = 0.0; D.light = 0.0; D.fireCooldown = 999.0;
                D.maxHullHP = 100000.0; D.hullHP = 100000.0; D.maxShield = 0.0; D.shield = 0.0;
                bool killed = false;
                for (int f = 0; f < 3000 && !killed; ++f) {
                    int pi = -1, ki = -1, di = -1;
                    for (size_t si = 0; si < sb.craft.size(); ++si) {
                        if (sb.craft[si].kind == CK_PATROL && sb.craft[si].hullHP > 0.0) pi = (int)si;
                        else if (sb.craft[si].kind == CK_PIRATE && sb.craft[si].heavy > 50.0) ki = (int)si;
                        else if (sb.craft[si].kind == CK_PIRATE) di = (int)si;
                    }
                    if (pi < 0) { killed = true; break; }
                    LocalCraft& pv = sb.craft[pi];
                    pv.x = cx; pv.y = cy; pv.z = cz; pv.vx = pv.vy = pv.vz = 0.0;
                    if (pv.hullHP > 1.0) pv.hullHP = 1.0;
                    pv.shield = 0.0;
                    if (ki >= 0) {
                        LocalCraft& kp = sb.craft[ki];
                        kp.x = cx + 30.0; kp.y = cy; kp.z = cz; kp.vx = kp.vy = kp.vz = 0.0;
                        kp.hullHP = kp.maxHullHP; kp.shield = 0.0;
                    }
                    if (di >= 0) {
                        LocalCraft& dp = sb.craft[di];
                        dp.x = cx; dp.y = cy + 10.0; dp.z = cz; dp.vx = dp.vy = dp.vz = 0.0;   // 10 LU < 30 ⇒ ближе к патрулю
                        dp.hullHP = dp.maxHullHP; dp.shield = 0.0;
                    }
                    LocalInput in; in.fire = false;
                    updateLocalScene(gck, sb, in, dtReal);
                }
                double finalB = -1.0;
                for (size_t si = 0; si < sb.craft.size(); ++si)
                    if (sb.craft[si].kind == CK_PIRATE && sb.craft[si].heavy > 50.0) finalB = sb.craft[si].wantedBounty;
                bOk = killed && (finalB == B0 + 200.0);
            }
        }
        copKillRaisesBountyOk = aOk && bOk;
        std::printf("cop-kill probe: escalatesOnQuarryKill=%s noEscalationWhenNotQuarry=%s\n",
                    aOk ? "YES (ok)" : "NO", bOk ? "YES (ok)" : "NO");
    }
    if (!copKillRaisesBountyOk) { std::printf("INVARIANT FAIL: §5.13.38 cop-kill bounty did not escalate or leaked when not quarry\n"); ++invariantFails; }

    // (§5.13.42) ЗАКОН, КОТОРЫЙ ТЫ ПЕРЕШЁЛ, ОХОТИТСЯ НА ТЕБЯ: патруль фракции F берёт ИГРОКА целью БЕЗ провокации,
    //   если игрок стоит с F на тире Enemy/Hostile (реп ≤ −48, тот же классификатор, что §5.13.37/§5.13.40/§5.13.41).
    //   Правило — расширение гейта предпочтения игрока в ветке CK_PATROL. В ОСНОВНОМ 120k-цикле ветка не бежит:
    //   18 воен-агентов init() стоят гарнизонами на controlledStars (разбросаны по кластеру-1200: 78/792/1073/…),
    //   disjoint фикс-маршруту soak {-1,0,1,2,3,5,7,11,23,99}; co-location гейт currentStar==звезда (localgen.cpp:438)
    //   ⇒ patrols=0 на всех 10 (замерено врем. инструментом) ⇒ 120k-бейзлайн выше побитово цел; изолир. пробник —
    //   единственное headless-покрытие. Сцена = РОВНО один борт: патруль фракции F (resize(1) ⇒ ни пиратов, ни иных
    //   целей), НЕспровоцированный (hostile=false каждый кадр, игрок не стреляет), здоровый неподвижный, пиньнется в
    //   300 LU (>WEAPON_RANGE 150 ⇒ огонь не достаёт, сцена чистая; <AW2 750 ⇒ игрок в осведомлённости). Пиратов
    //   нет ⇒ P0/подмога/рейдер/фолбэк пусты ⇒ единственная возможная цель — игрок, единственный триггер — СТОЯНИЕ.
    //   Наблюдаем aiState (прямой выход решения о цели: 1=есть цель). (A) реп −100 (Enemy) ⇒ патруль берёт игрока
    //   (aiState==1). (B, контроль) реп 0 (Neutral) ⇒ цели нет ⇒ простаивает (aiState==0). Баг «всегда охотится»
    //   валит B; баг «никогда» валит A ⇒ проба доказывает, что триггер — именно СТОЯНИЕ, а не провокация.
    bool lawHuntsHostileOk = false;
    {
        Game glh; glh.init(1200);
        int pf = glh.playerFaction;
        int F = -1;
        for (int f = 0; f < (int)glh.factions.size(); ++f) if (f != pf) { F = f; break; }
        int star = -1;
        for (int s : stars) { if (s >= 0) { star = s; break; } }
        bool aOk = false, bReached = false, bViolated = false;
        if (pf >= 0 && F >= 0 && star >= 0) {
            const int SEEDS[2] = { -100, 0 };            // (A) Enemy (охота) ; (B) Neutral (контроль — простой)
            for (int phase = 0; phase < 2; ++phase) {
                LocalScene ss;
                buildLocalScene(glh, star, ss);
                ss.active = true;
                while (ss.craft.size() < 1) { LocalCraft nc; ss.craft.push_back(nc); }
                ss.craft.resize(1);
                LocalCraft& P = ss.craft[0];                 // ПАТРУЛЬ фракции F, НЕ спровоцирован
                P.kind = CK_PATROL; P.hostile = false; P.faction = F; P.agentIndex = -1;
                P.heavy = 60.0; P.light = 0.0; P.wanted = false;
                P.maxHullHP = 100000.0; P.hullHP = 100000.0;
                P.maxShield = 0.0; P.shield = 0.0; P.fireCooldown = 0.0;
                int cur = glh.factionRelation(pf, F);
                glh.adjustFactionRelation(pf, F, SEEDS[phase] - cur);   // посев: rep(pf,F) = SEEDS[phase]
                for (int f = 0; f < 60; ++f) {
                    if (!ss.craft.empty() && ss.craft[0].kind == CK_PATROL) {
                        LocalCraft& p = ss.craft[0];
                        p.x = ss.px; p.y = ss.py + 300.0; p.z = ss.pz;   // 300 LU: >WEAPON_RANGE, <AW2 (в осведомлённости)
                        p.vx = p.vy = p.vz = 0.0;
                        p.hullHP = p.maxHullHP; p.shield = 0.0; p.hostile = false;   // держим НЕспровоцированным
                    }
                    LocalInput in; in.fire = false;                     // игрок НЕ стреляет ⇒ провокации нет
                    updateLocalScene(glh, ss, in, dtReal);
                    if (!ss.craft.empty()) {
                        if (phase == 0 && ss.craft[0].aiState == 1) aOk = true;                 // (A) взял игрока целью
                        if (phase == 1) { bReached = true; if (ss.craft[0].aiState == 1) bViolated = true; }
                    }
                }
            }
        }
        bool bOk = bReached && !bViolated;
        lawHuntsHostileOk = aOk && bOk;
        std::printf("law-hunts-hostile probe: huntsAtEnemyStanding=%s idleAtNeutral=%s\n",
                    aOk ? "YES (ok)" : "NO", bOk ? "YES (ok)" : "NO");
    }
    if (!lawHuntsHostileOk) { std::printf("INVARIANT FAIL: §5.13.42 hostile-standing patrol did not hunt player, or hunted at neutral\n"); ++invariantFails; }

    // (§5.13.43) СИГНАЛ ЦЕЛИ для кокпит-подсказки «HUNTING YOU». Это РЕНДЕР-срез: сама подсказка рисуется в
    //   localdraw.cpp (вне headless-пути соака), но ЧИТАЕТ поле c.aiTarget, которое теперь заполняет симуляция
    //   (localsim.cpp §5.13.43 — до сих пор поле было мёртвым; запись строго read-only ⇒ 120k-бейзлайн выше
    //   побитово цел). Здесь фиксируем ИМЕННО контракт данных подсказки: предикат
    //   c.kind==CK_PATROL && c.aiState==1 && c.aiTarget==LOCAL_TARGET_PLAYER && !c.hostile. Как и §5.13.42,
    //   в основном 120k-цикле ветка патруля не исполняется (гарнизоны disjoint-маршруту) ⇒ изолированный пробник.
    //   (A) Патруль фракции F, Enemy-стояние (реп −100), игрок в 300 LU, В СЦЕНЕ БОЛЬШЕ НИКОГО (resize(1)):
    //       единственная возможная цель — игрок ⇒ aiState==1 && aiTarget==LOCAL_TARGET_PLAYER (подсказка ГОРИТ).
    //   (B, анти-ложняк) тот же патруль + Enemy-стояние, НО ближе (80 LU) — РОЗЫСКНОЙ пират, игрок дальше (300 LU):
    //       патруль охотится на пирата (§5.13.28; пират ближе игрока ⇒ override §5.13.42 по pd2<atkBest НЕ
    //       срабатывает) ⇒ aiState==1 && aiTarget>=0 (индекс пирата), но НИКОГДА ==LOCAL_TARGET_PLAYER (подсказка
    //       ГАСНЕТ). Ловит регресс, при котором «HUNTING YOU» светила бы на патруле, идущем на пирата, а не на игрока.
    bool patrolTargetSignalOk = false;
    {
        Game gts; gts.init(1200);
        int pf = gts.playerFaction;
        int F = -1;
        for (int f = 0; f < (int)gts.factions.size(); ++f) if (f != pf) { F = f; break; }
        int star = -1;
        for (int s : stars) { if (s >= 0) { star = s; break; } }
        bool aOk = false;                        // (A) видели aiTarget==LOCAL_TARGET_PLAYER
        bool bHunted = false, bFalsePos = false; // (B) видели индекс пирата ; НИКОГДА игрока
        if (pf >= 0 && F >= 0 && star >= 0) {
            // (A) только патруль + игрок
            {
                LocalScene ss; buildLocalScene(gts, star, ss); ss.active = true;
                while (ss.craft.size() < 1) { LocalCraft nc; ss.craft.push_back(nc); }
                ss.craft.resize(1);
                LocalCraft& P = ss.craft[0];
                P.kind = CK_PATROL; P.hostile = false; P.faction = F; P.agentIndex = -1;
                P.heavy = 60.0; P.light = 0.0; P.wanted = false;
                P.maxHullHP = 100000.0; P.hullHP = 100000.0;
                P.maxShield = 0.0; P.shield = 0.0; P.fireCooldown = 0.0;
                int cur = gts.factionRelation(pf, F);
                gts.adjustFactionRelation(pf, F, -100 - cur);        // Enemy
                for (int f = 0; f < 60; ++f) {
                    LocalCraft& p = ss.craft[0];
                    p.x = ss.px; p.y = ss.py + 300.0; p.z = ss.pz;   // 300 LU: >WEAPON_RANGE, <AW2
                    p.vx = p.vy = p.vz = 0.0; p.hullHP = p.maxHullHP; p.shield = 0.0; p.hostile = false;
                    LocalInput in; in.fire = false;                  // игрок не стреляет ⇒ провокации нет
                    updateLocalScene(gts, ss, in, dtReal);
                    if (!ss.craft.empty() && ss.craft[0].aiState == 1 &&
                        ss.craft[0].aiTarget == LOCAL_TARGET_PLAYER) aOk = true;
                }
            }
            // (B) патруль + игрок + БЛИЖЕ розыскной пират
            {
                LocalScene ss; buildLocalScene(gts, star, ss); ss.active = true;
                while (ss.craft.size() < 2) { LocalCraft nc; ss.craft.push_back(nc); }
                ss.craft.resize(2);
                LocalCraft& P = ss.craft[0];
                P.kind = CK_PATROL; P.hostile = false; P.faction = F; P.agentIndex = -1;
                P.heavy = 60.0; P.light = 0.0; P.wanted = false;
                P.maxHullHP = 100000.0; P.hullHP = 100000.0;
                P.maxShield = 0.0; P.shield = 0.0; P.fireCooldown = 0.0;
                LocalCraft& W = ss.craft[1];                         // РОЗЫСКНОЙ пират, ближе игрока
                W.kind = CK_PIRATE; W.hostile = true; W.faction = -1; W.agentIndex = -1;
                W.wanted = true; W.wantedBounty = 650.0;
                W.maxHullHP = 100000.0; W.hullHP = 100000.0;
                W.maxShield = 0.0; W.shield = 0.0; W.shieldRegenTimer = 999.0;
                int cur = gts.factionRelation(pf, F);
                gts.adjustFactionRelation(pf, F, -100 - cur);        // Enemy: игрок — валидная цель по стоянию
                for (int f = 0; f < 60; ++f) {
                    LocalCraft& p = ss.craft[0]; LocalCraft& w = ss.craft[1];
                    p.x = ss.px; p.y = ss.py + 300.0; p.z = ss.pz;   // патруль в 300 LU от игрока
                    p.vx = p.vy = p.vz = 0.0; p.hullHP = p.maxHullHP; p.shield = 0.0; p.hostile = false;
                    w.x = ss.px; w.y = ss.py + 380.0; w.z = ss.pz;   // пират в 80 LU от патруля (ближе игрока)
                    w.vx = w.vy = w.vz = 0.0; w.hullHP = w.maxHullHP; w.shield = 0.0; w.shieldRegenTimer = 999.0;
                    LocalInput in; in.fire = false;
                    updateLocalScene(gts, ss, in, dtReal);
                    if (!ss.craft.empty() && ss.craft[0].aiState == 1) {
                        if (ss.craft[0].aiTarget == LOCAL_TARGET_PLAYER) bFalsePos = true;
                        else if (ss.craft[0].aiTarget >= 0)              bHunted   = true;
                    }
                }
            }
        }
        bool bOk = bHunted && !bFalsePos;
        patrolTargetSignalOk = aOk && bOk;
        std::printf("patrol-target-signal probe: playerTargetSentinel=%s pirateNotPlayer=%s\n",
                    aOk ? "YES (ok)" : "NO", bOk ? "YES (ok)" : "NO");
    }
    if (!patrolTargetSignalOk) { std::printf("INVARIANT FAIL: §5.13.43 aiTarget signal wrong (player sentinel missing, or false-positive on pirate hunt)\n"); ++invariantFails; }

    // (§5.13.44) ВЕНДЕТТА: патруль, что берёт ИГРОКА целью по стоянию (§5.13.42), летит тем резвее, чем ГЛУБЖЕ
    //   минус его фракции к игроку. Зеркало облавы §5.13.34, но ключ — ЖИВОЙ aiTarget==LOCAL_TARGET_PLAYER (его
    //   §5.13.43 начал заполнять) вместо P0-скана по пиратам ⇒ поле дорастает из рендерного до входа СИМ. Как
    //   §5.13.42/43, в 120k-цикле ветка патруля не бежит (гарнизоны disjoint фикс-маршруту soak) ⇒ изолированный
    //   пробник; сцена = РОВНО патруль фракции F + игрок (resize(1) ⇒ ни пиратов, ни иных целей), НЕспровоцирован,
    //   пиньнется в 600 LU (>WEAPON_RANGE 150 ⇒ огонь не тормозит и не гасит скорость; <AW2 750 ⇒ игрок в
    //   осведомлённости ⇒ override §5.13.42 берёт игрока целью, atkBest=AW2 нетронут ⇒ pd2=360k<562k). Пин
    //   ПОЗИЦИИ, но НЕ скорости — velocity растёт до effMax (как manhunt-проба). (A) реп −128 (дно вражды):
    //   heat=1 ⇒ множитель 1+GAIN ⇒ vmax≈maxSpeed*(1+GAIN). (B, контроль) реп −48 (порог Hostile, где override
    //   ВСЁ ЕЩЁ берёт игрока целью — aiTarget==PLAYER — но heat=(−48−(−48))/80=0): множитель 1.0 ⇒ vmax≈maxSpeed.
    //   Так проба доказывает, что бонус растёт с ГЛУБИНОЙ вражды, а не просто с фактом охоты (ОБА борта реально
    //   гонятся за игроком — фиксируем tgtOk — разница лишь в скорости). manhunt на этом борту молчит (guard
    //   aiTarget==PLAYER + гейт !defending, ведь override гасит defending) ⇒ два множителя дизъюнктны по построению.
    bool vendettaScalesWithStandingOk = false;
    {
        Game gv; gv.init(1200);
        int pf = gv.playerFaction;
        int F = -1;
        for (int f = 0; f < (int)gv.factions.size(); ++f) if (f != pf) { F = f; break; }
        int star = -1;
        for (int s : stars) { if (s >= 0) { star = s; break; } }
        double vmax[2] = {0.0, 0.0};
        const double MAXS = 60.0;
        const int REP[2] = { -128, -48 };            // (A) дно вражды (heat 1) ; (B) порог Hostile (heat 0, контроль)
        bool aTgt = false, bTgt = false;             // обе фазы РЕАЛЬНО берут игрока целью?
        if (pf >= 0 && F >= 0 && star >= 0) {
            for (int phase = 0; phase < 2; ++phase) {
                LocalScene s;
                buildLocalScene(gv, star, s);
                s.active = true;
                while (s.craft.size() < 1) { LocalCraft nc; s.craft.push_back(nc); }
                s.craft.resize(1);
                LocalCraft& pat = s.craft[0];             // патруль фракции F, НЕ спровоцирован
                pat.kind = CK_PATROL; pat.hostile = false; pat.faction = F; pat.agentIndex = -1;
                pat.heavy = 60.0; pat.light = 0.0; pat.wanted = false;
                pat.maxSpeed = MAXS; pat.accel = 300.0;  // быстрое насыщение до effMax за единицы кадров
                pat.maxHullHP = 100000.0; pat.hullHP = 100000.0;
                pat.maxShield = 0.0; pat.shield = 0.0; pat.fireCooldown = 0.0; pat.boost = 0.0;
                int cur = gv.factionRelation(pf, F);
                gv.adjustFactionRelation(pf, F, REP[phase] - cur);   // посев: rep(pf,F) = REP[phase]
                s.pvx = s.pvy = s.pvz = 0.0;
                for (int f = 0; f < 120; ++f) {
                    LocalCraft& p = s.craft[0];
                    p.x = s.px; p.y = s.py + 600.0; p.z = s.pz;   // 600 LU: >150 (без стрельбы), <750 (в осведомлённости)
                    p.hullHP = p.maxHullHP; p.shield = 0.0; p.boost = 0.0; p.hostile = false;  // держим НЕспровоцированным
                    LocalInput in; in.fire = false;               // игрок не стреляет ⇒ провокации нет
                    updateLocalScene(gv, s, in, dtReal);
                    if (!s.craft.empty()) {
                        LocalCraft& q = s.craft[0];
                        if (q.aiState == 1 && q.aiTarget == LOCAL_TARGET_PLAYER) {
                            if (phase == 0) aTgt = true; else bTgt = true;
                        }
                        double cs = std::sqrt(q.vx*q.vx + q.vy*q.vy + q.vz*q.vz);
                        if (cs > vmax[phase]) vmax[phase] = cs;
                    }
                }
            }
        }
        double expHigh = MAXS * (1.0 + LocalCfg::VENDETTA_SPEED_GAIN);   // heat=1 при реп −128
        double expLow  = MAXS;                                          // heat=0 при реп −48 ⇒ множитель 1.0
        bool highOk  = std::fabs(vmax[0] - expHigh) < 1.5;              // ускорен пропорц. глубине вражды
        bool lowOk   = std::fabs(vmax[1] - expLow)  < 1.0;             // нулевой регресс: ровно maxSpeed
        bool orderOk = vmax[0] > vmax[1] + 1.0;                        // ядро: глубже вражда ⇒ резвее закон
        bool tgtOk   = aTgt && bTgt;                                   // оба борта реально гонятся за игроком
        vendettaScalesWithStandingOk = highOk && lowOk && orderOk && tgtOk;
        std::printf("vendetta-scales-with-standing probe: deepEnmity=%.2f (exp %.2f) threshold=%.2f (exp %.2f) faster=%s bothHuntPlayer=%s\n",
                    vmax[0], expHigh, vmax[1], expLow, orderOk ? "yes (ok)" : "no", tgtOk ? "yes (ok)" : "no");
    }
    if (!vendettaScalesWithStandingOk) { std::printf("INVARIANT FAIL: §5.13.44 vendetta speed scaling did not fire, regressed baseline, or lost player-lock\n"); ++invariantFails; }

    std::printf("SOAK DONE frames=%ld deaths=%ld radioClaims=%ld craftDestroyed=%ld invariantFails=%ld deathPath=%s writeBack=%s sellWriteBack=%s npcWriteBack=%s patrolHuntsWanted=%s packRalliesOutlaw=%s patrolBacksUpSwarmed=%s manhuntScalesWithBounty=%s lawGratitude=%s copKillRaisesBounty=%s factionReprisal=%s lawHuntsHostile=%s patrolTargetSignal=%s vendettaScalesWithStanding=%s rocks=%ld shiny=%ld trades=%ld\n",
                totalFrames, deaths, claims, kills, invariantFails,
                deathPathOk ? "ok" : "UNTRIGGERED", writeBackOk ? "ok" : "FAILED",
                sellWriteBackOk ? "ok" : "FAILED", npcWriteBackOk ? "ok" : "FAILED",
                patrolHuntsWantedOk ? "ok" : "FAILED",
                packRalliesOutlawOk ? "ok" : "FAILED",
                patrolBacksUpSwarmedOk ? "ok" : "FAILED",
                manhuntScalesWithBountyOk ? "ok" : "FAILED",
                lawGratitudeOk ? "ok" : "FAILED",
                copKillRaisesBountyOk ? "ok" : "FAILED",
                factionReprisalOk ? "ok" : "FAILED",
                lawHuntsHostileOk ? "ok" : "FAILED",
                patrolTargetSignalOk ? "ok" : "FAILED",
                vendettaScalesWithStandingOk ? "ok" : "FAILED",
                rocksSeen, rocksShiny, tradesTotal);
    return invariantFails ? 1 : 0;
}
