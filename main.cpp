#define SDL_MAIN_HANDLED
#include "game.h"
#include "ui.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

struct InfluenceSource {
    int x = 0;
    int y = 0;
    double depth = 0.0;
    int faction = -1;
    double weight = 1.0;
};

struct InfluenceCell {
    SDL_Rect rect;
    int faction = -1;
    Uint8 alpha = 0;
};

struct InfluenceOverlayCache {
    bool valid = false;
    bool forceRefresh = true;
    int w = 0;
    int h = 0;
    int factionCount = 0;
    size_t starCount = 0;
    double centerX = 0.0;
    double centerY = 0.0;
    double centerZ = 0.0;
    double scale = 0.0;
    CameraBasis basis;
    double gameTime = -1.0e30;
    Uint64 refreshCounter = 0;
    std::vector<InfluenceSource> sources;
    std::vector<double> scores;
    std::vector<InfluenceCell> cells;
};

const double TARGET_FPS = 100.0;
const double TARGET_FRAME_SECONDS = 1.0 / TARGET_FPS;
const double BASE_SIM_YEARS_PER_SECOND = 1.0;
const double MAX_REAL_DT_SECONDS = 0.25;
const double MAX_CAMERA_DT_SECONDS = 0.05;
const double MAX_SIM_STEP_YEARS = 0.01;
const double CAMERA_YAW_RADIANS_PER_SECOND = 1.8;
const double CAMERA_PITCH_RADIANS_PER_SECOND = 1.35;
const double INFLUENCE_OVERLAY_MIN_REFRESH_SECONDS = 0.12;
const double INFLUENCE_OVERLAY_MAX_STALE_SECONDS = 0.35;
const double INFLUENCE_OVERLAY_GAME_REFRESH_YEARS = 0.35;
const char* SAVE_FILE = "starcluster.save";

InfluenceOverlayCache gInfluenceOverlayCache;

void invalidateInfluenceOverlayCache() {
    gInfluenceOverlayCache.valid = false;
    gInfluenceOverlayCache.forceRefresh = true;
}

double clampDouble(double value, double lo, double hi) {
    return std::max(lo, std::min(hi, value));
}

CameraBasis makeCameraBasis(const View3D& view) {
    CameraBasis basis;
    basis.cy = std::cos(view.yaw);
    basis.sy = std::sin(view.yaw);
    basis.cp = std::cos(view.pitch);
    basis.sp = std::sin(view.pitch);
    return basis;
}

ProjectedPoint projectPointWithBasis(double x, double y, double z, int w, int h, const View3D& view, const CameraBasis& basis) {
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

ProjectedPoint projectPoint(double x, double y, double z, int w, int h, const View3D& view) {
    return projectPointWithBasis(x, y, z, w, h, view, makeCameraBasis(view));
}

double depthFade(double depth) {
    return clampDouble(1.0 - std::abs(depth) / 130.0, 0.25, 1.0);
}

void panView(View3D& view, double screenDx, double screenDy) {
    const double cy = std::cos(view.yaw);
    const double sy = std::sin(view.yaw);
    const double cp = std::cos(view.pitch);
    const double sp = std::sin(view.pitch);
    const double worldDx = screenDx / std::max(0.001, view.scale);
    const double worldDy = screenDy / std::max(0.001, view.scale);

    view.centerX += cy * worldDx + sy * cp * worldDy;
    view.centerY += -sy * worldDx + cy * cp * worldDy;
    view.centerZ += -sp * worldDy;
}

double shipSpeed(const Ship& ship) {
    return std::sqrt(ship.vx * ship.vx + ship.vy * ship.vy + ship.vz * ship.vz);
}

void advanceGame(Game& game, double years) {
    while (years > 0.0) {
        const double step = std::min(years, MAX_SIM_STEP_YEARS);
        game.update(step);
        years -= step;
    }
}

void updateCameraRotation(View3D& view, const Uint8* keys, double dt) {
    const int yawDir = (keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0);
    const int pitchDir = (keys[SDL_SCANCODE_W] ? 1 : 0) - ((keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_X]) ? 1 : 0);
    view.yaw += double(yawDir) * CAMERA_YAW_RADIANS_PER_SECOND * dt;
    view.pitch += double(pitchDir) * CAMERA_PITCH_RADIANS_PER_SECOND * dt;
}

double marketPressureForElement(const Market& market, int element) {
    if (element < 0 || element >= int(market.prices.size())) return market.pricePressure();
    return market.prices[element] / elementDefinitions()[element].basePrice;
}

void marketColor(const Market& market, int element, Uint8& r, Uint8& g, Uint8& b) {
    const double pressure = std::max(0.2, std::min(2.4, marketPressureForElement(market, element)));
    r = Uint8(std::min(255.0, 70.0 + pressure * 78.0));
    g = Uint8(std::min(255.0, 120.0 + (2.4 - pressure) * 44.0));
    b = Uint8(std::min(255.0, 70.0 + (2.4 - pressure) * 62.0));
}

void setMarketColor(SDL_Renderer* renderer, const Market& market, int element) {
    Uint8 r = 255;
    Uint8 g = 255;
    Uint8 b = 255;
    marketColor(market, element, r, g, b);
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
}

void factionColor(const Game& game, int factionIndex, Uint8& r, Uint8& g, Uint8& b) {
    if (factionIndex >= 0 && factionIndex < int(game.factions.size())) {
        const Faction& faction = game.factions[factionIndex];
        r = Uint8(faction.colorR);
        g = Uint8(faction.colorG);
        b = Uint8(faction.colorB);
    } else {
        r = 100;
        g = 110;
        b = 130;
    }
}

void setFactionColor(SDL_Renderer* renderer, const Game& game, int factionIndex, Uint8 alpha) {
    Uint8 r = 100;
    Uint8 g = 110;
    Uint8 b = 130;
    factionColor(game, factionIndex, r, g, b);
    SDL_SetRenderDrawColor(renderer, r, g, b, alpha);
}

void setAgentColor(SDL_Renderer* renderer, const Agent& agent, bool selected) {
    if (selected) {
        SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
    } else if (agent.playerControlled || agent.type == "player") {
        SDL_SetRenderDrawColor(renderer, 255, 240, 130, 255);
    } else if (agent.type == "military") {
        SDL_SetRenderDrawColor(renderer, 255, 88, 78, 255);
    } else if (agent.type == "colonist") {
        SDL_SetRenderDrawColor(renderer, 105, 235, 142, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 70, 240, 255, 255);
    }
}

bool influenceOverlayCameraDirty(const InfluenceOverlayCache& cache, const View3D& view, const CameraBasis& basis) {
    const double dx = view.centerX - cache.centerX;
    const double dy = view.centerY - cache.centerY;
    const double dz = view.centerZ - cache.centerZ;
    const double centerDirty2 = 0.35 * 0.35;
    if (dx * dx + dy * dy + dz * dz > centerDirty2) return true;
    if (std::abs(view.scale - cache.scale) > std::max(0.02, cache.scale * 0.012)) return true;
    if (std::abs(basis.cy - cache.basis.cy) > 0.006) return true;
    if (std::abs(basis.sy - cache.basis.sy) > 0.006) return true;
    if (std::abs(basis.cp - cache.basis.cp) > 0.006) return true;
    if (std::abs(basis.sp - cache.basis.sp) > 0.006) return true;
    return false;
}

void drawCachedInfluenceOverlay(SDL_Renderer* renderer, const Game& game, const InfluenceOverlayCache& cache) {
    for (size_t i = 0; i < cache.cells.size(); ++i) {
        const InfluenceCell& cell = cache.cells[i];
        setFactionColor(renderer, game, cell.faction, cell.alpha);
        SDL_RenderFillRect(renderer, &cell.rect);
    }
}

void drawInfluenceOverlay(SDL_Renderer* renderer, const Game& game, int w, int h, const View3D& view, const CameraBasis& basis) {
    if (w <= 0 || h <= 0 || game.factions.empty()) return;

    InfluenceOverlayCache& cache = gInfluenceOverlayCache;
    const Uint64 now = SDL_GetPerformanceCounter();
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    const double realSinceRefresh = cache.refreshCounter == 0
        ? INFLUENCE_OVERLAY_MAX_STALE_SECONDS
        : double(now - cache.refreshCounter) / double(frequency);
    const bool shapeDirty =
        !cache.valid ||
        cache.forceRefresh ||
        cache.w != w ||
        cache.h != h ||
        cache.factionCount != int(game.factions.size()) ||
        cache.starCount != game.cluster.stars.size();
    const bool cameraDirty = !shapeDirty && influenceOverlayCameraDirty(cache, view, basis);
    const bool gameTimeDirty =
        !shapeDirty &&
        (game.time < cache.gameTime || game.time - cache.gameTime >= INFLUENCE_OVERLAY_GAME_REFRESH_YEARS);
    const bool staleDirty = !shapeDirty && realSinceRefresh >= INFLUENCE_OVERLAY_MAX_STALE_SECONDS;
    const bool cadenceReady = realSinceRefresh >= INFLUENCE_OVERLAY_MIN_REFRESH_SECONDS;
    if (!shapeDirty && !(cadenceReady && (cameraDirty || gameTimeDirty || staleDirty))) {
        drawCachedInfluenceOverlay(renderer, game, cache);
        return;
    }

    cache.sources.clear();
    if (cache.sources.capacity() < game.cluster.stars.size()) {
        cache.sources.reserve(game.cluster.stars.size());
    }
    const int margin = std::max(160, std::max(w, h) / 4);
    for (size_t i = 0; i < game.cluster.stars.size(); ++i) {
        if (!game.playerKnowsOwner(int(i))) continue;
        const int owner = game.playerKnownOwner(int(i));
        if (owner < 0 || owner >= int(game.factions.size())) continue;

        const ClusterStar& star = game.cluster.stars[i];
        const ProjectedPoint p = projectPointWithBasis(star.x, star.y, star.z, w, h, view, basis);
        if (p.x < -margin || p.x > w + margin || p.y < -margin || p.y > h + margin) continue;

        const double age = game.playerKnownOwnerAge(int(i));
        const double ageWeight = age < 0.0 ? 1.0 : clampDouble(1.0 - age / 80.0, 0.35, 1.0);

        InfluenceSource source;
        source.x = p.x;
        source.y = p.y;
        source.depth = p.depth;
        source.faction = owner;
        source.weight = ageWeight * depthFade(p.depth);
        cache.sources.push_back(source);
    }

    const int cols = 32;
    const int rows = 24;
    const int cellW = std::max(1, (w + cols - 1) / cols);
    const int cellH = std::max(1, (h + rows - 1) / rows);
    const double core = std::max(64.0, std::min(w, h) / 9.0);
    const double core2 = core * core;
    cache.scores.resize(game.factions.size());
    cache.cells.clear();
    if (cache.cells.capacity() < size_t(cols * rows)) {
        cache.cells.reserve(size_t(cols * rows));
    }

    if (!cache.sources.empty()) {
        for (int row = 0; row < rows; ++row) {
            const int sy = row * cellH + cellH / 2;
            for (int col = 0; col < cols; ++col) {
                const int sx = col * cellW + cellW / 2;
                std::fill(cache.scores.begin(), cache.scores.end(), 0.0);

                for (size_t i = 0; i < cache.sources.size(); ++i) {
                    const InfluenceSource& source = cache.sources[i];
                    const double dx = double(sx - source.x);
                    const double dy = double(sy - source.y);
                    const double dz = source.depth * view.scale * 0.35;
                    const double influence = source.weight / (dx * dx + dy * dy + dz * dz + core2);
                    cache.scores[source.faction] += influence;
                }

                int bestFaction = -1;
                double best = 0.0;
                double second = 0.0;
                for (size_t i = 0; i < cache.scores.size(); ++i) {
                    const double score = cache.scores[i];
                    if (score > best) {
                        second = best;
                        best = score;
                        bestFaction = int(i);
                    } else if (score > second) {
                        second = score;
                    }
                }
                if (bestFaction < 0 || best < 0.000009) continue;

                const double confidence = second > 0.0 ? clampDouble((best - second) / best, 0.0, 1.0) : 1.0;
                InfluenceCell cell;
                cell.rect.x = col * cellW;
                cell.rect.y = row * cellH;
                cell.rect.w = cellW + 1;
                cell.rect.h = cellH + 1;
                cell.faction = bestFaction;
                cell.alpha = Uint8(clampDouble((11.0 + best * 260000.0) * (0.45 + confidence * 0.55), 7.0, 42.0));
                cache.cells.push_back(cell);
            }
        }
    }

    cache.valid = true;
    cache.forceRefresh = false;
    cache.w = w;
    cache.h = h;
    cache.factionCount = int(game.factions.size());
    cache.starCount = game.cluster.stars.size();
    cache.centerX = view.centerX;
    cache.centerY = view.centerY;
    cache.centerZ = view.centerZ;
    cache.scale = view.scale;
    cache.basis = basis;
    cache.gameTime = game.time;
    cache.refreshCounter = now;
    drawCachedInfluenceOverlay(renderer, game, cache);
}

int strongestShortage(const Market& market) {
    int best = 0;
    double bestPressure = -1.0;
    for (size_t i = 0; i < market.prices.size(); ++i) {
        const double pressure = market.prices[i] / elementDefinitions()[i].basePrice;
        if (pressure > bestPressure) {
            bestPressure = pressure;
            best = int(i);
        }
    }
    return best;
}

int strongestSurplus(const Market& market) {
    int best = 0;
    double bestPressure = 1e9;
    for (size_t i = 0; i < market.prices.size(); ++i) {
        const double pressure = market.prices[i] / elementDefinitions()[i].basePrice;
        if (pressure < bestPressure) {
            bestPressure = pressure;
            best = int(i);
        }
    }
    return best;
}

int nearestStar(const Game& game, int mx, int my, int w, int h, const View3D& view) {
    int best = -1;
    double bestDist2 = 144.0;
    for (size_t i = 0; i < game.cluster.stars.size(); ++i) {
        const ClusterStar& star = game.cluster.stars[i];
        const ProjectedPoint p = projectPoint(star.x, star.y, star.z, w, h, view);
        const double dx = double(p.x - mx);
        const double dy = double(p.y - my);
        const double d2 = dx * dx + dy * dy + std::abs(p.depth) * 0.08;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            best = int(i);
        }
    }
    return best;
}

int nearestAgent(const Game& game, int mx, int my, int w, int h, const View3D& view) {
    int best = -1;
    double bestDist2 = 169.0;
    for (size_t i = 0; i < game.agents.size(); ++i) {
        if (!game.playerCanSeeAgent(int(i))) continue;
        const Ship& ship = game.agents[i].ship;
        const ProjectedPoint p = projectPoint(ship.x, ship.y, ship.z, w, h, view);
        const double dx = double(p.x - mx);
        const double dy = double(p.y - my);
        const double d2 = dx * dx + dy * dy + std::abs(p.depth) * 0.08;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            best = int(i);
        }
    }
    return best;
}

int nextVisibleAgent(const Game& game, int current) {
    if (game.agents.empty()) return -1;
    const int count = int(game.agents.size());
    for (int step = 1; step <= count; ++step) {
        const int index = (current + step + count) % count;
        if (game.playerCanSeeAgent(index)) return index;
    }
    return game.playerAgent;
}

void drawFocusRect(SDL_Renderer* renderer, int x, int y, int radius) {
    SDL_Rect r = {x - radius, y - radius, radius * 2 + 1, radius * 2 + 1};
    SDL_RenderDrawRect(renderer, &r);
}

void drawRouteLine(SDL_Renderer* renderer, const Game& game, const Agent& agent, int w, int h, const View3D& view, const CameraBasis& basis) {
    if (!agent.ship.enRoute || agent.ship.targetStar < 0 || agent.ship.targetStar >= int(game.cluster.stars.size())) return;

    ProjectedPoint from = projectPointWithBasis(agent.ship.x, agent.ship.y, agent.ship.z, w, h, view, basis);
    ProjectedPoint to = projectPointWithBasis(
        game.cluster.stars[agent.ship.targetStar].x,
        game.cluster.stars[agent.ship.targetStar].y,
        game.cluster.stars[agent.ship.targetStar].z,
        w, h, view, basis);
    SDL_RenderDrawLine(renderer, from.x, from.y, to.x, to.y);

    if (agent.destStar < 0 || agent.destStar >= int(game.cluster.stars.size()) || agent.destStar == agent.ship.targetStar) return;

    int current = agent.ship.targetStar;
    const int maxLegs = std::min(96, std::max(1, int(game.cluster.stars.size())));
    for (int guard = 0; guard < maxLegs && current != agent.destStar; ++guard) {
        const int next = game.routeNextStar(current, agent.destStar);
        if (next < 0 || next >= int(game.cluster.stars.size()) || next == current) break;
        const ClusterStar& a = game.cluster.stars[current];
        const ClusterStar& b = game.cluster.stars[next];
        from = projectPointWithBasis(a.x, a.y, a.z, w, h, view, basis);
        to = projectPointWithBasis(b.x, b.y, b.z, w, h, view, basis);
        SDL_RenderDrawLine(renderer, from.x, from.y, to.x, to.y);
        current = next;
    }
}

void drawStarGlyph(SDL_Renderer* renderer, int x, int y, int size, Uint8 r, Uint8 g, Uint8 b, Uint8 alpha) {
    SDL_SetRenderDrawColor(renderer, r, g, b, alpha);
    SDL_Rect core = {x - size / 2, y - size / 2, size, size};
    SDL_RenderFillRect(renderer, &core);

    if (size >= 3) {
        SDL_SetRenderDrawColor(renderer, r, g, b, Uint8(alpha / 2));
        SDL_RenderDrawPoint(renderer, x - size, y);
        SDL_RenderDrawPoint(renderer, x + size, y);
        SDL_RenderDrawPoint(renderer, x, y - size);
        SDL_RenderDrawPoint(renderer, x, y + size);
    }
}

int countAgentsOfType(const Game& game, const std::string& type) {
    int count = 0;
    for (const Agent& agent : game.agents) {
        if (agent.type == type) count += 1;
    }
    return count;
}

void resetSelectionAfterLoad(Game& game, View3D& view, UI::WindowState& ui, int screenW, int screenH, int& selectedStar, int& selectedAgent, bool& followAgent) {
    selectedAgent = -1;
    if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
        selectedAgent = game.playerAgent;
    } else if (!game.agents.empty()) {
        selectedAgent = 0;
    }

    selectedStar = -1;
    if (selectedAgent >= 0 && selectedAgent < int(game.agents.size())) {
        const Agent& agent = game.agents[selectedAgent];
        if (agent.currentStar >= 0 && agent.currentStar < int(game.cluster.stars.size())) {
            selectedStar = agent.currentStar;
        } else if (agent.destStar >= 0 && agent.destStar < int(game.cluster.stars.size())) {
            selectedStar = agent.destStar;
        }
        view.centerX = agent.ship.x;
        view.centerY = agent.ship.y;
        view.centerZ = agent.ship.z;
    } else if (!game.cluster.stars.empty()) {
        selectedStar = 0;
        const ClusterStar& star = game.cluster.stars[0];
        view.centerX = star.x;
        view.centerY = star.y;
        view.centerZ = star.z;
    }

    followAgent = selectedAgent >= 0;
    ui = UI::WindowState();
    if (selectedStar >= 0) UI::openSystemWindow(ui, selectedStar, screenW, screenH);
}

int main(int argc, char** argv) {
    bool smoke = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) smoke = true;
    }
    const char* smokeEnv = std::getenv("STARCLUSTER_SMOKE");
    if (smokeEnv && std::strcmp(smokeEnv, "0") != 0) smoke = true;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    int winW = 1200;
    int winH = 900;
    SDL_Window* window = SDL_CreateWindow("Starcluster", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    Game game;
    game.init(STAR_COUNT);

    bool quit = false;
    bool paused = false;
    SDL_Event e;
    View3D view;
    view.yaw = 0.62;
    view.pitch = 0.52;
    int titleTick = 0;
    int frames = 0;
    int selectedStar = game.cluster.stars.empty() ? -1 : 0;
    if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
        selectedStar = game.agents[game.playerAgent].currentStar;
    }
    int selectedAgent = game.playerAgent >= 0 ? game.playerAgent : (game.agents.empty() ? -1 : 0);
    int selectedElement = elementIndex("Fe");
    if (selectedElement < 0) selectedElement = 0;
    bool followAgent = selectedAgent >= 0;
    bool showInfluenceOverlay = false;
    double simSpeed = 1.0;
    UI::WindowState ui;
    if (selectedStar >= 0) UI::openSystemWindow(ui, selectedStar, winW, winH);

    const Uint64 perfFrequency = SDL_GetPerformanceFrequency();
    Uint64 lastCounter = SDL_GetPerformanceCounter();
    while (!quit) {
        const Uint64 frameStart = SDL_GetPerformanceCounter();
        double realDt = double(frameStart - lastCounter) / double(perfFrequency);
        lastCounter = frameStart;
        realDt = std::min(realDt, MAX_REAL_DT_SECONDS);

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                winW = e.window.data1;
                winH = e.window.data2;
            }
            if (e.type == SDL_MOUSEMOTION) {
                UI::handleMouseMove(ui, winW, winH, e.motion.x, e.motion.y);
                if (e.motion.state & SDL_BUTTON_MMASK) {
                    panView(view, -double(e.motion.xrel), double(e.motion.yrel));
                    followAgent = false;
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                UI::handleMouseUp(ui);
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                UI::HudSelection clickSelection;
                clickSelection.star = selectedStar;
                clickSelection.agent = selectedAgent;
                clickSelection.element = selectedElement;
                clickSelection.paused = paused;
                clickSelection.followAgent = followAgent;
                clickSelection.simSpeed = simSpeed;
                const bool handled = UI::handleMouseDown(ui, game, clickSelection, winW, winH, e.button.x, e.button.y, e.button.button);
                selectedStar = clickSelection.star;
                selectedAgent = clickSelection.agent;
                selectedElement = clickSelection.element;
                followAgent = clickSelection.followAgent;
                if (!handled) {
                    const int star = nearestStar(game, e.button.x, e.button.y, winW, winH, view);
                    const int agent = nearestAgent(game, e.button.x, e.button.y, winW, winH, view);
                    if (agent >= 0) {
                        selectedAgent = agent;
                        followAgent = false;
                    } else if (star >= 0) {
                        selectedStar = star;
                        followAgent = false;
                        UI::openSystemWindow(ui, selectedStar, winW, winH);
                    }
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                UI::HudSelection clickSelection;
                clickSelection.star = selectedStar;
                clickSelection.agent = selectedAgent;
                clickSelection.element = selectedElement;
                clickSelection.paused = paused;
                clickSelection.followAgent = followAgent;
                clickSelection.simSpeed = simSpeed;
                const bool handled = UI::handleMouseDown(ui, game, clickSelection, winW, winH, e.button.x, e.button.y, e.button.button);
                selectedStar = clickSelection.star;
                selectedAgent = clickSelection.agent;
                selectedElement = clickSelection.element;
                followAgent = clickSelection.followAgent;
                if (!handled) {
                    const int star = nearestStar(game, e.button.x, e.button.y, winW, winH, view);
                    if (star >= 0 && game.commandAgentToStar(game.playerAgent, star)) {
                        selectedStar = star;
                        selectedAgent = game.playerAgent;
                        followAgent = true;
                        UI::openSystemWindow(ui, selectedStar, winW, winH);
                    }
                }
            }
            if (e.type == SDL_MOUSEWHEEL) {
                if (e.wheel.y > 0) view.scale *= 1.15;
                if (e.wheel.y < 0) view.scale /= 1.15;
                view.scale = clampDouble(view.scale, 1.4, 42.0);
            }
            if (e.type == SDL_TEXTINPUT) {
                UI::handleTextInput(ui, e.text.text);
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_F5) {
                    const bool saved = game.saveToFile(SAVE_FILE);
                    game.lastEvent = saved ? "saved starcluster.save" : "save failed";
                    titleTick = 11;
                    continue;
                }
                if (e.key.keysym.sym == SDLK_F9) {
                    const bool loaded = game.loadFromFile(SAVE_FILE);
                    game.lastEvent = loaded ? "loaded starcluster.save" : "load failed";
                    if (loaded) {
                        resetSelectionAfterLoad(game, view, ui, winW, winH, selectedStar, selectedAgent, followAgent);
                        invalidateInfluenceOverlayCache();
                    }
                    titleTick = 11;
                    continue;
                }
                if (UI::handleKeyDown(ui, e.key.keysym.sym)) continue;
                if (e.key.keysym.sym == SDLK_SPACE) paused = !paused;
                if (e.key.keysym.sym == SDLK_ESCAPE) quit = true;
                if (e.key.keysym.sym == SDLK_1) simSpeed = 1.0;
                if (e.key.keysym.sym == SDLK_2) simSpeed = 2.0;
                if (e.key.keysym.sym == SDLK_3) simSpeed = 5.0;
                if (e.key.keysym.sym == SDLK_4) simSpeed = 10.0;
                if (e.key.keysym.sym == SDLK_TAB && !game.agents.empty()) {
                    selectedAgent = nextVisibleAgent(game, selectedAgent);
                }
                if (e.key.keysym.sym == SDLK_f) followAgent = selectedAgent >= 0;
                if (e.key.keysym.sym == SDLK_i) {
                    showInfluenceOverlay = !showInfluenceOverlay;
                    invalidateInfluenceOverlayCache();
                    titleTick = 11;
                }
                if (e.key.keysym.sym == SDLK_p && game.playerAgent >= 0) {
                    selectedAgent = game.playerAgent;
                    followAgent = true;
                }
                if (e.key.keysym.sym == SDLK_g && selectedStar >= 0 && game.commandAgentToStar(game.playerAgent, selectedStar)) {
                    selectedAgent = game.playerAgent;
                    followAgent = true;
                }
                if (e.key.keysym.sym == SDLK_b && game.agentBuyElement(game.playerAgent, selectedElement)) {
                    selectedAgent = game.playerAgent;
                }
                if (e.key.keysym.sym == SDLK_v && game.agentSellCargo(game.playerAgent)) {
                    selectedAgent = game.playerAgent;
                }
                if (e.key.keysym.sym == SDLK_t && game.agentAutoTrade(game.playerAgent)) {
                    selectedAgent = game.playerAgent;
                    followAgent = true;
                }
                if (e.key.keysym.sym == SDLK_c && game.playerFoundColony()) {
                    selectedAgent = game.playerAgent;
                }
                if (e.key.keysym.sym == SDLK_h) {
                    game.playerHireShip();
                    selectedAgent = game.playerAgent;
                }
                if (e.key.keysym.sym == SDLK_LEFT) {
                    panView(view, -18.0, 0.0);
                    followAgent = false;
                }
                if (e.key.keysym.sym == SDLK_RIGHT) {
                    panView(view, 18.0, 0.0);
                    followAgent = false;
                }
                if (e.key.keysym.sym == SDLK_UP) {
                    panView(view, 0.0, 18.0);
                    followAgent = false;
                }
                if (e.key.keysym.sym == SDLK_DOWN) {
                    panView(view, 0.0, -18.0);
                    followAgent = false;
                }
                if (e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_PLUS) view.scale = clampDouble(view.scale * 1.2, 1.4, 42.0);
                if (e.key.keysym.sym == SDLK_MINUS) view.scale = clampDouble(view.scale / 1.2, 1.4, 42.0);
                if (e.key.keysym.sym == SDLK_LEFTBRACKET && selectedElement >= 0) {
                    selectedElement = (selectedElement - 1 + int(elementCount())) % int(elementCount());
                }
                if (e.key.keysym.sym == SDLK_RIGHTBRACKET && selectedElement >= 0) {
                    selectedElement = (selectedElement + 1) % int(elementCount());
                }
                if (e.key.keysym.sym == SDLK_0) {
                    view = View3D();
                    view.yaw = 0.62;
                    view.pitch = 0.52;
                    followAgent = false;
                }
            }
        }

        if (!ui.tradeAmountEditing) {
            updateCameraRotation(view, SDL_GetKeyboardState(nullptr), std::min(realDt, MAX_CAMERA_DT_SECONDS));
        }

        const double simYearsPerSecond = BASE_SIM_YEARS_PER_SECOND * simSpeed;
        if (!paused) advanceGame(game, realDt * simYearsPerSecond);
        if (selectedAgent >= 0 && selectedAgent < int(game.agents.size())) {
            if (!game.playerCanSeeAgent(selectedAgent)) {
                selectedAgent = game.playerAgent;
            }
        }
        if (followAgent && selectedAgent >= 0 && selectedAgent < int(game.agents.size())) {
            const Ship& ship = game.agents[selectedAgent].ship;
            view.centerX = ship.x;
            view.centerY = ship.y;
            view.centerZ = ship.z;
        }

        SDL_SetRenderDrawColor(renderer, 3, 5, 14, 255);
        SDL_RenderClear(renderer);
        const CameraBasis cameraBasis = makeCameraBasis(view);
        if (showInfluenceOverlay) {
            drawInfluenceOverlay(renderer, game, winW, winH, view, cameraBasis);
        }

        for (size_t i = 0; i < game.agents.size(); ++i) {
            const Agent& agent = game.agents[i];
            if (!game.playerCanSeeAgent(int(i))) continue;
            if (agent.ship.enRoute && agent.ship.targetStar >= 0 && agent.ship.targetStar < int(game.cluster.stars.size())) {
                if (int(i) == selectedAgent) {
                    SDL_SetRenderDrawColor(renderer, 230, 210, 90, 255);
                } else if (agent.playerControlled || agent.type == "player") {
                    SDL_SetRenderDrawColor(renderer, 255, 230, 120, 180);
                } else if (agent.type == "military") {
                    SDL_SetRenderDrawColor(renderer, 150, 45, 55, 120);
                } else if (agent.type == "colonist") {
                    SDL_SetRenderDrawColor(renderer, 60, 150, 80, 120);
                } else {
                    SDL_SetRenderDrawColor(renderer, 30, 90, 120, 120);
                }
                drawRouteLine(renderer, game, agent, winW, winH, view, cameraBasis);
            }
        }

        for (size_t i = 0; i < game.cluster.stars.size(); ++i) {
            const ClusterStar& s = game.cluster.stars[i];
            const ProjectedPoint p = projectPointWithBasis(s.x, s.y, s.z, winW, winH, view, cameraBasis);
            const int sx = p.x;
            const int sy = p.y;
            if (sx < -4 || sx > winW + 4 || sy < -4 || sy > winH + 4) continue;

            const bool ownerKnown = game.playerKnowsOwner(int(i));
            const int knownOwner = game.playerKnownOwner(int(i));
            const bool liveInfo = game.playerAtStar(int(i));
            if (ownerKnown) {
                setFactionColor(renderer, game, knownOwner, liveInfo ? 170 : 75);
                const int ring = liveInfo && s.defense > 5.0 ? 6 : 5;
                SDL_Rect halo = {sx - ring / 2, sy - ring / 2, ring, ring};
                SDL_RenderDrawRect(renderer, &halo);
            }

            const int size = liveInfo ? 2 + (s.industry > 1.7 ? 1 : 0) + (ownerKnown && knownOwner >= 0 ? 1 : 0) : (ownerKnown ? 3 : 2);
            Uint8 r = 92;
            Uint8 g = 112;
            Uint8 b = 136;
            if (liveInfo) {
                marketColor(game.markets[i], selectedElement, r, g, b);
            } else if (ownerKnown) {
                factionColor(game, knownOwner, r, g, b);
            }
            const double pulse = 0.62 + 0.38 * std::sin(game.time * (liveInfo ? (2.2 + s.habitability) : 2.2) + double(i) * 1.618);
            const double fade = depthFade(p.depth);
            const Uint8 alpha = liveInfo ? Uint8((170.0 + 85.0 * pulse) * fade) : (ownerKnown ? Uint8((95.0 + 85.0 * pulse) * fade) : Uint8((55.0 + 55.0 * pulse) * fade));
            drawStarGlyph(renderer, sx, sy, size, r, g, b, alpha);

            if (int(i) == selectedStar) {
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                drawFocusRect(renderer, sx, sy, 6);
            }
        }

        for (size_t i = 0; i < game.agents.size(); ++i) {
            const Agent& agent = game.agents[i];
            if (!game.playerCanSeeAgent(int(i))) continue;
            const ProjectedPoint p = projectPointWithBasis(agent.ship.x, agent.ship.y, agent.ship.z, winW, winH, view, cameraBasis);
            const int sx = p.x;
            const int sy = p.y;
            if (int(i) == selectedAgent) {
                SDL_SetRenderDrawColor(renderer, 255, 210, 70, 255);
                drawFocusRect(renderer, sx, sy, 7);
            }
            if (agent.ship.ownerFaction >= 0) {
                setFactionColor(renderer, game, agent.ship.ownerFaction, 180);
                SDL_Rect owner = {sx - 3, sy - 3, 7, 7};
                SDL_RenderDrawRect(renderer, &owner);
            }
            setAgentColor(renderer, agent, int(i) == selectedAgent);
            const int agentSize = agent.playerControlled || agent.type == "player" ? 7 : (agent.type == "military" ? 6 : 5);
            SDL_Rect r = {sx - agentSize / 2, sy - agentSize / 2, agentSize, agentSize};
            SDL_RenderFillRect(renderer, &r);
        }

        if (++titleTick % 12 == 0) {
            char title[1024];
            const ElementDefinition& element = elementDefinitions()[selectedElement];
            const char* influenceMode = showInfluenceOverlay ? "inf on" : "inf off";
            const char* cargo = "empty";
            double speed = 0.0;
            double money = 0.0;
            int trades = 0;
            double profit = 0.0;
            double mass = 0.0;
            double fuel = 0.0;
            const char* shipName = "none";
            std::string agentType = "none";
            std::string agentOwner = "free";
            std::string targetName = "-";
            std::string action = "-";
            double coordX = 0.0;
            double coordY = 0.0;
            double coordZ = 0.0;
            bool hasCoords = false;
            if (selectedAgent >= 0 && selectedAgent < int(game.agents.size())) {
                const Agent& agent = game.agents[selectedAgent];
                shipName = agent.ship.name.c_str();
                cargo = agent.ship.cargo.empty() ? "empty" : agent.ship.cargo[0].element.c_str();
                speed = shipSpeed(agent.ship);
                coordX = agent.ship.x;
                coordY = agent.ship.y;
                coordZ = agent.ship.z;
                hasCoords = true;
                money = agent.money;
                trades = agent.trades;
                profit = agent.lastProfit;
                mass = shipTotalMass(agent.ship);
                fuel = shipFuelFraction(agent.ship) * 100.0;
                agentType = agent.type;
                action = agent.lastAction;
                if (agent.ship.ownerFaction >= 0 && agent.ship.ownerFaction < int(game.factions.size())) {
                    agentOwner = game.factions[agent.ship.ownerFaction].name;
                }
                if (agent.destStar >= 0 && agent.destStar < int(game.cluster.stars.size())) {
                    targetName = game.cluster.stars[agent.destStar].name;
                }
            }
            if (!hasCoords && game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
                const Ship& ship = game.agents[game.playerAgent].ship;
                coordX = ship.x;
                coordY = ship.y;
                coordZ = ship.z;
            }

            if (selectedStar >= 0 && selectedStar < int(game.cluster.stars.size())) {
                const ClusterStar& star = game.cluster.stars[selectedStar];
                const bool liveInfo = game.playerAtStar(selectedStar);
                std::string ownerName = "unknown";
                if (game.playerKnowsOwner(selectedStar)) {
                    const int knownOwner = game.playerKnownOwner(selectedStar);
                    if (knownOwner >= 0 && knownOwner < int(game.factions.size())) {
                        ownerName = game.factions[knownOwner].name;
                    } else {
                        ownerName = "free";
                    }
                    const double age = game.playerKnownOwnerAge(selectedStar);
                    ownerName += game.playerAtStar(selectedStar) ? " live" : " last " + std::to_string(int(age)) + "y";
                }
                if (liveInfo) {
                    const Market& market = game.markets[selectedStar];
                    const int shortage = strongestShortage(market);
                    const int surplus = strongestSurplus(market);
                    std::snprintf(title, sizeof(title),
                        "Starcluster | pos x %.1f y %.1f z %.1f | t %.1f rate %.2fy/s spd x%.1f %s | view %s %s | factions %zu colonies %zu captures %d | %s owner %s %s pop %.0f ind %.2f hab %.2f def %.1f | %s price %.1f supply %.1f demand %.1f | shortage %s x%.1f surplus %s x%.1f | %s %s/%s -> %s %.2fc fuel %.0f%% mass %.0f %s cr %.0f tr %d last %.0f %s | F5 save F9 load H hire/build C colony/reinforce | %s%s",
                        coordX, coordY, coordZ, game.time, simYearsPerSecond, simSpeed, (paused ? "PAUSED" : "LIVE"), element.symbol, influenceMode, game.factions.size(), game.colonies.size(), game.capturedSystems,
                        star.name.c_str(), ownerName.c_str(), star.economyRole.c_str(), star.population, star.industry, star.habitability, star.defense,
                        element.symbol, market.prices[selectedElement], market.supply[selectedElement].amount, market.demand[selectedElement].amount,
                        elementDefinitions()[shortage].symbol, marketPressureForElement(market, shortage),
                        elementDefinitions()[surplus].symbol, marketPressureForElement(market, surplus),
                        shipName, agentType.c_str(), agentOwner.c_str(), targetName.c_str(), speed, fuel, mass, cargo, money, trades, profit, action.c_str(),
                        game.lastEvent.c_str(), followAgent ? " follow" : "");
                } else {
                    std::string marketLine = "market unknown until arrival";
                    if (game.playerKnowsMarket(selectedStar)) {
                        const double age = game.playerKnownMarketAge(selectedStar);
                        const double confidence = game.playerKnownMarketConfidence(selectedStar, selectedElement);
                        const double supplyPressure = game.playerKnownSupplyPressure(selectedStar, selectedElement);
                        const double demandPressure = game.playerKnownDemandPressure(selectedStar, selectedElement);
                        char market[192];
                        std::snprintf(market, sizeof(market), "%s last-known price %.1f age %.0fy conf %.0f%% sup x%.2f dem x%.2f",
                            element.symbol, game.playerKnownPrice(selectedStar, selectedElement), age, confidence * 100.0, supplyPressure, demandPressure);
                        marketLine = market;
                    }
                    std::snprintf(title, sizeof(title),
                        "Starcluster | pos x %.1f y %.1f z %.1f | t %.1f rate %.2fy/s spd x%.1f %s | view %s %s | factions %zu colonies %zu captures %d | %s owner %s | %s | %s %s/%s -> %s %.2fc fuel %.0f%% mass %.0f %s cr %.0f tr %d last %.0f %s | F5 save F9 load H hire/build C colony/reinforce | %s%s",
                        coordX, coordY, coordZ, game.time, simYearsPerSecond, simSpeed, (paused ? "PAUSED" : "LIVE"), element.symbol, influenceMode, game.factions.size(), game.colonies.size(), game.capturedSystems,
                        star.name.c_str(), ownerName.c_str(), marketLine.c_str(),
                        shipName, agentType.c_str(), agentOwner.c_str(), targetName.c_str(), speed, fuel, mass, cargo, money, trades, profit, action.c_str(),
                        game.lastEvent.c_str(), followAgent ? " follow" : "");
                }
            } else {
                std::snprintf(title, sizeof(title), "Starcluster | pos x %.1f y %.1f z %.1f | t %.1f rate %.2fy/s spd x%.1f %s | view %s %s | factions %zu colonies %zu founded %d captures %d | traders %d military %d colonists %d | %s %s/%s -> %s %.2fc fuel %.0f%% mass %.0f %s money %.0f | F5 save F9 load RMB/G route B buy S sell T auto H hire/build C colony/reinforce | %s%s",
                    coordX, coordY, coordZ, game.time, simYearsPerSecond, simSpeed, (paused ? "PAUSED" : "LIVE"), element.symbol, influenceMode, game.factions.size(), game.colonies.size(), game.foundedColonies, game.capturedSystems,
                    countAgentsOfType(game, "trader"), countAgentsOfType(game, "military"), countAgentsOfType(game, "colonist"),
                    shipName, agentType.c_str(), agentOwner.c_str(), targetName.c_str(), speed, fuel, mass, cargo, money, game.lastEvent.c_str(), followAgent ? " follow" : "");
            }
            SDL_SetWindowTitle(window, title);
        }

        UI::HudSelection hud;
        hud.star = selectedStar;
        hud.agent = selectedAgent;
        hud.element = selectedElement;
        hud.paused = paused;
        hud.followAgent = followAgent;
        hud.simSpeed = simSpeed;
        hud.simYearsPerSecond = simYearsPerSecond;
        UI::drawHud(renderer, game, winW, winH, hud);
        UI::drawWindows(renderer, game, winW, winH, hud, ui);

        SDL_RenderPresent(renderer);
        if (smoke && ++frames >= 12) quit = true;
        if (!quit) {
            const Uint64 frameEnd = SDL_GetPerformanceCounter();
            const double frameElapsed = double(frameEnd - frameStart) / double(perfFrequency);
            if (frameElapsed < TARGET_FRAME_SECONDS) {
                SDL_Delay(Uint32(std::ceil((TARGET_FRAME_SECONDS - frameElapsed) * 1000.0)));
            }
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
