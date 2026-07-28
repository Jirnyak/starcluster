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

    std::printf("SHOTS DONE: %d/%d saved ok\n", ok, total);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    SDL_Quit();
    return ok == total ? 0 : 1;
}
