// localgen.cpp — детерминированная генерация локальной сцены ("микромир").
// Реализует РОВНО одну функцию buildLocalScene(): звезда, планеты по кеплеровым
// орбитам, пояс астероидов, набор кораблей (в т.ч. co-located макро-агенты) и
// стартовая позиция игрока. Работает ТОЛЬКО над локальным RNG — глобальный rng
// не трогаем, иначе рассинхронизируется макро-симуляция. Один и тот же starIndex
// всегда даёт одну и ту же систему. Читаем game только на чтение.

#include "local.h"
#include "game.h"
#include <cmath>
#include <cstdint>
#include <random>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// (§5.13.15) Внешний вид астероида по элементу → базовая палитра (0..255) + зеркальность.
// Классы «пород» повторяют реальную таксономию астероидов, ключ — атомный номер/металличность
// элемента (elementDefinitions()): лёд/летучие (H,He,N,O) — яркая голубовато-белая, блестит;
// углеродистые (C,S) — тёмный уголь, матовый; металлические (Fe/Co/Ni + тяжёлые металлы/актиниды
// с metallicTrait>0.35) — стальной, яркий блик; силикаты (всё прочее: Mg/Al/Si/Ca/Ti…) — тёпло-
// бурый камень, матовый (≈ прежний вид пояса — безопасный дефолт). ЧИСТАЯ арифметика: без RNG,
// без global rng (§2.3); клэмп индекса — переносит мусор без падения.
// (§5.13.16) ЕДИНЫЙ классификатор породы по элементу — реальная таксономия M/C/S/лёд.
// Зовётся и rockAppearance (палитра/блеск, §5.13.15), и добычей (rockYieldMult в local.h),
// чтобы «как выглядит» и «сколько даёт» шли от одного правила. Чистая арифметика над
// elementDefinitions(): без RNG, без global rng (§2.3). Индекс = Z−1 (см. resource.h).
int rockClass(int element) {
    const std::vector<ElementDefinition>& defs = elementDefinitions();
    const int n = int(defs.size());
    if (n <= 0) return ROCK_SILICATE;
    if (element < 0) element = 0; else if (element > n - 1) element = n - 1;
    const int    z     = defs[element].atomicNumber;
    const double metal = defs[element].metallicTrait;
    if (z == 1 || z == 2 || z == 7 || z == 8) return ROCK_ICE;      // H/He/N/O — лёд/летучие
    if (z == 6 || z == 16)                    return ROCK_CARBON;   // C/S — углеродистый
    if ((z >= 26 && z <= 28) || metal > 0.35) return ROCK_METAL;    // Fe/Co/Ni + тяж. металлы
    return ROCK_SILICATE;                                           // S-тип (дефолт)
}

void rockAppearance(int element, double& baseR, double& baseG, double& baseB, double& spec) {
    const std::vector<ElementDefinition>& defs = elementDefinitions();
    const int n = int(defs.size());
    if (n <= 0) { baseR = 150.0; baseG = 140.0; baseB = 130.0; spec = 0.0; return; } // старый серо-бурый
    if (element < 0) element = 0; else if (element > n - 1) element = n - 1;
    switch (rockClass(element)) {                         // палитра/блеск строго по классу (§5.13.16)
    case ROCK_ICE:                                        // лёд/летучие — яркий, голубовато-белый
        baseR = 198.0; baseG = 214.0; baseB = 236.0; spec = 0.55; break;
    case ROCK_CARBON:                                     // углеродистый — тёмный уголь, матовый
        baseR = 56.0;  baseG = 52.0;  baseB = 49.0;  spec = 0.05; break;
    case ROCK_METAL: {                                    // металлический — стальной, яркий блик
        const double metal = defs[element].metallicTrait;
        baseR = 150.0 + metal * 28.0;                     // тяжёлые/благородные металлы чуть теплее
        baseG = 150.0;
        baseB = 158.0 - metal * 22.0;
        spec  = 0.62;
        break;
    }
    default:                                              // силикат (S-тип) — тёпло-бурый, матовый
        baseR = 150.0; baseG = 128.0; baseB = 102.0; spec = 0.08; break;
    }
}

void buildLocalScene(const Game& game, int starIndex, LocalScene& scene) {
    // --- Сброс сцены (векторы очищаются свежим конструированием) ---
    scene = LocalScene();
    scene.active = true;
    scene.starIndex = starIndex;

    // --- Локальный детерминированный RNG (см. контракт в local.h) ---
    std::mt19937 lrng(starIndex >= 0 ? (game.seed ^ (uint32_t(starIndex) * 2654435761u)) : (game.seed ^ 0xD1B54A35u));

    // Локальные помощники розыгрыша — работают только над lrng.
    auto frand = [&lrng](double a, double b) -> double {
        std::uniform_real_distribution<double> d(a, b);
        return d(lrng);
    };
    auto irand = [&lrng](int a, int b) -> int {
        if (b < a) b = a;
        std::uniform_int_distribution<int> d(a, b);
        return d(lrng);
    };
    auto col = [](double v) -> uint8_t {
        if (v < 0.0) v = 0.0;
        if (v > 255.0) v = 255.0;
        return uint8_t(v);
    };

    // Розыгрыш одного радиоисточника в позиции (x,y,z) и добавление в scene.radio.
    // Общий для CASE A (пустота) и CASE B (система); работает только над lrng.
    auto makeRadioSource = [&](double x, double y, double z) {
        LocalRadioSource rs;
        rs.x = x; rs.y = y; rs.z = z;
        rs.kind = irand(0, 3);                    // RS_DERELICT/RS_CACHE/RS_DISTRESS/RS_ANOMALY
        double t = frand(0.0, 1.0);               // тир: чем выше — тем реже и богаче
        rs.tier = (t < 0.6) ? 0 : ((t < 0.9) ? 1 : 2);
        rs.strength = frand(0.35, 1.0);
        const double rewardMul   = 1.0 + 0.6 * rs.tier;
        const double researchMul = 1.0 + rs.tier;
        const int eN = int(elementCount());
        if (rs.kind == RS_DERELICT) {
            rs.reward   = frand(350.0, 700.0);
            rs.research = frand(1.0, 4.0);
            rs.element  = (eN > 0) ? irand(0, eN - 1) : -1;
            rs.amount   = frand(8.0, 24.0);
            rs.label    = "DERELICT";
        } else if (rs.kind == RS_CACHE) {
            rs.reward   = frand(150.0, 400.0);
            rs.research = frand(1.0, 3.0);
            rs.element  = (eN > 0) ? irand(0, eN - 1) : -1;
            rs.amount   = frand(12.0, 30.0);
            rs.label    = "SUPPLY CACHE";
        } else if (rs.kind == RS_DISTRESS) {
            rs.reward   = frand(500.0, 1000.0);
            rs.research = frand(2.0, 5.0);
            rs.element  = -1;
            rs.amount   = 0.0;
            rs.label    = "DISTRESS CALL";
        } else { // RS_ANOMALY
            rs.reward   = frand(100.0, 250.0);
            rs.research = frand(8.0, 20.0);
            rs.element  = -1;
            rs.amount   = 0.0;
            rs.label    = "ANOMALY";
        }
        rs.reward   *= rewardMul;                 // тир-множители — после базовых значений
        rs.research *= researchMul;
        if (rs.element > eN - 1) rs.element = eN - 1; // клэмп индекса элемента
        rs.revealed = false;
        rs.resolved = false;
        scene.radio.push_back(rs);
    };

    // --- Скорость/тяга игрока: из корабля игрока (с локальным полом), иначе LocalCfg ---
    if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
        double sp = game.agents[game.playerAgent].ship.speed;
        scene.playerMaxSpeed = std::max(60.0, std::min(140.0, sp * 300.0));
        scene.playerAccel = scene.playerMaxSpeed * 0.35;
    } else {
        scene.playerMaxSpeed = LocalCfg::PLAYER_MAXSPEED;
        scene.playerAccel = LocalCfg::PLAYER_ACCEL;
    }

    // --- Щиты игрока: база + от брони корабля игрока ---
    if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
        scene.pMaxShield = LocalCfg::PLAYER_SHIELD_BASE + game.agents[game.playerAgent].ship.armor * 1.5;
    } else {
        scene.pMaxShield = LocalCfg::PLAYER_SHIELD_BASE;
    }
    scene.pShield = scene.pMaxShield;
    scene.pShieldTimer = 0.0;

    // --- Уровень детектора радиосигналов из сенсоров игрока (хромокор) ---
    double sensors = (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size()))
                         ? game.tech.sensors : 1.0;
    scene.detectorTier = (sensors >= 1.20) ? 2 : (sensors >= 1.08 ? 1 : 0);

    const bool validStar = (starIndex >= 0 && starIndex < int(game.cluster.stars.size()));

    // --- Косметическая туманность: часть сцен получает слабый тон (draw уже рисует). ---
    // Порог выше в пустоте, чем в системе. Иначе strength=0 (нет туманности).
    {
        double nebThreshold = validStar ? 0.45 : 0.70;
        if (frand(0.0, 1.0) < nebThreshold) {
            scene.nebulaStrength = frand(0.25, 0.70);
            switch (irand(0, 3)) {
                case 0:  scene.nebulaR = 40;  scene.nebulaG = 90; scene.nebulaB = 120; break; // teal
                case 1:  scene.nebulaR = 110; scene.nebulaG = 50; scene.nebulaB = 120; break; // magenta
                case 2:  scene.nebulaR = 120; scene.nebulaG = 80; scene.nebulaB = 40;  break; // amber
                default: scene.nebulaR = 60;  scene.nebulaG = 60; scene.nebulaB = 130; break; // indigo
            }
        }
    }

    // ==================== CASE A: глубокий космос / пустота ====================
    if (!validStar) {
        scene.hasStar = false;
        scene.starRadius = 0.0;

        // 0..2 бродячих торговца в пределах ±400 LU от начала координат.
        int wanderers = int(lrng() % 3u);
        for (int i = 0; i < wanderers; ++i) {
            LocalCraft c;
            c.faction = -1;
            c.agentIndex = -1;
            c.hostile = false;
            c.maxSpeed = 50.0;
            c.accel = c.maxSpeed * 0.3;
            c.hullHP = 40.0; c.maxHullHP = 40.0;
            c.heavy = 6.0; c.light = 4.0; c.armor = 4.0;
            c.kind = CK_TRADER;
            c.aiState = 0; c.aiTarget = -1; c.boost = 0.0; c.hitFlash = 0.0; c.thrustGlow = 0.0;
            c.maxShield = c.armor * 1.5 + c.maxHullHP * 0.15; c.shield = c.maxShield; c.shieldRegenTimer = 0.0;
            c.label = "TRADER";
            c.r = 90; c.g = 200; c.b = 235;
            c.x = frand(-400.0, 400.0);
            c.y = frand(-400.0, 400.0);
            c.z = frand(-300.0, 300.0);   // реальный 3D-разброс по высоте (пустота — не плоскость)
            c.tx = 0.0; c.ty = 0.0; c.tz = 0.0;
            scene.craft.push_back(c);
        }

        // Радиоисточники в пустоте (POI/детекторы): 4..7 вокруг начала координат,
        // на дистанциях 400..2400 в разных направлениях (полный 3D-разброс по высоте).
        int deepRadio = irand(4, 7);
        for (int i = 0; i < deepRadio; ++i) {
            double bearing = frand(0.0, 2.0 * M_PI);
            double elev    = frand(-0.55, 0.55);      // наклон над/под плоскостью — источники реально off-plane
            double dist    = frand(400.0, 2400.0);
            double ce = std::cos(elev);
            makeRadioSource(dist * ce * std::cos(bearing), dist * ce * std::sin(bearing), dist * std::sin(elev));
        }

        // Игрок в начале координат, нос вдоль +X, скорость 0 (дефолты структуры).
        scene.px = 0.0; scene.py = 0.0; scene.pz = 0.0;
        localSetForward(scene, 1.0, 0.0, 0.0);   // нос вдоль +X (тело-относительный базис)
        return;
    }

    // ==================== CASE B: реальная звёздная система ====================
    const ClusterStar& star = game.cluster.stars[starIndex];
    scene.hasStar = true;
    // ГИГАНТСКАЯ звезда доминирует во ВСЁМ играбельном объёме: экранный размер зависит
    // ТОЛЬКО от D/R (дистанция/радиус), поэтому орбиты ниже заданы как ДОЛИ R и держат D/R
    // малым везде. R/R_планеты ~ 7..60 => по объёму тысячи..десятки тысяч планет влезают.
    // Рисуем аналитически (см. localdraw §3) — большой радиус не стоит ничего по памяти.
    scene.starRadius = 260.0 + star.metallicity * 100.0;   // 260..360 LU (гигант)

    // Цвет звезды: пульсар — бело-голубой, обычная — тёплый.
    if (star.stellarClass == 1) {
        scene.starR = 190; scene.starG = 210; scene.starB = 255;
    } else {
        scene.starR = 255; scene.starG = 232; scene.starB = col(150.0 + star.metallicity * 60.0);
    }

    // --- Планеты (0..10) ---
    int planetCount = int(lrng() % 11u);
    double currentGap = 1.05 + frand(0.0, 0.2); // Начальный отступ от звезды
    
    for (int i = 0; i < planetCount; ++i) {
        LocalBody body;
        
        currentGap += frand(0.25, 0.65); // Процедурный шаг орбиты
        double orbitRadius = scene.starRadius * currentGap;

        body.orbitRadius = orbitRadius;
        body.orbitPhase  = frand(0.0, 2.0 * M_PI);
        body.orbitSpeed  = 40.0 / std::pow(orbitRadius, 1.5);   // кеплерово ~ r^-1.5
        body.inclination = frand(-0.40, 0.40);   // заметный 3D-наклон орбит

        // Процедурный тип по удалению от звезды
        double heat = 1.0 - (double)i / std::max(1, planetCount - 1); // 1.0 = внутри, 0.0 = снаружи
        double rType = frand(0.0, 1.0);
        
        if (i == 0) {
            // Первая почти всегда каменная (чтобы рынок был на твердой земле)
            body.kind = (rType < 0.85) ? LB_ROCKY : LB_GASGIANT;
        } else if (heat > 0.6) {
            body.kind = (rType < 0.7) ? LB_ROCKY : LB_GASGIANT;
        } else if (heat > 0.3) {
            body.kind = (rType < 0.5) ? LB_GASGIANT : ((rType < 0.75) ? LB_ROCKY : LB_ICE);
        } else {
            body.kind = (rType < 0.7) ? LB_ICE : LB_GASGIANT;
        }

        // Контекст для генерации (металличность 0..1)
        double mNorm = (star.metallicity - 0.22) / 0.95;
        if (mNorm < 0.0) mNorm = 0.0; else if (mNorm > 1.0) mNorm = 1.0;

        if (body.kind == LB_ROCKY) {
            body.radius = frand(6.0, 14.0);
            // Цвет каменных планет зависит от близости к звезде (выгорание/ржавчина) и металличности (темнота)
            double rr = frand(80.0, 240.0) * (0.6 + 0.4 * heat);
            double gg = frand(70.0, 220.0) * (0.8 + 0.2 * heat) * (1.0 - 0.3 * mNorm);
            double bb = frand(60.0, 200.0) * (1.0 - 0.4 * heat) * (1.0 - 0.4 * mNorm);
            body.r = col(rr); body.g = col(gg); body.b = col(bb);
        } else if (body.kind == LB_GASGIANT) {
            body.radius = frand(18.0, 42.0);
            // Газовые гиганты: любой цвет спектра.
            double hue = frand(0.0, M_PI * 2.0);
            double sat = frand(0.4, 1.0);
            double val = frand(150.0, 255.0);
            double rr = val * (1.0 + sat * std::cos(hue));
            double gg = val * (1.0 + sat * std::cos(hue - 2.09439));
            double bb = val * (1.0 + sat * std::cos(hue + 2.09439));
            double maxC = std::max(rr, std::max(gg, bb));
            if (maxC > 255.0) { rr = rr * 255.0 / maxC; gg = gg * 255.0 / maxC; bb = bb * 255.0 / maxC; }
            body.r = col(rr); body.g = col(gg); body.b = col(bb);
            // Кольца (~55% газовых гигантов)
            if (frand(0.0, 1.0) < 0.55) {
                body.ringInner = body.radius * frand(1.4, 1.7);
                body.ringOuter = body.ringInner * frand(1.4, 1.9);
            }
        } else { // LB_ICE
            body.radius = frand(9.0, 18.0);
            // Лёд: от классического голубого/белого до экзотического (розовый аммиак, зеленый метан)
            double rr = frand(120.0, 240.0);
            double gg = frand(160.0, 255.0);
            double bb = frand(180.0, 255.0);
            if (frand(0.0, 1.0) < 0.35) {
                // Экзотический химический лёд
                if (frand(0.0, 1.0) < 0.5) std::swap(rr, bb); else std::swap(gg, bb);
            }
            body.r = col(rr); body.g = col(gg); body.b = col(bb);
        }

        body.name = star.name + "-" + std::to_string(i + 1);
        body.hasMarket = false;

        // Стартовая позиция из фазы+наклона (sim пересчитает её каждый кадр).
        body.x = orbitRadius * std::cos(body.orbitPhase);
        body.y = orbitRadius * std::sin(body.orbitPhase) * std::cos(body.inclination);
        body.z = orbitRadius * std::sin(body.orbitPhase) * std::sin(body.inclination);

        scene.bodies.push_back(body);
    }

    double outermostOrbit = planetCount > 0 ? scene.bodies[planetCount - 1].orbitRadius : scene.starRadius * 1.5;

    // Крупнейшая каменная планета — стыковочный хаб (рынок).
    int bestRocky = -1; double bestR = -1.0;
    for (int i = 0; i < planetCount; ++i) {
        if (scene.bodies[i].kind == LB_ROCKY && scene.bodies[i].radius > bestR) {
            bestR = scene.bodies[i].radius;
            bestRocky = i;
        }
    }
    if (bestRocky >= 0) scene.bodies[bestRocky].hasMarket = true;

    // --- Пояс астероидов ---
    double beltR = scene.starRadius * frand(1.3, 2.0);
    if (planetCount >= 2) {
        int gap = irand(0, planetCount - 2);
        beltR = 0.5 * (scene.bodies[gap].orbitRadius + scene.bodies[gap + 1].orbitRadius);
    }

    const std::vector<ElementDefinition>& defs = elementDefinitions();
    int elemN = int(elementCount());

    // (§5.13.20) --- Композиционный профиль пояса (уровень СИСТЕМЫ, без lrng) ---
    // Пояса перестают быть «состав-слепыми»: класс камней тянется к составу системы —
    // металличность → металлический пояс, бедность металлом → льдистый, экономический
    // профиль (resourceFocus) → свой класс. Пулы элементов по классам и веса классов
    // считаем ОДИН раз здесь (чистая арифметика над star/defs, ноль lrng); в цикле лишь
    // выбираем класс/элемент по ДЕТЕРМИНИРОВАННОМУ хэшу i — розыгрыши lrng ниже не трогаем,
    // поэтому станция/трафик/радио спавнятся ПОБИТОВО как прежде (строго аддитивно, §2.3).
    std::vector<int> classPool[4];   // индексы элементов по rockClass (enum RockClass 0..3)
    for (int j = 0; j < elemN; ++j) { int c = rockClass(j); if (c >= 0 && c < 4) classPool[c].push_back(j); }
    std::vector<int> focusPool[4];   // подмножество resourceFocus, разложенное по классам
    for (size_t f = 0; f < star.resourceFocus.size(); ++f) {
        int fe = star.resourceFocus[f];
        if (fe >= 0 && fe < elemN) { int c = rockClass(fe); if (c >= 0 && c < 4) focusPool[c].push_back(fe); }
    }
    double mNorm = (star.metallicity - 0.22) / 0.95;   // диапазон генерации [0.22,1.17] → [0,1]
    if (mNorm < 0.0) mNorm = 0.0; else if (mNorm > 1.0) mNorm = 1.0;
    double w[4];
    w[ROCK_SILICATE] = 1.6;                            // S-тип — самый обычный (≈ прежний тан-дефолт)
    w[ROCK_METAL]    = 0.5 + 2.6 * mNorm;              // металличность → металлический пояс
    w[ROCK_ICE]      = 0.5 + 1.8 * (1.0 - mNorm);      // бедные металлом → льдистые
    w[ROCK_CARBON]   = 0.8;                            // углеродистые — реже
    for (int c = 0; c < 4; ++c) w[c] += 0.9 * double(focusPool[c].size());  // экономический профиль
    for (int c = 0; c < 4; ++c) if (classPool[c].empty()) w[c] = 0.0;       // не выбирать пустой класс
    const double wsum = w[0] + w[1] + w[2] + w[3];

    int rockCount = 45 + int(lrng() % 45u);
    for (int i = 0; i < rockCount; ++i) {
        LocalRock rock;
        double angle = frand(0.0, 2.0 * M_PI);
        double rr = beltR + frand(-45.0, 45.0);
        rock.x = rr * std::cos(angle);
        rock.y = rr * std::sin(angle);
        rock.z = frand(-25.0, 25.0);   // вертикальная толщина пояса; sim сохраняет z -> реальный 3D-пояс
        rock.orbitR = rr;
        rock.orbitAng = angle;
        rock.orbitVel = (40.0 / std::pow(std::max(1.0, rr), 1.5)) * 0.18; // медленный дрейф пояса (доля кеплера), один знак на весь пояс
        rock.spin = frand(0.0, 6.2831853);
        rock.spinVel = frand(-0.6, 0.6);
        rock.radius = frand(1.5, 4.5);   // астероиды — пылинки в этом масштабе
        rock.ore = frand(20.0, 120.0);

        // Элемент — базовый выбор (СТРИМ-ЯКОРЬ §5.13.20): эти irand-розыгрыши сохранены
        // 1-в-1, чтобы поток lrng — а значит станция/трафик/радио ниже — остался ПОБИТОВО
        // прежним. Итог ниже переопределяется композиционным взвешиванием (хэш i, без lrng).
        int elem = 25; // fallback
        if (!star.resourceFocus.empty()) {
            elem = star.resourceFocus[irand(0, int(star.resourceFocus.size()) - 1)];
        } else if (elemN > 0) {
            int start = irand(0, elemN - 1);
            for (int s = 0; s < elemN; ++s) {
                int j = (start + s) % elemN;
                if (defs[j].metallicTrait > 0.6) { elem = j; break; }
            }
        }
        if (elemN > 0) {
            if (elem < 0) elem = 0;
            else if (elem > elemN - 1) elem = elemN - 1;
        } else {
            elem = 0;
        }

        // (§5.13.20) КОМПОЗИЦИОННО-ВЗВЕШЕННЫЙ выбор: тянем класс камня к составу системы
        // (веса w[] и пулы посчитаны до цикла). Класс — по CDF от ДЕТЕРМИНИРОВАННОГО хэша i;
        // элемент — из focusPool своего класса (что система добывает), иначе из общего пула
        // класса. Ноль lrng ⇒ строго аддитивно к спавну (§2.3). Хэш-константы иные, чем у
        // §5.13.15 (цвет), чтобы класс и яркостный джиттер были декоррелированы.
        if (wsum > 0.0) {
            double uc = std::sin(double(i) * 78.233 + 12.9898) * 43758.5453; uc -= std::floor(uc);
            double pick = uc * wsum, acc = 0.0;
            int cls = ROCK_SILICATE;
            for (int cc = 0; cc < 4; ++cc) { acc += w[cc]; if (pick < acc) { cls = cc; break; } }
            const std::vector<int>& pool = !focusPool[cls].empty() ? focusPool[cls] : classPool[cls];
            if (!pool.empty()) {
                double ue = std::sin(double(i) * 39.425 + 7.311) * 43758.5453; ue -= std::floor(ue);
                int idx = int(ue * double(pool.size()));
                if (idx < 0) idx = 0; else if (idx >= int(pool.size())) idx = int(pool.size()) - 1;
                elem = pool[idx];
            }
        }
        rock.element = elem;

        // (§5.13.22) ЗАПАС РУДЫ = базовый frand (выше) × класс × насыщенность системы. Множитель
        // применяется к УЖЕ разыгранному rock.ore ⇒ ноль новых draw lrng: поток остаётся стрим-
        // якорем §5.13.20, поэтому станция/трафик/радио/write-back спавнятся ПОБИТОВО как прежде
        // (§2.3), а rocks/shiny в soak неизменны. star.miningRichness прежде НЕ влияла на пояс —
        // теперь богатые системы дают более тучные металлические глыбы (среднее ≈1.0 ⇒ не инфляция).
        rock.ore *= oreRichnessMult(rockClass(elem), star.miningRichness);

        // (§5.13.15) Палитра/зеркальность по классу породы элемента; лёгкая вариация яркости
        // камень-к-камню — из ДЕТЕРМИНИРОВАННОГО хэша индекса, а НЕ из lrng: не сдвигаем поток
        // генерации, поэтому станция/радио/корабли спавнятся ровно как прежде (строго аддитивно,
        // §2.3) — меняется только цвет/блеск камня. Без порогов: гладкий множитель ≈[0.86,1.10].
        double br, bg, bb, sp;
        rockAppearance(elem, br, bg, bb, sp);
        double h = std::sin(double(i) * 12.9898 + 3.14159) * 43758.5453;
        h -= std::floor(h);                          // [0,1)
        const double jit = 0.86 + 0.24 * h;          // ≈[0.86, 1.10)
        rock.r = col(br * jit);
        rock.g = col(bg * jit);
        rock.b = col(bb * jit);
        rock.spec = sp;

        scene.rocks.push_back(rock);
    }

    // --- Станция (ещё одно тело на орбите), если система населена ---
    if (star.population > 0.0) {
        LocalBody st;
        st.kind = LB_STATION;
        st.hasMarket = true;
        st.radius = 9.0;
        st.orbitRadius = planetCount > 0 ? scene.bodies[0].orbitRadius + 40.0 : scene.starRadius * 1.2;
        st.orbitPhase  = frand(0.0, 2.0 * M_PI);
        st.orbitSpeed  = 40.0 / std::pow(st.orbitRadius, 1.5);
        st.inclination = frand(-0.05, 0.05);
        st.r = 200; st.g = 205; st.b = 215;                    // светло-серый
        st.name = star.name + " STATION";
        st.x = st.orbitRadius * std::cos(st.orbitPhase);
        st.y = st.orbitRadius * std::sin(st.orbitPhase) * std::cos(st.inclination);
        st.z = st.orbitRadius * std::sin(st.orbitPhase) * std::sin(st.inclination);
        scene.bodies.push_back(st);
    }

    // --- Co-located макро-агенты (зеркалим до 8 кораблей) ---
    int coCount = 0;
    for (size_t i = 0; i < game.agents.size() && coCount < 8; ++i) {
        if (int(i) == game.playerAgent) continue;
        const Agent& ag = game.agents[i];
        if (ag.currentStar != starIndex) continue;
        if (ag.ship.enRoute) continue;

        LocalCraft c;
        c.faction = ag.ship.ownerFaction;
        c.agentIndex = int(i);
        c.maxSpeed = std::max(35.0, std::min(90.0, ag.ship.speed * 260.0));
        c.accel = c.maxSpeed * 0.3;
        c.hullHP = ag.ship.hullHP;
        c.maxHullHP = std::max(1.0, ag.ship.maxHullHP);
        c.heavy = ag.ship.heavyWeapons;   // СЫРЫЕ поля (chromocore уже запечён)
        c.light = ag.ship.lightWeapons;
        c.armor = ag.ship.armor;
        c.hostile = (ag.type == "pirate"); // co-located макро-агенты в основном нейтральны
        if (ag.type == "pirate") c.kind = CK_PIRATE;
        else if (ag.type == "military" || ag.type == "patrol") c.kind = CK_PATROL;
        else if (ag.type == "trader") c.kind = CK_TRADER;
        else c.kind = CK_CIVILIAN;
        c.aiState = 0; c.aiTarget = -1; c.boost = 0.0; c.hitFlash = 0.0; c.thrustGlow = 0.0;
        c.maxShield = c.armor * 1.5 + c.maxHullHP * 0.15; c.shield = c.maxShield; c.shieldRegenTimer = 0.0;

        double radius = frand(scene.starRadius * 1.3, outermostOrbit + 100.0);
        double angle = frand(0.0, 2.0 * M_PI);
        c.x = radius * std::cos(angle);
        c.y = radius * std::sin(angle);
        c.z = frand(-20.0, 20.0);
        // Цель движения: случайная планета или начало координат.
        if (!scene.bodies.empty() && irand(0, 1) == 0) {
            const LocalBody& tb = scene.bodies[irand(0, int(scene.bodies.size()) - 1)];
            c.tx = tb.x; c.ty = tb.y; c.tz = tb.z;
        }

        c.label = ag.type; // шрифт сам поднимает регистр
        if (c.faction >= 0 && c.faction < int(game.factions.size())) {
            c.r = col(double(game.factions[c.faction].colorR));
            c.g = col(double(game.factions[c.faction].colorG));
            c.b = col(double(game.factions[c.faction].colorB));
        } else {
            c.r = 160; c.g = 160; c.b = 170;
        }
        scene.craft.push_back(c);
        ++coCount;
    }

    // --- Процедурные торговцы (1..3) ---
    int traderCount = 1 + int(lrng() % 3u);
    for (int t = 0; t < traderCount; ++t) {
        LocalCraft c;
        c.faction = -1;
        c.agentIndex = -1;
        c.hostile = false;
        c.maxSpeed = 50.0;
        c.accel = c.maxSpeed * 0.3;
        c.hullHP = 40.0; c.maxHullHP = 40.0;
        c.heavy = 6.0; c.light = 4.0; c.armor = 4.0;
        c.kind = CK_TRADER;
        c.aiState = 0; c.aiTarget = -1; c.boost = 0.0; c.hitFlash = 0.0; c.thrustGlow = 0.0;
        c.maxShield = c.armor * 1.5 + c.maxHullHP * 0.15; c.shield = c.maxShield; c.shieldRegenTimer = 0.0;
        c.label = "TRADER";
        c.r = 90; c.g = 200; c.b = 235;                        // голубоватый
        double radius = frand(scene.starRadius * 1.3, outermostOrbit + 80.0);
        double angle = frand(0.0, 2.0 * M_PI);
        c.x = radius * std::cos(angle);
        c.y = radius * std::sin(angle);
        c.z = frand(-20.0, 20.0);
        if (!scene.bodies.empty()) {
            const LocalBody& tb = scene.bodies[irand(0, int(scene.bodies.size()) - 1)];
            c.tx = tb.x; c.ty = tb.y; c.tz = tb.z;
        }
        scene.craft.push_back(c);
    }

    // --- Процедурные пираты (0..2) ---
    int pirateCount = int(lrng() % 3u);
    for (int p = 0; p < pirateCount; ++p) {
        LocalCraft c;
        c.faction = -1;
        c.agentIndex = -1;
        c.hostile = true;
        c.maxSpeed = 70.0;
        c.accel = c.maxSpeed * 0.3;
        c.hullHP = 50.0; c.maxHullHP = 50.0;
        c.heavy = 10.0; c.light = 6.0; c.armor = 5.0;
        c.kind = CK_PIRATE;
        c.aiState = 0; c.aiTarget = -1; c.boost = 0.0; c.hitFlash = 0.0; c.thrustGlow = 0.0;
        c.maxShield = c.armor * 1.5 + c.maxHullHP * 0.15; c.shield = c.maxShield; c.shieldRegenTimer = 0.0;
        c.label = "PIRATE";
        c.r = 230; c.g = 90; c.b = 80;                         // красный
        double radius = frand(scene.starRadius * 1.3, outermostOrbit + 120.0);
        double angle = frand(0.0, 2.0 * M_PI);
        c.x = radius * std::cos(angle);
        c.y = radius * std::sin(angle);
        c.z = frand(-20.0, 20.0);
        c.tx = 0.0; c.ty = 0.0; c.tz = 0.0;
        // (§5.13.24) Метка «в розыске» + сумма — ДЕТЕРМИНИРОВАННО из хэша (starIndex,p): ноль
        //   lrng ⇒ поток спавна остаётся побитово прежним (§2.3), rocks/shiny/станция/трафик/радио
        //   не меняются. ~40% пиратов в розыске; сумма 250..900 CR по второму (декоррелированному)
        //   хэшу. Только НАГРАДА за убийство — урон/ИИ пирата не трогаем (combat-инвариант, «не
        //   ослаблять пиратов»). Читается в блоке убийства (localsim) и HUD (§5.13.25).
        double hw = std::sin(double(starIndex * 131 + p) * 51.017 + 4.271) * 43758.5453;
        hw -= std::floor(hw);                                  // [0,1)
        if (hw > 0.60) {                                       // ~40% пиратов «в розыске»
            c.wanted = true;
            double hb = std::sin(double(starIndex * 197 + p) * 26.651 + 9.113) * 43758.5453;
            hb -= std::floor(hb);                              // [0,1)
            c.wantedBounty = 250.0 + std::floor(hb * 27.0) * 25.0;  // 250..900, шаг 25
        }
        scene.craft.push_back(c);
    }

    // --- Старт игрока: D/R≈2.8 от звезды (снаружи внутренних планет), нос к звезде ---
    scene.px = scene.starRadius * 2.8;   // звезда ~82% высоты экрана + видны крошечные планеты
    scene.py = 0.0;
    scene.pz = 0.0;
    localSetForward(scene, -1.0, 0.0, 0.0);   // нос назад к звезде в начале координат (-X)

    // ---- Радиоисточники системы (POI/детекторы): 2..4 вокруг звезды. ----
    // Отдельный вектор scene.radio — на индексы тел не влияет.
    {
        double innermostOrbit = scene.bodies[0].orbitRadius;
        int sysRadio = irand(2, 4);
        for (int i = 0; i < sysRadio; ++i) {
            double bearing = frand(0.0, 2.0 * M_PI);
            double elev    = frand(-0.35, 0.35);      // умеренный наклон — система остаётся диском, но 3D
            double dist    = frand(innermostOrbit, outermostOrbit * 1.1);
            double ce = std::cos(elev);
            makeRadioSource(dist * ce * std::cos(bearing), dist * ce * std::sin(bearing), dist * std::sin(elev));
        }
    }

    // ---- Луны: добавляем В САМОМ КОНЦЕ, чтобы НЕ сдвинуть индексы планет/станции.
    // parent = индекс планеты в 0..planetCount-1 (всегда < индекса самой луны, так
    // что sim обновит родителя раньше). Вешаем на 1..2 крупнейшие планеты. ----
    if (planetCount >= 1) {
        int hostCount = (planetCount >= 2) ? 2 : 1;
        int hosts[2] = { 0, -1 };
        for (int i = 1; i < planetCount; ++i) {
            if (scene.bodies[i].radius > scene.bodies[hosts[0]].radius) hosts[0] = i;
        }
        if (hostCount == 2) {
            for (int i = 0; i < planetCount; ++i) {
                if (i == hosts[0]) continue;
                if (hosts[1] < 0 || scene.bodies[i].radius > scene.bodies[hosts[1]].radius) hosts[1] = i;
            }
        }
        for (int h = 0; h < hostCount; ++h) {
            int p = hosts[h];
            if (p < 0) continue;
            int moonN = irand(1, 2);
            for (int moonNo = 0; moonNo < moonN; ++moonNo) {
                LocalBody m;
                m.kind = LB_MOON;
                m.parent = p;
                m.radius = frand(3.0, 7.0);
                m.orbitRadius = scene.bodies[p].radius + frand(14.0, 34.0) + (moonNo * frand(10.0, 20.0));
                m.orbitPhase  = frand(0.0, 2.0 * M_PI);
                m.orbitSpeed  = frand(0.6, 1.4);   // луны обращаются заметно быстрее планет (рад/час)
                m.inclination = frand(-0.25, 0.25);
                uint8_t grey = uint8_t(irand(150, 200));
                m.r = grey; m.g = grey; m.b = grey;
                m.name = scene.bodies[p].name + " MOON " + char('A' + moonNo);
                // Позиция при t=0: позиция родителя + орбитальное смещение (sim пересчитает).
                double ox = m.orbitRadius * std::cos(m.orbitPhase);
                double oy = m.orbitRadius * std::sin(m.orbitPhase) * std::cos(m.inclination);
                double oz = m.orbitRadius * std::sin(m.orbitPhase) * std::sin(m.inclination);
                m.x = scene.bodies[p].x + ox;
                m.y = scene.bodies[p].y + oy;
                m.z = scene.bodies[p].z + oz;
                scene.bodies.push_back(m);
            }
        }
    }

    // ---- (§5.13.9) Начальные РЕЙСЫ: не-боевые NPC стартуют с курсом на рынок (а не на случайное
    //      тело) — со старта у станции/хаба виден трафик. Пираты не участвуют (они охотники).
    //      Стартовый таймер прилётов разгоняем, чтобы первый гость появился не мгновенно.
    {
        std::vector<int> markets;
        for (size_t b = 0; b < scene.bodies.size(); ++b)
            if (scene.bodies[b].hasMarket) markets.push_back((int)b);
        for (size_t i = 0; i < scene.craft.size(); ++i) {
            LocalCraft& c = scene.craft[i];
            if (c.kind == CK_PIRATE) continue;
            c.errand = 0;
            if (!markets.empty()) {
                int bi = markets[(size_t)irand(0, (int)markets.size() - 1)];
                c.errandBody = bi;
                const LocalBody& tb = scene.bodies[bi];
                c.tx = tb.x; c.ty = tb.y; c.tz = tb.z;
            } else {
                c.errandBody = -1; // рынков нет — курс останется на ранее выбранное тело/центр
            }
        }
        scene.trafficTimer = frand(LocalCfg::TRAFFIC_MIN_H, LocalCfg::TRAFFIC_MAX_H);
    }
}
