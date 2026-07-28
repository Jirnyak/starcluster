#pragma once
#include <cmath>

// Общая камера/проекция (ортографическая) для макро- и локального рендера.
// Извлечено из main.cpp, чтобы переиспользовать в локальном режиме (localdraw.cpp).

struct View3D {
    double centerX = 0.0;
    double centerY = 0.0;
    double centerZ = 0.0;
    double yaw = 0.0;
    double pitch = 0.0;
    double scale = 4.2;
};

struct ProjectedPoint {
    int x = 0;
    int y = 0;
    double depth = 0.0;
};

struct CameraBasis {
    double cy = 1.0;
    double sy = 0.0;
    double cp = 1.0;
    double sp = 0.0;
};

inline CameraBasis makeCameraBasis(const View3D& view) {
    CameraBasis basis;
    basis.cy = std::cos(view.yaw);
    basis.sy = std::sin(view.yaw);
    basis.cp = std::cos(view.pitch);
    basis.sp = std::sin(view.pitch);
    return basis;
}

inline ProjectedPoint projectPointWithBasis(double x, double y, double z, int w, int h, const View3D& view, const CameraBasis& basis) {
    const double dx = x - view.centerX;
    const double dy = y - view.centerY;
    const double dz = z - view.centerZ;

    const double rx = dx * basis.cy - dy * basis.sy;
    const double ry = dx * basis.sy + dy * basis.cy;
    const double screenY = ry * basis.cp - dz * basis.sp;
    const double depth = ry * basis.sp + dz * basis.cp;

    ProjectedPoint p;
    p.x = int(w / 2 + rx * view.scale);
    p.y = int(h / 2 - screenY * view.scale);
    p.depth = depth;
    return p;
}

inline ProjectedPoint projectPoint(double x, double y, double z, int w, int h, const View3D& view) {
    return projectPointWithBasis(x, y, z, w, h, view, makeCameraBasis(view));
}

inline double depthFade(double depth) {
    const double v = 1.0 - std::abs(depth) / 130.0;
    return v < 0.25 ? 0.25 : (v > 1.0 ? 1.0 : v);
}
