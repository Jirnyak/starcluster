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

    std::printf("SHOTS DONE: %d/%d saved ok\n", ok, total);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    SDL_Quit();
    return ok == total ? 0 : 1;
}
