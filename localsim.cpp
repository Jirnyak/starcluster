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

    // emitWarp: сигнатура «прыжка» — циан-кольцо + яркая аддитивная вспышка-ядро + горсть искр.
    //   Ставится на ПРИЛЁТЕ (вход торговца с края) и на ОТЛЁТЕ (деспаун за краем). §5.13.10.
    //   Голубой тон намеренно отличается от тёплых взрывов, чтобы читаться как «варп», а не гибель.
    //   Локальный сид (fx.size/localHours/позиция) — global rng не трогаем (§2.3). Уважаем FX_MAX.
    auto emitWarp = [&scene](double x, double y, double z) {
        std::mt19937 lr((uint32_t)(scene.fx.size() * 2654435761u)
                        ^ (uint32_t)(uint64_t)(scene.localHours * 977.0)
                        ^ (uint32_t)(uint64_t)(std::fabs(x) * 8.0 + std::fabs(y) * 2.0 + std::fabs(z)));
        std::uniform_real_distribution<double> us(-1.0, 1.0);
        std::uniform_real_distribution<double> u01(0.0, 1.0);
        if ((int)scene.fx.size() < LocalCfg::FX_MAX) { // кольцо-ударная (растёт в draw)
            LocalFx f; f.x = x; f.y = y; f.z = z; f.vx = 0.0; f.vy = 0.0; f.vz = 0.0;
            f.kind = FX_RING; f.size = 26.0; f.life = 0.9; f.maxLife = 0.9;
            f.r = 90; f.g = 210; f.b = 255; f.a = 255;
            scene.fx.push_back(f);
        }
        if ((int)scene.fx.size() < LocalCfg::FX_MAX) { // ядро-вспышка (аддитивная)
            LocalFx f; f.x = x; f.y = y; f.z = z; f.vx = 0.0; f.vy = 0.0; f.vz = 0.0;
            f.kind = FX_MUZZLE; f.size = 2.6; f.life = 0.5; f.maxLife = 0.5;
            f.r = 150; f.g = 230; f.b = 255; f.a = 255;
            scene.fx.push_back(f);
        }
        for (int k = 0; k < 8; ++k) { // горсть циан-искр
            if ((int)scene.fx.size() >= LocalCfg::FX_MAX) break;
            LocalFx f; f.x = x; f.y = y; f.z = z;
            double sp = 20.0 + u01(lr) * 40.0;
            f.vx = us(lr) * sp; f.vy = us(lr) * sp; f.vz = us(lr) * sp * 0.6;
            f.kind = FX_SPARK;
            f.size = 1.0 + u01(lr) * 1.2;
            f.life = 0.4 + u01(lr) * 0.4; f.maxLife = f.life;
            f.r = 120; f.g = 220; f.b = 255; f.a = 255;
            scene.fx.push_back(f);
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
        // Мышь-взгляд складывается с клавиатурой. mouseYaw/Pitch — уже готовый угол за кадр
        // (чувствительность применена в main.cpp), поэтому НЕ множим на turn. Знак приводим к
        // конвенции yawA (+ = нос влево): нос-вправо (+mouseYaw) => вычитаем.
        const double yawA   = (in.yawL   ? turn : 0.0) - (in.yawR   ? turn : 0.0) - in.mouseYaw;
        const double pitchA = (in.pitchU ? turn : 0.0) - (in.pitchD ? turn : 0.0) + in.mousePitch;
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
            // Смещаем ниже линии прицела (как порт орудия), а не в саму камеру по оси взгляда:
            // иначе вспышка рождается в «глазу» и раздувается на пол-экрана.
            f.x = scene.px + dirx * 5.0 - scene.pupX * 1.6;
            f.y = scene.py + diry * 5.0 - scene.pupY * 1.6;
            f.z = scene.pz + dirz * 5.0 - scene.pupZ * 1.6;
            f.vx = scene.pvx; f.vy = scene.pvy; f.vz = scene.pvz;
            f.kind = FX_MUZZLE; f.size = 0.8;
            f.life = 0.05; f.maxLife = 0.05;
            f.r = 150; f.g = 220; f.b = 255; f.a = 210;
            scene.fx.push_back(f);
        }
    }

    // ---- Радиус «края системы» (§5.13.9): внешняя граница тел + запас. Используется рейсами
    //      (точка отлёта) и деспауном (кто вышел за край). Пол ~600 LU для разреженных систем.
    double edgeR = 600.0;
    for (size_t b = 0; b < scene.bodies.size(); ++b) {
        const LocalBody& bd = scene.bodies[b];
        double br = std::sqrt(bd.x*bd.x + bd.y*bd.y + bd.z*bd.z) + bd.radius + 150.0;
        if (br > edgeR) edgeR = br;
    }
    // Равновероятный выбор тела-назначения рейса: приоритет рыночным телам, иначе любое.
    // Reservoir-sampling — без временных векторов, детерминирован при данном RNG.
    auto pickMarketBody = [&scene](std::mt19937& r) -> int {
        int marketPick = -1, anyPick = -1; uint32_t mCount = 0, aCount = 0;
        for (size_t b = 0; b < scene.bodies.size(); ++b) {
            ++aCount; if ((r() % aCount) == 0u) anyPick = (int)b;
            if (scene.bodies[b].hasMarket) { ++mCount; if ((r() % mCount) == 0u) marketPick = (int)b; }
        }
        return (marketPick >= 0) ? marketPick : anyPick;
    };

    // (§5.13.11) Пре-проход сброса разметки бедствия. Жертва может иметь индекс БОЛЬШЕ пирата,
    //   поэтому чистим ОТДЕЛЬНЫМ проходом до ИИ — иначе очистка в начале обработки жертвы затёрла бы
    //   флаг, только что выставленный пиратом с меньшим индексом.
    for (size_t i = 0; i < scene.craft.size(); ++i) {
        scene.craft[i].underAttack = false;
        scene.craft[i].threatConvoy = false;
        scene.craft[i].threatVictimFaction = -1;
        scene.craft[i].defending = false;   // (§5.13.12) сбрасываем маркер эскорта; выставит проход ИИ
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
            // (§5.13.12) Эскорт-патруль. Сперва ищем БЛИЖАЙШЕГО НАЛЁТЧИКА — пирата в РАСШИРЕННОМ радиусе
            //   бедствия PATROL_DISTRESS_R, который сам находится вплотную (< RAID_NEAR) к торговцу/
            //   гражданскому. Это даёт реакцию «издалека» и приоритет над случайным пиратом. Детект идёт
            //   по СЫРЫМ позициям (не по кадровым SOS-флагам §5.13.11) — значит порядок обработки кораблей
            //   не важен и разметка бедствия здесь не нужна (нет зависимости индекс-жертвы-от-индекс-пирата).
            int raider = -1;
            double raiderBest = LocalCfg::PATROL_DISTRESS_R * LocalCfg::PATROL_DISTRESS_R;
            const double RAID2 = LocalCfg::RAID_NEAR * LocalCfg::RAID_NEAR;
            for (size_t j = 0; j < scene.craft.size(); ++j) {
                if (j == i) continue;
                LocalCraft& o = scene.craft[j];
                if (o.hullHP <= 0.0 || o.kind != CK_PIRATE) continue;
                double dx = o.x - c.x, dy = o.y - c.y, dz = o.z - c.z;
                double d2 = dx*dx + dy*dy + dz*dz;
                if (d2 >= raiderBest) continue;         // уже есть более близкий налётчик
                bool raiding = false;                    // пират у беззащитного борта (конвой)?
                for (size_t k = 0; k < scene.craft.size(); ++k) {
                    if (k == j) continue;
                    const LocalCraft& v = scene.craft[k];
                    if (v.hullHP <= 0.0) continue;
                    if (v.kind != CK_TRADER && v.kind != CK_CIVILIAN) continue;
                    double vx = v.x - o.x, vy = v.y - o.y, vz = v.z - o.z;
                    if (vx*vx + vy*vy + vz*vz < RAID2) { raiding = true; break; }
                }
                if (raiding) { raiderBest = d2; raider = (int)j; }
            }
            if (raider >= 0) {
                atkCraft = raider; atkBest = raiderBest; atkPlayer = false; c.defending = true;
            } else {
                // Фолбэк — прежняя логика: ближайший пират в радиусе осведомлённости AW2.
                for (size_t j = 0; j < scene.craft.size(); ++j) {
                    if (j == i) continue;
                    LocalCraft& o = scene.craft[j];
                    if (o.hullHP <= 0.0 || o.kind != CK_PIRATE) continue;
                    double dx = o.x - c.x, dy = o.y - c.y, dz = o.z - c.z;
                    double d2 = dx*dx + dy*dy + dz*dz;
                    if (d2 < atkBest) { atkBest = d2; atkCraft = (int)j; atkPlayer = false; }
                }
            }
            // Спровоцированный патруль всё ещё может предпочесть БОЛЕЕ БЛИЗКОГО игрока (тогда не «эскорт»).
            if (c.hostile && playerTargetable && pd2 < atkBest) {
                atkBest = pd2; atkPlayer = true; atkCraft = -1; c.defending = false;
            }
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

        // (§5.13.11) Разметка бедствия: пират в состоянии атаки на КОНКРЕТНЫЙ не-пиратский борт
        //   (atkCraft>=0, не игрок). Флаги очищены в пре-проходе выше, ставим здесь — на решения ИИ
        //   не влияют, поэтому порядок обработки безопасен. Индекс жертвы стабилен (удаление отложено
        //   за субшаги), c.threatConvoy читается на убийстве игроком (см. блок L2 — «CONVOY SAVED»).
        if (c.kind == CK_PIRATE && c.aiState == 1 && atkCraft >= 0) {
            LocalCraft& victim = scene.craft[atkCraft];
            victim.underAttack = true;
            c.threatConvoy = true;
            c.threatVictimFaction = victim.faction;
        }
        // (§5.13.12) «Эскорт» — только когда патруль ФАКТИЧЕСКИ идёт в атаку на налётчика (не бежит/патрулирует).
        if (c.kind == CK_PATROL && c.aiState != 1) c.defending = false;

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
        } else if (c.kind == CK_PIRATE) {
            // (0-пират) Обычное блуждание — прежняя логика на retargetTimer (охотник, не курьер).
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
        } else if (c.errand == 1) {
            // (0-стоянка) «У причала»: держимся рядом с (орбитирующим) телом, разнос по индексу,
            // гасим ход. По истечении таймера — выбираем новый рейс (круиз к рынку / отлёт).
            if (c.errandBody >= 0 && c.errandBody < (int)scene.bodies.size()) {
                const LocalBody& tb = scene.bodies[c.errandBody];
                double pa = (double)i * 1.3, off = tb.radius + 12.0;
                c.tx = tb.x + std::cos(pa) * off;
                c.ty = tb.y + std::sin(pa) * off;
                c.tz = tb.z + ((i & 1u) ? 4.0 : -4.0);
            }
            c.errandTimer -= dtHours;
            c.thrustGlow = 0.25;
            if (c.errandTimer <= 0.0) {
                std::mt19937 wr((uint32_t)i * 2654435761u
                                ^ (uint32_t)(uint64_t)(scene.localHours * 7.0 + 11.0));
                std::uniform_real_distribution<double> u01(0.0, 1.0);
                bool canLeave = (c.agentIndex < 0); // только чисто локальные покидают систему
                if (canLeave && u01(wr) < LocalCfg::ERRAND_DEPART_PROB) {
                    double a = u01(wr) * 6.2831853;
                    c.tx = std::cos(a) * (edgeR + 120.0);
                    c.ty = std::sin(a) * (edgeR + 120.0);
                    c.tz = (u01(wr) - 0.5) * edgeR * 0.3;
                    c.errand = 2; c.errandBody = -1; c.boost = 1.5; // короткий форсаж «на выход»
                    for (int k = 0; k < 4; ++k) { // синеватый пуфф форсажа у причала — «отчаливаю» (§5.13.10)
                        if ((int)scene.fx.size() >= LocalCfg::FX_MAX) break;
                        LocalFx f; f.x = c.x; f.y = c.y; f.z = c.z;
                        f.vx = (u01(wr)-0.5)*10.0; f.vy = (u01(wr)-0.5)*10.0; f.vz = (u01(wr)-0.5)*6.0;
                        f.kind = FX_SMOKE; f.size = 2.0 + u01(wr)*2.0;
                        f.life = 0.8 + u01(wr)*0.6; f.maxLife = f.life;
                        f.r = 120; f.g = 160; f.b = 200; f.a = 150;
                        scene.fx.push_back(f);
                    }
                } else {
                    int bi = pickMarketBody(wr);
                    c.errand = 0; c.errandBody = bi;
                    if (bi >= 0) { const LocalBody& tb = scene.bodies[bi]; c.tx = tb.x; c.ty = tb.y; c.tz = tb.z; }
                }
            }
        } else {
            // (0-круиз/отлёт) Летим к цели. Круиз (errand 0) следит за орбитой тела-назначения;
            // по прибытии в радиус стыковки — «швартуемся» (переход в стоянку). Отлёт (errand 2) —
            // к фикс. точке за краем; деспаун делает цикл удаления (L).
            if (c.errand == 0 && c.errandBody >= 0 && c.errandBody < (int)scene.bodies.size()) {
                const LocalBody& tb = scene.bodies[c.errandBody];
                c.tx = tb.x; c.ty = tb.y; c.tz = tb.z;
            }
            if (c.errand == 0) {
                double dx = c.tx - c.x, dy = c.ty - c.y, dz = c.tz - c.z;
                double d2 = dx*dx + dy*dy + dz*dz;
                double reach = (c.errandBody >= 0 && c.errandBody < (int)scene.bodies.size())
                             ? scene.bodies[c.errandBody].radius + LocalCfg::DOCK_RANGE : 30.0;
                if (d2 < reach*reach) {
                    std::mt19937 wr((uint32_t)i * 2654435761u
                                    ^ (uint32_t)(uint64_t)(scene.localHours * 13.0 + 3.0));
                    std::uniform_real_distribution<double> ul(LocalCfg::DOCK_LINGER_MIN_H, LocalCfg::DOCK_LINGER_MAX_H);
                    c.errand = 1; c.errandTimer = ul(wr);
                    c.vx *= 0.15; c.vy *= 0.15; c.vz *= 0.15; // сброс хода на «швартовку»
                }
            }
            c.thrustGlow = (c.errand == 2) ? 0.8 : 0.6;
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

    // (§5.13.11) Событие рейда: считаем жертв «под атакой» и на фронте рейда (после устойчивого затишья,
    //   с дебаунсом ниже) бросаем toast «CONVOY RAID» — нудж к действию (парен с «CONVOY SAVED» на выплате).
    //   Текст НАРОЧНО отличается от POI-радиоисточника RS_DISTRESS (label «DISTRESS CALL») — это РАЗНЫЕ
    //   сущности. Красные маяки (localdraw) несут детализацию по каждой жертве, поэтому toast не стомпит
    //   активное событие (ставим только на чистый слот).
    {
        int distressNow = 0;
        for (size_t i = 0; i < scene.craft.size(); ++i)
            if (scene.craft[i].hullHP > 0.0 && scene.craft[i].underAttack) ++distressNow;
        // Дебаунс: пират может дёргаться attack↔flee у порога низкого HP, роняя счётчик до 0 на кадр-другой.
        //   Это ТОТ ЖЕ рейд — не переобъявляем. Держим cooldown, пока есть жертвы, и даём ему сгореть только
        //   после устойчивого затишья; toast бросаем лишь на «холодном» слоте (edge из спокойствия в рейд).
        if (distressNow > 0) {
            if (scene.distressCooldown <= 0.0 && scene.toastTimer <= 0.0) {
                scene.toast = "CONVOY RAID";
                scene.toastTimer = 3.5;
            }
            scene.distressCooldown = 6.0;
        } else {
            scene.distressCooldown = std::max(0.0, scene.distressCooldown - dtReal);
        }
        scene.distressPrev = distressNow;
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
                            // (§5.13.11) Спасение конвоя: сбитый борт — пират, атаковавший НЕ-пиратскую
                            //   жертву в этом кадре (threatConvoy выставлен проходом ИИ). Бонус ПОВЕРХ
                            //   награды: доп. кредиты + исследование + перебитый toast «CONVOY SAVED» +
                            //   положительный rep-бамп фракции жертвы. Строго ADDITIVE — пиратов НЕ трогали.
                            if (c.kind == CK_PIRATE && c.threatConvoy) {
                                double convoyBonus = 200.0;
                                game.agents[game.playerAgent].money += convoyBonus;
                                game.addResearch(2.0);
                                scene.toast = "CONVOY SAVED +" + std::to_string((int)(bounty + convoyBonus)) + " CR";
                                scene.toastTimer = 3.0;
                                scene.shake = std::min(40.0, std::max(scene.shake, 18.0));
                                if (c.threatVictimFaction >= 0 && game.playerFaction >= 0) {
                                    game.adjustFactionRelation(game.playerFaction, c.threatVictimFaction, 6);
                                    game.pushNews("Convoy saved: pirate destroyed", 2);
                                }
                            }
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
                            // (§5.13.14) Write-back в макро: сбитый борт-зеркало «умирает» и в
                            //   постоянном мире — корабль макро-агента деградирует в спас-капсулу, груз
                            //   сброшен (тот же помощник downgradeAgentToEscapePod, что и в robAgent).
                            //   Кредиты не трогаем; репрессию фракции уже применили выше — тут ТОЛЬКО
                            //   даунгрейд. Игрока-агента (agentIndex==playerAgent) в зеркала не берём,
                            //   но проверяем явно для страховки.
                            if (c.agentIndex >= 0 && c.agentIndex < (int)game.agents.size()
                                && c.agentIndex != game.playerAgent) {
                                downgradeAgentToEscapePod(game.agents[c.agentIndex]);
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

    // ---- (L) Удаляем уничтоженные И покинувшие систему корабли ОДИН раз после субшагов ----
    //      Деспаун отлёта (errand==2) — только для чисто локальных (agentIndex<0), вышедших за
    //      край: зеркала макро-агентов не «исчезают» (это рассинхрон с макро). §5.13.9.
    double despawnR2 = (edgeR + 60.0) * (edgeR + 60.0);
    for (size_t i = 0; i < scene.craft.size(); ) {
        LocalCraft& c = scene.craft[i];
        bool dead = (c.hullHP <= 0.0);
        bool departed = false;
        if (!dead && c.errand == 2 && c.agentIndex < 0) {
            double d2 = c.x*c.x + c.y*c.y + c.z*c.z;
            if (d2 > despawnR2) departed = true;
        }
        if (dead || departed) {
            if (departed) emitWarp(c.x, c.y, c.z); // вспышка «ушёл в прыжок» (гибель уже даёт wreck)
            scene.craft[i] = scene.craft.back();
            scene.craft.pop_back();
        } else {
            ++i;
        }
    }

    // ---- (L2) ПРИЛЁТЫ (§5.13.9): поддерживаем живой трафик — новый торговец входит с края и
    //      идёт к рынку. Только реальные системы (звезда + тела). Чисто локальный (agentIndex=-1),
    //      неагрессивный; global rng не трогаем (§2.3). Потолок TRAFFIC_CAP + пейсинг держат
    //      scene.craft ограниченным (инвариант soak). Таймер сбрасывается даже при достигнутом
    //      потолке, чтобы не дёргать RNG каждый кадр.
    if (scene.starIndex >= 0 && !scene.bodies.empty()) {
        scene.trafficTimer -= dtHours;
        if (scene.trafficTimer <= 0.0) {
            if ((int)scene.craft.size() < LocalCfg::TRAFFIC_CAP) {
                std::mt19937 sr((uint32_t)scene.craft.size() * 2654435761u
                                ^ (uint32_t)(uint64_t)(scene.localHours * 100.0 + 17.0)
                                ^ (uint32_t)(scene.bodies.size() * 40503u));
                std::uniform_real_distribution<double> u01(0.0, 1.0);
                LocalCraft c;
                c.faction = -1; c.agentIndex = -1; c.hostile = false;
                c.maxSpeed = 50.0; c.accel = c.maxSpeed * 0.3;
                c.hullHP = 40.0; c.maxHullHP = 40.0;
                c.heavy = 6.0; c.light = 4.0; c.armor = 4.0;
                c.kind = (u01(sr) < 0.5) ? CK_TRADER : CK_CIVILIAN;
                c.maxShield = c.armor * 1.5 + c.maxHullHP * 0.15; c.shield = c.maxShield;
                if (c.kind == CK_TRADER) { c.label = "TRADER";   c.r = 90;  c.g = 200; c.b = 235; }
                else                     { c.label = "CIVILIAN"; c.r = 150; c.g = 190; c.b = 160; }
                double a = u01(sr) * 6.2831853;
                c.x = std::cos(a) * edgeR;
                c.y = std::sin(a) * edgeR;
                c.z = (u01(sr) - 0.5) * edgeR * 0.2;
                int bi = pickMarketBody(sr);
                c.errand = 0; c.errandBody = bi;
                if (bi >= 0) { const LocalBody& tb = scene.bodies[bi]; c.tx = tb.x; c.ty = tb.y; c.tz = tb.z; }
                else         { c.tx = 0.0; c.ty = 0.0; c.tz = 0.0; }
                // Начальная скорость внутрь — чтобы сразу «летел», а не разгонялся из нуля у края.
                double idx = -c.x, idy = -c.y, idz = -c.z;
                double il = 1.0 / std::max(1e-6, std::sqrt(idx*idx + idy*idy + idz*idz));
                c.vx = idx*il*c.maxSpeed*0.6; c.vy = idy*il*c.maxSpeed*0.6; c.vz = idz*il*c.maxSpeed*0.6;
                scene.craft.push_back(c);
                emitWarp(c.x, c.y, c.z); // вспышка «вышел из прыжка» у края (§5.13.10)
            }
            std::mt19937 tr((uint32_t)(scene.craft.size() * 40503u)
                            ^ (uint32_t)(uint64_t)(scene.localHours * 50.0 + 7.0));
            std::uniform_real_distribution<double> ut(LocalCfg::TRAFFIC_MIN_H, LocalCfg::TRAFFIC_MAX_H);
            scene.trafficTimer = ut(tr);
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
