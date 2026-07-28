#include "local.h"
#include "game.h"
#include "cluster.h"
#include "camera.h"
#include "render2d.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace UI;

// ============================================================================
//  localdraw.cpp — рендер локальной сцены и HUD.
//
//  Камера/базис приходят из main.cpp (см. local.h::renderLocalScene):
//   • 3D-ПОЛЁТ (по умолчанию): view.perspective==true, basis построен
//     makeCameraFrameBasis из тело-относительного базиса корабля (chase-cam
//     позади/над носом). Проекция перспективная (делим на глубину); точки за
//     ближней плоскостью помечены pp.behind. Экранный радиус тела ~ R*focal/depth.
//   • КАРТА (переключатель C): view.perspective==false — верхний ортогональный
//     вид (как прежний top-down). Экранный радиус ~ R*scale.
//
//  Фон в 3D — LOD-скайбокс реальных звёзд кластера (projectDirectionWithBasis):
//  направление на звезду = normalize(star.pos - anchor) в координатах кластера;
//  трансляция пренебрежима, важна лишь ориентация камеры => при повороте/крене
//  корабля скайбокс поворачивается корректно. Якорь — текущая звезда, в глубоком
//  космосе — центроид кластера. В режиме карты фон — дешёвая россыпь тусклых точек.
//
//  Порядок рисования — художником: дальнее раньше ближнего.
//  Детерминизм: глобальный rng здесь не трогается (только хеши от индексов/окна).
// ============================================================================

namespace {

// Собрать SDL_Color без brace-narrowing.
SDL_Color rgba(int r, int g, int b, int a) {
    SDL_Color c;
    c.r = (Uint8)std::max(0, std::min(255, r));
    c.g = (Uint8)std::max(0, std::min(255, g));
    c.b = (Uint8)std::max(0, std::min(255, b));
    c.a = (Uint8)std::max(0, std::min(255, a));
    return c;
}

// Умножить цвет на глубинный фейд (r,g,b), альфа как есть.
SDL_Color fade(uint8_t r, uint8_t g, uint8_t b, uint8_t a, double f) {
    return rgba(int(r * f), int(g * f), int(b * f), a);
}

// Приблизительная ширина строки для центрирования (глиф 5 + 1 пробел = 6 * scale).
int textWidth(const std::string& s, int scale) {
    return int(s.size()) * 6 * scale;
}

// Экранный радиус мирового радиуса R по режиму: перспектива => R*focal/depth
// (depth = дистанция перед камерой, >near); орто => R*scale. Возвращает СЫРОЙ px
// (клэмпы — на стороне вызова, как раньше по типам объектов).
int radiusPx(double R, double depth, const View3D& view) {
    if (view.perspective) {
        if (depth <= view.nearPlane) return 0;
        double v = R * view.focal / depth;
        return v < 0.0 ? 0 : int(v);
    }
    return int(R * view.scale);
}

// Аналитическая ray-sphere окклюзия: отрезок глаз->точка перекрыт непрозрачным
// веществом сферы (центр C, радиус R)? Ничего не держим в памяти — только чистая
// геометрия: единичный луч от глаза, ближнее пересечение t0=-b-sqrt(b²−c); тело за
// звездой (t0 внутри отрезка) невидимо. Так гигантская звезда честно перекрывает
// планеты/корабли, не будучи «моделью» — лишь центр и радиус.
static bool segIntersectsSphere(double ex, double ey, double ez,
                                double px, double py, double pz,
                                double cx, double cy, double cz, double R) {
    const double dx = px - ex, dy = py - ey, dz = pz - ez;
    const double L2 = dx * dx + dy * dy + dz * dz;
    if (L2 < 1e-12) return false;
    const double L = std::sqrt(L2);
    const double idx = dx / L, idy = dy / L, idz = dz / L;   // единичный луч глаз->точка
    const double ox = ex - cx, oy = ey - cy, oz = ez - cz;   // глаз относительно центра
    const double b = ox * idx + oy * idy + oz * idz;
    const double c = ox * ox + oy * oy + oz * oz - R * R;
    if (c < 0.0) return true;                 // глаз ВНУТРИ сферы — всё за веществом
    const double disc = b * b - c;
    if (disc < 0.0) return false;             // луч мимо сферы
    const double t0 = -b - std::sqrt(disc);   // ближнее пересечение
    return t0 > 1e-4 && t0 < L;               // сфера между глазом и точкой => перекрыто
}

// Заливка треугольника через edge-функции по bounding box (не зависит от обхода).
void fillTriangle(SDL_Renderer* rd, int x0, int y0, int x1, int y1, int x2, int y2, SDL_Color c) {
    int minX = std::min(x0, std::min(x1, x2));
    int maxX = std::max(x0, std::max(x1, x2));
    int minY = std::min(y0, std::min(y1, y2));
    int maxY = std::max(y0, std::max(y1, y2));
    SDL_SetRenderDrawColor(rd, c.r, c.g, c.b, c.a);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            double d0 = double(x1 - x0) * (y - y0) - double(y1 - y0) * (x - x0);
            double d1 = double(x2 - x1) * (y - y1) - double(y2 - y1) * (x - x1);
            double d2 = double(x0 - x2) * (y - y2) - double(y0 - y2) * (x - x2);
            bool neg = (d0 < 0) || (d1 < 0) || (d2 < 0);
            bool pos = (d0 > 0) || (d1 > 0) || (d2 > 0);
            if (!(neg && pos)) SDL_RenderDrawPoint(rd, x, y);
        }
    }
}

// Стрелка-корабль. sa — угол в ЭКРАННЫХ координатах (радианы, +x вправо, +y вниз).
void headingTriangle(SDL_Renderer* rd, int cx, int cy, double sa, double len, SDL_Color c, bool fill) {
    int tx = cx + int(std::cos(sa) * len);
    int ty = cy + int(std::sin(sa) * len);
    int lx = cx + int(std::cos(sa + 2.5) * len * 0.8);
    int ly = cy + int(std::sin(sa + 2.5) * len * 0.8);
    int rx = cx + int(std::cos(sa - 2.5) * len * 0.8);
    int ry = cy + int(std::sin(sa - 2.5) * len * 0.8);
    if (fill) {
        fillTriangle(rd, tx, ty, lx, ly, rx, ry, c);
    } else {
        SDL_SetRenderDrawColor(rd, c.r, c.g, c.b, c.a);
        SDL_RenderDrawLine(rd, tx, ty, lx, ly);
        SDL_RenderDrawLine(rd, lx, ly, rx, ry);
        SDL_RenderDrawLine(rd, rx, ry, tx, ty);
    }
}

// Маркер у края экрана, указывающий на off-screen цель (wx,wy,wz). Учитывает
// перспективу: цель за камерой (pp.behind) — пеленг берётся из осей камеры.
void drawEdgeMarker(SDL_Renderer* rd, int cx, int cy, int winW, int winH,
                    double wx, double wy, double wz,
                    const View3D& view, const CameraBasis& basis, SDL_Color col) {
    ProjectedPoint p = projectPointWithBasis(wx, wy, wz, winW, winH, view, basis);
    double sdx, sdy;
    if (view.perspective && p.behind) {
        // Цель за камерой — экранная проекция невалидна; берём направление из осей
        // камеры (право/вверх). Экран Y вниз => sdy = -camY.
        sdx = p.camX; sdy = -p.camY;
        if (std::abs(sdx) < 1e-9 && std::abs(sdy) < 1e-9) sdy = 1.0; // строго позади — вниз
    } else {
        if (!p.behind && p.x >= 0 && p.x <= winW && p.y >= 0 && p.y <= winH) return; // на экране
        sdx = double(p.x - cx);
        sdy = double(p.y - cy);
        if (std::abs(sdx) < 1e-6 && std::abs(sdy) < 1e-6) return;
    }
    double margin = 28.0;
    double maxX = std::max(10.0, winW * 0.5 - margin);
    double maxY = std::max(10.0, winH * 0.5 - margin);
    double ax = std::abs(sdx), ay = std::abs(sdy);
    double t = (ax * maxY > ay * maxX) ? (maxX / std::max(1.0, ax)) : (maxY / std::max(1.0, ay));
    int ex = cx + int(sdx * t);
    int ey = cy + int(sdy * t);
    headingTriangle(rd, ex, ey, std::atan2(sdy, sdx), 9.0, col, true);
}

// Цвет радиоисточника по типу (общий для мировых маркеров и радара).
SDL_Color radioColor(int kind) {
    switch (kind) {
        case RS_CACHE:    return rgba(245, 191,  78, 255); // клад — янтарный
        case RS_DISTRESS: return rgba(238,  88,  82, 255); // SOS — красный
        case RS_ANOMALY:  return rgba(190, 110, 240, 255); // аномалия — пурпур
        case RS_DERELICT:
        default:          return rgba(150, 200, 210, 255); // остов — серо-циан
    }
}

// Цвет далёкой звезды скайбокса по спектральному классу/индексу (детерминированно).
SDL_Color skyStarColor(unsigned i, int stellarClass) {
    if (stellarClass == 1) return rgba(205, 224, 255, 255); // нейтронная/пульсар — голубовато-белая
    unsigned h = (i + 1u) * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    switch (h % 5u) {
        case 0:  return rgba(255, 214, 170, 255); // тёплая (K/M)
        case 1:  return rgba(255, 236, 205, 255); // жёлто-белая (G)
        case 2:  return rgba(235, 240, 248, 255); // белая (F)
        case 3:  return rgba(200, 216, 255, 255); // бело-голубая (A)
        default: return rgba(178, 200, 255, 255); // голубая (B)
    }
}

// ── Аналитическая звезда как ИЗОТРОПНАЯ СФЕРА (попиксельный ray-sphere «шейдер») ──
// Тело задано ТОЛЬКО центром (0,0,0) и радиусом R — больше ничего. Для каждого пикселя
// восстанавливаем мировой луч из глаза, аналитически пересекаем со сферой и красим из
// ГЕОМЕТРИИ: лимб-даркенинг mu=√(1−(dmin/R)²) + анимированная турбулентность (плазма);
// корона за лимбом запечена в тот же проход. Никаких порогов и «переключения режима»:
// смотришь на звезду — плазма, отворачиваешься — космос, влетаешь внутрь — вещество
// вокруг с направленной структурой (НЕ плоская заливка). Изотропно ПО ПОСТРОЕНИЮ:
// результат зависит только от R и |eye| относительно луча, одинаков со всех сторон.
//
// Рендер в персистентную стриминг-текстуру половинного разрешения (ARGB8888, BLEND):
// диск непрозрачен (alpha=255 → честно перекрывает скайбокс, согласуясь с occ-окклюзией),
// корона — полупрозрачное тёплое свечение. Далёкую звезду считаем только в bbox её
// проекции; вблизи/внутри — весь кадр. Чистый SDL2 (никакого GL) — детерминированные
// шоты продолжают работать на программном рендерере.
static void renderStarPlasma(SDL_Renderer* renderer, const LocalScene& scene,
                             const View3D& view, const CameraBasis& basis,
                             int winW, int winH) {
    const double R = scene.starRadius;
    if (R <= 0.0 || winW <= 0 || winH <= 0) return;
    const double ex = view.centerX, ey = view.centerY, ez = view.centerZ;
    const double D2 = ex * ex + ey * ey + ez * ez;
    const double R2 = R * R;
    const double focal = std::max(1.0, view.focal);

    // Персистентная стриминг-текстура (полуразрешение). Пересоздаём при смене рендерера/размера.
    static SDL_Texture*  tex   = nullptr;
    static SDL_Renderer* texRd = nullptr;
    static int texW = 0, texH = 0;
    const int bw = (winW + 1) / 2, bh = (winH + 1) / 2;
    if (!tex || texRd != renderer || texW != bw || texH != bh) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, bw, bh);
        texRd = renderer; texW = bw; texH = bh;
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        }
    }
    if (!tex) return;

    // Экранный bbox в координатах буфера (полуразрешение). Далеко (D>R и центр перед
    // камерой) — квадрат вокруг проекции центра радиусом screenR·coronaK; иначе весь кадр.
    const double coronaK = 1.70;   // чуть шире — простор для протуберанцев/плюмов короны
    int bx0 = 0, by0 = 0, bx1 = bw, by1 = bh;
    if (D2 > R2 * 1.05) {
        ProjectedPoint sp = projectPointWithBasis(0.0, 0.0, 0.0, winW, winH, view, basis);
        if (sp.behind) return;                        // центр за камерой и D>R → звезда не в кадре
        const double screenR = focal * R / std::sqrt(D2 - R2);
        const double margin  = screenR * coronaK + 4.0;
        bx0 = std::max(0,  (int)std::floor((sp.x - margin) * 0.5));
        by0 = std::max(0,  (int)std::floor((sp.y - margin) * 0.5));
        bx1 = std::min(bw, (int)std::ceil ((sp.x + margin) * 0.5) + 1);
        by1 = std::min(bh, (int)std::ceil ((sp.y + margin) * 0.5) + 1);
        if (bx0 >= bx1 || by0 >= by1) return;         // проекция целиком вне экрана
    }

    // Цвета ядра (горячее/светлее) и лимба (темнее/краснее) из спектрального цвета звезды.
    // Прокси температуры O→M из синевы цвета (гор. звезда — сине-белая, высокая B; холодная —
    // красно-оранжевая, низкая B). Усиливает спектральный контраст: горячие — сине-белый шар с
    // ярким синим лимбом, холодные — жёлто-ядро + глубоко-красный сильно затемнённый лимб.
    const double sR = (double)scene.starR, sG = (double)scene.starG, sB = (double)scene.starB;
    double hot01 = (sB - 150.0) / 105.0; if (hot01 < 0.0) hot01 = 0.0; else if (hot01 > 1.0) hot01 = 1.0;
    const double crR = std::min(255.0, sR + 10.0 + 22.0 * (1.0 - hot01)); // ядро: холод.→жёлто-белое
    const double crG = std::min(255.0, sG + 26.0 + 10.0 * (1.0 - hot01));
    const double crB = std::min(255.0, sB + 34.0 + 60.0 * hot01);         //        гор.→сине-белое
    const double lmR = sR * (0.90 - 0.05 * hot01);                        // лимб: холод.→глубокий
    const double lmG = sG * (0.40 + 0.22 * hot01);                        //  красно-оранж, гор.→
    const double lmB = sB * (0.18 + 0.40 * hot01);                        //  синее и ярче
    // Тон хромосферного ободка (аддитивно) и короны: холодная звезда — H-alpha красно-розовый,
    // горячая — сине-белый (иначе красный ободок диссонирует на сине-белом диске). Холодный
    // предел короны = ТОЧНО прежний оранж (lm·), горячий — синева → без регресса тёплых звёзд.
    const double chR = 120.0 - 40.0 * hot01, chG = 30.0 + 90.0 * hot01, chB = 42.0 + 158.0 * hot01;
    const double coR = lmR       * (1.0 - hot01) + 150.0 * hot01;
    const double coG = lmG * 0.9 * (1.0 - hot01) + 180.0 * hot01;
    const double coB = lmB * 0.8 * (1.0 - hot01) + 235.0 * hot01;
    const double tcl = scene.fxClock;

    void* vpix = nullptr; int pitch = 0;
    if (SDL_LockTexture(tex, nullptr, &vpix, &pitch) != 0) return;
    unsigned char* rowbase = (unsigned char*)vpix;

    const double invF  = 1.0 / focal;
    const double halfW = winW * 0.5, halfH = winH * 0.5;
    const double coronaR = R * coronaK, coronaR2 = coronaR * coronaR;

    for (int by = by0; by < by1; ++by) {
        Uint32* row = (Uint32*)(rowbase + (size_t)by * pitch);
        std::memset(row + bx0, 0, (size_t)(bx1 - bx0) * sizeof(Uint32)); // промахи = прозрачно
        const double fy  = (double)(by * 2) + 0.5;    // полноэкранный y центра полупикселя
        const double scy = -(fy - halfH) * invF;      // «вверх»-компонента луча камеры
        for (int bx = bx0; bx < bx1; ++bx) {
            const double fx  = (double)(bx * 2) + 0.5;
            const double scx = (fx - halfW) * invF;   // «вправо»-компонента
            // Мировой луч: dir = right·scx + up·scy + fwd (затем нормировка).
            double dx = basis.rX * scx + basis.uX * scy + basis.fX;
            double dy = basis.rY * scx + basis.uY * scy + basis.fY;
            double dz = basis.rZ * scx + basis.uZ * scy + basis.fZ;
            const double il = 1.0 / std::sqrt(dx * dx + dy * dy + dz * dz);
            dx *= il; dy *= il; dz *= il;
            const double bb0 = ex * dx + ey * dy + ez * dz;   // O·dir
            double dmin2 = D2 - bb0 * bb0; if (dmin2 < 0.0) dmin2 = 0.0;
            if (dmin2 <= R2) {
                const double sq = std::sqrt(R2 - dmin2);
                const double t1 = -bb0 + sq;                  // дальний корень
                if (t1 > 0.0) {                               // сфера хотя бы частично впереди
                    double mu2 = 1.0 - dmin2 / R2; if (mu2 < 0.0) mu2 = 0.0;
                    const double mu = std::sqrt(mu2);         // косинус к лимбу (лимб-даркенинг)
                    const double t0 = -bb0 - sq;
                    const double te = t0 > 0.0 ? t0 : 0.0;    // точка входа (внутри звезды → глаз)
                    const double nx = (ex + dx * te) / R;     // нормаль/позиция на поверхности
                    const double ny = (ey + dy * te) / R;
                    const double nz = (ez + dz * te) / R;
                    // Многооктавная турбулентность + доменный варп 2-го порядка → «кипящая»
                    // плазма (филаменты/гранулы), а не гладкий градиент. Частоты умеренные.
                    const double wrp  = 0.35 * std::sin(ny * 7.0 - tcl * 0.5);
                    const double wrp2 = 0.22 * std::sin((nx + nz) * 11.0 + wrp * 2.3 - tcl * 0.7); // варп варпа
                    const double n1  = std::sin(nx * 8.0 + wrp + tcl * 0.6);
                    const double n2  = std::sin((ny + nz) * 12.0 - wrp2 * 1.3 - tcl * 0.9);
                    const double n3  = std::sin((nx - nz) * 19.0 + n1 * 1.6 + wrp2 + tcl * 0.4);
                    const double n4  = std::sin((ny - nx) * 31.0 + n2 * 1.4 - tcl * 0.5); // мелкая октава
                    const double turb = 0.46 * n1 + 0.28 * n2 + 0.20 * n3 + 0.12 * n4;
                    double bright = (0.42 + 0.58 * mu) * (1.0 + 0.20 * turb);
                    // Пятна (сунспоты): низкочаст. поле, мягко темнит редкие «клетки» (умбра+
                    // полутень), без порога — концентрируем в мелкие ядра степенью ^4.
                    double sf = 0.5 + 0.5 * std::sin(nx * 3.0 + tcl * 0.05)
                                          * std::sin((ny + nz) * 2.6 - tcl * 0.04)
                                          * std::sin(nz * 3.4 + n1 * 0.5);
                    double spot = sf * sf; spot *= spot;      // ^4 — маленькие тёмные ядра, мягкий край
                    bright *= 1.0 - 0.50 * spot;
                    if (bright < 0.0) bright = 0.0;
                    const double m = mu2;                     // ярче к ядру
                    double rr = (lmR + (crR - lmR) * m) * bright;
                    double gg = (lmG + (crG - lmG) * m) * bright;
                    double bb = (lmB + (crB - lmB) * m) * bright;
                    // Хромосферный ободок: тонкий H-alpha (красно-розовый) слой у самого лимба
                    // (mu→0), излучение — аддитивно поверх фотосферы, с лёгкой неровностью.
                    double rim = 1.0 - mu; rim *= rim; rim *= rim;    // (1-mu)^4 — узкая кромка
                    rim *= 0.85 + 0.15 * n2;
                    rr += rim * chR; gg += rim * chG; bb += rim * chB;
                    const Uint32 ir = rr > 255.0 ? 255u : (rr < 0.0 ? 0u : (Uint32)rr);
                    const Uint32 ig = gg > 255.0 ? 255u : (gg < 0.0 ? 0u : (Uint32)gg);
                    const Uint32 ib = bb > 255.0 ? 255u : (bb < 0.0 ? 0u : (Uint32)bb);
                    row[bx] = 0xFF000000u | (ir << 16) | (ig << 8) | ib;
                    continue;
                }
            }
            // Корона + протуберанцы: луч мимо сферы, но проходит рядом и «вперёд» (bb0<0).
            if (bb0 < 0.0 && dmin2 < coronaR2) {
                const double dm = std::sqrt(dmin2);
                // Угловая позиция точки наибольшего сближения луча со звездой: P = O − d·(O·d).
                const double px = ex - dx * bb0, py = ey - dy * bb0, pz = ez - dz * bb0;
                const double ipl = 1.0 / std::max(1e-6, std::sqrt(px * px + py * py + pz * pz));
                const double ax = px * ipl, ay = py * ipl, az = pz * ipl;
                // Медленно дрейфующие угловые плюмы (несколько по лимбу) — фоновые протуберанцы.
                const double prom = 0.5 * (std::sin(ax * 5.0 + tcl * 0.20) * std::sin(ay * 6.0 - tcl * 0.15)
                                           + std::sin((ay + az) * 7.0 + tcl * 0.10));
                // (§5.13.13) Вспышки-СОБЫТИЯ. Две активные области медленно дрейфуют по лимбу
                //   (долгота = угол ap) и ПЕРИОДИЧЕСКИ извергаются: фаза внутри цикла (frac) даёт
                //   мгновенный поджиг (env=1 в начале) с экспоненциальным спадом — резкий подъём
                //   яркости, бело-голубой (горячий) тон и локальный вынос плазмы дальше фоновых
                //   плюмов. Всё детерминировано по fxClock (никакого rng); эффект локализован у
                //   долготы области (cosang), т.е. вспышка бьёт из ОДНОЙ точки лимба, а не по кольцу.
                //   Вынос ограничен ТЕМ ЖЕ гейтом короны (coronaR2) — bbox/ambient-корона не тронуты.
                double flareBoost = 0.0;
                for (int k = 0; k < 2; ++k) {
                    const double ap = tcl * (0.06 + 0.015 * double(k)) + double(k) * 2.1;
                    double rx = std::cos(ap), ry = std::sin(ap), rz = 0.4 * std::sin(tcl * 0.05 + double(k));
                    const double irn = 1.0 / std::sqrt(rx * rx + ry * ry + rz * rz);
                    const double cosang = (ax * rx + ay * ry + az * rz) * irn; // близость луча к области
                    if (cosang <= 0.6) continue;                              // только у активной долготы
                    const double tight = (cosang - 0.6) * (1.0 / 0.4);        // 0..1 к центру области
                    const double cyc = tcl * (0.5 + 0.1 * double(k)) + double(k) * 3.7;
                    const double frac = cyc - std::floor(cyc);                // фаза внутри цикла 0..1
                    flareBoost += tight * tight * std::exp(-frac * 5.0);      // поджиг → спад
                }
                const double reach = coronaR * (1.0 + 0.28 * prom + 0.30 * flareBoost); // вспышка — дальше
                const double den   = 1.0 / std::max(1e-6, reach - R);
                double g = (reach - dm) * den;                        // 1 у лимба → 0 у края плюма
                if (g < 0.0) g = 0.0; else if (g > 1.0) g = 1.0;
                const double flare = 0.6 + 0.5 * (prom > 0.0 ? prom : 0.0) + 1.6 * flareBoost; // ярче во вспышке
                Uint32 ia = (Uint32)(g * g * flare * 255.0); if (ia > 255u) ia = 255u;
                if (ia) {
                    const double fb = flareBoost > 1.0 ? 1.0 : flareBoost; // тон → бело-голубой во вспышке
                    const Uint32 ir = (Uint32)std::min(255.0, coR + (255.0 - coR) * fb);
                    const Uint32 ig = (Uint32)std::min(255.0, coG + (248.0 - coG) * fb);
                    const Uint32 ib = (Uint32)std::min(255.0, coB + (235.0 - coB) * fb);
                    row[bx] = (ia << 24) | (ir << 16) | (ig << 8) | ib;
                }
            }
        }
    }
    SDL_UnlockTexture(tex);

    const SDL_Rect src = { bx0, by0, bx1 - bx0, by1 - by0 };
    const SDL_Rect dst = { bx0 * 2, by0 * 2, (bx1 - bx0) * 2, (by1 - by0) * 2 };
    SDL_RenderCopy(renderer, tex, &src, &dst);
}

// ── Планеты/луны как ИЗОТРОПНЫЕ ОСВЕЩЁННЫЕ СФЕРЫ (тот же ray-sphere «шейдер») ──
// Продолжение философии звезды на ВСЕ тела системы (playtest #4: «заполнять пространство
// как функцию от радиуса всех тел», «рендер бедный — используй шейдеры»). Тело задано
// ТОЛЬКО центром C и радиусом Rb — больше ничего. Для каждого пикселя bbox проекции:
// восстанавливаем мировой луч из глаза, аналитически пересекаем со сферой (ближний корень
// t0>0 → видимая поверхность), нормаль N=(S−C)/Rb. Освещение — ЧИСТАЯ ГЕОМЕТРИЯ: звезда в
// начале координат (0,0,0), направление на свет L=normalize(−S); ламберт N·L даёт настоящий
// терминатор день/ночь (фазы/серп в зависимости от положения глаза). Поверхность по типу:
// газовый гигант — широтные полосы с турбулентным варпом (медленный дрейф), скалы — крап,
// лёд — высокое альбедо + светлые полюса, луна — серые «моря». Мягкий атмосферный лимб на
// освещённом крае для газа/льда. ПОПИКСЕЛЬНАЯ окклюзия звездой (если её вещество ближе
// поверхности тела — пиксель пропускаем, там уже плазма). Половинное разрешение (как звезда),
// bbox вокруг проекции, RenderCopy ×2 (linear). Изотропно ПО ПОСТРОЕНИЮ (функция геометрии).
// Возвращает true, если сфера отрисована (иначе вызывающий рисует диск-фолбэк).
static bool renderBodySphere(SDL_Renderer* renderer, const LocalScene& scene,
                             const View3D& view, const CameraBasis& basis,
                             int winW, int winH, const LocalBody& bd,
                             const ProjectedPoint& pc, double fade01) {
    const double Rb = bd.radius;
    if (Rb <= 0.0 || winW <= 0 || winH <= 0) return false;
    if (pc.behind || pc.depth <= view.nearPlane) return false;
    const double focal = std::max(1.0, view.focal);
    const double screenR = Rb * focal / pc.depth;
    if (screenR < 1.0) return false;    // меньше ~2 полноэкранных px — диск неотличим, фолбэк

    // Кольца газового гиганта (перспектива): аннулус в экваториальной плоскости тела
    // (нормаль = ось A, ниже). 0-поля => колец нет. Геометрия, как у сферы (луч↔плоскость).
    const bool   hasRing = (bd.kind == LB_GASGIANT && bd.ringOuter > bd.ringInner &&
                            bd.ringInner > 0.0);
    const double Ri = bd.ringInner, Ro = bd.ringOuter;
    const double Ri2 = Ri * Ri, Ro2 = Ro * Ro;
    const double invSpan = (Ro > Ri) ? 1.0 / (Ro - Ri) : 0.0;

    // Персистентная half-res стриминг-текстура (как у звезды, но отдельная).
    static SDL_Texture*  tex   = nullptr;
    static SDL_Renderer* texRd = nullptr;
    static int texW = 0, texH = 0;
    const int bw = (winW + 1) / 2, bh = (winH + 1) / 2;
    if (!tex || texRd != renderer || texW != bw || texH != bh) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, bw, bh);
        texRd = renderer; texW = bw; texH = bh;
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        }
    }
    if (!tex) return false;

    // bbox в координатах half-res буфера вокруг проекции центра (+рамка на атмосферный лимб).
    // При кольцах расширяем bbox на их внешний радиус (проекция кольца шире диска планеты).
    double margin = screenR * 1.14 + 3.0;
    if (hasRing) {
        double dd = pc.depth - Ro; if (dd < view.nearPlane) dd = view.nearPlane;
        const double ringScreenR = Ro * focal / dd + 3.0;
        if (ringScreenR > margin) margin = ringScreenR;
        const double cap = (double)(winW + winH);
        if (margin > cap) margin = cap;
    }
    int bx0 = std::max(0,  (int)std::floor((pc.x - margin) * 0.5));
    int by0 = std::max(0,  (int)std::floor((pc.y - margin) * 0.5));
    int bx1 = std::min(bw, (int)std::ceil ((pc.x + margin) * 0.5) + 1);
    int by1 = std::min(bh, (int)std::ceil ((pc.y + margin) * 0.5) + 1);
    if (bx0 >= bx1 || by0 >= by1) return false;

    const double ex = view.centerX, ey = view.centerY, ez = view.centerZ;
    const double cx = bd.x, cy = bd.y, cz = bd.z;
    const double Rb2 = Rb * Rb;
    const double Rs = scene.starRadius;                       // радиус звезды (для окклюзии)
    const bool   hasStar = scene.hasStar && Rs > 0.0;
    const double Es2 = ex * ex + ey * ey + ez * ez;           // |глаз|²
    // Инварианты луча по телу (не зависят от пикселя): глаз относительно центра тела.
    const double ocx = ex - cx, ocy = ey - cy, ocz = ez - cz;
    const double cBody = ocx * ocx + ocy * ocy + ocz * ocz - Rb2;
    const double csStar = Es2 - Rs * Rs;                      // <0 => глаз ВНУТРИ звезды

    // База цвета тела и параметры поверхности по типу.
    const double baseR = (double)bd.r, baseG = (double)bd.g, baseB = (double)bd.b;
    const int kind = bd.kind;
    // Детерминированный per-body сдвиг фаз/наклон оси (НЕ rng): из орбиты/радиуса.
    const double seed = bd.orbitRadius * 0.013 + bd.orbitPhase * 1.7 + Rb * 0.37;
    // Ось «вращения» (широтные полосы газовых гигантов, полюса льда): в основном мировой +Z
    // с лёгким детерминированным наклоном (нормаль плоскости орбит — тоже +Z, см. localgen).
    double axX = 0.20 * std::sin(seed * 1.3);
    double axY = 0.20 * std::cos(seed * 0.9);
    double axZ = 1.0;
    { const double al = 1.0 / std::sqrt(axX * axX + axY * axY + axZ * axZ); axX *= al; axY *= al; axZ *= al; }
    // Инварианты колец (не зависят от пикселя): (E−C)·A, |центр|², базовый цвет колец.
    const double ocA = ocx * axX + ocy * axY + ocz * axZ;     // глаз относительно плоскости колец
    const double CC  = cx * cx + cy * cy + cz * cz;           // |центр тела|² (для тени на кольцах)
    const double ringBR = 0.45 * baseR + 0.55 * 214.0;        // лёд/камень: светлее и «серее» тела
    const double ringBG = 0.45 * baseG + 0.55 * 205.0;
    const double ringBB = 0.45 * baseB + 0.55 * 188.0;
    const double tcl   = scene.fxClock;
    const double drift = (kind == LB_GASGIANT) ? tcl * 0.05 : 0.0;   // медленный дрейф полос
    const double ambient = 0.15;                              // подсветка от кластера (тысячи звёзд)

    const double invF  = 1.0 / focal;
    const double halfW = winW * 0.5, halfH = winH * 0.5;

    void* vpix = nullptr; int pitch = 0;
    SDL_Rect lockRect = { bx0, by0, bx1 - bx0, by1 - by0 };
    if (SDL_LockTexture(tex, &lockRect, &vpix, &pitch) != 0) return false;
    unsigned char* rowbase = (unsigned char*)vpix;

    for (int by = by0; by < by1; ++by) {
        Uint32* row = (Uint32*)(rowbase + (size_t)(by - by0) * pitch);
        std::memset(row, 0, (size_t)(bx1 - bx0) * sizeof(Uint32));   // промахи луча = прозрачно
        const double fy  = (double)(by * 2) + 0.5;
        const double scy = -(fy - halfH) * invF;
        for (int bx = bx0; bx < bx1; ++bx) {
            const double fx  = (double)(bx * 2) + 0.5;
            const double scx = (fx - halfW) * invF;
            // Мировой луч: dir = right·scx + up·scy + fwd (нормируем).
            double dx = basis.rX * scx + basis.uX * scy + basis.fX;
            double dy = basis.rY * scx + basis.uY * scy + basis.fY;
            double dz = basis.rZ * scx + basis.uZ * scy + basis.fZ;
            const double il = 1.0 / std::sqrt(dx * dx + dy * dy + dz * dz);
            dx *= il; dy *= il; dz *= il;

            // --- (a) Сфера тела: ближний корень t0, точка/нормаль поверхности. ---
            bool   sHit = false; double t0 = 0.0;
            double nx = 0.0, ny = 0.0, nz = 0.0, sx = 0.0, sy = 0.0, sz = 0.0;
            {
                const double b = ocx * dx + ocy * dy + ocz * dz;
                const double disc = b * b - cBody;
                if (disc >= 0.0) {
                    const double tt = -b - std::sqrt(disc);
                    if (tt > 0.0) {
                        sHit = true; t0 = tt;
                        sx = ex + dx * tt; sy = ey + dy * tt; sz = ez + dz * tt;
                        nx = (sx - cx) / Rb; ny = (sy - cy) / Rb; nz = (sz - cz) / Rb;
                    }
                }
            }

            // --- (b) Кольца: пересечение луча с экваториальной плоскостью (нормаль A). ---
            bool   rHit = false; double tR = 0.0, ralpha = 0.0;
            double rcr = 0.0, rcg = 0.0, rcb = 0.0;
            if (hasRing) {
                const double denom = dx * axX + dy * axY + dz * axZ;
                if (std::fabs(denom) > 1e-6) {
                    const double tt = -ocA / denom;                     // (C−E)·A / (d·A)
                    if (tt > 1e-4) {
                        const double px = ex + dx * tt, py = ey + dy * tt, pz = ez + dz * tt;
                        const double rvx = px - cx, rvy = py - cy, rvz = pz - cz;
                        const double rad2 = rvx * rvx + rvy * rvy + rvz * rvz;
                        if (rad2 >= Ri2 && rad2 <= Ro2) {
                            const double rad = std::sqrt(rad2);
                            const double u = (rad - Ri) * invSpan;      // 0..1 поперёк колец
                            double edge = 1.0;                          // мягкие кромки
                            if (u < 0.10) edge = u / 0.10;
                            else if (u > 0.90) edge = (1.0 - u) / 0.10;
                            double gap;                                 // деления (Кассини)
                            { const double g = (u - 0.52) / 0.055; gap  = 1.0 - 0.85 * std::exp(-g * g); }
                            { const double g = (u - 0.80) / 0.030; gap *= 1.0 - 0.55 * std::exp(-g * g); }
                            const double bands = 0.72 + 0.28 * std::sin(rad * 0.85 + seed * 2.0);
                            const double dens = edge * gap * bands;     // локальная плотность частиц
                            if (dens > 0.02) {
                                // Тень тела на кольцах (мягкая): прицельный параметр луча
                                // звезда(0)→P относительно центра тела; полутень ~18% радиуса.
                                double shadow = 1.0;
                                const double PP = px * px + py * py + pz * pz;
                                if (PP > 1e-9) {
                                    const double PC = px * cx + py * cy + pz * cz;
                                    const double tc = PC / PP;                      // ближайшее сближение
                                    if (tc > 0.0 && tc < 1.0) {                     // тело между звездой и P
                                        double dp2 = CC - PC * PC / PP;             // квадрат прицельного
                                        if (dp2 < 0.0) dp2 = 0.0;
                                        double sh = (std::sqrt(dp2) - Rb) / (0.18 * Rb);
                                        if (sh < 0.0) sh = 0.0; else if (sh > 1.0) sh = 1.0;
                                        shadow = 0.16 + 0.84 * sh;                  // 0.16 умбра → 1 свет
                                    }
                                }
                                const double lightR = ambient + (1.0 - ambient) * shadow;
                                double graze = 0.34 / (std::fabs(denom) + 0.22);   // edge-on → плотнее
                                if (graze < 1.0) graze = 1.0; else if (graze > 2.2) graze = 2.2;
                                ralpha = dens * graze * 0.80;
                                if (ralpha > 0.92) ralpha = 0.92;
                                rcr = ringBR * lightR; rcg = ringBG * lightR; rcb = ringBB * lightR;
                                rHit = true; tR = tt;
                            }
                        }
                    }
                }
            }

            // --- (c) Попиксельная окклюзия звездой (одна проверка на сферу и кольца). ---
            if (hasStar) {
                if (csStar < 0.0) continue;                     // глаз внутри звезды — всё за веществом
                const double bsr = ex * dx + ey * dy + ez * dz; // центр звезды = 0
                const double dss = bsr * bsr - csStar;
                if (dss >= 0.0) {
                    const double ts0 = -bsr - std::sqrt(dss);
                    if (ts0 > 1e-4) {
                        if (sHit && ts0 < t0) sHit = false;     // звезда ближе тела
                        if (rHit && ts0 < tR) rHit = false;     // звезда ближе колец
                    }
                }
            }
            if (!sHit && !rHit) continue;                       // мимо всего → прозрачно

            // --- (d) Шейдинг сферы (если попали) → scr/scg/scb. ---
            double scr = 0.0, scg = 0.0, scb = 0.0;
            if (sHit) {
                const double sl = std::sqrt(sx * sx + sy * sy + sz * sz);
                double lam = 1.0;
                if (sl > 1e-9) lam = -(nx * sx + ny * sy + nz * sz) / sl;
                double diff = (lam + 0.10) / 1.10;              // wrap-lighting: мягче кромка
                if (diff < 0.0) diff = 0.0; else if (diff > 1.0) diff = 1.0;
                const double lightF = ambient + (1.0 - ambient) * diff;
                double cr = baseR, cg = baseG, cb = baseB;
                if (kind == LB_GASGIANT) {
                    const double lat  = nx * axX + ny * axY + nz * axZ;      // [-1,1] «широта»
                    const double warp = 0.35 * std::sin((nx - nz) * 4.0 + drift * 2.0 + seed);
                    const double band = std::sin(lat * 8.0 + warp + seed * 3.0);
                    const double swirl= std::sin(lat * 17.0 - warp * 2.2 + drift * 4.0);
                    const double m = 0.5 + 0.5 * (0.72 * band + 0.28 * swirl);  // 0..1 пояс
                    const double f2 = 0.74 + 0.46 * m;
                    cr = baseR * f2; cg = baseG * f2; cb = baseB * f2;
                } else if (kind == LB_ICE) {
                    const double lat = std::fabs(nx * axX + ny * axY + nz * axZ);
                    const double sp1 = std::sin(nx * 13.0 + seed) * std::sin(ny * 11.0 - seed) *
                                       std::sin(nz * 12.0 + seed * 2.0);
                    const double alb = 1.05 + 0.12 * lat + 0.05 * sp1;      // ярче к полюсам
                    cr = baseR * alb; cg = baseG * alb; cb = std::min(255.0, baseB * alb + 8.0);
                } else if (kind == LB_MOON) {
                    const double sp1 = std::sin(nx * 15.0 + seed * 2.0) * std::sin(ny * 14.0 - seed) *
                                       std::sin(nz * 16.0 + seed);
                    const double mare = 0.82 + 0.18 * sp1;                  // тёмные «моря»
                    cr = baseR * mare; cg = baseG * mare; cb = baseB * mare;
                } else {                                                    // LB_ROCKY
                    const double sp1 = std::sin(nx * 7.0 + seed) * std::sin(ny * 8.0 - seed * 1.3);
                    const double sp2 = std::sin((nx + ny) * 13.0 - seed) * std::sin(nz * 11.0 + seed);
                    const double mott = 0.86 + 0.14 * sp1 + 0.06 * sp2;     // континенты/кратеры
                    cr = baseR * mott; cg = baseG * mott; cb = baseB * mott;
                }
                cr *= lightF; cg *= lightF; cb *= lightF;
                // Атмосферный лимб (газ/лёд): мягкое свечение на ОСВЕЩЁННОМ крае (рассеяние).
                if (kind == LB_GASGIANT || kind == LB_ICE) {
                    double ndv = -(nx * dx + ny * dy + nz * dz);            // к глазу (>0 у ближней грани)
                    if (ndv < 0.0) ndv = 0.0; else if (ndv > 1.0) ndv = 1.0;
                    double rim = 1.0 - ndv; rim *= rim;                     // резче к самому лимбу
                    const double glow = rim * diff * 85.0;                  // виден на дневной стороне
                    if (kind == LB_ICE) { cr += glow * 0.60; cg += glow * 0.80; cb += glow; }
                    else                { cr += glow;        cg += glow * 0.80; cb += glow * 0.50; }
                }
                scr = cr; scg = cg; scb = cb;
            }

            // --- (e) Композит кольца/сфера по глубине (t0 vs tR). ---
            double outR, outG, outB; Uint32 outA;
            if (sHit && (!rHit || t0 <= tR)) {
                outR = scr; outG = scg; outB = scb; outA = 255u;    // сфера ближе (кольцо за телом)
            } else if (sHit) {                                       // кольцо перед телом → поверх
                const double a = ralpha;
                outR = a * rcr + (1.0 - a) * scr;
                outG = a * rcg + (1.0 - a) * scg;
                outB = a * rcb + (1.0 - a) * scb;
                outA = 255u;
            } else {                                                 // только кольцо над фоном
                outR = rcr; outG = rcg; outB = rcb;
                const double aa = ralpha * 255.0;
                outA = aa > 255.0 ? 255u : (aa < 0.0 ? 0u : (Uint32)aa);
            }

            outR *= fade01; outG *= fade01; outB *= fade01;         // глубинный фейд сцены
            const Uint32 ir = outR > 255.0 ? 255u : (outR < 0.0 ? 0u : (Uint32)outR);
            const Uint32 ig = outG > 255.0 ? 255u : (outG < 0.0 ? 0u : (Uint32)outG);
            const Uint32 ib = outB > 255.0 ? 255u : (outB < 0.0 ? 0u : (Uint32)outB);
            row[bx - bx0] = (outA << 24) | (ir << 16) | (ig << 8) | ib;
        }
    }
    SDL_UnlockTexture(tex);

    const SDL_Rect src = { bx0, by0, bx1 - bx0, by1 - by0 };
    const SDL_Rect dst = { bx0 * 2, by0 * 2, (bx1 - bx0) * 2, (by1 - by0) * 2 };
    SDL_RenderCopy(renderer, tex, &src, &dst);
    return true;
}

// ── Астероид как ОСВЕЩЁННАЯ НЕПРАВИЛЬНАЯ ГЛЫБА (тот же ray-sphere «шейдер») ──
// Тот же приём, что планеты (§5.13.4), но для скал пояса. Отличия, делающие камень
// камнем, а не мини-планетой: (1) СИЛУЭТ неровный — эффективный радиус сферы
// модулируется угловым шумом (в осн. ВНУТРЬ, «сколы») → выщербленная глыба, а не
// гладкий шар; шум берётся во ВРАЩАЮЩЕЙСЯ системе камня (spin/spinVel уже
// интегрируются в localsim) → камень зримо кувыркается; (2) поверхность — крап +
// тёмные кратеры (клетки в степени ^4, мягкое ядро); (3) ламбертов терминатор от
// звезды в (0,0,0) с wrap-подсветкой + попиксельная окклюзия её веществом (как у тел).
// Только для КРУПНЫХ на экране скал (screenR>=ROCK_SHADE_MIN); мелкие крапинки рисует
// вызывающий дешёвым диском с фазовой подсветкой. Половинное разрешение, bbox,
// RenderCopy ×2. Детерминизм: сид из индекса/орбиты/радиуса (НЕ глобальный rng).
// Возвращает true, если глыба отрисована (иначе вызывающий рисует диск-фолбэк).
static bool renderRockLit(SDL_Renderer* renderer, const LocalScene& scene,
                          const View3D& view, const CameraBasis& basis,
                          int winW, int winH, const LocalRock& rk,
                          const ProjectedPoint& pc, double fade01, unsigned idx) {
    const double Rb = rk.radius;
    if (Rb <= 0.0 || winW <= 0 || winH <= 0) return false;
    if (pc.behind || pc.depth <= view.nearPlane) return false;
    const double focal = std::max(1.0, view.focal);
    const double screenR = Rb * focal / pc.depth;
    const double ROCK_SHADE_MIN = 5.0;      // < ~5 полноэкранных px — фолбэк-диск у вызывающего
    if (screenR < ROCK_SHADE_MIN) return false;

    // Персистентная half-res стриминг-текстура (как у звезды/тел, но своя).
    static SDL_Texture*  tex   = nullptr;
    static SDL_Renderer* texRd = nullptr;
    static int texW = 0, texH = 0;
    const int bw = (winW + 1) / 2, bh = (winH + 1) / 2;
    if (!tex || texRd != renderer || texW != bw || texH != bh) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, bw, bh);
        texRd = renderer; texW = bw; texH = bh;
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        }
    }
    if (!tex) return false;

    const double margin = screenR * 1.10 + 3.0;
    int bx0 = std::max(0,  (int)std::floor((pc.x - margin) * 0.5));
    int by0 = std::max(0,  (int)std::floor((pc.y - margin) * 0.5));
    int bx1 = std::min(bw, (int)std::ceil ((pc.x + margin) * 0.5) + 1);
    int by1 = std::min(bh, (int)std::ceil ((pc.y + margin) * 0.5) + 1);
    if (bx0 >= bx1 || by0 >= by1) return false;

    const double ex = view.centerX, ey = view.centerY, ez = view.centerZ;
    const double cx = rk.x, cy = rk.y, cz = rk.z;
    const double Rb2 = Rb * Rb;
    const double Rs = scene.starRadius;                       // радиус звезды (окклюзия)
    const bool   hasStar = scene.hasStar && Rs > 0.0;
    const double Es2 = ex * ex + ey * ey + ez * ez;
    const double ocx = ex - cx, ocy = ey - cy, ocz = ez - cz; // глаз относительно центра камня
    const double cBody = ocx * ocx + ocy * ocy + ocz * ocz - Rb2;
    const double csStar = Es2 - Rs * Rs;                      // <0 => глаз ВНУТРИ звезды

    const double baseR = (double)rk.r, baseG = (double)rk.g, baseB = (double)rk.b;
    const double ambient = 0.13;
    // Детерминированный сдвиг фаз шума (НЕ rng) + кувыркание вокруг мировой Z на угол spin.
    const double seed = idx * 0.61803398875 + rk.orbitAng * 0.31 + Rb * 0.13;
    const double ca = std::cos(rk.spin), sa = std::sin(rk.spin);

    const double invF  = 1.0 / focal;
    const double halfW = winW * 0.5, halfH = winH * 0.5;

    void* vpix = nullptr; int pitch = 0;
    SDL_Rect lockRect = { bx0, by0, bx1 - bx0, by1 - by0 };
    if (SDL_LockTexture(tex, &lockRect, &vpix, &pitch) != 0) return false;
    unsigned char* rowbase = (unsigned char*)vpix;

    for (int by = by0; by < by1; ++by) {
        Uint32* row = (Uint32*)(rowbase + (size_t)(by - by0) * pitch);
        std::memset(row, 0, (size_t)(bx1 - bx0) * sizeof(Uint32));   // промахи/сколы = прозрачно
        const double fy  = (double)(by * 2) + 0.5;
        const double scy = -(fy - halfH) * invF;
        for (int bx = bx0; bx < bx1; ++bx) {
            const double fx  = (double)(bx * 2) + 0.5;
            const double scx = (fx - halfW) * invF;
            double dx = basis.rX * scx + basis.uX * scy + basis.fX;
            double dy = basis.rY * scx + basis.uY * scy + basis.fY;
            double dz = basis.rZ * scx + basis.uZ * scy + basis.fZ;
            const double il = 1.0 / std::sqrt(dx * dx + dy * dy + dz * dz);
            dx *= il; dy *= il; dz *= il;

            // Номинальная сфера радиуса Rb: ближний корень t0, нормаль поверхности.
            const double b = ocx * dx + ocy * dy + ocz * dz;
            const double disc = b * b - cBody;
            if (disc < 0.0) continue;                    // луч мимо
            const double t0 = -b - std::sqrt(disc);
            if (t0 <= 0.0) continue;                     // сфера позади глаза
            const double sx = ex + dx * t0, sy = ey + dy * t0, sz = ez + dz * t0;
            const double nx = (sx - cx) / Rb, ny = (sy - cy) / Rb, nz = (sz - cz) / Rb;

            // Координата выборки шума во вращающейся системе камня (tumbling вокруг Z).
            const double qx = nx * ca - ny * sa;
            const double qy = nx * sa + ny * ca;
            const double qz = nz;

            // Неровный силуэт: эффективный радиус reff = Rb·(1+disp), disp в осн. ВНУТРЬ.
            // dmin² = Rb²−disc; пиксель принят, если dmin ≤ reff ⇔ disc ≥ Rb²−reff².
            const double lump = std::sin(qx * 5.0 + seed) * std::sin(qy * 6.0 - seed * 1.3)
                              + 0.5 * std::sin(qz * 7.0 + seed * 2.1);   // [-1.5, 1.5]
            const double disp = -0.10 + 0.10 * lump;                     // ≈[-0.25, +0.05]
            const double reff = Rb * (1.0 + disp);
            if (disc < Rb2 - reff * reff) continue;                      // выщерблено → прозрачно

            // Освещение: свет из (0,0,0), L=−normalize(S); ламберт с wrap (мягче кромка).
            const double sl = std::sqrt(sx * sx + sy * sy + sz * sz);
            double lam = 1.0;
            if (sl > 1e-9) lam = -(nx * sx + ny * sy + nz * sz) / sl;
            double diff = (lam + 0.10) / 1.10;
            if (diff < 0.0) diff = 0.0; else if (diff > 1.0) diff = 1.0;
            const double lightF = ambient + (1.0 - ambient) * diff;

            // Поверхность: крап (широкий) + тёмные кратеры (мелкие клетки, степень ^4).
            const double mott = 0.80 + 0.20 * std::sin(qx * 9.0 + seed) * std::sin(qy * 8.0 - seed);
            double cf = 0.5 + 0.5 * std::sin(qx * 15.0 + seed * 3.0)
                                  * std::sin(qy * 17.0 - seed * 1.7)
                                  * std::sin(qz * 16.0 + seed);
            double crater = cf * cf; crater *= crater;   // ^4 — маленькие тёмные ядра
            const double surf = mott * (1.0 - 0.55 * crater);

            double cr = baseR * surf * lightF;
            double cg = baseG * surf * lightF;
            double cb = baseB * surf * lightF;

            // (§5.13.15) Зеркальный блик Блинна–Фонга — только для блестящих материалов
            // (лёд/металл, rk.spec>0) и на освещённой стороне. Свет из (0,0,0): L=−S/|S|,
            // взгляд V=−dir (нормированный луч из глаза). Аддитивно к диффузному цвету,
            // общий клэмп ниже; матовые породы (углерод/силикат, spec≈0) блика не дают.
            if (rk.spec > 0.0 && lam > 0.0 && sl > 1e-9) {
                const double Lx = -sx / sl, Ly = -sy / sl, Lz = -sz / sl;
                double hx = Lx - dx, hy = Ly - dy, hz = Lz - dz;   // H = L + V, V = −dir
                const double hl = std::sqrt(hx * hx + hy * hy + hz * hz);
                if (hl > 1e-9) {
                    const double ndh = (nx * hx + ny * hy + nz * hz) / hl;
                    if (ndh > 0.0) {
                        const double spc = std::pow(ndh, 24.0) * rk.spec * lightF;
                        cr += spc * (0.72 * 255.0 + 0.28 * baseR);   // блик — в осн. белый,
                        cg += spc * (0.72 * 255.0 + 0.28 * baseG);   //  с лёгким оттенком
                        cb += spc * (0.72 * 255.0 + 0.28 * baseB);   //  материала
                    }
                }
            }

            // Попиксельная окклюзия веществом звезды (камень частично за лимбом).
            if (hasStar) {
                if (csStar < 0.0) continue;              // глаз внутри звезды — всё за веществом
                const double bsr = ex * dx + ey * dy + ez * dz;   // центр звезды = 0
                const double dss = bsr * bsr - csStar;
                if (dss >= 0.0) {
                    const double ts0 = -bsr - std::sqrt(dss);
                    if (ts0 > 1e-4 && ts0 < t0) continue;         // звезда ближе камня
                }
            }

            cr *= fade01; cg *= fade01; cb *= fade01;    // глубинный фейд сцены
            const Uint32 ir = cr > 255.0 ? 255u : (cr < 0.0 ? 0u : (Uint32)cr);
            const Uint32 ig = cg > 255.0 ? 255u : (cg < 0.0 ? 0u : (Uint32)cg);
            const Uint32 ib = cb > 255.0 ? 255u : (cb < 0.0 ? 0u : (Uint32)cb);
            row[bx - bx0] = 0xFF000000u | (ir << 16) | (ig << 8) | ib;
        }
    }
    SDL_UnlockTexture(tex);

    const SDL_Rect src = { bx0, by0, bx1 - bx0, by1 - by0 };
    const SDL_Rect dst = { bx0 * 2, by0 * 2, (bx1 - bx0) * 2, (by1 - by0) * 2 };
    SDL_RenderCopy(renderer, tex, &src, &dst);
    return true;
}

// Туманность-фон как газовое поле на «небесной сфере»: для каждого пикселя мировой луч
// (тот же приём, что шейдеры), многооктавный шум с доменным варпом ОТ НАПРАВЛЕНИЯ луча →
// клочья/волокна газа, устойчивые при повороте камеры (согласовано со скайбоксом §1).
// Крупномасштабная «банка» сгущает газ в одной части неба; ядра филаментов чуть белее;
// медленный дрейф по fxClock. Чистая функция направления (изотропно), global rng не трогаем.
// Только перспектива — орто-карта (§2.7) рисует прежний плоский тон отдельной веткой.
static void renderNebula(SDL_Renderer* renderer, const LocalScene& scene,
                         const View3D& view, const CameraBasis& basis, int winW, int winH) {
    if (winW <= 0 || winH <= 0) return;
    const double strength = scene.nebulaStrength;
    if (strength <= 0.0) return;
    const double focal = std::max(1.0, view.focal);

    static SDL_Texture*  tex   = nullptr;
    static SDL_Renderer* texRd = nullptr;
    static int texW = 0, texH = 0;
    const int bw = (winW + 1) / 2, bh = (winH + 1) / 2;
    if (!tex || texRd != renderer || texW != bw || texH != bh) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, bw, bh);
        texRd = renderer; texW = bw; texH = bh;
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        }
    }
    if (!tex) return;

    const double gR = (double)scene.nebulaR, gG = (double)scene.nebulaG, gB = (double)scene.nebulaB;
    const double t  = scene.fxClock;
    // Детерминированная ось «банки» из тона (без rng) — плотнее в одной части неба.
    double axx = std::sin(gR * 0.021 + 1.3), axy = std::sin(gG * 0.017 + 2.7), axz = std::sin(gB * 0.013 + 0.6);
    { double al = std::sqrt(axx*axx + axy*axy + axz*axz);
      if (al < 1e-6) { axx = 0.0; axy = 1.0; axz = 0.0; al = 1.0; }
      axx /= al; axy /= al; axz /= al; }

    void* vpix = nullptr; int pitch = 0;
    if (SDL_LockTexture(tex, nullptr, &vpix, &pitch) != 0) return;
    unsigned char* rowbase = (unsigned char*)vpix;

    const double invF  = 1.0 / focal;
    const double halfW = winW * 0.5, halfH = winH * 0.5;
    const double MAXA  = strength * 200.0;   // потолок альфы в плотном ядре банки

    for (int by = 0; by < bh; ++by) {
        Uint32* row = (Uint32*)(rowbase + (size_t)by * pitch);
        const double fy  = (double)(by * 2) + 0.5;
        const double scy = -(fy - halfH) * invF;
        for (int bx = 0; bx < bw; ++bx) {
            const double fx  = (double)(bx * 2) + 0.5;
            const double scx = (fx - halfW) * invF;
            double dx = basis.rX * scx + basis.uX * scy + basis.fX;
            double dy = basis.rY * scx + basis.uY * scy + basis.fY;
            double dz = basis.rZ * scx + basis.uZ * scy + basis.fZ;
            const double il = 1.0 / std::sqrt(dx*dx + dy*dy + dz*dz);
            dx *= il; dy *= il; dz *= il;

            // Доменный варп направления луча.
            const double w1 = 0.40 * std::sin(dy*3.0 + t*0.05) * std::sin(dz*2.6 - t*0.04);
            const double wx = dx + w1, wy = dy - w1, wz = dz + w1;
            // Пухлые облака: НИЗКОчастотные произведения синусов дают локализованные сгустки
            // (а не полосы, как сумма синусов), + средняя и мелкая октавы — волокна. Домен уже
            // искажён варпом (wx..wz), поэтому сгустки не выровнены по осям.
            const double base = std::sin(wx*2.2 + t*0.02) * std::sin(wy*1.9 - t*0.015) * std::sin(wz*2.5 + t*0.01);
            const double mid  = std::sin(wx*5.0 - t*0.02) * std::sin(wz*5.5 + t*0.02);
            const double hi   = std::sin(wy*11.0)         * std::sin(wz*13.0);
            const double f    = 0.60*base + 0.30*mid + 0.15*hi;      // ~[-1,1]: сгустки + волокна
            double cloud = 0.5 + 0.7*f;                              // ~[-0.2,1.2]

            const double bank = 0.5 + 0.5*(dx*axx + dy*axy + dz*axz); // 0..1 поперёк неба
            double dens = cloud * (0.35 + 0.65*bank) - 0.18;         // мягкий пол → клочья, не заливка
            if (dens <= 0.0) { row[bx] = 0u; continue; }             // промах = прозрачно
            if (dens > 1.0) dens = 1.0;
            dens *= (0.6 + 0.4*dens);                                // мягкий контраст (мид не давит)

            const double a  = MAXA * dens;
            const double wf = 0.30 * dens;                           // ядра волокон чуть белее
            double rr = gR + (255.0 - gR) * wf;
            double gg = gG + (255.0 - gG) * wf;
            double bb = gB + (255.0 - gB) * wf;
            const Uint32 ia = a  > 255.0 ? 255u : (a  < 0.0 ? 0u : (Uint32)a);
            const Uint32 ir = rr > 255.0 ? 255u : (rr < 0.0 ? 0u : (Uint32)rr);
            const Uint32 ig = gg > 255.0 ? 255u : (gg < 0.0 ? 0u : (Uint32)gg);
            const Uint32 ib = bb > 255.0 ? 255u : (bb < 0.0 ? 0u : (Uint32)bb);
            row[bx] = (ia << 24) | (ir << 16) | (ig << 8) | ib;
        }
    }
    SDL_UnlockTexture(tex);

    const SDL_Rect src = { 0, 0, bw, bh };
    const SDL_Rect dst = { 0, 0, winW, winH };
    SDL_RenderCopy(renderer, tex, &src, &dst);
}

} // namespace

void renderLocalScene(SDL_Renderer* renderer, const Game& game, const LocalScene& scene,
                      const View3D& view0, const CameraBasis& basis0, int winW, int winH) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const int cx = winW / 2, cy = winH / 2;

    // (0) ТРЯСКА ЭКРАНА. Перспектива: угловое дрожание камеры (панорама/тангаж на
    //     малый угол) — сдвиг ~одинаков на любой глубине, глаз не двигаем. Орто:
    //     смещаем центр камеры в LU (как прежний top-down). Фаза — fxClock (реальные
    //     секунды, независимы от warp).
    View3D view = view0;
    CameraBasis basis = basis0;
    if (view.perspective) {
        if (scene.shake > 0.0) {
            double ox = scene.shake * std::sin(scene.fxClock * 90.0);
            double oy = scene.shake * std::cos(scene.fxClock * 70.0);
            double yawJ   = ox / std::max(1.0, view.focal); // сдвиг ~ox px
            double pitchJ = oy / std::max(1.0, view.focal);
            double nfx = basis.fX + basis.rX * yawJ + basis.uX * pitchJ;
            double nfy = basis.fY + basis.rY * yawJ + basis.uY * pitchJ;
            double nfz = basis.fZ + basis.rZ * yawJ + basis.uZ * pitchJ;
            basis = makeCameraFrameBasis(nfx, nfy, nfz, basis.uX, basis.uY, basis.uZ);
        }
    } else {
        double ox = scene.shake * std::sin(scene.fxClock * 90.0);
        double oy = scene.shake * std::cos(scene.fxClock * 70.0);
        view.centerX += ox / std::max(1e-6, view.scale);
        view.centerY -= oy / std::max(1e-6, view.scale);
        basis = makeCameraBasis(view);
    }

    // Глубинный фейд сцены: в перспективе — мягкий (глубина достигает тысяч LU),
    // в орто — прежний depthFade (сохраняет вид карты).
    auto sceneFade = [&](double depth) -> double {
        if (view.perspective) {
            double v = 1.15 - std::abs(depth) / 1700.0;
            return v < 0.30 ? 0.30 : (v > 1.0 ? 1.0 : v);
        }
        return depthFade(depth);
    };

    // (0b) ТУМАННОСТЬ (косметический фон; strength задаётся в localgen для части сцен).
    // Перспектива — богатое газовое поле на небесной сфере (renderNebula, попиксельно).
    // Орто-карта (C) — прежний плоский тон + 3 сгустка, ПОБИТОВО НЕ ТРОГАЕМ (§2.7).
    if (scene.nebulaStrength > 0.0) {
        if (view.perspective) {
            renderNebula(renderer, scene, view, basis, winW, winH);
        } else {
            int na = (int)(scene.nebulaStrength * 60.0);
            fillRect(renderer, 0, 0, winW, winH, rgba(scene.nebulaR, scene.nebulaG, scene.nebulaB, na));
            // Мягкие газовые сгустки: 3 больших круга в ДЕТЕРМИНИРОВАННЫХ точках
            // (хеш от размеров окна, НЕ rng), очень низкая альфа. Стоимость мизерна.
            int ba = (int)(scene.nebulaStrength * 30.0);
            for (int i = 0; ba > 0 && i < 3; ++i) {
                unsigned h = ((unsigned)winW * 73856093u) ^ ((unsigned)winH * 19349663u) ^
                             ((unsigned)(i + 1) * 83492791u);
                h ^= h >> 13; h *= 2246822519u; h ^= h >> 16;
                unsigned h2 = h * 2654435761u; h2 ^= h2 >> 15;
                int bx = int(h % (unsigned)std::max(1, winW));
                int by = int(h2 % (unsigned)std::max(1, winH));
                int br = std::min(winW, winH) / 4 + int((h >> 5) % 120u);
                fillCircle(renderer, bx, by, br, rgba(scene.nebulaR, scene.nebulaG, scene.nebulaB, ba));
            }
        }
    }

    // (1) ФОН.
    if (view.perspective) {
        // LOD-СКАЙБОКС: реальные звёзды кластера на «небесной сфере». Направление на
        // звезду = normalize(star.pos - anchor) в координатах кластера. Проецируем как
        // точку на бесконечности (важна только ориентация камеры). LOD: яркость/размер
        // ~ обратно дистанции (нормировано на среднюю дистанцию от якоря).
        const std::vector<ClusterStar>& cs = game.cluster.stars;
        const size_t N = cs.size();
        if (N > 0) {
            double ax, ay, az;
            int anchorIdx = -1;
            if (scene.starIndex >= 0 && (size_t)scene.starIndex < N) {
                ax = cs[scene.starIndex].x; ay = cs[scene.starIndex].y; az = cs[scene.starIndex].z;
                anchorIdx = scene.starIndex;
            } else {
                double sx = 0.0, sy = 0.0, sz = 0.0; // глубокий космос — центроид кластера
                for (size_t i = 0; i < N; ++i) { sx += cs[i].x; sy += cs[i].y; sz += cs[i].z; }
                ax = sx / double(N); ay = sy / double(N); az = sz / double(N);
            }
            double sumd = 0.0; int cnt = 0;
            for (size_t i = 0; i < N; ++i) {
                if ((int)i == anchorIdx) continue;
                double dx = cs[i].x - ax, dy = cs[i].y - ay, dz = cs[i].z - az;
                sumd += std::sqrt(dx * dx + dy * dy + dz * dz); ++cnt;
            }
            double meanD = (cnt > 0) ? sumd / double(cnt) : 1.0;
            if (meanD < 1e-6) meanD = 1.0;
            for (size_t i = 0; i < N; ++i) {
                if ((int)i == anchorIdx) continue;
                double dx = cs[i].x - ax, dy = cs[i].y - ay, dz = cs[i].z - az;
                double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dist < 1e-6) continue;
                double inv = 1.0 / dist;
                ProjectedPoint p = projectDirectionWithBasis(dx * inv, dy * inv, dz * inv,
                                                             winW, winH, view, basis);
                if (p.behind) continue;
                if (p.x < -2 || p.x > winW + 2 || p.y < -2 || p.y > winH + 2) continue;
                double mag = meanD / dist;               // >1 ближе среднего
                if (mag > 3.0) mag = 3.0;
                int a = 40 + int(70.0 * mag); if (a > 235) a = 235;
                SDL_Color col = skyStarColor((unsigned)i, cs[i].stellarClass);
                col.a = (Uint8)a;
                if (mag > 2.4) {                          // яркая близкая — крестик
                    fillRect(renderer, p.x - 1, p.y, 3, 1, col);
                    fillRect(renderer, p.x, p.y - 1, 1, 3, col);
                } else if (mag > 1.6) {
                    fillRect(renderer, p.x, p.y, 2, 2, col);
                } else {
                    fillRect(renderer, p.x, p.y, 1, 1, col);
                }
            }
        }
    } else {
        // Карта: детерминированная россыпь тусклых звёзд (без глобального rng).
        for (int i = 0; i < 60; ++i) {
            unsigned h = (unsigned)(i + 1) * 2654435761u;
            h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            unsigned hy = h * 668265263u; hy ^= hy >> 15;
            int sx = int(h % (unsigned)std::max(1, winW));
            int sy = int(hy % (unsigned)std::max(1, winH));
            int b = 44 + int((h >> 3) % 46u);
            fillRect(renderer, sx, sy, 1, 1, rgba(b, b, b + 10, 150));
        }
    }

    // Звезда в мировом центре (0,0,0).
    ProjectedPoint sp = projectPointWithBasis(0.0, 0.0, 0.0, winW, winH, view, basis);

    // Окклюзия веществом звезды (только перспектива-кокпит; на орто-карте вид сверху —
    // ничего не прячем). Центр звезды = (0,0,0), радиус = scene.starRadius.
    auto occ = [&](double wx, double wy, double wz) -> bool {
        return scene.hasStar && view.perspective &&
               segIntersectsSphere(view.centerX, view.centerY, view.centerZ,
                                    wx, wy, wz, 0.0, 0.0, 0.0, scene.starRadius);
    };

    // (2) ОРБИТАЛЬНЫЕ КОЛЬЦА — плоские окружности, осмысленны только в орто-карте
    //     (в перспективе орбита проецируется в эллипс; пропускаем).
    if (scene.hasStar && !view.perspective) {
        SDL_Color ring = rgba(70, 90, 120, 90);
        for (size_t i = 0; i < scene.bodies.size(); ++i) {
            int rr = int(scene.bodies[i].orbitRadius * view.scale);
            if (rr <= 0 || rr > 4000) continue;
            strokeCircle(renderer, sp.x, sp.y, rr, ring);
        }
    }

    // (3) ЗВЕЗДА — ИЗОТРОПНАЯ АНАЛИТИЧЕСКАЯ СФЕРА (ray-sphere «шейдер», см. renderStarPlasma).
    //     Тело задано ТОЛЬКО центром (0,0,0) и радиусом R; плазма — функция геометрии луча,
    //     одинаковая со всех сторон. Никаких порогов/«переключения режима» и заглушек-заливок:
    //     каждый пиксель, чей мировой луч попадает в сферу, показывает вещество; остальные —
    //     космос (или корону у лимба). ray-sphere окклюзия (occ) честно прячет за ней тела.
    if (scene.hasStar && view.perspective) {
        renderStarPlasma(renderer, scene, view, basis, winW, winH);
    } else if (scene.hasStar) {
        // Орто-карта (вид сверху): звезда — простой диск + два ореола (как раньше).
        int sr = std::min(2000, std::max(3, radiusPx(scene.starRadius, sp.depth, view)));
        double gp = 1.0 + 0.04 * std::sin(scene.fxClock * 2.0);
        fillCircle(renderer, sp.x, sp.y, sr, rgba(scene.starR, scene.starG, scene.starB, 255));
        strokeCircle(renderer, sp.x, sp.y, std::min(2600, int(sr * 1.4 * gp)),
                     rgba(scene.starR, scene.starG, scene.starB, 70));
        strokeCircle(renderer, sp.x, sp.y, std::min(3200, int(sr * 1.9 * gp)),
                     rgba(scene.starR, scene.starG, scene.starB, 34));
    }

    // (4) ТЕЛА (планеты/станции), отсортированные по глубине (дальнее раньше ближнего).
    {
        std::vector<int> order;
        std::vector<double> depth(scene.bodies.size(), 0.0);
        order.reserve(scene.bodies.size());
        for (size_t i = 0; i < scene.bodies.size(); ++i) {
            const LocalBody& bd = scene.bodies[i];
            depth[i] = projectPointWithBasis(bd.x, bd.y, bd.z, winW, winH, view, basis).depth;
            order.push_back((int)i);
        }
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return view.perspective ? (depth[a] > depth[b]) : (depth[a] < depth[b]);
        });
        for (size_t k = 0; k < order.size(); ++k) {
            const LocalBody& bd = scene.bodies[order[k]];
            ProjectedPoint p = projectPointWithBasis(bd.x, bd.y, bd.z, winW, winH, view, basis);
            if (view.perspective && p.behind) continue;
            if (p.x < -200 || p.x > winW + 200 || p.y < -200 || p.y > winH + 200) continue;
            if (occ(bd.x, bd.y, bd.z)) continue;   // за веществом звезды — не просвечивает
            double f = sceneFade(p.depth);
            if (!view.perspective) {
                // (4a) КОЛЬЦА газового гиганта — концентрические окружности (плоский вид).
                if (bd.ringOuter > 0.0) {
                    int ri = std::max(1, int(bd.ringInner * view.scale));
                    int ro = int(bd.ringOuter * view.scale);
                    if (ro > ri && ro < 4000) {
                        const int nRings = 4;
                        for (int rr = 0; rr < nRings; ++rr) {
                            double tt = (nRings > 1) ? double(rr) / (nRings - 1) : 0.0;
                            int rad = ri + int((ro - ri) * tt);
                            int a = int(150.0 - 90.0 * tt); // 150 (внутр.) -> 60 (внешн.)
                            strokeCircle(renderer, p.x, p.y, rad, fade(bd.r, bd.g, bd.b, a, f));
                        }
                    }
                }
                // (4b) ОРБИТА ЛУНЫ: слабое кольцо вокруг РОДИТЕЛЯ на радиусе орбиты луны.
                if (bd.kind == LB_MOON && bd.parent >= 0 && (size_t)bd.parent < scene.bodies.size()) {
                    const LocalBody& par = scene.bodies[bd.parent];
                    ProjectedPoint pc = projectPointWithBasis(par.x, par.y, par.z, winW, winH, view, basis);
                    int orad = int(bd.orbitRadius * view.scale);
                    if (orad > 2 && orad < 4000)
                        strokeCircle(renderer, pc.x, pc.y, orad, fade(bd.r, bd.g, bd.b, 40, f));
                }
            }
            int r = std::min(1200, std::max(2, radiusPx(bd.radius, p.depth, view)));
            // Перспектива: планеты/луны — ОСВЕЩЁННЫЕ ray-sphere сферы (звезда в 0,0,0 даёт
            // настоящий терминатор день/ночь). Мелкие/орто-карта → простой диск (фолбэк).
            const bool sphereKind = (bd.kind == LB_ROCKY || bd.kind == LB_GASGIANT ||
                                     bd.kind == LB_ICE   || bd.kind == LB_MOON);
            bool drew = false;
            if (view.perspective && sphereKind)
                drew = renderBodySphere(renderer, scene, view, basis, winW, winH, bd, p, f);
            if (!drew)
                fillCircle(renderer, p.x, p.y, r, fade(bd.r, bd.g, bd.b, 255, f));
            if (bd.hasMarket) {
                strokeCircle(renderer, p.x, p.y, r + 3, rgba(P.green.r, P.green.g, P.green.b, 150));
                double bdist = std::sqrt((bd.x - scene.px) * (bd.x - scene.px) +
                                         (bd.y - scene.py) * (bd.y - scene.py) +
                                         (bd.z - scene.pz) * (bd.z - scene.pz));
                // Лунам крупную подпись не рисуем (снижаем визуальный шум).
                if (bd.kind != LB_MOON && (r >= 4 || bdist < 220.0) && !bd.name.empty()) {
                    drawText(renderer, p.x - textWidth(bd.name, 1) / 2, p.y + r + 4, bd.name, P.dim, 1);
                }
            }
        }
    }

    // (5) АСТЕРОИДЫ. Крупные на экране — освещённые неправильные глыбы (renderRockLit,
    //     терминатор от звезды + кувыркание + кратеры/сколы); мелкие крапинки и орто-карта —
    //     дешёвый диск. В перспективе диск получает ФАЗОВУЮ подсветку (тёмный силуэтом к
    //     звезде, яркий — звезда за спиной), чтобы не выглядеть плоской монеткой у светила.
    //     Орто-карта (§2.7) — по-прежнему ровный диск (br=1.0), побитово не тронута.
    for (size_t i = 0; i < scene.rocks.size(); ++i) {
        const LocalRock& rk = scene.rocks[i];
        ProjectedPoint p = projectPointWithBasis(rk.x, rk.y, rk.z, winW, winH, view, basis);
        if (view.perspective && p.behind) continue;
        if (p.x < -100 || p.x > winW + 100 || p.y < -100 || p.y > winH + 100) continue;
        if (occ(rk.x, rk.y, rk.z)) continue;   // астероид за звездой — скрыт
        double f = sceneFade(p.depth);
        int r = std::min(400, std::max(1, radiusPx(rk.radius, p.depth, view)));
        bool drew = false;
        if (view.perspective)
            drew = renderRockLit(renderer, scene, view, basis, winW, winH, rk, p, f, (unsigned)i);
        if (!drew) {
            double br = 1.0;                   // фаза только в перспективе (орто не трогаем)
            if (view.perspective && scene.hasStar) {
                const double lx = -rk.x, ly = -rk.y, lz = -rk.z;                       // к звезде (0,0,0)
                const double vx = scene.px - rk.x, vy = scene.py - rk.y, vz = scene.pz - rk.z; // к камере
                const double ll = std::sqrt(lx * lx + ly * ly + lz * lz);
                const double vl = std::sqrt(vx * vx + vy * vy + vz * vz);
                if (ll > 1e-9 && vl > 1e-9) {
                    const double ph = (lx * vx + ly * vy + lz * vz) / (ll * vl); // cos фазового угла
                    const double phase01 = 0.5 + 0.5 * ph;                       // 1 фронт-свет, 0 контровой
                    br = 0.28 + 0.72 * phase01;                                  // не гасим в полный ноль
                }
            }
            fillCircle(renderer, p.x, p.y, r, fade(rk.r, rk.g, rk.b, 255, f * br));
        }
        if ((int)i == scene.miningRock) {
            strokeCircle(renderer, p.x, p.y, r + 4, P.amber);
            strokeCircle(renderer, p.x, p.y, r + 8, rgba(P.amber.r, P.amber.g, P.amber.b, 110));
        }
    }

    // (5b) ЛУТ: вращающийся янтарный ромбик; символ элемента при сближении (<120 LU).
    for (size_t i = 0; i < scene.loot.size(); ++i) {
        const LocalLoot& lt = scene.loot[i];
        ProjectedPoint p = projectPointWithBasis(lt.x, lt.y, lt.z, winW, winH, view, basis);
        if (view.perspective && p.behind) continue;
        if (p.x < -40 || p.x > winW + 40 || p.y < -40 || p.y > winH + 40) continue;
        if (occ(lt.x, lt.y, lt.z)) continue;   // лут за звездой — скрыт
        double f = sceneFade(p.depth);
        int rad = 4;
        int dx[4], dy[4];
        for (int k = 0; k < 4; ++k) {
            double ang = lt.spin + k * 1.5707963; // pi/2 шаг: вершины повёрнутого квадрата
            dx[k] = p.x + int(std::cos(ang) * rad);
            dy[k] = p.y + int(std::sin(ang) * rad);
        }
        SDL_Color col = fade(lt.r, lt.g, lt.b, 255, f);
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
        SDL_RenderDrawLine(renderer, dx[0], dy[0], dx[1], dy[1]);
        SDL_RenderDrawLine(renderer, dx[1], dy[1], dx[2], dy[2]);
        SDL_RenderDrawLine(renderer, dx[2], dy[2], dx[3], dy[3]);
        SDL_RenderDrawLine(renderer, dx[3], dy[3], dx[0], dy[0]);
        strokeCircle(renderer, p.x, p.y, rad + 2, rgba(P.amber.r, P.amber.g, P.amber.b, 80));
        double ld = std::sqrt((lt.x - scene.px) * (lt.x - scene.px) +
                              (lt.y - scene.py) * (lt.y - scene.py) +
                              (lt.z - scene.pz) * (lt.z - scene.pz));
        if (ld < 120.0 && lt.element >= 0 && (size_t)lt.element < elementCount()) {
            const char* sym = elementDefinitions()[lt.element].symbol;
            if (sym && sym[0]) {
                std::string s(sym);
                drawText(renderer, p.x - textWidth(s, 1) / 2, p.y + rad + 4, s, P.amber, 1);
            }
        }
    }

    // Хелпер отрисовки одной частицы (juice). Альфа = a * clamp(life/maxLife,0,1).
    // В кокпите ГЛАЗ камеры совпадает с кораблём, поэтому его собственный выхлоп/дым
    // рождается фактически В камере: без защиты близкая частица раздувается в
    // экранный квад (radiusPx = size*focal/depth → ∞ при depth→0). Поэтому в
    // перспективе отсекаем частицы у самой камеры и клампим экранный размер.
    auto drawParticle = [&](const LocalFx& fx) {
        ProjectedPoint p = projectPointWithBasis(fx.x, fx.y, fx.z, winW, winH, view, basis);
        if (view.perspective && p.behind) return;
        if (view.perspective && p.depth < 2.0) return;   // частица практически в кокпите
        if (p.x < -40 || p.x > winW + 40 || p.y < -40 || p.y > winH + 40) return;
        if (occ(fx.x, fx.y, fx.z)) return;   // частица за веществом звезды — скрыта
        double t = fx.life / std::max(1e-6, fx.maxLife);
        if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
        int alpha = int(double(fx.a) * t);
        if (alpha <= 0) return;
        if (fx.kind == FX_RING) {
            double radiusLU = fx.size * (1.0 - t);           // растёт по мере life->0
            int rr = std::max(1, radiusPx(radiusLU, p.depth, view));
            if (view.perspective && rr > 300) rr = 300;      // взрыв-кольцо не на весь экран
            strokeCircle(renderer, p.x, p.y, rr, rgba(fx.r, fx.g, fx.b, alpha));
            return;
        }
        int s = std::max(2, radiusPx(fx.size, p.depth, view));
        if (view.perspective) {
            // Экранный кап по типу: близкий собственный выхлоп/дым читается искрами-точками,
            // а боевой juice (вспышка/искры/обломки) остаётся сочным. radiusPx у камеры огромен.
            int cap = 48;
            switch (fx.kind) {
                case FX_TRAIL: case FX_SMOKE: cap = 8;  break; // выхлоп/дым — мелкие угольки
                case FX_DEBRIS:               cap = 22; break;
                case FX_SPARK:                cap = 40; break; // искры попаданий — заметны
                case FX_MUZZLE:               cap = 14; break; // вспышка у дула — компактный блик
                default:                      cap = 48; break;
            }
            if (s > cap) s = cap;
        }
        if (fx.kind == FX_MUZZLE) {
            // Аддитивный блик-кружок (НЕ квад): мягкий ореол + белое ядро. На тёмном фоне
            // читается яркой вспышкой у дула, а не «синим квадратом» во весь экран (был баг).
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
            fillCircle(renderer, p.x, p.y, s,     rgba(fx.r, fx.g, fx.b, alpha));
            fillCircle(renderer, p.x, p.y, s / 2, rgba(255, 255, 255, alpha));
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            return;
        }
        int a2 = (fx.kind == FX_SMOKE) ? alpha / 2 : alpha;  // дым тусклее
        fillRect(renderer, p.x - s / 2, p.y - s / 2, s, s, rgba(fx.r, fx.g, fx.b, a2));
    };

    // (5c) ЧАСТИЦЫ ПОЗАДИ КОРАБЛЕЙ: трейл двигателя, дым, обломки.
    for (size_t i = 0; i < scene.fx.size(); ++i) {
        const LocalFx& fx = scene.fx[i];
        if (fx.kind == FX_TRAIL || fx.kind == FX_SMOKE || fx.kind == FX_DEBRIS) drawParticle(fx);
    }

    // (6) КОРАБЛИ.
    for (size_t i = 0; i < scene.craft.size(); ++i) {
        const LocalCraft& c = scene.craft[i];
        ProjectedPoint p = projectPointWithBasis(c.x, c.y, c.z, winW, winH, view, basis);
        if (view.perspective && p.behind) continue;
        if (p.x < -60 || p.x > winW + 60 || p.y < -60 || p.y > winH + 60) continue;
        if (occ(c.x, c.y, c.z)) continue;   // корабль за веществом звезды — скрыт
        // Экранный курс носа: по вектору скорости, спроецированному в экран (робастно
        // для обоих режимов — проецируем точку впереди по скорости).
        double sa = 0.0;
        double vlen = std::sqrt(c.vx * c.vx + c.vy * c.vy + c.vz * c.vz);
        if (vlen > 1e-6) {
            double vhx = c.vx / vlen, vhy = c.vy / vlen, vhz = c.vz / vlen;
            ProjectedPoint pn = projectPointWithBasis(c.x + vhx * 3.0, c.y + vhy * 3.0, c.z + vhz * 3.0,
                                                      winW, winH, view, basis);
            if (!(view.perspective && pn.behind))
                sa = std::atan2(double(pn.y - p.y), double(pn.x - p.x));
        }
        SDL_Color col = c.hostile ? P.red : rgba(c.r, c.g, c.b, 255);
        double sz;
        if (view.perspective) sz = std::max(4.0, std::min(40.0, double(radiusPx(1.6, p.depth, view))));
        else                  sz = std::max(6.0, std::min(9.0, view.scale * 1.6));
        headingTriangle(renderer, p.x, p.y, sa, sz, col, true);
        if (c.shield > 0.0 && c.maxShield > 0.0) {
            int sha = int(70.0 * c.shield / c.maxShield); // бледный синеватый щит
            strokeCircle(renderer, p.x, p.y, int(sz) + 4, rgba(120, 180, 255, sha));
        }
        if (c.errand == 1) { // «у причала»: пульсирующее янтарное кольцо-швартовка (§5.13.10)
            double pl = 0.5 + 0.5 * std::sin(scene.fxClock * 3.0 + double(i) * 1.7);
            int da = 60 + int(pl * 120.0);                 // 60..180 — мягкий «дыхательный» пульс
            strokeCircle(renderer, p.x, p.y, int(sz) + 7, rgba(255, 190, 60, da));
            strokeCircle(renderer, p.x, p.y, int(sz) + 10, rgba(255, 190, 60, da / 3));
        }
        if (c.underAttack) { // (§5.13.11) маяк бедствия: быстрый красный «SOS»-пульс вокруг жертвы
            double pl = 0.5 + 0.5 * std::sin(scene.fxClock * 6.0 + double(i)); // быстрее/ярче швартовки
            int da = 90 + int(pl * 140.0);                 // 90..230 — тревожный
            strokeCircle(renderer, p.x, p.y, int(sz) + 9,  rgba(238, 88, 82, da));
            strokeCircle(renderer, p.x, p.y, int(sz) + 13, rgba(238, 88, 82, da / 3));
        }
        if (c.defending) { // (§5.13.12) эскорт: зелёный пульс вокруг патруля, идущего на перехват («свои»)
            double pl = 0.5 + 0.5 * std::sin(scene.fxClock * 4.5 + double(i) * 0.9); // ≠ SOS-красный/янтарь
            int da = 80 + int(pl * 120.0);                 // 80..200 — уверенный
            strokeCircle(renderer, p.x, p.y, int(sz) + 8,  rgba(90, 220, 130, da));       // вне cyan-рамки цели (sz+6)
            strokeCircle(renderer, p.x, p.y, int(sz) + 13, rgba(90, 220, 130, da / 3));
        }
        if (c.hullHP < c.maxHullHP) {
            double frac = c.hullHP / std::max(1.0, c.maxHullHP);
            bar(renderer, p.x - 8, p.y - int(sz) - 6, 16, 2, frac, c.hostile ? P.red : P.green);
        }
        double cdist = std::sqrt((c.x - scene.px) * (c.x - scene.px) +
                                 (c.y - scene.py) * (c.y - scene.py) +
                                 (c.z - scene.pz) * (c.z - scene.pz));
        if (((view.perspective ? cdist < 700.0 : view.scale >= 5.0) || cdist < 90.0) && !c.label.empty()) {
            drawText(renderer, p.x - textWidth(c.label, 1) / 2, p.y + int(sz) + 3, c.label, P.dim, 1);
        }
        if ((int)i == scene.targetCraft) {
            int bx = int(sz) + 6;
            strokeRect(renderer, p.x - bx, p.y - bx, 2 * bx, 2 * bx, P.cyan);
            // Жёсткий захват игрока (Tab): удвоенная рамка + угловые засечки.
            if (scene.lockTarget >= 0 && (int)i == scene.lockTarget) {
                int bx2 = bx + 5;
                strokeRect(renderer, p.x - bx2, p.y - bx2, 2 * bx2, 2 * bx2,
                           rgba(P.cyan.r, P.cyan.g, P.cyan.b, 170));
                int tick = 4;
                SDL_SetRenderDrawColor(renderer, P.cyan.r, P.cyan.g, P.cyan.b, 255);
                SDL_RenderDrawLine(renderer, p.x - bx2, p.y - bx2, p.x - bx2 + tick, p.y - bx2);
                SDL_RenderDrawLine(renderer, p.x - bx2, p.y - bx2, p.x - bx2, p.y - bx2 + tick);
                SDL_RenderDrawLine(renderer, p.x + bx2, p.y - bx2, p.x + bx2 - tick, p.y - bx2);
                SDL_RenderDrawLine(renderer, p.x + bx2, p.y - bx2, p.x + bx2, p.y - bx2 + tick);
                SDL_RenderDrawLine(renderer, p.x - bx2, p.y + bx2, p.x - bx2 + tick, p.y + bx2);
                SDL_RenderDrawLine(renderer, p.x - bx2, p.y + bx2, p.x - bx2, p.y + bx2 - tick);
                SDL_RenderDrawLine(renderer, p.x + bx2, p.y + bx2, p.x + bx2 - tick, p.y + bx2);
                SDL_RenderDrawLine(renderer, p.x + bx2, p.y + bx2, p.x + bx2, p.y + bx2 - tick);
            }
        }
    }

    // (7) СНАРЯДЫ: короткая яркая линия назад по вектору скорости (проецируем хвост).
    for (size_t i = 0; i < scene.shots.size(); ++i) {
        const LocalProjectile& s = scene.shots[i];
        ProjectedPoint p = projectPointWithBasis(s.x, s.y, s.z, winW, winH, view, basis);
        if (view.perspective && p.behind) continue;
        if (p.x < -20 || p.x > winW + 20 || p.y < -20 || p.y > winH + 20) continue;
        if (occ(s.x, s.y, s.z)) continue;   // снаряд за звездой — скрыт
        SDL_Color col = (s.team == 0) ? P.cyan : P.red;
        double vl = std::sqrt(s.vx * s.vx + s.vy * s.vy + s.vz * s.vz);
        if (vl > 1e-6) {
            double k = 2.0 / vl;
            ProjectedPoint p2 = projectPointWithBasis(s.x - s.vx * k, s.y - s.vy * k, s.z - s.vz * k,
                                                      winW, winH, view, basis);
            if (!(view.perspective && p2.behind)) {
                SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 255);
                SDL_RenderDrawLine(renderer, p.x, p.y, p2.x, p2.y);
            }
        }
        fillRect(renderer, p.x - 1, p.y - 1, 2, 2, col);
    }

    // (7b) ЧАСТИЦЫ ПОВЕРХ: искры попаданий, ударные кольца, вспышки дул.
    for (size_t i = 0; i < scene.fx.size(); ++i) {
        const LocalFx& fx = scene.fx[i];
        if (fx.kind == FX_SPARK || fx.kind == FX_RING || fx.kind == FX_MUZZLE) drawParticle(fx);
    }

    // (8) ИГРОК.
    //  • КАРТА (орто, вид сверху): свой корабль — тёплая стрелка по курсу носа +
    //    кольцо щита + прицел по направлению стрельбы (тактический вид).
    //  • КОКПИТ (перспектива, вид «из глаз»): глаз камеры В корабле, поэтому сам
    //    корабль НЕ рисуем. Вместо него неподвижная «мушка» по курсу стрельбы (центр
    //    экрана) + маркер вектора скорости (куда несёт по инерции, как flight-path в
    //    Elite) + дуга щита у мушки. Крен читается по повороту скайбокса/мира.
    if (!view.perspective) {
        ProjectedPoint pp = projectPointWithBasis(scene.px, scene.py, scene.pz, winW, winH, view, basis);
        ProjectedPoint pn = projectPointWithBasis(scene.px + scene.pfwdX * 6.0,
                                                  scene.py + scene.pfwdY * 6.0,
                                                  scene.pz + scene.pfwdZ * 6.0,
                                                  winW, winH, view, basis);
        double psa = std::atan2(double(pn.y - pp.y), double(pn.x - pp.x));
        headingTriangle(renderer, pp.x, pp.y, psa, 11.0, rgba(255, 230, 120, 255), true);
        headingTriangle(renderer, pp.x, pp.y, psa, 11.0, rgba(255, 255, 255, 180), false);
        if (scene.pShield > 0.0 && scene.pMaxShield > 0.0) {
            int psha = int(120.0 * scene.pShield / scene.pMaxShield); // бледный циан-щит игрока
            strokeCircle(renderer, pp.x, pp.y, 14, rgba(P.cyan.r, P.cyan.g, P.cyan.b, psha));
            strokeCircle(renderer, pp.x, pp.y, 15, rgba(P.cyan.r, P.cyan.g, P.cyan.b, psha / 2));
        }
        int rx = pp.x + int(std::cos(psa) * 40.0);
        int ry = pp.y + int(std::sin(psa) * 40.0);
        SDL_Color ret = rgba(P.cyan.r, P.cyan.g, P.cyan.b, 120);
        strokeCircle(renderer, rx, ry, 5, ret);
        SDL_SetRenderDrawColor(renderer, ret.r, ret.g, ret.b, ret.a);
        SDL_RenderDrawLine(renderer, rx - 8, ry, rx - 3, ry);
        SDL_RenderDrawLine(renderer, rx + 3, ry, rx + 8, ry);
        SDL_RenderDrawLine(renderer, rx, ry - 8, rx, ry - 3);
        SDL_RenderDrawLine(renderer, rx, ry + 3, rx, ry + 8);
    } else {
        // (8a) ОРИЕНТИР ЭКЛИПТИКИ (искусственный горизонт). Плоскость системы — мировая
        //      X-Y (нормаль +Z; см. localgen: x=r·cosφ, y=r·sinφ·cosI, z=r·sinφ·sinI).
        //      Её «линия схода» на экране: НАКЛОН линии = крен корабля, СМЕЩЕНИЕ вверх/вниз
        //      относительно мушки = тангаж относительно диска системы. Даёт чувство
        //      ориентации в 6DOF-полёте. Строим как проекцию НАПРАВЛЕНИЙ в плоскости
        //      (точки на бесконечности) тем же projectDirectionWithBasis, что и скайбокс:
        //      d(θ)=(cosθ,sinθ,0). В глубоком космосе опорной плоскости нет — пропускаем.
        if (scene.hasStar) {
            const int N = 84;
            const double kTwoPi = 6.28318530717958648;
            const SDL_Color ec = rgba(120, 150, 190, 70);   // тусклый холодный ориентир
            SDL_SetRenderDrawColor(renderer, ec.r, ec.g, ec.b, ec.a);
            ProjectedPoint prev; bool havePrev = false;
            for (int i = 0; i <= N; ++i) {
                const double th = (double(i) / double(N)) * kTwoPi;
                ProjectedPoint d = projectDirectionWithBasis(std::cos(th), std::sin(th), 0.0,
                                                             winW, winH, view, basis);
                // camZ>0.14: отсекаем почти-рёберные направления (линия уходит в бесконечность
                // у горизонта) — иначе focal/camZ выбрасывает соседние точки в разные стороны
                // за экран => длинные «спицы». Порог поднят с 0.06 => лучей у горизонта нет.
                const bool ok = (!d.behind) && (d.camZ > 0.14) &&
                                d.x > -winW && d.x < 2 * winW && d.y > -winH && d.y < 2 * winH;
                if (ok && havePrev)
                    SDL_RenderDrawLine(renderer, prev.x, prev.y, d.x, d.y);
                prev = d; havePrev = ok;
            }
        }
        // Мушка по курсу стрельбы: точка далеко вдоль носа (~центр экрана при нулевом сносе).
        ProjectedPoint pa = projectPointWithBasis(scene.px + scene.pfwdX * 600.0,
                                                  scene.py + scene.pfwdY * 600.0,
                                                  scene.pz + scene.pfwdZ * 600.0,
                                                  winW, winH, view, basis);
        int rx = pa.behind ? cx : pa.x;
        int ry = pa.behind ? cy : pa.y;
        SDL_Color ret = rgba(P.cyan.r, P.cyan.g, P.cyan.b, 175);
        strokeCircle(renderer, rx, ry, 6, ret);
        SDL_SetRenderDrawColor(renderer, ret.r, ret.g, ret.b, ret.a);
        SDL_RenderDrawLine(renderer, rx - 11, ry, rx - 4, ry);
        SDL_RenderDrawLine(renderer, rx + 4, ry, rx + 11, ry);
        SDL_RenderDrawLine(renderer, rx, ry - 11, rx, ry - 4);
        SDL_RenderDrawLine(renderer, rx, ry + 4, rx, ry + 11);
        SDL_RenderDrawPoint(renderer, rx, ry);
        if (scene.pShield > 0.0 && scene.pMaxShield > 0.0) {   // дуга щита у мушки
            int psha = int(150.0 * scene.pShield / scene.pMaxShield);
            strokeCircle(renderer, rx, ry, 10, rgba(P.cyan.r, P.cyan.g, P.cyan.b, psha));
        }
        // Маркер вектора скорости (prograde): куда реально несёт по инерции.
        double sp = std::sqrt(scene.pvx * scene.pvx + scene.pvy * scene.pvy + scene.pvz * scene.pvz);
        if (sp > 0.5) {
            const double k = 600.0 / sp;
            ProjectedPoint pv = projectPointWithBasis(scene.px + scene.pvx * k,
                                                      scene.py + scene.pvy * k,
                                                      scene.pz + scene.pvz * k,
                                                      winW, winH, view, basis);
            if (!pv.behind) {
                SDL_Color vg = rgba(150, 235, 170, 160); // мягкий зелёный (прогрейд)
                strokeCircle(renderer, pv.x, pv.y, 4, vg);
                SDL_SetRenderDrawColor(renderer, vg.r, vg.g, vg.b, vg.a);
                SDL_RenderDrawLine(renderer, pv.x - 7, pv.y, pv.x - 4, pv.y);
                SDL_RenderDrawLine(renderer, pv.x + 4, pv.y, pv.x + 7, pv.y);
                SDL_RenderDrawLine(renderer, pv.x, pv.y - 7, pv.x, pv.y - 4);
            }
        }
        // Лид-маркер (упреждение): куда целить, чтобы попасть по движущейся цели.
        // Снаряд игрока несёт скорость корабля (pv) + PROJ_SPEED вдоль носа (localsim),
        // поэтому решаем перехват в относительной скорости u = targetVel − playerVel:
        //   |r + u·t| = S·t  →  (u·u − S²)t² + 2(r·u)t + r·r = 0,  берём наименьший t>0.
        // Целевое направление ∝ (r + u·t); ставим пип на том же радиусе 600 LU, что и мушка.
        if (scene.targetCraft >= 0 && (size_t)scene.targetCraft < scene.craft.size()) {
            const LocalCraft& tc = scene.craft[scene.targetCraft];
            const double rxL = tc.x - scene.px, ryL = tc.y - scene.py, rzL = tc.z - scene.pz;
            const double distL = std::sqrt(rxL * rxL + ryL * ryL + rzL * rzL);
            if (distL < 1500.0) {   // показываем только на дистанции завязки боя
                const double uxL = tc.vx - scene.pvx, uyL = tc.vy - scene.pvy, uzL = tc.vz - scene.pvz;
                const double S = LocalCfg::PROJ_SPEED;
                const double a = uxL * uxL + uyL * uyL + uzL * uzL - S * S;
                const double b = 2.0 * (rxL * uxL + ryL * uyL + rzL * uzL);
                const double cc = rxL * rxL + ryL * ryL + rzL * rzL;
                double tHit = -1.0;
                if (std::fabs(a) < 1e-6) {
                    if (std::fabs(b) > 1e-9) tHit = -cc / b;
                } else {
                    const double disc = b * b - 4.0 * a * cc;
                    if (disc >= 0.0) {
                        const double sq = std::sqrt(disc);
                        const double t1 = (-b - sq) / (2.0 * a), t2 = (-b + sq) / (2.0 * a);
                        tHit = 1e30;
                        if (t1 > 1e-4 && t1 < tHit) tHit = t1;
                        if (t2 > 1e-4 && t2 < tHit) tHit = t2;
                        if (tHit >= 1e30) tHit = -1.0;
                    }
                }
                if (tHit > 0.0) {
                    const double axL = rxL + uxL * tHit, ayL = ryL + uyL * tHit, azL = rzL + uzL * tHit;
                    const double al = std::sqrt(axL * axL + ayL * ayL + azL * azL);
                    if (al > 1e-6) {
                        const double k2 = 600.0 / al;
                        ProjectedPoint lp = projectPointWithBasis(scene.px + axL * k2, scene.py + ayL * k2,
                                                                  scene.pz + azL * k2, winW, winH, view, basis);
                        if (!lp.behind) {
                            SDL_Color lc = tc.hostile ? rgba(255, 150, 90, 205)   // амбер-красный (враг)
                                                      : rgba(240, 205, 130, 185); // янтарный (нейтрал)
                            strokeCircle(renderer, lp.x, lp.y, 5, lc);
                            SDL_SetRenderDrawColor(renderer, lc.r, lc.g, lc.b, lc.a);
                            SDL_RenderDrawLine(renderer, lp.x - 9, lp.y, lp.x - 5, lp.y);
                            SDL_RenderDrawLine(renderer, lp.x + 5, lp.y, lp.x + 9, lp.y);
                            SDL_RenderDrawLine(renderer, lp.x, lp.y - 9, lp.x, lp.y - 5);
                            SDL_RenderDrawLine(renderer, lp.x, lp.y + 5, lp.x, lp.y + 9);
                        }
                    }
                }
            }
        }
    }

    // Маркеры off-screen целей: ближайший рынок (зелёный), враг (красный), лут (янтарь).
    {
        int nearMkt = -1; double bestMkt = 1e18;
        for (size_t i = 0; i < scene.bodies.size(); ++i) {
            if (!scene.bodies[i].hasMarket) continue;
            const LocalBody& bd = scene.bodies[i];
            double d = (bd.x - scene.px) * (bd.x - scene.px) + (bd.y - scene.py) * (bd.y - scene.py) +
                       (bd.z - scene.pz) * (bd.z - scene.pz);
            if (d < bestMkt) { bestMkt = d; nearMkt = (int)i; }
        }
        int nearHos = -1; double bestHos = 1e18;
        for (size_t i = 0; i < scene.craft.size(); ++i) {
            if (!scene.craft[i].hostile) continue;
            const LocalCraft& c = scene.craft[i];
            double d = (c.x - scene.px) * (c.x - scene.px) + (c.y - scene.py) * (c.y - scene.py) +
                       (c.z - scene.pz) * (c.z - scene.pz);
            if (d < bestHos) { bestHos = d; nearHos = (int)i; }
        }
        int nearLoot = -1; double bestLoot = 1e18;
        for (size_t i = 0; i < scene.loot.size(); ++i) {
            const LocalLoot& lt = scene.loot[i];
            double d = (lt.x - scene.px) * (lt.x - scene.px) + (lt.y - scene.py) * (lt.y - scene.py) +
                       (lt.z - scene.pz) * (lt.z - scene.pz);
            if (d < bestLoot) { bestLoot = d; nearLoot = (int)i; }
        }
        int nearVic = -1; double bestVic = 1e18; // (§5.13.11) ближайшая жертва бедствия
        for (size_t i = 0; i < scene.craft.size(); ++i) {
            if (!scene.craft[i].underAttack || scene.craft[i].hullHP <= 0.0) continue;
            const LocalCraft& c = scene.craft[i];
            double d = (c.x - scene.px) * (c.x - scene.px) + (c.y - scene.py) * (c.y - scene.py) +
                       (c.z - scene.pz) * (c.z - scene.pz);
            if (d < bestVic) { bestVic = d; nearVic = (int)i; }
        }
        int nearDef = -1; double bestDef = 1e18; // (§5.13.12) ближайший патруль-эскорт (идёт на перехват)
        for (size_t i = 0; i < scene.craft.size(); ++i) {
            if (!scene.craft[i].defending || scene.craft[i].hullHP <= 0.0) continue;
            const LocalCraft& c = scene.craft[i];
            double d = (c.x - scene.px) * (c.x - scene.px) + (c.y - scene.py) * (c.y - scene.py) +
                       (c.z - scene.pz) * (c.z - scene.pz);
            if (d < bestDef) { bestDef = d; nearDef = (int)i; }
        }
        if (nearMkt >= 0) {
            const LocalBody& bd = scene.bodies[nearMkt];
            drawEdgeMarker(renderer, cx, cy, winW, winH, bd.x, bd.y, bd.z, view, basis, P.green);
        }
        if (nearHos >= 0) {
            const LocalCraft& c = scene.craft[nearHos];
            drawEdgeMarker(renderer, cx, cy, winW, winH, c.x, c.y, c.z, view, basis, P.red);
        }
        if (nearVic >= 0) { // (§5.13.11) краевой маркер бедствия — ведёт к жертве за кадром (SOS-красный)
            const LocalCraft& c = scene.craft[nearVic];
            drawEdgeMarker(renderer, cx, cy, winW, winH, c.x, c.y, c.z, view, basis, rgba(238, 88, 82, 255));
        }
        if (nearDef >= 0) { // (§5.13.12) краевой маркер эскорта — «помощь идёт» из-за кадра (зелёный «свои»)
            const LocalCraft& c = scene.craft[nearDef];
            drawEdgeMarker(renderer, cx, cy, winW, winH, c.x, c.y, c.z, view, basis, rgba(90, 220, 130, 255));
        }
        if (nearLoot >= 0) {
            const LocalLoot& lt = scene.loot[nearLoot];
            drawEdgeMarker(renderer, cx, cy, winW, winH, lt.x, lt.y, lt.z, view, basis, P.amber);
        }
    }

    // (8b) РАДИОИСТОЧНИКИ (мир): проявленные и незабранные. На экране — пульсирующий
    //      ромбик + метка + дистанция в LU; за кадром — краевой маркер в цвете типа.
    {
        auto drawDiamond = [&](int x, int y, int s, SDL_Color c, bool fill) {
            if (fill) {
                fillTriangle(renderer, x, y - s, x + s, y, x, y + s, c);
                fillTriangle(renderer, x, y - s, x, y + s, x - s, y, c);
            } else {
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
                SDL_RenderDrawLine(renderer, x, y - s, x + s, y);
                SDL_RenderDrawLine(renderer, x + s, y, x, y + s);
                SDL_RenderDrawLine(renderer, x, y + s, x - s, y);
                SDL_RenderDrawLine(renderer, x - s, y, x, y - s);
            }
        };
        char rbuf[48];
        for (size_t i = 0; i < scene.radio.size(); ++i) {
            const LocalRadioSource& rs = scene.radio[i];
            if (!rs.revealed || rs.resolved) continue;
            SDL_Color kc = radioColor(rs.kind);
            double s01 = rs.strength < 0.0 ? 0.0 : (rs.strength > 1.0 ? 1.0 : rs.strength);
            double pulse = 0.5 + 0.5 * std::sin(scene.fxClock * 4.0 + double(i));
            int a = 120 + int(pulse * (80.0 + 55.0 * s01)); // ~120..255
            ProjectedPoint p = projectPointWithBasis(rs.x, rs.y, rs.z, winW, winH, view, basis);
            bool on = !occ(rs.x, rs.y, rs.z) &&   // за звездой — не блип, а пеленг на кромке
                      !(view.perspective && p.behind) &&
                      (p.x >= 0 && p.x <= winW && p.y >= 0 && p.y <= winH);
            if (on) {
                drawDiamond(p.x, p.y, 6, rgba(kc.r, kc.g, kc.b, a), true);
                drawDiamond(p.x, p.y, 9, rgba(kc.r, kc.g, kc.b, a / 2), false);
                if (!rs.label.empty())
                    drawText(renderer, p.x - textWidth(rs.label, 1) / 2, p.y + 12, rs.label,
                             rgba(kc.r, kc.g, kc.b, 255), 1);
                double d = std::sqrt((rs.x - scene.px) * (rs.x - scene.px) +
                                     (rs.y - scene.py) * (rs.y - scene.py) +
                                     (rs.z - scene.pz) * (rs.z - scene.pz));
                std::snprintf(rbuf, sizeof(rbuf), "%d LU", int(d));
                drawText(renderer, p.x - textWidth(rbuf, 1) / 2, p.y + 22, rbuf, P.dim, 1);
            } else {
                drawEdgeMarker(renderer, cx, cy, winW, winH, rs.x, rs.y, rs.z, view, basis,
                               rgba(kc.r, kc.g, kc.b, a));
            }
        }
    }

    // (7c) ВАРП-ПОЛОСЫ: радиальные штрихи из центра при активном warp (суперкруиз).
    if (scene.warping) {
        double half = std::sqrt(double(winW) * winW + double(winH) * winH) * 0.5;
        double rInner = 60.0, rOuter = half;
        for (int i = 0; i < 40; ++i) {
            double ai = i * 0.157;                       // детерминированный угол
            double ca = std::cos(ai), sa = std::sin(ai);
            unsigned h = (unsigned)(i + 1) * 2654435761u; h ^= h >> 13; h *= 2246822519u; h ^= h >> 15;
            double stream = std::fmod(scene.fxClock * 260.0 + double(h % 46u), 46.0); // бег наружу
            double rin = rInner + stream;
            double rout = rOuter * (0.72 + 0.28 * (double((h >> 7) % 100u) / 100.0));
            int x1 = cx + int(ca * rin), y1 = cy + int(sa * rin);
            int x2 = cx + int(ca * rout), y2 = cy + int(sa * rout);
            int a = 40 + int((h >> 3) % 46u);            // низкая альфа циан/белый
            SDL_SetRenderDrawColor(renderer, 160, 220, 255, (Uint8)a);
            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        }
    }

    // (6) ВИГНЕТКА УРОНА: красные полосы по краям при вспышке урона / низком корпусе.
    double hullFrac = 1.0;
    if (game.playerAgent >= 0 && (size_t)game.playerAgent < game.agents.size()) {
        const Ship& s = game.agents[game.playerAgent].ship;
        hullFrac = s.hullHP / std::max(1.0, s.maxHullHP);
    }
    {
        double hitA = scene.playerHitFlash / 0.6;
        double lowA = (hullFrac < 0.30) ? (0.30 - hullFrac) / 0.30 * 0.7 : 0.0;
        double vg = hitA > lowA ? hitA : lowA;
        if (vg < 0.0) vg = 0.0; else if (vg > 1.0) vg = 1.0;
        if (vg > 0.0) {
            double pulse = 0.85 + 0.15 * std::sin(scene.fxClock * 6.0); // мягкая пульсация
            int a = int(vg * 150.0 * pulse);
            int t = 26;
            SDL_Color vc = rgba(220, 42, 36, a);
            fillRect(renderer, 0, 0, winW, t, vc);
            fillRect(renderer, 0, winH - t, winW, t, vc);
            fillRect(renderer, 0, 0, t, winH, vc);
            fillRect(renderer, winW - t, 0, t, winH, vc);
        }
    }

    // ------------------------------- HUD -------------------------------
    char buf[192];

    // Верх-слева: система / локальное время / warp.
    panel(renderer, 12, 12, 230, 66);
    std::string sysName = "DEEP SPACE";
    if (scene.starIndex >= 0 && (size_t)scene.starIndex < game.cluster.stars.size())
        sysName = game.cluster.stars[scene.starIndex].name;
    drawText(renderer, 20, 18, sysName, P.text, 2);
    int days = int(scene.localHours / 24.0);
    int hrs = int(std::fmod(scene.localHours, 24.0));
    std::snprintf(buf, sizeof(buf), "T +%dD %02dH", days, hrs);
    drawText(renderer, 20, 40, buf, P.dim, 1);
    if (scene.warping) {
        std::snprintf(buf, sizeof(buf), "WARP X%d", (int)LocalCfg::WARP_MULT);
        drawText(renderer, 120, 40, buf, P.amber, 1);
    } else {
        drawText(renderer, 120, 40, "WARP OFF", P.dim, 1);
    }

    // Низ-слева: скорость + ЩИТ + корпус (панель выше, чтобы вместить три ряда).
    int blY = winH - 130;
    panel(renderer, 12, blY, 236, 104);
    double speed = std::sqrt(scene.pvx * scene.pvx + scene.pvy * scene.pvy + scene.pvz * scene.pvz);
    double maxSpd = std::max(1.0, scene.playerMaxSpeed);
    std::snprintf(buf, sizeof(buf), "SPD %.0F / %.0F LU/H", speed, scene.playerMaxSpeed);
    drawText(renderer, 20, blY + 8, buf, P.text, 1);
    bar(renderer, 20, blY + 20, 220, 6, speed / maxSpd, P.cyan);
    {
        // Щит — поглощает урон до корпуса; синеватый/циановый ряд над корпусом.
        double sfrac = (scene.pMaxShield > 0.0) ? scene.pShield / scene.pMaxShield : 0.0;
        std::snprintf(buf, sizeof(buf), "SHD %.0F/%.0F", scene.pShield, scene.pMaxShield);
        drawText(renderer, 20, blY + 40, buf, rgba(120, 200, 255, 255), 1);
        bar(renderer, 20, blY + 52, 220, 6, sfrac, rgba(90, 170, 255, 255));
    }
    if (game.playerAgent >= 0 && (size_t)game.playerAgent < game.agents.size()) {
        const Ship& s = game.agents[game.playerAgent].ship;
        double frac = s.hullHP / std::max(1.0, s.maxHullHP);
        std::snprintf(buf, sizeof(buf), "HULL %.0F/%.0F", s.hullHP, s.maxHullHP);
        drawText(renderer, 20, blY + 72, buf, P.text, 1);
        SDL_Color hc = (frac > 0.5) ? P.green : (frac > 0.25 ? P.amber : P.red);
        bar(renderer, 20, blY + 84, 220, 6, frac, hc);
    }

    // Верх-справа: цель.
    if (scene.targetCraft >= 0 && (size_t)scene.targetCraft < scene.craft.size()) {
        const LocalCraft& t = scene.craft[scene.targetCraft];
        double dist = std::sqrt((t.x - scene.px) * (t.x - scene.px) +
                                (t.y - scene.py) * (t.y - scene.py) +
                                (t.z - scene.pz) * (t.z - scene.pz));
        int tx = winW - 236, ty = 12;
        const bool hasShield = t.maxShield > 0.0;
        const bool sos = t.underAttack;   // (§5.13.11) цель — жертва бедствия
        const bool esc = t.defending;     // (§5.13.12) цель — патруль-эскорт на перехвате (взаимоискл. с sos)
        int ph = hasShield ? 72 : 60;
        panel(renderer, tx, ty, 224, ph);
        if (sos) {   // пульсирующая SOS-красная рамка вокруг панели — «эта цель под атакой»
            double pl = 0.5 + 0.5 * std::sin(scene.fxClock * 6.0);
            strokeRect(renderer, tx - 2, ty - 2, 224 + 4, ph + 4, rgba(238, 88, 82, 120 + int(pl * 135.0)));
            strokeRect(renderer, tx - 3, ty - 3, 224 + 6, ph + 6, rgba(238, 88, 82, 60));
        } else if (esc) {   // зелёная рамка — «цель на нашей стороне, идёт на перехват налётчика»
            double pl = 0.5 + 0.5 * std::sin(scene.fxClock * 4.5);
            strokeRect(renderer, tx - 2, ty - 2, 224 + 4, ph + 4, rgba(90, 220, 130, 110 + int(pl * 120.0)));
            strokeRect(renderer, tx - 3, ty - 3, 224 + 6, ph + 6, rgba(90, 220, 130, 55));
        }
        drawText(renderer, tx + 8, ty + 8, t.label.empty() ? std::string("CONTACT") : t.label,
                 sos ? rgba(238, 88, 82, 255) : (esc ? rgba(90, 220, 130, 255) : (t.hostile ? P.red : P.text)), 1);
        if (scene.lockTarget >= 0 && scene.lockTarget == scene.targetCraft) {
            drawText(renderer, tx + 224 - 8 - textWidth("LOCK", 1), ty + 8, "LOCK", P.cyan, 1);
        } else if (sos) {   // «SOS»-чип, если панель не занята значком захвата
            drawText(renderer, tx + 224 - 8 - textWidth("SOS", 1), ty + 8, "SOS", rgba(238, 88, 82, 255), 1);
        } else if (esc) {   // «ESCORT»-чип для дружественного перехватчика
            drawText(renderer, tx + 224 - 8 - textWidth("ESCORT", 1), ty + 8, "ESCORT", rgba(90, 220, 130, 255), 1);
        }
        std::snprintf(buf, sizeof(buf), "DIST %.0F LU", dist);
        drawText(renderer, tx + 8, ty + 22, buf, P.dim, 1);
        // Скорость сближения: closing = −d(dist)/dt = −dot(r,u)/dist, u = targetVel − playerVel.
        // Плюс = сближаемся (янтарь), минус = расходимся (серый).
        double closing = 0.0;
        if (dist > 1e-6) {
            const double ux = t.vx - scene.pvx, uy = t.vy - scene.pvy, uz = t.vz - scene.pvz;
            closing = -((t.x - scene.px) * ux + (t.y - scene.py) * uy + (t.z - scene.pz) * uz) / dist;
        }
        std::snprintf(buf, sizeof(buf), "CLS %+.0F", closing);
        drawText(renderer, tx + 224 - 8 - textWidth(buf, 1), ty + 22, buf, closing >= 0.0 ? P.amber : P.dim, 1);
        int by = ty + 38;
        if (hasShield) {   // щит цели (циан), над корпусом
            bar(renderer, tx + 8, by, 208, 6, t.shield / std::max(1.0, t.maxShield), P.cyan);
            by += 12;
        }
        double frac = t.hullHP / std::max(1.0, t.maxHullHP);
        bar(renderer, tx + 8, by, 208, 6, frac, t.hostile ? P.red : P.green);
    }

    // Индикатор добычи (по центру, над подсказкой стыковки).
    if (scene.miningRock >= 0 && scene.miningRock < (int)scene.rocks.size()) {
        // (§5.13.16) Класс породы в индикаторе: игрок видит, что даёт текущая глыба (металл богаче).
        const LocalRock& mr = scene.rocks[scene.miningRock];
        std::snprintf(buf, sizeof(buf), "MINING %s  +%.0F",
                      rockClassName(rockClass(mr.element)), scene.miningAccum);
        drawText(renderer, cx - textWidth(buf, 2) / 2, winH - 182, buf, P.amber, 2);
    }

    // Подсказка стыковки.
    if (scene.dockPrompt >= 0 && (size_t)scene.dockPrompt < scene.bodies.size()) {
        std::string msg = std::string("PRESS K TO DOCK - ") + scene.bodies[scene.dockPrompt].name;
        int w = textWidth(msg, 2);
        int x = cx - w / 2, y = winH - 152;
        panel(renderer, x - 10, y - 6, w + 20, 26);
        drawText(renderer, x, y, msg, P.green, 2);
    }

    // Подсказка добычи: доступен ближайший астероид (если ещё не добываем).
    if (scene.minePrompt >= 0 && scene.miningRock < 0) {
        const char* msg = "PRESS M TO MINE";
        drawText(renderer, cx - textWidth(msg, 2) / 2, winH - 124, msg, P.amber, 2);
    }

    // Toast (всплывающее событие) — сверху по центру.
    if (scene.toastTimer > 0.0 && !scene.toast.empty()) {
        drawText(renderer, cx - textWidth(scene.toast, 2) / 2, 24, scene.toast, P.text, 2);
    }

    // Низ-СПРАВА: РАДАР / ДЕТЕКТОР. Тактический вид от кабины: ось вперёд (pfwd) —
    // ВВЕРХ радара, ось right (pfwd×pup) — вправо. Контакт впереди по курсу -> вверх.
    {
        int rw = 154, rh = 178;     // rh: +20 под строку трафика над счётчиком SIG (§5.13.10)
        int rpx = winW - rw - 12;   // левый край панели  = winW-166
        int rpy = winH - rh - 52;   // верхний край       = winH-230
        panel(renderer, rpx, rpy, rw, rh);
        int rcx = rpx + rw / 2;
        int rcy = rpy + 80;         // центр радара (место под заголовок сверху и счётчик снизу)
        int R = 54;                 // экранный радиус радара

        std::snprintf(buf, sizeof(buf), "DETECTOR T%d", scene.detectorTier);
        drawText(renderer, rcx - textWidth(buf, 2) / 2, rpy + 8, buf, P.cyan, 2);

        // Дальномерные кольца + перекрестье (тусклый циан).
        SDL_Color grid  = rgba(P.cyan.r, P.cyan.g, P.cyan.b, 70);
        SDL_Color grid2 = rgba(P.cyan.r, P.cyan.g, P.cyan.b, 38);
        strokeCircle(renderer, rcx, rcy, R, grid);
        strokeCircle(renderer, rcx, rcy, R / 2, grid2);
        SDL_SetRenderDrawColor(renderer, grid2.r, grid2.g, grid2.b, grid2.a);
        SDL_RenderDrawLine(renderer, rcx - R, rcy, rcx + R, rcy);
        SDL_RenderDrawLine(renderer, rcx, rcy - R, rcx, rcy + R);

        const double RANGE = 1400.0; // мировая дальность радара, LU
        // Тело-относительный базис: right = pfwd × pup; forward = pfwd (вверх радара).
        double rgtX, rgtY, rgtZ;
        localShipRight(scene, rgtX, rgtY, rgtZ);

        // Корабли: мелкие точки (враг красный, иначе серый).
        for (size_t i = 0; i < scene.craft.size(); ++i) {
            const LocalCraft& c = scene.craft[i];
            double dx = c.x - scene.px, dy = c.y - scene.py, dz = c.z - scene.pz;
            double rx = dx * rgtX + dy * rgtY + dz * rgtZ;                       // вправо
            double ry = dx * scene.pfwdX + dy * scene.pfwdY + dz * scene.pfwdZ;  // вперёд (вверх)
            double dd = std::sqrt(rx * rx + ry * ry);
            bool beyond = dd > RANGE;
            double k = beyond ? (double(R) / std::max(1e-6, dd)) : (double(R) / RANGE);
            int bx = rcx + int(rx * k);
            int by = rcy - int(ry * k);
            SDL_Color cc = c.hostile ? P.red : rgba(150, 165, 182, 200);
            if (beyond) strokeCircle(renderer, bx, by, 2, cc);
            else        fillRect(renderer, bx - 1, by - 1, 3, 3, cc);
        }

        // Радиоисточники: заметнее (крупнее, пульсируют), цвет по типу.
        int sigCount = 0;
        for (size_t i = 0; i < scene.radio.size(); ++i) {
            const LocalRadioSource& rs = scene.radio[i];
            if (!rs.revealed || rs.resolved) continue;
            ++sigCount;
            double dx = rs.x - scene.px, dy = rs.y - scene.py, dz = rs.z - scene.pz;
            double rx = dx * rgtX + dy * rgtY + dz * rgtZ;
            double ry = dx * scene.pfwdX + dy * scene.pfwdY + dz * scene.pfwdZ;
            double dd = std::sqrt(rx * rx + ry * ry);
            bool beyond = dd > RANGE;
            double k = beyond ? (double(R) / std::max(1e-6, dd)) : (double(R) / RANGE);
            int bx = rcx + int(rx * k);
            int by = rcy - int(ry * k);
            SDL_Color kc = radioColor(rs.kind);
            double s01 = rs.strength < 0.0 ? 0.0 : (rs.strength > 1.0 ? 1.0 : rs.strength);
            double pulse = 0.5 + 0.5 * std::sin(scene.fxClock * 5.0 + double(i));
            int a = 130 + int(pulse * (70.0 + 55.0 * s01));
            if (beyond) {
                strokeCircle(renderer, bx, by, 3, rgba(kc.r, kc.g, kc.b, a));
            } else {
                fillRect(renderer, bx - 2, by - 2, 4, 4, rgba(kc.r, kc.g, kc.b, a));
                strokeCircle(renderer, bx, by, 4, rgba(kc.r, kc.g, kc.b, a / 2));
            }
        }

        // Игрок — крошечный треугольник в центре, нос ВВЕРХ.
        headingTriangle(renderer, rcx, rcy, -1.5707963, 5.0, rgba(255, 230, 120, 255), true);

        // Тальник трафика (§5.13.10): сколько неагрессивных бортов идут к рынку (IN) и стоят
        // у причала (DOCK). Делает живую популяцию §5.13.9 читаемой из кабины (scale 1, над SIG).
        int inbound = 0, docked = 0;
        for (size_t i = 0; i < scene.craft.size(); ++i) {
            const LocalCraft& c = scene.craft[i];
            if (c.hostile) continue;
            if (c.errand == 1) ++docked;
            else if (c.errand == 0 && c.errandBody >= 0) ++inbound;
        }
        std::snprintf(buf, sizeof(buf), "IN %d  DOCK %d", inbound, docked);
        SDL_Color trafCol = (inbound + docked > 0) ? rgba(120, 200, 220, 255) : P.dim;
        drawText(renderer, rcx - textWidth(buf, 1) / 2, rpy + rh - 36, buf, trafCol, 1);

        // Счётчик сигналов внизу панели (scale 2). Показываем всегда (виден тир детектора).
        std::snprintf(buf, sizeof(buf), "%d SIG", sigCount);
        SDL_Color sigCol = (sigCount > 0) ? P.amber : P.dim;
        drawText(renderer, rcx - textWidth(buf, 2) / 2, rpy + rh - 20, buf, sigCol, 2);
    }

    // Подсказка управления — вдоль самого низа.
    drawText(renderer, 12, winH - 14,
             "W/S THRUST  A/D YAW  R/F PITCH  Q/E ROLL  SHIFT WARP  M MINE  K DOCK  TAB LOCK  C VIEW  L EXIT",
             P.dim, 1);
}
