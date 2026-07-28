#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "camera.h"   // View3D / CameraBasis / проекция (нужны buildLocalCamera ниже)

// ============================================================================
//  Локальный режим полёта ("микромир").
//  Поверх макро-кластера: игрок ныряет в одну систему (или в пустой космос) и
//  летает в реальном масштабе — инерционный полёт как в Elite. Звезда в центре,
//  планеты по кеплеровым орбитам, пояс астероидов, другие корабли, живой бой,
//  добыча руды на месте, стыковка с планетой = рынок.
//
//  Единицы: LU (local units). Время: локальные ЧАСЫ. Базовый темп задаётся в
//  main.cpp (1 реальная сек = HOURS_PER_SEC локальных часов); удержание "warp"
//  умножает поток времени на WARP_MULT ("автопилот-ускорение").
//
//  Правило CHROMOCORE (см. chromo.cpp): материалы/кинематика/тактика уже ЗАПЕЧЕНЫ
//  в поля корабля (cargoCapacity, maxHullHP, speed, acceleration, heavyWeapons,
//  lightWeapons, armor). Локальный код читает СЫРЫЕ поля и НЕ домножает их снова.
//
//  Детерминизм: локальный код НИКОГДА не трогает глобальный rng (это
//  рассинхронизировало бы макро-симуляцию). Для локальной случайности заводится
//  свой std::mt19937, засеянный от индекса звезды. Единственное исключение —
//  game.addResearch() может пересечь порог хромокора (это осознанно: исследование,
//  добытое в локальном режиме, — часть прогрессии; макро при этом заморожено).
// ============================================================================

class Game;
struct SDL_Renderer;   // View3D/CameraBasis приходят из camera.h (см. include выше)

// ---- Тюнинг (общий для gen/sim/draw; main.cpp тоже читает HOURS/ WARP) ----
namespace LocalCfg {
    constexpr double HOURS_PER_SEC    = 1.0;    // базовый темп: 1 реальная сек = 1 локальный час
    constexpr double WARP_MULT        = 60.0;   // множитель времени при удержании warp (Shift)
    constexpr double PLAYER_ACCEL     = 30.0;   // тяга игрока, LU/час^2
    constexpr double PLAYER_MAXSPEED  = 95.0;   // макс. скорость игрока, LU/час
    constexpr double TURN_RATE        = 2.4;    // поворот носа, рад/РЕАЛЬНУЮ секунду (не зависит от warp)
    constexpr double DOCK_RANGE       = 26.0;   // дистанция стыковки с телом (сверх его радиуса), LU
    constexpr double MINE_RANGE       = 20.0;   // дистанция добычи астероида (сверх его радиуса), LU
    constexpr double PROJ_SPEED       = 520.0;  // скорость снаряда, LU/час
    constexpr double PROJ_LIFE_HOURS  = 1.2;    // время жизни снаряда, часы
    constexpr double WEAPON_RANGE     = 150.0;  // дальность стрельбы, LU
    constexpr double FIRE_COOLDOWN_H  = 0.10;   // перезарядка орудия, часы

    // --- Щиты (локальные, только в микромире; поглощают урон до корпуса) ---
    constexpr double SHIELD_REGEN_DELAY_H = 3.0;   // часы без урона до начала регена щита
    constexpr double SHIELD_REGEN_RATE    = 7.0;   // ед. щита в час
    constexpr double PLAYER_SHIELD_BASE   = 22.0;  // базовый щит игрока (+ от брони корабля)

    // --- Лут: контейнеры с грузом с уничтоженных кораблей ---
    constexpr double LOOT_SCOOP_RANGE = 24.0;   // дистанция подбора контейнера (сверх радиуса), LU
    constexpr double LOOT_LIFE_HOURS  = 300.0;  // время жизни контейнера, часы

    // --- Форсаж NPC (уклонение/отрыв при низком корпусе) ---
    constexpr double NPC_BOOST_MULT   = 1.8;

    // --- Эскорт-патрули: реакция на бедствие конвоя (§5.13.12). Патруль «слышит» рейд издалека и
    // приоритетно летит на перехват налётчика; чисто аддитивно — пиратов НЕ ослабляет, огонь всё
    // так же гейтится WEAPON_RANGE (патруль долетает и бьётся в упор, а не снайпит с 1400 LU). ---
    constexpr double PATROL_DISTRESS_R = 1400.0; // радиус, с которого патруль реагирует на рейд, LU
    constexpr double RAID_NEAR         = 260.0;  // пират считается «налётчиком», если ближе к не-пирату, LU

    // --- Живой трафик: рейсы NPC к рынкам + прилёты/отлёты (§5.13.9) ---
    // Не-боевые NPC (торговцы/гражданские/патруль вне боя) выполняют РЕЙСЫ: круиз к рынку →
    // стоянка «у причала» → новый рейс; изредка чисто-локальные покидают систему и деспаунятся.
    // Прилёты подсаживают свежих торговцев с края, пока число кораблей ниже потолка. Пейсинг +
    // потолок держат scene.craft ограниченным (инвариант soak). Локальный seeded RNG (§2.3).
    constexpr int    TRAFFIC_CAP        = 10;   // потолок кораблей, поддерживаемый прилётами
    constexpr double TRAFFIC_MIN_H      = 8.0;  // мин. интервал между попытками прилёта, лок. часов
    constexpr double TRAFFIC_MAX_H      = 22.0; // макс. интервал между попытками прилёта
    constexpr double DOCK_LINGER_MIN_H  = 3.0;  // мин. «стоянка у причала» после прибытия, часов
    constexpr double DOCK_LINGER_MAX_H  = 9.0;  // макс. стоянка
    constexpr double ERRAND_DEPART_PROB = 0.30; // вероятность «покинуть систему» при выборе рейса

    // --- Радиосигналы / детекторы (глубокий космос и населённые системы, §7.2) ---
    constexpr double RADIO_REVEAL_RANGE = 300.0; // источник "проявляется" (маркер+пеленг) при достатке детектора
    constexpr double RADIO_CLAIM_RANGE  = 36.0;  // дистанция активации/забора награды, LU

    // --- Добыча по классу породы (§5.13.16): темп извлечения (масса/час) зависит от состава
    // (класс из §5.13.15). Металл — плотная богатая руда (ищи блестящие глыбы!), лёд — легко
    // возгоняемые летучие, силикат — базовая порода (=1.0, прежний темп ⇒ нулевой регресс для
    // дефолт-класса), углерод — рыхлый низкосортный. Только СКОРОСТЬ добычи; combat/AI/ген не тронуты.
    constexpr double MINE_RATE_BASE      = 9.0;   // базовая скорость (была inline-константой в localsim)
    constexpr double MINE_YIELD_SILICATE = 1.00;  // силикат = база (прежнее поведение)
    constexpr double MINE_YIELD_ICE      = 1.25;  // лёд/летучие
    constexpr double MINE_YIELD_CARBON   = 0.65;  // углеродистый (медленно)
    constexpr double MINE_YIELD_METAL    = 1.60;  // металлический (богато/быстро)

    // (§5.13.22) Множитель КОЛИЧЕСТВА руды в камне по классу — ОТДЕЛЬНАЯ ось от скорости выше
    // (MINE_YIELD_* = темп; здесь = запас тоннажа). Металл — плотная богатая глыба, лёд/углерод
    // беднее рудой. Средневзвешенно ≈1.0 ⇒ общий тоннаж пояса не раздувается, а ПЕРЕРАСПРЕДЕЛЯется
    // к металлу в богатых системах. Силикат=1.0 ⇒ дефолт-класс без регресса.
    constexpr double ORE_QTY_SILICATE = 1.00;
    constexpr double ORE_QTY_ICE      = 0.90;
    constexpr double ORE_QTY_CARBON   = 0.80;
    constexpr double ORE_QTY_METAL    = 1.50;

    // --- FX / juice ---
    constexpr int    FX_MAX = 3600;             // потолок частиц (безопасность/пейсинг)
}

// ---- Классы пород астероидов (§5.13.15/§5.13.16) ----
// Плоский числовой класс по составу элемента (реальная таксономия M/C/S/лёд). rockClass()
// (определён в localgen.cpp) — ЕДИНЫЙ источник этой классификации: его зовут и rockAppearance
// (палитра/блеск, §5.13.15), и добыча (множитель выхода, §5.13.16) — чтобы «как выглядит» и
// «сколько даёт» шли от одного правила. Чистые функции, без RNG (§2.3).
enum RockClass { ROCK_SILICATE = 0, ROCK_ICE = 1, ROCK_CARBON = 2, ROCK_METAL = 3 };

// Множитель скорости добычи по классу (относительно LocalCfg::MINE_RATE_BASE).
inline double rockYieldMult(int rc) {
    switch (rc) {
    case ROCK_ICE:    return LocalCfg::MINE_YIELD_ICE;
    case ROCK_CARBON: return LocalCfg::MINE_YIELD_CARBON;
    case ROCK_METAL:  return LocalCfg::MINE_YIELD_METAL;
    default:          return LocalCfg::MINE_YIELD_SILICATE;   // ROCK_SILICATE / неизвестное
    }
}

// (§5.13.22) Множитель КОЛИЧЕСТВА руды в камне: класс породы × насыщенность системы.
// star.miningRichness ∈ [0.45,1.95] (cluster.cpp) прежде влияла ТОЛЬКО на макро-добычу
// (mining.cpp) — локальный пояс её игнорировал; здесь она наконец правит запас руды пояса.
// Обе компоненты нормированы к среднему ≈1.0 ⇒ чистое ПЕРЕРАСПРЕДЕЛЕНИЕ тоннажа, не инфляция.
// Чистая функция без RNG (§2.3); в localgen применяется как множитель УЖЕ разыгранного frand,
// поэтому поток lrng не сдвигается (стрим-якорь §5.13.20) — спавн/soak побитово прежние.
inline double oreRichnessMult(int rc, double miningRichness) {
    double qty;
    switch (rc) {
    case ROCK_ICE:    qty = LocalCfg::ORE_QTY_ICE;    break;
    case ROCK_CARBON: qty = LocalCfg::ORE_QTY_CARBON; break;
    case ROCK_METAL:  qty = LocalCfg::ORE_QTY_METAL;  break;
    default:          qty = LocalCfg::ORE_QTY_SILICATE; break;   // ROCK_SILICATE / неизвестное
    }
    double rNorm = (miningRichness - 0.45) / 1.5;      // [0.45,1.95] → [0,1]
    if (rNorm < 0.0) rNorm = 0.0; else if (rNorm > 1.0) rNorm = 1.0;
    const double fRich = 0.6 + 0.8 * rNorm;            // [0.6,1.4], среднее ≈1.0
    return qty * fRich;
}

// Короткое имя класса для HUD (UPPERCASE, §2.6).
inline const char* rockClassName(int rc) {
    switch (rc) {
    case ROCK_ICE:    return "ICE";
    case ROCK_CARBON: return "CARBON";
    case ROCK_METAL:  return "METAL";
    default:          return "SILICATE";
    }
}

// Спец-значение aiTarget: цель NPC — игрок (а не индекс в scene.craft).
constexpr int LOCAL_TARGET_PLAYER = -1000;

enum LocalBodyKind { LB_ROCKY = 0, LB_GASGIANT = 1, LB_ICE = 2, LB_STATION = 3, LB_MOON = 4 };

// Роль корабля для локального ИИ (кто кого атакует).
enum LocalCraftKind { CK_TRADER = 0, CK_PIRATE = 1, CK_PATROL = 2, CK_CIVILIAN = 3 };

// Вид частицы/эффекта.
enum LocalFxKind {
    FX_SPARK  = 0,  // короткая искра (попадание), быстро гаснет
    FX_DEBRIS = 1,  // обломок (взрыв корабля), летит и гаснет
    FX_TRAIL  = 2,  // точка выхлопа двигателя, оседает
    FX_RING   = 3,  // расширяющееся кольцо (ударная волна взрыва), size растёт
    FX_MUZZLE = 4,  // вспышка у дула при выстреле
    FX_SMOKE  = 5   // тусклая дымка/пыль (добыча, тлеющий обломок)
};

// Тип радиоисточника (что найдёт игрок, долетев).
enum LocalRadioKind { RS_DERELICT = 0, RS_CACHE = 1, RS_DISTRESS = 2, RS_ANOMALY = 3 };

// Планета/станция/луна на круговой орбите.
struct LocalBody {
    double orbitRadius = 0.0;   // радиус орбиты, LU (для луны — вокруг родителя)
    double orbitPhase = 0.0;    // фаза при localHours==0, радианы
    double orbitSpeed = 0.0;    // угловая скорость, рад/час (кеплерово: ~ r^-1.5)
    double inclination = 0.0;   // наклон плоскости орбиты, радианы
    double x = 0.0, y = 0.0, z = 0.0; // текущая позиция (пересчитывается в sim из орбиты)
    double radius = 3.0;        // визуальный/коллизионный радиус, LU (мал рядом с гигантской звездой)
    int kind = LB_ROCKY;
    int parent = -1;            // индекс родительского тела для LB_MOON (орбита вокруг него), иначе -1
    double ringInner = 0.0;     // кольца газового гиганта: внутренний радиус (0 = нет колец)
    double ringOuter = 0.0;     // внешний радиус колец, LU
    uint8_t r = 180, g = 180, b = 190;
    bool hasMarket = false;     // стыковка открывает рынок якорной звезды
    std::string name;
};

// Процедурный астероид. В поясе МЕДЛЕННО обращается вокруг звезды (orbit*) и
// собственно вращается (spin*); sim пересчитывает x,y из orbitR/orbitAng.
struct LocalRock {
    double x = 0.0, y = 0.0, z = 0.0;
    double radius = 2.0;
    double ore = 0.0;           // остаток руды (единицы массы элемента)
    int element = 0;            // индекс элемента (resource.h)
    double orbitR = 0.0;        // радиус в поясе (в плоскости XY)
    double orbitAng = 0.0;      // текущий угол в поясе, радианы
    double orbitVel = 0.0;      // угловая скорость дрейфа пояса, рад/час (очень медленно)
    double spin = 0.0;          // собственный угол поворота (для рисования гранёной формы)
    double spinVel = 0.0;       // скорость вращения, рад/час
    uint8_t r = 150, g = 140, b = 130;
    double spec = 0.0;          // зеркальность материала 0..1 (§5.13.15): лёд/металл блестят, углерод/силикат матовы
};

// Другой корабль в системе. Если agentIndex>=0 — зеркалит макро-агента.
struct LocalCraft {
    double x = 0.0, y = 0.0, z = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    double tx = 0.0, ty = 0.0, tz = 0.0; // текущая цель движения
    double maxSpeed = 60.0, accel = 20.0;
    double hullHP = 40.0, maxHullHP = 40.0;
    double shield = 0.0, maxShield = 0.0; // локальный щит (поглощает урон до корпуса)
    double shieldRegenTimer = 0.0;        // часы до начала регена щита
    double heavy = 8.0, light = 4.0, armor = 4.0; // СЫРЫЕ поля корабля (тактика уже запечена)
    int faction = -1;
    int agentIndex = -1;        // связанный макро-агент, или -1 (чисто локальный NPC)
    int kind = CK_CIVILIAN;     // роль для ИИ (trader/pirate/patrol/civilian)
    bool hostile = false;       // атакует ли игрока (пираты — да; патруль/торговец — нет, пока не спровоцирован)
    int aiState = 0;            // 0 travel/patrol, 1 attack, 2 flee
    int aiTarget = -1;          // индекс цели в scene.craft; LOCAL_TARGET_PLAYER = игрок; -1 = нет
    double fireCooldown = 0.0;  // часы до следующего выстрела
    double retargetTimer = 0.0; // часы до смены путевой точки / пересчёта ИИ
    double boost = 0.0;         // остаток форсажа при отрыве, часы
    double hitFlash = 0.0;      // реальные сек вспышки при получении урона (для рендера)
    double thrustGlow = 0.0;    // 0..1 активность тяги (для трейла)
    // --- Рейс/трафик (§5.13.9): жизненный цикл не-боевого NPC. Пираты его не используют. ---
    int    errand = 0;          // фаза рейса: 0 круиз к цели, 1 стоянка «у причала», 2 отлёт к краю (потом деспаун)
    double errandTimer = 0.0;   // часы до конца стоянки (фаза 1)
    int    errandBody = -1;     // тело-назначение (рынок/планета); цель следит за его орбитой. -1 = фикс. точка/край
    // --- Бедствие/защита конвоя (§5.13.11): чисто ADDITIVE-разметка, пиратов НЕ ослабляет. ---
    bool   underAttack = false;   // жертва: этот НЕ-пират сейчас цель атакующего пирата (маяк SOS)
    bool   threatConvoy = false;  // пират: сейчас атакует не-пирата (при убийстве игроком → «CONVOY SAVED»)
    int    threatVictimFaction = -1; // фракция жертвы на момент угрозы (для положительного rep-бампа)
    bool   defending = false;     // (§5.13.12) патруль: сейчас летит на перехват налётчика (маркер эскорта)
    // --- Розыск/награда (§5.13.24): маркер «в розыске» на пирате + сумма бонуса. Ставится
    //   ДЕТЕРМИНИРОВАННО при генерации (хэш индекса ⇒ ноль lrng, поток спавна побитово прежний,
    //   §2.3). Читается ТОЛЬКО в блоке убийства (доп. кредиты) и в HUD-рендере (§5.13.25).
    //   Строго ADDITIVE: НЕ влияет на ИИ/цель/урон пирата — combat-инвариант соблюдён. ---
    bool   wanted = false;        // пират «в розыске» — платит wantedBounty сверх награды при убийстве игроком
    double wantedBounty = 0.0;    // сумма награды за розыск (кредиты), детерминированная 250..900
    uint8_t r = 200, g = 200, b = 210;
    std::string label;
};

// Снаряд (общий для игрока и NPC).
struct LocalProjectile {
    double x = 0.0, y = 0.0, z = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    double life = 0.0;          // часы до исчезновения
    double damage = 0.0;
    int team = 0;               // 0 = игрок, 1 = враги
    uint8_t r = 130, g = 220, b = 255; // цвет трассера
};

// Частица/эффект (juice). Хранится в общем пуле scene.fx; интегрируется в sim,
// рисуется в draw. Ничего не влияет на геймплей — чистая косметика.
struct LocalFx {
    double x = 0.0, y = 0.0, z = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    double life = 0.0, maxLife = 1.0; // часы; альфа/размер интерполируются по life/maxLife
    double size = 1.0;                // базовый радиус, LU (для FX_RING растёт со временем)
    int kind = FX_SPARK;
    uint8_t r = 255, g = 255, b = 255, a = 255;
};

// Контейнер с грузом, выпавший из уничтоженного корабля. Дрейфует и тускнеет;
// подбирается автоматически при сближении (уважая cargoCapacity).
struct LocalLoot {
    double x = 0.0, y = 0.0, z = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    int element = 0;
    double amount = 0.0;
    double life = 0.0;          // часы до исчезновения
    double spin = 0.0;          // визуальное вращение
    uint8_t r = 210, g = 200, b = 140;
};

// Радиоисточник (deep-space POI / прогрессия детекторов, §7.2). Виден на HUD
// (пеленг+дистанция) только если detectorTier сцены >= tier источника и игрок
// в пределах RADIO_REVEAL_RANGE. Долёт на RADIO_CLAIM_RANGE выдаёт награду.
struct LocalRadioSource {
    double x = 0.0, y = 0.0, z = 0.0;
    int kind = RS_DERELICT;
    int tier = 0;               // требуемый детектор: 0 базовый, 1 нейтринный, 2 тёмной материи
    double strength = 1.0;      // сила сигнала для HUD (0..1)
    double reward = 0.0;        // кредиты за забор
    double research = 0.0;      // очки исследования за забор
    int element = -1;           // элемент клада (или -1)
    double amount = 0.0;        // масса клада
    bool revealed = false;      // уже проявлен детектором
    bool resolved = false;      // уже забран
    std::string label;
};

// Полное состояние локальной сцены. Одна на сессию; живёт в main.cpp.
struct LocalScene {
    bool active = false;
    int starIndex = -1;         // якорная звезда, или -1 = глубокий космос (пусто)
    bool hasStar = false;
    double starRadius = 110.0;   // гигантская по умолчанию; localgen задаёт 90..135 по металличности
    uint8_t starR = 255, starG = 236, starB = 170;
    double localHours = 0.0;    // накопленное локальное время — двигатель орбит
    double trafficTimer = 4.0;  // часы до следующей попытки прилёта торговца (§5.13.9; localgen рандомит старт)

    // Косметический тон туманности (0 = нет). Красит фон/дымку в некоторых системах.
    uint8_t nebulaR = 0, nebulaG = 0, nebulaB = 0;
    double nebulaStrength = 0.0; // 0..1

    std::vector<LocalBody> bodies;
    std::vector<LocalRock> rocks;
    std::vector<LocalCraft> craft;
    std::vector<LocalProjectile> shots;
    std::vector<LocalFx> fx;             // частицы/эффекты (juice)
    std::vector<LocalLoot> loot;         // контейнеры груза
    std::vector<LocalRadioSource> radio; // радиоисточники (POI/детекторы)

    // Игрок в локальных координатах:
    double px = 0.0, py = 0.0, pz = 0.0;
    double pvx = 0.0, pvy = 0.0, pvz = 0.0;
    // Ориентация корабля — ТЕЛО-ОТНОСИТЕЛЬНЫЙ ортонормированный базис (полное 3D, 3 оси,
    // как в Elite). pfwd — нос (куда тяга/оружие/выхлоп), pup — «крыша кабины».
    // right = pfwd × pup (см. localShipRight). Управление вращает этот базис ТЕЛЕСНО:
    //   yaw  (A/D) — вокруг pup, pitch (R/F) — вокруг right, roll (Q/E) — вокруг pfwd.
    // localsim ре-ортонормирует базис каждый кадр (защита от дрейфа). Экранный курс
    // для радара/карты выводится из pfwd (atan2). НИКАКИХ pyaw/ppitch — гимбал-лока нет.
    double pfwdX = 1.0, pfwdY = 0.0, pfwdZ = 0.0; // нос (единичный)
    double pupX  = 0.0, pupY  = 0.0, pupZ  = 1.0; // «вверх» корабля (единичный, ⟂ pfwd)
    double fireCooldown = 0.0;
    double playerMaxSpeed = LocalCfg::PLAYER_MAXSPEED; // из ship.speed, но с локальным полом
    double playerAccel = LocalCfg::PLAYER_ACCEL;

    // Щиты игрока (локальные). pShieldTimer — часы до начала регена.
    double pShield = 0.0, pMaxShield = 0.0, pShieldTimer = 0.0;
    bool   playerDestroyed = false;     // корпус дошёл до 0 -> main делает аварийный выход + штраф
    double playerHitFlash = 0.0;        // реальные сек вспышки урона игрока (для вигнетки)

    int miningRock = -1;        // индекс добываемого астероида, или -1
    double miningAccum = 0.0;   // сколько добыто за текущий сеанс (для HUD)
    int dockPrompt = -1;        // индекс тела в зоне стыковки, или -1 (для HUD-подсказки)
    int minePrompt = -1;        // индекс ближайшего астероида с рудой в зоне (для HUD/кнопки)
    int targetCraft = -1;       // ближайший/залоченный корабль (для HUD-таргета), или -1
    int lockTarget = -1;        // залоченная игроком цель (Tab циклит); -1 => авто-ближайший

    int detectorTier = 0;       // уровень детектора радиосигналов (из game.tech.sensors при генерации)

    double shake = 0.0;         // амплитуда тряски экрана (LU-эквивалент), затухает в sim
    double fxClock = 0.0;       // аккумулятор РЕАЛЬНЫХ секунд для анимации fx/тряски (независим от warp)
    bool   thrusting = false;   // держится ли тяга игрока в этом кадре (для трейла/HUD)

    std::string toast;          // всплывающее событие ("MINING", "SHIP DESTROYED", ...)
    double toastTimer = 0.0;    // реальные сек до скрытия toast
    bool warping = false;       // активно ли ускорение времени в этом кадре (для HUD)

    int distressPrev = 0;       // (§5.13.11) число жертв «под атакой» в прошлом кадре (телеметрия/совместимость)
    double distressCooldown = 0.0; // (§5.13.11) сек «рейд ещё активен» — дебаунс toast «CONVOY RAID» от дребезга счётчика

    double tradeCooldown = 0.0; // (§5.13.18) сек — дебаунс toast «TRADER SOLD» при стыковке зеркала-торговца
    int tradesExecuted = 0;     // (§5.13.18) счётчик сделок продажи груза зеркалом-торговцем за сцену (soak-пруф)
};

// Ввод игрока за кадр (собирается в main.cpp; edge-поля — фронт нажатия за 1 кадр).
struct LocalInput {
    bool thrust = false;      // W  — тяга по носу
    bool brake  = false;      // S  — гашение скорости (тяга против вектора v)
    bool yawL   = false;      // A  — рыскание влево (вокруг pup)
    bool yawR   = false;      // D  — рыскание вправо
    bool pitchU = false;      // R  — тангаж вверх (вокруг right)
    bool pitchD = false;      // F  — тангаж вниз
    bool rollL  = false;      // Q  — крен влево (вокруг pfwd)
    bool rollR  = false;      // E  — крен вправо
    bool fire   = false;      // Space — огонь
    bool warp   = false;      // Shift — ускорение времени (удержание)
    bool mineToggle = false;  // M (edge) — вкл/выкл добычу ближайшего астероида
    bool dock   = false;      // K (edge) — стыковка с телом в зоне
    bool cycleTarget = false; // Tab (edge) — переключить залоченную HUD-цель
    // Мышь-взгляд (шутер): доп. поворот ЗА ЭТОТ КАДР в радианах (уже помножен на
    // чувствительность в main.cpp). Складывается с клавиатурным рысканием/тангажом.
    double mouseYaw = 0.0;    // + = нос вправо
    double mousePitch = 0.0;  // + = нос вверх
};

// ---------------------------------------------------------------------------
//  Утилиты ориентации корабля (общие для gen/sim/draw/тестов — единая конвенция).
// ---------------------------------------------------------------------------

// right = pfwd × pup (правая тройка; согласована с makeCameraFrameBasis в camera.h).
inline void localShipRight(const LocalScene& s, double& rx, double& ry, double& rz) {
    rx = s.pfwdY * s.pupZ - s.pfwdZ * s.pupY;
    ry = s.pfwdZ * s.pupX - s.pfwdX * s.pupZ;
    rz = s.pfwdX * s.pupY - s.pfwdY * s.pupX;
}

// Направить нос корабля по вектору dir (не обязательно единичному) и пересобрать
// ортонормированный базис, держа «вверх» максимально близким к мировому +Z.
// Для генерации стартовой ориентации, а также для скриптов/soak-теста (наведение).
inline void localSetForward(LocalScene& s, double dx, double dy, double dz) {
    double fl = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (fl < 1e-9) { dx = 1.0; dy = 0.0; dz = 0.0; fl = 1.0; }
    dx /= fl; dy /= fl; dz /= fl;
    // предпочтительный «вверх» — мировой +Z, спроецированный ⟂ носу
    double ux = 0.0, uy = 0.0, uz = 1.0;
    double d = ux * dx + uy * dy + uz * dz;
    ux -= d * dx; uy -= d * dy; uz -= d * dz;
    double ul = std::sqrt(ux * ux + uy * uy + uz * uz);
    if (ul < 1e-6) {              // нос почти вертикален — берём мировой +Y как up
        ux = 0.0; uy = 1.0; uz = 0.0;
        d = ux * dx + uy * dy + uz * dz;
        ux -= d * dx; uy -= d * dy; uz -= d * dz;
        ul = std::sqrt(ux * ux + uy * uy + uz * uz);
    }
    ux /= ul; uy /= ul; uz /= ul;
    s.pfwdX = dx; s.pfwdY = dy; s.pfwdZ = dz;
    s.pupX = ux; s.pupY = uy; s.pupZ = uz;
}

// Построить камеру локального режима — ЕДИНЫЙ источник для игры (main.cpp) и
// скриншот-харнеса, чтобы кадр в тесте был ПОБИТОВО как в игре.
//   mapMode==false  =>  3D-КОКПИТ (вид «из глаз» корабля): перспектива, ГЛАЗ камеры
//                       ровно в позиции корабля, взгляд вдоль носа (pfwd), крен через
//                       pup. Свой корабль при этом не рисуется (см. localdraw §8).
//                       Фокус = winW*fovScale (fovScale — оптический зум/FOV на +/-).
//   mapMode==true   =>  орто вид-«карта» сверху: центр = игрок, scale = mapZoom пикс/LU.
// winH зарезервирован (проекция берёт центр экрана сама); НЕ трогает глобальный rng.
inline void buildLocalCamera(const LocalScene& s, int winW, int winH,
                             bool mapMode, double fovScale, double mapZoom,
                             View3D& view, CameraBasis& basis) {
    (void)winH;
    view = View3D();
    if (mapMode) {
        view.perspective = false;
        view.centerX = s.px; view.centerY = s.py; view.centerZ = s.pz;
        view.scale = mapZoom;
        basis = makeCameraBasis(view);
    } else {
        view.perspective = true;
        view.focal = double(winW) * fovScale;
        view.nearPlane = 0.5;
        view.centerX = s.px; view.centerY = s.py; view.centerZ = s.pz; // глаз = корабль
        basis = makeCameraFrameBasis(s.pfwdX, s.pfwdY, s.pfwdZ, s.pupX, s.pupY, s.pupZ);
    }
}

// ---------------------------------------------------------------------------
//  Контракт (реализации в localgen.cpp / localsim.cpp / localdraw.cpp)
// ---------------------------------------------------------------------------

// Детерминированно строит сцену из индекса звезды (или -1 = пустой космос).
// Сид: 0x9E3779B9u ^ uint32_t(starIndex) (для starIndex>=0), иначе 0xD1B54A35u.
// Заполняет звезду, планеты (+луны/кольца), пояс астероидов, корабли (в т.ч. из
// co-located макро-агентов), радиоисточники и стартовую позицию/скорость игрока.
// Читает game только для чтения; НИКОГДА не трогает глобальный rng.
void buildLocalScene(const Game& game, int starIndex, LocalScene& scene);

// Внешний вид астероида по индексу элемента (§5.13.15): базовая палитра (0..255) и
// зеркальность spec (0..1). Классы «пород» — по атомному номеру/металличности элемента
// (реальная таксономия M/C/S/лёд). ЧИСТАЯ арифметика над elementDefinitions(): без RNG,
// без global rng (§2.3) — зовётся из localgen (запекает в rock.r/g/b/spec) и из шот-харнеса.
void rockAppearance(int element, double& baseR, double& baseG, double& baseB, double& spec);

// Класс породы астероида по индексу элемента (§5.13.16): один из RockClass
// (SILICATE/ICE/CARBON/METAL). ЕДИНЫЙ источник таксономии — rockAppearance() (палитра/блеск)
// и добыча (rockYieldMult) зовут именно его. Чистая арифметика над elementDefinitions(),
// без RNG, без global rng (§2.3) — безопасно из localsim/localgen/шот-харнеса.
int rockClass(int element);

// Продвигает сцену на один кадр. dtReal — реальные секунды кадра.
// Мутирует и сцену, и game: добыча/лут кладут груз в трюм игрока (уважая
// cargoCapacity через shipCargoMass); бой меняет money/hullHP игрока (корпус
// МОЖЕТ дойти до 0 -> scene.playerDestroyed=true, main делает аварийный выход).
// Возвращает индекс звезды для стыковки (открыть рынок) в этом кадре, иначе -1.
int updateLocalScene(Game& game, LocalScene& scene, const LocalInput& in, double dtReal);

// Рисует сцену и локальный HUD. Камера (view/basis) строится buildLocalCamera:
//  • 3D-КОКПИТ: view.perspective==true, basis из makeCameraFrameBasis по ориентации
//    корабля; ГЛАЗ камеры = позиция корабля, взгляд вдоль носа. Свой корабль НЕ
//    рисуется — вместо него прицел-«мушка» по курсу + маркер вектора скорости.
//    Точки за ближней плоскостью помечаются pp.behind.
//  • Карта (переключатель C): view.perspective==false, верхний ортогональный вид
//    (свой корабль рисуется стрелкой — это тактический вид сверху).
// Фон — LOD-скайбокс реальных звёзд кластера (projectDirectionWithBasis): направление
// на звезду = normalize(star.pos - anchor.pos) в координатах кластера (anchor =
// game.cluster.stars[scene.starIndex]), яркость/размер по видимой звёздной величине.
// Тряска экрана (scene.shake) применяется ВНУТРИ draw. Переиспользует
// camera.h-проекцию и render2d.h-примитивы (namespace UI).
void renderLocalScene(SDL_Renderer* renderer, const Game& game, const LocalScene& scene,
                      const View3D& view, const CameraBasis& basis, int winW, int winH);
