#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace UI {
namespace {

struct Palette {
    SDL_Color panel = {12, 18, 34, 218};
    SDL_Color panel2 = {22, 30, 52, 230};
    SDL_Color border = {84, 112, 150, 210};
    SDL_Color text = {214, 228, 238, 255};
    SDL_Color dim = {116, 136, 158, 255};
    SDL_Color cyan = {82, 222, 246, 255};
    SDL_Color amber = {245, 191, 78, 255};
    SDL_Color red = {238, 88, 82, 255};
    SDL_Color green = {90, 220, 132, 255};
};

const Palette P;

struct TradeLayout {
    int x = 0;
    int y = 0;
    int panelW = 0;
    int panelH = 0;
    int tableX = 0;
    int tableY = 0;
    int cellW = 0;
    int cellH = 0;
    SDL_Rect amount = {0, 0, 0, 0};
    SDL_Rect buy = {0, 0, 0, 0};
    SDL_Rect sell = {0, 0, 0, 0};
    SDL_Rect autoTrade = {0, 0, 0, 0};
    SDL_Rect refuel = {0, 0, 0, 0};
};

struct SystemLayout {
    SDL_Rect route = {0, 0, 0, 0};
    SDL_Rect trade = {0, 0, 0, 0};
    SDL_Rect contracts = {0, 0, 0, 0};
    SDL_Rect colony = {0, 0, 0, 0};
};

struct KnownFactionSummary {
    int knownSystems = 0;
    double newestAge = -1.0;
    double confidence = 0.0;
};

struct PlayerMarketView {
    bool live = false;
    double age = -1.0;
    double confidence = 0.0;
    double price = 0.0;
    double supplyPressure = 1.0;
    double demandPressure = 1.0;
};

struct KnownFactionSummaryCache {
    const Game* game = nullptr;
    size_t factionCount = 0;
    size_t starCount = 0;
    double refreshedAt = -1.0;
    std::vector<KnownFactionSummary> summaries;
};

KnownFactionSummaryCache gKnownFactionSummaryCache;

const int TITLE_H = 24;
const int WINDOW_PAD = 10;
const int CONTRACT_ROW_H = 34;

bool contains(const SDL_Rect& rect, int x, int y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

int clampInt(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

SDL_Rect clampRectToScreen(SDL_Rect rect, int screenW, int screenH) {
    const int minW = std::min(rect.w, std::max(80, screenW - 24));
    const int minH = std::min(rect.h, std::max(80, screenH - 24));
    rect.w = minW;
    rect.h = minH;
    rect.x = clampInt(rect.x, 8, std::max(8, screenW - rect.w - 8));
    rect.y = clampInt(rect.y, 8, std::max(8, screenH - rect.h - 8));
    return rect;
}

SDL_Rect defaultWindowRect(WindowKind kind, int screenW, int screenH, int cascade) {
    SDL_Rect rect = {0, 0, 0, 0};
    if (kind == WindowKind::Trade) {
        rect.w = std::min(1000, std::max(740, screenW - 220));
        rect.h = std::min(520, std::max(420, screenH - 180));
        rect.x = std::max(300, (screenW - rect.w) / 2 + cascade * 18);
        rect.y = std::max(70, screenH - rect.h - 28 - cascade * 10);
    } else if (kind == WindowKind::Contracts) {
        rect.w = std::min(660, std::max(520, screenW - 520));
        rect.h = 374;
        rect.x = std::max(344, (screenW - rect.w) / 2 + cascade * 18);
        rect.y = std::max(96, screenH - rect.h - 46 - cascade * 10);
    } else {
        rect.w = 362;
        rect.h = 286;
        rect.x = std::max(352, std::min(screenW - rect.w - 280, 372 + cascade * 22));
        rect.y = 82 + cascade * 22;
    }
    return clampRectToScreen(rect, screenW, screenH);
}

const Ship* hudShip(const Game& game, const HudSelection& selection) {
    if (selection.agent >= 0 && selection.agent < int(game.agents.size())) return &game.agents[selection.agent].ship;
    if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) return &game.agents[game.playerAgent].ship;
    return nullptr;
}

SDL_Rect titleRect(const Window& window) {
    return {window.rect.x, window.rect.y, window.rect.w, TITLE_H};
}

SDL_Rect closeRect(const Window& window) {
    return {window.rect.x + window.rect.w - 22, window.rect.y + 4, 16, 16};
}

SystemLayout systemLayout(const Window& window) {
    SystemLayout layout;
    const int y = window.rect.y + window.rect.h - 38;
    layout.route = {window.rect.x + WINDOW_PAD, y, 70, 24};
    layout.trade = {window.rect.x + WINDOW_PAD + 78, y, 70, 24};
    layout.contracts = {window.rect.x + WINDOW_PAD + 156, y, 92, 24};
    layout.colony = {window.rect.x + WINDOW_PAD + 256, y, 82, 24};
    return layout;
}

TradeLayout tradeLayoutForWindow(const Window& window) {
    TradeLayout layout;
    layout.x = window.rect.x;
    layout.y = window.rect.y;
    layout.panelW = window.rect.w;
    layout.panelH = window.rect.h;
    layout.cellW = std::max(24, std::min(42, (window.rect.w - 54 - 178) / 18));
    layout.cellH = std::max(24, std::min(32, layout.cellW - 5));
    const int tableW = layout.cellW * 18;
    layout.tableX = window.rect.x + WINDOW_PAD;
    layout.tableY = window.rect.y + TITLE_H + 82;
    const int bx = layout.tableX + tableW + 16;
    const int buttonW = std::max(136, window.rect.x + window.rect.w - bx - WINDOW_PAD);
    layout.amount = {bx, window.rect.y + TITLE_H + 45, buttonW, 30};
    layout.buy = {bx, layout.tableY, buttonW, 28};
    layout.sell = {bx, layout.tableY + 36, buttonW, 28};
    layout.autoTrade = {bx, layout.tableY + 72, buttonW, 28};
    layout.refuel = {bx, layout.tableY + 108, buttonW, 28};
    return layout;
}

void color(SDL_Renderer* renderer, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
}

void fillRect(SDL_Renderer* renderer, int x, int y, int w, int h, SDL_Color c) {
    SDL_Rect r = {x, y, w, h};
    color(renderer, c);
    SDL_RenderFillRect(renderer, &r);
}

void strokeRect(SDL_Renderer* renderer, int x, int y, int w, int h, SDL_Color c) {
    SDL_Rect r = {x, y, w, h};
    color(renderer, c);
    SDL_RenderDrawRect(renderer, &r);
}

void panel(SDL_Renderer* renderer, int x, int y, int w, int h) {
    fillRect(renderer, x, y, w, h, P.panel);
    strokeRect(renderer, x, y, w, h, P.border);
    fillRect(renderer, x + 1, y + 1, w - 2, 1, {128, 174, 210, 65});
}

const char* glyph(char ch) {
    switch (ch) {
    case 'A': return "01110100011000111111100011000110001";
    case 'B': return "111101000111110100011000111110";
    case 'C': return "01111100001000010000100001000001111";
    case 'D': return "11110100011000110001100011000111110";
    case 'E': return "11111100001111010000100001000011111";
    case 'F': return "11111100001111010000100001000010000";
    case 'G': return "01111100001000010111100011000101111";
    case 'H': return "10001100011000111111100011000110001";
    case 'I': return "11111001000010000100001000010011111";
    case 'J': return "00111000100001000010100101001001100";
    case 'K': return "10001100101010011000101001001010001";
    case 'L': return "10000100001000010000100001000011111";
    case 'M': return "10001110111010110101100011000110001";
    case 'N': return "10001110011010110011100011000110001";
    case 'O': return "01110100011000110001100011000101110";
    case 'P': return "11110100011000111110100001000010000";
    case 'Q': return "01110100011000110001101011001001101";
    case 'R': return "11110100011000111110101001001010001";
    case 'S': return "01111100001000001110000010000111110";
    case 'T': return "11111001000010000100001000010000100";
    case 'U': return "10001100011000110001100011000101110";
    case 'V': return "10001100011000110001100010101000100";
    case 'W': return "10001100011000110101101011101110001";
    case 'X': return "10001100010101000100010101000110001";
    case 'Y': return "10001100010101000100001000010000100";
    case 'Z': return "11111000010001000100010001000011111";
    case '0': return "01110100011001110101110011000101110";
    case '1': return "00100011000010000100001000010001110";
    case '2': return "01110100010000100010001000100011111";
    case '3': return "11110000010000101110000010000111110";
    case '4': return "00010001100101010010111110001000010";
    case '5': return "11111100001111000001000010000111110";
    case '6': return "00110010001000011110100011000101110";
    case '7': return "11111000010001000100010000100001000";
    case '8': return "01110100011000101110100011000101110";
    case '9': return "01110100011000101111000010001001100";
    case '-': return "00000000000000011111000000000000000";
    case '+': return "00000001000010011111001000010000000";
    case '.': return "00000000000000000000000000110001100";
    case ':': return "00000011000110000000011000110000000";
    case '/': return "00001000010001000100010001000010000";
    case '%': return "11001000010001000100010001000010011";
    case '[': return "01110010000100001000010000100001110";
    case ']': return "01110000100001000010000100001001110";
    case '>': return "10000010000010000010001001000010000";
    case '<': return "00001000100010001000001000010000001";
    case ' ': return "00000000000000000000000000000000000";
    default: return "11111000010001000100010000000000100";
    }
}

void drawText(SDL_Renderer* renderer, int x, int y, const std::string& text, SDL_Color c, int scale = 2) {
    color(renderer, c);
    int penX = x;
    int penY = y;
    for (char raw : text) {
        if (raw == '\n') {
            penX = x;
            penY += 8 * scale;
            continue;
        }
        char ch = raw;
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
        const char* bits = glyph(ch);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (bits[row * 5 + col] == '1') {
                    SDL_Rect r = {penX + col * scale, penY + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &r);
                }
            }
        }
        penX += 6 * scale;
    }
}

std::string fmt(const char* format, double value) {
    char text[64];
    std::snprintf(text, sizeof(text), format, value);
    return text;
}

std::string fmtInt(const char* label, int value) {
    char text[64];
    std::snprintf(text, sizeof(text), "%s %d", label, value);
    return text;
}

double clamp01(double v) {
    return std::max(0.0, std::min(1.0, v));
}

void bar(SDL_Renderer* renderer, int x, int y, int w, int h, double value, SDL_Color c) {
    fillRect(renderer, x, y, w, h, {8, 12, 22, 230});
    const int filled = std::max(0, std::min(w, int(std::round(w * clamp01(value)))));
    if (filled > 0) fillRect(renderer, x, y, filled, h, c);
    strokeRect(renderer, x, y, w, h, {76, 96, 124, 220});
}

void labelBar(SDL_Renderer* renderer, int x, int y, int w, const std::string& label, double value, SDL_Color c) {
    drawText(renderer, x, y, label, P.dim, 1);
    bar(renderer, x + 55, y - 1, w - 55, 7, value, c);
}

double marketPressure(const Market& market, int elementIndex) {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    if (elementIndex < 0 || elementIndex >= int(market.prices.size()) || elementIndex >= int(elements.size())) {
        return market.pricePressure();
    }
    return market.prices[elementIndex] / elements[elementIndex].basePrice;
}

bool playerMarketView(const Game& game, int starIndex, int elementIndex, PlayerMarketView& view) {
    if (starIndex < 0 || elementIndex < 0 || elementIndex >= int(elementCount())) return false;
    if (!game.playerKnowsMarket(starIndex)) return false;

    view.live = game.playerAtStar(starIndex);
    view.age = game.playerKnownMarketAge(starIndex);
    view.confidence = clamp01(game.playerKnownMarketConfidence(starIndex, elementIndex));
    view.price = game.playerKnownPrice(starIndex, elementIndex);
    view.supplyPressure = game.playerKnownSupplyPressure(starIndex, elementIndex);
    view.demandPressure = game.playerKnownDemandPressure(starIndex, elementIndex);
    return std::isfinite(view.price) && std::isfinite(view.supplyPressure) && std::isfinite(view.demandPressure);
}

void drawPlayerMarketView(SDL_Renderer* renderer, const PlayerMarketView& view, const ElementDefinition& element, int x, int& y) {
    char line[128];
    if (view.live) {
        std::snprintf(line, sizeof(line), "MARKET LIVE/LOCAL CONF %.0F%%", view.confidence * 100.0);
        drawText(renderer, x, y, line, P.green, 1);
    } else {
        std::snprintf(line, sizeof(line), "MARKET SNAPSHOT %.0FY CONF %.0F%%", view.age, view.confidence * 100.0);
        drawText(renderer, x, y, line, P.cyan, 1);
    }
    y += 15;
    std::snprintf(line, sizeof(line), "%s P %.1F SUP X%.2F DEM X%.2F",
        element.symbol, view.price, view.supplyPressure, view.demandPressure);
    drawText(renderer, x, y, line, P.amber, 1);
    y += 15;
}

const std::vector<KnownFactionSummary>& knownFactionSummaries(const Game& game) {
    const size_t factionCount = game.factions.size();
    const size_t starCount = game.cluster.stars.size();
    const bool stale = gKnownFactionSummaryCache.game != &game ||
        gKnownFactionSummaryCache.factionCount != factionCount ||
        gKnownFactionSummaryCache.starCount != starCount ||
        gKnownFactionSummaryCache.refreshedAt < 0.0 ||
        game.time < gKnownFactionSummaryCache.refreshedAt ||
        game.time - gKnownFactionSummaryCache.refreshedAt >= 0.5;

    if (!stale) return gKnownFactionSummaryCache.summaries;

    gKnownFactionSummaryCache.game = &game;
    gKnownFactionSummaryCache.factionCount = factionCount;
    gKnownFactionSummaryCache.starCount = starCount;
    gKnownFactionSummaryCache.refreshedAt = game.time;
    gKnownFactionSummaryCache.summaries.assign(factionCount, KnownFactionSummary());

    std::vector<double> confidenceSums(factionCount, 0.0);
    for (size_t i = 0; i < game.cluster.stars.size(); ++i) {
        const int starIndex = int(i);
        if (!game.playerKnowsOwner(starIndex)) continue;
        const int factionIndex = game.playerKnownOwner(starIndex);
        if (factionIndex < 0 || factionIndex >= int(factionCount)) continue;
        const double age = game.playerKnownOwnerAge(starIndex);
        KnownFactionSummary& summary = gKnownFactionSummaryCache.summaries[factionIndex];
        summary.knownSystems += 1;
        if (summary.newestAge < 0.0 || (age >= 0.0 && age < summary.newestAge)) summary.newestAge = age;
        confidenceSums[factionIndex] += age >= 0.0 ? std::exp(-age / 32.0) : 0.0;
    }
    for (size_t i = 0; i < factionCount; ++i) {
        KnownFactionSummary& summary = gKnownFactionSummaryCache.summaries[i];
        if (summary.knownSystems > 0) summary.confidence = confidenceSums[i] / double(summary.knownSystems);
    }
    return gKnownFactionSummaryCache.summaries;
}

const Faction* factionAt(const Game& game, int index) {
    if (index < 0 || index >= int(game.factions.size())) return nullptr;
    return &game.factions[index];
}

const ClusterStar* starAt(const Game& game, int index) {
    if (index < 0 || index >= int(game.cluster.stars.size())) return nullptr;
    return &game.cluster.stars[index];
}

const Colony* colonyAtStar(const Game& game, int starIndex) {
    for (size_t i = 0; i < game.colonies.size(); ++i) {
        if (game.colonies[i].starIndex == starIndex) return &game.colonies[i];
    }
    return nullptr;
}

SDL_Color factionColor(const Faction& faction, Uint8 alpha = 255) {
    return {Uint8(std::max(0, std::min(255, faction.colorR))),
        Uint8(std::max(0, std::min(255, faction.colorG))),
        Uint8(std::max(0, std::min(255, faction.colorB))), alpha};
}

void header(SDL_Renderer* renderer, int x, int y, const std::string& title) {
    drawText(renderer, x, y, title, P.cyan, 2);
}

int cargoAmount(const Agent& agent) {
    int total = 0;
    for (size_t i = 0; i < agent.ship.cargo.size(); ++i) total += int(agent.ship.cargo[i].amount);
    return total;
}

double speedOf(const Agent& agent) {
    const Ship& ship = agent.ship;
    return std::sqrt(ship.vx * ship.vx + ship.vy * ship.vy + ship.vz * ship.vz);
}

bool periodicCell(int atomicNumber, int& col, int& row) {
    row = 0;
    col = 0;
    if (atomicNumber == 1) { col = 0; row = 0; return true; }
    if (atomicNumber == 2) { col = 17; row = 0; return true; }
    if (atomicNumber >= 3 && atomicNumber <= 10) {
        row = 1;
        const int cols[] = {0, 1, 12, 13, 14, 15, 16, 17};
        col = cols[atomicNumber - 3];
        return true;
    }
    if (atomicNumber >= 11 && atomicNumber <= 18) {
        row = 2;
        const int cols[] = {0, 1, 12, 13, 14, 15, 16, 17};
        col = cols[atomicNumber - 11];
        return true;
    }
    if (atomicNumber >= 19 && atomicNumber <= 36) {
        row = 3;
        col = atomicNumber - 19;
        return true;
    }
    if (atomicNumber >= 37 && atomicNumber <= 54) {
        row = 4;
        col = atomicNumber - 37;
        return true;
    }
    if (atomicNumber == 55) { col = 0; row = 5; return true; }
    if (atomicNumber == 56) { col = 1; row = 5; return true; }
    if (atomicNumber >= 57 && atomicNumber <= 71) {
        row = 7;
        col = atomicNumber - 54;
        return true;
    }
    if (atomicNumber >= 72 && atomicNumber <= 86) {
        row = 5;
        col = atomicNumber - 69;
        return true;
    }
    if (atomicNumber == 87) { col = 0; row = 6; return true; }
    if (atomicNumber == 88) { col = 1; row = 6; return true; }
    if (atomicNumber >= 89 && atomicNumber <= 103) {
        row = 8;
        col = atomicNumber - 86;
        return true;
    }
    if (atomicNumber >= 104 && atomicNumber <= 118) {
        row = 6;
        col = atomicNumber - 101;
        return true;
    }
    return false;
}

SDL_Rect elementRect(const TradeLayout& layout, int elementIndex) {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    if (elementIndex < 0 || elementIndex >= int(elements.size())) return {0, 0, 0, 0};
    int col = 0;
    int row = 0;
    if (!periodicCell(elements[elementIndex].atomicNumber, col, row)) return {0, 0, 0, 0};
    return {layout.tableX + col * layout.cellW, layout.tableY + row * layout.cellH, layout.cellW - 2, layout.cellH - 2};
}

bool hasFocus(const std::vector<int>& focus, int elementIndex) {
    return std::find(focus.begin(), focus.end(), elementIndex) != focus.end();
}

SDL_Color marketCellColor(const Market& market, int elementIndex) {
    const double pressure = marketPressure(market, elementIndex);
    if (pressure > 1.25) {
        const Uint8 r = Uint8(std::min(245.0, 88.0 + pressure * 46.0));
        return {r, 66, 70, 225};
    }
    if (pressure < 0.75) {
        const Uint8 g = Uint8(std::min(235.0, 118.0 + (1.0 - pressure) * 180.0));
        return {52, g, 142, 225};
    }
    return {56, 82, 116, 220};
}

int playerMarketStar(const Game& game) {
    if (game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) return -1;
    const Agent& player = game.agents[game.playerAgent];
    if (player.ship.enRoute) return -1;
    return player.currentStar;
}

double tradeRequestedAmount(const WindowState& state) {
    if (state.tradeAmount.empty()) return 1.0e12;
    char* end = nullptr;
    const double amount = std::strtod(state.tradeAmount.c_str(), &end);
    if (end == state.tradeAmount.c_str() || !std::isfinite(amount) || amount <= 0.0) return 0.0;
    return std::min(amount, 1.0e12);
}

std::string tradeAmountLabel(const WindowState& state) {
    if (state.tradeAmount.empty()) return "MAX";
    return state.tradeAmount;
}

int findWindowIndex(const WindowState& state, int id) {
    for (size_t i = 0; i < state.windows.size(); ++i) {
        if (state.windows[i].id == id) return int(i);
    }
    return -1;
}

int findWindowIndex(const WindowState& state, WindowKind kind, int starIndex) {
    for (size_t i = 0; i < state.windows.size(); ++i) {
        if (state.windows[i].kind == kind && state.windows[i].star == starIndex) return int(i);
    }
    return -1;
}

Window* findWindow(WindowState& state, int id) {
    const int index = findWindowIndex(state, id);
    if (index < 0) return nullptr;
    return &state.windows[index];
}

void bringWindowToFront(WindowState& state, int id) {
    const int index = findWindowIndex(state, id);
    if (index < 0 || index == int(state.windows.size()) - 1) {
        state.activeId = id;
        return;
    }
    Window window = state.windows[index];
    state.windows.erase(state.windows.begin() + index);
    state.windows.push_back(window);
    state.activeId = id;
}

void openWindow(WindowState& state, WindowKind kind, int starIndex, int screenW, int screenH) {
    const int existing = findWindowIndex(state, kind, starIndex);
    if (existing >= 0) {
        bringWindowToFront(state, state.windows[existing].id);
        return;
    }

    Window window;
    window.id = state.nextId++;
    window.kind = kind;
    window.star = starIndex;
    window.rect = defaultWindowRect(kind, screenW, screenH, int(state.windows.size()));
    state.windows.push_back(window);
    state.activeId = window.id;
}

std::string factionName(const Game& game, int factionIndex) {
    if (factionIndex >= 0 && factionIndex < int(game.factions.size())) {
        return game.factions[factionIndex].name;
    }
    return "FREE";
}

std::string ownerKnowledgeLine(const Game& game, int starIndex) {
    if (!game.playerKnowsOwner(starIndex)) return "OWNER UNKNOWN";
    const int owner = game.playerKnownOwner(starIndex);
    char line[96];
    std::snprintf(line, sizeof(line), "%s %.0FY",
        game.playerAtStar(starIndex) ? "LIVE" : "LAST SEEN",
        game.playerKnownOwnerAge(starIndex));
    return "OWNER " + factionName(game, owner) + " / " + line;
}

void drawRoutePreview(SDL_Renderer* renderer, const Game& game, int starIndex, int elementIndex, int x, int& y, int w) {
    if (game.playerAgent < 0) return;

    const double distance = game.agentRouteDistance(game.playerAgent, starIndex);
    const double years = game.agentRouteTravelTime(game.playerAgent, starIndex);
    const double fuelNeeded = game.agentRouteFuelNeeded(game.playerAgent, starIndex);
    const double shortfall = game.agentRouteFuelShortfall(game.playerAgent, starIndex);
    const double risk = game.agentRouteThreatRisk(game.playerAgent, starIndex);
    if (distance < 0.0 || years < 0.0 || fuelNeeded < 0.0 ||
        !std::isfinite(distance) || !std::isfinite(years) || !std::isfinite(fuelNeeded)) {
        return;
    }

    const bool fuelShort = shortfall > 0.05;
    const SDL_Color primary = fuelShort ? P.red : P.cyan;
    const SDL_Color secondary = fuelShort ? P.amber : P.dim;
    char route[160];
    char detail[160];

    if (fuelShort) {
        std::snprintf(route, sizeof(route), "ROUTE %.1FLY %.1FY FUEL %.0F SHORT %.0F",
            distance, years, fuelNeeded, shortfall);
    } else {
        std::snprintf(route, sizeof(route), "ROUTE %.1FLY %.1FY FUEL %.0F OK",
            distance, years, fuelNeeded);
    }

    const bool hasElement = elementIndex >= 0 && elementIndex < int(elementCount());
    if (hasElement) {
        const ElementDefinition& element = elementDefinitions()[elementIndex];
        const double confidence = clamp01(game.playerRouteMarketConfidence(starIndex, elementIndex));
        std::snprintf(detail, sizeof(detail), "RISK %.0F%% MARKET %s CONF %.0F%%",
            risk * 100.0, element.symbol, confidence * 100.0);
    } else {
        std::snprintf(detail, sizeof(detail), "RISK %.0F%% MARKET CONF NONE", risk * 100.0);
    }

    const int maxW = w - WINDOW_PAD * 2;
    const int routeW = int(std::strlen(route)) * 6;
    const int detailW = int(std::strlen(detail)) * 6;
    if (routeW + 6 + detailW <= maxW) {
        drawText(renderer, x, y, route, primary, 1);
        drawText(renderer, x + routeW + 6, y, detail, secondary, 1);
        y += 16;
    } else {
        drawText(renderer, x, y, route, primary, 1);
        y += 16;
        drawText(renderer, x, y, detail, secondary, 1);
        y += 16;
    }
}

const char* contractTypeLabel(ContractType type) {
    switch (type) {
    case ContractType::Delivery: return "DEL";
    case ContractType::Courier: return "CUR";
    case ContractType::Scout: return "SCT";
    case ContractType::Bounty: return "BNT";
    case ContractType::Escort: return "ESC";
    case ContractType::Raid: return "RAD";
    case ContractType::ColonySupply: return "SUP";
    }
    return "JOB";
}

bool contractUsesCargo(ContractType type) {
    return type == ContractType::Delivery || type == ContractType::ColonySupply;
}

}

void drawStarPanel(SDL_Renderer* renderer, const Game& game, int starIndex, int elementIndex, int x, int y, int w) {
    const ClusterStar* star = starAt(game, starIndex);
    if (!star) return;

    const int h = 150;
    panel(renderer, x, y, w, h);
    header(renderer, x + 10, y + 9, star->name.empty() ? "STAR" : star->name);

    const bool ownerKnown = game.playerKnowsOwner(starIndex);
    const int knownOwner = game.playerKnownOwner(starIndex);
    const bool liveInfo = game.playerAtStar(starIndex);
    const Faction* owner = ownerKnown ? factionAt(game, knownOwner) : nullptr;
    drawText(renderer, x + 10, y + 30, "OWNER", P.dim, 1);
    if (!ownerKnown) {
        drawText(renderer, x + 52, y + 30, "UNKNOWN", P.dim, 1);
    } else if (owner) {
        fillRect(renderer, x + 52, y + 28, 9, 9, factionColor(*owner));
        drawText(renderer, x + 66, y + 30, owner->name, P.text, 1);
    } else {
        drawText(renderer, x + 52, y + 30, "FREE", P.text, 1);
    }
    if (ownerKnown) {
        char age[48];
        std::snprintf(age, sizeof(age), "%s %.0FY", liveInfo ? "LIVE" : "LAST", game.playerKnownOwnerAge(starIndex));
        drawText(renderer, x + w - 70, y + 30, age, liveInfo ? P.green : P.amber, 1);
    }

    PlayerMarketView marketView;
    const bool marketKnown = playerMarketView(game, starIndex, elementIndex, marketView);

    if (!liveInfo) {
        drawText(renderer, x + 10, y + 52, marketKnown ? "REMOTE DATA FROM SIGNALS" : "SYSTEM DATA UNKNOWN UNTIL ARRIVAL", P.dim, 1);
        drawText(renderer, x + 10, y + 68, marketKnown ? "OWNER/MARKET MAY BE STALE" : "OWNER OVERLAY IS LAST-KNOWN", P.amber, 1);
        if (marketKnown) {
            int marketY = y + 88;
            drawPlayerMarketView(renderer, marketView, elementDefinitions()[elementIndex], x + 10, marketY);
        } else {
            drawText(renderer, x + 10, y + 88, "MARKET UNKNOWN / NO SNAPSHOT", P.dim, 1);
            drawText(renderer, x + 10, y + 103, "LOCAL AGENTS HIDDEN", P.dim, 1);
        }
        return;
    }

    drawText(renderer, x + 10, y + 43, "ROLE " + star->economyRole, P.text, 1);
    if (star->captureProgress > 0.0 && star->occupyingFaction >= 0) {
        char cap[96];
        std::snprintf(cap, sizeof(cap), "CONTEST %s %.0F%%",
            factionName(game, star->occupyingFaction).c_str(), star->captureProgress * 100.0);
        drawText(renderer, x + w - 126, y + 43, cap, P.red, 1);
    }
    labelBar(renderer, x + 10, y + 58, w - 20, "POP", star->population / 1400000.0, P.green);
    labelBar(renderer, x + 10, y + 72, w - 20, "IND", star->industry / 3.0, P.amber);
    labelBar(renderer, x + 10, y + 86, w - 20, "HAB", star->habitability, P.cyan);
    labelBar(renderer, x + 10, y + 100, w - 20, "DEF", star->defense / 10.0, P.red);

    if (marketKnown) {
        int marketY = y + 114;
        drawPlayerMarketView(renderer, marketView, elementDefinitions()[elementIndex], x + 10, marketY);
    }
}

void drawAgentPanel(SDL_Renderer* renderer, const Game& game, int agentIndex, int x, int y, int w) {
    if (agentIndex < 0 || agentIndex >= int(game.agents.size())) return;
    const Agent& agent = game.agents[agentIndex];

    const int h = 120;
    panel(renderer, x, y, w, h);
    header(renderer, x + 10, y + 9, agent.ship.name.empty() ? "AGENT" : agent.ship.name);

    SDL_Color typeColor = P.cyan;
    if (agent.type == "military") typeColor = P.red;
    if (agent.type == "colonist") typeColor = P.green;
    if (agent.playerControlled || agent.type == "player") typeColor = P.amber;
    drawText(renderer, x + 10, y + 31, "TYPE " + agent.type, typeColor, 1);

    const Faction* owner = factionAt(game, agent.ship.ownerFaction);
    drawText(renderer, x + 108, y + 31, owner ? owner->name : "FREE", owner ? factionColor(*owner) : P.dim, 1);

    const ClusterStar* here = starAt(game, agent.currentStar);
    const ClusterStar* dest = starAt(game, agent.destStar);
    drawText(renderer, x + 10, y + 46, std::string("FROM ") + (here ? here->name : "-"), P.text, 1);
    drawText(renderer, x + 10, y + 59, std::string("TO   ") + (dest ? dest->name : "-"), P.text, 1);

    labelBar(renderer, x + 10, y + 75, w - 20, "CARGO", shipCargoMass(agent.ship) / std::max(1.0, agent.ship.cargoCapacity), P.amber);

    char line[128];
    std::snprintf(line, sizeof(line), "MASS %.0F FUEL %.0F%%", shipTotalMass(agent.ship), shipFuelFraction(agent.ship) * 100.0);
    drawText(renderer, x + 10, y + 90, line, P.text, 1);
    std::snprintf(line, sizeof(line), "CR %.0F SPD %.2FC TR %d", agent.money, speedOf(agent), agent.trades);
    drawText(renderer, x + 10, y + 104, line, P.text, 1);
}

void drawFactionPanel(SDL_Renderer* renderer, const Game& game, int x, int y, int w) {
    const int rows = std::min(6, int(game.factions.size()));
    const std::vector<KnownFactionSummary>& summaries = knownFactionSummaries(game);
    const int h = 44 + rows * 19;
    panel(renderer, x, y, w, h);
    header(renderer, x + 10, y + 9, "FACTIONS");
    drawText(renderer, x + 10, y + 30, "KNOWN OWNER REPORTS", P.dim, 1);
    for (int i = 0; i < rows; ++i) {
        const Faction& faction = game.factions[i];
        const KnownFactionSummary known = i < int(summaries.size()) ? summaries[i] : KnownFactionSummary();
        const int rowY = y + 46 + i * 19;
        fillRect(renderer, x + 10, rowY, 9, 9, factionColor(faction));
        drawText(renderer, x + 25, rowY, faction.name, P.text, 1);
        bar(renderer, x + w - 96, rowY - 1, 28, 7, known.confidence, factionColor(faction));
        char count[40];
        std::snprintf(count, sizeof(count), "KNOWN %d", known.knownSystems);
        drawText(renderer, x + w - 62, rowY, count, known.knownSystems > 0 ? P.dim : P.red, 1);
        char meta[48];
        if (known.newestAge >= 0.0) {
            std::snprintf(meta, sizeof(meta), "LAST %.0FY CONF %.0F%%", known.newestAge, known.confidence * 100.0);
        } else {
            std::snprintf(meta, sizeof(meta), "LAST - CONF 0%%");
        }
        drawText(renderer, x + 25, rowY + 10, meta, known.knownSystems > 0 ? P.amber : P.dim, 1);
    }
}

void drawControlHints(SDL_Renderer* renderer, int screenW, int screenH) {
    const char* hints[] = {
        "LMB SELECT",
        "RMB ROUTE",
        "G GO",
        "B BUY",
        "V SELL",
        "T AUTO TRADE",
        "H HIRE SHIP",
        "C COL/REINF",
        "F5 SAVE",
        "F9 LOAD",
        "1-4 SPEED",
        "WHEEL ZOOM",
        "MMB DRAG PAN",
        "ARROWS PAN",
        "WASD ROTATE",
        "I INFLUENCE",
        "SPACE PAUSE",
        "TAB AGENT",
        "F FOLLOW",
        "[ ] MARKET",
        "P PLAYER",
        "0 RESET"
    };
    const int w = 144;
    const int h = 18 + int(sizeof(hints) / sizeof(hints[0])) * 12;
    const int x = std::max(8, screenW - w - 12);
    const int y = std::max(8, screenH - h - 12);
    panel(renderer, x, y, w, h);
    for (int i = 0; i < int(sizeof(hints) / sizeof(hints[0])); ++i) {
        drawText(renderer, x + 10, y + 10 + i * 12, hints[i], i == 0 ? P.cyan : P.dim, 1);
    }
}

void drawButton(SDL_Renderer* renderer, const SDL_Rect& rect, const std::string& label, SDL_Color c, bool enabled = true) {
    const SDL_Color stroke = enabled ? c : P.dim;
    const SDL_Color text = enabled ? c : SDL_Color{86, 98, 118, 255};
    fillRect(renderer, rect.x, rect.y, rect.w, rect.h, enabled ? SDL_Color{16, 22, 38, 235} : SDL_Color{12, 16, 26, 210});
    strokeRect(renderer, rect.x, rect.y, rect.w, rect.h, stroke);
    drawText(renderer, rect.x + 10, rect.y + 7, label, text, 1);
}

void drawLiveColonySummary(SDL_Renderer* renderer, const Game& game, int starIndex, int x, int& y) {
    const Colony* colony = colonyAtStar(game, starIndex);
    if (!colony) return;

    char line[128];
    std::snprintf(line, sizeof(line), "COLONY LEDGER %.0F SHIPYARD %d Q %zu",
        colony->localLedger,
        colonyShipHiringCapacity(*colony),
        colonyQueueCount(*colony));
    drawText(renderer, x, y, line, P.green, 1);
    y += 15;

    if (colonyQueueCount(*colony) > 0) {
        const ConstructionItem& item = colony->constructionQueue.front();
        const double progress = colonyQueueProgress(*colony) * 100.0;
        std::snprintf(line, sizeof(line), "QUEUE %s %.0F%% COST %.0F",
            colonyQueueLabel(*colony).c_str(), progress, item.cost);
        drawText(renderer, x, y, line, P.amber, 1);
        y += 15;
    } else {
        drawText(renderer, x, y, "QUEUE EMPTY / H HIRE-BUILD SHIP", P.dim, 1);
        y += 15;
    }
}

void drawContractRouteLine(SDL_Renderer* renderer, const Game& game, const Contract& contract, int x, int y, int maxW) {
    if (game.playerAgent < 0) return;

    const bool remoteOffer = contract.acceptedByAgent < 0 && !game.playerAtStar(contract.originStar);
    const double years = remoteOffer ?
        game.agentRouteTravelTime(game.playerAgent, contract.originStar) :
        game.agentContractRouteTravelTime(game.playerAgent, contract.id);
    const double fuelNeeded = remoteOffer ?
        game.agentRouteFuelNeeded(game.playerAgent, contract.originStar) :
        game.agentContractRouteFuelNeeded(game.playerAgent, contract.id);
    const double shortfall = remoteOffer ?
        game.agentRouteFuelShortfall(game.playerAgent, contract.originStar) :
        game.agentContractRouteFuelShortfall(game.playerAgent, contract.id);
    const double risk = remoteOffer ?
        game.agentRouteThreatRisk(game.playerAgent, contract.originStar) :
        game.agentContractRouteThreatRisk(game.playerAgent, contract.id);
    if (years < 0.0 || fuelNeeded < 0.0 ||
        !std::isfinite(years) || !std::isfinite(fuelNeeded) || !std::isfinite(risk)) {
        return;
    }

    const bool fuelShort = shortfall > 0.05;
    const bool cargoFits = remoteOffer || game.agentContractCargoFits(game.playerAgent, contract.id);
    const SDL_Color c = !cargoFits ? P.red : (fuelShort ? P.amber : P.dim);
    char line[160];
    if (fuelShort) {
        std::snprintf(line, sizeof(line), "%s %.0FY  FUEL %.0F SHORT %.0F  RISK %.0F%%%s",
            remoteOffer ? "BOARD" : "ETA", years, fuelNeeded, shortfall, risk * 100.0, cargoFits ? "" : " HOLD/HEAVY");
    } else {
        std::snprintf(line, sizeof(line), "%s %.0FY  FUEL %.0F OK  RISK %.0F%%%s",
            remoteOffer ? "BOARD" : "ETA", years, fuelNeeded, risk * 100.0, cargoFits ? "" : " HOLD/HEAVY");
    }
    if (int(std::strlen(line)) * 6 > maxW) {
        if (fuelShort) {
            std::snprintf(line, sizeof(line), "%.0FY F%.0F S%.0F R%.0F%%%s",
                years, fuelNeeded, shortfall, risk * 100.0, cargoFits ? "" : " HOLD/HEAVY");
        } else {
            std::snprintf(line, sizeof(line), "%.0FY F%.0F OK R%.0F%%%s",
                years, fuelNeeded, risk * 100.0, cargoFits ? "" : " HOLD/HEAVY");
        }
    }
    drawText(renderer, x, y, line, c, 1);
}

std::string focusList(const std::vector<int>& focus, int maxItems) {
    std::string out;
    for (size_t i = 0; i < focus.size() && int(i) < maxItems; ++i) {
        const int idx = focus[i];
        if (idx < 0 || idx >= int(elementCount())) continue;
        if (!out.empty()) out += " ";
        out += elementDefinitions()[idx].symbol;
    }
    return out.empty() ? "-" : out;
}

void openSystemWindow(WindowState& state, int starIndex, int screenW, int screenH) {
    openWindow(state, WindowKind::SystemInfo, starIndex, screenW, screenH);
}

void openTradeWindow(WindowState& state, int starIndex, int screenW, int screenH) {
    openWindow(state, WindowKind::Trade, starIndex, screenW, screenH);
}

void openContractsWindow(WindowState& state, int starIndex, int screenW, int screenH) {
    openWindow(state, WindowKind::Contracts, starIndex, screenW, screenH);
}

bool handleSystemWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int screenW, int screenH, int mouseX, int mouseY) {
    const SystemLayout layout = systemLayout(window);
    if (contains(layout.route, mouseX, mouseY)) {
        if (game.commandAgentToStar(game.playerAgent, window.star)) {
            selection.star = window.star;
            selection.agent = game.playerAgent;
            selection.followAgent = true;
        }
        return true;
    }
    if (contains(layout.trade, mouseX, mouseY)) {
        if (playerMarketStar(game) == window.star) {
            openTradeWindow(state, window.star, screenW, screenH);
            selection.star = window.star;
            selection.agent = game.playerAgent;
        }
        return true;
    }
    if (contains(layout.contracts, mouseX, mouseY)) {
        if (game.playerCanOpenContractsAt(window.star)) {
            openContractsWindow(state, window.star, screenW, screenH);
            selection.star = window.star;
            selection.agent = game.playerAgent;
        }
        return true;
    }
    if (contains(layout.colony, mouseX, mouseY)) {
        if (game.playerAtStar(window.star) && game.playerFoundColony()) {
            selection.star = window.star;
            selection.agent = game.playerAgent;
        }
        return true;
    }
    return true;
}

SDL_Rect contractButtonRect(const Window& window, int row) {
    return {window.rect.x + window.rect.w - 96, window.rect.y + TITLE_H + 55 + row * CONTRACT_ROW_H, 82, 22};
}

int contractMaxRows(const Window& window) {
    return std::max(1, (window.rect.h - TITLE_H - 64) / CONTRACT_ROW_H);
}

bool handleContractsWindowMouseDown(Game& game, const Window& window, HudSelection& selection, int mouseX, int mouseY) {
    if (game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) return true;

    int row = 0;
    const int maxRows = contractMaxRows(window);
    const std::vector<Contract> visibleContracts = game.playerVisibleContractsAt(window.star);
    for (const Contract& contract : visibleContracts) {
        if (row >= maxRows) break;
        if (!contract.completed && !contract.failed && contract.acceptedByAgent == game.playerAgent) {
            const SDL_Rect button = contractButtonRect(window, row++);
            if (contains(button, mouseX, mouseY)) {
                if (game.playerAtStar(contract.targetStar) && game.agentCompleteContract(game.playerAgent, contract.id)) {
                    selection.agent = game.playerAgent;
                    selection.star = window.star;
                } else if (!game.playerAtStar(contract.targetStar) && game.commandAgentToStar(game.playerAgent, contract.targetStar)) {
                    selection.agent = game.playerAgent;
                    selection.star = contract.targetStar;
                    selection.followAgent = true;
                }
                return true;
            }
        }
    }

    for (const Contract& contract : visibleContracts) {
        if (row >= maxRows) break;
        if (!contract.completed && !contract.failed && contract.acceptedByAgent < 0) {
            const SDL_Rect button = contractButtonRect(window, row++);
            if (contains(button, mouseX, mouseY)) {
                if (game.playerAtStar(contract.originStar) && game.agentAcceptContract(game.playerAgent, contract.id)) {
                    selection.agent = game.playerAgent;
                    selection.star = contract.originStar;
                    selection.followAgent = true;
                } else if (!game.playerAtStar(contract.originStar) && game.commandAgentToStar(game.playerAgent, contract.originStar)) {
                    selection.agent = game.playerAgent;
                    selection.star = contract.originStar;
                    selection.followAgent = true;
                }
                return true;
            }
        }
    }
    return true;
}

bool handleTradeWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int mouseX, int mouseY, int button) {
    const TradeLayout layout = tradeLayoutForWindow(window);
    const int dockedStar = playerMarketStar(game);
    const bool liveMarket = dockedStar == window.star && dockedStar >= 0;
    const double amount = tradeRequestedAmount(state);

    if (button == SDL_BUTTON_LEFT && contains(layout.amount, mouseX, mouseY)) {
        state.tradeAmountEditing = true;
        SDL_StartTextInput();
        return true;
    }
    if (state.tradeAmountEditing) {
        state.tradeAmountEditing = false;
        SDL_StopTextInput();
    }

    if (contains(layout.buy, mouseX, mouseY)) {
        if (liveMarket && game.agentBuyElementAmount(game.playerAgent, selection.element, amount)) {
            selection.agent = game.playerAgent;
        }
        return true;
    }
    if (contains(layout.sell, mouseX, mouseY)) {
        if (liveMarket && game.agentSellCargoAmount(game.playerAgent, amount)) {
            selection.agent = game.playerAgent;
        }
        return true;
    }
    if (contains(layout.autoTrade, mouseX, mouseY)) {
        if (liveMarket && game.agentAutoTrade(game.playerAgent)) {
            selection.agent = game.playerAgent;
            selection.followAgent = true;
        }
        return true;
    }
    if (contains(layout.refuel, mouseX, mouseY)) {
        if (liveMarket && game.agentBuyFuel(game.playerAgent)) {
            selection.agent = game.playerAgent;
        }
        return true;
    }

    const std::vector<ElementDefinition>& elements = elementDefinitions();
    for (size_t i = 0; i < elements.size(); ++i) {
        const SDL_Rect rect = elementRect(layout, int(i));
        if (rect.w > 0 && contains(rect, mouseX, mouseY)) {
            selection.element = int(i);
            selection.star = window.star;
            selection.agent = game.playerAgent;
            if (button == SDL_BUTTON_RIGHT && liveMarket) {
                game.agentBuyElementAmount(game.playerAgent, selection.element, amount);
            }
            return true;
        }
    }
    return true;
}

bool handleMouseDown(WindowState& state, Game& game, HudSelection& selection, int screenW, int screenH, int mouseX, int mouseY, int button) {
    int hitIndex = -1;
    for (int i = int(state.windows.size()) - 1; i >= 0; --i) {
        if (contains(state.windows[i].rect, mouseX, mouseY)) {
            hitIndex = i;
            break;
        }
    }
    if (hitIndex < 0) {
        if (state.tradeAmountEditing) {
            state.tradeAmountEditing = false;
            SDL_StopTextInput();
        }
        return false;
    }

    const int id = state.windows[hitIndex].id;
    bringWindowToFront(state, id);
    Window* active = findWindow(state, id);
    if (!active) return true;

    if (button == SDL_BUTTON_LEFT && contains(closeRect(*active), mouseX, mouseY)) {
        const int index = findWindowIndex(state, id);
        if (index >= 0) state.windows.erase(state.windows.begin() + index);
        state.activeId = state.windows.empty() ? -1 : state.windows.back().id;
        state.draggingId = -1;
        if (state.tradeAmountEditing) {
            state.tradeAmountEditing = false;
            SDL_StopTextInput();
        }
        return true;
    }

    if (button == SDL_BUTTON_LEFT && contains(titleRect(*active), mouseX, mouseY)) {
        active->dragging = true;
        active->dragX = mouseX - active->rect.x;
        active->dragY = mouseY - active->rect.y;
        state.draggingId = active->id;
        return true;
    }

    const Window window = *active;
    if (window.kind == WindowKind::SystemInfo) {
        return handleSystemWindowMouseDown(state, game, window, selection, screenW, screenH, mouseX, mouseY);
    }
    if (window.kind == WindowKind::Contracts) {
        return handleContractsWindowMouseDown(game, window, selection, mouseX, mouseY);
    }
    return handleTradeWindowMouseDown(state, game, window, selection, mouseX, mouseY, button);
}

void handleMouseMove(WindowState& state, int screenW, int screenH, int mouseX, int mouseY) {
    Window* window = findWindow(state, state.draggingId);
    if (!window || !window->dragging) return;
    window->rect.x = mouseX - window->dragX;
    window->rect.y = mouseY - window->dragY;
    window->rect = clampRectToScreen(window->rect, screenW, screenH);
}

void handleMouseUp(WindowState& state) {
    Window* window = findWindow(state, state.draggingId);
    if (window) window->dragging = false;
    state.draggingId = -1;
}

void handleTextInput(WindowState& state, const char* text) {
    if (!state.tradeAmountEditing || !text) return;
    for (const char* p = text; *p; ++p) {
        const char ch = *p;
        if (ch >= '0' && ch <= '9') {
            if (state.tradeAmount.size() < 12) state.tradeAmount.push_back(ch);
        } else if (ch == '.' && state.tradeAmount.find('.') == std::string::npos) {
            if (state.tradeAmount.size() < 12) state.tradeAmount.push_back(ch);
        }
    }
}

bool handleKeyDown(WindowState& state, SDL_Keycode key) {
    if (!state.tradeAmountEditing) return false;
    if (key == SDLK_BACKSPACE) {
        if (!state.tradeAmount.empty()) state.tradeAmount.pop_back();
        return true;
    }
    if (key == SDLK_DELETE) {
        state.tradeAmount.clear();
        return true;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
        state.tradeAmountEditing = false;
        SDL_StopTextInput();
        return true;
    }
    return true;
}

void drawWindowFrame(SDL_Renderer* renderer, const Window& window, const std::string& title, bool active) {
    fillRect(renderer, window.rect.x + 4, window.rect.y + 5, window.rect.w, window.rect.h, {0, 0, 0, 95});
    fillRect(renderer, window.rect.x, window.rect.y, window.rect.w, window.rect.h, P.panel);
    fillRect(renderer, window.rect.x, window.rect.y, window.rect.w, TITLE_H, P.panel2);
    strokeRect(renderer, window.rect.x, window.rect.y, window.rect.w, window.rect.h, active ? P.cyan : P.border);
    drawText(renderer, window.rect.x + 8, window.rect.y + 7, title, active ? P.cyan : P.text, 1);
    const SDL_Rect close = closeRect(window);
    fillRect(renderer, close.x, close.y, close.w, close.h, {36, 18, 24, 235});
    strokeRect(renderer, close.x, close.y, close.w, close.h, P.red);
    drawText(renderer, close.x + 5, close.y + 4, "X", P.red, 1);
}

void drawSystemWindow(SDL_Renderer* renderer, const Game& game, const Window& window, const HudSelection& selection, bool active) {
    const ClusterStar* star = starAt(game, window.star);
    const std::string title = star ? ("SYSTEM / " + star->name) : "SYSTEM / NONE";
    drawWindowFrame(renderer, window, title, active);
    if (!star) {
        drawText(renderer, window.rect.x + 12, window.rect.y + 42, "NO SYSTEM SELECTED", P.red, 1);
        return;
    }

    const int x = window.rect.x + WINDOW_PAD;
    int y = window.rect.y + TITLE_H + 12;
    const bool liveInfo = game.playerAtStar(window.star);
    PlayerMarketView marketView;
    const bool marketKnown = playerMarketView(game, window.star, selection.element, marketView);
    drawText(renderer, x, y, ownerKnowledgeLine(game, window.star), liveInfo ? P.green : P.amber, 1);
    y += 16;
    if (game.playerFaction >= 0) {
        const int threats = game.factionKnownThreatCount(game.playerFaction, window.star);
        if (threats > 0) {
            char line[64];
            std::snprintf(line, sizeof(line), "THREATS %d AGE %.0F RISK %.0F",
                threats,
                game.factionKnownThreatAge(game.playerFaction, window.star),
                game.factionKnownThreatRisk(game.playerFaction, window.star));
            if (int(std::strlen(line)) * 6 <= window.rect.w - WINDOW_PAD * 2) {
                drawText(renderer, x, y, line, P.red, 1);
            } else {
                std::snprintf(line, sizeof(line), "THREATS %d AGE %.0F", threats,
                    game.factionKnownThreatAge(game.playerFaction, window.star));
                drawText(renderer, x, y, line, P.red, 1);
                y += 16;
                std::snprintf(line, sizeof(line), "RISK %.0F",
                    game.factionKnownThreatRisk(game.playerFaction, window.star));
                drawText(renderer, x, y, line, P.red, 1);
            }
            y += 16;
        }
    }
    drawRoutePreview(renderer, game, window.star, selection.element, x, y, window.rect.w);
    if (!liveInfo) {
        drawText(renderer, x, y, marketKnown ? "REMOTE SYSTEM DATA FROM SIGNALS" : "REMOTE SYSTEM DATA UNKNOWN UNTIL ARRIVAL", P.dim, 1);
        y += 16;
        drawText(renderer, x, y, marketKnown ? "LAST-KNOWN OWNER/MARKET MAY BE STALE" : "LAST-KNOWN OWNER MAY BE STALE", P.amber, 1);
        y += 16;
        if (marketKnown) {
            drawPlayerMarketView(renderer, marketView, elementDefinitions()[selection.element], x, y);
        } else {
            drawText(renderer, x, y, "MARKET UNKNOWN / NO SNAPSHOT", P.dim, 1);
        }
        const SystemLayout layout = systemLayout(window);
        drawButton(renderer, layout.route, "ROUTE", P.cyan, game.playerAgent >= 0);
        drawButton(renderer, layout.trade, "TRADE", P.green, false);
        drawButton(renderer, layout.contracts, "JOBS", P.cyan, game.playerCanOpenContractsAt(window.star));
        drawButton(renderer, layout.colony, "C COL/RE", P.amber, false);
        return;
    }

    drawText(renderer, x, y, "ROLE " + star->economyRole, P.text, 1);
    y += 18;
    labelBar(renderer, x, y, window.rect.w - 28, "POP", star->population / 1400000.0, P.green);
    y += 14;
    labelBar(renderer, x, y, window.rect.w - 28, "IND", star->industry / 3.0, P.amber);
    y += 14;
    labelBar(renderer, x, y, window.rect.w - 28, "HAB", star->habitability, P.cyan);
    y += 14;
    labelBar(renderer, x, y, window.rect.w - 28, "DEF", star->defense / 10.0, P.red);
    y += 18;
    drawText(renderer, x, y, "RICH " + focusList(star->resourceFocus, 5), P.cyan, 1);
    drawText(renderer, x + 154, y, "NEED " + focusList(star->demandFocus, 5), P.red, 1);
    y += 16;

    if (marketKnown) {
        drawPlayerMarketView(renderer, marketView, elementDefinitions()[selection.element], x, y);
    }
    drawLiveColonySummary(renderer, game, window.star, x, y);

    const SystemLayout layout = systemLayout(window);
    drawButton(renderer, layout.route, "ROUTE", P.cyan, game.playerAgent >= 0);
    drawButton(renderer, layout.trade, "TRADE", P.green, playerMarketStar(game) == window.star);
    drawButton(renderer, layout.contracts, "JOBS", P.cyan, game.playerCanOpenContractsAt(window.star));
    drawButton(renderer, layout.colony, "C COL/RE", P.amber, game.playerAtStar(window.star));
}

void drawContractRow(SDL_Renderer* renderer, const Game& game, const Window& window, const Contract& contract, int row, bool activeContractRow) {
    const int x = window.rect.x + WINDOW_PAD;
    const int y = window.rect.y + TITLE_H + 52 + row * CONTRACT_ROW_H;
    const SDL_Rect button = contractButtonRect(window, row);
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    const bool validResource = contract.resource >= 0 && contract.resource < int(elements.size());
    const ClusterStar* origin = starAt(game, contract.originStar);
    const ClusterStar* target = starAt(game, contract.targetStar);
    const double yearsLeft = contract.deadline - game.time;

    fillRect(renderer, x, y - 5, window.rect.w - 24, 31, activeContractRow ? SDL_Color{24, 34, 52, 205} : SDL_Color{13, 20, 34, 195});
    strokeRect(renderer, x, y - 5, window.rect.w - 24, 31, activeContractRow ? P.amber : P.border);

    char line[192];
    if (contractUsesCargo(contract.type)) {
        std::snprintf(line, sizeof(line), "#%d %s %.0F %s > %s  CR %.0F  %.1FY",
            contract.id,
            validResource ? elements[contract.resource].symbol : "?",
            contract.amount,
            origin ? origin->name.c_str() : "-",
            target ? target->name.c_str() : "-",
            contract.reward,
            yearsLeft);
        if (int(std::strlen(line)) * 6 > button.x - x - 16) {
            std::snprintf(line, sizeof(line), "#%d %s %.0F > %s CR%.0F %.1FY",
                contract.id,
                validResource ? elements[contract.resource].symbol : "?",
                contract.amount,
                target ? target->name.c_str() : "-",
                contract.reward,
                yearsLeft);
        }
    } else {
        std::snprintf(line, sizeof(line), "#%d %s %s > %s  CR %.0F  %.1FY",
            contract.id,
            contractTypeLabel(contract.type),
            origin ? origin->name.c_str() : "-",
            target ? target->name.c_str() : "-",
            contract.reward,
            yearsLeft);
        if (int(std::strlen(line)) * 6 > button.x - x - 16) {
            std::snprintf(line, sizeof(line), "#%d %s > %s CR%.0F %.1FY",
                contract.id,
                contractTypeLabel(contract.type),
                target ? target->name.c_str() : "-",
                contract.reward,
                yearsLeft);
        }
    }
    drawText(renderer, x + 8, y + 2, line, activeContractRow ? P.amber : P.text, 1);
    drawContractRouteLine(renderer, game, contract, x + 8, y + 16, button.x - x - 18);

    if (activeContractRow) {
        std::string label = game.playerAtStar(contract.targetStar) ? "DONE" : "ROUTE";
        if (contract.type == ContractType::Scout && contract.reportSignalPending) label = "SIGNAL";
        if (contract.type == ContractType::Escort && game.playerAtStar(contract.targetStar) && !contract.escortArrived) label = "WAIT";
        drawButton(renderer, button, label, P.green, true);
    } else {
        const bool atOrigin = game.playerAtStar(contract.originStar);
        const bool playerReady =
            game.playerAgent >= 0 &&
            game.playerAgent < int(game.agents.size()) &&
            !game.agents[game.playerAgent].ship.enRoute;
        const bool canAccept = game.playerAtStar(contract.originStar) &&
            game.playerAgent >= 0 &&
            game.playerAgent < int(game.agents.size()) &&
            game.agentContractCargoFits(game.playerAgent, contract.id);
        const bool canRoute = playerReady && !atOrigin && starAt(game, contract.originStar);
        drawButton(renderer, button, atOrigin ? "ACCEPT" : "ORIGIN", P.cyan, atOrigin ? canAccept : canRoute);
    }
}

void drawContractsWindow(SDL_Renderer* renderer, const Game& game, const Window& window, const HudSelection&, bool active) {
    const ClusterStar* star = starAt(game, window.star);
    drawWindowFrame(renderer, window, star ? ("JOBS / " + star->name) : "JOBS / NO SYSTEM", active);

    const int x = window.rect.x + WINDOW_PAD;
    int y = window.rect.y + TITLE_H + 12;
    if (!star) {
        drawText(renderer, x, y, "NO SYSTEM SELECTED", P.red, 1);
        return;
    }

    const bool liveBoard = game.playerAtStar(window.star);
    const bool canOpen = game.playerCanOpenContractsAt(window.star);
    if (liveBoard) {
        drawText(renderer, x, y, "LOCAL JOB BOARD / LIVE", P.green, 1);
    } else if (canOpen) {
        drawText(renderer, x, y, "LAST-KNOWN JOB SIGNAL / GO ORIGIN TO ACCEPT", P.amber, 1);
    } else {
        drawText(renderer, x, y, "NO LOCAL OR SIGNAL JOB BOARD", P.red, 1);
    }
    y += 16;
    drawText(renderer, x, y, "ACTIVE JOBS + VISIBLE LISTINGS", P.dim, 1);

    int row = 0;
    const int maxRows = contractMaxRows(window);
    const std::vector<Contract> visibleContracts = game.playerVisibleContractsAt(window.star);
    for (const Contract& contract : visibleContracts) {
        if (row >= maxRows) break;
        if (!contract.completed && !contract.failed && contract.acceptedByAgent == game.playerAgent) {
            drawContractRow(renderer, game, window, contract, row++, true);
        }
    }
    for (const Contract& contract : visibleContracts) {
        if (row >= maxRows) break;
        if (!contract.completed && !contract.failed && contract.acceptedByAgent < 0) {
            drawContractRow(renderer, game, window, contract, row++, false);
        }
    }
    if (row == 0) {
        drawText(renderer, x, window.rect.y + TITLE_H + 54,
            liveBoard ? "NO LOCAL CONTRACTS RIGHT NOW" : "NO VISIBLE CONTRACT SIGNALS", P.dim, 1);
    }
}

void drawTradeWindow(SDL_Renderer* renderer, const Game& game, const Window& window, const HudSelection& selection, const WindowState& state, bool active) {
    const int dockedStar = playerMarketStar(game);
    const bool liveMarket = dockedStar == window.star && dockedStar >= 0 && dockedStar < int(game.markets.size());
    const ClusterStar* star = starAt(game, window.star);
    const Market* market = liveMarket ? &game.markets[dockedStar] : nullptr;
    drawWindowFrame(renderer, window, star ? ("TRADE / " + star->name) : "TRADE / NO MARKET", active);

    const TradeLayout layout = tradeLayoutForWindow(window);
    const int topX = window.rect.x + WINDOW_PAD;
    const int topY = window.rect.y + TITLE_H + 12;
    if (liveMarket && star) {
        drawText(renderer, topX, topY, star->name + " LOCAL MARKET", P.green, 1);
    } else {
        drawText(renderer, topX, topY, "NO LIVE MARKET - DOCK IN THIS SYSTEM", P.red, 1);
    }
    if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
        const Agent& player = game.agents[game.playerAgent];
        char line[96];
        std::snprintf(line, sizeof(line), "CR %.0F CARGO %.0F/%.0F FUEL %.0F%%",
            player.money,
            shipCargoMass(player.ship),
            player.ship.cargoCapacity,
            shipFuelFraction(player.ship) * 100.0);
        drawText(renderer, topX, topY + 16, line, P.text, 1);
    }

    const std::vector<ElementDefinition>& elements = elementDefinitions();
    for (size_t i = 0; i < elements.size(); ++i) {
        const int idx = int(i);
        const SDL_Rect rect = elementRect(layout, idx);
        if (rect.w <= 0) continue;

        SDL_Color fill = {34, 44, 62, 190};
        if (market) fill = marketCellColor(*market, idx);
        fillRect(renderer, rect.x, rect.y, rect.w, rect.h, fill);

        SDL_Color border = {52, 68, 92, 220};
        if (market && star && hasFocus(star->resourceFocus, idx)) border = P.cyan;
        if (market && star && hasFocus(star->demandFocus, idx)) border = P.red;
        if (idx == selection.element) border = P.amber;
        strokeRect(renderer, rect.x, rect.y, rect.w, rect.h, border);

        drawText(renderer, rect.x + 3, rect.y + 3, elements[i].symbol, idx == selection.element ? P.amber : P.text, 1);
        char z[8];
        std::snprintf(z, sizeof(z), "%d", elements[i].atomicNumber);
        drawText(renderer, rect.x + rect.w - 15, rect.y + rect.h - 9, z, P.dim, 1);
    }

    drawText(renderer, layout.amount.x, layout.amount.y - 12, "AMOUNT", P.dim, 1);
    fillRect(renderer, layout.amount.x, layout.amount.y, layout.amount.w, layout.amount.h, {9, 14, 26, 245});
    strokeRect(renderer, layout.amount.x, layout.amount.y, layout.amount.w, layout.amount.h,
        state.tradeAmountEditing && active ? P.cyan : P.border);
    drawText(renderer, layout.amount.x + 10, layout.amount.y + 10, tradeAmountLabel(state),
        state.tradeAmount.empty() ? P.dim : P.text, 1);

    drawButton(renderer, layout.buy, "BUY", P.green, liveMarket);
    drawButton(renderer, layout.sell, "SELL", P.amber, liveMarket);
    drawButton(renderer, layout.autoTrade, "AUTO", P.cyan, liveMarket);
    drawButton(renderer, layout.refuel, "FUEL", P.amber, liveMarket);

    const int infoX = layout.buy.x;
    const int infoY = layout.refuel.y + 42;
    if (selection.element >= 0 && selection.element < int(elements.size())) {
        const ElementDefinition& element = elements[selection.element];
        drawText(renderer, infoX, infoY, std::string(element.symbol) + " " + element.name, P.text, 1);
        if (market && selection.element < int(market->prices.size())) {
            char line[96];
            std::snprintf(line, sizeof(line), "PRICE %.1F", market->prices[selection.element]);
            drawText(renderer, infoX, infoY + 14, line, P.amber, 1);
            std::snprintf(line, sizeof(line), "SUP %.0F", market->supply[selection.element].amount);
            drawText(renderer, infoX, infoY + 28, line, P.green, 1);
            std::snprintf(line, sizeof(line), "DEM %.0F", market->demand[selection.element].amount);
            drawText(renderer, infoX, infoY + 42, line, P.red, 1);
            std::snprintf(line, sizeof(line), "MASS %.1F FUEL %.2F/%.2F", element.atomicMass, element.fusionFuelTrait, element.fissionFuelTrait);
            drawText(renderer, infoX, infoY + 56, line, P.dim, 1);
        }
    }

    drawText(renderer, layout.tableX, window.rect.y + window.rect.h - 15, "CLICK AMOUNT + TYPE NUMBER / EMPTY=MAX / RMB CELL QUICK BUY", P.dim, 1);
}

void drawWindows(SDL_Renderer* renderer, const Game& game, int, int, const HudSelection& selection, const WindowState& state) {
    for (size_t i = 0; i < state.windows.size(); ++i) {
        const Window& window = state.windows[i];
        const bool active = window.id == state.activeId;
        if (window.kind == WindowKind::SystemInfo) {
            drawSystemWindow(renderer, game, window, selection, active);
        } else if (window.kind == WindowKind::Contracts) {
            drawContractsWindow(renderer, game, window, selection, active);
        } else {
            drawTradeWindow(renderer, game, window, selection, state, active);
        }
    }
}

void drawHud(SDL_Renderer* renderer, const Game& game, int screenW, int screenH, const HudSelection& selection) {
    const int leftW = std::min(330, std::max(250, screenW / 3));
    int y = 12;

    panel(renderer, 12, y, leftW, 62);
    char top[160];
    header(renderer, 22, y + 10, "STARCLUSTER");
    if (const Ship* ship = hudShip(game, selection)) {
        std::snprintf(top, sizeof(top), "X %.1F  Y %.1F  Z %.1F", ship->x, ship->y, ship->z);
    } else {
        std::snprintf(top, sizeof(top), "X -  Y -  Z -");
    }
    drawText(renderer, 22, y + 32, top, P.text, 1);
    std::snprintf(top, sizeof(top), "%s  T %.1F  R %.2FY/S  ST %d  CT %d", selection.paused ? "PAUSED" : "LIVE",
        game.time, selection.simYearsPerSecond,
        int(game.cluster.stars.size()), game.playerVisibleAgentCount());
    drawText(renderer, 22, y + 46, top, selection.paused ? P.amber : P.dim, 1);
    y += 72;

    if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
        const Agent& player = game.agents[game.playerAgent];
        int activeContracts = 0;
        for (const Contract& contract : game.contracts) {
            if (!contract.completed && !contract.failed && contract.acceptedByAgent == game.playerAgent) activeContracts += 1;
        }
        panel(renderer, 12, y, leftW, 44);
        drawText(renderer, 22, y + 10, "PLAYER STATUS", P.amber, 1);
        std::snprintf(top, sizeof(top), "CR %.0F TR %d COL %d JOB %d", player.money, player.trades, game.playerColonyCount(), activeContracts);
        drawText(renderer, 22, y + 27, top, P.text, 1);
        y += 54;
    }

    drawStarPanel(renderer, game, selection.star, selection.element, 12, y, leftW);
    y += 160;
    drawAgentPanel(renderer, game, selection.agent, 12, y, leftW);

    drawFactionPanel(renderer, game, screenW - 260, 12, 248);
    drawControlHints(renderer, screenW, screenH);

    if (!game.lastEvent.empty()) {
        const int w = std::min(screenW - 24, 640);
        const int eventY = std::max(12, screenH - 50);
        panel(renderer, 12, eventY, w, 34);
        drawText(renderer, 22, eventY + 12, game.lastEvent, P.amber, 1);
    }

    if (selection.followAgent) {
        drawText(renderer, screenW / 2 - 36, 14, "FOLLOW", P.amber, 2);
    }
}

}
