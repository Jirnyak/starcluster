#pragma once
#include <cmath>

// Общая камера/проекция для макро- и локального рендера.
//
//  МАКРО (кластер/карта) — СТРОГО ортографическая (§2.7). Ветка `perspective==false`
//  в projectPointWithBasis ПОБИТОВО совпадает с исходной реализацией: макро-рендер
//  выполняет ровно те же выражения, что и раньше, и не затрагивается.
//
//  ЛОКАЛЬНЫЙ режим полёта ("микромир") — включает `perspective==true` (перспектива,
//  как в Elite) и ЯВНЫЙ ортонормированный базис камеры (right/up/fwd в мировых осях),
//  что даёт крен (roll) и произвольную ориентацию камеры-от-корабля (chase-cam).
//  Верхний вид «карты» в локальном режиме — это тот же ортопуть (perspective==false).
//
//  Разрешение пользователя (§2.7): перспектива допускается ТОЛЬКО в локальном режиме;
//  макро/карта кластера остаются ортографическими.

struct View3D {
    double centerX = 0.0;   // ортопуть: центр камеры; перспектива: положение ГЛАЗА камеры
    double centerY = 0.0;
    double centerZ = 0.0;
    double yaw = 0.0;       // ортопуть: рыскание вокруг оси Z
    double pitch = 0.0;     // ортопуть: тангаж
    double scale = 4.2;     // ортопуть: пикселей на единицу

    // --- Перспектива: ТОЛЬКО локальный режим. В макро всегда false. ---
    bool   perspective = false; // true => перспективная проекция (делим на глубину)
    double focal = 800.0;       // фокус в пикселях: screen = focal * (coord / depth)
    double nearPlane = 0.5;     // ближнее отсечение (единицы глубины); depth<=near => behind
};

struct ProjectedPoint {
    int x = 0;
    int y = 0;
    double depth = 0.0;   // ортопуть: знаковая глубина вдоль взгляда; перспектива: дистанция ПЕРЕД камерой (>0)
    bool behind = false;  // перспектива: точка за ближней плоскостью (не рисовать/на кромку). Ортопуть: всегда false.
    double camX = 0.0;    // координата в осях камеры (право) — для пеленга off-screen маркеров
    double camY = 0.0;    // координата в осях камеры (вверх)
    double camZ = 0.0;    // координата в осях камеры (вглубь) == depth в перспективе
};

struct CameraBasis {
    // Ортографический путь (макро + карта): в точности как раньше.
    double cy = 1.0;
    double sy = 0.0;
    double cp = 1.0;
    double sp = 0.0;
    // Явный ортонормированный базис камеры в мировых осях (используется ТОЛЬКО
    // перспективным путём и только когда построен makeCameraFrameBasis).
    // Столбцы: right (вправо по экрану), up (вверх по экрану), fwd (вглубь экрана).
    double rX = 1.0, rY = 0.0, rZ = 0.0;
    double uX = 0.0, uY = 1.0, uZ = 0.0;
    double fX = 0.0, fY = 0.0, fZ = 1.0;
};

// Базис из углов (yaw/pitch) — ортопуть макро. ПОБИТОВО как исходник: заполняет
// только cy/sy/cp/sp. Явные поля right/up/fwd остаются дефолтными (перспективный
// путь их при этом не использует, т.к. макро всегда perspective==false).
inline CameraBasis makeCameraBasis(const View3D& view) {
    CameraBasis basis;
    basis.cy = std::cos(view.yaw);
    basis.sy = std::sin(view.yaw);
    basis.cp = std::cos(view.pitch);
    basis.sp = std::sin(view.pitch);
    return basis;
}

// Базис камеры из ЯВНЫХ векторов носа (fwd) и «вверх» (up) — для перспективной
// камеры-от-корабля. Выполняет орто-нормировку (Грам–Шмидт) и защиту от вырождения.
// Конвенция: right = fwd × up (правая тройка экрана, не зеркалит вид).
inline CameraBasis makeCameraFrameBasis(double fx, double fy, double fz,
                                        double ux, double uy, double uz) {
    CameraBasis b;
    double fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (fl < 1e-9) { fx = 0.0; fy = 0.0; fz = 1.0; fl = 1.0; }
    fx /= fl; fy /= fl; fz /= fl;
    // up_perp = up - fwd*(up·fwd)
    double d = ux * fx + uy * fy + uz * fz;
    ux -= d * fx; uy -= d * fy; uz -= d * fz;
    double ul = std::sqrt(ux * ux + uy * uy + uz * uz);
    if (ul < 1e-9) {
        // up почти коллинеарен fwd — берём любой устойчивый перпендикуляр
        if (std::fabs(fx) < 0.9) { ux = 1.0; uy = 0.0; uz = 0.0; }
        else                     { ux = 0.0; uy = 1.0; uz = 0.0; }
        d = ux * fx + uy * fy + uz * fz;
        ux -= d * fx; uy -= d * fy; uz -= d * fz;
        ul = std::sqrt(ux * ux + uy * uy + uz * uz);
    }
    ux /= ul; uy /= ul; uz /= ul;
    // right = fwd × up
    const double rx = fy * uz - fz * uy;
    const double ry = fz * ux - fx * uz;
    const double rz = fx * uy - fy * ux;
    b.fX = fx; b.fY = fy; b.fZ = fz;
    b.uX = ux; b.uY = uy; b.uZ = uz;
    b.rX = rx; b.rY = ry; b.rZ = rz;
    return b;
}

inline ProjectedPoint projectPointWithBasis(double x, double y, double z, int w, int h, const View3D& view, const CameraBasis& basis) {
    const double dx = x - view.centerX;
    const double dy = y - view.centerY;
    const double dz = z - view.centerZ;

    ProjectedPoint p;
    if (!view.perspective) {
        // === Ортографический путь: ПОБИТОВО как исходник (макро/карта) ===
        const double rx = dx * basis.cy - dy * basis.sy;
        const double ry = dx * basis.sy + dy * basis.cy;
        const double screenY = ry * basis.cp - dz * basis.sp;
        const double depth = ry * basis.sp + dz * basis.cp;
        p.x = int(w / 2 + rx * view.scale);
        p.y = int(h / 2 - screenY * view.scale);
        p.depth = depth;
        p.camX = rx; p.camY = screenY; p.camZ = depth;
        return p;
    }

    // === Перспективный путь (локальный режим) ===
    const double cx = dx * basis.rX + dy * basis.rY + dz * basis.rZ;
    const double cyv = dx * basis.uX + dy * basis.uY + dz * basis.uZ;
    const double cz = dx * basis.fX + dy * basis.fY + dz * basis.fZ;
    p.depth = cz;
    p.camX = cx; p.camY = cyv; p.camZ = cz;
    if (cz <= view.nearPlane) {
        p.behind = true;
        p.x = w / 2;
        p.y = h / 2;
        return p;
    }
    const double inv = view.focal / cz;
    p.x = int(double(w) / 2.0 + cx * inv);
    p.y = int(double(h) / 2.0 - cyv * inv);
    return p;
}

// Проекция НАПРАВЛЕНИЯ (точки на бесконечности) — для LOD-скайбокса кластера:
// далёкие звёзды задаются единичным вектором направления, трансляция камеры
// пренебрежима (важна только ориентация). Только перспективный путь.
inline ProjectedPoint projectDirectionWithBasis(double dx, double dy, double dz, int w, int h, const View3D& view, const CameraBasis& basis) {
    ProjectedPoint p;
    const double cx = dx * basis.rX + dy * basis.rY + dz * basis.rZ;
    const double cyv = dx * basis.uX + dy * basis.uY + dz * basis.uZ;
    const double cz = dx * basis.fX + dy * basis.fY + dz * basis.fZ;
    p.depth = cz;
    p.camX = cx; p.camY = cyv; p.camZ = cz;
    if (cz <= 1e-6) {
        p.behind = true;
        p.x = w / 2;
        p.y = h / 2;
        return p;
    }
    const double inv = view.focal / cz;
    p.x = int(double(w) / 2.0 + cx * inv);
    p.y = int(double(h) / 2.0 - cyv * inv);
    return p;
}

inline ProjectedPoint projectPoint(double x, double y, double z, int w, int h, const View3D& view) {
    return projectPointWithBasis(x, y, z, w, h, view, makeCameraBasis(view));
}

inline double depthFade(double depth) {
    const double v = 1.0 - std::abs(depth) / 130.0;
    return v < 0.25 ? 0.25 : (v > 1.0 ? 1.0 : v);
}
