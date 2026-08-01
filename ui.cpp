#include "ui.h"
#include "modules.h"
#include "render2d.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace UI {
namespace {

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
    SDL_Rect upgrade = {0, 0, 0, 0};
};

struct SystemLayout {
    SDL_Rect route = {0, 0, 0, 0};
    SDL_Rect trade = {0, 0, 0, 0};
    SDL_Rect contracts = {0, 0, 0, 0};
    SDL_Rect colony = {0, 0, 0, 0};
    SDL_Rect cargo = {0, 0, 0, 0};
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
    } else if (kind == WindowKind::Cargo) {
        rect.w = 340;
        rect.h = 400;
        rect.x = std::max(380, (screenW - rect.w) / 2 + cascade * 18);
        rect.y = std::max(100, screenH - rect.h - 50 - cascade * 10);
    } else {
        rect.w = 448;
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
    layout.colony = {window.rect.x + WINDOW_PAD + 256, y, 96, 24};
    layout.cargo = {window.rect.x + WINDOW_PAD + 360, y, 64, 24};
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
    layout.upgrade = {bx, layout.tableY + 144, buttonW, 28};
    return layout;
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

void labelBar(SDL_Renderer* renderer, int x, int y, int w, const std::string& label, double value, SDL_Color c) {
    drawText(renderer, x, y, label, P.dim, 1);
    bar(renderer, x + 85, y - 1, w - 85, 7, value, c);
}

int drawStat(SDL_Renderer* renderer, int x, int y, const std::string& label, const std::string& value, SDL_Color lc = P.dim, SDL_Color vc = P.text) {
    drawText(renderer, x, y, label, lc, 1);
    x += int(label.length()) * 6;
    drawText(renderer, x, y, value, vc, 1);
    x += int(value.length()) * 6 + 12;
    return x;
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
    if (game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) return;

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
    case ContractType::Delivery: return "DELIVERY";
    case ContractType::Courier: return "COURIER";
    case ContractType::Scout: return "SCOUT";
    case ContractType::Bounty: return "BOUNTY";
    case ContractType::Escort: return "ESCORT";
    case ContractType::Raid: return "RAID";
    case ContractType::ColonySupply: return "SUPPLY";
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
    labelBar(renderer, x + 10, y + 58, w - 20, "POPULATION", star->population / 1400000.0, P.green);
    labelBar(renderer, x + 10, y + 72, w - 20, "INDUSTRY", star->industry / 3.0, P.amber);
    labelBar(renderer, x + 10, y + 86, w - 20, "HABITABILITY", star->habitability, P.cyan);
    labelBar(renderer, x + 10, y + 100, w - 20, "DEFENSE", star->defense / 10.0, P.red);

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

    char buf1[32], buf2[32], buf3[32];
    std::snprintf(buf1, sizeof(buf1), "%.0F", shipTotalMass(agent.ship));
    std::snprintf(buf2, sizeof(buf2), "%.0F%%", shipFuelFraction(agent.ship) * 100.0);
    int cx = x + 10;
    cx = drawStat(renderer, cx, y + 90, "MASS ", buf1);
    cx = drawStat(renderer, cx, y + 90, "FUEL ", buf2);

    std::snprintf(buf1, sizeof(buf1), "%.0F", agent.money);
    std::snprintf(buf2, sizeof(buf2), "%.2FC", speedOf(agent));
    std::snprintf(buf3, sizeof(buf3), "%d", agent.trades);
    cx = x + 10;
    cx = drawStat(renderer, cx, y + 104, "CREDITS ", buf1, P.green);
    cx = drawStat(renderer, cx, y + 104, "SPEED ", buf2);
    cx = drawStat(renderer, cx, y + 104, "TRADES ", buf3);
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
        "U SHIPYARD",
        "M MINE ORE",
        "J REPAIR HULL",
        "K SCAN ANOMALY",
        "H HIRE SHIP",
        "C COL/REINF",
        "F5 SAVE",
        "F9 LOAD",
        "1-4 SPEED",
        "WHEEL ZOOM",
        "MMB DRAG PAN",
        "ARROWS PAN",
        "WASD ROTATE",
        "SPACE PAUSE",
        "TAB AGENT",
        "F FOLLOW",
        "R ROB",
        "[ ] MARKET",
        "P PLAYER",
        "0 RESET",
        "X STOP SHIP",
        "ENTER OPEN SYS",
        "L ENTER SYSTEM"
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
    int mx = 0, my = 0;
    Uint32 mstate = SDL_GetMouseState(&mx, &my);
    bool hovered = enabled && (mx >= rect.x && mx < rect.x + rect.w && my >= rect.y && my < rect.y + rect.h);
    bool pressed = hovered && (mstate & SDL_BUTTON(SDL_BUTTON_LEFT));

    const SDL_Color stroke = enabled ? c : P.dim;
    SDL_Color text = enabled ? P.text : P.dim;
    SDL_Color fill = enabled ? SDL_Color{12, 16, 26, 200} : SDL_Color{8, 12, 18, 150};

    if (pressed) {
        fill = stroke;
        text = {12, 16, 26, 255};
    } else if (hovered) {
        fill = SDL_Color{ Uint8(c.r / 3 + 16), Uint8(c.g / 3 + 22), Uint8(c.b / 3 + 38), 235 };
    }

    fillRect(renderer, rect.x, rect.y, rect.w, rect.h, fill);
    strokeRect(renderer, rect.x, rect.y, rect.w, rect.h, stroke);
    drawText(renderer, rect.x + (rect.w - int(label.size()) * 6) / 2, rect.y + (rect.h - 7) / 2, label, text, 1);
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

void openCargoWindow(WindowState& state, int starIndex, int screenW, int screenH) {
    openWindow(state, WindowKind::Cargo, starIndex, screenW, screenH);
}

void drawFactionPanel(SDL_Renderer* renderer, const Game& game, int x, int y, int w);
void drawSystemWindow(SDL_Renderer* renderer, const Game& game, const Window& window, const HudSelection& selection, bool active);
void drawTradeWindow(SDL_Renderer* renderer, const Game& game, const Window& window, const HudSelection& selection, const WindowState& state, bool active);
void drawContractsWindow(SDL_Renderer* renderer, const Game& game, const Window& window, const HudSelection& selection, bool active);
void drawShipyardWindow(SDL_Renderer* renderer, const Game& game, const Window& window, const WindowState& state, bool active);

bool handleSystemWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int screenW, int screenH, int mouseX, int mouseY);
bool handleTradeWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int mouseX, int mouseY, int button);
bool handleContractsWindowMouseDown(Game& game, const Window& window, const HudSelection& selection, int mouseX, int mouseY);
bool handleShipyardWindowMouseDown(WindowState& state, Game& game, const Window& window, int mouseX, int mouseY, int button);


bool handleSystemWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int screenW, int screenH, int mouseX, int mouseY) {
    const SystemLayout layout = systemLayout(window);
    if (contains(layout.route, mouseX, mouseY)) {
        // Explicitly matched with GO implementation
        if (window.star >= 0) {
            bool success = game.commandAgentToStar(game.playerAgent, window.star);
            printf("DEBUG UI: GO button clicked. Target star: %d. Success: %s\n", window.star, success ? "true" : "false");
            if (success) {
                selection.star = window.star;
                selection.agent = game.playerAgent;
                selection.followAgent = true;
                
                // Close the window immediately to reveal the ship and route line
                for (size_t i = 0; i < state.windows.size(); ++i) {
                    if (state.windows[i].id == window.id) {
                        state.windows.erase(state.windows.begin() + i);
                        break;
                    }
                }
            }
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
    if (contains(layout.cargo, mouseX, mouseY)) {
        if (game.playerAgent >= 0) {
            openCargoWindow(state, window.star, screenW, screenH);
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

bool handleCargoWindowMouseDown(Game& game, const Window& window, int mouseX, int mouseY) {
    if (game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) return true;
    
    Agent& player = game.agents[game.playerAgent];
    Ship& ship = player.ship;
    
    int y = window.rect.y + TITLE_H + 12 + 24 + 18;
    int x = window.rect.x + 12;
    
    for (auto it = ship.cargo.begin(); it != ship.cargo.end(); ) {
        if (it->amount <= 0.0) {
            ++it;
            continue;
        }
        
        SDL_Rect dropBtn = {x + 270, y - 2, 44, 18};
        if (contains(dropBtn, mouseX, mouseY)) {
            it = ship.cargo.erase(it);
            return true;
        }
        
        ++it;
        y += 24;
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
        if (liveMarket && game.agentSellCargoAmount(game.playerAgent, amount, selection.element)) {
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
    if (contains(layout.upgrade, mouseX, mouseY)) {
        if (liveMarket && (game.colonies[dockedStar].shipyardLevel > 0 || game.colonies[dockedStar].infrastructure >= 1.0)) {
            openShipyardWindow(state, dockedStar, window.rect.x + 20, window.rect.y + 20);
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

    for (Window& w : state.windows) {
        if (contains(w.rect, mouseX, mouseY)) {
            if (w.kind == WindowKind::SystemInfo && handleSystemWindowMouseDown(state, game, w, selection, screenW, screenH, mouseX, mouseY)) return true;
            if (w.kind == WindowKind::Trade && handleTradeWindowMouseDown(state, game, w, selection, mouseX, mouseY, button)) return true;
            if (w.kind == WindowKind::Contracts && handleContractsWindowMouseDown(game, w, selection, mouseX, mouseY)) return true;
            if (w.kind == WindowKind::Shipyard && handleShipyardWindowMouseDown(state, game, w, mouseX, mouseY, button)) return true;
            if (w.kind == WindowKind::Cargo && handleCargoWindowMouseDown(game, w, mouseX, mouseY)) return true;
        }
    }
    return true;
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

void openShipyardWindow(WindowState& state, int starIndex, int x, int y) {
    for (auto& w : state.windows) {
        if (w.kind == WindowKind::Shipyard && w.star == starIndex) {
            state.activeId = w.id;
            return;
        }
    }
    Window w;
    w.id = state.nextId++;
    w.kind = WindowKind::Shipyard;
    w.star = starIndex;
    w.rect = {x, std::max(20, std::min(y, 40)), 470, 700};
    state.windows.push_back(w);
    state.activeId = w.id;
}

bool handleShipyardWindowMouseDown(WindowState& state, Game& game, const Window& window, int mouseX, int mouseY, int button) {
    const int dockedStar = playerMarketStar(game);
    const bool liveShipyard = dockedStar == window.star && dockedStar >= 0 && dockedStar < int(game.markets.size());
    if (!liveShipyard || game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) return true;
    Window* w = nullptr;
    for (auto& win : state.windows) if (win.id == window.id) { w = &win; break; }
    if (!w) return true;

    const int nShips = int(shipClasses().size());
    const int nMods = int(moduleDefs().size());
    const int total = nShips + 1 + nMods;   // ships, divider, modules
    const int maxRows = 16;
    const int startIdx = std::min(w->scrollOffset, std::max(0, total - maxRows));
    const int endIdx = std::min(total, startIdx + maxRows);

    if (state.confirmBuyShipIndex >= 0) {
        SDL_Rect dlg = { window.rect.x + 20, window.rect.y + window.rect.h / 2 - 40, window.rect.w - 40, 80 };
        SDL_Rect yesBtn = { dlg.x + 10, dlg.y + 50, 60, 20 };
        SDL_Rect noBtn = { dlg.x + 80, dlg.y + 50, 60, 20 };
        if (contains(yesBtn, mouseX, mouseY)) {
            if (state.confirmBuyShipIndex < nShips) {
                game.buyShip(game.playerAgent, dockedStar, state.confirmBuyShipIndex);
            }
            state.confirmBuyShipIndex = -1;
        } else if (contains(noBtn, mouseX, mouseY)) {
            state.confirmBuyShipIndex = -1;
        }
        return true;
    }

    SDL_Rect upBtn = {window.rect.x + window.rect.w - 50, window.rect.y + TITLE_H + 4, 40, 20};
    SDL_Rect downBtn = {window.rect.x + window.rect.w - 50, window.rect.y + window.rect.h - 26, 40, 20};

    if (contains(upBtn, mouseX, mouseY) && startIdx > 0) {
        w->scrollOffset = std::max(0, w->scrollOffset - 5);
        return true;
    }
    if (contains(downBtn, mouseX, mouseY) && endIdx < total) {
        w->scrollOffset = std::min(std::max(0, total - maxRows), w->scrollOffset + 5);
        return true;
    }

    const int listY = window.rect.y + TITLE_H + 30;
    for (int i = startIdx; i < endIdx; ++i) {
        const int row = i - startIdx;
        SDL_Rect btn = {window.rect.x + window.rect.w - 96, listY + row * 36 - 6, 80, 24};
        if (contains(btn, mouseX, mouseY)) {
            if (i < nShips) {
                state.confirmBuyShipIndex = i;
            } else if (i > nShips) {
                game.installModule(game.playerAgent, i - nShips - 1);
            }
            return true;
        }
    }
    return true;
}

void drawShipyardWindow(SDL_Renderer* renderer, const Game& game, const Window& window, const WindowState& state, bool active) {
    const int dockedStar = playerMarketStar(game);
    const bool liveShipyard = dockedStar == window.star && dockedStar >= 0 && dockedStar < int(game.markets.size());
    const ClusterStar* star = starAt(game, window.star);
    drawWindowFrame(renderer, window, star ? ("SHIPYARD / " + star->name) : "SHIPYARD / NO MARKET", active);

    const int topX = window.rect.x + WINDOW_PAD;
    const int topY = window.rect.y + TITLE_H + 12;
    const int syLevel = game.shipyardLevelAtStar(window.star);
    if (liveShipyard && star) {
        char hdr[96];
        std::snprintf(hdr, sizeof(hdr), "BUY SHIPS / FIT MODULES     SHIPYARD LVL %d", syLevel);
        drawText(renderer, topX, topY, hdr, P.green, 1);
    } else {
        drawText(renderer, topX, topY, "NO LIVE SHIPYARD - DOCK IN THIS SYSTEM", P.red, 1);
        return;
    }

    const double cash = (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size()))
                            ? game.agents[game.playerAgent].money : 0.0;

    const auto& classes = shipClasses();
    const auto& mods = moduleDefs();
    const int nShips = int(classes.size());
    const int nMods = int(mods.size());
    const int total = nShips + 1 + nMods;   // корабли, разделитель, модули
    const int maxRows = 16;
    const int startIdx = std::min(window.scrollOffset, std::max(0, total - maxRows));
    const int endIdx = std::min(total, startIdx + maxRows);

    SDL_Rect upBtn = {window.rect.x + window.rect.w - 50, window.rect.y + TITLE_H + 4, 40, 20};
    SDL_Rect downBtn = {window.rect.x + window.rect.w - 50, window.rect.y + window.rect.h - 26, 40, 20};
    drawButton(renderer, upBtn, "UP", P.green, startIdx > 0);
    drawButton(renderer, downBtn, "DN", P.green, endIdx < total);

    const int listY = topY + 18;
    char line[160];
    for (int i = startIdx; i < endIdx; ++i) {
        const int row = i - startIdx;
        const int rowY = listY + row * 36;
        SDL_Rect btn = {window.rect.x + window.rect.w - 96, rowY - 6, 80, 24};
        if (i < nShips) {
            const ShipClass& sc = classes[i];
            drawText(renderer, topX, rowY, sc.name.c_str(), P.cyan, 1);
            std::snprintf(line, sizeof(line), "CR%.0F | CG:%.0F HW:%.0F LW:%.0F AR:%.0F U:%.0F", sc.price, sc.cargoCapacity, sc.heavyWeapons, sc.lightWeapons, sc.armor, sc.utility);
            drawText(renderer, topX, rowY + 14, line, P.text, 1);
            drawButton(renderer, btn, "BUY", P.green, cash >= sc.price);
        } else if (i == nShips) {
            fillRect(renderer, topX, rowY + 7, window.rect.w - 2 * WINDOW_PAD, 1, P.border);
            drawText(renderer, topX, rowY + 13, "-- SHIP MODULES (FIT WHILE DOCKED) --", P.amber, 1);
        } else {
            const int m = i - nShips - 1;
            const ModuleDef& def = mods[m];
            std::snprintf(line, sizeof(line), "%s [%s]", def.name.c_str(), moduleSlotLabel(def.slot));
            drawText(renderer, topX, rowY, line, P.cyan, 1);
            std::snprintf(line, sizeof(line), "CR%.0F  SY%d  %s", def.price, def.minShipyard, def.blurb.c_str());
            const bool locked = syLevel < def.minShipyard;
            drawText(renderer, topX, rowY + 14, line, locked ? P.dim : P.text, 1);
            const bool canFit = liveShipyard && !locked && cash >= def.price;
            drawButton(renderer, btn, locked ? "LVL" : "FIT", canFit ? P.green : P.dim, canFit);
        }
    }
    
    if (state.confirmBuyShipIndex >= 0) {
        SDL_Rect dlg = { window.rect.x + 20, window.rect.y + window.rect.h / 2 - 40, window.rect.w - 40, 80 };
        fillRect(renderer, dlg.x, dlg.y, dlg.w, dlg.h, SDL_Color{20, 25, 35, 250});
        strokeRect(renderer, dlg.x, dlg.y, dlg.w, dlg.h, P.amber);
        drawText(renderer, dlg.x + 10, dlg.y + 10, "ARE YOU SURE YOU WANT TO CHANGE YOUR HULL?", P.red, 1);
        drawText(renderer, dlg.x + 10, dlg.y + 25, "THIS WILL PERMANENTLY DESTROY YOUR PREVIOUS HULL.", P.amber, 1);
        
        SDL_Rect yesBtn = { dlg.x + 10, dlg.y + 50, 60, 20 };
        SDL_Rect noBtn = { dlg.x + 80, dlg.y + 50, 60, 20 };
        drawButton(renderer, yesBtn, "YES", P.green);
        drawButton(renderer, noBtn, "NO", P.red);
    }
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
        const bool isEnRoute = game.playerAgent >= 0 && game.agents[game.playerAgent].ship.enRoute;
        const bool isEnRouteToThis = isEnRoute && game.agents[game.playerAgent].destStar == window.star;
        const bool isAtThis = game.playerAgent >= 0 && game.agents[game.playerAgent].currentStar == window.star && !isEnRoute;
        
        std::string label = "GO";
        SDL_Color color = P.cyan;
        if (isEnRouteToThis) {
            label = "EN ROUTE"; color = P.green;
        } else if (isAtThis) {
            label = "DOCKED"; color = P.dim;
        } else if (isEnRoute) {
            label = "MOVING"; color = P.red;
        }
        drawButton(renderer, layout.route, label, color, game.playerAgent >= 0);
        drawButton(renderer, layout.trade, "TRADE", P.green, false);
        drawButton(renderer, layout.contracts, "JOBS", P.cyan, game.playerCanOpenContractsAt(window.star));
        drawButton(renderer, layout.colony, "C COL/RE", P.amber, false);
        drawButton(renderer, layout.cargo, "CARGO", P.cyan, game.playerAgent >= 0);
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
    const bool isEnRoute = game.playerAgent >= 0 && game.agents[game.playerAgent].ship.enRoute;
    const bool isEnRouteToThis = isEnRoute && game.agents[game.playerAgent].destStar == window.star;
    const bool isAtThis = game.playerAgent >= 0 && game.agents[game.playerAgent].currentStar == window.star && !isEnRoute;
    
    std::string label = "GO";
    SDL_Color color = P.cyan;
    if (isEnRouteToThis) {
        label = "EN ROUTE"; color = P.green;
    } else if (isAtThis) {
        label = "DOCKED"; color = P.dim;
    } else if (isEnRoute) {
        label = "MOVING"; color = P.red;
    }
    drawButton(renderer, layout.route, label, color, game.playerAgent >= 0);
    drawButton(renderer, layout.trade, "TRADE", P.green, playerMarketStar(game) == window.star);
    drawButton(renderer, layout.contracts, "JOBS", P.cyan, game.playerCanOpenContractsAt(window.star));
    drawButton(renderer, layout.colony, "C COL/RE", P.amber, game.playerAtStar(window.star));
    drawButton(renderer, layout.cargo, "CARGO", P.cyan, game.playerAgent >= 0);
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
        if (contract.type == ContractType::Scout && contract.reportSignalPending) label = "WAIT SIGNAL";
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
        char buf1[32], buf2[32], buf3[32];
        std::snprintf(buf1, sizeof(buf1), "%.0F", player.money);
        std::snprintf(buf2, sizeof(buf2), "%.0F/%.0F", shipCargoMass(player.ship), player.ship.cargoCapacity);
        std::snprintf(buf3, sizeof(buf3), "%.0F%%", shipFuelFraction(player.ship) * 100.0);
        int cx = topX;
        cx = drawStat(renderer, cx, topY + 16, "CREDITS ", buf1, P.green);
        cx = drawStat(renderer, cx, topY + 16, "CARGO ", buf2);
        cx = drawStat(renderer, cx, topY + 16, "FUEL ", buf3);
    }

    const std::vector<ElementDefinition>& elements = elementDefinitions();
    for (size_t i = 0; i < elements.size(); ++i) {
        const int idx = int(i);
        const SDL_Rect rect = elementRect(layout, idx);
        if (rect.w <= 0) continue;

        int mx = 0, my = 0;
        Uint32 mstate = SDL_GetMouseState(&mx, &my);
        bool hovered = (mx >= rect.x && mx < rect.x + rect.w && my >= rect.y && my < rect.y + rect.h);
        bool pressed = hovered && (mstate & SDL_BUTTON(SDL_BUTTON_LEFT));

        SDL_Color fill = {34, 44, 62, 190};
        if (market) fill = marketCellColor(*market, idx);

        if (pressed) {
            fill = {12, 16, 26, 255};
        } else if (hovered) {
            fill.r = Uint8(std::min(255, fill.r + 40));
            fill.g = Uint8(std::min(255, fill.g + 40));
            fill.b = Uint8(std::min(255, fill.b + 40));
        }

        fillRect(renderer, rect.x, rect.y, rect.w, rect.h, fill);

        SDL_Color border = {52, 68, 92, 220};
        if (market && star && hasFocus(star->resourceFocus, idx)) border = P.cyan;
        if (market && star && hasFocus(star->demandFocus, idx)) border = P.red;
        if (idx == selection.element) border = P.amber;
        if (pressed) border = P.amber;
        
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
    
    bool canUpgrade = false;
    if (liveMarket && star && window.star >= 0 && window.star < int(game.colonies.size())) {
        if (game.colonies[window.star].shipyardLevel > 0 || game.colonies[window.star].infrastructure >= 1.0) canUpgrade = true;
    }
    drawButton(renderer, layout.upgrade, "SHIPYARD", P.cyan, canUpgrade);

    const int infoX = layout.buy.x;
    const int infoY = layout.upgrade.y + 42;
    if (selection.element >= 0 && selection.element < int(elements.size())) {
        const ElementDefinition& element = elements[selection.element];
        drawText(renderer, infoX, infoY, std::string(element.symbol) + " " + element.name, P.text, 1);
        if (market && selection.element < int(market->prices.size())) {
            char buf1[32], buf2[32];
            std::snprintf(buf1, sizeof(buf1), "%.1F", market->prices[selection.element]);
            drawStat(renderer, infoX, infoY + 14, "PRICE ", buf1, P.dim, P.amber);
            std::snprintf(buf1, sizeof(buf1), "%.0F", market->supply[selection.element].amount);
            drawStat(renderer, infoX, infoY + 28, "SUPPLY ", buf1, P.dim, P.green);
            std::snprintf(buf1, sizeof(buf1), "%.0F", market->demand[selection.element].amount);
            drawStat(renderer, infoX, infoY + 42, "DEMAND ", buf1, P.dim, P.red);
            std::snprintf(buf1, sizeof(buf1), "%.1F", element.atomicMass);
            std::snprintf(buf2, sizeof(buf2), "%.2F/%.2F", element.fusionFuelTrait, element.fissionFuelTrait);
            int cx = infoX;
            cx = drawStat(renderer, cx, infoY + 56, "MASS ", buf1, P.dim, P.text);
            cx = drawStat(renderer, cx, infoY + 56, "FUEL ", buf2, P.dim, P.text);
        }
    }

    drawText(renderer, layout.tableX, window.rect.y + window.rect.h - 15, "CLICK AMOUNT + TYPE NUMBER / EMPTY=MAX / RMB CELL QUICK BUY", P.dim, 1);
}

void drawCargoWindow(SDL_Renderer* renderer, const Game& game, const Window& window, bool active) {
    drawWindowFrame(renderer, window, "PLAYER CARGO", active);
    
    int y = window.rect.y + TITLE_H + 12;
    int x = window.rect.x + 12;
    
    if (game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) {
        drawText(renderer, x, y, "NO PLAYER", P.red, 1);
        return;
    }
    
    const Agent& player = game.agents[game.playerAgent];
    const Ship& ship = player.ship;
    
    char line[128];
    std::snprintf(line, sizeof(line), "TOTAL MASS: %.0F / %.0F", shipCargoMass(ship), ship.cargoCapacity);
    drawText(renderer, x, y, line, P.amber, 1);
    y += 24;
    
    if (ship.cargo.empty()) {
        drawText(renderer, x, y, "CARGO HOLD IS EMPTY", P.dim, 1);
        return;
    }
    
    drawText(renderer, x, y, "ELEMENT", P.dim, 1);
    drawText(renderer, x + 100, y, "AMOUNT", P.dim, 1);
    drawText(renderer, x + 200, y, "MASS", P.dim, 1);
    y += 18;
    
    for (const auto& item : ship.cargo) {
        if (item.amount <= 0.0) continue;
        double mass = item.amount * resourceUnitMass(item.element);
        std::snprintf(line, sizeof(line), "%s", item.element.c_str());
        drawText(renderer, x, y, line, P.text, 1);
        
        std::snprintf(line, sizeof(line), "%.1F", item.amount);
        drawText(renderer, x + 100, y, line, P.text, 1);
        
        std::snprintf(line, sizeof(line), "%.1F", mass);
        drawText(renderer, x + 200, y, line, P.amber, 1);
        
        SDL_Rect dropBtn = {x + 270, y - 2, 44, 18};
        drawButton(renderer, dropBtn, "DROP", P.red, true);
        
        y += 24;
    }
}

void drawWindows(SDL_Renderer* renderer, const Game& game, int, int, const HudSelection& selection, const WindowState& state) {
    for (size_t i = 0; i < state.windows.size(); ++i) {
        const Window& window = state.windows[i];
        const bool active = window.id == state.activeId;
        if (window.kind == WindowKind::SystemInfo) {
            drawSystemWindow(renderer, game, window, selection, active);
        } else if (window.kind == WindowKind::Contracts) {
            drawContractsWindow(renderer, game, window, selection, active);
        } else if (window.kind == WindowKind::Shipyard) {
            drawShipyardWindow(renderer, game, window, state, active);
        } else if (window.kind == WindowKind::Cargo) {
            drawCargoWindow(renderer, game, window, active);
        } else {
            drawTradeWindow(renderer, game, window, selection, state, active);
        }
    }
}

// --- Vertical-slice HUD additions: hull/mining/chromocore, objectives, news log ---
static SDL_Color hullColor(double frac) {
    if (frac > 0.5) return P.green;
    if (frac > 0.25) return P.amber;
    return P.red;
}

static void drawShipTechPanel(SDL_Renderer* renderer, const Game& game, int x, int y, int w) {
    panel(renderer, x, y, w, 120);
    drawText(renderer, x + 10, y + 9, "SHIP SYSTEMS", P.cyan, 1);

    char line[160];
    double hull = 0.0, hullMax = 1.0;
    if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
        const Ship& sh = game.agents[game.playerAgent].ship;
        hull = sh.hullHP;
        hullMax = std::max(1.0, sh.maxHullHP);
    }
    const double hf = clamp01(hull / hullMax);
    drawText(renderer, x + 10, y + 26, "HULL", P.dim, 1);
    bar(renderer, x + 46, y + 25, w - 120, 7, hf, hullColor(hf));
    std::snprintf(line, sizeof(line), "%.0F/%.0F", hull, hullMax);
    drawText(renderer, x + w - 66, y + 26, line, P.text, 1);

    if (game.playerMining) {
        const char* nm = (game.miningStar >= 0 && game.miningStar < int(game.cluster.stars.size()))
            ? game.cluster.stars[game.miningStar].name.c_str() : "-";
        std::snprintf(line, sizeof(line), "MINING @ %s  +%.0F", nm, game.miningYieldAccum);
        drawText(renderer, x + 10, y + 40, line, P.green, 1);
    } else {
        drawText(renderer, x + 10, y + 40, "MINING OFF  (DOCK + M)", P.dim, 1);
    }

    const double threshold = 100.0 + game.tech.cores * 40.0;
    const double rprog = clamp01(threshold > 0.0 ? game.tech.research / threshold : 0.0);
    std::snprintf(line, sizeof(line), "CORES %d", game.tech.cores);
    drawText(renderer, x + 10, y + 54, line, P.amber, 1);
    drawText(renderer, x + 92, y + 54, "RSCH", P.dim, 1);
    bar(renderer, x + 128, y + 53, w - 138, 7, rprog, P.cyan);

    const char* codes[7] = {"IN", "CH", "MA", "TA", "KI", "SE", "LU"};
    const double vals[7] = {game.tech.intellect, game.tech.charisma, game.tech.materials,
        game.tech.tactics, game.tech.kinematics, game.tech.sensors, game.tech.luck};
    for (int i = 0; i < 7; ++i) {
        const int col = i % 4, rowi = i / 4;
        char cell[24];
        std::snprintf(cell, sizeof(cell), "%s%.2F", codes[i], vals[i]);
        const SDL_Color cc = vals[i] > 1.0001 ? P.green : P.dim;
        drawText(renderer, x + 10 + col * 78, y + 72 + rowi * 12, cell, cc, 1);
    }
}

static void drawObjectivesPanel(SDL_Renderer* renderer, const Game& game, int x, int y, int w) {
    struct Obj { const char* text; bool done; };
    std::vector<Obj> objs;
    if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
        const Agent& p = game.agents[game.playerAgent];
        const Ship& sh = p.ship;
        objs.push_back({"TRADE: BUY (B) / SELL (V)", p.trades > 0});
        objs.push_back({"MINE ORE: DOCK + PRESS M", game.miningYieldAccum > 0.0 || game.playerMining});
        objs.push_back({"UPGRADE: SHIPYARD (U)", !sh.modules.empty()});
        objs.push_back({"RESEARCH A CHROMOCORE", game.tech.cores > 0});
        bool anomalyKnown = false;
        for (size_t i = 0; i < game.anomalies.size(); ++i) {
            if (game.anomalies[i].discovered && !game.anomalies[i].resolved) { anomalyKnown = true; break; }
        }
        if (anomalyKnown) objs.push_back({"SCAN ANOMALY: PRESS K", false});
        if (sh.hullHP < sh.maxHullHP - 0.5) objs.push_back({"REPAIR HULL: DOCK + PRESS J", false});
    }
    const int rows = std::min(6, int(objs.size()));
    const int h = 30 + rows * 13;
    panel(renderer, x, y, w, h);
    drawText(renderer, x + 10, y + 9, "OBJECTIVES", P.amber, 1);
    for (int i = 0; i < rows; ++i) {
        const int ry = y + 26 + i * 13;
        drawText(renderer, x + 10, ry, objs[i].done ? "[X]" : "[ ]", objs[i].done ? P.green : P.dim, 1);
        drawText(renderer, x + 34, ry, objs[i].text, objs[i].done ? P.dim : P.text, 1);
    }
}

static SDL_Color newsColor(int kind) {
    switch (kind) {
        case 1: return P.cyan;   // market
        case 2: return P.red;    // combat
        case 3: return P.green;  // discovery
        case 4: return P.amber;  // progress
        default: return P.text;  // info
    }
}

static void drawNewsFeed(SDL_Renderer* renderer, const Game& game, int x, int y, int w, int maxLines) {
    const int h = 26 + maxLines * 12;
    panel(renderer, x, y, w, h);
    drawText(renderer, x + 10, y + 9, "LOG", P.cyan, 1);
    const int n = int(game.news.size());
    const int shown = std::min(maxLines, n);
    const int maxChars = std::max(8, (w - 20) / 6);
    for (int i = 0; i < shown; ++i) {
        const NewsItem& it = game.news[size_t(n - 1 - i)];
        std::string t = it.text;
        if (int(t.size()) > maxChars) t = t.substr(0, size_t(maxChars - 1)) + "~";
        drawText(renderer, x + 10, y + 24 + i * 12, t, newsColor(it.kind), 1);
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
        char buf1[32], buf2[32], buf3[32], buf4[32];
        std::snprintf(buf1, sizeof(buf1), "%.0F", player.money);
        std::snprintf(buf2, sizeof(buf2), "%d", player.trades);
        std::snprintf(buf3, sizeof(buf3), "%d", game.playerColonyCount());
        std::snprintf(buf4, sizeof(buf4), "%d", activeContracts);
        int cx = 22;
        cx = drawStat(renderer, cx, y + 27, "CREDITS ", buf1, P.green);
        cx = drawStat(renderer, cx, y + 27, "TRADES ", buf2);
        cx = drawStat(renderer, cx, y + 27, "COLONIES ", buf3);
        cx = drawStat(renderer, cx, y + 27, "JOBS ", buf4);
        y += 54;
    }

    drawShipTechPanel(renderer, game, 12, y, leftW);
    y += 130;

    drawStarPanel(renderer, game, selection.star, selection.element, 12, y, leftW);
    y += 160;
    drawAgentPanel(renderer, game, selection.agent, 12, y, leftW);

    drawFactionPanel(renderer, game, screenW - 260, 12, 248);
    {
        const int facH = 44 + std::min(6, int(game.factions.size())) * 19;
        drawObjectivesPanel(renderer, game, screenW - 260, 12 + facH + 10, 248);
    }
    // drawControlHints(renderer, screenW, screenH);

    {
        const int newsLines = screenH < 780 ? 8 : 14;
        const int newsH = 26 + newsLines * 12;
        const int newsW = std::min(620, std::max(280, screenW - 12 - 168));
        const int newsY = std::max(y + 8, screenH - newsH - 72);
        drawNewsFeed(renderer, game, 12, newsY, newsW, newsLines);
    }

    if (!game.lastEvent.empty()) {
        const int w = std::min(screenW - 24, 640);
        const int eventY = std::max(12, screenH - 70);
        panel(renderer, 12, eventY, w, 34);
        drawText(renderer, 22, eventY + 12, game.lastEvent, P.amber, 1);
    }

    if (selection.followAgent) {
        drawText(renderer, screenW / 2 - 36, 14, "FOLLOW", P.amber, 2);
    }
}

std::string getTutorialText(const Game& game, int step, int& outArrowTarget, bool& outOpenTrade) {
    outArrowTarget = 0;
    outOpenTrade = false;
    switch (step) {
        case 0: return "Master, I am Timertia - your AI core Agent.";
        case 1: return "Congratulations with obtaining your trading Licence!";
        case 2: outArrowTarget = 1; return "You can view your balance here.";
        case 3: return "You own 1 space ship unit for now.";
        case 4: outArrowTarget = 2; return "My subagents will monitor its system states here.";
        case 5: {
            std::string starName = "Unknown Node";
            if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                int starId = game.agents[game.playerAgent].currentStar;
                if (starId >= 0 && starId < (int)game.cluster.stars.size()) {
                    starName = game.cluster.stars[starId].name;
                }
            }
            outArrowTarget = 0;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "Your vessel is curretnly at %s. You can acess a model of local star system here.", starName.c_str());
            return buf;
        }
        case 6: outArrowTarget = 0; outOpenTrade = true; return "With your trading licence you can perform HIGH-FREQUENCY BROKERAGE on local market.";
        case 7: return "A periodic table based on standard supersymmetrical model is common CONVENTION of interstellar market.";
        case 8: return "NASH EQUILIBRIUM proofs that it is the best to buy on suply and sell on demand.";
        case 9: {
            std::string element = "isotopes";
            if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                int starId = game.agents[game.playerAgent].currentStar;
                if (starId >= 0 && starId < (int)game.markets.size()) {
                    const Market& m = game.markets[starId];
                    int bestEl = -1;
                    double maxSupply = -1.0;
                    for (int i=0; i<(int)elementCount(); ++i) {
                        if (i < (int)m.supply.size() && m.supply[i].amount > maxSupply) {
                            maxSupply = m.supply[i].amount;
                            bestEl = i;
                        }
                    }
                    if (bestEl >= 0) {
                        const auto& defs = elementDefinitions();
                        if (bestEl < (int)defs.size()) element = defs[bestEl].name;
                    }
                }
            }
            char buf[256];
            std::snprintf(buf, sizeof(buf), "Local model suggest you to buy %s.", element.c_str());
            return buf;
        }
        case 10: {
            std::string starName = "an adjacent node";
            if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                int starId = game.agents[game.playerAgent].currentStar;
                if (starId >= 0 && starId < (int)game.cluster.stars.size() && starId < (int)game.markets.size()) {
                    const ClusterStar& s = game.cluster.stars[starId];
                    const Market& m = game.markets[starId];
                    int bestEl = -1;
                    double maxSupply = -1.0;
                    for (int i=0; i<(int)elementCount(); ++i) {
                        if (i < (int)m.supply.size() && m.supply[i].amount > maxSupply) {
                            maxSupply = m.supply[i].amount;
                            bestEl = i;
                        }
                    }
                    if (bestEl >= 0) {
                        int bestStar = -1;
                        double maxDemand = -1.0;
                        for (int i=0; i<(int)game.cluster.stars.size(); ++i) {
                            if (i == starId) continue;
                            if (i >= (int)game.markets.size()) continue;
                            double dx = game.cluster.stars[i].x - s.x;
                            double dy = game.cluster.stars[i].y - s.y;
                            double dz = game.cluster.stars[i].z - s.z;
                            double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                            if (dist < 15.0) {
                                if (bestEl < (int)game.markets[i].demand.size() && game.markets[i].demand[bestEl].amount > maxDemand) {
                                    maxDemand = game.markets[i].demand[bestEl].amount;
                                    bestStar = i;
                                }
                            }
                        }
                        if (bestStar >= 0) {
                            starName = game.cluster.stars[bestStar].name;
                        }
                    }
                }
            }
            char buf[256];
            std::snprintf(buf, sizeof(buf), "We also have insight that the best place to sell it right now is %s. I want to accentuate your attention to %s!", starName.c_str(), starName.c_str());
            return buf;
        }
        case 11: return "By the way, you can also upgrade your vessel and purchase more trading licenses.";
        case 12: return "Finally, new technology of applied color supercondctivity has developed novel AI cores.";
        case 13: outArrowTarget = 0; return "They are still prototypes and very rare. Be sure to privatise all you finde.";
        case 14: return "I am at your service with more insignts at any time Master [V].";
        case 100: {

            std::string element = "isotopes";
            std::string starName = "an adjacent node";
            if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                int starId = game.agents[game.playerAgent].currentStar;
                if (starId >= 0 && starId < (int)game.markets.size()) {
                    const Market& m = game.markets[starId];
                    int bestEl = -1;
                    double maxSupply = -1.0;
                    for (int i=0; i<(int)elementCount(); ++i) {
                        if (i < (int)m.supply.size() && m.supply[i].amount > maxSupply) {
                            maxSupply = m.supply[i].amount;
                            bestEl = i;
                        }
                    }
                    if (bestEl >= 0) {
                        const auto& defs = elementDefinitions();
                        if (bestEl < (int)defs.size()) element = defs[bestEl].name;
                        
                        const ClusterStar& s = game.cluster.stars[starId];
                        int bestStar = -1;
                        double maxDemand = -1.0;
                        for (int i=0; i<(int)game.cluster.stars.size(); ++i) {
                            if (i == starId) continue;
                            if (i >= (int)game.markets.size()) continue;
                            double dx = game.cluster.stars[i].x - s.x;
                            double dy = game.cluster.stars[i].y - s.y;
                            double dz = game.cluster.stars[i].z - s.z;
                            double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                            if (dist < 15.0) {
                                if (bestEl < (int)game.markets[i].demand.size() && game.markets[i].demand[bestEl].amount > maxDemand) {
                                    maxDemand = game.markets[i].demand[bestEl].amount;
                                    bestStar = i;
                                }
                            }
                        }
                        if (bestStar >= 0) {
                            starName = game.cluster.stars[bestStar].name;
                        }
                    }
                }
            }
            char buf[512];
            std::snprintf(buf, sizeof(buf), "Local model suggest you to buy %s. We also have insight that the best place to sell it right now is %s. I want to accentuate your attention to %s!", element.c_str(), starName.c_str(), starName.c_str());
            return buf;
        }
    }
    return "";
}

bool advanceVisualNovel(WindowState& state, Game& game, int winW, int winH) {
    auto& vn = state.vnState;
    if (!vn.active) return false;
    
    if (vn.textProgress < vn.targetText.length()) {
        vn.textProgress = vn.targetText.length();
        vn.currentText = vn.targetText;
    } else {
        if (!vn.tutorialCompleted) {
            vn.tutorialStep++;
            if (vn.tutorialStep > 14) {
                vn.tutorialCompleted = true;
                vn.active = false;
            } else {
                bool openTrade = false;
                vn.targetText = getTutorialText(game, vn.tutorialStep, vn.arrowTarget, openTrade);
                vn.textProgress = 0.0f;
                if (openTrade && game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                    openTradeWindow(state, game.agents[game.playerAgent].currentStar, winW, winH);
                }
            }
        } else {
            vn.active = false;
        }
    }
    return true;
}

void updateVisualNovel(WindowState& state, Game& game, double dt, int screenW, int screenH) {
    auto& vn = state.vnState;
    if (!vn.active) return;
    
    if (vn.tutorialCompleted) {
        if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
            int currentStar = game.agents[game.playerAgent].currentStar;
            if (currentStar >= 0 && vn.visitedSystems.find(currentStar) == vn.visitedSystems.end()) {
                vn.visitedSystems.insert(currentStar);
                vn.tutorialStep = 100;
                bool dummy;
                vn.targetText = getTutorialText(game, 100, vn.arrowTarget, dummy);
                vn.textProgress = 0.0f;
                vn.currentText = "";
                vn.active = true;
            }
        }
    } else {
        if (vn.targetText.empty() && vn.tutorialStep == 0) {
            bool dummyOpen = false;
            vn.targetText = getTutorialText(game, vn.tutorialStep, vn.arrowTarget, dummyOpen);
        }
    }
    
    if (vn.textProgress < vn.targetText.length()) {
        vn.textProgress += dt * 50.0f;
        if (vn.textProgress > vn.targetText.length()) vn.textProgress = vn.targetText.length();
        vn.currentText = vn.targetText.substr(0, (size_t)vn.textProgress);
    }
}

std::string wrapText(const std::string& text, int maxChars) {
    std::string result;
    int lineLen = 0;
    std::string word;
    
    for (char c : text) {
        if (c == ' ' || c == '\n') {
            if (lineLen + word.length() > maxChars) {
                result += "\n";
                lineLen = 0;
            } else if (!result.empty() && result.back() != '\n') {
                result += " ";
                lineLen++;
            }
            result += word;
            lineLen += word.length();
            word.clear();
            if (c == '\n') {
                result += "\n";
                lineLen = 0;
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        if (lineLen + word.length() > maxChars && !result.empty() && result.back() != '\n') {
            result += "\n";
        } else if (!result.empty() && result.back() != '\n') {
            result += " ";
        }
        result += word;
    }
    return result;
}

void drawVisualNovel(SDL_Renderer* renderer, const WindowState& state, int screenW, int screenH, SDL_Texture* tex) {
    if (!state.vnState.active) return;
    
    // Render Alice texture with glitch effect
    if (tex) {
        int texW = 0, texH = 0;
        SDL_QueryTexture(tex, NULL, NULL, &texW, &texH);
        
        // Scale to fit on screen if needed, leave space for dialogue
        float scale = std::min(1.0f, (float)(screenH - 180) / texH);
        if (scale < 0.2f) scale = 0.2f;
        int drawW = texW * scale;
        int drawH = texH * scale;
        
        int baseX = screenW - drawW - 40; // Bottom-right corner
        int baseY = screenH - drawH;
        
        // Draw the base image with a smooth wave (CRT wobble/degaussing effect)
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        Uint32 time = SDL_GetTicks();
        
        // Slice the image horizontally to apply the wave
        const int stripHeight = 4; // Higher is faster but blockier, 4 is a good balance
        for (int sy = 0; sy < texH; sy += stripHeight) {
            int h = std::min(stripHeight, texH - sy);
            SDL_Rect src = {0, sy, texW, h};
            
            // Smooth wave combination
            float wave = sin(sy * 0.015f + time * 0.002f) * 6.0f + 
                         sin(sy * 0.040f - time * 0.008f) * 2.0f;
                         
            SDL_Rect dst = {
                baseX + (int)wave, 
                baseY + (int)(sy * scale), 
                drawW, 
                (int)(h * scale) + 1 // +1 to prevent gaps between strips
            };
            SDL_RenderCopy(renderer, tex, &src, &dst);
        }
        
        // --- Glitch Effect ---
        
        // Mode 1: Constant Micro-Ripples (small blocks, small offsets, runs every frame)
        int numMicro = 10 + (rand() % 15);
        for (int i = 0; i < numMicro; ++i) {
            int gw = 10 + (rand() % 40);
            int gh = 2 + (rand() % 10);
            int gx = rand() % std::max(1, (texW - gw + 1));
            int gy = rand() % std::max(1, (texH - gh + 1));
            
            SDL_Rect src = {gx, gy, gw, gh};
            
            // Small ripple offset (mostly horizontal, slight vertical)
            int offsetX = (rand() % 20) - 10;
            int offsetY = (rand() % 6) - 3;
            
            SDL_Rect dst = {
                baseX + (int)(gx * scale) + offsetX, 
                baseY + (int)(gy * scale) + offsetY, 
                (int)(gw * scale) + 1, 
                (int)(gh * scale) + 1
            };
            
            // Randomly flash some ripples in dark/bright B&W
            int intensity = (rand() % 2 == 0) ? 100 + (rand() % 155) : 255;
            SDL_SetTextureColorMod(tex, intensity, intensity, intensity);
            SDL_RenderCopy(renderer, tex, &src, &dst);
        }
        
        // Mode 2: Occasional Macro-Glitch (large blocks, large offsets, rare)
        if (rand() % 100 < 8) { // 8% chance per frame
            int numMacro = 1 + (rand() % 3);
            for (int i = 0; i < numMacro; ++i) {
                // Large chunks, sometimes full width
                int gw = (rand() % 2 == 0) ? texW : (texW / 2 + rand() % (texW / 2));
                int gh = 10 + (rand() % (texH / 4));
                int gx = rand() % std::max(1, (texW - gw + 1));
                int gy = rand() % std::max(1, (texH - gh + 1));
                
                SDL_Rect src = {gx, gy, gw, gh};
                
                // Large jerky offset
                int offsetX = (rand() % 100) - 50;
                int offsetY = (rand() % 20) - 10;
                
                SDL_Rect dst = {
                    baseX + (int)(gx * scale) + offsetX, 
                    baseY + (int)(gy * scale) + offsetY, 
                    (int)(gw * scale) + 1, 
                    (int)(gh * scale) + 1
                };
                
                // Severe color distortion (dark or bright white)
                int intensity = (rand() % 2 == 0) ? 50 : 255; 
                SDL_SetTextureColorMod(tex, intensity, intensity, intensity);
                SDL_RenderCopy(renderer, tex, &src, &dst);
            }
        }
        SDL_SetTextureColorMod(tex, 255, 255, 255); // Reset
    }
    
    // Draw Dialogue Box (above the log)
    int newsLines = screenH < 780 ? 8 : 14;
    int newsH = 26 + newsLines * 12;
    int boxH = 150;
    int boxY = screenH - newsH - 72 - boxH - 20;
    int boxX = 20;
    int boxW = screenW - 40;
    
    // Semi-transparent background
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 220);
    SDL_Rect boxRect = {boxX, boxY, boxW, boxH};
    SDL_RenderFillRect(renderer, &boxRect);
    
    // Border
    SDL_SetRenderDrawColor(renderer, 70, 240, 255, 255);
    SDL_RenderDrawRect(renderer, &boxRect);
    
    // Text
    drawText(renderer, boxX + 20, boxY + 20, "TIMERTIA", {70, 240, 255, 255}, 2);
    int maxChars = (boxW - 40) / 12;
    std::string wrapped = wrapText(state.vnState.currentText, maxChars);
    drawText(renderer, boxX + 20, boxY + 60, wrapped, {214, 228, 238, 255}, 2);
    
    int ax = 0, ay = 0;
    if (state.vnState.arrowTarget == 1) { ax = 350; ay = 94; }
    else if (state.vnState.arrowTarget == 2) { ax = 350; ay = 148; }
    else if (state.vnState.arrowTarget == 3) { ax = 350; ay = 278; }
    else if (state.vnState.arrowTarget == 4) { ax = screenW / 2 + 150; ay = 100; }
    else if (state.vnState.arrowTarget == 5) { ax = 350; ay = 250; }
    
    if (ax > 0 && ay > 0) {
        if ((SDL_GetTicks() / 300) % 2 == 0) {
            drawText(renderer, ax, ay, "<-- TARGET", {255, 100, 100, 255}, 2);
        }
    }
}

}
