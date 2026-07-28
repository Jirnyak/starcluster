#include "local.h"
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

// Родригес: поворот вектора (vx,vy,vz) НА МЕСТЕ вокруг ЕДИНИЧНОЙ оси (kx,ky,kz) на угол a.
//   v' = v·cos a + (k×v)·sin a + k·(k·v)·(1−cos a)
// Ось считается единичной (базис корабля ре-ортонормируется каждый кадр в updateLocalScene).
static void rodriguesRotate(double& vx, double& vy, double& vz,
                            double kx, double ky, double kz, double a) {
    const double c = std::cos(a), s = std::sin(a), omc = 1.0 - c;
    const double dot = kx * vx + ky * vy + kz * vz;
    const double cx = ky * vz - kz * vy;   // k × v
    const double cy = kz * vx - kx * vz;
    const double cz = kx * vy - ky * vx;
    const double nx = vx * c + cx * s + kx * dot * omc;
    const double ny = vy * c + cy * s + ky * dot * omc;
    const double nz = vz * c + cz * s + kz * dot * omc;
    vx = nx; vy = ny; vz = nz;
}

// ============================================================================
//  Один кадр локальной сцены: полёт игрока, орбиты, стрельба, умный ИИ NPC, бой
//  (со щитами и убиваемым игроком), дрейф пояса, лут, частицы-juice, добыча руды
//  (с уважением к трюму) и стыковка. Детерминизм макро-симуляции не трогаем —
//  глобальный rng НЕ используем; при нужде берём ЛОКАЛЬНЫЙ движок, засеянный
//  только от локальных величин. Возвращает индекс звезды для открытия рынка
//  (стыковка), иначе -1.
// ============================================================================
int updateLocalScene(Game& game, LocalScene& scene, const LocalInput& in, double dtReal) {
    // ---- Модель времени ----
    scene.warping = in.warp;
    if (dtReal < 0.0) dtReal = 0.0;
    if (dtReal > 0.1) dtReal = 0.1;                     // защита от длинных кадров
    double timeScale = LocalCfg::HOURS_PER_SEC * (in.warp ? LocalCfg::WARP_MULT : 1.0);
    double dtHours = dtReal * timeScale;

    // ---- (A) РЕАЛЬНОЕ время для fx + затухание тряски/вспышки (независимо от warp) ----
    scene.fxClock += dtReal;
    scene.shake *= std::max(0.0, 1.0 - dtReal * 7.0);
    if (scene.shake < 0.1) scene.shake = 0.0;
    scene.playerHitFlash = std::max(0.0, scene.playerHitFlash - dtReal);

    // Субшаги против «тоннелирования» снарядов сквозь цели.
    int N = (int)std::ceil(dtHours / 0.05);
    if (N < 1) N = 1;
    if (N > 64) N = 64;
    double h = dtHours / (double)N;

    bool playerValid = (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size());

    // ---- (B/D) Локальные помощники: урон сквозь щит, искры, обломки+лут ----
    // applyDamage: щит гасит урон первым, остаток идёт в корпус, сбрасывает таймер регена.
    auto applyDamage = [](double& shield, double& shieldTimer, double& hull, double dmg) {
        if (dmg <= 0.0) return;
        double absorbed = std::min(shield, dmg);
        shield -= absorbed;
        if (shield < 0.0) shield = 0.0;
        double rest = dmg - absorbed;
        if (rest > 0.0) hull -= rest;
        shieldTimer = LocalCfg::SHIELD_REGEN_DELAY_H;
    };

    // spawnSparks: короткие искры попадания (цвет по команде стрелявшего).
    auto spawnSparks = [&scene](double x, double y, double z,
                                uint8_t cr, uint8_t cg, uint8_t cb, int count) {
        std::mt19937 lr((uint32_t)(scene.fx.size() * 2654435761u)
                        ^ (uint32_t)(uint64_t)(scene.localHours * 1000.0)
                        ^ (uint32_t)(uint64_t)(std::fabs(x) * 8.0 + std::fabs(y) * 3.0 + std::fabs(z)));
        std::uniform_real_distribution<double> us(-1.0, 1.0);
        std::uniform_real_distribution<double> u01(0.0, 1.0);
        for (int k = 0; k < count; ++k) {
            if ((int)scene.fx.size() >= LocalCfg::FX_MAX) break;
            LocalFx f;
            f.x = x; f.y = y; f.z = z;
            double sp = 12.0 + u01(lr) * 46.0;
            f.vx = us(lr) * sp; f.vy = us(lr) * sp; f.vz = us(lr) * sp * 0.5;
            f.kind = FX_SPARK;
            f.size = 0.8 + u01(lr) * 0.9;
            f.life = 0.10 + u01(lr) * 0.14; f.maxLife = f.life;
            f.r = cr; f.g = cg; f.b = cb; f.a = 255;
            scene.fx.push_back(f);
        }
    };

    // spawnWreck: ударная волна + обломки + дым + 1..3 контейнера лута в точке гибели.
    auto spawnWreck = [&scene](double x, double y, double z,
                               double vx, double vy, double vz,
                               uint8_t cr, uint8_t cg, uint8_t cb) {
        std::mt19937 lr((uint32_t)(scene.fx.size() * 2654435761u)
                        ^ (uint32_t)(uint64_t)(scene.localHours * 1000.0)
                        ^ (uint32_t)(scene.loot.size() * 40503u)
                        ^ (uint32_t)(uint64_t)(std::fabs(x) * 16.0 + std::fabs(y) * 4.0 + std::fabs(z)));
        std::uniform_real_distribution<double> us(-1.0, 1.0);
        std::uniform_real_distribution<double> u01(0.0, 1.0);
        // Ударная волна (кольцо; растёт в draw).
        if ((int)scene.fx.size() < LocalCfg::FX_MAX) {
            LocalFx f;
            f.x = x; f.y = y; f.z = z;
            f.vx = 0.0; f.vy = 0.0; f.vz = 0.0;
            f.kind = FX_RING; f.size = 18.0;
            f.life = 0.8; f.maxLife = 0.8;
            f.r = cr; f.g = cg; f.b = cb; f.a = 255;
            scene.fx.push_back(f);
        }
        // Обломки (~10).
        for (int k = 0; k < 10; ++k) {
            if ((int)scene.fx.size() >= LocalCfg::FX_MAX) break;
            LocalFx f;
            f.x = x; f.y = y; f.z = z;
            double sp = 22.0 + u01(lr) * 64.0;
            f.vx = vx + us(lr) * sp; f.vy = vy + us(lr) * sp; f.vz = vz + us(lr) * sp * 0.6;
            f.kind = FX_DEBRIS;
            f.size = 1.0 + u01(lr) * 1.8;
            f.life = 1.2 + u01(lr) * 0.6; f.maxLife = f.life;
            if (u01(lr) < 0.5) { f.r = cr; f.g = cg; f.b = cb; }
            else { f.r = 255; f.g = (uint8_t)(150.0 + u01(lr) * 80.0); f.b = 60; } // тёплые искры
            f.a = 255;
            scene.fx.push_back(f);
        }
        // Дым (несколько).
        for (int k = 0; k < 3; ++k) {
            if ((int)scene.fx.size() >= LocalCfg::FX_MAX) break;
            LocalFx f;
            f.x = x; f.y = y; f.z = z;
            f.vx = vx * 0.3 + us(lr) * 8.0; f.vy = vy * 0.3 + us(lr) * 8.0; f.vz = vz * 0.3 + us(lr) * 8.0;
            f.kind = FX_SMOKE;
            f.size = 3.0 + u01(lr) * 3.0;
            f.life = 1.6 + u01(lr) * 1.0; f.maxLife = f.life;
            f.r = 90; f.g = 88; f.b = 92; f.a = 150;
            scene.fx.push_back(f);
        }
        // Лут: 1..3 контейнера с элементом (руда пояса, если есть, иначе средний элемент).
        size_t ec = elementCount();
        int el;
        if (!scene.rocks.empty()) {
            el = scene.rocks[(size_t)(lr() % (uint32_t)scene.rocks.size())].element;
        } else {
            el = (ec > 0) ? (int)(ec / 2) : 0;
        }
        if (el < 0) el = 0;
        if (ec > 0 && el >= (int)ec) el = (int)ec - 1;
        int nLoot = 1 + (int)(u01(lr) * 3.0); // 1..3
        if (nLoot > 3) nLoot = 3;
        for (int k = 0; k < nLoot; ++k) {
            if ((int)scene.loot.size() >= 200) break;
            LocalLoot lt;
            lt.x = x + us(lr) * 4.0; lt.y = y + us(lr) * 4.0; lt.z = z + us(lr) * 4.0;
            lt.vx = vx * 0.2 + us(lr) * 6.0; lt.vy = vy * 0.2 + us(lr) * 6.0; lt.vz = vz * 0.2 + us(lr) * 6.0;
            lt.element = el;
            lt.amount = 3.0 + u01(lr) * 9.0; // 3..12
            lt.life = LocalCfg::LOOT_LIFE_HOURS;
            lt.spin = u01(lr) * 6.28318;
            scene.loot.push_back(lt);
        }
    };

    // ---- Ориентация корабля: ТЕЛО-ОТНОСИТЕЛЬНОЕ вращение базиса (полное 3D, по РЕАЛЬНОМУ времени) ----
    //  yaw (A/D) — вокруг pup; pitch (R/F) — вокруг right (крутит И нос, И «вверх»); roll (Q/E) — вокруг pfwd.
    //  Знаки: yawL=+, pitchU=+ — как в прежней Эйлеровой схеме. Крен: rollR (E) = +TURN_RATE => КРЕН ВПРАВО
    //  (правое крыло вниз: положительный поворот pup вокруг pfwd в правой тройке right=pfwd×pup); rollL (Q) = −.
    //  Клампа тангажа НЕТ — полное 3D, за ориентацию отвечает крен. Базис ре-ортонормируется каждый кадр.
    //  TURN_RATE — рад/РЕАЛЬНУЮ секунду (не зависит от warp), поэтому умножаем на dtReal, а не dtHours.
    {
        const double turn = LocalCfg::TURN_RATE * dtReal;
        double rx, ry, rz;
        localShipRight(scene, rx, ry, rz);              // текущая ось «право» = pfwd × pup
        const double yawA   = (in.yawL   ? turn : 0.0) - (in.yawR   ? turn : 0.0);
        const double pitchA = (in.pitchU ? turn : 0.0) - (in.pitchD ? turn : 0.0);
        const double rollA  = (in.rollR  ? turn : 0.0) - (in.rollL  ? turn : 0.0); // E банкует вправо
        if (yawA != 0.0)                                // YAW: нос вокруг «вверх».
            rodriguesRotate(scene.pfwdX, scene.pfwdY, scene.pfwdZ, scene.pupX, scene.pupY, scene.pupZ, yawA);
        if (pitchA != 0.0) {                            // PITCH: и нос, и «вверх» вокруг «право».
            rodriguesRotate(scene.pfwdX, scene.pfwdY, scene.pfwdZ, rx, ry, rz, pitchA);
            rodriguesRotate(scene.pupX,  scene.pupY,  scene.pupZ,  rx, ry, rz, pitchA);
        }
        if (rollA != 0.0)                               // ROLL: «вверх» вокруг носа.
            rodriguesRotate(scene.pupX, scene.pupY, scene.pupZ, scene.pfwdX, scene.pfwdY, scene.pfwdZ, rollA);

        // Ре-ортонормировка (Грам–Шмидт) против численного дрейфа; защита от вырождения.
        double fl = std::sqrt(scene.pfwdX*scene.pfwdX + scene.pfwdY*scene.pfwdY + scene.pfwdZ*scene.pfwdZ);
        if (fl < 1e-9) { scene.pfwdX = 1.0; scene.pfwdY = 0.0; scene.pfwdZ = 0.0; fl = 1.0; }
        const double finv = 1.0 / fl;
        scene.pfwdX *= finv; scene.pfwdY *= finv; scene.pfwdZ *= finv;
        double d = scene.pupX*scene.pfwdX + scene.pupY*scene.pfwdY + scene.pupZ*scene.pfwdZ;
        scene.pupX -= d * scene.pfwdX; scene.pupY -= d * scene.pfwdY; scene.pupZ -= d * scene.pfwdZ;
        double ul = std::sqrt(scene.pupX*scene.pupX + scene.pupY*scene.pupY + scene.pupZ*scene.pupZ);
        if (ul < 1e-9) {                                // pup схлопнулся на pfwd — берём устойчивый ⟂ носу
            if (std::fabs(scene.pfwdX) < 0.9) { scene.pupX = 1.0; scene.pupY = 0.0; scene.pupZ = 0.0; }
            else                              { scene.pupX = 0.0; scene.pupY = 1.0; scene.pupZ = 0.0; }
            d = scene.pupX*scene.pfwdX + scene.pupY*scene.pfwdY + scene.pupZ*scene.pfwdZ;
            scene.pupX -= d * scene.pfwdX; scene.pupY -= d * scene.pfwdY; scene.pupZ -= d * scene.pfwdZ;
            ul = std::sqrt(scene.pupX*scene.pupX + scene.pupY*scene.pupY + scene.pupZ*scene.pupZ);
        }
        const double uinv = 1.0 / ul;
        scene.pupX *= uinv; scene.pupY *= uinv; scene.pupZ *= uinv;
    }

    // Нос корабля (единичный) — источник для тяги/огня/дульной вспышки/выхлопа ниже.
    // Имена dirx/diry/dirz СОХРАНЕНЫ, чтобы весь низлежащий код работал без изменений.
    double dirx = scene.pfwdX, diry = scene.pfwdY, dirz = scene.pfwdZ;

    // ---- Орбиты тел (раз в кадр) ----
    //  Луна (b.parent>=0) позиционируется ОТНОСИТЕЛЬНО родителя. Луны в векторе идут
    //  ПОСЛЕ родителя (больший индекс), а обход — по возрастанию индекса, поэтому
    //  родитель уже пересчитан на этой же итерации к моменту обработки луны.
    for (size_t i = 0; i < scene.bodies.size(); ++i) {
        LocalBody& b = scene.bodies[i];
        double angle = b.orbitPhase + b.orbitSpeed * scene.localHours;
        double ox = b.orbitRadius * std::cos(angle);
        double oy = b.orbitRadius * std::sin(angle) * std::cos(b.inclination);
        double oz = b.orbitRadius * std::sin(angle) * std::sin(b.inclination);
        if (b.parent >= 0 && b.parent < (int)scene.bodies.size()) {
            b.x = scene.bodies[b.parent].x + ox;
            b.y = scene.bodies[b.parent].y + oy;
            b.z = scene.bodies[b.parent].z + oz;
        } else {
            b.x = ox; b.y = oy; b.z = oz;
        }
    }

    // ---- (G) Дрейф пояса + собственное вращение астероидов ----
    if (!scene.rocks.empty()) {
        for (size_t i = 0; i < scene.rocks.size(); ++i) {
            LocalRock& rk = scene.rocks[i];
            rk.orbitAng += rk.orbitVel * dtHours;
            rk.spin     += rk.spinVel  * dtHours;
            rk.x = rk.orbitR * std::cos(rk.orbitAng);
            rk.y = rk.orbitR * std::sin(rk.orbitAng);
            // z сохраняем как есть
        }
    }

    scene.localHours += dtHours;

    // ---- (B) Реген щита игрока (раз в кадр) ----
    if (playerValid) {
        if (scene.pShieldTimer > 0.0) scene.pShieldTimer -= dtHours;
        else scene.pShield = std::min(scene.pMaxShield, scene.pShield + LocalCfg::SHIELD_REGEN_RATE * dtHours);
    }

    // ---- Стрельба игрока (раз в кадр) ----
    scene.fireCooldown -= dtHours;
    if (in.fire && scene.fireCooldown <= 0.0 && playerValid && !scene.playerDestroyed) {
        const Ship& ps = game.agents[game.playerAgent].ship;
        double dmg = ps.heavyWeapons * 0.6 + ps.lightWeapons * 0.4 + 2.0; // сырые поля, chromocore запечён
        LocalProjectile p;
        p.x = scene.px; p.y = scene.py; p.z = scene.pz;
        p.vx = scene.pvx + dirx * LocalCfg::PROJ_SPEED;
        p.vy = scene.pvy + diry * LocalCfg::PROJ_SPEED;
        p.vz = scene.pvz + dirz * LocalCfg::PROJ_SPEED;
        p.team = 0;
        p.damage = dmg;
        p.life = LocalCfg::PROJ_LIFE_HOURS;
        p.r = 130; p.g = 220; p.b = 255;
        scene.shots.push_back(p);
        scene.fireCooldown = LocalCfg::FIRE_COOLDOWN_H;
        scene.shake = std::min(40.0, std::max(scene.shake, 1.5)); // собственный огонь
        // Дульная вспышка.
        if ((int)scene.fx.size() < LocalCfg::FX_MAX) {
            LocalFx f;
            f.x = scene.px + dirx * 3.0; f.y = scene.py + diry * 3.0; f.z = scene.pz + dirz * 3.0;
            f.vx = scene.pvx; f.vy = scene.pvy; f.vz = scene.pvz;
            f.kind = FX_MUZZLE; f.size = 1.6;
            f.life = 0.05; f.maxLife = 0.05;
            f.r = 160; f.g = 230; f.b = 255; f.a = 255;
            scene.fx.push_back(f);
        }
    }

    // ---- (F) Умный ИИ NPC (раз в кадр: реген, выбор цели, состояние, путевая точка, огонь) ----
    //  Цели пересчитываются КАЖДЫЙ кадр (индексы не устаревают между кадрами); удаление
    //  мёртвых кораблей вынесено за субшаги (см. L), поэтому индексы стабильны здесь и в бою.
    for (size_t i = 0; i < scene.craft.size(); ++i) {
        LocalCraft& c = scene.craft[i];
        if (c.hullHP <= 0.0) continue;
        c.fireCooldown  -= dtHours;
        c.retargetTimer -= dtHours;
        if (c.boost > 0.0) c.boost -= dtHours;
        c.hitFlash = std::max(0.0, c.hitFlash - dtReal);

        // Реген щита корабля.
        if (c.shieldRegenTimer > 0.0) c.shieldRegenTimer -= dtHours;
        else c.shield = std::min(c.maxShield, c.shield + LocalCfg::SHIELD_REGEN_RATE * dtHours);

        // Дистанция до игрока (валидная цель, только если игрок жив).
        bool playerTargetable = playerValid && !scene.playerDestroyed;
        double pd2 = 1e30;
        if (playerTargetable) {
            double dx = scene.px - c.x, dy = scene.py - c.y, dz = scene.pz - c.z;
            pd2 = dx*dx + dy*dy + dz*dz;
        }

        const double AW2 = 750.0 * 750.0;   // радиус осведомлённости
        int atkCraft = -1; bool atkPlayer = false; double atkBest = AW2;
        int threatCraft = -1; double threatBest = 380.0 * 380.0;

        if (c.kind == CK_PIRATE) {
            // Пират: ближайший враг среди {игрок, любой НЕ-пират} в радиусе.
            if (playerTargetable && pd2 < atkBest) { atkBest = pd2; atkPlayer = true; atkCraft = -1; }
            for (size_t j = 0; j < scene.craft.size(); ++j) {
                if (j == i) continue;
                LocalCraft& o = scene.craft[j];
                if (o.hullHP <= 0.0 || o.kind == CK_PIRATE) continue;
                double dx = o.x - c.x, dy = o.y - c.y, dz = o.z - c.z;
                double d2 = dx*dx + dy*dy + dz*dz;
                if (d2 < atkBest) { atkBest = d2; atkCraft = (int)j; atkPlayer = false; }
            }
        } else if (c.kind == CK_PATROL) {
            // Патруль: ближайший пират в радиусе; спровоцированный — ещё и игрок.
            for (size_t j = 0; j < scene.craft.size(); ++j) {
                if (j == i) continue;
                LocalCraft& o = scene.craft[j];
                if (o.hullHP <= 0.0 || o.kind != CK_PIRATE) continue;
                double dx = o.x - c.x, dy = o.y - c.y, dz = o.z - c.z;
                double d2 = dx*dx + dy*dy + dz*dz;
                if (d2 < atkBest) { atkBest = d2; atkCraft = (int)j; atkPlayer = false; }
            }
            if (c.hostile && playerTargetable && pd2 < atkBest) { atkBest = pd2; atkPlayer = true; atkCraft = -1; }
        } else { // CK_TRADER / CK_CIVILIAN
            // Спровоцированный торговец/гражданский считает игрока целью.
            if (c.hostile && playerTargetable && pd2 < atkBest) { atkBest = pd2; atkPlayer = true; atkCraft = -1; }
            // Иначе — бежит от ближайшего пирата в радиусе бегства.
            for (size_t j = 0; j < scene.craft.size(); ++j) {
                if (j == i) continue;
                LocalCraft& o = scene.craft[j];
                if (o.hullHP <= 0.0 || o.kind != CK_PIRATE) continue;
                double dx = o.x - c.x, dy = o.y - c.y, dz = o.z - c.z;
                double d2 = dx*dx + dy*dy + dz*dz;
                if (d2 < threatBest) { threatBest = d2; threatCraft = (int)j; }
            }
        }

        bool haveAtk = (atkPlayer || atkCraft >= 0);
        double tgx = 0, tgy = 0, tgz = 0, tvx = 0, tvy = 0, tvz = 0, tdist = 0.0;
        if (haveAtk) {
            if (atkPlayer) {
                tgx = scene.px; tgy = scene.py; tgz = scene.pz;
                tvx = scene.pvx; tvy = scene.pvy; tvz = scene.pvz;
            } else {
                LocalCraft& o = scene.craft[atkCraft];
                tgx = o.x; tgy = o.y; tgz = o.z;
                tvx = o.vx; tvy = o.vy; tvz = o.vz;
            }
            tdist = std::sqrt(atkBest);
        }

        double selfHull = (c.maxHullHP > 1e-6) ? (c.hullHP / c.maxHullHP) : 1.0;
        bool lowHull = (selfHull < 0.30);
        bool threatened = (threatCraft >= 0);

        // Источник угрозы для бегства: свой критический корпус — от боевой цели; торговец — от пирата.
        double thx = 0, thy = 0, thz = 0; bool fleeing = false;
        if (lowHull && haveAtk) { thx = tgx; thy = tgy; thz = tgz; fleeing = true; }
        else if (threatened) {
            LocalCraft& o = scene.craft[threatCraft];
            thx = o.x; thy = o.y; thz = o.z; fleeing = true;
        }

        if (fleeing)      c.aiState = 2;
        else if (haveAtk) c.aiState = 1;
        else              c.aiState = 0;

        if (c.aiState == 2) {
            // Бегство: путевая точка в направлении (self - threat) на 400 LU + короткий форсаж.
            double dx = c.x - thx, dy = c.y - thy, dz = c.z - thz;
            double d = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (d < 1e-6) { dx = 1.0; dy = 0.0; dz = 0.0; d = 1.0; }
            double inv = 1.0 / d;
            c.tx = c.x + dx * inv * 400.0;
            c.ty = c.y + dy * inv * 400.0;
            c.tz = c.z + dz * inv * 400.0;
            c.boost = 2.0;          // короткий форсаж отрыва (часы), NPC_BOOST_MULT к скорости
            c.thrustGlow = 1.0;
        } else if (c.aiState == 1) {
            if (tdist > 95.0) {
                c.tx = tgx; c.ty = tgy; c.tz = tgz; // сближение
            } else {
                // Страйф: устойчивый перпендикуляр к вектору «на цель» (r × Z, при вырождении — r × Y).
                double rx = tgx - c.x, ry = tgy - c.y, rz = tgz - c.z;
                double px = ry * 1.0 - rz * 0.0;   // r × (0,0,1) = (ry, -rx, 0)
                double py = rz * 0.0 - rx * 1.0;
                double pz = rx * 0.0 - ry * 0.0;
                double pl = std::sqrt(px*px + py*py + pz*pz);
                if (pl < 1e-6) {                   // почти вертикальный вектор — берём ось Y
                    px = ry * 0.0 - rz * 1.0;      // r × (0,1,0) = (-rz, 0, rx)
                    py = rz * 0.0 - rx * 0.0;
                    pz = rx * 1.0 - ry * 0.0;
                    pl = std::sqrt(px*px + py*py + pz*pz);
                }
                if (pl < 1e-6) pl = 1.0;
                double inv = 70.0 / pl;
                c.tx = tgx + px * inv;
                c.ty = tgy + py * inv;
                c.tz = tgz + pz * inv;
            }
            c.thrustGlow = 1.0;
        } else {
            // (0) Обычное блуждание — прежняя логика на retargetTimer (локальный движок).
            if (c.retargetTimer <= 0.0) {
                std::mt19937 wr((uint32_t)i * 2654435761u ^ (uint32_t)(int)scene.localHours);
                if (!scene.bodies.empty()) {
                    int bi = (int)(wr() % (uint32_t)scene.bodies.size());
                    c.tx = scene.bodies[bi].x; c.ty = scene.bodies[bi].y; c.tz = scene.bodies[bi].z;
                } else {
                    std::uniform_real_distribution<double> u(-800.0, 800.0);
                    c.tx = u(wr); c.ty = u(wr); c.tz = u(wr);
                }
                std::uniform_real_distribution<double> ur(6.0, 14.0);
                c.retargetTimer = ur(wr);
            }
            c.thrustGlow = 0.6;
        }

        // Огонь с УПРЕЖДЕНИЕМ (только в атаке, в пределах дальности, по перезарядке).
        if (c.aiState == 1 && haveAtk && c.fireCooldown <= 0.0 && tdist < LocalCfg::WEAPON_RANGE) {
            double t = tdist / LocalCfg::PROJ_SPEED;
            double lx = tgx + tvx * t, ly = tgy + tvy * t, lz = tgz + tvz * t; // предсказанная позиция
            double dx = lx - c.x, dy = ly - c.y, dz = lz - c.z;
            double d = std::sqrt(dx*dx + dy*dy + dz*dz);
            double inv = (d > 1e-6) ? 1.0 / d : 0.0;
            double dmg = c.heavy * 0.6 + c.light * 0.4 + 1.0;
            if (atkPlayer) {
                // По игроку — реальный снаряд team=1 в упреждённом направлении.
                LocalProjectile p;
                p.x = c.x; p.y = c.y; p.z = c.z;
                p.vx = dx * inv * LocalCfg::PROJ_SPEED;
                p.vy = dy * inv * LocalCfg::PROJ_SPEED;
                p.vz = dz * inv * LocalCfg::PROJ_SPEED;
                p.team = 1;
                p.damage = dmg;
                p.life = LocalCfg::PROJ_LIFE_HOURS;
                p.r = 255; p.g = 90; p.b = 70;
                scene.shots.push_back(p);
                c.fireCooldown = LocalCfg::FIRE_COOLDOWN_H * 1.5;
                if ((int)scene.fx.size() < LocalCfg::FX_MAX) {
                    LocalFx f;
                    f.x = c.x + dx * inv * 3.0; f.y = c.y + dy * inv * 3.0; f.z = c.z + dz * inv * 3.0;
                    f.vx = c.vx; f.vy = c.vy; f.vz = c.vz;
                    f.kind = FX_MUZZLE; f.size = 1.4;
                    f.life = 0.05; f.maxLife = 0.05;
                    f.r = 255; f.g = 140; f.b = 90; f.a = 255;
                    scene.fx.push_back(f);
                }
            } else {
                // NPC против NPC — прямой DPS этим кадром (индексы снарядов небезопасны).
                LocalCraft& o = scene.craft[atkCraft];
                double before = o.hullHP;
                applyDamage(o.shield, o.shieldRegenTimer, o.hullHP, dmg);
                o.hitFlash = 0.4;
                c.fireCooldown = LocalCfg::FIRE_COOLDOWN_H * 1.5;
                // Косметический трассер: искры на цели + короткий оранжевый шлейф вдоль линии.
                spawnSparks(o.x, o.y, o.z, 255, 150, 80, 2);
                if ((int)scene.fx.size() < LocalCfg::FX_MAX) {
                    LocalFx f;
                    f.x = c.x + dx * inv * 6.0; f.y = c.y + dy * inv * 6.0; f.z = c.z + dz * inv * 6.0;
                    f.vx = dx * inv * 120.0; f.vy = dy * inv * 120.0; f.vz = dz * inv * 120.0;
                    f.kind = FX_TRAIL; f.size = 1.0;
                    f.life = 0.06; f.maxLife = 0.06;
                    f.r = 255; f.g = 150; f.b = 80; f.a = 220;
                    scene.fx.push_back(f);
                }
                if (before > 0.0 && o.hullHP <= 0.0) {
                    spawnWreck(o.x, o.y, o.z, o.vx, o.vy, o.vz, o.r, o.g, o.b); // без награды/фракций
                }
            }
        }
    }

    // ---- Субшаговая физика: полёт, движение NPC, снаряды, столкновения ----
    for (int step = 0; step < N; ++step) {
        // Полёт игрока (инерционный).
        if (in.thrust) {
            scene.pvx += dirx * scene.playerAccel * h;
            scene.pvy += diry * scene.playerAccel * h;
            scene.pvz += dirz * scene.playerAccel * h;
        }
        if (in.brake) {
            double speed = std::sqrt(scene.pvx*scene.pvx + scene.pvy*scene.pvy + scene.pvz*scene.pvz);
            if (speed > 1e-6) {
                double dv = std::min(speed, scene.playerAccel * h);
                double f = (speed - dv) / speed;
                scene.pvx *= f; scene.pvy *= f; scene.pvz *= f;
            }
        }
        {
            double speed = std::sqrt(scene.pvx*scene.pvx + scene.pvy*scene.pvy + scene.pvz*scene.pvz);
            if (speed > scene.playerMaxSpeed && speed > 1e-9) {
                double f = scene.playerMaxSpeed / speed;
                scene.pvx *= f; scene.pvy *= f; scene.pvz *= f;
            }
        }
        scene.px += scene.pvx * h;
        scene.py += scene.pvy * h;
        scene.pz += scene.pvz * h;

        // Движение NPC: разгон к цели, ограничение скорости (с форсажем), интеграция.
        for (size_t i = 0; i < scene.craft.size(); ++i) {
            LocalCraft& c = scene.craft[i];
            if (c.hullHP <= 0.0) continue;
            double dx = c.tx - c.x, dy = c.ty - c.y, dz = c.tz - c.z;
            double d = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (d > 1e-6) {
                double inv = 1.0 / d;
                c.vx += dx * inv * c.accel * h;
                c.vy += dy * inv * c.accel * h;
                c.vz += dz * inv * c.accel * h;
            }
            double effMax = c.maxSpeed * ((c.boost > 0.0) ? LocalCfg::NPC_BOOST_MULT : 1.0);
            double cs = std::sqrt(c.vx*c.vx + c.vy*c.vy + c.vz*c.vz);
            if (cs > effMax && cs > 1e-9) {
                double f = effMax / cs;
                c.vx *= f; c.vy *= f; c.vz *= f;
            }
            c.x += c.vx * h; c.y += c.vy * h; c.z += c.vz * h;
        }

        // Снаряды: интеграция и время жизни.
        for (size_t i = 0; i < scene.shots.size(); ++i) {
            LocalProjectile& p = scene.shots[i];
            p.x += p.vx * h; p.y += p.vy * h; p.z += p.vz * h;
            p.life -= h;
        }

        // Столкновения.
        for (size_t i = 0; i < scene.shots.size(); ++i) {
            LocalProjectile& p = scene.shots[i];
            if (p.life <= 0.0) continue;
            if (p.team == 0) {
                // Выстрелы игрока против кораблей.
                for (size_t j = 0; j < scene.craft.size(); ++j) {
                    LocalCraft& c = scene.craft[j];
                    if (c.hullHP <= 0.0) continue;
                    double dx = p.x - c.x, dy = p.y - c.y, dz = p.z - c.z;
                    double hr = 5.0 + c.armor * 0.1;
                    if (dx*dx + dy*dy + dz*dz < hr*hr) {
                        double before = c.hullHP;
                        applyDamage(c.shield, c.shieldRegenTimer, c.hullHP, p.damage);
                        c.hitFlash = 0.4;
                        p.life = -1.0;                        // снаряд израсходован
                        spawnSparks(p.x, p.y, p.z, 130, 220, 255, 4); // (E) циан-искры игрока
                        if (before > 0.0 && c.hullHP <= 0.0 && playerValid) {
                            // (E) Убийство: награда + исследование + тост + разбор + тряска + реакция фракции.
                            double bounty = 150.0 + c.maxHullHP * 6.0;
                            game.agents[game.playerAgent].money += bounty;
                            game.addResearch(3.0);
                            scene.toast = "SHIP DOWN +" + std::to_string((int)bounty) + " CR";
                            scene.toastTimer = 2.5;
                            spawnWreck(c.x, c.y, c.z, c.vx, c.vy, c.vz, c.r, c.g, c.b);
                            scene.shake = std::min(40.0, std::max(scene.shake, 14.0));
                            if (c.faction >= 0 && !c.hostile && game.playerFaction >= 0) {
                                int fac = c.faction;
                                game.adjustFactionRelation(game.playerFaction, fac, -8);
                                game.pushNews("Reprisal: faction ship destroyed", 3);
                                // Все уцелевшие корабли той же фракции звереют на игрока.
                                for (size_t k = 0; k < scene.craft.size(); ++k) {
                                    if (k == j) continue;
                                    LocalCraft& o = scene.craft[k];
                                    if (o.hullHP > 0.0 && o.faction == fac) { o.hostile = true; o.aiState = 1; }
                                }
                            }
                        }
                        break;                                 // один выстрел — одно попадание
                    }
                }
            } else {
                // Выстрелы врагов против игрока (игрок ТЕПЕРЬ убиваем).
                double dx = p.x - scene.px, dy = p.y - scene.py, dz = p.z - scene.pz;
                if (dx*dx + dy*dy + dz*dz < 5.0 * 5.0) {
                    if (playerValid && !scene.playerDestroyed) {
                        Ship& ps = game.agents[game.playerAgent].ship;
                        double before = ps.hullHP;
                        applyDamage(scene.pShield, scene.pShieldTimer, ps.hullHP, p.damage);
                        bool hullHit = (ps.hullHP < before);
                        if (hullHit) {
                            scene.playerHitFlash = 0.6;        // (C) вигнетка урона
                            scene.toast = "HULL HIT"; scene.toastTimer = 1.5;
                            scene.shake = std::min(40.0, std::max(scene.shake, 9.0));
                        } else {
                            scene.toast = "SHIELD"; scene.toastTimer = 1.0;
                            scene.shake = std::min(40.0, std::max(scene.shake, 4.0));
                        }
                        spawnSparks(p.x, p.y, p.z, 255, 90, 70, 4); // (E) красные искры врага
                        // (C) Переход корпуса из >0 в <=0 — гибель игрока.
                        if (before > 0.0 && ps.hullHP <= 0.0 && !scene.playerDestroyed) {
                            scene.playerDestroyed = true;
                            std::mt19937 lr((uint32_t)(scene.fx.size() * 2654435761u)
                                            ^ (uint32_t)(uint64_t)(scene.localHours * 1000.0));
                            std::uniform_real_distribution<double> us(-1.0, 1.0);
                            std::uniform_real_distribution<double> u01(0.0, 1.0);
                            if ((int)scene.fx.size() < LocalCfg::FX_MAX) {   // большое кольцо ~30
                                LocalFx f; f.x = scene.px; f.y = scene.py; f.z = scene.pz;
                                f.kind = FX_RING; f.size = 30.0; f.life = 1.0; f.maxLife = 1.0;
                                f.r = 255; f.g = 200; f.b = 120; f.a = 255; scene.fx.push_back(f);
                            }
                            for (int k = 0; k < 14; ++k) {                   // ~14 обломков
                                if ((int)scene.fx.size() >= LocalCfg::FX_MAX) break;
                                LocalFx f; f.x = scene.px; f.y = scene.py; f.z = scene.pz;
                                double sp = 25.0 + u01(lr) * 75.0;
                                f.vx = scene.pvx + us(lr) * sp; f.vy = scene.pvy + us(lr) * sp; f.vz = scene.pvz + us(lr) * sp * 0.6;
                                f.kind = FX_DEBRIS; f.size = 1.2 + u01(lr) * 2.0;
                                f.life = 1.4 + u01(lr) * 0.8; f.maxLife = f.life;
                                if (u01(lr) < 0.5) { f.r = 120; f.g = 180; f.b = 255; }
                                else { f.r = 255; f.g = (uint8_t)(160.0 + u01(lr) * 80.0); f.b = 70; }
                                f.a = 255; scene.fx.push_back(f);
                            }
                            for (int k = 0; k < 5; ++k) {                    // немного дыма
                                if ((int)scene.fx.size() >= LocalCfg::FX_MAX) break;
                                LocalFx f; f.x = scene.px; f.y = scene.py; f.z = scene.pz;
                                f.vx = us(lr) * 10.0; f.vy = us(lr) * 10.0; f.vz = us(lr) * 10.0;
                                f.kind = FX_SMOKE; f.size = 4.0 + u01(lr) * 4.0;
                                f.life = 2.0 + u01(lr) * 1.0; f.maxLife = f.life;
                                f.r = 90; f.g = 88; f.b = 92; f.a = 160; scene.fx.push_back(f);
                            }
                            scene.shake = 28.0;
                            scene.toast = "HULL BREACH"; scene.toastTimer = 3.0;
                        }
                    }
                    p.life = -1.0;
                }
            }
        }

        // Убираем «мёртвые» снаряды (swap-pop) — каждый субшаг.
        for (size_t i = 0; i < scene.shots.size(); ) {
            if (scene.shots[i].life <= 0.0) {
                scene.shots[i] = scene.shots.back();
                scene.shots.pop_back();
            } else {
                ++i;
            }
        }
    }

    // ---- (L) Удаляем уничтоженные корабли ОДИН раз после субшагов (индексы были стабильны) ----
    for (size_t i = 0; i < scene.craft.size(); ) {
        if (scene.craft[i].hullHP <= 0.0) {
            scene.craft[i] = scene.craft.back();
            scene.craft.pop_back();
        } else {
            ++i;
        }
    }

    // ---- (H) Выхлопной шлейф двигателя игрока ----
    scene.thrusting = in.thrust;
    if (in.thrust && playerValid && !scene.playerDestroyed) {
        std::mt19937 lr((uint32_t)(scene.fx.size() * 2654435761u)
                        ^ (uint32_t)(uint64_t)(scene.localHours * 1000.0)
                        ^ (uint32_t)(uint64_t)(scene.fxClock * 733.0));
        std::uniform_real_distribution<double> uj(-6.0, 6.0);
        int n = 1 + (int)(lr() % 2u); // 1..2
        for (int k = 0; k < n; ++k) {
            if ((int)scene.fx.size() >= LocalCfg::FX_MAX) break;
            LocalFx f;
            f.x = scene.px - dirx * 4.0; f.y = scene.py - diry * 4.0; f.z = scene.pz - dirz * 4.0;
            f.vx = scene.pvx - dirx * 20.0 + uj(lr);
            f.vy = scene.pvy - diry * 20.0 + uj(lr);
            f.vz = scene.pvz - dirz * 20.0 + uj(lr);
            f.kind = FX_TRAIL; f.size = 1.2;
            f.life = 0.5; f.maxLife = 0.5;
            f.r = 255; f.g = 180; f.b = 90; f.a = 220;
            scene.fx.push_back(f);
        }
    }

    // ---- Добыча руды ----
    if (in.mineToggle) {
        if (scene.miningRock >= 0) {
            scene.miningRock = -1;
            scene.toast = "MINING OFF";
            scene.toastTimer = 2.0;
        } else {
            int best = -1; double bestD2 = 0.0;
            for (size_t i = 0; i < scene.rocks.size(); ++i) {
                LocalRock& rk = scene.rocks[i];
                if (rk.ore <= 0.0) continue;
                double dx = rk.x - scene.px, dy = rk.y - scene.py, dz = rk.z - scene.pz;
                double d2 = dx*dx + dy*dy + dz*dz;
                double range = rk.radius + LocalCfg::MINE_RANGE;
                if (d2 <= range*range && (best < 0 || d2 < bestD2)) { best = (int)i; bestD2 = d2; }
            }
            if (best >= 0) {
                scene.miningRock = best;
                scene.miningAccum = 0.0;
                size_t ec = elementCount();
                if (ec > 0) {
                    int el = scene.rocks[best].element;
                    if (el < 0) el = 0;
                    if (el >= (int)ec) el = (int)ec - 1;
                    scene.toast = std::string("MINING ") + elementDefinitions()[el].symbol;
                } else {
                    scene.toast = "MINING";
                }
                scene.toastTimer = 2.0;
            }
        }
    }
    if (scene.miningRock >= 0 && scene.miningRock < (int)scene.rocks.size() && playerValid) {
        LocalRock& rk = scene.rocks[scene.miningRock];
        double dx = rk.x - scene.px, dy = rk.y - scene.py, dz = rk.z - scene.pz;
        double d2 = dx*dx + dy*dy + dz*dz;
        double range = rk.radius + LocalCfg::MINE_RANGE;
        size_t ec = elementCount();
        if (rk.ore > 0.0 && d2 <= range*range && ec > 0) {
            double rate = 9.0;
            double amt = rate * dtHours;               // масса за кадр
            int el = rk.element;
            if (el < 0) el = 0;
            if (el >= (int)ec) el = (int)ec - 1;
            const char* sym = elementDefinitions()[el].symbol;
            double unit = std::max(0.001, resourceUnitMassByIndex(el));
            Ship& ps = game.agents[game.playerAgent].ship;
            if (shipCargoMass(ps) + amt * unit > ps.cargoCapacity) {
                scene.miningRock = -1;
                scene.toast = "CARGO FULL";
                scene.toastTimer = 2.0;
            } else {
                amt = std::min(amt, rk.ore);
                std::string s(sym);
                bool found = false;
                for (size_t k = 0; k < ps.cargo.size(); ++k) {
                    if (ps.cargo[k].element == s) { ps.cargo[k].amount += amt; found = true; break; }
                }
                if (!found) ps.cargo.emplace_back(s, amt);
                rk.ore -= amt;
                scene.miningAccum += amt;
                game.addResearch(0.2 * dtHours);
                // (I) Пыль добычи: изредка тусклая дымка от астероида к игроку.
                if ((int)scene.fx.size() < LocalCfg::FX_MAX) {
                    std::mt19937 lr((uint32_t)(scene.fx.size() * 2654435761u)
                                    ^ (uint32_t)(uint64_t)(scene.fxClock * 911.0)
                                    ^ (uint32_t)(scene.miningRock * 2246822519u));
                    std::uniform_real_distribution<double> u01(0.0, 1.0);
                    if (u01(lr) < 0.35) {
                        std::uniform_real_distribution<double> us(-1.0, 1.0);
                        double ddx = scene.px - rk.x, ddy = scene.py - rk.y, ddz = scene.pz - rk.z;
                        double dd = std::sqrt(ddx*ddx + ddy*ddy + ddz*ddz);
                        double dinv = (dd > 1e-6) ? 1.0 / dd : 0.0;
                        LocalFx f;
                        f.x = rk.x + ddx * dinv * rk.radius;
                        f.y = rk.y + ddy * dinv * rk.radius;
                        f.z = rk.z + ddz * dinv * rk.radius;
                        f.vx = ddx * dinv * 8.0 + us(lr) * 3.0;
                        f.vy = ddy * dinv * 8.0 + us(lr) * 3.0;
                        f.vz = ddz * dinv * 8.0 + us(lr) * 3.0;
                        f.kind = FX_SMOKE; f.size = 1.4 + u01(lr) * 1.2;
                        f.life = 0.5 + u01(lr) * 0.5; f.maxLife = f.life;
                        f.r = rk.r; f.g = rk.g; f.b = rk.b; f.a = 120;
                        scene.fx.push_back(f);
                    }
                }
                if (rk.ore <= 0.0) {
                    scene.miningRock = -1;
                    scene.toast = "ROCK DEPLETED";
                    scene.toastTimer = 2.0;
                }
            }
        }
        // Вне зоны (но руда есть) — просто пауза: miningRock сохраняем.
    }

    // ---- Стыковка: ищем ближайшее тело с рынком в зоне ----
    scene.dockPrompt = -1;
    {
        int best = -1; double bestD2 = 0.0;
        for (size_t i = 0; i < scene.bodies.size(); ++i) {
            LocalBody& b = scene.bodies[i];
            if (!b.hasMarket) continue;
            double dx = b.x - scene.px, dy = b.y - scene.py, dz = b.z - scene.pz;
            double d2 = dx*dx + dy*dy + dz*dz;
            double range = b.radius + LocalCfg::DOCK_RANGE;
            if (d2 <= range*range && (best < 0 || d2 < bestD2)) { best = (int)i; bestD2 = d2; }
        }
        scene.dockPrompt = best;
    }

    // ---- Подсказка добычи: ближайший астероид с рудой в зоне (для HUD/кнопки) ----
    scene.minePrompt = -1;
    {
        int best = -1; double bestD2 = 0.0;
        for (size_t i = 0; i < scene.rocks.size(); ++i) {
            const LocalRock& rk = scene.rocks[i];
            if (rk.ore <= 0.0) continue;
            double dx = rk.x - scene.px, dy = rk.y - scene.py, dz = rk.z - scene.pz;
            double d2 = dx*dx + dy*dy + dz*dz;
            double range = rk.radius + LocalCfg::MINE_RANGE;
            if (d2 <= range*range && (best < 0 || d2 < bestD2)) { best = (int)i; bestD2 = d2; }
        }
        scene.minePrompt = best;
    }

    // ---- (J) Лут: интеграция, автосбор (с уважением к трюму), отсев ----
    {
        size_t ec = elementCount();
        for (size_t i = 0; i < scene.loot.size(); ) {
            LocalLoot& lt = scene.loot[i];
            lt.x += lt.vx * dtHours; lt.y += lt.vy * dtHours; lt.z += lt.vz * dtHours;
            lt.spin += 2.0 * dtHours;
            lt.life -= dtHours;
            bool remove = (lt.life <= 0.0);
            if (!remove && playerValid && !scene.playerDestroyed && ec > 0) {
                double dx = lt.x - scene.px, dy = lt.y - scene.py, dz = lt.z - scene.pz;
                double d2 = dx*dx + dy*dy + dz*dz;
                if (d2 <= LocalCfg::LOOT_SCOOP_RANGE * LocalCfg::LOOT_SCOOP_RANGE) {
                    int el = lt.element; if (el < 0) el = 0; if (el >= (int)ec) el = (int)ec - 1;
                    const char* sym = elementDefinitions()[el].symbol;
                    double unit = std::max(0.001, resourceUnitMassByIndex(el));
                    Ship& ps = game.agents[game.playerAgent].ship;
                    if (shipCargoMass(ps) + lt.amount * unit <= ps.cargoCapacity) {
                        std::string s(sym);
                        bool found = false;
                        for (size_t k = 0; k < ps.cargo.size(); ++k) {
                            if (ps.cargo[k].element == s) { ps.cargo[k].amount += lt.amount; found = true; break; }
                        }
                        if (!found) ps.cargo.emplace_back(s, lt.amount);
                        game.addResearch(0.1);
                        scene.toast = "+" + std::to_string((int)lt.amount) + " " + s;
                        scene.toastTimer = 2.0;
                        remove = true;
                    }
                    // иначе трюм полон — оставляем контейнер дрейфовать
                }
            }
            if (remove) { scene.loot[i] = scene.loot.back(); scene.loot.pop_back(); }
            else ++i;
        }
    }

    // ---- (N) Радиоисточники (§7.2): проявление детектором + забор награды ----
    //  Детерминизм: reward/research/element запечены при генерации; единственная
    //  мутация game вне money/cargo — game.addResearch (допустимое исключение). scene.radio
    //  НЕ режем (draw пропускает resolved). FX уважают потолок FX_MAX, как и всюду в файле.
    for (LocalRadioSource& rs : scene.radio) {
        if (rs.resolved) continue;
        double dx = rs.x - scene.px, dy = rs.y - scene.py, dz = rs.z - scene.pz;
        double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        // Проявление: детектор достаточно силён И источник достаточно близко (порог растёт с tier).
        double revealRange = LocalCfg::RADIO_REVEAL_RANGE * (1.0 + 0.6 * scene.detectorTier);
        if (scene.detectorTier >= rs.tier && dist <= revealRange) rs.revealed = true; // раз true — навсегда
        // Забор: проявленный источник в зоне активации.
        if (rs.revealed && dist <= LocalCfg::RADIO_CLAIM_RANGE) {
            if (playerValid) game.agents[game.playerAgent].money += rs.reward;
            game.addResearch(rs.research);
            // Клад-элемент в трюм (та же логика уважения cargoCapacity, что у лута/добычи).
            bool cargoFull = false;
            std::string lootStr;
            if (rs.element >= 0 && rs.amount > 0.0 && playerValid) {
                size_t ec = elementCount();
                if (ec > 0) {
                    int el = rs.element; if (el < 0) el = 0; if (el >= (int)ec) el = (int)ec - 1;
                    const char* sym = elementDefinitions()[el].symbol;
                    double unit = std::max(0.001, resourceUnitMassByIndex(el));
                    Ship& ps = game.agents[game.playerAgent].ship;
                    if (shipCargoMass(ps) + rs.amount * unit <= ps.cargoCapacity) {
                        std::string s(sym);
                        bool found = false;
                        for (size_t k = 0; k < ps.cargo.size(); ++k) {
                            if (ps.cargo[k].element == s) { ps.cargo[k].amount += rs.amount; found = true; break; }
                        }
                        if (!found) ps.cargo.emplace_back(s, rs.amount);
                        lootStr = " +" + std::to_string((int)rs.amount) + " " + s;
                    } else {
                        cargoFull = true; // трюм полон: деньги/исследование выданы, элемент пропущен
                    }
                }
            }
            rs.resolved = true;
            scene.toast = rs.label + " CLAIMED +" + std::to_string((int)rs.reward) + " CR" + lootStr;
            if (cargoFull) scene.toast += " CARGO FULL";
            scene.toastTimer = 3.0;
            game.pushNews("Signal resolved: " + rs.label, 2);
            // FX-вспышка в точке источника: 1 кольцо + горсть искр, зеленовато-циановые (учёт FX_MAX).
            if ((int)scene.fx.size() < LocalCfg::FX_MAX) {
                LocalFx f;
                f.x = rs.x; f.y = rs.y; f.z = rs.z;
                f.vx = 0.0; f.vy = 0.0; f.vz = 0.0;
                f.kind = FX_RING; f.size = 14.0;
                f.life = 0.8; f.maxLife = 0.8;
                f.r = 80; f.g = 255; f.b = 200; f.a = 255;
                scene.fx.push_back(f);
            }
            spawnSparks(rs.x, rs.y, rs.z, 80, 255, 200, 6);
        }
    }

    // ---- (K) Цель HUD: залоченная (Tab) или ближайший корабль ----
    {
        int nearest = -1; double nbest = 0.0;
        for (size_t i = 0; i < scene.craft.size(); ++i) {
            LocalCraft& c = scene.craft[i];
            double dx = c.x - scene.px, dy = c.y - scene.py, dz = c.z - scene.pz;
            double d2 = dx*dx + dy*dy + dz*dz;
            if (nearest < 0 || d2 < nbest) { nearest = (int)i; nbest = d2; }
        }
        if (in.cycleTarget) {
            if (scene.craft.empty()) scene.lockTarget = -1;
            else if (scene.lockTarget < 0 || scene.lockTarget >= (int)scene.craft.size()) scene.lockTarget = 0;
            else scene.lockTarget = (scene.lockTarget + 1) % (int)scene.craft.size();
        }
        if (scene.lockTarget >= (int)scene.craft.size()) scene.lockTarget = -1; // защита от устаревания
        scene.targetCraft = (scene.lockTarget >= 0) ? scene.lockTarget : nearest;
    }

    // ---- (M) Интеграция и отсев частиц fx (рост кольца делает draw из life/maxLife) ----
    for (size_t i = 0; i < scene.fx.size(); ) {
        LocalFx& f = scene.fx[i];
        f.x += f.vx * dtHours; f.y += f.vy * dtHours; f.z += f.vz * dtHours;
        f.life -= dtHours;
        if (f.life <= 0.0) { scene.fx[i] = scene.fx.back(); scene.fx.pop_back(); }
        else ++i;
    }

    // ---- Таймер тоста ----
    scene.toastTimer = std::max(0.0, scene.toastTimer - dtReal);

    // ---- Действие стыковки ----
    if (in.dock && scene.dockPrompt >= 0) return scene.starIndex; // main откроет рынок
    return -1;
}
