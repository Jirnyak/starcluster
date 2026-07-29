// Скриншот-харнес локального режима (НЕ часть игры; вспомогательный инструмент).
// Рендерит несколько репрезентативных кадров микромира в BMP через ПРОГРАММНЫЙ
// рендерер SDL (SDL_CreateSoftwareRenderer → SDL_Surface → SDL_SaveBMP) — без окна,
// без доп. библиотек (SaveBMP встроен в SDL2). Камера строится тем же buildLocalCamera,
// что и игра, поэтому картинка ПОБИТОВО совпадает с реальным кадром.
//
// Собирается целью `make shots` (линкует LIBSOURCES, как soak). BMP затем конвертятся
// в PNG через macOS `sips` (см. Makefile) для визуальной проверки.
#include <SDL.h>
#include "local.h"
#include "game.h"
#include "ship.h"
#include "resource.h"
#include <cmath>
#include <cstdio>

// Крен камеры на angle рад вокруг носа (для демонстрации 3-осевой ориентации/скайбокса).
static void rollScene(LocalScene& s, double angle) {
    double rx, ry, rz; localShipRight(s, rx, ry, rz);
    const double c = std::cos(angle), sn = std::sin(angle);
    double ux = s.pupX * c + rx * sn;
    double uy = s.pupY * c + ry * sn;
    double uz = s.pupZ * c + rz * sn;
    const double l = std::sqrt(ux * ux + uy * uy + uz * uz);
    if (l > 1e-9) { s.pupX = ux / l; s.pupY = uy / l; s.pupZ = uz / l; }
}

// Навести нос на ближайший корабль (для боевого кадра).
static void aimNearestCraft(LocalScene& s) {
    int best = -1; double bd = 1e300;
    for (size_t i = 0; i < s.craft.size(); ++i) {
        const double dx = s.craft[i].x - s.px, dy = s.craft[i].y - s.py, dz = s.craft[i].z - s.pz;
        const double d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = (int)i; }
    }
    if (best >= 0) localSetForward(s, s.craft[best].x - s.px, s.craft[best].y - s.py, s.craft[best].z - s.pz);
}

static bool saveShot(SDL_Renderer* r, SDL_Surface* surf, const Game& game, const LocalScene& s,
                     int W, int H, bool mapMode, double fovScale, double mapZoom, const char* path) {
    View3D view; CameraBasis basis;
    buildLocalCamera(s, W, H, mapMode, fovScale, mapZoom, view, basis);
    SDL_SetRenderDrawColor(r, 3, 5, 14, 255);   // тот же фон, что и в игре
    SDL_RenderClear(r);
    renderLocalScene(r, game, s, view, basis, W, H);
    SDL_RenderPresent(r);
    // Сохраняем 24-битную RGB-копию: 32-битный ARGB-BMP из SDL_SaveBMP не читается
    // частью инструментов (sips/Preview) из-за альфа-заголовка; кадр всё равно
    // непрозрачен (фон залит с alpha=255), так что альфу просто отбрасываем.
    SDL_Surface* out = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGB24, 0);
    const int rc = out ? SDL_SaveBMP(out, path) : -1;
    if (out) SDL_FreeSurface(out);
    std::printf("  %-30s rc=%d  mode=%s  bodies=%d craft=%d radio=%d fx=%d shots=%d loot=%d\n",
                path, rc, mapMode ? "MAP" : "COCKPIT",
                (int)s.bodies.size(), (int)s.craft.size(), (int)s.radio.size(),
                (int)s.fx.size(), (int)s.shots.size(), (int)s.loot.size());
    return rc == 0;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        std::printf("SDL_Init note: %s (software render still ok)\n", SDL_GetError());

    const int W = 1280, H = 800;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) { std::printf("surface fail: %s\n", SDL_GetError()); return 2; }
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(surf);
    if (!r) { std::printf("renderer fail: %s\n", SDL_GetError()); return 2; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    Game game;
    game.init(1200);
    game.tech.sensors = 1.25;    // проявить радиоисточники (как в soak)
    const double dt = 0.05;
    int ok = 0, total = 0;

    // (1,2) СИСТЕМА (звезда 0), кокпит: держим дистанцию (без варпа, чтобы не влететь в
    //       светило и не залить экран его ореолом), нос на звезду; звезда — диск в кадре.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        for (int f = 0; f < 4; ++f) {
            localSetForward(s, -s.px, -s.py, -s.pz);      // нос на светило (в центре системы)
            LocalInput in; in.thrust = true;              // немного тяги — частицы выхлопа
            updateLocalScene(game, s, in, dt);
        }
        localSetForward(s, -s.px, -s.py, -s.pz);
        std::printf("  [system] dist-to-star = %.1f LU  starR=%.2f\n",
                    std::sqrt(s.px * s.px + s.py * s.py + s.pz * s.pz), s.starRadius);
        total += 2;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_system_cockpit.bmp");
        rollScene(s, 0.55);                               // крен — виден поворот скайбокса
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_system_rolled.bmp");
    }

    // (3) СИСТЕМА (звезда 0), карта (вид сверху): орбиты/планеты/стрелка игрока.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        for (int f = 0; f < 10; ++f) { localSetForward(s, -s.px, -s.py, -s.pz); LocalInput in; in.thrust = true; updateLocalScene(game, s, in, dt); }
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, true, 0.60, 0.9, "shot_system_map.bmp");
    }

    // (4) БОЙ (звезда 2), кокпит: гарантируем враждебного пирата, летим и стреляем.
    {
        LocalScene s; buildLocalScene(game, 2, s); s.active = true;
        if (!s.craft.empty()) { s.craft[0].hostile = true; s.craft[0].kind = CK_PIRATE; s.craft[0].aiState = 1; }
        s.pMaxShield = s.pMaxShield > 200.0 ? s.pMaxShield : 200.0; s.pShield = s.pMaxShield;
        if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
            Ship& ps = game.agents[game.playerAgent].ship; ps.hullHP = ps.maxHullHP;
        }
        for (int f = 0; f < 150; ++f) {
            aimNearestCraft(s);
            LocalInput in; in.thrust = true; in.fire = true;
            updateLocalScene(game, s, in, dt);
            if (s.playerDestroyed) {   // не даём умереть — держим кадр «в бою»
                s.playerDestroyed = false; s.pShield = s.pMaxShield;
                if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size())
                    game.agents[game.playerAgent].ship.hullHP = game.agents[game.playerAgent].ship.maxHullHP;
            }
        }
        aimNearestCraft(s);
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_combat_cockpit.bmp");
    }

    // (5) ГЛУБОКИЙ КОСМОС (звезда -1), кокпит: LOD-скайбокс всех звёзд кластера.
    {
        LocalScene s; buildLocalScene(game, -1, s); s.active = true;
        for (int f = 0; f < 8; ++f) { LocalInput in; in.thrust = true; in.warp = true; updateLocalScene(game, s, in, dt); }
        rollScene(s, 0.3);
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.55, 0.8, "shot_deepspace_skybox.bmp");
    }

    // (6) ЛИД-ПРИЦЕЛ (звезда 2), кокпит: цель идёт наперерез, нос наведён на её ТЕКУЩУЮ
    //     позицию (мушка на цели), поэтому янтарный пип упреждения заметно смещён от центра —
    //     наглядная проверка геометрии перехвата. Кадр статичный: сим НЕ шагаем, чтобы
    //     скорости/позиции были детерминированы и читаемы (перехват: |r+u·t|=S·t).
    {
        LocalScene s; buildLocalScene(game, 2, s); s.active = true;
        if (!s.craft.empty()) {
            LocalCraft& t = s.craft[0];
            t.hostile = true; t.kind = CK_PIRATE; t.label = "PIRATE";
            t.x = s.px + 250.0; t.y = s.py; t.z = s.pz;    // прямо по носу, 250 LU
            t.vx = -80.0; t.vy = 220.0; t.vz = 0.0;         // сближение + сильный поперечный снос
            if (t.maxShield <= 0.0) t.maxShield = 120.0;
            t.shield = t.maxShield * 0.6;
            s.pvx = s.pvy = s.pvz = 0.0;                     // игрок неподвижен: чистый лид
            localSetForward(s, 250.0, 0.0, 0.0);            // мушка — на текущую позицию цели
            s.targetCraft = 0; s.lockTarget = 0;
        }
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_combat_lead.bmp");
    }

    // (7) БЛИЗКО К ЗВЕЗДЕ (звезда 0), кокпит: игрок чуть снаружи поверхности, нос на светило.
    //     Изотропная сфера (ray-sphere) заполняет почти весь кадр плазмой с лимб-даркенингом
    //     и структурой — «гигантская звезда, корабли — ничто». Без порога/заглушки-заливки.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        const double R = s.starRadius;
        s.px = R * 1.25; s.py = 0.0; s.pz = 0.0;      // снаружи поверхности, D/R=1.25
        s.pvx = s.pvy = s.pvz = 0.0;
        localSetForward(s, -1.0, 0.0, 0.0);           // нос на светило (в центре)
        std::printf("  [near-star] D=%.1f LU  starR=%.1f LU  (D/R=1.25 => сфера почти во весь кадр)\n", s.px, R);
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_star_near.bmp");
    }

    // (7б) БЛИЗКО, НО НОС ОТ ЗВЕЗДЫ (звезда 0), кокпит: РЕШАЮЩАЯ проверка изотропии. Глаз в
    //      D/R=1.3 от центра, но смотрит ПРОЧЬ (+X, звезда позади). Изотропный ray-sphere
    //      обязан показать ЧЁРНЫЙ КОСМОС (скайбокс), а НЕ жёлтую заливку. Если тут жёлтое —
    //      значит вернулся старый баг-костыль «переключения режима». Кадр должен быть тёмным.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        const double R = s.starRadius;
        s.px = R * 1.3; s.py = 0.0; s.pz = 0.0;       // так же близко, как near-star
        s.pvx = s.pvy = s.pvz = 0.0;
        localSetForward(s, 1.0, 0.0, 0.0);            // нос ПРОЧЬ от звезды (звезда за спиной)
        std::printf("  [look-away] D=%.1f LU (D/R=1.3), нос ОТ звезды => ожидаем КОСМОС, не плазму\n", s.px);
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_star_lookaway.bmp");
    }

    // (8) ОККЛЮЗИЯ (звезда 0), кокпит: два пробных тела на оси глаз–звезда. FRONT (перед
    //     ближней поверхностью) виден на диске; BACK (за дальней поверхностью) обязан быть
    //     скрыт веществом звезды (ray-sphere occlusion). Наглядная проверка «планеты больше
    //     не просвечивают сквозь звезду».
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        const double R = s.starRadius;
        s.bodies.clear(); s.rocks.clear(); s.craft.clear(); s.loot.clear(); s.radio.clear();
        LocalBody front; front.kind = LB_ROCKY; front.radius = 6.0;
        front.r = 90; front.g = 255; front.b = 120; front.name = "FRONT";
        front.x = R * 1.4; front.y = 8.0; front.z = 0.0;      // снаружи, между глазом и звездой
        LocalBody back;  back.kind = LB_ROCKY; back.radius = 9.0;
        back.r = 255; back.g = 70; back.b = 70; back.name = "BACK";
        back.x = -R * 1.4; back.y = 0.0; back.z = 0.0;        // снаружи, но ЗА центром от глаза
        s.bodies.push_back(front); s.bodies.push_back(back);
        s.px = R * 2.4; s.py = 0.0; s.pz = 0.0; s.pvx = s.pvy = s.pvz = 0.0;
        localSetForward(s, -1.0, 0.0, 0.0);
        std::printf("  [occlusion] FRONT виден на диске, BACK скрыт звездой (за её центром)\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_star_occlusion.bmp");
    }

    // (9) ДУЛЬНАЯ ВСПЫШКА (звезда 2), кокпит: один выстрел, кадр СРАЗУ после (вспышка жива,
    //     life=0.05h > dt). Должен быть компактный аддитивный блик у дула, а НЕ синий квадрат.
    {
        LocalScene s; buildLocalScene(game, 2, s); s.active = true;
        LocalInput in; in.fire = true;
        updateLocalScene(game, s, in, 0.02);
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_muzzle.bmp");
    }

    // (10) ВНУТРИ ЗВЕЗДЫ (звезда 0), кокпит: глаз глубоко в веществе (D<R). Каждый луч входит
    //      в сферу (te=0 → от глаза), поэтому кадр окружён плазмой, но с НАПРАВЛЕННОЙ
    //      структурой (турбулентность/градиент по нормали), а не плоской заливкой. Изотропно.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        s.px = s.starRadius * 0.4; s.py = 0.0; s.pz = 0.0;
        s.pvx = s.pvy = s.pvz = 0.0;
        localSetForward(s, 1.0, 0.0, 0.0);
        std::printf("  [inside-star] D=%.1f < starR=%.1f -> плазма вокруг со структурой\n", s.px, s.starRadius);
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_star_inside.bmp");
    }

    // (10b) ВСПЫШКА-СОБЫТИЕ на лимбе (§5.13.13, звезда 0). Камера снаружи (D/R=2.3, диск целиком в
    //       кадре с полем короны), смотрит на звезду вдоль -X. fxClock=26 подобран так, что активная
    //       область №0 (ap≈1.56) стоит почти точно на лимбе (X-компонента dir≈0.01, т.е. сбоку) и её
    //       цикл эрупции в пике (frac→0): яркий горячий бело-голубой протуберанец + локальный вырост
    //       короны. При fxClock=0 (все прочие звёздные шоты) эффект <2% — без регресса.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        s.px = s.starRadius * 2.3; s.py = 0.0; s.pz = 0.0;
        s.pvx = s.pvy = s.pvz = 0.0;
        localSetForward(s, -1.0, 0.0, 0.0);
        s.fxClock = 26.0;
        std::printf("  [star-flare] D/R=2.3 fxClock=%.1f -> ожидаем горячую вспышку на лимбе\n", s.fxClock);
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_star_flare.bmp");
    }

    // (11) ПЛАНЕТА С КОЛЬЦАМИ КРУПНО, кокпит: находим первую звезду с ОКОЛЬЦОВАННЫМ газовым
    //      гигантом (детерминированно), смотрим сбоку (терминатор по центру диска) и чуть
    //      СВЕРХУ плоскости колец, чтобы кольца были РАСКРЫТЫ (эллипс), а не с ребра. Проверяет
    //      сферу-шейдер тела + перспективные кольца (передняя дуга поверх диска, задняя — за
    //      сферой) + тень планеты на кольцах. Фолбэк — крупнейший гигант / тело звезды 0.
    {
        LocalScene s; int useStar = 0, gi = -1; double bestR = 0.0;
        const int nStars = (int)game.cluster.stars.size();
        for (int st = 0; st < nStars && gi < 0; ++st) {
            LocalScene cand; buildLocalScene(game, st, cand);
            double br = 0.0; int found = -1;
            for (size_t i = 0; i < cand.bodies.size(); ++i)
                if (cand.bodies[i].kind == LB_GASGIANT &&
                    cand.bodies[i].ringOuter > cand.bodies[i].ringInner &&
                    cand.bodies[i].ringInner > 0.0 && cand.bodies[i].radius > br) {
                    br = cand.bodies[i].radius; found = (int)i;
                }
            if (found >= 0) { s = cand; useStar = st; gi = found; bestR = br; }
        }
        if (gi < 0) {                                   // колец нигде нет — крупнейший гигант зв.0
            buildLocalScene(game, 0, s); useStar = 0;
            for (size_t i = 0; i < s.bodies.size(); ++i)
                if (s.bodies[i].kind == LB_GASGIANT && s.bodies[i].radius > bestR) { bestR = s.bodies[i].radius; gi = (int)i; }
            if (gi < 0)
                for (size_t i = 0; i < s.bodies.size(); ++i)
                    if (s.bodies[i].kind != LB_STATION && s.bodies[i].radius > bestR) { bestR = s.bodies[i].radius; gi = (int)i; }
        }
        s.active = true;
        if (gi >= 0) {
            const LocalBody& b = s.bodies[gi];
            double dl = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z); if (dl < 1e-6) dl = 1.0;
            const double ux = b.x / dl, uy = b.y / dl, uz = b.z / dl;      // от звезды к планете
            double perpx = uy, perpy = -ux;                                // ⟂ линии звезда–планета
            double pl = std::sqrt(perpx * perpx + perpy * perpy);
            if (pl < 1e-6) { perpx = 1.0; perpy = 0.0; pl = 1.0; }
            perpx /= pl; perpy /= pl;
            // Сбоку (терминатор) + приподняты над плоскостью колец (+Z ≈ нормаль колец) => кольца
            // РАСКРЫТЫ эллипсом; лёгкое смещение к звезде => ~3/4 фаза.
            double offx = perpx * 0.72 - ux * 0.26;
            double offy = perpy * 0.72 - uy * 0.26;
            double offz = 0.60 - uz * 0.26;
            double ol = std::sqrt(offx * offx + offy * offy + offz * offz); if (ol < 1e-6) ol = 1.0;
            offx /= ol; offy /= ol; offz /= ol;
            const double D = b.radius * 3.6;                               // кольца целиком в кадр
            s.px = b.x + offx * D; s.py = b.y + offy * D; s.pz = b.z + offz * D;
            s.pvx = s.pvy = s.pvz = 0.0;
            localSetForward(s, b.x - s.px, b.y - s.py, b.z - s.pz);         // нос на планету
            std::printf("  [planet] star=%d kind=%d R=%.1f ring=%.1f..%.1f view D=%.1f LU\n",
                        useStar, b.kind, b.radius, b.ringInner, b.ringOuter, D);
            total += 1;
            ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_planet_near.bmp");
        }
    }

    // (12) ГОРЯЧАЯ (сине-белая) ЗВЕЗДА, кокпит: проверка спектрального контраста O→M. Ищем
    //      пульсар (stellarClass==1 → цвет 190,210,255) — самый «синий»; фолбэк — тёплая звезда
    //      с макс. металличностью (самая синяя из обычных). Кадрируем полный диск с короной, как
    //      shot_system_cockpit (там холодная STAR70 — красно-оранж): рядом видно, что горячая
    //      звезда сине-белая с ярким синим лимбом/короной, а холодная — глубоко-красная.
    {
        const int nStars = (int)game.cluster.stars.size();
        int hs = -1, bluest = 0; double bestMet = -1.0;
        for (int st = 0; st < nStars; ++st) {
            if (game.cluster.stars[st].stellarClass == 1) { hs = st; break; } // пульсар — синейший
            if (game.cluster.stars[st].metallicity > bestMet) {
                bestMet = game.cluster.stars[st].metallicity; bluest = st;
            }
        }
        if (hs < 0) hs = bluest;            // пульсара нет → самая синяя тёплая (макс. металличность)
        LocalScene s; buildLocalScene(game, hs, s); s.active = true;
        const double R = s.starRadius;
        s.px = R * 2.2; s.py = 0.0; s.pz = 0.0;       // полный диск + корона в кадре
        s.pvx = s.pvy = s.pvz = 0.0;
        localSetForward(s, -1.0, 0.0, 0.0);           // нос на светило
        std::printf("  [hot-star] star=%d class=%d rgb=%d,%d,%d D/R=2.2 (спектр. контраст O->M)\n",
                    hs, game.cluster.stars[hs].stellarClass, (int)s.starR, (int)s.starG, (int)s.starB);
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_star_hot.bmp");
    }

    // (13) ПОЯС АСТЕРОИДОВ вблизи (звезда 0), кокпит: подлёт к крупнейшей глыбе сбоку от
    //      направления света, чтобы терминатор день/ночь шёл по центру диска. Проверяем
    //      renderRockLit: освещённая по Ламберту сфера с неровным (выгрызенным) силуэтом,
    //      кратерами/сколами и кувырканием (spin), плюс янтарные кольца добычи вокруг цели.
    //      Мелкие соседние камни идут дешёвым фолбэком (фазовый диск).
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        int big = -1; double bestR = -1.0;
        for (size_t i = 0; i < s.rocks.size(); ++i)
            if (s.rocks[i].radius > bestR) { bestR = s.rocks[i].radius; big = (int)i; }
        if (big >= 0) {
            const LocalRock& rk = s.rocks[big];
            const double dl = std::sqrt(rk.x*rk.x + rk.y*rk.y + rk.z*rk.z);
            // Единичный радиус-вектор звезда->камень (= направление света у камня).
            const double ux = dl>1e-6 ? rk.x/dl : 1.0;
            const double uy = dl>1e-6 ? rk.y/dl : 0.0;
            const double uz = dl>1e-6 ? rk.z/dl : 0.0;
            // Тангенс в плоскости пояса (⟂ свету): perp = norm(light × Z).
            double px = uy*1.0 - uz*0.0, py = uz*0.0 - ux*1.0, pz = ux*0.0 - uy*0.0;
            double pl = std::sqrt(px*px + py*py + pz*pz);
            if (pl < 1e-6) { px = 1.0; py = 0.0; pz = 0.0; pl = 1.0; }
            px/=pl; py/=pl; pz/=pl;
            const double off = std::max(14.0, rk.radius * 6.0);
            // Глаз сбоку (перпендикулярно свету) + чуть к звезде, чтобы светило ушло за спину.
            s.px = rk.x + px*off - ux*off*0.35;
            s.py = rk.y + py*off - uy*off*0.35;
            s.pz = rk.z + pz*off - uz*off*0.35 + rk.radius*0.6;
            s.pvx = s.pvy = s.pvz = 0.0;
            localSetForward(s, rk.x - s.px, rk.y - s.py, rk.z - s.pz);  // нос на глыбу
            s.miningRock = big;                                          // янтарные кольца добычи
            std::printf("  [belt] rocks=%d big=%d R=%.2f off=%.1f dl=%.0f LU\n",
                        (int)s.rocks.size(), big, rk.radius, off, dl);
        }
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_belt_near.bmp");
    }

    // (13+) ТИПЫ ПОРОД (§5.13.15): четыре камня в ряд — по одному на класс материала
    //       (лёд O / углерод C / металл Fe / силикат Si), палитра/зеркальность взяты РОВНО
    //       из rockAppearance() (та же ф-я, что печёт цвета пояса в localgen). Свет — звезда
    //       в (0,0,0); группа вынесена вне оси, глаз с освещённой стороны и чуть сверху ⇒ на
    //       каждом видна дневная сторона + терминатор, а на льду/металле — зеркальный блик,
    //       тогда как углерод/силикат матовы. QA per-element палитры и specular-ветки.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        s.rocks.clear(); s.craft.clear(); s.loot.clear(); s.radio.clear(); s.miningRock = -1;
        const int    reps[4] = {7, 5, 25, 13};       // индексы элементов = Z−1: O, C, Fe, Si
        const double R = 6.5;                         // > ROCK_SHADE_MIN ⇒ лит-шейдер, не фолбэк
        const double c0x = 300.0, c0y = 300.0;        // центр группы вне оси — свет под ~45°
        const double ll = std::sqrt(c0x * c0x + c0y * c0y);
        const double ux = c0x / ll, uy = c0y / ll;    // −(ux,uy,0) = направление на звезду у камня
        const double perpx = -uy, perpy = ux;         // ⟂ свету в плоскости XY — вдоль ряда
        const double step = R * 3.4;
        for (int k = 0; k < 4; ++k) {
            LocalRock rk;
            rk.x = c0x + perpx * (k - 1.5) * step;
            rk.y = c0y + perpy * (k - 1.5) * step;
            rk.z = 0.0;
            rk.radius = R;
            rk.orbitAng = 0.7 * k;                    // разный сид силуэта/шума на каждом
            rk.spin = 0.5 * k;
            rk.element = reps[k];
            double br, bg, bb, sp; rockAppearance(reps[k], br, bg, bb, sp);
            rk.r = (uint8_t)br; rk.g = (uint8_t)bg; rk.b = (uint8_t)bb; rk.spec = sp;
            s.rocks.push_back(rk);
        }
        // Глаз с освещённой стороны (сдвиг к звезде вдоль −u) и приподнят по Z; нос — на центр ряда.
        s.px = c0x - ux * 30.0; s.py = c0y - uy * 30.0; s.pz = 40.0;
        s.pvx = s.pvy = s.pvz = 0.0;
        localSetForward(s, c0x - s.px, c0y - s.py, 0.0 - s.pz);
        // (§5.13.16) Заодно печатаем класс+множитель выхода по каждому элементу: проверяем, что
        // rockClass(O/C/Fe/Si) -> ICE/CARBON/METAL/SILICATE и yields = 1.25/0.65/1.60/1.00.
        std::printf("  [belt-types] rocks=%d elems=O/C/Fe/Si specs=%.2f/%.2f/%.2f/%.2f "
                    "class=%s/%s/%s/%s yields=%.2f/%.2f/%.2f/%.2f\n",
                    (int)s.rocks.size(), s.rocks[0].spec, s.rocks[1].spec, s.rocks[2].spec, s.rocks[3].spec,
                    rockClassName(rockClass(reps[0])), rockClassName(rockClass(reps[1])),
                    rockClassName(rockClass(reps[2])), rockClassName(rockClass(reps[3])),
                    rockYieldMult(rockClass(reps[0])), rockYieldMult(rockClass(reps[1])),
                    rockYieldMult(rockClass(reps[2])), rockYieldMult(rockClass(reps[3])));
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_belt_types.bmp");
    }

    // (14+) ЛУЧ ДОБЫЧИ (§5.13.17): майнинг-лазер из кокпита к глыбе, ОКРАШЕННЫЙ в цвет КЛАССА
    //       породы. Берём крупнейший камень пояса, ПРИНУДИТЕЛЬНО делаем его металлическим
    //       (element=Fe ⇒ ROCK_METAL) и перепекаем палитру ⇒ луч выйдет сталь-серым; ставим глаз
    //       В ЗОНЕ добычи (< radius+MINE_RANGE) и включаем miningRock ⇒ рисуется луч + точка удара.
    //       fxClock подобран так, что пульс близок к пику. QA: гейт луча «в зоне» + окраска по классу.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        s.craft.clear(); s.loot.clear(); s.radio.clear();
        int big = -1; double bigR = 0.0;
        for (size_t i = 0; i < s.rocks.size(); ++i)
            if (s.rocks[i].radius > bigR) { bigR = s.rocks[i].radius; big = (int)i; }
        if (big >= 0) {
            LocalRock& rk = s.rocks[big];
            rk.element = 25;                             // Fe (Z=26) ⇒ ROCK_METAL — сталь-серый луч
            double br, bg, bb, sp; rockAppearance(rk.element, br, bg, bb, sp);
            rk.r = (uint8_t)br; rk.g = (uint8_t)bg; rk.b = (uint8_t)bb; rk.spec = sp;
            rk.ore = 80.0;                               // есть что добывать
            const double dl = std::sqrt(rk.x*rk.x + rk.y*rk.y + rk.z*rk.z);
            const double ux = dl>1e-6 ? rk.x/dl : 1.0, uy = dl>1e-6 ? rk.y/dl : 0.0;
            double perpx = -uy, perpy = ux;              // ⟂ свету в плоскости XY
            double pl = std::sqrt(perpx*perpx + perpy*perpy);
            if (pl < 1e-6) { perpx = 1.0; perpy = 0.0; pl = 1.0; }
            perpx/=pl; perpy/=pl;
            const double off = rk.radius + LocalCfg::MINE_RANGE * 0.45;   // < radius+MINE_RANGE ⇒ В ЗОНЕ
            s.px = rk.x + perpx*off - ux*off*0.3;
            s.py = rk.y + perpy*off - uy*off*0.3;
            s.pz = rk.z + rk.radius*0.8 + 3.0;
            s.pvx = s.pvy = s.pvz = 0.0;
            localSetForward(s, rk.x - s.px, rk.y - s.py, rk.z - s.pz);    // нос на глыбу
            s.miningRock = big; s.miningAccum = 42.0;
            s.fxClock = 0.08;                            // фаза ⇒ луч у пика пульса
            const double dd = std::sqrt((rk.x-s.px)*(rk.x-s.px)+(rk.y-s.py)*(rk.y-s.py)+(rk.z-s.pz)*(rk.z-s.pz));
            std::printf("  [mining-beam] rock=%d R=%.2f class=%s dist=%.1f range=%.1f inRange=%s beamRGB=%d,%d,%d\n",
                        big, rk.radius, rockClassName(rockClass(rk.element)), dd,
                        rk.radius + LocalCfg::MINE_RANGE, (dd <= rk.radius + LocalCfg::MINE_RANGE ? "YES" : "no"),
                        rk.r, rk.g, rk.b);
        }
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_mining_beam.bmp");
    }

    // (14) ТУМАННОСТЬ изолированно: глубокий космос (звезды НЕТ), туманность включена
    //      принудительно (QA рендера renderNebula — газовое поле на небесной сфере: клочья/
    //      волокна, крупная «банка», медленный дрейф; без диска/короны звезды, чтобы не путать
    //      с короной). Нос — вдоль оси «банки» (та же формула из тона, что в renderNebula) ⇒
    //      плотная часть неба в центре кадра. Пурпурный тон (пара к амбру shot_system_cockpit).
    {
        LocalScene s; buildLocalScene(game, -1, s); s.active = true;   // -1 = глубокий космос
        s.nebulaStrength = 0.60; s.nebulaR = 110; s.nebulaG = 50; s.nebulaB = 120; // magenta, поярче
        const double axx = std::sin(110.0*0.021 + 1.3);
        const double axy = std::sin(50.0 *0.017 + 2.7);
        const double axz = std::sin(120.0*0.013 + 0.6);
        s.px = s.py = s.pz = 0.0; s.pvx = s.pvy = s.pvz = 0.0;
        localSetForward(s, axx, axy, axz);            // нос в плотную часть неба
        std::printf("  [nebula] forced strength=%.2f rgb=%d,%d,%d (deep space, QA renderNebula)\n",
                    s.nebulaStrength, (int)s.nebulaR, (int)s.nebulaG, (int)s.nebulaB);
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_nebula.bmp");
    }

    // (15) ТРАФИК (§5.13.10): пришвартованный торговец у рынка — пульсирующее ЯНТАРНОЕ кольцо
    //      швартовки (errand==1) — плюс циан-вспышка «варпа» (прилёт: FX_RING + FX_MUZZLE) рядом,
    //      и HUD-тальник трафика (IN/DOCK) в панели радара покажет DOCK>=1. Проверяет berth-
    //      индикатор, счётчик и рендер warp-FX разом. Кадр сбоку от рыночного тела.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        int mi = -1;
        for (size_t i = 0; i < s.bodies.size(); ++i)
            if (s.bodies[i].hasMarket) { mi = (int)i; break; }
        if (mi < 0)                                    // фолбэк: крупнейшее не-станционное тело
            for (size_t i = 0; i < s.bodies.size(); ++i)
                if (s.bodies[i].kind != LB_STATION && (mi < 0 || s.bodies[i].radius > s.bodies[mi].radius)) mi = (int)i;
        if (mi >= 0 && !s.craft.empty()) {
            const LocalBody& b = s.bodies[mi];
            LocalCraft& t = s.craft[0];                // пришвартованный торговец у причала
            t.hostile = false; t.kind = CK_TRADER; t.label = "TRADER";
            t.r = 90; t.g = 200; t.b = 235;
            t.errand = 1; t.errandBody = mi; t.errandTimer = 6.0;
            t.x = b.x + b.radius + 12.0; t.y = b.y; t.z = b.z + 3.0;
            t.vx = t.vy = t.vz = 0.0;
            t.hullHP = t.maxHullHP; t.shield = t.maxShield;
            const double D = std::max(60.0, b.radius * 4.0);   // камера сбоку: тело + причал в кадре
            s.px = b.x + D; s.py = b.y; s.pz = b.z + D * 0.35;
            s.pvx = s.pvy = s.pvz = 0.0;
            localSetForward(s, b.x - s.px, b.y - s.py, b.z - s.pz);
            s.targetCraft = 0;
            double wx = b.x - b.radius - 20.0, wy = b.y + 10.0, wz = b.z; // циан-варп рядом (прилёт)
            { LocalFx f; f.x=wx; f.y=wy; f.z=wz; f.vx=f.vy=f.vz=0.0; f.kind=FX_RING;
              f.size=26.0; f.life=0.6; f.maxLife=0.9; f.r=90; f.g=210; f.b=255; f.a=255; s.fx.push_back(f); }
            { LocalFx f; f.x=wx; f.y=wy; f.z=wz; f.vx=f.vy=f.vz=0.0; f.kind=FX_MUZZLE;
              f.size=2.6; f.life=0.35; f.maxLife=0.5; f.r=150; f.g=230; f.b=255; f.a=255; s.fx.push_back(f); }
            std::printf("  [traffic] market body=%d R=%.1f docked TRADER at berth + warp-FX beside\n", mi, b.radius);
        }
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_traffic_dock.bmp");
    }

    // (16) БЕДСТВИЕ КОНВОЯ (§5.13.11): пират в дальности оружия у торговца-жертвы. Жертва помечена
    //      underAttack -> красный SOS-маяк (кольцо у борта) + рамка/чип «SOS» в панели цели (наводим
    //      на жертву). Пират рядом — красный враждебный треугольник + красный muzzle к жертве.
    //      Проверяет маяк, панель бедствия и краевой маркер разом. Инсценируется в открытом космосе
    //      вдали от светила (без окклюзии). underAttack форсируем — sim в headless не запускается.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 2) { LocalCraft c; s.craft.push_back(c); } // гарантируем 2 борта
        LocalCraft& v = s.craft[0];                    // ЖЕРТВА-торговец
        v.hostile = false; v.kind = CK_TRADER; v.label = "TRADER"; v.faction = 1;
        v.r = 90; v.g = 200; v.b = 235;
        v.errand = 0; v.errandBody = -1;
        v.underAttack = true;                          // ФОРСИРУЕМ разметку бедствия
        v.x = 600.0; v.y = 0.0; v.z = 180.0; v.vx = v.vy = v.vz = 0.0;
        v.maxHullHP = 60.0; v.hullHP = v.maxHullHP * 0.55; v.maxShield = 0.0; v.shield = 0.0;
        LocalCraft& pir = s.craft[1];                  // АТАКУЮЩИЙ пират
        pir.hostile = true; pir.kind = CK_PIRATE; pir.label = "PIRATE"; pir.faction = -1;
        pir.r = 230; pir.g = 90; pir.b = 80;
        pir.aiState = 1; pir.errand = 0; pir.errandBody = -1;
        pir.threatConvoy = true; pir.threatVictimFaction = v.faction;
        pir.x = 600.0; pir.y = 50.0; pir.z = 175.0; pir.vx = pir.vy = pir.vz = 0.0; // ~50 LU (< WEAPON_RANGE)
        pir.maxHullHP = 45.0; pir.hullHP = pir.maxHullHP;
        s.px = 510.0; s.py = -20.0; s.pz = 235.0; s.pvx = s.pvy = s.pvz = 0.0; // камера сзади-сбоку
        localSetForward(s, v.x - s.px, v.y - s.py, v.z - s.pz);               // нос на жертву
        s.targetCraft = 0;                             // цель — жертва -> панель покажет SOS
        s.fxClock = 0.26;                              // фаза пульса -> яркое SOS-кольцо
        { double dx=v.x-pir.x, dy=v.y-pir.y, dz=v.z-pir.z, d=std::sqrt(dx*dx+dy*dy+dz*dz), inv=(d>1e-6)?1.0/d:0.0;
          LocalFx f; f.x=pir.x+dx*inv*4.0; f.y=pir.y+dy*inv*4.0; f.z=pir.z+dz*inv*4.0; f.vx=f.vy=f.vz=0.0;
          f.kind=FX_MUZZLE; f.size=2.2; f.life=0.05; f.maxLife=0.05; f.r=255; f.g=90; f.b=70; f.a=255; s.fx.push_back(f); }
        std::printf("  [distress] pirate 50 LU from victim TRADER (underAttack), targeting victim -> SOS beacon+panel\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_convoy_distress.bmp");
    }

    // (17) ЭСКОРТ-ПАТРУЛЬ (§5.13.12): патруль летит на перехват налётчика у торговца. Патруль помечен
    //      defending -> зелёное кольцо-эскорт у борта + зелёная рамка/метка/чип «ESCORT» в панели цели
    //      (наводим на патруль). В кадре разом: SOS-жертва (красный маяк), пират (красный треуг.+muzzle)
    //      и патруль (зелёный маяк + cyan-muzzle к пирату). Флаги форсируем — sim в headless не крутится.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 3) { LocalCraft c; s.craft.push_back(c); } // гарантируем 3 борта
        LocalCraft& v = s.craft[0];                    // ЖЕРТВА-торговец
        v.hostile = false; v.kind = CK_TRADER; v.label = "TRADER"; v.faction = 1;
        v.r = 90; v.g = 200; v.b = 235; v.errand = 0; v.errandBody = -1;
        v.underAttack = true;
        v.x = 600.0; v.y = 0.0; v.z = 180.0; v.vx = v.vy = v.vz = 0.0;
        v.maxHullHP = 60.0; v.hullHP = v.maxHullHP * 0.5; v.maxShield = 0.0; v.shield = 0.0;
        LocalCraft& pir = s.craft[1];                  // НАЛЁТЧИК-пират
        pir.hostile = true; pir.kind = CK_PIRATE; pir.label = "PIRATE"; pir.faction = -1;
        pir.r = 230; pir.g = 90; pir.b = 80;
        pir.aiState = 1; pir.errand = 0; pir.errandBody = -1;
        pir.threatConvoy = true; pir.threatVictimFaction = v.faction;
        pir.x = 600.0; pir.y = 45.0; pir.z = 175.0; pir.vx = pir.vy = pir.vz = 0.0;
        pir.maxHullHP = 45.0; pir.hullHP = pir.maxHullHP;
        LocalCraft& pat = s.craft[2];                  // ПАТРУЛЬ-ЭСКОРТ (идёт на перехват)
        pat.hostile = false; pat.kind = CK_PATROL; pat.label = "PATROL"; pat.faction = 1;
        pat.r = 110; pat.g = 215; pat.b = 170;
        pat.aiState = 1; pat.errand = 0; pat.errandBody = -1;
        pat.defending = true;                          // ФОРСИРУЕМ маркер эскорта
        pat.x = 560.0; pat.y = -40.0; pat.z = 205.0; pat.vx = pat.vy = pat.vz = 0.0; // ~99 LU от пирата (< WEAPON_RANGE)
        pat.maxHullHP = 70.0; pat.hullHP = pat.maxHullHP; pat.maxShield = 20.0; pat.shield = 20.0;
        s.px = 520.0; s.py = -85.0; s.pz = 225.0; s.pvx = s.pvy = s.pvz = 0.0;        // камера позади патруля
        localSetForward(s, pir.x - s.px, pir.y - s.py, pir.z - s.pz);                 // нос на пирата (общий фокус)
        s.targetCraft = 2;                             // цель — патруль -> панель покажет ESCORT
        s.fxClock = 0.20;                              // фаза: и SOS-, и зелёный пульс ярко видны
        { double dx=v.x-pir.x, dy=v.y-pir.y, dz=v.z-pir.z, d=std::sqrt(dx*dx+dy*dy+dz*dz), inv=(d>1e-6)?1.0/d:0.0;
          LocalFx f; f.x=pir.x+dx*inv*4.0; f.y=pir.y+dy*inv*4.0; f.z=pir.z+dz*inv*4.0; f.vx=f.vy=f.vz=0.0;
          f.kind=FX_MUZZLE; f.size=2.2; f.life=0.05; f.maxLife=0.05; f.r=255; f.g=90; f.b=70; f.a=255; s.fx.push_back(f); }
        { double dx=pir.x-pat.x, dy=pir.y-pat.y, dz=pir.z-pat.z, d=std::sqrt(dx*dx+dy*dy+dz*dz), inv=(d>1e-6)?1.0/d:0.0;
          LocalFx f; f.x=pat.x+dx*inv*4.0; f.y=pat.y+dy*inv*4.0; f.z=pat.z+dz*inv*4.0; f.vx=f.vy=f.vz=0.0;
          f.kind=FX_MUZZLE; f.size=2.2; f.life=0.05; f.maxLife=0.05; f.r=150; f.g=230; f.b=255; f.a=255; s.fx.push_back(f); }
        std::printf("  [escort] patrol ~99 LU from raider (defending), targeting patrol -> green escort ring+ESCORT panel\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_patrol_escort.bmp");
    }

    // (18) РОЗЫСК (§5.13.25): пират «в розыске» (wanted) в кадре, наведён -> золотое кольцо-розыск у борта
    //      + золотая рамка/чип «WANTED» и строка «BOUNTY N CR» в панели цели. Награда назначается ещё при
    //      генерации (§5.13.24, детерминированный хэш); здесь форсируем флаг+сумму (sim в headless не крутится).
    //      Открытый космос вдали от светила — без окклюзии. Темп золотого пульса (5.2) отличен от SOS/эскорта.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 1) { LocalCraft c; s.craft.push_back(c); } // гарантируем 1 борт
        LocalCraft& pir = s.craft[0];                  // ПИРАТ В РОЗЫСКЕ
        pir.hostile = true; pir.kind = CK_PIRATE; pir.label = "PIRATE"; pir.faction = -1;
        pir.r = 230; pir.g = 90; pir.b = 80;
        pir.aiState = 1; pir.errand = 0; pir.errandBody = -1;
        pir.wanted = true; pir.wantedBounty = 650.0;   // ФОРСИРУЕМ метку розыска + сумму (детерм. диапазон 250..900)
        pir.x = 600.0; pir.y = 0.0; pir.z = 180.0; pir.vx = pir.vy = pir.vz = 0.0;
        pir.maxHullHP = 45.0; pir.hullHP = pir.maxHullHP * 0.8; pir.maxShield = 15.0; pir.shield = 15.0;
        s.px = 510.0; s.py = -20.0; s.pz = 235.0; s.pvx = s.pvy = s.pvz = 0.0; // камера сзади-сбоку
        localSetForward(s, pir.x - s.px, pir.y - s.py, pir.z - s.pz);         // нос на пирата
        s.targetCraft = 0;                             // цель — пират в розыске -> панель покажет WANTED + BOUNTY
        s.fxClock = 0.30;                              // фаза золотого пульса -> яркое кольцо/рамка
        std::printf("  [wanted] wanted pirate (bounty 650) targeted -> gold ring + WANTED panel + BOUNTY line\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_wanted.bmp");
    }

    // (19) РАДАР-УГРОЗЫ (§5.13.27): раньше радар (низ-справа) красил блип только «враг=красный,
    //      иначе серый». Теперь цвет кодирует состояние борта тем же языком, что и кабина: розыск→
    //      золотой (пульс 5.2), SOS/под огнём→красный (пульс 6.0), эскорт-патруль→зелёный (пульс 4.5),
    //      простой враг→ровный красный, нейтраль→серый. Приоритет розыск>SOS>эскорт>враг. Пять бортов
    //      в разных состояниях в пределах дальности (1400 LU) -> пять цветов блипов. Только рендер.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 5) { LocalCraft c; s.craft.push_back(c); } // гарантируем 5 бортов
        s.px = 500.0; s.py = 0.0; s.pz = 200.0; s.pvx = s.pvy = s.pvz = 0.0;
        localSetForward(s, 1.0, 0.0, 0.0);             // нос вдоль +X (радар: вперёд=+X, право=−Y)
        // Разнесённые смещения по (dx,dy) -> распределённые блипы (Z радаром игнорируется).
        LocalCraft& w = s.craft[0];                    // РОЗЫСК -> золотой (перебивает hostile)
        w.kind = CK_PIRATE; w.label = "PIRATE"; w.faction = -1; w.r = 230; w.g = 90; w.b = 80;
        w.aiState = 1; w.errand = 0; w.errandBody = -1;
        w.hostile = true; w.wanted = true; w.wantedBounty = 650.0;
        w.x = s.px + 700.0; w.y = s.py + 300.0; w.z = s.pz; w.vx = w.vy = w.vz = 0.0;
        w.maxHullHP = 45.0; w.hullHP = w.maxHullHP;
        LocalCraft& u = s.craft[1];                    // SOS: не-пират под огнём -> красный пульс
        u.kind = CK_TRADER; u.label = "TRADER"; u.faction = 2; u.r = 120; u.g = 170; u.b = 220;
        u.aiState = 1; u.errand = 0; u.errandBody = -1;
        u.hostile = false; u.underAttack = true;
        u.x = s.px + 500.0; u.y = s.py - 400.0; u.z = s.pz; u.vx = u.vy = u.vz = 0.0;
        u.maxHullHP = 40.0; u.hullHP = u.maxHullHP * 0.6;
        LocalCraft& d = s.craft[2];                    // ЭСКОРТ: патруль на перехвате -> зелёный пульс
        d.kind = CK_PATROL; d.label = "PATROL"; d.faction = 1; d.r = 110; d.g = 215; d.b = 170;
        d.aiState = 1; d.errand = 0; d.errandBody = -1;
        d.hostile = false; d.defending = true;
        d.x = s.px - 300.0; d.y = s.py + 500.0; d.z = s.pz; d.vx = d.vy = d.vz = 0.0;
        d.maxHullHP = 70.0; d.hullHP = d.maxHullHP;
        LocalCraft& h = s.craft[3];                    // ПРОСТОЙ ВРАГ -> ровный красный
        h.kind = CK_PIRATE; h.label = "PIRATE"; h.faction = -1; h.r = 230; h.g = 90; h.b = 80;
        h.aiState = 1; h.errand = 0; h.errandBody = -1;
        h.hostile = true; h.wanted = false; h.underAttack = false; h.defending = false;
        h.x = s.px + 800.0; h.y = s.py + 100.0; h.z = s.pz; h.vx = h.vy = h.vz = 0.0;
        h.maxHullHP = 45.0; h.hullHP = h.maxHullHP;
        LocalCraft& n = s.craft[4];                    // НЕЙТРАЛЬ -> серый
        n.kind = CK_TRADER; n.label = "TRADER"; n.faction = 2; n.r = 120; n.g = 170; n.b = 220;
        n.aiState = 1; n.errand = 0; n.errandBody = -1;
        n.hostile = false; n.wanted = false; n.underAttack = false; n.defending = false;
        n.x = s.px - 200.0; n.y = s.py - 600.0; n.z = s.pz; n.vx = n.vy = n.vz = 0.0;
        n.maxHullHP = 40.0; n.hullHP = n.maxHullHP;
        s.targetCraft = -1;                            // без цели — фокус на радаре
        s.fxClock = 0.30;                              // фаза: пульсы блипов ярко видны
        std::printf("  [radar-threat] 5 craft wanted/SOS/escort/hostile/neutral -> gold/red-pulse/green/red/grey blips\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_radar_threat.bmp");
    }

    // (20) ПОГОНЯ ЗАКОНА (§5.13.29): defending-патруль (§5.13.28) активно преследует РОЗЫСКНОГО
    //      пирата в радиусе PATROL_DISTRESS_R. Прежние подсказки говорили ЧТО патруль на перехвате,
    //      но не КОГО — теперь золотой вектор перехвата (маршевый пунктир + остриё) тянется
    //      патруль→жертва в 3D, золотая связка блипов на радаре, и строка «IN PURSUIT» в панели цели.
    //      Цель ПЕРЕВЫВЕДЕНА рендером из живых полей (тот же скан, что и sim §5.13.28) — ноль полей/RNG/шага.
    //      Оба борта в открытом космосе вдали от светила (без окклюзии); патруль наведён -> ESCORT + IN PURSUIT.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 2) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(2);                             // ровно два борта: патруль + его розыскная жертва
        LocalCraft& pat = s.craft[0];                  // ПАТРУЛЬ, ведущий охоту (defending)
        pat.kind = CK_PATROL; pat.label = "PATROL"; pat.faction = 1; pat.r = 110; pat.g = 215; pat.b = 170;
        pat.hostile = false; pat.defending = true; pat.aiState = 1; pat.errand = 0; pat.errandBody = -1;
        pat.x = 560.0; pat.y = -20.0; pat.z = 200.0; pat.vx = pat.vy = pat.vz = 0.0;
        pat.maxHullHP = 70.0; pat.hullHP = pat.maxHullHP; pat.maxShield = 20.0; pat.shield = 20.0;
        LocalCraft& pir = s.craft[1];                  // РОЗЫСКНАЯ ЖЕРТВА — её и перевыведет pursuitQuarry
        pir.kind = CK_PIRATE; pir.label = "PIRATE"; pir.faction = -1; pir.r = 230; pir.g = 90; pir.b = 80;
        pir.hostile = true; pir.wanted = true; pir.wantedBounty = 650.0;
        pir.aiState = 1; pir.errand = 0; pir.errandBody = -1;
        pir.x = 650.0; pir.y = 60.0; pir.z = 165.0; pir.vx = pir.vy = pir.vz = 0.0;   // ~125 LU от патруля (<1400)
        pir.maxHullHP = 45.0; pir.hullHP = pir.maxHullHP * 0.7;
        s.px = 470.0; s.py = -150.0; s.pz = 250.0; s.pvx = s.pvy = s.pvz = 0.0;       // камера видит обоих
        localSetForward(s, 605.0 - s.px, 20.0 - s.py, 182.0 - s.pz);                  // нос на середину пары
        s.targetCraft = 0;                             // цель — патруль -> панель: ESCORT + «IN PURSUIT PIRATE»
        s.fxClock = 0.30;                              // фаза золотого пульса/марша штрихов
        std::printf("  [law-pursuit] defending patrol hunts wanted pirate -> gold intercept vector + radar link + IN PURSUIT\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_law_pursuit.bmp");
    }

    // (21) НАБАТ СТАИ (§5.13.31): draw-only спутник §5.13.30. Патруль-ОХОТНИК (defending, §5.13.28)
    //      прижал РОЗЫСКНОГО изгоя вплотную (близкий бой, золотой IN PURSUIT §5.13.29), а поодаль
    //      ПРОСТАИВАЮЩИЙ пират (>750 LU от патруля => сам цели не нашёл, но <1400 => «слышит» закон)
    //      срывается таранить патруль в защиту изгоя (§5.13.30). Рендер перевыводит набат из живых
    //      полей (rallyTarget: тот же idle-гейт + скан, что и sim) => горячо-оранжевый вектор тарана
    //      pack→patrol в 3D, оранжевая связка на радаре, строка «UNDER PACK X1» РЯДОМ с золотым
    //      «IN PURSUIT OUTLAW» в панели патруля — весь закон-цикл в одном кадре. Ноль полей/RNG/шага.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 3) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(3);                             // патруль-охотник + изгой + пират-набат
        LocalCraft& pat = s.craft[0];                  // ПАТРУЛЬ (defending) — цель панели, жертва тарана
        pat.kind = CK_PATROL; pat.label = "PATROL"; pat.faction = 1; pat.r = 110; pat.g = 215; pat.b = 170;
        pat.hostile = false; pat.defending = true; pat.aiState = 1; pat.errand = 0; pat.errandBody = -1;
        pat.x = 900.0; pat.y = 0.0; pat.z = 150.0; pat.vx = pat.vy = pat.vz = 0.0;
        pat.maxHullHP = 70.0; pat.hullHP = pat.maxHullHP; pat.maxShield = 20.0; pat.shield = 20.0;
        LocalCraft& out = s.craft[1];                  // ИЗГОЙ в розыске — вплотную к патрулю (близкий бой)
        out.kind = CK_PIRATE; out.label = "OUTLAW"; out.faction = -1; out.r = 230; out.g = 90; out.b = 80;
        out.hostile = true; out.wanted = true; out.wantedBounty = 800.0;
        out.aiState = 1; out.errand = 0; out.errandBody = -1;
        out.x = 900.0; out.y = 210.0; out.z = 150.0; out.vx = out.vy = out.vz = 0.0;    // 210 LU (<750 => idle-гейт изгоя ЗАКРЫТ => без оранжа)
        out.maxHullHP = 45.0; out.hullHP = out.maxHullHP * 0.65;
        LocalCraft& pak = s.craft[2];                  // ПРОСТАИВАЮЩИЙ пират — набат: таранит патруль
        pak.kind = CK_PIRATE; pak.label = "RAIDER"; pak.faction = -1; pak.r = 230; pak.g = 110; pak.b = 70;
        pak.hostile = true; pak.wanted = false; pak.aiState = 1; pak.errand = 0; pak.errandBody = -1;
        pak.x = 900.0; pak.y = -790.0; pak.z = 150.0; pak.vx = pak.vy = pak.vz = 0.0;   // 790 LU от патруля: >750 (idle) & <1400 (набат)
        pak.maxHullHP = 45.0; pak.hullHP = pak.maxHullHP;                                // полный корпус => не бегство (aiState 1, не 2)
        s.px = 90.0; s.py = -360.0; s.pz = 270.0; s.pvx = s.pvy = s.pvz = 0.0;           // 925 LU от набата (>750: гейт игрока открыт), 459 от звезды
        localSetForward(s, 900.0 - s.px, -300.0 - s.py, 150.0 - s.pz);                   // нос между патрулём и набатом
        s.targetCraft = 0;                             // цель — патруль -> панель: ESCORT + IN PURSUIT + UNDER PACK
        s.fxClock = 0.30;                              // фаза оранжевого пульса/марша
        std::printf("  [pack-rally] idle raider charges the hunting patrol to defend the outlaw -> orange ram vector + radar link + UNDER PACK\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_pack_rally.bmp");
    }

    // (22) ПОДМОГА ЗАКОНА (§5.13.33): draw-only спутник §5.13.32. Патруль-ОХОТНИК H (defending §5.13.28)
    //      прижал РОЗЫСКНОГО изгоя (золотой IN PURSUIT §5.13.29); рядом ПРОСТАИВАЮЩИЙ пират сорвался
    //      таранить H (оранжевый набат §5.13.31), а СВОБОДНЫЙ патруль R (его wanted-скан пуст ⇒ сам не
    //      охотник) идёт на выручку, целясь в увязшего пирата (§5.13.32). Рендер перевыводит подмогу из
    //      живых полей (backupTarget) ⇒ сине-белый вектор выручки R→pack в 3D, синяя связка на радаре и
    //      строка «BACKUP INBOUND X1» ПОД «UNDER PACK X1»/«IN PURSUIT» в панели H — весь закон-цикл
    //      (гон → набат → подмога) в одном кадре. Ноль полей/RNG/шага.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 4) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(4);                             // охотник + изгой + пират-набат + свободная подмога
        LocalCraft& hun = s.craft[0];                  // H: ПАТРУЛЬ-ОХОТНИК (defending) — цель панели, жертва набата
        hun.kind = CK_PATROL; hun.label = "PATROL"; hun.faction = 1; hun.r = 110; hun.g = 215; hun.b = 170;
        hun.hostile = false; hun.defending = true; hun.underAttack = false; hun.wanted = false;
        hun.aiState = 1; hun.errand = 0; hun.errandBody = -1;
        hun.x = 8000.0; hun.y = 0.0; hun.z = 150.0; hun.vx = hun.vy = hun.vz = 0.0;      // далеко от звезды => без ореола
        hun.maxHullHP = 70.0; hun.hullHP = hun.maxHullHP; hun.maxShield = 20.0; hun.shield = 20.0;
        LocalCraft& out = s.craft[1];                  // ИЗГОЙ в розыске — вплотную к H (делает H охотником, §5.13.28/29)
        out.kind = CK_PIRATE; out.label = "OUTLAW"; out.faction = -1; out.r = 230; out.g = 90; out.b = 80;
        out.hostile = true; out.wanted = true; out.wantedBounty = 800.0;
        out.aiState = 1; out.errand = 0; out.errandBody = -1;
        out.x = 8000.0; out.y = 260.0; out.z = 150.0; out.vx = out.vy = out.vz = 0.0;    // 260 LU от H (<750 => idle-гейт изгоя ЗАКРЫТ => без оранжа; <1400 => H охотник)
        out.maxHullHP = 45.0; out.hullHP = out.maxHullHP * 0.65;
        LocalCraft& pak = s.craft[2];                  // ПРОСТАИВАЮЩИЙ пират — набат: таранит H (§5.13.31)
        pak.kind = CK_PIRATE; pak.label = "RAIDER"; pak.faction = -1; pak.r = 230; pak.g = 110; pak.b = 70;
        pak.hostile = true; pak.wanted = false; pak.aiState = 1; pak.errand = 0; pak.errandBody = -1;
        pak.x = 8000.0; pak.y = -820.0; pak.z = 150.0; pak.vx = pak.vy = pak.vz = 0.0;   // 820 LU от H: >750 (idle) & <1400 (набат)
        pak.maxHullHP = 45.0; pak.hullHP = pak.maxHullHP;                                // полный корпус => не бегство (aiState 1)
        LocalCraft& bak = s.craft[3];                  // R: СВОБОДНЫЙ патруль — подмога, идёт на увязшего пирата (§5.13.32)
        bak.kind = CK_PATROL; bak.label = "PATROL"; bak.faction = 1; bak.r = 110; bak.g = 215; bak.b = 170;
        bak.hostile = false; bak.defending = true; bak.underAttack = false; bak.wanted = false;
        bak.aiState = 1; bak.errand = 0; bak.errandBody = -1;
        bak.x = 6700.0; bak.y = -820.0; bak.z = 150.0; bak.vx = bak.vy = bak.vz = 0.0;   // pack в 1300<1400 (цель подмоги); изгой в 1690>1400 => R сам НЕ охотник
        bak.maxHullHP = 70.0; bak.hullHP = bak.maxHullHP; bak.maxShield = 20.0; bak.shield = 20.0;
        s.px = 5200.0; s.py = -345.0; s.pz = 320.0; s.pvx = s.pvy = s.pvz = 0.0;         // сверху-сбоку, звезда позади камеры (нет окклюзии/ореола)
        localSetForward(s, 7675.0 - s.px, -345.0 - s.py, 150.0 - s.pz);                  // нос в центр тяжести четвёрки
        s.targetCraft = 0;                             // цель — H => панель: ESCORT + IN PURSUIT + UNDER PACK + BACKUP INBOUND
        s.fxClock = 0.30;                              // фаза: золотой/оранжевый/синий пульсы все яркие
        std::printf("  [law-backup] free patrol rushes to reinforce the swarmed hunter -> blue reinforcement vector + radar link + BACKUP INBOUND\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_law_backup.bmp");
    }

    // (23) ОБЛАВА (§5.13.35): draw-only спутник §5.13.34. defending-патруль (§5.13.28) преследует
    //      РОЗЫСКНОГО пирата с МАКСИМАЛЬНОЙ наградой (1500 CR => heat=1). Золотой вектор погони
    //      §5.13.29 КАЛИТСЯ ДОБЕЛА и штрихи бегут в ×1.5 быстрее (зеркало прибавки скорости патруля
    //      §5.13.34), панель патруля показывает бело-золотые «IN PURSUIT PIRATE» + «MANHUNT 1500 CR».
    //      Пара к shot_law_pursuit (там 650 CR => heat≈0.32, спокойное золото): рядом читается жар
    //      облавы. Геометрия та же, что law-pursuit — меняется ТОЛЬКО награда. Ноль полей/RNG/шага sim.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 2) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(2);                             // ровно два борта: патруль + его розыскная жертва
        LocalCraft& pat = s.craft[0];                  // ПАТРУЛЬ, ведущий охоту (defending)
        pat.kind = CK_PATROL; pat.label = "PATROL"; pat.faction = 1; pat.r = 110; pat.g = 215; pat.b = 170;
        pat.hostile = false; pat.defending = true; pat.aiState = 1; pat.errand = 0; pat.errandBody = -1;
        pat.x = 560.0; pat.y = -20.0; pat.z = 200.0; pat.vx = pat.vy = pat.vz = 0.0;
        pat.maxHullHP = 70.0; pat.hullHP = pat.maxHullHP; pat.maxShield = 20.0; pat.shield = 20.0;
        LocalCraft& pir = s.craft[1];                  // РОЗЫСКНАЯ ЖЕРТВА — МАКС. награда => heat=1 (бело-калёный)
        pir.kind = CK_PIRATE; pir.label = "PIRATE"; pir.faction = -1; pir.r = 230; pir.g = 90; pir.b = 80;
        pir.hostile = true; pir.wanted = true; pir.wantedBounty = 1500.0;   // потолок §5.13.26 => heat=1
        pir.aiState = 1; pir.errand = 0; pir.errandBody = -1;
        pir.x = 650.0; pir.y = 60.0; pir.z = 165.0; pir.vx = pir.vy = pir.vz = 0.0;   // ~125 LU от патруля (<1400)
        pir.maxHullHP = 45.0; pir.hullHP = pir.maxHullHP * 0.7;
        s.px = 470.0; s.py = -150.0; s.pz = 250.0; s.pvx = s.pvy = s.pvz = 0.0;       // камера видит обоих
        localSetForward(s, 605.0 - s.px, 20.0 - s.py, 182.0 - s.pz);                  // нос на середину пары
        s.targetCraft = 0;                             // цель — патруль -> панель: ESCORT + IN PURSUIT + MANHUNT
        s.fxClock = 0.30;                              // фаза золотого пульса/марша штрихов
        std::printf("  [manhunt] defending patrol hunts MAX-bounty (1500 CR) pirate -> white-hot pursuit vector + MANHUNT panel\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_manhunt.bmp");
    }

    // (24) СТОЯНИЕ С ФРАКЦИЕЙ (§5.13.37): draw-only итог репутации — прямой ПОКАЗ последствия §5.13.36.
    //      Патруль дружественной фракции; со-оп-добивы розыскных изгоев (§5.13.36) и спасения конвоев
    //      (§5.13.11) подняли стояние игрока с этой фракцией до ALLY. Панель цели теперь несёт строку
    //      «STANDING <WORD> <±N>», перевыведенную НА ЛЁТУ из game.factionRelation(playerFaction, t.faction)
    //      тем же классификатором порогов, что и макрослой (faction.cpp) — накопленный итог закон-цикла
    //      читается прямо из кокпита. Ноль полей/RNG/шага sim; пираты бесфракционны (faction=-1) => им
    //      строка не показывается, combat-фильтр §0.2-G пройден тривиально (только чтение репутации).
    //      (§5.13.39: тем же стоянием теперь красится и РАМКА цели в мире, и лид-пип упреждения —
    //      здесь оба ярко-зелёные, тир ALLY; пара — красный прицел в shot_standing_reticle ниже.)
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 1) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(1);                             // один борт: дружественный патруль в фокусе панели
        const int stFac = 0;                           // faction 0 всегда валидна и != playerFaction (= число NPC-фракций)
        game.setFactionRelation(game.playerFaction, stFac, 96);   // ALLY (>=80) => ярко-зелёная строка «STANDING ALLY +96»
        LocalCraft& pat = s.craft[0];                  // ПАТРУЛЬ дружественной фракции — цель панели
        pat.kind = CK_PATROL; pat.label = "PATROL"; pat.faction = stFac; pat.r = 110; pat.g = 215; pat.b = 170;
        pat.hostile = false; pat.defending = true; pat.aiState = 1; pat.errand = 0; pat.errandBody = -1;
        pat.x = 8000.0; pat.y = 0.0; pat.z = 150.0; pat.vx = pat.vy = pat.vz = 0.0;   // далеко от звезды => без ореола/окклюзии
        pat.maxHullHP = 70.0; pat.hullHP = pat.maxHullHP; pat.maxShield = 20.0; pat.shield = 20.0;
        s.px = 7080.0; s.py = -140.0; s.pz = 250.0; s.pvx = s.pvy = s.pvz = 0.0;      // ~935 LU: патруль крупно, панель читаема
        localSetForward(s, 8000.0 - s.px, 0.0 - s.py, 150.0 - s.pz);                  // нос на патруль
        s.targetCraft = 0;                             // цель — патруль => панель: ESCORT-рамка/чип + STANDING ALLY +96
        s.fxClock = 0.30;                              // фаза зелёного пульса рамки эскорта
        std::printf("  [law-standing] friendly patrol targeted -> panel carries STANDING ALLY +96 (faction rep readout §5.13.37)\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_law_standing.bmp");
    }

    // (25) СТОЯНИЕ НА ПРИЦЕЛЕ (§5.13.39): draw-only углубление §5.13.37 — теперь стояние красит САМ
    //      ПРИЦЕЛ (рамку цели в мире + лид-пип упреждения), а не только строку в панели. Цель —
    //      торговец ВРАЖДЕБНОЙ фракции (rep <= -96 => тир Enemy), НО hostile=false: единственная
    //      причина красного прицела здесь — СТОЯНИЕ, не боевой флаг (наглядно, что цвет ведёт
    //      репутация). Рамка и пип красные (Enemy); пара к shot_law_standing (там ALLY => зелёные).
    //      Тот же классификатор порогов faction.cpp. Ноль полей/RNG/шага sim; пиратам (faction<0)
    //      прицел неизменен => combat-фильтр §0.2-G пройден тривиально (только чтение репутации).
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 1) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(1);                             // один борт: враждебный торговец в фокусе прицела
        const int stFac = 0;                           // валидная NPC-фракция != playerFaction
        game.setFactionRelation(game.playerFaction, stFac, -100);  // ENEMY (<=-96) => красные рамка/пип
        LocalCraft& en = s.craft[0];                   // ТОРГОВЕЦ вражеской фракции — цель прицела
        en.kind = CK_TRADER; en.label = "TRADER"; en.faction = stFac; en.r = 120; en.g = 170; en.b = 220;
        en.hostile = false;                            // НЕ боевой флаг: красный ведёт СТОЯНИЕ, не hostile
        en.defending = false; en.underAttack = false; en.wanted = false;
        en.aiState = 1; en.errand = 0; en.errandBody = -1;
        en.x = 8000.0; en.y = 0.0; en.z = 150.0; en.vx = en.vy = en.vz = 0.0;   // далеко от звезды => без ореола
        en.maxHullHP = 60.0; en.hullHP = en.maxHullHP; en.maxShield = 25.0; en.shield = 25.0;
        s.px = 7080.0; s.py = -140.0; s.pz = 250.0; s.pvx = s.pvy = s.pvz = 0.0; // ~935 LU: цель крупно, прицел читаем
        localSetForward(s, 8000.0 - s.px, 0.0 - s.py, 150.0 - s.pz);            // нос на цель => лид-пип у цели
        s.targetCraft = 0; s.lockTarget = 0;           // цель + жёсткий захват => красная рамка (двойная) + красный пип
        s.fxClock = 0.30;
        std::printf("  [standing-reticle] hostile-faction (ENEMY) target -> target box + lead pip tint RED by standing, not the hostile flag (§5.13.39)\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_standing_reticle.bmp");
    }

    // (26) МЕСТЬ ФРАКЦИИ НА ВЕКТОРЕ (§5.13.41): draw-only углубление §5.13.40 — тот же золотой вектор
    //      перехвата (§5.13.29, defending-патруль → его розыскная жертва) теперь ЗАРАНЕЕ красится по
    //      стоянию игрока с ФРАКЦИЕЙ патруля-охотника тем же классификатором, что панель STANDING
    //      (§5.13.37) и прицел (§5.13.39). Фракция патруля ВРАЖДЕБНА игроку (Enemy, rep=-100) => вектор
    //      И связка на радаре КРАСНЫЕ: ещё до выстрела видно, что co-op-добивка тут СТОИЛА БЫ репутации
    //      (§5.13.40 отдал бы −rep). Геометрия/награда (650 CR) = shot_law_pursuit — меняется ТОЛЬКО
    //      стояние, поэтому цвет ведёт репутация, не боевой флаг. Ноль полей/RNG/шага sim; combat-фильтр
    //      §0.2-G пройден тривиально (только чтение репутации). Пара — зелёный shot_gratitude_vector ниже.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 2) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(2);                             // ровно два борта: патруль-охотник + его розыскная жертва
        const int patFac = 1;                          // валидная NPC-фракция != playerFaction (как патрули §5.13.29)
        game.setFactionRelation(game.playerFaction, patFac, -100);   // ENEMY (<=-96) => красный вектор/связка
        LocalCraft& pat = s.craft[0];                  // ПАТРУЛЬ, ведущий охоту (defending)
        pat.kind = CK_PATROL; pat.label = "PATROL"; pat.faction = patFac; pat.r = 110; pat.g = 215; pat.b = 170;
        pat.hostile = false; pat.defending = true; pat.aiState = 1; pat.errand = 0; pat.errandBody = -1;
        pat.x = 760.0; pat.y = -140.0; pat.z = 195.0; pat.vx = pat.vy = pat.vz = 0.0;
        pat.maxHullHP = 70.0; pat.hullHP = pat.maxHullHP; pat.maxShield = 20.0; pat.shield = 20.0;
        LocalCraft& pir = s.craft[1];                  // РОЗЫСКНАЯ ЖЕРТВА — награда 650 CR (как law-pursuit)
        pir.kind = CK_PIRATE; pir.label = "PIRATE"; pir.faction = -1; pir.r = 230; pir.g = 90; pir.b = 80;
        pir.hostile = true; pir.wanted = true; pir.wantedBounty = 650.0;
        pir.aiState = 1; pir.errand = 0; pir.errandBody = -1;
        pir.x = 760.0; pir.y = 160.0; pir.z = 205.0; pir.vx = pir.vy = pir.vz = 0.0;   // 300 LU от патруля вдоль +Y (<1400 => pursuitQuarry ловит)
        pir.maxHullHP = 45.0; pir.hullHP = pir.maxHullHP * 0.7;
        s.px = 250.0; s.py = 10.0; s.pz = 240.0; s.pvx = s.pvy = s.pvz = 0.0;          // борт-обзор: пара симметрична, вектор тянется поперёк экрана (звезда позади камеры)
        localSetForward(s, 760.0 - s.px, 10.0 - s.py, 200.0 - s.pz);                  // нос на середину пары
        s.targetCraft = 0;                             // цель — патруль => панель STANDING ENEMY + рамка/пип §5.13.39 в тон вектору
        s.fxClock = 0.30;                              // фаза пульса/марша штрихов
        std::printf("  [reprisal-vector] hunting patrol's faction is ENEMY -> pursuit vector + radar link RED (co-op kill would COST rep §5.13.40)\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_reprisal_vector.bmp");
    }

    // (27) БЛАГОДАРНОСТЬ ФРАКЦИИ НА ВЕКТОРЕ (§5.13.41): пара к shot_reprisal_vector. ТА ЖЕ геометрия и
    //      награда дичи (650 CR), но фракция патруля-охотника СОЮЗНА игроку (Ally, rep=+96) => тот же
    //      вектор перехвата (§5.13.29) и связка на радаре ЗЕЛЁНЫЕ: co-op-добивка тут ПРИНЕСЛА БЫ
    //      репутацию (§5.13.40 отдал бы +rep). Рядом с красным собратом наглядно, что §5.13.40 меняет не
    //      величину, а ЗНАК — и знак читается ещё до выстрела, прямо на приглашающей геометрии. Нейтраль/
    //      бесфракц. патруль дал бы прежнее золото (shot_law_pursuit) => нулевой регресс. Ноль полей/RNG/шага.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 2) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(2);                             // ровно два борта: патруль-охотник + его розыскная жертва
        const int patFac = 1;                          // та же фракция, что в reprisal-варианте
        game.setFactionRelation(game.playerFaction, patFac, 96);     // ALLY (>=80) => зелёный вектор/связка
        LocalCraft& pat = s.craft[0];                  // ПАТРУЛЬ, ведущий охоту (defending)
        pat.kind = CK_PATROL; pat.label = "PATROL"; pat.faction = patFac; pat.r = 110; pat.g = 215; pat.b = 170;
        pat.hostile = false; pat.defending = true; pat.aiState = 1; pat.errand = 0; pat.errandBody = -1;
        pat.x = 760.0; pat.y = -140.0; pat.z = 195.0; pat.vx = pat.vy = pat.vz = 0.0;
        pat.maxHullHP = 70.0; pat.hullHP = pat.maxHullHP; pat.maxShield = 20.0; pat.shield = 20.0;
        LocalCraft& pir = s.craft[1];                  // РОЗЫСКНАЯ ЖЕРТВА — та же награда 650 CR
        pir.kind = CK_PIRATE; pir.label = "PIRATE"; pir.faction = -1; pir.r = 230; pir.g = 90; pir.b = 80;
        pir.hostile = true; pir.wanted = true; pir.wantedBounty = 650.0;
        pir.aiState = 1; pir.errand = 0; pir.errandBody = -1;
        pir.x = 760.0; pir.y = 160.0; pir.z = 205.0; pir.vx = pir.vy = pir.vz = 0.0;   // 300 LU от патруля вдоль +Y (та же геометрия, что reprisal-вариант)
        pir.maxHullHP = 45.0; pir.hullHP = pir.maxHullHP * 0.7;
        s.px = 250.0; s.py = 10.0; s.pz = 240.0; s.pvx = s.pvy = s.pvz = 0.0;          // борт-обзор: пара симметрична, вектор тянется поперёк экрана (звезда позади камеры)
        localSetForward(s, 760.0 - s.px, 10.0 - s.py, 200.0 - s.pz);                  // нос на середину пары
        s.targetCraft = 0;                             // цель — патруль => панель STANDING ALLY + рамка/пип §5.13.39 в тон вектору
        s.fxClock = 0.30;                              // фаза пульса/марша штрихов
        std::printf("  [gratitude-vector] hunting patrol's faction is ALLY -> pursuit vector + radar link GREEN (co-op kill would EARN rep §5.13.40)\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_gratitude_vector.bmp");
        game.setFactionRelation(game.playerFaction, patFac, 0);      // восстановить нейтраль (гигиена для будущих сцен)
    }

    // (28) ОХОТА ПО СТОЯНИЮ (§5.13.43): draw-only спутник §5.13.42 — кокпит-подсказка «HUNTING YOU».
    //      Патруль фракции, с которой игрок стоит Enemy (rep=-100), взял ИГРОКА целью БЕЗ провокации
    //      (hostile=false): в симуляции это выражено полем aiTarget==LOCAL_TARGET_PLAYER при aiState==1
    //      (localsim.cpp §5.13.43; в реальной игре так бывает ТОЛЬКО через override §5.13.42 по стоянию).
    //      Рендер рисует над бортом багрово-красную подпись «HUNTING YOU» + быстрый пульс (темп 7.4) цветом
    //      стояния, плюс краевую стрелку, если охотник уходит за кадр. Пара — shot_hunted_none ниже, где
    //      тот же патруль при том же Enemy-стоянии охотится на ПИРАТА (aiTarget=индекс), и подсказка ГАСНЕТ:
    //      видно, что триггер — именно «патруль идёт на ТЕБЯ», а не просто «враждебная фракция рядом».
    //      Ноль полей/RNG/шага sim (aiTarget заполнен зеркально симуляции; combat-фильтр §0.2-G — только чтение репутации).
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 1) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(1);                             // один борт: патруль-охотник в фокусе
        const int stFac = 0;                           // валидная NPC-фракция != playerFaction
        game.setFactionRelation(game.playerFaction, stFac, -100);  // ENEMY (<=-96) => багровая подсказка/пульс
        LocalCraft& pat = s.craft[0];                  // ПАТРУЛЬ вражеской фракции, идёт на ИГРОКА по стоянию
        pat.kind = CK_PATROL; pat.label = "PATROL"; pat.faction = stFac; pat.r = 110; pat.g = 215; pat.b = 170;
        pat.hostile = false;                           // НЕ спровоцирован: подсказку зажигает СТОЯНИЕ, не боевой флаг
        pat.defending = false; pat.underAttack = false; pat.wanted = false;
        pat.aiState = 1; pat.aiTarget = LOCAL_TARGET_PLAYER;   // взял игрока целью (в игре — только override §5.13.42)
        pat.errand = 0; pat.errandBody = -1;
        pat.x = 8000.0; pat.y = 0.0; pat.z = 150.0; pat.vx = pat.vy = pat.vz = 0.0;   // далеко от звезды => без ореола
        pat.maxHullHP = 70.0; pat.hullHP = pat.maxHullHP; pat.maxShield = 20.0; pat.shield = 20.0;
        s.px = 7080.0; s.py = -140.0; s.pz = 250.0; s.pvx = s.pvy = s.pvz = 0.0;      // ~935 LU: патруль крупно, подпись читаема
        localSetForward(s, 8000.0 - s.px, 0.0 - s.py, 150.0 - s.pz);                  // нос на патруль
        s.targetCraft = 0;                             // цель — патруль => панель STANDING ENEMY для контекста
        s.fxClock = 0.30;                              // фаза быстрого пульса подсказки
        std::printf("  [hunted-standing] enemy-faction patrol targets the PLAYER unprovoked -> red HUNTING YOU cue + pulse (§5.13.43)\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_hunted_standing.bmp");
    }

    // (29) АНТИ-ЛОЖНЯК ПОДСКАЗКИ (§5.13.43): пара к shot_hunted_standing. ТОТ ЖЕ патруль при ТОМ ЖЕ Enemy-
    //      стоянии (rep=-100), но его цель — ПИРАТ (aiTarget=индекс борта, не LOCAL_TARGET_PLAYER): предикат
    //      подсказки не выполняется => «HUNTING YOU» НЕ рисуется, хотя фракция патруля враждебна и он в бою
    //      (aiState=1). Наглядно, что подсказка ведётся полем aiTarget (§5.13.43), а не просто CK_PATROL +
    //      hostile-стояние — иначе патруль, гоняющий пирата, ложно кричал бы «охочусь на тебя». Панель по-
    //      прежнему честно показывает STANDING ENEMY (тир не изменился). Ноль полей/RNG/шага sim.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 2) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(2);                             // патруль + пират (его фактическая цель)
        const int stFac = 0;
        game.setFactionRelation(game.playerFaction, stFac, -100);  // тот же ENEMY, что в паре
        LocalCraft& pat = s.craft[0];                  // ПАТРУЛЬ вражеской фракции — но охотится на ПИРАТА
        pat.kind = CK_PATROL; pat.label = "PATROL"; pat.faction = stFac; pat.r = 110; pat.g = 215; pat.b = 170;
        pat.hostile = false; pat.defending = false; pat.underAttack = false; pat.wanted = false;
        pat.aiState = 1; pat.aiTarget = 1;             // цель — борт №1 (пират), НЕ игрок => подсказка гаснет
        pat.errand = 0; pat.errandBody = -1;
        pat.x = 8000.0; pat.y = 0.0; pat.z = 150.0; pat.vx = pat.vy = pat.vz = 0.0;
        pat.maxHullHP = 70.0; pat.hullHP = pat.maxHullHP; pat.maxShield = 20.0; pat.shield = 20.0;
        LocalCraft& pir = s.craft[1];                  // ПИРАТ — фактическая цель патруля (aiTarget=1)
        pir.kind = CK_PIRATE; pir.label = "PIRATE"; pir.faction = -1; pir.r = 230; pir.g = 90; pir.b = 80;
        pir.hostile = true; pir.wanted = false; pir.aiState = 1; pir.errand = 0; pir.errandBody = -1;
        pir.x = 8120.0; pir.y = 80.0; pir.z = 150.0; pir.vx = pir.vy = pir.vz = 0.0;   // рядом с патрулём
        pir.maxHullHP = 45.0; pir.hullHP = pir.maxHullHP;
        s.px = 7080.0; s.py = -140.0; s.pz = 250.0; s.pvx = s.pvy = s.pvz = 0.0;       // та же камера, что в паре
        localSetForward(s, 8000.0 - s.px, 0.0 - s.py, 150.0 - s.pz);                   // нос на патруль
        s.targetCraft = 0;                             // цель — патруль => панель STANDING ENEMY (тот же контекст)
        s.fxClock = 0.30;
        std::printf("  [hunted-none] enemy-faction patrol hunts a PIRATE (aiTarget!=player) -> NO HUNTING YOU cue (false-positive guard §5.13.43)\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_hunted_none.bmp");
        game.setFactionRelation(game.playerFaction, stFac, 0);     // восстановить нейтраль (гигиена для будущих сцен)
    }

    // (30) КРАЕВОЙ ОХОТНИК (§5.13.43): покрытие OFF-SCREEN ветки cue — краевого маркера `nearHunt` (адверсарное
    //      ревью §5.13.43, item #3: on-screen пара #28/#29 не задевала off-screen путь `localdraw.cpp` nearHunt-
    //      селектор/draw). ТОТ ЖЕ Enemy-патруль-охотник (`aiTarget=LOCAL_TARGET_PLAYER`, `hostile=false`), но нос
    //      камеры ОТВЁРНУТ от борта ⇒ патруль ЗА КАДРОМ ⇒ on-screen подпись `HUNTING YOU` не рисуется, зато
    //      `drawEdgeMarker` клампит точку на ободок экрана и рисует стрелку цветом стояния (Enemy — багровый).
    //      `nearHunt` DISJOINT со старым `nearHos` (тот требует `hostile`) ⇒ ноль двойного маркера. Ноль полей/RNG/шага sim.
    //      NB для ревьюера: БАГРОВАЯ стрелка на ПРАВОМ ободке = искомый `nearHunt`; зелёная слева = штатный указатель на
    //      ближайший off-screen рынок (`nearMkt`, `localdraw.cpp`) — ортогонален срезу, ложится из `buildLocalScene`.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 1) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(1);
        const int stFac = 0;
        game.setFactionRelation(game.playerFaction, stFac, -100);  // ENEMY — тот же тир, что #28
        LocalCraft& pat = s.craft[0];                  // тот же охотник по стоянию, что #28
        pat.kind = CK_PATROL; pat.label = "PATROL"; pat.faction = stFac; pat.r = 110; pat.g = 215; pat.b = 170;
        pat.hostile = false; pat.defending = false; pat.underAttack = false; pat.wanted = false;
        pat.aiState = 1; pat.aiTarget = LOCAL_TARGET_PLAYER;
        pat.errand = 0; pat.errandBody = -1;
        pat.x = 8000.0; pat.y = 0.0; pat.z = 150.0; pat.vx = pat.vy = pat.vz = 0.0;   // борт на +X от камеры
        pat.maxHullHP = 70.0; pat.hullHP = pat.maxHullHP; pat.maxShield = 20.0; pat.shield = 20.0;
        s.px = 7080.0; s.py = -140.0; s.pz = 250.0; s.pvx = s.pvy = s.pvz = 0.0;
        localSetForward(s, 0.4, 0.9, 0.0);             // нос ОТВёрнут от патруля (тот на +X) ⇒ борт уходит за правый край
        s.targetCraft = -1;                            // цель не задана: фокус на КРАЕВОЙ стрелке, а не на панели
        s.fxClock = 0.30;
        std::printf("  [hunted-edge] off-screen standing-hunter -> nearHunt edge arrow in standing colour (§5.13.43 off-screen path)\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_hunted_edge.bmp");
        game.setFactionRelation(game.playerFaction, stFac, 0);     // восстановить нейтраль (гигиена)
    }

    // (31) ЖАР ВЕНДЕТТЫ — ПОТОЛОК (§5.13.45): тот же охотник-по-стоянию, что #28, но реп на ДНЕ (−128) ⇒
    //      heat=1.0 (ПОБИТОВО == sim-скорость §5.13.44 vendettaSpeedMult) ⇒ подсказка «HUNTING YOU» калится к
    //      БЕЛО-ГОРЯЧЕМУ (vendettaHuntColor), пульс РЕЗЧЕ (7.4→11.1, ×1.5 как марш облавы §5.13.35) и база ярче
    //      (110→160). Пара — #32 (реп −48, heat=0): там cue РОВНО прежний §5.13.43. Ноль полей/RNG/шага sim
    //      (heat выведен из ЖИВОЙ репутации draw-side, приём D). Фаза fxClock — пик быстрого пульса (яркий кадр).
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 1) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(1);
        const int stFac = 0;
        game.setFactionRelation(game.playerFaction, stFac, -128);  // ДНО => heat=1.0 => бело-горячий/резкий cue
        LocalCraft& pat = s.craft[0];
        pat.kind = CK_PATROL; pat.label = "PATROL"; pat.faction = stFac; pat.r = 110; pat.g = 215; pat.b = 170;
        pat.hostile = false; pat.defending = false; pat.underAttack = false; pat.wanted = false;
        pat.aiState = 1; pat.aiTarget = LOCAL_TARGET_PLAYER;
        pat.errand = 0; pat.errandBody = -1;
        pat.x = 8000.0; pat.y = 0.0; pat.z = 150.0; pat.vx = pat.vy = pat.vz = 0.0;
        pat.maxHullHP = 70.0; pat.hullHP = pat.maxHullHP; pat.maxShield = 20.0; pat.shield = 20.0;
        s.px = 7080.0; s.py = -140.0; s.pz = 250.0; s.pvx = s.pvy = s.pvz = 0.0;
        localSetForward(s, 8000.0 - s.px, 0.0 - s.py, 150.0 - s.pz);
        s.targetCraft = 0;
        s.fxClock = 0.1415;                            // 11.1·t ≈ π/2 ⇒ пик быстрого пульса ⇒ яркий кадр
        std::printf("  [hunted-vendetta-hot] floor standing (rep -128, heat 1.0) -> white-hot HUNTING YOU, fastest pulse (§5.13.45)\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_hunted_vendetta_hot.bmp");
        game.setFactionRelation(game.playerFaction, stFac, 0);     // гигиена
    }

    // (32) ЖАР ВЕНДЕТТЫ — ПОРОГ / НУЛЕВОЙ РЕГРЕСС (§5.13.45): тот же охотник, реп на пороге Hostile (−48) ⇒
    //      heat=0.0 ⇒ int(0·k)==0, темп 7.4 ⇒ подсказка РОВНО как в §5.13.43: базовый оранж-красный тир Hostile,
    //      база 110. Пара к #31 — глазами видно, что при мелкой вражде cue прежний, а калится лишь с ГЛУБИНОЙ
    //      стояния (доказательство «нулевого визуального регресса» §5.13.45). В игре реп −48 уже включает §5.13.42.
    {
        LocalScene s; buildLocalScene(game, 0, s); s.active = true;
        while (s.craft.size() < 1) { LocalCraft c; s.craft.push_back(c); }
        s.craft.resize(1);
        const int stFac = 0;
        game.setFactionRelation(game.playerFaction, stFac, -48);   // ПОРОГ Hostile => heat=0 => cue §5.13.43 побитово
        LocalCraft& pat = s.craft[0];
        pat.kind = CK_PATROL; pat.label = "PATROL"; pat.faction = stFac; pat.r = 110; pat.g = 215; pat.b = 170;
        pat.hostile = false; pat.defending = false; pat.underAttack = false; pat.wanted = false;
        pat.aiState = 1; pat.aiTarget = LOCAL_TARGET_PLAYER;
        pat.errand = 0; pat.errandBody = -1;
        pat.x = 8000.0; pat.y = 0.0; pat.z = 150.0; pat.vx = pat.vy = pat.vz = 0.0;
        pat.maxHullHP = 70.0; pat.hullHP = pat.maxHullHP; pat.maxShield = 20.0; pat.shield = 20.0;
        s.px = 7080.0; s.py = -140.0; s.pz = 250.0; s.pvx = s.pvy = s.pvz = 0.0;
        localSetForward(s, 8000.0 - s.px, 0.0 - s.py, 150.0 - s.pz);
        s.targetCraft = 0;
        s.fxClock = 0.2123;                            // 7.4·t ≈ π/2 ⇒ пик прежнего пульса (та же фаза-пик, что #31)
        std::printf("  [hunted-vendetta-cold] threshold standing (rep -48, heat 0.0) -> unchanged §5.13.43 cue (zero-regression pin)\n");
        total += 1;
        ok += saveShot(r, surf, game, s, W, H, false, 0.60, 0.8, "shot_hunted_vendetta_cold.bmp");
        game.setFactionRelation(game.playerFaction, stFac, 0);     // гигиена
    }

    std::printf("SHOTS DONE: %d/%d saved ok\n", ok, total);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    SDL_Quit();
    return ok == total ? 0 : 1;
}
