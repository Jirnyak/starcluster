#define SDL_MAIN_HANDLED
#include "game.h"
#include "ui.h"
#include "camera.h"
#include "local.h"
#include "render2d.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include "stb_image.h"
#include <dirent.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// --- Единая панель действий: кликабельные кнопки с состоянием вкл/выкл. ---
// Снимает нагрузку «помни десятки горячих клавиш»: доступные действия — цветные,
// недоступные в этот момент — затемнены. Клавиатура продолжает работать параллельно.
enum ActionId {
    ACT_NONE = 0,
    // Макро-режим (звёздная карта):
    ACT_ENTER, ACT_GO, ACT_STOP, ACT_TRADE, ACT_SHIPFIT, ACT_SWITCH, ACT_REPAIR, ACT_HIRE, ACT_CARGO, ACT_PAUSE, ACT_SPEED, ACT_TRANSACTIONS,
    // Локальный режим (полёт):
    ACT_FIRE, ACT_MINE, ACT_DOCK, ACT_TARGET, ACT_ZOOM_IN, ACT_ZOOM_OUT, ACT_VIEW, ACT_EXIT
};
struct ActionButton {
    SDL_Rect rect;
    std::string label;
    SDL_Color color;
    bool enabled;
    bool on;        // подсветка активного тумблера (пауза идёт / добыча идёт / цель залочена)
    int action;
};

const double TARGET_FPS = 100.0;
const double TARGET_FRAME_SECONDS = 1.0 / TARGET_FPS;
const double BASE_SIM_YEARS_PER_SECOND = 1.0;
const double MAX_REAL_DT_SECONDS = 0.25;
const double MAX_CAMERA_DT_SECONDS = 0.05;
const double MAX_SIM_STEP_YEARS = 0.01;
const double CAMERA_YAW_RADIANS_PER_SECOND = 1.8;
const double CAMERA_PITCH_RADIANS_PER_SECOND = 1.35;
const char* SAVE_FILE = "starcluster.save";

double clampDouble(double value, double lo, double hi) {
    return std::max(lo, std::min(hi, value));
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
    if (size <= 4) {
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
    } else {
        // Natural halo (drawn first so core overlays it)
        if (size >= 8) {
            SDL_SetRenderDrawColor(renderer, r, g, b, Uint8(alpha / 3));
            int haloRadius = size;
            for (int dy = -haloRadius; dy <= haloRadius; ++dy) {
                int dx = std::round(std::sqrt(std::max(0, haloRadius * haloRadius - dy * dy)));
                SDL_RenderDrawLine(renderer, x - dx, y + dy, x + dx, y + dy);
            }
        }
        // Solid core
        SDL_SetRenderDrawColor(renderer, r, g, b, alpha);
        int radius = size / 2;
        for (int dy = -radius; dy <= radius; ++dy) {
            int dx = std::round(std::sqrt(std::max(0, radius * radius - dy * dy)));
            SDL_RenderDrawLine(renderer, x - dx, y + dy, x + dx, y + dy);
        }
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
    bool localSmoke = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) smoke = true;
        if (std::strcmp(argv[i], "--localsmoke") == 0) { smoke = true; localSmoke = true; }
    }
    const char* smokeEnv = std::getenv("STARCLUSTER_SMOKE");
    if (smokeEnv && std::strcmp(smokeEnv, "0") != 0) smoke = true;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
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
    
    SDL_Texture* timertiaTex = nullptr;
    int timertiaW, timertiaH, timertiaChannels;
    unsigned char* timertiaData = stbi_load("timertia.png", &timertiaW, &timertiaH, &timertiaChannels, 4);
    if (timertiaData) {
        SDL_Surface* surface = SDL_CreateRGBSurfaceFrom((void*)timertiaData, timertiaW, timertiaH, 32, timertiaW * 4,
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                                                        0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff
#else
                                                        0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000
#endif
                                                       );
        if (surface) {
            timertiaTex = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
        }
        stbi_image_free(timertiaData);
    } else {
        std::cerr << "Failed to load timertia.png: " << stbi_failure_reason() << "\n";
    }

    Game game;
    game.init(STAR_COUNT);

    std::vector<Mix_Music*> playlist;
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "SDL_mixer could not initialize! Error: " << Mix_GetError() << "\n";
    } else {
        DIR* dir = opendir("music/processed");
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL) {
                std::string filename = ent->d_name;
                if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".mp3") {
                    Mix_Music* music = Mix_LoadMUS(("music/processed/" + filename).c_str());
                    if (music) {
                        playlist.push_back(music);
                    }
                }
            }
            closedir(dir);
        }
    }

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
    double simSpeed = 1.0;
    UI::WindowState ui;
    ui.vnState.active = true; // Start introductory tutorial

    // --- Локальный режим полёта ("микромир") ---
    LocalScene localScene;
    View3D localView;              // 3D-кокпит (перспектива, глаз в корабле) или верхний ортовид (карта)
    double localZoom = 0.8;        // ортозум карты, пикселей на LU (+/- в режиме карты)
    double localFovScale = 0.60;   // оптический зум/FOV кокпита: фокус = winW*fovScale (+/- в 3D)
    bool localMapMode = false;     // false = 3D-полёт (перспектива), true = верхняя карта (клавиша C)
    bool localMineEdge = false;    // фронт нажатия M за кадр (добыча; E теперь крен)
    bool localDockEdge = false;    // фронт нажатия K за кадр
    bool localTargetEdge = false;  // фронт нажатия Tab за кадр
    bool localFireClick = false;   // одноразовый выстрел по кнопке панели (в этот кадр)
    bool localFireHeld = false;    // ЛКМ удерживается в кокпите (мышь-взгляд как в шутере)
    bool relMouseOn = false;       // включён ли SDL relative-mouse (захват указателя в кокпите)
    const double LOCAL_MOUSE_SENS = 0.0025;   // рад/пиксель для мышь-взгляда
    if (localSmoke) {
        buildLocalScene(game, selectedStar >= 0 ? selectedStar : 0, localScene);
        localScene.active = true;
        // Headless-покрытие: ставим игрока вплотную к первому астероиду, чтобы
        // инъекция ввода реально прогнала путь добычи (поиск+начисление+трюм).
        if (!localScene.rocks.empty()) {
            localScene.px = localScene.rocks[0].x + 4.0;
            localScene.py = localScene.rocks[0].y;
            localScene.pz = localScene.rocks[0].z;
        }
    }

    // ---------- Единая панель действий: раскладка, отрисовка, диспетчеризация ----------
    auto playerDocked = [&]() -> int {
        // Индекс звезды, где игрок пристыкован (не в пути), иначе -1.
        if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size()) &&
            !game.agents[game.playerAgent].ship.enRoute) {
            return game.agents[game.playerAgent].currentStar;
        }
        return -1;
    };
    auto playerEnRoute = [&]() -> bool {
        return game.playerAgent >= 0 && game.playerAgent < int(game.agents.size()) &&
               game.agents[game.playerAgent].ship.enRoute;
    };
    auto anchorStar = [&]() -> int {
        int d = playerDocked();
        return d >= 0 ? d : selectedStar; // может быть -1, если ничего не выбрано
    };
    auto localAnchorStar = [&]() -> int {
        if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
            if (!game.agents[game.playerAgent].ship.enRoute) {
                return game.agents[game.playerAgent].currentStar;
            } else {
                return -1; // В полёте переходим в пустоту
            }
        }
        return selectedStar >= 0 ? selectedStar : -1;
    };

    auto buildBar = [&](std::vector<ActionButton>& out) {
        out.clear();
        struct Spec { int action; std::string label; SDL_Color color; bool enabled; bool on; };
        std::vector<Spec> specs;
        if (localScene.active) {
            const bool mining = localScene.miningRock >= 0;
            specs.push_back(Spec{ACT_FIRE, "SPC FIRE", UI::P.red,
                                 localScene.fireCooldown <= 0.0 && !localScene.playerDestroyed, false});
            specs.push_back(Spec{ACT_MINE, mining ? "M STOP" : "M MINE", UI::P.amber,
                                 mining || localScene.minePrompt >= 0, mining});
            specs.push_back(Spec{ACT_DOCK, "K DOCK", UI::P.green, localScene.dockPrompt >= 0, false});
            specs.push_back(Spec{ACT_TARGET, "TAB LOCK", UI::P.cyan,
                                 !localScene.craft.empty(), localScene.lockTarget >= 0});
            // Зум зависит от режима: карта => ортозум; 3D => оптический зум/FOV кокпита.
            const bool zoomOutEnabled = localMapMode ? (localZoom > 0.0501) : (localFovScale > 0.401);
            const bool zoomInEnabled  = localMapMode ? (localZoom < 39.99)  : (localFovScale < 1.599);
            specs.push_back(Spec{ACT_ZOOM_OUT, "- ZOOM", UI::P.dim, zoomOutEnabled, false});
            specs.push_back(Spec{ACT_ZOOM_IN, "+ ZOOM", UI::P.dim, zoomInEnabled, false});
            specs.push_back(Spec{ACT_VIEW, localMapMode ? "C 3D" : "C MAP", UI::P.cyan, true, localMapMode});
            specs.push_back(Spec{ACT_EXIT, "L EXIT", UI::P.cyan, true, false});
        } else {
            const bool docked = playerDocked() >= 0;
            const bool enRoute = playerEnRoute();
            bool hullHurt = false;
            if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
                const Ship& ps = game.agents[game.playerAgent].ship;
                hullHurt = ps.hullHP < ps.maxHullHP - 0.5;
            }
            char spd[16]; std::snprintf(spd, sizeof(spd), "SPD X%d", int(simSpeed));
            specs.push_back(Spec{ACT_ENTER, "L ENTER", UI::P.cyan, true, false});
            specs.push_back(Spec{ACT_GO, "G GO", UI::P.green,
                                 selectedStar >= 0 && game.playerAgent >= 0 && !enRoute, false});
            specs.push_back(Spec{ACT_STOP, "X STOP", UI::P.amber, enRoute, false});
            specs.push_back(Spec{ACT_TRADE, "T TRADE", UI::P.cyan, anchorStar() >= 0, false});
            specs.push_back(Spec{ACT_SHIPFIT, "U FIT", UI::P.cyan, game.playerAgent >= 0, false});
            specs.push_back(Spec{ACT_SWITCH, "W SWITCH", UI::P.amber, game.playerAgent >= 0, false});
            specs.push_back(Spec{ACT_REPAIR, "J REPAIR", UI::P.green, docked && hullHurt, false});
            specs.push_back(Spec{ACT_HIRE, "H HIRE", UI::P.green, docked, false});
            specs.push_back(Spec{ACT_CARGO, "O CARGO", UI::P.cyan, game.playerAgent >= 0, false});
            specs.push_back(Spec{ACT_TRANSACTIONS, "I LOG", UI::P.cyan, game.playerAgent >= 0, false});
            specs.push_back(Spec{ACT_PAUSE, paused ? "|| PAUSE" : "> PLAY", UI::P.amber, true, paused});
            specs.push_back(Spec{ACT_SPEED, spd, UI::P.dim, true, false});
        }
        // Раскладка: одна центрированная строка вдоль низа. В локальном режиме поднята
        // выше (чтобы не наехать на нижнюю подсказку управления).
        const int h = 24, gap = 6, pad = 14, scale = 1;
        int total = 0;
        for (size_t i = 0; i < specs.size(); ++i)
            total += pad + int(specs[i].label.size()) * 6 * scale + (i ? gap : 0);
        int x = std::max(6, (winW - total) / 2);
        const int y = localScene.active ? (winH - h - 34) : (winH - h - 6);
        for (size_t i = 0; i < specs.size(); ++i) {
            ActionButton b;
            b.rect = SDL_Rect{ x, y, pad + int(specs[i].label.size()) * 6 * scale, h };
            b.label = specs[i].label;
            b.color = specs[i].color;
            b.enabled = specs[i].enabled;
            b.on = specs[i].on;
            b.action = specs[i].action;
            out.push_back(b);
            x += b.rect.w + gap;
        }
    };

    auto drawBar = [&](const std::vector<ActionButton>& bar) {
        int mx = 0, my = 0;
        Uint32 mstate = SDL_GetMouseState(&mx, &my);
        for (size_t i = 0; i < bar.size(); ++i) {
            const ActionButton& b = bar[i];
            bool hovered = b.enabled && (mx >= b.rect.x && mx < b.rect.x + b.rect.w && my >= b.rect.y && my < b.rect.y + b.rect.h);
            bool pressed = hovered && (mstate & SDL_BUTTON(SDL_BUTTON_LEFT));

            SDL_Color fill = b.on
                ? SDL_Color{ Uint8(b.color.r / 3 + 10), Uint8(b.color.g / 3 + 10), Uint8(b.color.b / 3 + 14), 235 }
                : (b.enabled ? SDL_Color{ 16, 22, 38, 235 } : SDL_Color{ 12, 16, 26, 200 });
            SDL_Color txt = b.enabled ? (b.on ? UI::P.text : b.color) : SDL_Color{ 86, 98, 118, 255 };

            if (pressed) {
                fill = b.color;
                txt = { 12, 16, 26, 255 };
            } else if (hovered) {
                fill = SDL_Color{ Uint8(b.color.r / 3 + 16), Uint8(b.color.g / 3 + 22), Uint8(b.color.b / 3 + 38), 235 };
            }

            UI::fillRect(renderer, b.rect.x, b.rect.y, b.rect.w, b.rect.h, fill);
            UI::strokeRect(renderer, b.rect.x, b.rect.y, b.rect.w, b.rect.h, b.enabled ? b.color : UI::P.dim);
            const int lw = int(b.label.size()) * 6;
            UI::drawText(renderer, b.rect.x + (b.rect.w - lw) / 2, b.rect.y + (b.rect.h - 7) / 2, b.label, txt, 1);
        }
    };

    auto dispatch = [&](int action) {
        switch (action) {
            case ACT_ENTER: {
                buildLocalScene(game, localAnchorStar(), localScene);
                localScene.active = true;
                game.lastEvent = "entered local flight";
                titleTick = 11;
            } break;
            case ACT_GO:
                if (selectedStar >= 0 && game.commandAgentToStar(game.playerAgent, selectedStar)) {
                    selectedAgent = game.playerAgent; followAgent = true;
                }
                break;
            case ACT_STOP:
                if (game.playerAgent >= 0) game.abortAgentRoute(game.playerAgent);
                break;
            case ACT_TRADE: {
                int a = anchorStar();
                if (a >= 0) { selectedStar = a; UI::openTradeWindow(ui, a, winW, winH); }
            } break;
            case ACT_CARGO: {
                UI::openCargoWindow(ui, anchorStar(), winW, winH);
                break;
            }
            case ACT_TRANSACTIONS: {
                UI::openTransactionsWindow(ui, winW, winH);
                break;
            }
            case ACT_SHIPFIT: {
                int a = anchorStar();
                if (a >= 0) { selectedStar = a; UI::openShipFitWindow(ui, a, winW, winH); }
            } break;
            case ACT_SWITCH: {
                if (game.playerAgent >= 0) {
                    int nextAgent = game.playerAgent;
                    for (size_t i = 1; i <= game.agents.size(); ++i) {
                        int index = (game.playerAgent + i) % game.agents.size();
                        if (game.agents[index].playerControlled && game.agents[index].ship.ownerFaction == game.playerFaction) {
                            nextAgent = index;
                            break;
                        }
                    }
                    if (nextAgent != game.playerAgent) {
                        game.playerAgent = nextAgent;
                        selectedAgent = nextAgent;
                        selectedStar = game.agents[nextAgent].ship.enRoute ? game.agents[nextAgent].destStar : game.agents[nextAgent].currentStar;
                        followAgent = true;
                    }
                }
            } break;
            case ACT_REPAIR:
                if (game.playerRepairHull()) { selectedAgent = game.playerAgent; titleTick = 11; }
                break;
            case ACT_HIRE:
                game.playerHireShip(); selectedAgent = game.playerAgent;
                break;
            case ACT_PAUSE: paused = !paused; break;
            case ACT_SPEED:
                simSpeed = (simSpeed >= 10.0) ? 1.0 : (simSpeed >= 5.0 ? 10.0 : (simSpeed >= 2.0 ? 5.0 : 2.0));
                break;
            case ACT_FIRE:   localFireClick = true;  break;
            case ACT_MINE:   localMineEdge = true;   break;
            case ACT_DOCK:   localDockEdge = true;   break;
            case ACT_TARGET: localTargetEdge = true; break;
            case ACT_VIEW:   localMapMode = !localMapMode; break;
            case ACT_ZOOM_IN:
                if (localMapMode) localZoom = clampDouble(localZoom * 1.2, 0.05, 40.0);
                else localFovScale = clampDouble(localFovScale * 1.15, 0.40, 1.60); // уже FOV
                break;
            case ACT_ZOOM_OUT:
                if (localMapMode) localZoom = clampDouble(localZoom / 1.2, 0.05, 40.0);
                else localFovScale = clampDouble(localFovScale / 1.15, 0.40, 1.60); // шире FOV
                break;
            case ACT_EXIT: localScene.active = false; game.lastEvent = "exited local flight"; break;
        }
    };

    const Uint64 perfFrequency = SDL_GetPerformanceFrequency();
    Uint64 lastCounter = SDL_GetPerformanceCounter();
    while (!quit) {
        const Uint64 frameStart = SDL_GetPerformanceCounter();
        double realDt = double(frameStart - lastCounter) / double(perfFrequency);
        lastCounter = frameStart;
        realDt = std::min(realDt, MAX_REAL_DT_SECONDS);

        if (!playlist.empty() && !Mix_PlayingMusic()) {
            int idx = randomer(rng, int(playlist.size()) - 1);
            Mix_PlayMusic(playlist[idx], 1);
        }
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
            
            // Intercept clicks if tariff is pending
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && game.pendingTariff) {
                int boxW = 500;
                int boxH = 150;
                int boxX = (winW - boxW) / 2;
                int boxY = (winH - boxH) / 2;
                int btnY = boxY + boxH - 40;
                
                int payX = boxX + 40;
                int payW = 150;
                int refuseX = boxX + boxW - 150 - 40;
                int refuseW = 150;
                
                if (e.button.y >= btnY && e.button.y <= btnY + 24) {
                    if (e.button.x >= payX && e.button.x <= payX + payW) {
                        if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
                            Agent& pa = game.agents[game.playerAgent];
                            if (pa.money >= game.tariffFee) {
                                pa.money -= game.tariffFee;
                                game.pendingTariff = false;
                                game.lastEvent = "paid system access fee";
                                game.pushNews("Paid system access fee.", 1);
                            } else {
                                game.pushNews("Not enough credits to pay tariff!", 0);
                            }
                        }
                    } else if (e.button.x >= refuseX && e.button.x <= refuseX + refuseW) {
                        game.pendingTariff = false;
                        game.lastEvent = "refused tariff — hostile encounter!";
                        game.pushNews("Tariff refused! Hostile intercept!", 3);
                        if (game.tariffFaction >= 0) {
                            game.adjustFactionRelation(game.playerFaction, game.tariffFaction, -30);
                        }
                        localScene.active = true;
                        // Forcing local hostiles: just rely on the relation drop and existing local combat.
                    }
                }
                continue; // Block all other UI interactions
            }

            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                UI::handleMouseUp(ui);
                localFireHeld = false;   // отпустили гашетку
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                localScene.active && !localMapMode) {
                localFireHeld = true;    // ЛКМ в кокпите = огонь (указатель захвачен мышь-взглядом,
                                         // панель действий в полёте не кликается — управление с клавиш)
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
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
                    // Сначала — единая панель действий (перекрывает выбор звезды).
                    std::vector<ActionButton> bar;
                    buildBar(bar);
                    bool hitBtn = false;
                    int act = ACT_NONE;
                    for (size_t bi = 0; bi < bar.size(); ++bi) {
                        const SDL_Rect& rr = bar[bi].rect;
                        if (e.button.x >= rr.x && e.button.y >= rr.y &&
                            e.button.x < rr.x + rr.w && e.button.y < rr.y + rr.h) {
                            hitBtn = true;
                            if (bar[bi].enabled) act = bar[bi].action;
                            break;
                        }
                    }
                    if (act != ACT_NONE) {
                        dispatch(act);
                    } else if (!hitBtn && (!localScene.active || localMapMode)) {
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
                if (!handled && (!localScene.active || localMapMode)) {
                    const int star = nearestStar(game, e.button.x, e.button.y, winW, winH, view);
                    if (star >= 0) {
                        selectedStar = star;
                        UI::openSystemWindow(ui, selectedStar, winW, winH);
                        bool success = game.commandAgentToStar(game.playerAgent, star);
                        printf("DEBUG MOUSE: Right-click GO. Target star: %d. Success: %s\n", star, success ? "true" : "false");
                        if (!success) {
                            printf("DEBUG MOUSE: Reason: %s\n", game.lastEvent.c_str());
                        } else {
                            selectedAgent = game.playerAgent;
                            followAgent = true;
                        }
                    }
                }
            }
            if (e.type == SDL_MOUSEWHEEL) {
                if (e.wheel.y > 0) view.scale *= 1.15;
                if (e.wheel.y < 0) view.scale /= 1.15;
                view.scale = clampDouble(view.scale, 1.4, 300.0);
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
                    }
                    titleTick = 11;
                    continue;
                }
                if (UI::handleKeyDown(ui, e.key.keysym.sym)) continue;
                
                if (e.key.keysym.sym == SDLK_v) {
                    ui.vnState.active = !ui.vnState.active;
                    continue;
                }
                
                // L — вход/выход из локального режима полёта (работает в обоих режимах)
                if (e.key.keysym.sym == SDLK_l) {
                    if (localScene.active) {
                        localScene.active = false;
                        game.lastEvent = "exited local flight";
                    } else {
                        buildLocalScene(game, localAnchorStar(), localScene);
                        localScene.active = true;
                        game.lastEvent = "entered local flight";
                    }
                    titleTick = 11;
                    continue;
                }
                if (localScene.active) {
                    // Локальный режим: клавиши обрабатываем здесь, макро-обработчики ниже пропускаем.
                    if (e.key.keysym.sym == SDLK_ESCAPE) { localScene.active = false; continue; }
                    if (e.key.keysym.sym == SDLK_m) localMineEdge = true;   // добыча (E теперь крен)
                    if (e.key.keysym.sym == SDLK_k) localDockEdge = true;
                    if (e.key.keysym.sym == SDLK_TAB) localTargetEdge = true;
                    if (e.key.keysym.sym == SDLK_c) localMapMode = !localMapMode; // 3D <-> карта
                    if (e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_PLUS) {
                        if (localMapMode) localZoom = clampDouble(localZoom * 1.2, 0.05, 40.0);
                        else localFovScale = clampDouble(localFovScale * 1.15, 0.40, 1.60);
                    }
                    if (e.key.keysym.sym == SDLK_MINUS) {
                        if (localMapMode) localZoom = clampDouble(localZoom / 1.2, 0.05, 40.0);
                        else localFovScale = clampDouble(localFovScale / 1.15, 0.40, 1.60);
                    }
                    // Q/E (крен) и R/F (тангаж) читаются как удержание в сборке LocalInput ниже.
                    continue;
                }
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
                if (e.key.keysym.sym == SDLK_r && selectedAgent >= 0 && selectedAgent != game.playerAgent) {
                    if (game.robAgent(game.playerAgent, selectedAgent)) followAgent = true;
                }
                if (e.key.keysym.sym == SDLK_RETURN) {
                    if (UI::advanceVisualNovel(ui, game, winW, winH)) {
                        continue;
                    }
                    if (game.playerAgent >= 0) {
                        selectedStar = game.agents[game.playerAgent].currentStar;
                        if (selectedStar >= 0) {
                            followAgent = false;
                            UI::openSystemWindow(ui, selectedStar, winW, winH);
                        }
                    }
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
                if (e.key.keysym.sym == SDLK_x && game.playerAgent >= 0) {
                    game.abortAgentRoute(game.playerAgent);
                }
                if (e.key.keysym.sym == SDLK_c && game.playerFoundColony()) {
                    selectedAgent = game.playerAgent;
                }
                if (e.key.keysym.sym == SDLK_h) {
                    game.playerHireShip();
                    selectedAgent = game.playerAgent;
                }
                if (e.key.keysym.sym == SDLK_m && game.playerToggleMining()) {
                    selectedAgent = game.playerAgent;
                    titleTick = 11;
                }
                if (e.key.keysym.sym == SDLK_j && game.playerRepairHull()) {
                    selectedAgent = game.playerAgent;
                    titleTick = 11;
                }
                if (e.key.keysym.sym == SDLK_k && game.playerScanAnomaly()) {
                    selectedAgent = game.playerAgent;
                    titleTick = 11;
                }
                if (e.key.keysym.sym == SDLK_u) {
                    int syStar = selectedStar;
                    if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size()) &&
                        !game.agents[game.playerAgent].ship.enRoute) {
                        syStar = game.agents[game.playerAgent].currentStar;
                    }
                    if (syStar >= 0) {
                        selectedStar = syStar;
                        UI::openShipyardWindow(ui, syStar, std::max(20, winW / 2 - 235), 40);
                    }
                }
                if (e.key.keysym.sym == SDLK_i) {
                    dispatch(ACT_TRANSACTIONS);
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

        if (!localScene.active && !ui.tradeAmountEditing) {
            updateCameraRotation(view, SDL_GetKeyboardState(nullptr), std::min(realDt, MAX_CAMERA_DT_SECONDS));
        }

        // Мышь-взгляд (шутер): в кокпите захватываем указатель (relative mode) — курсор скрыт,
        // читаем только относительные дельты. На карте/в макро — обычный курсор для кликов.
        {
            const bool wantRel = localScene.active && !localMapMode && !localSmoke;
            if (wantRel != relMouseOn) {
                SDL_SetRelativeMouseMode(wantRel ? SDL_TRUE : SDL_FALSE);
                relMouseOn = wantRel;
                int jx = 0, jy = 0; SDL_GetRelativeMouseState(&jx, &jy); // сброс накопленной дельты
            }
        }

        const double simYearsPerSecond = BASE_SIM_YEARS_PER_SECOND * simSpeed;
        if (localScene.active) {
            // Локальный режим: собираем ввод и продвигаем сцену; макро-симуляция заморожена.
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            LocalInput li;
            li.thrust = ks[SDL_SCANCODE_W] != 0;
            li.brake  = ks[SDL_SCANCODE_S] != 0;
            li.yawL   = ks[SDL_SCANCODE_A] != 0;
            li.yawR   = ks[SDL_SCANCODE_D] != 0;
            li.pitchU = ks[SDL_SCANCODE_R] != 0;
            li.pitchD = ks[SDL_SCANCODE_F] != 0;
            li.rollL  = ks[SDL_SCANCODE_Q] != 0;
            li.rollR  = ks[SDL_SCANCODE_E] != 0;
            li.fire   = ks[SDL_SCANCODE_SPACE] != 0 || localFireClick || localFireHeld;
            li.warp   = ks[SDL_SCANCODE_LSHIFT] != 0 || ks[SDL_SCANCODE_RSHIFT] != 0;
            li.mineToggle = localMineEdge;
            li.dock   = localDockEdge;
            li.cycleTarget = localTargetEdge;
            // Мышь-взгляд: относительные дельты -> доп. поворот за кадр (только в кокпите).
            if (relMouseOn) {
                int mdx = 0, mdy = 0;
                SDL_GetRelativeMouseState(&mdx, &mdy);
                li.mouseYaw   =  mdx * LOCAL_MOUSE_SENS;   // мышь вправо => нос вправо
                li.mousePitch = -mdy * LOCAL_MOUSE_SENS;   // мышь вверх  => нос вверх (не инвертирован)
            }
            if (localSmoke) {
                // Синтетический ввод для headless-прогона всех веток:
                li.fire = true;                 // снаряды: спавн/интеграция/коллизии
                li.mineToggle = (frames == 0);  // фронт: включить добычу у астероида
                li.thrust = (frames >= 4);      // позже — инерционный полёт
                li.warp   = (frames >= 6);      // позже — субшаги/клэмп N при большом dt
            }
            const int dockStar = updateLocalScene(game, localScene, li, realDt);
            // Камера (localView/localBasis) строится ниже, у самого рендера, единым
            // помощником buildLocalCamera (тот же код, что и в скриншот-харнесе).
            if (localScene.playerDestroyed) {
                // Корпус разрушен в микромире — аварийный прыжок (не терминально):
                // корабль деградирует в спасательную капсулу (потеря груза).
                localScene.active = false;
                if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
                    Agent& pa = game.agents[game.playerAgent];
                    extern void downgradeAgentToEscapePod(Agent&);
                    downgradeAgentToEscapePod(pa);
                    pa.ship.hullHP = pa.ship.maxHullHP; // escape pod is intact
                }
                game.lastEvent = "ship destroyed — using escape pod";
                game.pushNews("Ship destroyed! Cargo lost, using escape pod.", 3);

            } else if (dockStar >= 0) {
                localScene.active = false;
                selectedStar = dockStar;
                UI::openTradeWindow(ui, dockStar, winW, winH);
                game.lastEvent = "docked — market open";
            }
        } else if (!paused && !game.pendingTariff) {
            advanceGame(game, realDt * simYearsPerSecond);
        }
        localMineEdge = localDockEdge = localTargetEdge = localFireClick = false;
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
        if (localScene.active) {
            CameraBasis localBasis;
            buildLocalCamera(localScene, winW, winH, localMapMode, localFovScale, localZoom,
                             localView, localBasis);
            renderLocalScene(renderer, game, localScene, localView, localBasis, winW, winH);
            { std::vector<ActionButton> bar; buildBar(bar); drawBar(bar); }
            SDL_RenderPresent(renderer);
            if (smoke && ++frames >= 12) quit = true;
            if (!quit) {
                const Uint64 frameEndLocal = SDL_GetPerformanceCounter();
                const double frameElapsedLocal = double(frameEndLocal - frameStart) / double(perfFrequency);
                if (frameElapsedLocal < TARGET_FRAME_SECONDS) {
                    SDL_Delay(Uint32(std::ceil((TARGET_FRAME_SECONDS - frameElapsedLocal) * 1000.0)));
                }
            }
            continue;
        }
        const CameraBasis cameraBasis = makeCameraBasis(view);

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

            if (view.scale > 100.0) {
                double dx = s.x - view.centerX;
                double dy = s.y - view.centerY;
                double dz = s.z - view.centerZ;
                if (dx * dx + dy * dy + dz * dz > 225.0) continue;
            }

            const bool ownerKnown = game.playerKnowsOwner(int(i));
            const int knownOwner = game.playerKnownOwner(int(i));
            const bool liveInfo = game.playerAtStar(int(i));
            const int baseSize = liveInfo ? 2 + (s.industry > 1.7 ? 1 : 0) + (ownerKnown && knownOwner >= 0 ? 1 : 0) : (ownerKnown ? 3 : 2);
            int size = baseSize;
            if (view.scale > 100.0) {
                double zoomFactor = (view.scale - 100.0) / 25.0 * (1.0 + 0.4 * std::sqrt(s.radius));
                size = std::min(60, int(baseSize * (1.0 + zoomFactor)));
            }

            if (ownerKnown) {
                setFactionColor(renderer, game, knownOwner, liveInfo ? 170 : 75);
                int ring = (view.scale > 100.0) ? (size * 2 + (liveInfo && s.defense > 5.0 ? 12 : 8)) : (liveInfo && s.defense > 5.0 ? 6 : 5);
                SDL_Rect halo = {sx - ring / 2, sy - ring / 2, ring, ring};
                SDL_RenderDrawRect(renderer, &halo);
            }

            Uint8 r = s.colorR;
            Uint8 g = s.colorG;
            Uint8 b = s.colorB;
            
            if (view.scale <= 100.0) {
                if (liveInfo) {
                    marketColor(game.markets[i], selectedElement, r, g, b);
                } else if (ownerKnown) {
                    factionColor(game, knownOwner, r, g, b);
                }
            } else {
                if (selectedElement >= 0 && liveInfo) {
                    Uint8 mr, mg, mb;
                    marketColor(game.markets[i], selectedElement, mr, mg, mb);
                    SDL_SetRenderDrawColor(renderer, mr, mg, mb, 200);
                    int mRing = size * 2 + 20;
                    SDL_Rect mRect = {sx - mRing / 2, sy - mRing / 2, mRing, mRing};
                    SDL_RenderDrawRect(renderer, &mRect);
                }
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

        // Аномалии — только обнаруженные и ещё не обследованные
        for (size_t i = 0; i < game.anomalies.size(); ++i) {
            const Anomaly& a = game.anomalies[i];
            if (!a.discovered || a.resolved) continue;
            const ProjectedPoint p = projectPointWithBasis(a.x, a.y, a.z, winW, winH, view, cameraBasis);
            const int sx = p.x;
            const int sy = p.y;
            if (sx < -6 || sx > winW + 6 || sy < -6 || sy > winH + 6) continue;
            Uint8 ar = 180, ag = 180, ab = 190;
            switch (a.kind) {
                case AnomalyKind::DerelictShip: ar = 170; ag = 170; ab = 180; break;
                case AnomalyKind::AncientCache: ar = 230; ag = 190; ab = 90; break;
                case AnomalyKind::ChromocoreVault: ar = 210; ag = 120; ab = 235; break;
                case AnomalyKind::NebulaEcho: ar = 110; ag = 170; ab = 230; break;
                case AnomalyKind::IonStorm: ar = 230; ag = 90; ab = 90; break;
            }
            const double pulse = 0.6 + 0.4 * std::sin(game.time * 3.0 + double(i) * 2.1);
            const Uint8 alpha = Uint8(150.0 + 90.0 * pulse);
            SDL_SetRenderDrawColor(renderer, ar, ag, ab, alpha);
            const int d = 5;
            SDL_Point diamond[5] = {{sx, sy - d}, {sx + d, sy}, {sx, sy + d}, {sx - d, sy}, {sx, sy - d}};
            SDL_RenderDrawLines(renderer, diamond, 5);
        }

        if (++titleTick % 12 == 0) {
            char title[1024];
            const ElementDefinition& element = elementDefinitions()[selectedElement];
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
                        "Starcluster | pos x %.1f y %.1f z %.1f | t %.1f rate %.2fy/s spd x%.1f %s | view %s | factions %zu colonies %zu captures %d | %s owner %s %s pop %.0f ind %.2f hab %.2f def %.1f | %s price %.1f supply %.1f demand %.1f | shortage %s x%.1f surplus %s x%.1f | %s %s/%s -> %s %.2fc fuel %.0f%% mass %.0f %s cr %.0f tr %d last %.0f %s | F5 save F9 load H hire/build C colony/reinforce | %s%s",
                        coordX, coordY, coordZ, game.time, simYearsPerSecond, simSpeed, (paused ? "PAUSED" : "LIVE"), element.symbol, game.factions.size(), game.colonies.size(), game.capturedSystems,
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
                        "Starcluster | pos x %.1f y %.1f z %.1f | t %.1f rate %.2fy/s spd x%.1f %s | view %s | factions %zu colonies %zu captures %d | %s owner %s | %s | %s %s/%s -> %s %.2fc fuel %.0f%% mass %.0f %s cr %.0f tr %d last %.0f %s | F5 save F9 load H hire/build C colony/reinforce | %s%s",
                        coordX, coordY, coordZ, game.time, simYearsPerSecond, simSpeed, (paused ? "PAUSED" : "LIVE"), element.symbol, game.factions.size(), game.colonies.size(), game.capturedSystems,
                        star.name.c_str(), ownerName.c_str(), marketLine.c_str(),
                        shipName, agentType.c_str(), agentOwner.c_str(), targetName.c_str(), speed, fuel, mass, cargo, money, trades, profit, action.c_str(),
                        game.lastEvent.c_str(), followAgent ? " follow" : "");
                }
            } else {
                std::snprintf(title, sizeof(title), "Starcluster | pos x %.1f y %.1f z %.1f | t %.1f rate %.2fy/s spd x%.1f %s | view %s | factions %zu colonies %zu founded %d captures %d | traders %d military %d colonists %d | %s %s/%s -> %s %.2fc fuel %.0f%% mass %.0f %s money %.0f | F5 save F9 load RMB/G route B buy S sell T auto H hire/build C colony/reinforce | %s%s",
                    coordX, coordY, coordZ, game.time, simYearsPerSecond, simSpeed, (paused ? "PAUSED" : "LIVE"), element.symbol, game.factions.size(), game.colonies.size(), game.foundedColonies, game.capturedSystems,
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
        UI::updateVisualNovel(ui, game, realDt, winW, winH);
        UI::drawHud(renderer, game, winW, winH, hud);
        UI::drawWindows(renderer, game, winW, winH, hud, ui);
        { std::vector<ActionButton> bar; buildBar(bar); drawBar(bar); }
        
        UI::drawVisualNovel(renderer, ui, winW, winH, timertiaTex);
        UI::drawTariffModal(renderer, game, winW, winH);

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

    for (Mix_Music* m : playlist) {
        Mix_FreeMusic(m);
    }
    Mix_CloseAudio();

    if (timertiaTex) SDL_DestroyTexture(timertiaTex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
