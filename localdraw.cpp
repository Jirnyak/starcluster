#include "local.h"
#include "game.h"
#include "cluster.h"
#include "camera.h"
#include "render2d.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
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

    // (0b) ТУМАННОСТЬ: слабый полноэкранный тон (в этом проходе неактивен — strength=0).
    if (scene.nebulaStrength > 0.0) {
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

    // (3) ЗВЕЗДА + свечение (радиус ореола слегка пульсирует по fxClock).
    if (scene.hasStar && !(view.perspective && sp.behind)) {
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

    // (5) АСТЕРОИДЫ.
    for (size_t i = 0; i < scene.rocks.size(); ++i) {
        const LocalRock& rk = scene.rocks[i];
        ProjectedPoint p = projectPointWithBasis(rk.x, rk.y, rk.z, winW, winH, view, basis);
        if (view.perspective && p.behind) continue;
        if (p.x < -100 || p.x > winW + 100 || p.y < -100 || p.y > winH + 100) continue;
        double f = sceneFade(p.depth);
        int r = std::min(400, std::max(1, radiusPx(rk.radius, p.depth, view)));
        fillCircle(renderer, p.x, p.y, r, fade(rk.r, rk.g, rk.b, 255, f));
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
                case FX_MUZZLE:               cap = 90; break; // вспышка у дула — крупная
                default:                      cap = 48; break;
            }
            if (s > cap) s = cap;
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
        if (view.perspective) sz = std::max(4.0, std::min(40.0, double(radiusPx(3.0, p.depth, view))));
        else                  sz = std::max(6.0, std::min(9.0, view.scale * 1.6));
        headingTriangle(renderer, p.x, p.y, sa, sz, col, true);
        if (c.shield > 0.0 && c.maxShield > 0.0) {
            int sha = int(70.0 * c.shield / c.maxShield); // бледный синеватый щит
            strokeCircle(renderer, p.x, p.y, int(sz) + 4, rgba(120, 180, 255, sha));
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
                // camZ>0.06: отсекаем почти-рёберные направления (линия уходит в бесконечность
                // у горизонта) — иначе focal/camZ раздувает координату в штрих на весь экран.
                const bool ok = (!d.behind) && (d.camZ > 0.06) &&
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
        if (nearMkt >= 0) {
            const LocalBody& bd = scene.bodies[nearMkt];
            drawEdgeMarker(renderer, cx, cy, winW, winH, bd.x, bd.y, bd.z, view, basis, P.green);
        }
        if (nearHos >= 0) {
            const LocalCraft& c = scene.craft[nearHos];
            drawEdgeMarker(renderer, cx, cy, winW, winH, c.x, c.y, c.z, view, basis, P.red);
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
            bool on = !(view.perspective && p.behind) &&
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
        panel(renderer, tx, ty, 224, 58);
        drawText(renderer, tx + 8, ty + 8, t.label.empty() ? std::string("CONTACT") : t.label,
                 t.hostile ? P.red : P.text, 1);
        if (scene.lockTarget >= 0 && scene.lockTarget == scene.targetCraft) {
            drawText(renderer, tx + 224 - 8 - textWidth("LOCK", 1), ty + 8, "LOCK", P.cyan, 1);
        }
        std::snprintf(buf, sizeof(buf), "DIST %.0F LU", dist);
        drawText(renderer, tx + 8, ty + 22, buf, P.dim, 1);
        double frac = t.hullHP / std::max(1.0, t.maxHullHP);
        bar(renderer, tx + 8, ty + 38, 208, 6, frac, t.hostile ? P.red : P.green);
    }

    // Индикатор добычи (по центру, над подсказкой стыковки).
    if (scene.miningRock >= 0) {
        std::snprintf(buf, sizeof(buf), "MINING  +%.0F", scene.miningAccum);
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
        int rw = 154, rh = 158;
        int rpx = winW - rw - 12;   // левый край панели  = winW-166
        int rpy = winH - rh - 52;   // верхний край       = winH-210
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
