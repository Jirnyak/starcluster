#include "ui.h"
#include "drive.h"
#include "econ.h"
#include "modules.h"
#include "render2d.h"
#include "i18n.h"
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
    SDL_Rect sellAll = {0, 0, 0, 0};
    SDL_Rect refuel = {0, 0, 0, 0};
    SDL_Rect hold = {0, 0, 0, 0};
};

struct SystemLayout {
    SDL_Rect route = {0, 0, 0, 0};
    SDL_Rect trade = {0, 0, 0, 0};
    SDL_Rect contracts = {0, 0, 0, 0};
    SDL_Rect colony = {0, 0, 0, 0};
    SDL_Rect cargo = {0, 0, 0, 0};
    SDL_Rect shipyard = {0, 0, 0, 0};
    SDL_Rect exchange = {0, 0, 0, 0};
    SDL_Rect exotics = {0, 0, 0, 0};
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
// Высота шапки доски над списком (§24: две новых строки — репутация у
// владельца системы и трюм флота здесь). Вынесена в константу, потому что
// от неё зависят СРАЗУ ТРИ места: строки, кнопки и число влезающих строк.
const int CONTRACT_HEADER_H = 24;

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
        // Нижняя граница 456, а не 420: столбец действий вырос на ряд (SELL ALL),
        // и на прежнем минимуме разбор выбранного элемента уезжал под подсказку.
        rect.h = std::min(520, std::max(456, screenH - 180));
        rect.x = std::max(300, (screenW - rect.w) / 2 + cascade * 18);
        rect.y = std::max(70, screenH - rect.h - 28 - cascade * 10);
    } else if (kind == WindowKind::Contracts) {
        rect.w = std::min(660, std::max(520, screenW - 520));
        rect.h = 374;
        rect.x = std::max(344, (screenW - rect.w) / 2 + cascade * 18);
        rect.y = std::max(96, screenH - rect.h - 46 - cascade * 10);
    } else if (kind == WindowKind::Cargo) {
        rect.w = 720;
        rect.h = 512;
        rect.x = std::max(380, (screenW - rect.w) / 2 + cascade * 18);
        rect.y = std::max(100, screenH - rect.h - 50 - cascade * 10);
    } else if (kind == WindowKind::Exchange) {
        rect.w = std::min(880, std::max(680, screenW - 360));
        rect.h = std::min(560, std::max(420, screenH - 200));
        rect.x = std::max(280, (screenW - rect.w) / 2 + cascade * 18);
        rect.y = std::max(80, screenH - rect.h - 60 - cascade * 10);
    } else if (kind == WindowKind::Exotics) {
        // Высота считается от содержимого: три вещества по 58 px, шапка и два
        // ряда переоснастки. Оставлять запас «на всякий случай» тут нечему —
        // веществ ровно три, и окно на пол-экрана выглядело пустым.
        rect.w = std::min(760, std::max(620, screenW - 420));
        rect.h = 352;
        rect.x = std::max(300, (screenW - rect.w) / 2 + cascade * 18);
        rect.y = std::max(80, screenH - rect.h - 56 - cascade * 10);
    } else if (kind == WindowKind::Colony) {
        rect.w = 520;
        rect.h = 356;
        rect.x = std::max(300, (screenW - rect.w) / 2 + cascade * 18);
        rect.y = std::max(90, screenH - rect.h - 56 - cascade * 10);
    } else if (kind == WindowKind::ShipFit) {
        rect.w = 400;
        rect.h = 420;
        rect.x = std::max(400, (screenW - rect.w) / 2 + cascade * 18);
        rect.y = std::max(120, screenH - rect.h - 50 - cascade * 10);
    } else {
        // 440 не хватало: ряд действий доходит до 525 px (последняя кнопка —
        // EXCHANGE), и он лез за край окна поверх текста.
        rect.w = 550;
        rect.h = 300;
        rect.x = std::max(252, std::min(screenW - rect.w - 280, 272 + cascade * 22));
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
    layout.route = {window.rect.x + WINDOW_PAD, y, 65, 24};
    layout.trade = {window.rect.x + WINDOW_PAD + 70, y, 60, 24};
    layout.contracts = {window.rect.x + WINDOW_PAD + 135, y, 55, 24};
    layout.colony = {window.rect.x + WINDOW_PAD + 195, y, 80, 24};
    layout.cargo = {window.rect.x + WINDOW_PAD + 280, y, 60, 24};
    layout.shipyard = {window.rect.x + WINDOW_PAD + 345, y, 80, 24};
    // Биржа — в тот же ряд. Вторым этажом кнопка легла поверх текста системы;
    // окно расширено (см. defaultWindowRect), чтобы ряд поместился целиком.
    layout.exchange = {window.rect.x + WINDOW_PAD + 430, y, 95, 24};
    // Хайтек-этаж (§31). Кнопка стоит рядом с биржей и ГОРИТ только там, где
    // такой рынок есть: он редок (около 12% систем), и это должно быть видно
    // с одного взгляда — иначе игрок не поймёт, что системы бывают разные.
    layout.exotics = {window.rect.x + WINDOW_PAD, y - 28, 95, 24};
    return layout;
}

// Окно собственности. Слева — из чего сложилась цена (или как живёт колония),
// справа — единственный столбец действий: поле суммы и кнопки над кассой.
struct ColonyLayout {
    SDL_Rect amount = {0, 0, 0, 0};
    SDL_Rect buy = {0, 0, 0, 0};
    SDL_Rect deposit = {0, 0, 0, 0};
    SDL_Rect withdraw = {0, 0, 0, 0};
    SDL_Rect withdrawAll = {0, 0, 0, 0};
    SDL_Rect rename = {0, 0, 0, 0};
    int columnX = 0;
};

ColonyLayout colonyLayout(const Window& window) {
    ColonyLayout layout;
    const int bw = 168;
    const int bx = window.rect.x + window.rect.w - WINDOW_PAD - bw;
    const int by = window.rect.y + TITLE_H + 46;
    layout.columnX = bx;
    layout.amount = {bx, by, bw, 28};
    layout.buy = {bx, by + 40, bw, 30};
    layout.deposit = {bx, by + 40, bw, 28};
    layout.withdraw = {bx, by + 74, bw, 28};
    layout.withdrawAll = {bx, by + 108, bw, 28};
    // Поле имени — под кассой, отдельным ярусом: это единственное действие в
    // окне, которое ничего не стоит и ничего не двигает, кроме карты игрока.
    layout.rename = {bx, by + 158, bw, 28};
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
    // Сдать трюм целиком — сразу под поштучной продажей: это её же действие,
    // только без выбора элемента. Иначе конец рейса превращается в перебор
    // таблицы по буквам ради того, чтобы вспомнить, что вообще лежит в трюме.
    layout.sellAll = {bx, layout.tableY + 72, buttonW, 28};
    layout.refuel = {bx, layout.tableY + 108, buttonW, 28};
    // Ручной перелив живёт в окне HOLD: там видны обе ёмкости и их состав.
    layout.hold = {bx, layout.tableY + 144, buttonW, 28};
    return layout;
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
    // Ширину берём у textWidth: она меряет ПЕРЕВЕДЁННУЮ строку и считает символы,
    // а не байты. На `label.length()` русская подпись наезжала на своё значение.
    x += textWidth(label, 1);
    drawText(renderer, x, y, value, vc, 1);
    x += textWidth(value, 1) + 12;
    return x;
}

// Отношение местной цены к опорной цене скопления: <1 — здесь дёшево (бери),
// >1 — здесь дорого (вези сюда). Это и есть сигнал арбитража.
double marketPressure(const Market& market, int elementIndex) {
    if (elementIndex < 0 || elementIndex >= int(market.prices.size())) {
        return market.pricePressure();
    }
    const double reference = marketReferencePrice(elementIndex);
    if (reference <= 0.0) return 1.0;
    return market.prices[elementIndex] / reference;
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
    // Бесполезное вещество (ни одной функции) — тусклая заглушка, не шум.
    if (econPrimaryFunction(elementIndex) < 0) return {30, 34, 42, 200};

    const double pressure = marketPressure(market, elementIndex);
    if (pressure > 1.15) {
        // Дорого: чем ярче красный, тем выгоднее сюда ВЕЗТИ.
        const double heat = std::min(1.0, std::log(pressure) / std::log(4.0));
        return {Uint8(96 + 150 * heat), Uint8(62 - 26 * heat), Uint8(66 - 26 * heat), 230};
    }
    if (pressure < 0.87) {
        // Дёшево: чем ярче зелёный, тем выгоднее ЗДЕСЬ брать.
        const double deal = std::min(1.0, std::log(1.0 / pressure) / std::log(5.0));
        return {Uint8(38 + 30 * deal), Uint8(96 + 150 * deal), Uint8(104 + 40 * deal), 230};
    }
    return {56, 72, 96, 218};
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
    const int routeW = textWidth(route, 1);
    const int detailW = textWidth(detail, 1);
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
    labelBar(renderer, x + 10, y + 58, w - 20, "POPULATION", starPopulationWeight(*star) / 8.0, P.green);
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
    // Экстренное торможение — это НЕ мгновенная остановка: корабль гасит
    // скорость той же тягой и на то же топливо, что и разгонялся, и по дороге
    // пролетает по инерции. Без явной строки это выглядит как «кнопка не
    // сработала»: линия маршрута пропадает сразу, а корабль ещё летит.
    const bool braking = agent.ship.enRoute && agent.ship.targetStar == -2;
    const bool adrift = !agent.ship.enRoute && !here;
    if (braking) {
        char brake[64];
        std::snprintf(brake, sizeof(brake), "BRAKING  %.3FC -> 0", speedOf(agent));
        drawText(renderer, x + 10, y + 46, brake, P.red, 1);
        drawText(renderer, x + 10, y + 59, "BURNING FUEL TO STOP", P.amber, 1);
    } else if (adrift) {
        drawText(renderer, x + 10, y + 46, "ADRIFT - NO PORT", P.amber, 1);
        drawText(renderer, x + 10, y + 59, std::string("TO   ") + (dest ? dest->name : "-"), P.text, 1);
    } else {
        drawText(renderer, x + 10, y + 46, std::string("FROM ") + (here ? here->name : "-"), P.text, 1);
        drawText(renderer, x + 10, y + 59, std::string("TO   ") + (dest ? dest->name : "-"), P.text, 1);
    }

    labelBar(renderer, x + 10, y + 75, w - 20, "CARGO", shipCargoMass(agent.ship) / std::max(1.0, agent.ship.cargoCapacity), P.amber);

    char buf1[32], buf2[32], buf3[32];
    std::snprintf(buf1, sizeof(buf1), "%.0F", shipTotalMass(agent.ship));
    std::snprintf(buf2, sizeof(buf2), "%.0F%%", shipFuelFill(agent.ship) * 100.0);
    std::snprintf(buf3, sizeof(buf3), "%.0F%%", shipPropellantFill(agent.ship) * 100.0);
    int cx = x + 10;
    cx = drawStat(renderer, cx, y + 90, "MASS ", buf1);
    cx = drawStat(renderer, cx, y + 90, "FUEL ", buf2);
    cx = drawStat(renderer, cx, y + 90, "PROP ", buf3);

    std::snprintf(buf1, sizeof(buf1), "%.0F", agent.money);
    std::snprintf(buf2, sizeof(buf2), "%.2FC", speedOf(agent));
    std::snprintf(buf3, sizeof(buf3), "%d", agent.trades);
    cx = x + 10;
    cx = drawStat(renderer, cx, y + 104, "CREDITS ", buf1, P.green);
    cx = drawStat(renderer, cx, y + 104, "SPEED ", buf2);
    cx = drawStat(renderer, cx, y + 104, "TRADES ", buf3);
}

// Панель фракций. С §24 у неё две темы вместо одной: что мы про фракцию ЗНАЕМ
// (сводки владельцев) и кто мы ДЛЯ НЕЁ (репутация возчика). Вторая строка
// каждой записи отдана репутации, потому что именно она решает, какую работу
// здесь дадут, — а карта владений без этого не подсказывала игроку ничего о
// том, куда лететь работать.
void drawFactionPanel(SDL_Renderer* renderer, const Game& game, int x, int y, int w) {
    const int rows = std::min(6, int(game.factions.size()));
    const std::vector<KnownFactionSummary>& summaries = knownFactionSummaries(game);
    const int h = 44 + rows * 30;
    panel(renderer, x, y, w, h);
    header(renderer, x + 10, y + 9, "FACTIONS");
    drawText(renderer, x + 10, y + 30, "STANDING AND KNOWN OWNER REPORTS", P.dim, 1);
    for (int i = 0; i < rows; ++i) {
        const Faction& faction = game.factions[i];
        const KnownFactionSummary known = i < int(summaries.size()) ? summaries[i] : KnownFactionSummary();
        const int rowY = y + 46 + i * 30;
        fillRect(renderer, x + 10, rowY, 9, 9, factionColor(faction));
        drawText(renderer, x + 25, rowY, faction.name, P.text, 1);
        bar(renderer, x + w - 96, rowY - 1, 28, 7, known.confidence, factionColor(faction));
        char count[40];
        std::snprintf(count, sizeof(count), "KNOWN %d", known.knownSystems);
        drawText(renderer, x + w - 62, rowY, count, known.knownSystems > 0 ? P.dim : P.red, 1);

        // Репутация: звание, шкала и «сколько заказов сдано». Число заказов
        // держим на виду — это единица шкалы, и по ней игрок сам считает, что
        // ему осталось (та же логика, что у квоты в §21).
        const double tier = game.factionJobTier(int(i));
        const double done = game.factionReputationOf(int(i));
        char rep[72];
        // «JOBS DONE» — ЦЕЛАЯ фраза словаря (§14): отдельного ключа "JOBS"
        // заводить нельзя, он уже занят кнопкой доски и переводится как
        // «ЗАКАЗЫ», а здесь нужен родительный падеж.
        std::snprintf(rep, sizeof(rep), "%s  JOBS DONE %.0F", Game::jobRankName(tier), done);
        drawText(renderer, x + 25, rowY + 10, rep, tier > 0.001 ? P.cyan : P.dim, 1);
        bar(renderer, x + 25, rowY + 21, w - 45, 4, tier, tier > 0.001 ? P.cyan : P.dim);
    }
}

// Карточка управления. Раньше это была вечная колонка из 33 строк в правом
// нижнем углу — она занимала угол экрана всю партию и всё равно читалась как
// свалка. Теперь это одна центрированная карточка, разбитая по смыслу: её
// показывает оболочка перед стартом партии и клавиша F1 в игре.
// ⚠️ Совсем убирать её нельзя: кроме неё игрок нигде не узнаёт про клавишу L
// (вход в локальный полёт) — см. master_prompt.md §10.1(D).
void drawControlsCard(SDL_Renderer* renderer, int screenW, int screenH) {
    struct Group { const char* title; const char* lines[10]; };
    static const Group groups[] = {
        { "FLIGHT", { "L    ENTER SYSTEM", "G    GO TO SELECTED", "X    STOP SHIP",
                      "F    FOLLOW SHIP", "TAB  NEXT AGENT", "ENTER OPEN SYSTEM",
                      "SPACE PAUSE", "1-4  SIM SPEED", NULL, NULL } },
        { "TRADE",  { "T    AUTO TRADE", "B    BUY", "Q    SELL", "E    BROKERAGE",
                      "O    CARGO", "I    TRANSACTION LOG", "V    ADVISOR",
                      "F2   BUY BACK LICENCE", "[ ]  CYCLE ELEMENT", NULL } },
        { "SHIP",   { "U    SHIPYARD / FIT", "M    MINE ORE", "J    REPAIR HULL",
                      "K    SCAN ANOMALY", "C    COLONY / BUY SYSTEM", "N    SWITCH SHIP",
                      "Y    EXOTICS / FORGE", "R    ROB", NULL, NULL } },
        { "VIEW",   { "LMB  SELECT", "RMB  SET ROUTE", "WHEEL ZOOM", "MMB  DRAG PAN",
                      "ARROWS PAN", "WASD ROTATE", "P    PLAYER SHIP", "0    RESET VIEW",
                      NULL, NULL } },
        { "GAME",   { "F5   SAVE", "F9   LOAD", "F1   THIS CARD", NULL, NULL,
                      NULL, NULL, NULL, NULL, NULL } }
    };
    const int groupCount = int(sizeof(groups) / sizeof(groups[0]));

    const int cols = screenW >= 1000 ? 3 : 2;
    const int rows = (groupCount + cols - 1) / cols;
    const int colW = 232;
    const int groupH = 9 * 13 + 26;
    const int w = std::min(screenW - 24, cols * colW + 28);
    const int h = std::min(screenH - 24, rows * groupH + 54);
    const int x = (screenW - w) / 2;
    const int y = (screenH - h) / 2;

    fillRect(renderer, 0, 0, screenW, screenH, {3, 5, 14, 205});
    panel(renderer, x, y, w, h);
    header(renderer, x + 14, y + 12, "CONTROLS");
    drawText(renderer, x + w - 108, y + 14, "F1 / ESC CLOSE", P.dim, 1);

    for (int g = 0; g < groupCount; ++g) {
        const int cx = x + 16 + (g % cols) * colW;
        const int cy = y + 42 + (g / cols) * groupH;
        drawText(renderer, cx, cy, groups[g].title, P.cyan, 1);
        for (int i = 0; i < 10 && groups[g].lines[i]; ++i) {
            // Первая строка FLIGHT — вход в локальный полёт. Единственное место,
            // где игрок вообще может о нём узнать, поэтому она подсвечена.
            const SDL_Color c = (g == 0 && i == 0) ? P.amber : P.dim;
            drawText(renderer, cx, cy + 15 + i * 13, groups[g].lines[i], c, 1);
        }
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
    drawText(renderer, rect.x + (rect.w - textWidth(label, 1)) / 2, rect.y + (rect.h - 7) / 2, label, text, 1);
}

void drawLiveColonySummary(SDL_Renderer* renderer, const Game& game, int starIndex, int x, int& y) {
    const Colony* colony = colonyAtStar(game, starIndex);
    if (!colony) return;

    char line[128];
    const bool owned = game.playerOwnsStar(starIndex);
    std::snprintf(line, sizeof(line), "COLONY VAULT %.0F INCOME %.0F/Y%s",
        colony->localLedger,
        game.colonyIncomeAt(starIndex),
        owned ? " - YOURS" : "");
    drawText(renderer, x, y, line, owned ? P.green : P.dim, 1);
    y += 15;

    if (colonyQueueCount(*colony) > 0) {
        const ConstructionItem& item = colony->constructionQueue.front();
        const double progress = colonyQueueProgress(*colony) * 100.0;
        std::snprintf(line, sizeof(line), "QUEUE %s %.0F%% COST %.0F",
            colonyQueueLabel(*colony).c_str(), progress, item.cost);
        drawText(renderer, x, y, line, P.amber, 1);
        y += 15;
    } else {
        drawText(renderer, x, y, "QUEUE EMPTY / C COLONY", P.dim, 1);
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
    // Крупный заказ серый не «просто так», а потому что не хватает ФЛОТА, и
    // цифру нехватки надо назвать (§24). Без неё игрок видел бы недоступную
    // строку и не знал, чего именно докупить.
    const double needMass = game.contractCargoMass(contract);
    const double fleetMass = game.playerFleetCapacityAt(contract.originStar);
    char fleetNote[64] = "";
    if (needMass > 0.0 && needMass > fleetMass + 0.001) {
        std::snprintf(fleetNote, sizeof(fleetNote), " FLEET %.0F/%.0F T", fleetMass, needMass);
    } else if (needMass > 0.0 && !cargoFits) {
        std::snprintf(fleetNote, sizeof(fleetNote), " HOLD/HEAVY");
    }

    if (fuelShort) {
        std::snprintf(line, sizeof(line), "%s %.0FY  FUEL %.0F SHORT %.0F  RISK %.0F%%%s",
            remoteOffer ? "BOARD" : "ETA", years, fuelNeeded, shortfall, risk * 100.0, fleetNote);
    } else {
        std::snprintf(line, sizeof(line), "%s %.0FY  FUEL %.0F OK  RISK %.0F%%%s",
            remoteOffer ? "BOARD" : "ETA", years, fuelNeeded, risk * 100.0, fleetNote);
    }
    if (textWidth(line, 1) > maxW) {
        // Короткая форма. Нехватка флота остаётся и здесь: она важнее топлива,
        // потому что без неё кнопка серая и причина непонятна.
        if (fuelShort) {
            std::snprintf(line, sizeof(line), "%.0FY F%.0F S%.0F R%.0F%%%s",
                years, fuelNeeded, shortfall, risk * 100.0, fleetNote);
        } else {
            std::snprintf(line, sizeof(line), "%.0FY F%.0F OK R%.0F%%%s",
                years, fuelNeeded, risk * 100.0, fleetNote);
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
void drawColonyWindow(SDL_Renderer* renderer, const Game& game, const Window& window, const WindowState& state, bool active);
bool handleExchangeWindowMouseDown(WindowState& state, Game& game, const Window& window, int mouseX, int mouseY);
bool handleColonyWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int mouseX, int mouseY, int button);
void drawExoticsWindow(SDL_Renderer* renderer, const Game& game, const Window& window, const WindowState& state, bool active);
bool handleExoticsWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int mouseX, int mouseY, int button);

bool handleSystemWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int screenW, int screenH, int mouseX, int mouseY);
bool handleTradeWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int mouseX, int mouseY, int button, int screenW, int screenH);
bool handleContractsWindowMouseDown(Game& game, const Window& window, const HudSelection& selection, int mouseX, int mouseY);
bool handleShipyardWindowMouseDown(WindowState& state, Game& game, const Window& window, int mouseX, int mouseY, int button);


bool handleSystemWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int screenW, int screenH, int mouseX, int mouseY) {
    const SystemLayout layout = systemLayout(window);
    if (contains(layout.route, mouseX, mouseY)) {
        // Кнопка НАЗНАЧАЕТ цель, но не стартует. Вылет — отдельным GO на карте,
        // чтобы между выбором системы и стартом настроить под неё двигатель
        // (кнопка OPTIMAL в окне HOLD считает режим именно под назначенную цель).
        // Отказ объясняется через game.lastEvent, который HUD показывает строкой.
        if (window.star >= 0 && game.setAgentDestination(game.playerAgent, window.star)) {
            selection.star = window.star;
            selection.agent = game.playerAgent;
        }
        return true;
    }
    if (contains(layout.trade, mouseX, mouseY)) {
        if (playerMarketStar(game) == window.star) openTradeWindow(state, window.star, screenW, screenH);
        return true;
    }
    if (contains(layout.contracts, mouseX, mouseY)) {
        if (game.playerCanOpenContractsAt(window.star)) openContractsWindow(state, window.star, screenW, screenH);
        return true;
    }
    if (contains(layout.colony, mouseX, mouseY)) {
        openColonyWindow(state, window.star, screenW, screenH);
        selection.star = window.star;
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
    if (contains(layout.exchange, mouseX, mouseY)) {
        openExchangeWindow(state, window.star, screenW, screenH);
        return true;
    }
    if (contains(layout.exotics, mouseX, mouseY)) {
        if (game.exoticMarketAt(window.star)) openExoticsWindow(state, window.star, screenW, screenH);
        return true;
    }
    if (contains(layout.shipyard, mouseX, mouseY)) {
        if (game.playerAgent >= 0) openShipyardWindow(state, window.star, screenW, screenH);
        return true;
    }
    return true;
}

SDL_Rect contractButtonRect(const Window& window, int row) {
    return {window.rect.x + window.rect.w - 96,
            window.rect.y + TITLE_H + CONTRACT_HEADER_H + 55 + row * CONTRACT_ROW_H, 82, 22};
}

int contractMaxRows(const Window& window) {
    return std::max(1, (window.rect.h - TITLE_H - CONTRACT_HEADER_H - 64) / CONTRACT_ROW_H);
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

bool handleShipFitWindowMouseDown(WindowState& state, Game& game, const Window& window, int mouseX, int mouseY) {
    if (game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) return true;
    Agent& agent = game.agents[game.playerAgent];
    
    int listY = window.rect.y + TITLE_H + 24;
    int y = listY;
    
    // Up / Down scroll buttons could go here, omitting for simplicity since window is large enough
    
    for (size_t i = 0; i < agent.ship.modules.size(); ++i) {
        SDL_Rect btn = {window.rect.x + window.rect.w - 90, y - 4, 70, 20};
        if (contains(btn, mouseX, mouseY)) {
            game.unequipModule(game.playerAgent, i);
            return true;
        }
        y += 24;
    }
    
    y += 12;
    
    std::vector<int> cargoMods;
    for (size_t i = 0; i < agent.ship.cargo.size(); ++i) {
        if (agent.ship.cargo[i].element.rfind("Module: ", 0) == 0 && agent.ship.cargo[i].amount > 0.0) {
            cargoMods.push_back(i);
        }
    }
    
    for (size_t i = 0; i < cargoMods.size(); ++i) {
        SDL_Rect btn = {window.rect.x + window.rect.w - 90, y - 4, 70, 20};
        if (contains(btn, mouseX, mouseY)) {
            std::string modName = agent.ship.cargo[cargoMods[i]].element.substr(8);
            int defIdx = -1;
            const auto& defs = moduleDefs();
            for (size_t j = 0; j < defs.size(); ++j) {
                if (defs[j].name == modName) { defIdx = j; break; }
            }
            if (defIdx >= 0) {
                game.equipModule(game.playerAgent, defIdx);
            }
            return true;
        }
        y += 24;
    }
    return true;
}

namespace {

// Геометрия окна HOLD. Порядок колонок: БУНКЕР | КАРГО | БАК — трюм в центре,
// потому что перелив всегда идёт через него, и стрелки расходятся от центра
// наружу («залить») либо к центру («слить»).
const int HOLD_BUNKER = 0;
const int HOLD_CARGO = 1;
const int HOLD_TANK = 2;
const int HOLD_COL_W = 232;
const int HOLD_ROW_H = 18;
const int HOLD_ARROW_W = 18;
const int HOLD_MAX_ROWS = 16;

int holdColumnX(const Window& window, int column) {
    return window.rect.x + 12 + column * HOLD_COL_W;
}

int holdRowY(const Window& window, int row) {
    return window.rect.y + TITLE_H + 74 + row * HOLD_ROW_H;
}

// Поле «шаг перелива». Намеренно ТО ЖЕ значение, что AMOUNT окна торговли:
// одно число на все операции с веществом, отдельного состояния не заводим.
SDL_Rect holdStepRect(const Window& window) {
    SDL_Rect r;
    r.x = window.rect.x + window.rect.w - 114;
    r.y = window.rect.y + window.rect.h - 28;
    r.w = 96;
    r.h = 22;
    return r;
}

// Строка про назначенную цель — чтобы OPTIMAL не был кнопкой в никуда.
std::string destStarLabel(const Game& game) {
    if (game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) return "";
    const int dest = game.agents[game.playerAgent].destStar;
    if (dest < 0 || dest >= int(game.cluster.stars.size())) {
        return "NO DESTINATION - OPEN A SYSTEM AND PRESS DESTINATION";
    }
    return "DESTINATION: " + game.cluster.stars[dest].name;
}

// Ручка режима двигателя. Слева платим рабочим телом, справа — топливом.
SDL_Rect holdThrottleRect(const Window& window) {
    SDL_Rect r;
    r.x = window.rect.x + window.rect.w - 168;
    r.y = window.rect.y + window.rect.h - 78;
    r.w = 116;
    r.h = 12;
    return r;
}

// Шкала крейсерской скорости: доля от потолка корпуса. Лететь медленнее
// объективно дешевле — бюджет быстроты равен 2*artanh(peak).
SDL_Rect holdCruiseRect(const Window& window) {
    SDL_Rect r = holdThrottleRect(window);
    r.y += 32;
    return r;
}

// Кнопка автоподбора режима под НАЗНАЧЕННУЮ цель.
SDL_Rect holdOptimalRect(const Window& window) {
    SDL_Rect r;
    r.x = window.rect.x + window.rect.w - 210;
    r.y = window.rect.y + window.rect.h - 28;
    r.w = 88;
    r.h = 22;
    return r;
}

// Перевод кредитов на соседний свой борт и обратно. Живут в окне HOLD, потому
// что HOLD — это «что на корабле и куда оно уходит», а деньги капитана такой же
// предмет на борту, как груз: у фракции касса общая, у корабля — своя.
// Сумму берут из того же поля шага, что и переливы вещества, — одно понятие на
// все операции окна.
// Ряд из ТРЁХ кнопок в тех же 196 пикселях: отдать, взять, автопилот. Кнопка
// автопилота сначала стояла строкой выше и ложилась поверх строки двигателя
// «ЗАЛИТЬ РАБ. ТЕЛО» — по-русски та длиннее и уходила под неё целиком.
SDL_Rect holdGiveRect(const Window& window) {
    SDL_Rect r = holdOptimalRect(window);
    r.x -= 196;
    r.w = 60;
    return r;
}

SDL_Rect holdTakeRect(const Window& window) {
    SDL_Rect r = holdGiveRect(window);
    r.x += 64;
    return r;
}

// (§35) Тумблер автопилота соседнего борта. Место выбрано не случайно: он
// действует на `playerOtherShipHere()`, а тот по определению стоит рядом, —
// значит физически невозможно поставить под автопилот борт, который сейчас в
// полёте, и незачем отдельно это проверять в интерфейсе.
SDL_Rect holdAutoRect(const Window& window) {
    SDL_Rect r = holdGiveRect(window);
    r.x += 128;
    return r;
}

// Кнопка «за борт»: единственный выход, если перегрузился вдали от рынка.
SDL_Rect holdJettisonRect(const Window& window, int row) {
    SDL_Rect r;
    r.x = holdColumnX(window, HOLD_CARGO) + HOLD_COL_W - 32 - HOLD_ARROW_W - 22;
    r.y = holdRowY(window, row) - 2;
    r.w = 20;
    r.h = HOLD_ROW_H - 3;
    return r;
}

// Кнопка-стрелка строки. left=true — стрелка у левого края колонки.
SDL_Rect holdArrowRect(const Window& window, int column, int row, bool left) {
    SDL_Rect r;
    r.x = left ? holdColumnX(window, column)
               : holdColumnX(window, column) + HOLD_COL_W - 32 - HOLD_ARROW_W;
    r.y = holdRowY(window, row) - 2;
    r.w = HOLD_ARROW_W;
    r.h = HOLD_ROW_H - 3;
    return r;
}

}

// Клик по стрелке в окне HOLD переливает вещество между ёмкостями.
// Колонка CARGO: строка уезжает в бункер (LMB) или в бак (RMB).
// Колонки BUNKER/TANK: строка сливается обратно в трюм.
bool handleCargoWindowMouseDown(WindowState& state, Game& game, const Window& window,
                                int mouseX, int mouseY, int button, double amount) {
    (void)button;
    if (contains(holdStepRect(window), mouseX, mouseY)) {
        state.tradeAmountEditing = true;
        SDL_StartTextInput();
        return true;
    }
    if (game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) return true;

    if (contains(holdOptimalRect(window), mouseX, mouseY)) {
        const int dest = game.agents[game.playerAgent].destStar;
        if (dest >= 0) game.agentOptimiseForTarget(game.playerAgent, dest);
        else game.lastEvent = "no destination set: open a system and press DESTINATION";
        return true;
    }
    // Деньги между своими бортами: та же сумма из поля шага, что и переливы.
    if (contains(holdGiveRect(window), mouseX, mouseY)) {
        game.playerTransferCredits(amount, true);
        return true;
    }
    if (contains(holdTakeRect(window), mouseX, mouseY)) {
        game.playerTransferCredits(amount, false);
        return true;
    }
    if (contains(holdAutoRect(window), mouseX, mouseY)) {
        const int mate = game.playerOtherShipHere();
        if (mate >= 0) game.playerSetAutoTrade(mate, !game.agents[mate].autoTrade);
        return true;
    }
    {
        SDL_Rect cru = holdCruiseRect(window);
        cru.y -= 6;
        cru.h += 12;
        if (contains(cru, mouseX, mouseY)) {
            const double f = 0.2 + 0.8 * double(mouseX - cru.x) / double(std::max(1, cru.w - 6));
            game.agentSetCruiseFraction(game.playerAgent, f);
            return true;
        }
    }

    // Ручка режима: щелчок по шкале ставит значение по позиции курсора.
    // Зона захвата чуть выше и ниже полосы, иначе в неё трудно попасть.
    {
        SDL_Rect thr = holdThrottleRect(window);
        thr.y -= 6;
        thr.h += 12;
        if (contains(thr, mouseX, mouseY)) {
            const double t = double(mouseX - thr.x) / double(std::max(1, thr.w - 6));
            game.agentSetThrottle(game.playerAgent, t);
            return true;
        }
    }
    const Ship& ship = game.agents[game.playerAgent].ship;

    const std::vector<Resource>* columns[3];
    columns[HOLD_BUNKER] = &ship.fuel;
    columns[HOLD_CARGO] = &ship.cargo;
    columns[HOLD_TANK] = &ship.propellant;

    for (int column = 0; column < 3; ++column) {
        int row = 0;
        for (size_t i = 0; i < columns[column]->size(); ++i) {
            const Resource& item = (*columns[column])[i];
            if (item.amount <= 0.0) continue;
            if (row >= HOLD_MAX_ROWS) break;

            const int element = elementIndex(item.element);
            // Пустое поле AMOUNT означает «всё, что есть в этой строке».
            const double units = amount > 0.0 ? amount : item.amount;
            const bool leftHit = contains(holdArrowRect(window, column, row, true), mouseX, mouseY);
            const bool rightHit = contains(holdArrowRect(window, column, row, false), mouseX, mouseY);
            if (column == HOLD_CARGO && element >= 0 &&
                contains(holdJettisonRect(window, row), mouseX, mouseY)) {
                game.agentJettisonCargo(game.playerAgent, element, units);
                return true;
            }

            if (element >= 0 && (leftHit || rightHit)) {
                if (column == HOLD_CARGO) {
                    if (leftHit) game.agentLoadFuelFromCargo(game.playerAgent, element, units);
                    else game.agentLoadPropellantFromCargo(game.playerAgent, element, units);
                } else if (column == HOLD_BUNKER) {
                    if (rightHit) game.agentDrainFuelToCargo(game.playerAgent, element, units);
                } else {
                    if (leftHit) game.agentDrainPropellantToCargo(game.playerAgent, element, units);
                }
                return true;
            }
            ++row;
        }
    }
    return true;
}

bool handleTradeWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int mouseX, int mouseY, int button, int screenW, int screenH) {
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
    if (contains(layout.sellAll, mouseX, mouseY)) {
        if (liveMarket && game.agentSellAllCargo(game.playerAgent) > 0) {
            selection.agent = game.playerAgent;
        }
        return true;
    }
    if (contains(layout.refuel, mouseX, mouseY)) {
        if (liveMarket && game.agentBuyFuel(game.playerAgent)) {
            selection.agent = game.playerAgent;
        }
        return true;
    }
    if (contains(layout.hold, mouseX, mouseY)) {
        openCargoWindow(state, window.star, screenW, screenH);
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

bool handleColonyWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int mouseX, int mouseY, int button) {
    const ColonyLayout layout = colonyLayout(window);
    // Всё в этом окне требует физического присутствия: и сделка на миллиард, и
    // касса. Империю приходится облетать — она маршрут, а не строка в меню.
    const bool onSite = game.playerAtStar(window.star);
    const bool owned = game.playerOwnsStar(window.star);
    const double amount = tradeRequestedAmount(state);

    if (button == SDL_BUTTON_LEFT && contains(layout.amount, mouseX, mouseY)) {
        state.tradeAmountEditing = true;
        state.renameStar = -1;
        SDL_StartTextInput();
        return true;
    }
    if (state.tradeAmountEditing) {
        state.tradeAmountEditing = false;
        SDL_StopTextInput();
    }
    // Имя правится только у своей системы и только на месте — как и всё
    // остальное в этом окне.
    if (button == SDL_BUTTON_LEFT && owned && onSite && contains(layout.rename, mouseX, mouseY)) {
        const ClusterStar* star = starAt(game, window.star);
        state.renameStar = window.star;
        state.renameText = star ? starNameStem(star->name) : std::string();
        SDL_StartTextInput();
        return true;
    }
    // Клик мимо поля — тихая отмена: набранное имя не применяется само.
    if (state.renameStar >= 0) {
        state.renameStar = -1;
        SDL_StopTextInput();
    }

    if (!owned) {
        if (contains(layout.buy, mouseX, mouseY) && onSite && game.playerBuySystem()) {
            selection.star = window.star;
            selection.agent = game.playerAgent;
        }
        return true;
    }
    if (contains(layout.deposit, mouseX, mouseY) && onSite) {
        if (game.playerColonyDeposit(amount) > 0.0) selection.agent = game.playerAgent;
        return true;
    }
    if (contains(layout.withdraw, mouseX, mouseY) && onSite) {
        if (game.playerColonyWithdraw(amount) > 0.0) selection.agent = game.playerAgent;
        return true;
    }
    if (contains(layout.withdrawAll, mouseX, mouseY) && onSite) {
        if (game.playerColonyWithdraw(0.0) > 0.0) selection.agent = game.playerAgent;
        return true;
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

    // Клик получает только верхнее окно под курсором — то самое, которое мы
    // сейчас подняли на передний план. Раньше цикл шёл с нижнего окна и отдавал
    // клик первому попавшемуся: каскадом перекрытые окна съедали чужие GO/TRADE.
    // Копия окна нужна потому, что обработчик может закрыть окно и сдвинуть вектор.
    const Window w = *active;
    switch (w.kind) {
        case WindowKind::SystemInfo:
            handleSystemWindowMouseDown(state, game, w, selection, screenW, screenH, mouseX, mouseY);
            break;
        case WindowKind::Trade:
            handleTradeWindowMouseDown(state, game, w, selection, mouseX, mouseY, button, screenW, screenH);
            break;
        case WindowKind::Contracts:
            handleContractsWindowMouseDown(game, w, selection, mouseX, mouseY);
            break;
        case WindowKind::Shipyard:
            handleShipyardWindowMouseDown(state, game, w, mouseX, mouseY, button);
            break;
        case WindowKind::Cargo:
            handleCargoWindowMouseDown(state, game, w, mouseX, mouseY, button, tradeRequestedAmount(state));
            break;
        case WindowKind::ShipFit:
            handleShipFitWindowMouseDown(state, game, w, mouseX, mouseY);
            break;
        case WindowKind::Exchange:
            handleExchangeWindowMouseDown(state, game, w, mouseX, mouseY);
            break;
        case WindowKind::Colony:
            handleColonyWindowMouseDown(state, game, w, selection, mouseX, mouseY, button);
            break;
        case WindowKind::Exotics:
            handleExoticsWindowMouseDown(state, game, w, selection, mouseX, mouseY, button);
            break;
        case WindowKind::Transactions:
            break;
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

namespace {

// Длина строки в БУКВАХ, а не в байтах: имя можно набрать кириллицей, где
// буква — два байта, и лимит в байтах урезал бы её вдвое.
size_t glyphCount(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    }
    return n;
}

// Стереть последнюю букву целиком — иначе backspace рвёт кириллицу пополам и
// оставляет в имени битый байт, который дойдёт до сейва.
void popGlyph(std::string& s) {
    while (!s.empty()) {
        const unsigned char c = static_cast<unsigned char>(s[s.size() - 1]);
        s.erase(s.size() - 1);
        if ((c & 0xC0) != 0x80) break;
    }
}

const size_t NAME_MAX_GLYPHS = 12;

}  // namespace

void handleTextInput(WindowState& state, const char* text) {
    if (!text) return;
    if (state.renameStar >= 0) {
        for (const char* p = text; *p; ++p) {
            const unsigned char ch = static_cast<unsigned char>(*p);
            // Пробелы и управляющие — мимо: имя системы одно слово. Всё
            // печатное, включая кириллицу, пропускаем как есть.
            if (ch <= ' ') continue;
            if (glyphCount(state.renameText) >= NAME_MAX_GLYPHS && (ch & 0xC0) != 0x80) continue;
            state.renameText.push_back(char(ch));
        }
        return;
    }
    if (!state.tradeAmountEditing) return;
    for (const char* p = text; *p; ++p) {
        const char ch = *p;
        if (ch >= '0' && ch <= '9') {
            if (state.tradeAmount.size() < 12) state.tradeAmount.push_back(ch);
        } else if (ch == '.' && state.tradeAmount.find('.') == std::string::npos) {
            if (state.tradeAmount.size() < 12) state.tradeAmount.push_back(ch);
        }
    }
}

bool handleKeyDown(WindowState& state, Game& game, SDL_Keycode key) {
    if (state.renameStar >= 0) {
        if (key == SDLK_BACKSPACE) { popGlyph(state.renameText); return true; }
        if (key == SDLK_DELETE) { state.renameText.clear(); return true; }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            game.playerRenameSystem(state.renameStar, state.renameText);
            state.renameStar = -1;
            SDL_StopTextInput();
            return true;
        }
        if (key == SDLK_ESCAPE) {
            state.renameStar = -1;
            SDL_StopTextInput();
            return true;
        }
        return true;
    }
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
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    bool hovered = contains(close, mx, my);
    fillRect(renderer, close.x, close.y, close.w, close.h, hovered ? SDL_Color{160, 40, 40, 255} : SDL_Color{36, 18, 24, 235});
    strokeRect(renderer, close.x, close.y, close.w, close.h, P.red);
    drawText(renderer, close.x + 5, close.y + 4, "X", hovered ? P.text : P.red, 1);
}

void openShipFitWindow(WindowState& state, int starIndex, int screenW, int screenH) {
    int cascade = 0;
    for (auto& w : state.windows) {
        if (w.kind == WindowKind::ShipFit) {
            bringWindowToFront(state, w.id);
            return;
        }
        if (w.kind == WindowKind::Cargo || w.kind == WindowKind::ShipFit) cascade++;
    }
    Window w;
    w.id = state.nextId++;
    w.kind = WindowKind::ShipFit;
    w.star = starIndex;
    w.rect = defaultWindowRect(WindowKind::ShipFit, screenW, screenH, cascade);
    state.windows.push_back(w);
    state.activeId = w.id;
}

void openShipyardWindow(WindowState& state, int starIndex, int screenW, int screenH) {
    for (auto& w : state.windows) {
        if (w.kind == WindowKind::Shipyard && w.star == starIndex) {
            bringWindowToFront(state, w.id);
            return;
        }
    }
    Window w;
    w.id = state.nextId++;
    w.kind = WindowKind::Shipyard;
    w.star = starIndex;
    int ww = 600;
    int wh = 700;
    w.rect = {(screenW - ww) / 2, std::max(20, (screenH - wh) / 2), ww, wh};
    state.windows.push_back(w);
    state.activeId = w.id;
}

void openColonyWindow(WindowState& state, int starIndex, int screenW, int screenH) {
    int cascade = 0;
    for (auto& w : state.windows) {
        if (w.kind == WindowKind::Colony && w.star == starIndex) {
            bringWindowToFront(state, w.id);
            return;
        }
        cascade++;
    }
    Window w;
    w.id = state.nextId++;
    w.kind = WindowKind::Colony;
    w.star = starIndex;
    w.rect = defaultWindowRect(WindowKind::Colony, screenW, screenH, cascade);
    state.windows.push_back(w);
    state.activeId = w.id;
}

void openTransactionsWindow(WindowState& state, int screenW, int screenH) {
    int cascade = 0;
    for (auto& w : state.windows) {
        if (w.kind == WindowKind::Transactions) {
            bringWindowToFront(state, w.id);
            return;
        }
        if (w.kind != WindowKind::Transactions) cascade++;
    }
    Window w;
    w.id = state.nextId++;
    w.kind = WindowKind::Transactions;
    w.star = -1;
    w.rect = defaultWindowRect(WindowKind::SystemInfo, screenW, screenH, cascade);
    state.windows.push_back(w);
    state.activeId = w.id;
}

// Раскладка биржи: сводка занимает верх, панель лицензий — низ.
struct ExchangeLayout {
    SDL_Rect board = {0, 0, 0, 0};      // список сделок
    SDL_Rect buyLicence = {0, 0, 0, 0};
    SDL_Rect settleQuota = {0, 0, 0, 0};
    SDL_Rect scrollUp = {0, 0, 0, 0};
    SDL_Rect scrollDown = {0, 0, 0, 0};
    SDL_Rect elementsBtn = {0, 0, 0, 0};
    // Биржа держав (§33): тумблер режима и по паре кнопок на строку.
    SDL_Rect sharesBtn = {0, 0, 0, 0};
    SDL_Rect shareBuy[8];
    SDL_Rect shareSell[8];
    // Счёт фракции: деньги, которые ходят СВЕТОМ, а не с кораблём.
    SDL_Rect accountIn = {0, 0, 0, 0};
    SDL_Rect accountOut = {0, 0, 0, 0};
    int rowH = 14;
    int rows = 0;
    // Сетка таблицы Менделеева (режим выбора элемента)
    int tableX = 0, tableY = 0, cellW = 0, cellH = 0;
};

ExchangeLayout exchangeLayout(const Window& window) {
    ExchangeLayout layout;
    const int x = window.rect.x + WINDOW_PAD;
    const int w = window.rect.w - WINDOW_PAD * 2;
    // Шапка на строку выше: под статусом лицензии живёт ещё и счёт фракции.
    const int top = window.rect.y + TITLE_H + 70;
    const int bottom = window.rect.y + window.rect.h - 40;
    layout.board = {x, top, w, std::max(0, bottom - top - 8)};
    layout.rows = std::max(0, layout.board.h / layout.rowH - 1);
    const int by = window.rect.y + window.rect.h - 32;
    layout.buyLicence  = {x, by, 170, 24};
    layout.settleQuota = {x + 178, by, 190, 24};
    layout.scrollUp    = {window.rect.x + window.rect.w - 46, top - 18, 36, 16};
    layout.scrollDown  = {window.rect.x + window.rect.w - 46, by, 36, 24};
    layout.elementsBtn = {x + 376, by, 150, 24};
    layout.sharesBtn = {x + 534, by, 130, 24};
    for (int i = 0; i < 8; ++i) {
        const int ry = layout.board.y + 18 + i * 22;
        layout.shareBuy[i]  = {window.rect.x + window.rect.w - 190, ry, 82, 19};
        layout.shareSell[i] = {window.rect.x + window.rect.w - 100, ry, 82, 19};
    }
    // Взнос и снятие — над нижним рядом, чтобы не тесниться с лицензиями.
    layout.accountIn  = {x, by - 28, 170, 24};
    layout.accountOut = {x + 178, by - 28, 190, 24};
    // Таблица Менделеева: 18 столбцов, вписываем в ширину доски.
    layout.cellW = std::max(18, std::min(34, (layout.board.w - 20) / 18));
    layout.cellH = std::max(16, layout.cellW - 6);
    layout.tableX = x;
    layout.tableY = top + 4;
    return layout;
}

void openExchangeWindow(WindowState& state, int starIndex, int screenW, int screenH) {
    int cascade = 0;
    for (auto& w : state.windows) {
        if (w.kind == WindowKind::Exchange) {
            w.star = starIndex;                     // биржа следует за системой игрока
            bringWindowToFront(state, w.id);
            return;
        }
        cascade++;
    }
    Window w;
    w.id = state.nextId++;
    w.kind = WindowKind::Exchange;
    w.star = starIndex;
    w.rect = defaultWindowRect(WindowKind::Exchange, screenW, screenH, cascade);
    state.windows.push_back(w);
    state.activeId = w.id;
}

// Сводка пересчитывается не каждый кадр: при полной разведке это ~40 мс. Условия
// пересчёта — сменилась система стоянки или прошло ощутимое время (цены дрейфуют,
// а повторное посещение системы обновляет её запись в базе знаний, и сводка обязана
// это подхватить). Два года игрового времени = две секунды на скорости x1.
void updateExchangeBoard(WindowState& state, const Game& game) {
    const int docked = playerMarketStar(game);
    // Хайтек-этаж следует за кораблём: покупает он по системе СТОЯНКИ, а
    // показывать обязан её же. Иначе игрок, глядя на цену дальнего рынка,
    // покупал бы по цене своего. Стоит ВЫШЕ раннего выхода: окно экзотики
    // живёт само по себе и от биржи не зависит.
    for (size_t i = 0; i < state.windows.size(); ++i) {
        Window& w = state.windows[i];
        if (w.kind == WindowKind::Exotics && docked >= 0 && game.exoticMarketAt(docked)) w.star = docked;
    }

    Window* exchange = nullptr;
    for (Window& w : state.windows) {
        if (w.kind == WindowKind::Exchange) { exchange = &w; break; }
    }
    if (!exchange) return;                      // окно закрыто — не тратим время

    // Биржа СЛЕДУЕТ за игроком. Раньше `star` фиксировался в момент открытия, и
    // после перелёта окно продолжало показывать прежнюю систему: котировки покупки
    // считались чужими, список пропадал, а снимок текущей системы (который игра
    // обновляет каждый тик, пока корабль стоит) в сводку не попадал.
    if (docked >= 0 && exchange->star != docked) {
        exchange->star = docked;
        exchange->scrollOffset = 0;
    }

    const int star = exchange->star;
    const double REFRESH_YEARS = 2.0;
    const bool sameQuery = star == state.exchangeStar &&
                           state.exchangeElement == state.exchangeBoardElement;
    if (sameQuery && game.time - state.exchangeBuiltAt < REFRESH_YEARS) return;
    state.exchangeBoard = game.playerArbitrageBoard(star, 0, state.exchangeElement);
    state.exchangeStar = star;
    state.exchangeBoardElement = state.exchangeElement;
    state.exchangeBuiltAt = game.time;
}

bool handleMouseWheel(WindowState& state, int mouseX, int mouseY, int dy) {
    // Колесо достаётся ВЕРХНЕМУ окну под курсором; если такого нет — вызывающая
    // сторона зумит карту как раньше.
    for (int i = int(state.windows.size()) - 1; i >= 0; --i) {
        Window& w = state.windows[size_t(i)];
        if (!contains(w.rect, mouseX, mouseY)) continue;
        if (w.kind == WindowKind::Exchange || w.kind == WindowKind::Shipyard ||
            w.kind == WindowKind::Transactions) {
            w.scrollOffset = std::max(0, w.scrollOffset - dy * 3);
            return true;
        }
        return true;                            // окно поймало колесо, но не листается
    }
    return false;
}

bool handleExchangeWindowMouseDown(WindowState& state, Game& game, const Window& window, int mouseX, int mouseY) {
    const ExchangeLayout layout = exchangeLayout(window);
    Window* w = nullptr;
    for (auto& win : state.windows) if (win.id == window.id) { w = &win; break; }
    if (w) {
        const int total = int(state.exchangeBoard.size());
        if (contains(layout.scrollUp, mouseX, mouseY)) {
            w->scrollOffset = std::max(0, w->scrollOffset - layout.rows);
            return true;
        }
        if (contains(layout.scrollDown, mouseX, mouseY)) {
            w->scrollOffset = std::min(std::max(0, total - layout.rows), w->scrollOffset + layout.rows);
            return true;
        }
    }
    if (contains(layout.sharesBtn, mouseX, mouseY)) {
        state.exchangeShares = !state.exchangeShares;
        state.exchangeTable = false;
        if (w) w->scrollOffset = 0;
        return true;
    }
    if (state.exchangeShares) {
        const double want = tradeRequestedAmount(state);
        for (int i = 0; i < 8 && i < int(game.factions.size()); ++i) {
            if (contains(layout.shareBuy[i], mouseX, mouseY)) {
                game.playerBuyShares(i, want);
                return true;
            }
            if (contains(layout.shareSell[i], mouseX, mouseY)) {
                game.playerSellShares(i, want);
                return true;
            }
        }
        // Ниже в режиме акций НЕ рисуется ничего: ни лицензии, ни счёт, ни
        // фильтр по элементам — `drawExchangeWindow` выходит раньше. Значит и
        // кликов там быть не может: без этого `return` клик по пустому месту
        // молча покупал лицензию или переводил деньги на счёт.
        return true;
    }
    if (contains(layout.elementsBtn, mouseX, mouseY)) {
        // Один тумблер: показать таблицу для выбора элемента, а если фильтр уже
        // стоит — снять его и вернуться ко всему списку.
        if (state.exchangeElement >= 0) {
            state.exchangeElement = -1;
            state.exchangeTable = false;
        } else {
            state.exchangeTable = !state.exchangeTable;
        }
        if (w) w->scrollOffset = 0;
        return true;
    }
    if (state.exchangeTable) {
        const std::vector<ElementDefinition>& elements = elementDefinitions();
        for (size_t i = 0; i < elements.size(); ++i) {
            int col = 0, row = 0;
            if (!periodicCell(elements[i].atomicNumber, col, row)) continue;
            const SDL_Rect cell = {layout.tableX + col * layout.cellW, layout.tableY + row * layout.cellH,
                                   layout.cellW - 2, layout.cellH - 2};
            if (contains(cell, mouseX, mouseY)) {
                state.exchangeElement = int(i);
                state.exchangeTable = false;
                if (w) w->scrollOffset = 0;
                return true;
            }
        }
        return true;                       // в режиме таблицы прочие клики глушим
    }
    if (contains(layout.buyLicence, mouseX, mouseY)) {
        game.playerBuyLicence();
        return true;
    }
    if (contains(layout.settleQuota, mouseX, mouseY)) {
        game.playerSettleQuota();
        return true;
    }
    // Счёт фракции: сумма — из того же поля, что и всё остальное в окнах.
    if (contains(layout.accountIn, mouseX, mouseY)) {
        game.playerAccountDeposit(tradeRequestedAmount(state));
        return true;
    }
    if (contains(layout.accountOut, mouseX, mouseY)) {
        game.playerAccountWithdraw(tradeRequestedAmount(state));
        return true;
    }
    return false;
}

std::string creditsLabel(double value);   // определена ниже, у окна колонии

void drawExchangeWindow(SDL_Renderer* renderer, const Game& game, const Window& window,
                        const WindowState& state, bool active) {
    const ClusterStar* star = starAt(game, window.star);
    drawWindowFrame(renderer, window, star ? ("BROKERAGE / " + star->name) : "BROKERAGE", active);

    const int x = window.rect.x + WINDOW_PAD;
    int y = window.rect.y + TITLE_H + 10;
    const ExchangeLayout layout = exchangeLayout(window);

    // --- Шапка: состояние лицензии. Это и есть причина, по которой сюда заходят.
    const double target = game.licenceQuotaTarget();
    const double remaining = std::max(0.0, target - game.licenceQuotaPaid);
    const double yearsLeft = std::max(0.0, game.licencePeriodEnd - game.time);
    char head[160];
    if (game.licenceRevoked) {
        std::snprintf(head, sizeof(head), "LICENCE REVOKED - BUY BACK %d CR (F2)",
                      int(std::ceil(game.licenceBuyback)));
        drawText(renderer, x, y, head, P.red, 1);
    } else {
        std::snprintf(head, sizeof(head), "QUOTA %d/%d CR   %dY LEFT   TARIFF %.0F%%",
                      int(game.licenceQuotaPaid), int(target), int(std::ceil(yearsLeft)),
                      game.licenceTariffRate * 100.0);
        drawText(renderer, x, y, head, remaining <= 0.0 ? P.green : P.amber, 1);
    }
    y += 14;
    std::snprintf(head, sizeof(head), "LICENCES %d   HULLS %d   FREE %d",
                  game.licenceCount, game.playerShipCount(), game.playerFreeLicences());
    drawText(renderer, x, y, head, game.playerFreeLicences() > 0 ? P.green : P.dim, 1);
    y += 14;

    // --- Счёт фракции. Деньги на нём ходят СВЕТОМ: внёс в одном конце
    // скопления — потратить в другом можно только когда туда дошло известие.
    // Поэтому цифры две, и вторая объясняет, почему первая меньше.
    const double cleared = game.factionClearedTreasury(game.playerFaction);
    const double inFlight = game.factionCreditsInFlight(game.playerFaction);
    if (inFlight > 0.0) {
        std::snprintf(head, sizeof(head), "ACCOUNT %s CR CLEARED   %s CR IN FLIGHT",
                      creditsLabel(cleared).c_str(), creditsLabel(inFlight).c_str());
        drawText(renderer, x, y, head, P.amber, 1);
    } else {
        std::snprintf(head, sizeof(head), "ACCOUNT %s CR CLEARED", creditsLabel(cleared).c_str());
        drawText(renderer, x, y, head, P.green, 1);
    }
    y += 14;

    const int dockedStar = playerMarketStar(game);
    const bool live = dockedStar >= 0 && dockedStar == window.star;
    if (!live) {
        drawText(renderer, x, y, "DOCK IN THIS SYSTEM FOR LIVE QUOTES", P.red, 1);
    } else {
        char sub[192];
        if (state.exchangeElement >= 0) {
            // Система, где мы стоим, в списке отсутствует по построению (сделке нужны
            // ДВА разных рынка), поэтому её цену показываем здесь — это точка отсчёта,
            // относительно которой читается вся колонка MODEL.
            const int el = state.exchangeElement;
            double here = 0.0;
            if (window.star >= 0 && window.star < int(game.markets.size()) &&
                el < int(game.markets[window.star].prices.size())) {
                here = game.markets[window.star].prices[el];
            }
            std::snprintf(sub, sizeof(sub), "%s - HERE %.2F CR - EVERY ONE OF %d SURVEYED MARKETS BELOW",
                          elementDefinitions()[el].symbol, here, game.playerSurveyedMarketCount());
        } else {
            std::snprintf(sub, sizeof(sub), "BEST DEALS ACROSS %d SURVEYED MARKETS - NOT A LIVE FEED",
                          game.playerSurveyedMarketCount());
        }
        drawText(renderer, x, y, sub, P.dim, 1);
    }

    // --- Режим акций держав (§33): вместо сделок — список держав.
    const double cash = (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size()))
                            ? game.agents[size_t(game.playerAgent)].money : 0.0;
    if (state.exchangeShares) {
        int by = layout.board.y;
        drawText(renderer, x, by, "POWER          PER SHARE  PER YEAR     HELD     VALUE    PROFIT", P.cyan, 1);
        by += 18;
        char row[220];
        for (size_t f = 0; f < game.factions.size() && f < 8; ++f) {
            const double price = game.factionSharePrice(int(f));
            const double div = game.factionShareDividend(int(f));
            const double held = f < game.playerShares.size() ? game.playerShares[f] : 0.0;
            const double value = held * price;
            const double basis = f < game.shareCostBasis.size() ? game.shareCostBasis[f] : 0.0;
            const double pl = value - basis;
            const double age = game.factionBookAge(int(f));
            if (int(f) == game.playerFaction) {
                std::snprintf(row, sizeof(row), "%-16s YOUR OWN FREEHOLD",
                              game.factions[f].name.substr(0, 16).c_str());
                drawText(renderer, x, by, row, P.dim, 1);
            } else if (price <= 0.0) {
                std::snprintf(row, sizeof(row), "%-16s NO REPORT PUBLISHED YET",
                              game.factions[f].name.substr(0, 16).c_str());
                drawText(renderer, x, by, row, P.dim, 1);
            } else {
                std::snprintf(row, sizeof(row), "%-14s %9s %9s %8.0F %9s %9s",
                              game.factions[f].name.substr(0, 14).c_str(),
                              creditsLabel(price).c_str(), creditsLabel(div).c_str(), held,
                              creditsLabel(value).c_str(),
                              (pl >= 0.0 ? "+" + creditsLabel(pl) : "-" + creditsLabel(-pl)).c_str());
                drawText(renderer, x, by, row, held > 0.0 ? (pl >= 0.0 ? P.green : P.red) : P.text, 1);
            }
            // Возраст отчёта — это и есть вся игра в акции: котировка отстаёт от
            // жизни, и тот, кто там ЛЕТАЛ, знает больше биржи.
            if (age >= 0.0) {
                std::snprintf(row, sizeof(row), "REPORT %.0FY", age);
                drawText(renderer, x + 12, by + 10, row, age > 4.0 ? P.amber : P.dim, 1);
            }
            const bool canBuy = price > 0.0 && cash > price && int(f) != game.playerFaction;
            drawButton(renderer, layout.shareBuy[f], "BUY", P.green, canBuy);
            drawButton(renderer, layout.shareSell[f], "SELL", P.amber, held > 0.0);
            by += 22;
        }
        by += 6;
        std::snprintf(row, sizeof(row), "PORTFOLIO %s CR", creditsLabel(game.playerShareValue()).c_str());
        drawText(renderer, x, by, row, P.dim, 1);
        drawText(renderer, x, by + 13, "DIVIDENDS ARRIVE ON THE ACCOUNT AT THE SPEED OF LIGHT", P.dim, 1);
        drawText(renderer, x, by + 26, "A POWER IS PRICED BY WHAT IT EARNS - HELP IT AND YOUR STAKE GROWS", P.dim, 1);
        drawButton(renderer, layout.sharesBtn, "BACK TO DEALS", P.violet, true);
        return;
    }

    // --- Режим выбора элемента: таблица Менделеева вместо списка.
    if (state.exchangeTable) {
        drawText(renderer, x, layout.board.y, "PICK AN ELEMENT TO LIST EVERY SURVEYED MARKET FOR IT", P.cyan, 1);
        const std::vector<ElementDefinition>& elements = elementDefinitions();
        for (size_t i = 0; i < elements.size(); ++i) {
            int col = 0, row = 0;
            if (!periodicCell(elements[i].atomicNumber, col, row)) continue;
            const SDL_Rect cell = {layout.tableX + col * layout.cellW, layout.tableY + row * layout.cellH,
                                   layout.cellW - 2, layout.cellH - 2};
            // Живой рынок раскрашивает клетки так же, как в окне торговли —
            // игрок узнаёт привычную картину дефицита и избытка.
            SDL_Color fill = {34, 44, 62, 190};
            if (live && window.star >= 0 && window.star < int(game.markets.size())) {
                fill = marketCellColor(game.markets[window.star], int(i));
            }
            fillRect(renderer, cell.x, cell.y, cell.w, cell.h, fill);
            strokeRect(renderer, cell.x, cell.y, cell.w, cell.h,
                       int(i) == state.exchangeElement ? P.amber : SDL_Color{52, 68, 92, 220});
            drawText(renderer, cell.x + 2, cell.y + 2, elements[i].symbol, P.text, 1);
        }
        drawButton(renderer, layout.elementsBtn, "BACK TO LIST", P.cyan, true);
        return;
    }

    // --- Сводка сделок. Данные из кэша (updateExchangeBoard), список листается.
    const std::vector<ArbitrageDeal>& deals = state.exchangeBoard;
    const int total = int(deals.size());
    int by = layout.board.y;
    drawText(renderer, x, by, "ELEM  DESTINATION      UNITS   BUY  MODEL   PROFIT  LY  AGE CONF", P.cyan, 1);
    by += layout.rowH;

    const int startIdx = std::min(std::max(0, window.scrollOffset), std::max(0, total - layout.rows));
    const int endIdx = std::min(total, startIdx + layout.rows);
    if (!live) {
        // не наша система — котировки покупки неживые, список не показываем
    } else if (total == 0) {
        drawText(renderer, x, by, "NOTHING SURVEYED YET.", P.dim, 1);
        drawText(renderer, x, by + 14, "VISIT NEIGHBOURING SYSTEMS - EACH ONE YOU DOCK AT", P.dim, 1);
        drawText(renderer, x, by + 28, "IS ADDED HERE AND STAYS IN YOUR MODEL.", P.dim, 1);
    } else {
        for (int i = startIdx; i < endIdx; ++i) {
            const ArbitrageDeal& d = deals[size_t(i)];
            const ClusterStar* ts = starAt(game, d.targetStar);
            char row[192];
            std::snprintf(row, sizeof(row), "%-5s %-15s %6.0F %5.1F %6.1F %8.0F %3.0F %4.0FY %3.0F%%",
                          d.element >= 0 ? elementDefinitions()[d.element].symbol : "?",
                          ts ? ts->name.substr(0, 15).c_str() : "?",
                          d.units, d.buyPrice, d.sellPrice, d.profit, d.distanceLy,
                          d.ageYears >= 0.0 ? d.ageYears : 0.0, d.confidence * 100.0);
            // Цвет строки — доверие к модели: свежая разведка зелёная, протухшая тусклая.
            // Убыточное направление (видно только под фильтром) всегда красное.
            const SDL_Color tone = d.profit <= 0.0 ? P.red
                                 : (d.confidence > 0.66 ? P.green : (d.confidence > 0.33 ? P.text : P.dim));
            drawText(renderer, x, by, row, tone, 1);
            by += layout.rowH;
        }
        char pos[64];
        std::snprintf(pos, sizeof(pos), "%d-%d OF %d", startIdx + 1, endIdx, total);
        drawText(renderer, layout.board.x + layout.board.w - 110, layout.board.y, pos, P.dim, 1);
        if (startIdx > 0) drawButton(renderer, layout.scrollUp, "UP", P.cyan, true);
        if (endIdx < total) drawButton(renderer, layout.scrollDown, "DOWN", P.cyan, true);
    }

    // --- Кнопки лицензий.
    const double licPrice = game.licencePrice();
    const double settleCost = game.licenceSettleCost();
    const bool canBuy = game.playerAgent >= 0 && game.playerAgent < int(game.agents.size()) &&
                        game.agents[game.playerAgent].money >= licPrice;
    // При отозванной лицензии кнопка гаснет: погашение квоты торговлю не
    // размораживает, а деньги забирает (см. playerSettleQuota).
    const bool canSettle = remaining > 0.0 && !game.licenceRevoked && game.playerAgent >= 0 &&
                           game.playerAgent < int(game.agents.size()) &&
                           game.agents[game.playerAgent].money >= settleCost;
    char btn[96];
    std::snprintf(btn, sizeof(btn), "+LICENCE %d", int(std::ceil(licPrice)));
    drawButton(renderer, layout.buyLicence, btn, P.amber, canBuy);
    if (remaining > 0.0) {
        std::snprintf(btn, sizeof(btn), "SETTLE QUOTA %d", int(std::ceil(settleCost)));
    } else {
        std::snprintf(btn, sizeof(btn), "QUOTA MET");
    }
    drawButton(renderer, layout.settleQuota, btn, P.green, canSettle);
    drawButton(renderer, layout.sharesBtn, "SHARES", P.violet, true);
    if (state.exchangeElement >= 0) {
        std::snprintf(btn, sizeof(btn), "ALL ELEMENTS (%s)",
                      elementDefinitions()[state.exchangeElement].symbol);
    } else {
        std::snprintf(btn, sizeof(btn), "FILTER BY ELEMENT");
    }
    // Взнос доступен на любой стоянке, снятие — только на покрытую светом сумму.
    {
        const bool docked = playerMarketStar(game) >= 0;
        const double wallet = (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size()))
                                  ? game.agents[game.playerAgent].money : 0.0;
        drawButton(renderer, layout.accountIn, "TO ACCOUNT", P.amber, docked && wallet > 0.0);
        drawButton(renderer, layout.accountOut, "FROM ACCOUNT", P.green,
                   docked && game.factionClearedTreasury(game.playerFaction) > 0.0);
    }
    drawButton(renderer, layout.elementsBtn, btn, P.cyan, true);
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
        const int rowY = listY + row * 42;
        SDL_Rect upgradeBtn = {window.rect.x + window.rect.w - 200, rowY, 90, 24};
        SDL_Rect buyNewBtn = {window.rect.x + window.rect.w - 105, rowY, 90, 24};
        
        if (i < nShips) {
            if (contains(upgradeBtn, mouseX, mouseY)) {
                state.confirmBuyShipIndex = i;
                return true;
            }
            if (contains(buyNewBtn, mouseX, mouseY)) {
                game.buyAdditionalShip(game.playerAgent, dockedStar, i);
                return true;
            }
        } else if (i > nShips) {
            SDL_Rect btn = {window.rect.x + window.rect.w - 105, rowY, 90, 24};
            if (contains(btn, mouseX, mouseY)) {
                game.buyModule(game.playerAgent, i - nShips - 1);
                return true;
            }
        }
    }
    return true;
}

// Кредиты на шкале от сотни до триллиона в одну строку 5x7-шрифта: миллиард
// цифрами читается как шум, а «1.84 B» — как цена.
std::string creditsLabel(double value) {
    char buf[32];
    const double v = std::fabs(value);
    if (v >= 1.0e12) std::snprintf(buf, sizeof(buf), "%.2F T", value / 1.0e12);
    else if (v >= 1.0e9) std::snprintf(buf, sizeof(buf), "%.2F B", value / 1.0e9);
    else if (v >= 1.0e6) std::snprintf(buf, sizeof(buf), "%.2F M", value / 1.0e6);
    else if (v >= 1.0e3) std::snprintf(buf, sizeof(buf), "%.1F K", value / 1.0e3);
    else std::snprintf(buf, sizeof(buf), "%.0F", value);
    return std::string(buf);
}

// Строка разбора цены: что за характеристика, чему она равна и во сколько раз
// она умножает цену. Игрок должен видеть НЕ число, а из чего оно собрано.
void drawPriceFactor(SDL_Renderer* renderer, int x, int y, int width,
                     const char* label, const std::string& value, double factor) {
    drawText(renderer, x, y, label, P.dim, 1);
    drawText(renderer, x + 96, y, value, P.text, 1);
    char mul[16];
    std::snprintf(mul, sizeof(mul), "X%.2F", factor);
    const SDL_Color tint = factor >= 1.60 ? P.green : (factor <= 1.08 ? P.dim : P.text);
    drawText(renderer, x + width - textWidth(mul, 1), y, mul, tint, 1);
}

// --- Окно хайтек-этажа (§31) -------------------------------------------------
//
// Три вещества в строках, три действия внизу. Устроено намеренно скучно и
// одинаково с окном торговли: игрок уже умеет читать «цена / запас / сколько
// беру», и хайтек не должен требовать учиться заново. Отличается только
// масштаб чисел — и это единственное, что должно бросаться в глаза.
struct ExoticsLayout {
    SDL_Rect buy[EX_COUNT];
    SDL_Rect sell[EX_COUNT];
    SDL_Rect amount = {0, 0, 0, 0};
    SDL_Rect containment = {0, 0, 0, 0};
    SDL_Rect plating = {0, 0, 0, 0};
    SDL_Rect forge[TECH_STAT_COUNT];
    int rowY = 0;
    int rowH = 58;
};

ExoticsLayout exoticsLayout(const Window& window) {
    ExoticsLayout layout;
    const int x = window.rect.x + WINDOW_PAD;
    const int right = window.rect.x + window.rect.w - WINDOW_PAD;
    layout.rowY = window.rect.y + TITLE_H + 52;
    for (int k = 0; k < EX_COUNT; ++k) {
        const int ry = layout.rowY + k * layout.rowH + 16;
        layout.buy[k]  = {right - 178, ry, 84, 22};
        layout.sell[k] = {right - 88, ry, 84, 22};
    }
    const int by = window.rect.y + window.rect.h - 34;
    layout.amount = {x, by - 30, 110, 22};
    layout.containment = {x + 120, by - 30, 190, 22};
    layout.plating = {x + 318, by - 30, 190, 22};
    const int fw = std::max(56, (window.rect.w - WINDOW_PAD * 2 - 6 * 4) / TECH_STAT_COUNT);
    for (int i = 0; i < TECH_STAT_COUNT; ++i) {
        layout.forge[i] = {x + i * (fw + 4), by, fw, 24};
    }
    return layout;
}

const char* exoticStatCode(int stat) {
    switch (stat) {
        case TECH_INTELLECT: return "INT";
        case TECH_CHARISMA: return "CHA";
        case TECH_MATERIALS: return "MAT";
        case TECH_TACTICS: return "TAC";
        case TECH_KINEMATICS: return "KIN";
        case TECH_SENSORS: return "SEN";
        default: return "LUK";
    }
}

void drawExoticsWindow(SDL_Renderer* renderer, const Game& game, const Window& window,
                       const WindowState& state, bool active) {
    const ClusterStar* star = starAt(game, window.star);
    drawWindowFrame(renderer, window,
        star ? ("HIGH-TECH EXCHANGE / " + star->name) : "HIGH-TECH EXCHANGE", active);
    if (!star) return;

    const ExoticsLayout layout = exoticsLayout(window);
    const int x = window.rect.x + WINDOW_PAD;
    int y = window.rect.y + TITLE_H + 12;
    const bool onSite = game.playerAtStar(window.star);
    const Ship* ship = (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size()))
                           ? &game.agents[size_t(game.playerAgent)].ship : NULL;
    const double cash = ship ? game.agents[size_t(game.playerAgent)].money : 0.0;

    char line[220];
    const double bay = ship ? ship->containment : 0.0;
    const double room = ship ? shipExoticRoom(*ship) : 0.0;
    std::snprintf(line, sizeof(line), "CREDITS %s   BAY %.0F/%.0F UNITS",
                  creditsLabel(cash).c_str(), bay - room, bay);
    drawText(renderer, x, y, line, bay > 0.0 ? P.green : P.red, 1);
    y += 14;
    if (bay <= 0.0) {
        drawText(renderer, x, y, "NO CONTAINMENT BAY - EXOTIC MATTER CANNOT BE HELD", P.red, 1);
    } else if (!onSite) {
        drawText(renderer, x, y, "NOT ON SITE - DOCK HERE TO TRADE", P.red, 1);
    } else {
        drawText(renderer, x, y, "PRICE FOLLOWS THE LOCAL RESERVE - BUYING IT UP RAISES IT", P.dim, 1);
    }
    y += 16;
    drawText(renderer, x, y, "MATTER      HELD    RESERVE      UNIT PRICE", P.cyan, 1);

    for (int k = 0; k < EX_COUNT; ++k) {
        const int ry = layout.rowY + k * layout.rowH;
        const double stock = game.exoticStockAt(window.star, k);
        const double price = game.exoticPriceAt(window.star, k);
        const bool here = price > 0.0;
        const ExoticDef& def = exoticDefs()[size_t(k)];
        const double held = ship ? ship->exotic[k] : 0.0;
        const std::string priceCell = here ? (creditsLabel(price) + " CR") : std::string("NOT TRADED HERE");
        std::snprintf(line, sizeof(line), "%-11s %6.0F  %9.0F   %s",
                      def.name, held, stock, priceCell.c_str());
        drawText(renderer, x, ry, line, here ? P.text : P.dim, 1);
        drawText(renderer, x, ry + 12, def.blurb, P.dim, 1);
        // Тонкая полоса выработанности: она и есть «сколько тут ещё осталось».
        const double target = exoticTargetStock(*star, k);
        if (target > 0.0) {
            bar(renderer, x, ry + 26, 220, 5, clamp01(stock / (target * 1.2)),
                stock < target * 0.35 ? P.red : (stock < target * 0.8 ? P.amber : P.green));
        }
        const bool canBuy = here && onSite && room > 0.0 && cash > price;
        const bool canSell = here && onSite && held > 0.0;
        drawButton(renderer, layout.buy[k], "BUY", P.green, canBuy);
        drawButton(renderer, layout.sell[k], "SELL", P.amber, canSell);
    }

    // --- Переоснастка и кузница ---
    const int shipyard = game.shipyardLevelAtStar(window.star);
    // Подсказка про верфь стоит ВЫШЕ подписи поля объёма: на 16 пикселях они
    // накладывались друг на друга, и по-русски это читалось как каша.
    int fy = layout.amount.y - 28;
    if (shipyard < 2) {
        drawText(renderer, x, fy, "REFIT AND FORGE NEED A SHIPYARD - THIS SYSTEM HAS NONE", P.dim, 1);
    } else {
        // Строка собирается из ПЕРЕВОДИМОГО куска и чисел отдельно: словарь
        // видит текст уже собранным, поэтому ключ с «%s» здесь не сработал бы.
        drawText(renderer, x, fy, "FORGE A CHROMOCORE OF YOUR CHOICE", P.amber, 1);
        std::snprintf(line, sizeof(line), "%.0F %s + %s CR", game.forgeCondensateCost(),
                      exoticDefs()[EX_CONDENSATE].symbol, creditsLabel(game.forgeCreditCost()).c_str());
        drawText(renderer, x + 240, fy, line, P.amber, 1);
    }

    drawText(renderer, layout.amount.x, layout.amount.y - 11, "AMOUNT", P.dim, 1);
    fillRect(renderer, layout.amount.x, layout.amount.y, layout.amount.w, layout.amount.h, {9, 14, 26, 245});
    strokeRect(renderer, layout.amount.x, layout.amount.y, layout.amount.w, layout.amount.h,
        state.tradeAmountEditing && active ? P.cyan : P.border);
    drawText(renderer, layout.amount.x + 8, layout.amount.y + 8, tradeAmountLabel(state),
        state.tradeAmount.empty() ? P.dim : P.text, 1);

    const double bayPrice = game.containmentNextPrice();
    if (bayPrice > 0.0) {
        std::snprintf(line, sizeof(line), "BAY %d %s", game.playerRefitLevel(false) + 1,
                      creditsLabel(bayPrice).c_str());
    } else {
        std::snprintf(line, sizeof(line), "BAY MAXED");
    }
    drawButton(renderer, layout.containment, line, P.cyan,
               onSite && shipyard >= 2 && bayPrice > 0.0 && cash >= bayPrice);

    const double platePrice = game.platingNextPrice();
    const bool canPlate = ship && onSite && shipyard >= 2 && platePrice > 0.0 &&
                          cash >= platePrice && ship->exotic[EX_NEUTRONIUM] >= PLATING_NEUTRONIUM_UNITS;
    if (platePrice > 0.0) {
        std::snprintf(line, sizeof(line), "PLATE %d: %.0F NM", game.playerRefitLevel(true) + 1,
                      PLATING_NEUTRONIUM_UNITS);
    } else {
        std::snprintf(line, sizeof(line), "PLATING MAXED");
    }
    drawButton(renderer, layout.plating, line, P.cyan, canPlate);

    const bool canForge = ship && onSite && shipyard >= 2 &&
                          ship->exotic[EX_CONDENSATE] >= game.forgeCondensateCost() &&
                          cash >= game.forgeCreditCost();
    for (int i = 0; i < TECH_STAT_COUNT; ++i) {
        drawButton(renderer, layout.forge[i], exoticStatCode(i), P.amber, canForge);
    }
}

bool handleExoticsWindowMouseDown(WindowState& state, Game& game, const Window& window,
                                  HudSelection& selection, int mouseX, int mouseY, int button) {
    const ExoticsLayout layout = exoticsLayout(window);
    const double amount = tradeRequestedAmount(state);
    // Все действия окна работают по системе, ГДЕ СТОИТ КОРАБЛЬ, а показывает
    // окно ту, что открыли. Кнопки в чужой системе нарисованы серыми — значит
    // и кликаться не должны, иначе игрок, глядя на цену дальнего рынка, купил
    // бы по цене своего.
    const bool onSite = game.playerAtStar(window.star);

    if (button == SDL_BUTTON_LEFT && contains(layout.amount, mouseX, mouseY)) {
        state.tradeAmountEditing = true;
        SDL_StartTextInput();
        return true;
    }
    if (state.tradeAmountEditing) {
        state.tradeAmountEditing = false;
        SDL_StopTextInput();
    }

    if (!onSite) return true;
    for (int k = 0; k < EX_COUNT; ++k) {
        if (contains(layout.buy[k], mouseX, mouseY)) {
            if (game.playerBuyExotic(k, amount) > 0.0) selection.agent = game.playerAgent;
            return true;
        }
        if (contains(layout.sell[k], mouseX, mouseY)) {
            if (game.playerSellExotic(k, amount) > 0.0) selection.agent = game.playerAgent;
            return true;
        }
    }
    if (contains(layout.containment, mouseX, mouseY)) {
        game.playerUpgradeContainment();
        return true;
    }
    if (contains(layout.plating, mouseX, mouseY)) {
        game.playerAddHullPlating();
        return true;
    }
    for (int i = 0; i < TECH_STAT_COUNT; ++i) {
        if (contains(layout.forge[i], mouseX, mouseY)) {
            game.playerForgeChromocore(i);
            return true;
        }
    }
    return true;
}

void openExoticsWindow(WindowState& state, int starIndex, int screenW, int screenH) {
    int cascade = 0;
    for (auto& w : state.windows) {
        if (w.kind == WindowKind::Exotics) {
            w.star = starIndex;
            bringWindowToFront(state, w.id);
            return;
        }
        cascade++;
    }
    Window w;
    w.id = state.nextId++;
    w.kind = WindowKind::Exotics;
    w.star = starIndex;
    w.rect = defaultWindowRect(WindowKind::Exotics, screenW, screenH, cascade);
    state.windows.push_back(w);
    state.activeId = w.id;
}

void drawColonyWindow(SDL_Renderer* renderer, const Game& game, const Window& window, const WindowState& state, bool active) {
    const ClusterStar* star = starAt(game, window.star);
    const bool owned = game.playerOwnsStar(window.star);
    const bool onSite = game.playerAtStar(window.star);
    drawWindowFrame(renderer, window,
        star ? (std::string(owned ? "COLONY / " : "SYSTEM FOR SALE / ") + star->name) : "COLONY / NO SYSTEM",
        active);
    if (!star) return;

    const ColonyLayout layout = colonyLayout(window);
    const int x = window.rect.x + WINDOW_PAD;
    const int columnW = layout.columnX - x - 14;
    int y = window.rect.y + TITLE_H + 12;

    const double cash = (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size()))
                            ? game.agents[game.playerAgent].money : 0.0;
    drawStat(renderer, x, y, "CREDITS ", creditsLabel(cash), P.dim, P.green);
    y += 16;
    if (!onSite) {
        drawText(renderer, x, y, "NOT ON SITE - DOCK HERE TO ACT", P.red, 1);
    } else if (owned) {
        drawText(renderer, x, y, "FREE MARKET - TAKE AND GIVE AT ZERO", P.green, 1);
    } else {
        drawText(renderer, x, y, "BUY THE SYSTEM WHOLE - IT KEEPS LIVING", P.amber, 1);
    }
    y += 20;

    const Colony* colony = colonyAtStar(game, window.star);
    char buf[64];

    if (!owned) {
        const SystemPrice price = game.systemPrice(window.star);
        drawPriceFactor(renderer, x, y, columnW, "POPULATION", creditsLabel(star->population), price.population); y += 14;
        std::snprintf(buf, sizeof(buf), "%.2F", star->industry);
        drawPriceFactor(renderer, x, y, columnW, "INDUSTRY", buf, price.industry); y += 14;
        std::snprintf(buf, sizeof(buf), "%.2F", star->habitability);
        drawPriceFactor(renderer, x, y, columnW, "HABITABLE", buf, price.habitability); y += 14;
        // Недра оценены не тоннами, а годовым выпуском в кредитах: рынок уже
        // знает, что иридий дороже песка, и таблица для этого не нужна.
        drawPriceFactor(renderer, x, y, columnW, "RESOURCES",
            creditsLabel(std::max(0.0, (price.resources - 1.0) * SYSTEM_PRICE_TURNOVER_REF)) + "/Y",
            price.resources); y += 14;
        if (colony) {
            std::snprintf(buf, sizeof(buf), "INF %.2F SY %d", colony->infrastructure, colony->shipyardLevel);
        } else {
            std::snprintf(buf, sizeof(buf), "NONE");
        }
        drawPriceFactor(renderer, x, y, columnW, "BUILT", buf, price.development); y += 14;
        const int owner = star->ownerFaction;
        const std::string ownerName = (owner >= 0 && owner < int(game.factions.size()))
                                          ? game.factions[owner].name : std::string("UNCLAIMED");
        drawPriceFactor(renderer, x, y, columnW, "OWNER", ownerName, price.sovereignty); y += 20;

        drawText(renderer, x, y, "ASKING PRICE", P.dim, 1);
        drawText(renderer, x + 96, y, creditsLabel(price.total) + " CR",
                 cash >= price.total ? P.green : P.red, 1);
        y += 14;
        // Окупаемость — не вход формулы, а СЛЕДСТВИЕ: цена собрана из
        // характеристик системы, доход идёт от её годового выпуска, и частное
        // отвечает игроку на единственный вопрос, который у него есть, — стоит
        // ли. Медиана по скоплению ~10 000 лет, но гарантии нет ни у одной
        // системы: богатая людьми дороже, чем даёт её выпуск, тихая рудная —
        // наоборот. Ровно поэтому «какую покупать» — вопрос, а не таблица.
        {
            const double income = game.colonyIncomeAt(window.star);
            char pay[96];
            if (income > 0.0) {
                const double years = price.total / income;
                std::snprintf(pay, sizeof(pay), "%s CR/Y - PAYS BACK IN %s Y",
                              creditsLabel(income).c_str(), creditsLabel(years).c_str());
                drawText(renderer, x, y, "YIELD", P.dim, 1);
                drawText(renderer, x + 96, y, pay, years < 12000.0 ? P.green : P.amber, 1);
            } else {
                drawText(renderer, x, y, "YIELD", P.dim, 1);
                drawText(renderer, x + 96, y, "NO WORKING ECONOMY YET", P.dim, 1);
            }
        }
        y += 18;
        if (owner >= 0 && owner < int(game.factions.size())) {
            drawText(renderer, x, y, "PAID IN FULL TO THE SELLING FACTION", P.dim, 1);
            y += 13;
        }
        if (cash < price.total) {
            drawText(renderer, x, y, "SHORT BY " + creditsLabel(price.total - cash) + " CR", P.red, 1);
        }

        drawButton(renderer, layout.buy, "BUY SYSTEM", P.amber, onSite && cash >= price.total);
        return;
    }

    // --- Своя система: как она живёт и что в кассе ---
    drawStat(renderer, x, y, "POPULATION ", creditsLabel(star->population)); y += 14;
    std::snprintf(buf, sizeof(buf), "%.2F", star->industry);
    int cx = drawStat(renderer, x, y, "INDUSTRY ", buf);
    std::snprintf(buf, sizeof(buf), "%d", game.shipyardLevelAtStar(window.star));
    drawStat(renderer, cx, y, "SHIPYARD ", buf); y += 14;
    if (colony) {
        std::snprintf(buf, sizeof(buf), "%.2F", colony->infrastructure);
        cx = drawStat(renderer, x, y, "INFRA ", buf);
        std::snprintf(buf, sizeof(buf), "%.2F", colony->automation);
        drawStat(renderer, cx, y, "AUTOMATION ", buf); y += 14;
    }
    if (window.star >= 0 && window.star < int(game.markets.size())) {
        const double strain = game.markets[window.star].strain;
        std::snprintf(buf, sizeof(buf), "%.0F%%", strain * 100.0);
        drawStat(renderer, x, y, "UNMET NEEDS ", buf, P.dim,
                 strain > 0.35 ? P.red : (strain > 0.15 ? P.amber : P.green));
        y += 18;
    }

    drawStat(renderer, x, y, "INCOME ", creditsLabel(game.colonyIncomeAt(window.star)) + " CR/Y", P.dim, P.green);
    y += 14;
    drawStat(renderer, x, y, "VAULT ", creditsLabel(game.colonyLedgerAt(window.star)) + " CR", P.dim, P.amber);
    y += 18;

    // Очередь стройки — это и есть «колония развивает себя сама»: касса тратится
    // без участия игрока, поэтому забирать всё до копейки значит замораживать рост.
    if (colony && colonyQueueCount(*colony) > 0) {
        const ConstructionItem& item = colony->constructionQueue.front();
        std::snprintf(buf, sizeof(buf), "%s %.0F%% OF %s", colonyQueueLabel(*colony).c_str(),
            colonyQueueProgress(*colony) * 100.0, creditsLabel(item.cost).c_str());
        drawStat(renderer, x, y, "BUILDING ", buf, P.dim, P.cyan);
        y += 14;
        drawText(renderer, x, y, "THE COLONY SPENDS ITS OWN VAULT", P.dim, 1);
    } else {
        drawText(renderer, x, y, "IDLE - IT WILL START A BUILD WHEN FUNDED", P.dim, 1);
    }

    drawText(renderer, layout.amount.x, layout.amount.y - 12, "AMOUNT", P.dim, 1);
    fillRect(renderer, layout.amount.x, layout.amount.y, layout.amount.w, layout.amount.h, {9, 14, 26, 245});
    strokeRect(renderer, layout.amount.x, layout.amount.y, layout.amount.w, layout.amount.h,
        state.tradeAmountEditing && active ? P.cyan : P.border);
    drawText(renderer, layout.amount.x + 10, layout.amount.y + 10, tradeAmountLabel(state),
        state.tradeAmount.empty() ? P.dim : P.text, 1);

    const double vault = game.colonyLedgerAt(window.star);
    drawButton(renderer, layout.deposit, "DEPOSIT", P.green, onSite && cash > 0.0);
    drawButton(renderer, layout.withdraw, "WITHDRAW", P.amber, onSite && vault > 0.0);
    drawButton(renderer, layout.withdrawAll, "TAKE ALL", P.amber, onSite && vault > 0.0);

    // Имя своей системы. Номер не показываем в поле и не даём править: правится
    // ОСНОВА, номер приписывается сам (§25).
    const bool renaming = state.renameStar == window.star && active;
    drawText(renderer, layout.rename.x, layout.rename.y - 12, "SYSTEM NAME", P.dim, 1);
    fillRect(renderer, layout.rename.x, layout.rename.y, layout.rename.w, layout.rename.h, {9, 14, 26, 245});
    strokeRect(renderer, layout.rename.x, layout.rename.y, layout.rename.w, layout.rename.h,
               renaming ? P.cyan : P.border);
    const std::string shown = renaming ? state.renameText + "_" : starNameStem(star->name);
    drawText(renderer, layout.rename.x + 10, layout.rename.y + 10, shown,
             renaming ? P.text : P.dim, 1);
    // Подсказка не длиннее колонки: 168 px — это 28 знаков шрифта, и русская
    // строка обязана уложиться в те же 28 (см. §14 про ширину колонок).
    drawText(renderer, layout.rename.x, layout.rename.y + 34,
             renaming ? "ENTER APPLIES / ESC CANCELS"
                      : (onSite ? "CLICK TO RENAME" : "RENAME ON SITE ONLY"),
             renaming ? P.cyan : P.dim, 1);
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
        const int rowY = listY + row * 42;
        SDL_Rect btn = {window.rect.x + window.rect.w - 105, rowY, 90, 24};
        if (i < nShips) {
            const ShipClass& sc = classes[i];
            drawText(renderer, topX, rowY, sc.name.c_str(), P.cyan, 1);
            double currentHullPrice = 0.0;
            if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
                for (const auto& c : classes) {
                    if (c.name == game.agents[game.playerAgent].ship.name) {
                        currentHullPrice = c.price;
                        break;
                    }
                }
            }
            const double upgradePrice = std::max(0.0, sc.price - currentHullPrice);
            // Второй борт больше не стоит `price + 1000000` (заградительная константа
            // убрана): он требует СВОБОДНОЙ ЛИЦЕНЗИИ, купленной в брокерской конторе.
            const double buyNewPrice = sc.price;
            const int freeLic = game.playerFreeLicences();

            std::snprintf(line, sizeof(line), "CR:%.0F | CG:%.0F HW:%.0F LW:%.0F AR:%.0F U:%.0F", sc.price, sc.cargoCapacity, sc.heavyWeapons, sc.lightWeapons, sc.armor, sc.utility);
            drawText(renderer, topX, rowY + 14, line, P.text, 1);

            // Разница с ТЕКУЩИМ корпусом. Не запрещаем невыгодную покупку — решает
            // игрок, — но и не даём купить даунгрейд вслепую: первые два корпуса в
            // списке по трюму МЕНЬШЕ стартового, и без этой строки это не видно.
            if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
                const Ship& mine = game.agents[game.playerAgent].ship;
                if (sc.name != mine.name) {
                    const double dCargo = sc.cargoCapacity - mine.cargoCapacity;
                    const double dProp = sc.propellantVolume - mine.propellantVolume;
                    const double dBunker = sc.fuelVolume - mine.fuelVolume;
                    char delta[128];
                    std::snprintf(delta, sizeof(delta), "VS YOURS  CARGO %+.0F  TANK %+.0F  BUNKER %+.0F",
                        dCargo, dProp, dBunker);
                    drawText(renderer, topX + 300, rowY + 28, delta, dCargo < 0.0 ? P.red : P.green, 1);
                }
            }

            SDL_Rect upgradeBtn = {window.rect.x + window.rect.w - 200, rowY, 90, 24};
            SDL_Rect buyNewBtn = {window.rect.x + window.rect.w - 105, rowY, 90, 24};

            char upgStr[32], newStr[32];
            std::snprintf(upgStr, sizeof(upgStr), "UPG %.0F", upgradePrice);
            if (freeLic > 0) std::snprintf(newStr, sizeof(newStr), "NEW %.0F", buyNewPrice);
            else             std::snprintf(newStr, sizeof(newStr), "NEED LICENCE");

            drawButton(renderer, upgradeBtn, upgStr, P.green, cash >= upgradePrice);
            drawButton(renderer, buyNewBtn, newStr, P.amber, freeLic > 0 && cash >= buyNewPrice);
        } else if (i == nShips) {
            fillRect(renderer, topX, rowY + 7, window.rect.w - 2 * WINDOW_PAD, 1, P.border);
            drawText(renderer, topX, rowY + 13, "-- SHIP MODULES (FIT WHILE DOCKED) --", P.amber, 1);
        } else {
            const int m = i - nShips - 1;
            const ModuleDef& def = mods[m];
            std::snprintf(line, sizeof(line), "%s [%s]", def.name.c_str(), moduleSlotLabel(def.slot));
            drawText(renderer, topX, rowY, line, P.cyan, 1);
            std::snprintf(line, sizeof(line), "CR:%.0F  SY:%d  %s", def.price, def.minShipyard, def.blurb.c_str());
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
            if (textWidth(line, 1) <= window.rect.w - WINDOW_PAD * 2) {
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
        
        // Кнопка НАЗНАЧАЕТ цель, а не стартует: вылет отдельным GO на карте.
        const bool isDestination = game.playerAgent >= 0 &&
            game.agents[game.playerAgent].destStar == window.star && !isEnRoute;
        std::string label = "DESTINATION";
        SDL_Color color = P.cyan;
        if (isEnRouteToThis) {
            label = "EN ROUTE"; color = P.green;
        } else if (isAtThis) {
            label = "DOCKED"; color = P.dim;
        } else if (isEnRoute) {
            label = "MOVING"; color = P.red;
        } else if (isDestination) {
            label = "DEST SET"; color = P.green;
        }
        drawButton(renderer, layout.route, label, color, game.playerAgent >= 0);
        drawButton(renderer, layout.trade, "TRADE", P.green, false);
        drawButton(renderer, layout.contracts, "JOBS", P.cyan, game.playerCanOpenContractsAt(window.star));
        drawButton(renderer, layout.colony, "C COLONY", P.amber, true);
        drawButton(renderer, layout.cargo, "CARGO", P.cyan, game.playerAgent >= 0);
        return;
    }

    drawText(renderer, x, y, "ROLE " + star->economyRole, P.text, 1);
    y += 18;
    labelBar(renderer, x, y, window.rect.w - 28, "POP", starPopulationWeight(*star) / 8.0, P.green);
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

    // Чего система хочет НА САМОМ ДЕЛЕ — три главные функции-нужды и их закрытие.
    if (window.star >= 0 && window.star < int(game.markets.size())) {
        const Market& market = game.markets[window.star];
        if (market.needs.size() == size_t(EF_COUNT)) {
            int order[EF_COUNT];
            for (int f = 0; f < EF_COUNT; ++f) order[f] = f;
            for (int a = 0; a < EF_COUNT; ++a) {
                for (int b = a + 1; b < EF_COUNT; ++b) {
                    if (market.needs[order[b]] > market.needs[order[a]]) {
                        const int tmp = order[a]; order[a] = order[b]; order[b] = tmp;
                    }
                }
            }
            double total = 0.0;
            for (int f = 0; f < EF_COUNT; ++f) total += market.needs[f];
            std::string demandLine = "DEMANDS ";
            for (int k = 0; k < 3 && total > 0.0; ++k) {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "%s %.0F%%  ", econFunctionCode(order[k]),
                              market.needs[order[k]] / total * 100.0);
                demandLine += buf;
            }
            drawText(renderer, x, y, demandLine, P.amber, 1);
            y += 14;
            if (market.strain > 0.02) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "UNMET NEEDS %.0F%% - COLONY STARVING", market.strain * 100.0);
                drawText(renderer, x, y, buf, market.strain > 0.25 ? P.red : P.amber, 1);
                y += 14;
            }
        }
    }

    if (marketKnown) {
        drawPlayerMarketView(renderer, marketView, elementDefinitions()[selection.element], x, y);
    }
    drawLiveColonySummary(renderer, game, window.star, x, y);

    const SystemLayout layout = systemLayout(window);
    const bool isEnRoute = game.playerAgent >= 0 && game.agents[game.playerAgent].ship.enRoute;
    const bool isEnRouteToThis = isEnRoute && game.agents[game.playerAgent].destStar == window.star;
    const bool isAtThis = game.playerAgent >= 0 && game.agents[game.playerAgent].currentStar == window.star && !isEnRoute;
    
    // Кнопка НАЗНАЧАЕТ цель, а не стартует: вылет отдельным GO на карте.
    const bool isDestination = game.playerAgent >= 0 &&
        game.agents[game.playerAgent].destStar == window.star && !isEnRoute;
    std::string label = "DESTINATION";
    SDL_Color color = P.cyan;
    if (isEnRouteToThis) {
        label = "EN ROUTE"; color = P.green;
    } else if (isAtThis) {
        label = "DOCKED"; color = P.dim;
    } else if (isEnRoute) {
        label = "MOVING"; color = P.red;
    } else if (isDestination) {
        label = "DEST SET"; color = P.green;
    }
    drawButton(renderer, layout.route, label, color, game.playerAgent >= 0);
    drawButton(renderer, layout.trade, "TRADE", P.green, playerMarketStar(game) == window.star);
    drawButton(renderer, layout.contracts, "JOBS", P.cyan, game.playerCanOpenContractsAt(window.star));
    // Окно собственности открывается всегда — оно и есть витрина с ценой; сделка
    // и касса внутри уже требуют стоянки. Зелёный = система уже твоя.
    drawButton(renderer, layout.colony, game.playerOwnsStar(window.star) ? "MY SYS" : "COLONY",
               game.playerOwnsStar(window.star) ? P.green : P.amber, true);
    drawButton(renderer, layout.cargo, "CARGO", P.cyan, game.playerAgent >= 0);
    drawButton(renderer, layout.shipyard, "YARD", P.cyan, true);
    // Свободная лицензия — условие покупки нового борта, поэтому кнопка светится,
    // когда она есть: игрок видит «можно расширяться» не открывая биржу.
    drawButton(renderer, layout.exchange, "BROKER", P.amber,
               game.playerFreeLicences() > 0 || playerMarketStar(game) == window.star);
    drawButton(renderer, layout.exotics, "EXOTICS", P.violet, game.exoticMarketAt(window.star));
}

void drawContractRow(SDL_Renderer* renderer, const Game& game, const Window& window, const Contract& contract, int row, bool activeContractRow) {
    const int x = window.rect.x + WINDOW_PAD;
    const int y = window.rect.y + TITLE_H + CONTRACT_HEADER_H + 52 + row * CONTRACT_ROW_H;
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
        std::snprintf(line, sizeof(line), "%s#%d %s %.0F %s > %s  CR %.0F  %.1FY",
            contract.rushFactor > 1.01 ? "RUSH " : "",
            contract.id,
            validResource ? elements[contract.resource].symbol : "?",
            contract.amount,
            origin ? origin->name.c_str() : "-",
            target ? target->name.c_str() : "-",
            contract.reward,
            yearsLeft);
        if (textWidth(line, 1) > button.x - x - 16) {
            std::snprintf(line, sizeof(line), "#%d %s %.0F > %s CR%.0F %.1FY",
                contract.id,
                validResource ? elements[contract.resource].symbol : "?",
                contract.amount,
                target ? target->name.c_str() : "-",
                contract.reward,
                yearsLeft);
        }
    } else {
        std::snprintf(line, sizeof(line), "%s#%d %s %s > %s  CR %.0F  %.1FY",
            contract.rushFactor > 1.01 ? "RUSH " : "",
            contract.id,
            contractTypeLabel(contract.type),
            origin ? origin->name.c_str() : "-",
            target ? target->name.c_str() : "-",
            contract.reward,
            yearsLeft);
        if (textWidth(line, 1) > button.x - x - 16) {
            std::snprintf(line, sizeof(line), "#%d %s > %s CR%.0F %.1FY",
                contract.id,
                contractTypeLabel(contract.type),
                target ? target->name.c_str() : "-",
                contract.reward,
                yearsLeft);
        }
    }
    // Срочный заказ метится ПРЯМО В СТРОКЕ и цветом: его повышенная ставка уже
    // сидит в награде, и игрок должен видеть, за что переплачивают, ДО взятия
    // (§24). Иначе «дорогой заказ с коротким сроком» выглядел бы подарком.
    const bool rush = contract.rushFactor > 1.01;
    const SDL_Color rowColour = activeContractRow ? P.amber : (rush ? P.red : P.text);
    drawText(renderer, x + 8, y + 2, line, rowColour, 1);
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
    // Кто ты ЗДЕСЬ. Тир доски задаёт репутация у владельца этой системы (§24),
    // и без этой строки игрок не понимал бы, почему на соседней доске работа
    // крупнее: разница объясняется именно здесь, у самой доски.
    const int boardFaction = star->ownerFaction;
    if (boardFaction >= 0 && boardFaction < int(game.factions.size())) {
        const double tier = game.factionJobTier(boardFaction);
        char standing[128];
        std::snprintf(standing, sizeof(standing), "%s - %s, %.0F JOBS DONE, MAX LOAD %.0F T",
            game.factions[boardFaction].name.c_str(), Game::jobRankName(tier),
            game.factionReputationOf(boardFaction), Game::jobCargoForTier(tier));
        drawText(renderer, x, y, standing, tier > 0.001 ? P.cyan : P.dim, 1);
        y += 12;
    }
    char fleet[96];
    std::snprintf(fleet, sizeof(fleet), "FLEET HOLD HERE %.0F T", game.playerFleetCapacityAt(window.star));
    drawText(renderer, x, y, fleet, P.dim, 1);
    y += 12;
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
        drawText(renderer, x, window.rect.y + TITLE_H + CONTRACT_HEADER_H + 54,
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
    // Свой рынок — не отдельный режим торговли, а тот же рынок с ценой 0. Всё
    // отличие в интерфейсе — этот флажок и подписи на кнопках.
    const bool freeMarket = game.playerOwnsStar(window.star);
    if (liveMarket && star && freeMarket) {
        drawText(renderer, topX, topY, star->name + " - YOUR COLONY, ALL PRICES 0", P.green, 1);
    } else if (liveMarket && star) {
        drawText(renderer, topX, topY, star->name + " LOCAL MARKET", P.green, 1);
    } else {
        drawText(renderer, topX, topY, "NO LIVE MARKET - DOCK IN THIS SYSTEM", P.red, 1);
    }
    if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
        const Agent& player = game.agents[game.playerAgent];
        char buf1[32], buf2[32], buf3[32];
        std::snprintf(buf1, sizeof(buf1), "%.0F", player.money);
        std::snprintf(buf2, sizeof(buf2), "%.0F/%.0F", shipCargoMass(player.ship), player.ship.cargoCapacity);
        std::snprintf(buf3, sizeof(buf3), "%.0F%%/%.0F%%",
            shipFuelFill(player.ship) * 100.0, shipPropellantFill(player.ship) * 100.0);
        int cx = topX;
        cx = drawStat(renderer, cx, topY + 16, "CREDITS ", buf1, P.green);
        cx = drawStat(renderer, cx, topY + 16, "CARGO ", buf2);
        cx = drawStat(renderer, cx, topY + 16, "FUEL/PROP ", buf3);
    }

    const std::vector<ElementDefinition>& elements = elementDefinitions();
    // Что лежит в трюме ПРЯМО СЕЙЧАС. Без этого продать привезённое можно было
    // только вспомнив символ и найдя его глазами среди 118 клеток — таблица
    // показывала рынок, но не корабль.
    std::vector<double> holdCargo(elements.size(), 0.0);
    bool hasCargo = false;
    if (game.playerAgent >= 0 && game.playerAgent < int(game.agents.size())) {
        const Ship& ps = game.agents[game.playerAgent].ship;
        for (size_t c = 0; c < ps.cargo.size(); ++c) {
            const int e = elementIndex(ps.cargo[c].element);
            if (e >= 0 && e < int(holdCargo.size()) && ps.cargo[c].amount > 0.0) {
                holdCargo[size_t(e)] += ps.cargo[c].amount;
                hasCargo = true;
            }
        }
    }
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
        // Груз на борту — ВТОРАЯ, внутренняя рамка. Именно вторая, а не замена
        // цвета: внешняя рамка говорит про рынок (что система добывает и что ей
        // нужно), внутренняя — про корабль. Две разные вещи, два разных контура.
        if (holdCargo[i] > 0.0) {
            strokeRect(renderer, rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, P.green);
        }

        drawText(renderer, rect.x + 3, rect.y + 3, elements[i].symbol, idx == selection.element ? P.amber : P.text, 1);
        // Для чего этот элемент годится — прямо в клетке (три буквы функции).
        const int primary = econPrimaryFunction(idx);
        if (rect.w >= 26 && rect.h >= 24) {
            const char* code = primary >= 0 ? econFunctionCode(primary) : "-";
            drawText(renderer, rect.x + 3, rect.y + 12, code, primary >= 0 ? P.dim : P.border, 1);
        }
        char z[8];
        int len = std::snprintf(z, sizeof(z), "%d", elements[i].atomicNumber);
        drawText(renderer, rect.x + rect.w - 3 - len * 6, rect.y + rect.h - 9, z, P.dim, 1);
    }

    drawText(renderer, layout.amount.x, layout.amount.y - 12, "AMOUNT", P.dim, 1);
    fillRect(renderer, layout.amount.x, layout.amount.y, layout.amount.w, layout.amount.h, {9, 14, 26, 245});
    strokeRect(renderer, layout.amount.x, layout.amount.y, layout.amount.w, layout.amount.h,
        state.tradeAmountEditing && active ? P.cyan : P.border);
    drawText(renderer, layout.amount.x + 10, layout.amount.y + 10, tradeAmountLabel(state),
        state.tradeAmount.empty() ? P.dim : P.text, 1);

    drawButton(renderer, layout.buy, freeMarket ? "TAKE" : "BUY", P.green, liveMarket);
    drawButton(renderer, layout.sell, freeMarket ? "GIVE" : "SELL", P.amber, liveMarket);
    drawButton(renderer, layout.sellAll, freeMarket ? "GIVE ALL" : "SELL ALL", P.amber,
               liveMarket && hasCargo);
    drawButton(renderer, layout.refuel, freeMarket ? "FILL FUEL+PROP" : "BUY FUEL+PROP", P.amber, liveMarket);
    drawButton(renderer, layout.hold, "HOLD / TANKS", P.cyan, true);


    const int infoX = layout.buy.x;
    const int infoY = layout.hold.y + 42;
    if (selection.element >= 0 && selection.element < int(elements.size())) {
        const ElementDefinition& element = elements[selection.element];
        drawText(renderer, infoX, infoY, std::string(element.symbol) + " " + element.name, P.text, 1);

        // К чему элемент пригоден: три лучшие функции с качеством кандидата.
        int order[EF_COUNT];
        for (int f = 0; f < EF_COUNT; ++f) order[f] = f;
        for (int a = 0; a < EF_COUNT; ++a) {
            for (int b = a + 1; b < EF_COUNT; ++b) {
                if (econQuality(selection.element, order[b]) > econQuality(selection.element, order[a])) {
                    const int tmp = order[a]; order[a] = order[b]; order[b] = tmp;
                }
            }
        }
        int line = infoY + 14;
        bool anyUse = false;
        for (int k = 0; k < 3; ++k) {
            const int f = order[k];
            const double q = econQuality(selection.element, f);
            if (q <= ECON_QUALITY_FLOOR) continue;
            anyUse = true;
            char buf[64];
            const double share = market ? market->marketShare(selection.element, f) : 0.0;
            std::snprintf(buf, sizeof(buf), "%-4s Q%.2F", econFunctionCode(f), q);
            int cx = drawStat(renderer, infoX, line, "", buf, P.dim, k == 0 ? P.cyan : P.dim);
            if (market && share > 0.004) {
                std::snprintf(buf, sizeof(buf), "%.0F%%", share * 100.0);
                drawStat(renderer, cx, line, "SHARE ", buf, P.dim, P.text);
            }
            line += 13;
        }
        if (!anyUse) {
            drawText(renderer, infoX, line, "NO INDUSTRIAL USE", P.red, 1);
            line += 13;
        }

        if (market && selection.element < int(market->prices.size())) {
            char buf1[32], buf2[32];
            const double price = market->prices[selection.element];
            const double reference = marketReferencePrice(selection.element);
            std::snprintf(buf1, sizeof(buf1), "%.1F", price);
            int cx = drawStat(renderer, infoX, line, "PRICE ", buf1, P.dim, P.amber);
            if (reference > 0.0) {
                const double ratio = price / reference;
                std::snprintf(buf2, sizeof(buf2), "X%.2F", ratio);
                drawStat(renderer, cx, line, "VS CLUSTER ", buf2, P.dim,
                         ratio < 0.87 ? P.green : (ratio > 1.15 ? P.red : P.text));
            }
            line += 13;
            // Склад и годовой расход — величины масштаба §17 (сотни миллионов). Полным
            // числом они не помещались в колонку и переползали за край окна, а прочесть
            // «275574988» всё равно нельзя. Тот же формат, что и у денег: 275.57 M.
            cx = drawStat(renderer, infoX, line, "STOCK ",
                          creditsLabel(market->supply[selection.element].amount), P.dim, P.green);
            drawStat(renderer, cx, line, "USE ",
                     creditsLabel(market->demandRate[selection.element]) + "/Y", P.dim, P.red);
            line += 13;
            const double cover = market->coverageYears(selection.element);
            std::snprintf(buf1, sizeof(buf1), "%.1FY", std::min(999.0, cover));
            cx = drawStat(renderer, infoX, line, "COVER ", buf1, P.dim, cover < 2.0 ? P.red : P.text);
            std::snprintf(buf1, sizeof(buf1), "%.0F", element.atomicMass);
            drawStat(renderer, cx, line, "MASS ", buf1, P.dim, P.text);
        }
    }

    drawText(renderer, layout.tableX, window.rect.y + window.rect.h - 15, "CLICK AMOUNT + TYPE NUMBER / EMPTY=MAX / RMB CELL QUICK BUY", P.dim, 1);
}

void drawTransactionsWindow(SDL_Renderer* renderer, const Game& game, const Window& window, bool active) {
    drawWindowFrame(renderer, window, "TRANSACTION HISTORY", active);
    
    int y = window.rect.y + TITLE_H + 12;
    int x = window.rect.x + 12;
    
    if (game.transactions.empty()) {
        drawText(renderer, x, y, "NO TRANSACTIONS RECORDED.", P.dim, 1);
        return;
    }
    
    // Draw in reverse order (newest first)
    for (auto it = game.transactions.rbegin(); it != game.transactions.rend(); ++it) {
        const auto& t = *it;
        if (y + 12 > window.rect.y + window.rect.h - 10) break;

        char buf[256];
        std::string starName = "Deep Space";
        if (t.starIndex >= 0 && t.starIndex < int(game.cluster.stars.size())) {
            starName = game.cluster.stars[t.starIndex].name;
        }

        // Цвет — это ВИД записи, а не знак суммы. Заказ на руках белый (он ещё
        // ничего не стоил и ничего не принёс), сданный зелёный, сгоревший
        // красный; касса как была — приход зелёный, расход красный.
        SDL_Color colour = t.amount >= 0 ? P.green : P.red;
        switch (t.kind) {
        case JournalKind::JobAccepted:  colour = P.text; break;
        case JournalKind::JobCompleted: colour = P.green; break;
        case JournalKind::JobFailed:    colour = P.red; break;
        case JournalKind::Money:        break;
        }

        if (t.kind == JournalKind::Money) {
            std::snprintf(buf, sizeof(buf), "[YEAR %.2f] @ %s : %c%.0f Cr",
                t.time, starName.c_str(), t.amount >= 0 ? '+' : '-', std::abs(t.amount));
        } else {
            std::snprintf(buf, sizeof(buf), "[YEAR %.2f] @ %s : %s",
                t.time, starName.c_str(), t.text.c_str());
        }

        drawText(renderer, x, y, buf, colour, 1);
        y += 16;
    }
}

void drawShipFitWindow(SDL_Renderer* renderer, const Game& game, const Window& window, bool active) {
    drawWindowFrame(renderer, window, "SHIP UPGRADES", active);
    
    if (game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) {
        drawText(renderer, window.rect.x + 12, window.rect.y + TITLE_H + 12, "NO PLAYER", P.red, 1);
        return;
    }
    
    const Agent& agent = game.agents[game.playerAgent];
    char line[128];
    int y = window.rect.y + TITLE_H + 12;
    int x = window.rect.x + 12;
    
    std::snprintf(line, sizeof(line), "INSTALLED (%d / %d):", int(agent.ship.modules.size()), agent.ship.maxModules);
    drawText(renderer, x, y, line, P.amber, 1);
    y += 12;
    
    const auto& defs = moduleDefs();
    for (size_t i = 0; i < agent.ship.modules.size(); ++i) {
        const ModuleDef& def = defs[agent.ship.modules[i]];
        std::snprintf(line, sizeof(line), "- %s", def.name.c_str());
        drawText(renderer, x, y, line, P.cyan, 1);
        
        SDL_Rect btn = {window.rect.x + window.rect.w - 90, y - 4, 70, 20};
        drawButton(renderer, btn, "UNEQUIP", P.red, true);
        y += 24;
    }
    
    y += 12;
    drawText(renderer, x, y, "AVAILABLE IN CARGO:", P.amber, 1);
    y += 12;
    
    std::vector<int> cargoMods;
    for (size_t i = 0; i < agent.ship.cargo.size(); ++i) {
        if (agent.ship.cargo[i].element.rfind("Module: ", 0) == 0 && agent.ship.cargo[i].amount > 0.0) {
            cargoMods.push_back(i);
        }
    }
    
    for (size_t i = 0; i < cargoMods.size(); ++i) {
        std::string modName = agent.ship.cargo[cargoMods[i]].element.substr(8);
        std::snprintf(line, sizeof(line), "- %s (x%d)", modName.c_str(), int(agent.ship.cargo[cargoMods[i]].amount));
        drawText(renderer, x, y, line, P.text, 1);
        
        SDL_Rect btn = {window.rect.x + window.rect.w - 90, y - 4, 70, 20};
        bool canEquip = int(agent.ship.modules.size()) < agent.ship.maxModules;
        drawButton(renderer, btn, "EQUIP", canEquip ? P.green : P.dim, canEquip);
        y += 24;
    }
}

// Окно HOLD: три ёмкости корабля рядом и перелив между ними.
//
// Раньше загрузка жила двумя кнопками в окне торговли и постоянно была серой:
// ёмкость держала один элемент, а слить его было нечем. Теперь бункер и бак —
// такие же смеси, как трюм, грузить можно что угодно, и всё видно разом.
namespace {

void drawHoldColumn(SDL_Renderer* renderer, const Window& window, int column,
                    const std::string& title, const std::vector<Resource>& items,
                    double used, double capacity, const std::string& unitLabel,
                    SDL_Color accent, bool enabled) {
    const int x = holdColumnX(window, column);
    int y = window.rect.y + TITLE_H + 12;
    drawText(renderer, x, y, title, accent, 1);
    y += 16;

    char line[96];
    const bool over = used > capacity + 0.001;
    if (over) {
        std::snprintf(line, sizeof(line), "%.0F / %.0F %s  OVERLOAD +%.0F",
            used, capacity, unitLabel.c_str(), used - capacity);
    } else {
        std::snprintf(line, sizeof(line), "%.0F / %.0F %s", used, capacity, unitLabel.c_str());
    }
    drawText(renderer, x, y, line, over ? P.red : P.text, 1);
    y += 12;
    bar(renderer, x, y, HOLD_COL_W - 24, 6, capacity > 0.0 ? used / capacity : 0.0, over ? P.red : accent);
    y += 16;

    const int textX = x + HOLD_ARROW_W + 4;
    drawText(renderer, textX, y, "EL", P.dim, 1);
    drawText(renderer, textX + 30, y, "AMOUNT", P.dim, 1);
    drawText(renderer, textX + 96, y, "MASS", P.dim, 1);

    if (items.empty()) {
        drawText(renderer, textX, holdRowY(window, 0), "EMPTY", P.dim, 1);
        return;
    }

    int row = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].amount <= 0.0) continue;
        if (row >= HOLD_MAX_ROWS) break;
        const int ry = holdRowY(window, row);
        const double mass = items[i].amount * resourceUnitMass(items[i].element);
        drawText(renderer, textX, ry, items[i].element, P.text, 1);
        std::snprintf(line, sizeof(line), "%.1F", items[i].amount);
        drawText(renderer, textX + 30, ry, line, P.text, 1);
        std::snprintf(line, sizeof(line), "%.1F", mass);
        drawText(renderer, textX + 96, ry, line, accent, 1);

        // Стрелки. Трюм в центре: из него грузим наружу в обе стороны,
        // из ёмкостей — обратно к центру.
        if (column == HOLD_CARGO) {
            drawButton(renderer, holdArrowRect(window, column, row, true), "<", P.green, true);
            drawButton(renderer, holdJettisonRect(window, row), "X", P.red, true);
            drawButton(renderer, holdArrowRect(window, column, row, false), ">", P.cyan, enabled);
        } else if (column == HOLD_BUNKER) {
            drawButton(renderer, holdArrowRect(window, column, row, false), ">", P.amber, true);
        } else {
            drawButton(renderer, holdArrowRect(window, column, row, true), "<", P.amber, true);
        }
        ++row;
    }
}

}

void drawCargoWindow(SDL_Renderer* renderer, const Game& game, const Window& window, bool active, const WindowState& state) {
    drawWindowFrame(renderer, window, "HOLD / TANKS", active);

    int x = window.rect.x + 12;
    if (game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) {
        drawText(renderer, x, window.rect.y + TITLE_H + 12, "NO PLAYER", P.red, 1);
        return;
    }

    const Ship& ship = game.agents[game.playerAgent].ship;
    const MixSummary fuelMix = shipFuelMix(ship);
    const MixSummary propMix = shipPropellantMix(ship);
    // У факела рабочего тела нет вовсе: топливо и есть выхлоп.
    const bool torch = driveUsesFuelAsPropellant(ship.driveIndex);

    drawHoldColumn(renderer, window, HOLD_BUNKER, "BUNKER (FUEL)", ship.fuel,
                   fuelMix.volume, shipFuelTankVolume(ship), "VOL", P.green, true);
    drawHoldColumn(renderer, window, HOLD_CARGO, "CARGO", ship.cargo,
                   shipCargoMass(ship), ship.cargoCapacity, "MASS", P.amber, !torch);
    drawHoldColumn(renderer, window, HOLD_TANK, torch ? "TANK (UNUSED BY TORCH)" : "TANK (PROPELLANT)",
                   ship.propellant, propMix.volume, ship.propellantVolume, "VOL", P.cyan, !torch);

    // Итог по двигательной установке: что реально даёт текущая заправка.
    const int footY = window.rect.y + window.rect.h - 86;
    strokeRect(renderer, window.rect.x + 8, footY - 8, window.rect.w - 16, 1, P.border);

    const std::vector<DriveDef>& drives = driveDefs();
    const std::string driveName = (ship.driveIndex >= 0 && ship.driveIndex < int(drives.size()))
        ? drives[ship.driveIndex].name : "-";
    char line[192];
    std::snprintf(line, sizeof(line), "DRIVE %s (%s)", driveName.c_str(),
        driveFamilyLabel(driveFamilyOf(ship.driveIndex)));
    drawText(renderer, x, footY, line, P.text, 1);

    // Показываем скорость истечения, которую даёт ТЕКУЩАЯ ручка на типовом
    // манёвре, а не только физический потолок: иначе ручку не видно в работе.
    const RouteCost nominal = shipRouteCost(ship, ship.speed * 2.0, 1.0, 1.0);
    const double ceiling = shipMaxExhaustVelocity(ship);
    // Энергия топлива УЖЕ с учётом доли поджига: балласт виден сразу.
    std::snprintf(line, sizeof(line), "FUEL %.2F MEV/NUC   VE %.4FC / MAX %.4FC   MEAN A %.0F",
        fuelMix.specificEnergy, nominal.feasible ? nominal.exhaustVelocity : 0.0, ceiling,
        shipExhaustMix(ship).meanAtomicMass);
    drawText(renderer, x, footY + 14, line, P.dim, 1);

    // Во что обходится типовой манёвр при текущем режиме — так видно, ЧЕМ платим.
    if (nominal.feasible) {
        std::snprintf(line, sizeof(line), "PER HOP  PROPELLANT %.1F   FUEL %.1F  (MASS)",
            nominal.propellantMass, nominal.fuelMass);
        drawText(renderer, x, footY + 28, line, P.dim, 1);
    }

    drawText(renderer, x, footY + 44, "<  LOAD FUEL      X JETTISON      LOAD PROPELLANT  >", P.dim, 1);
    drawText(renderer, x, footY + 58,
        destStarLabel(game).c_str(), P.dim, 1);

    // --- Ручка режима двигателя ---
    // В полёте обе ручки заперты: рабочая точка двигателя фиксируется на вылете
    // (§12.4), иначе маршрутная оценка и реальный расход считают по разным
    // скоростям истечения. Гейт — `enRoute`, поэтому вставший после STOP
    // посреди пустоты корабль перенастроить МОЖНО.
    const bool driveLocked = ship.enRoute;
    const SDL_Rect thr = holdThrottleRect(window);
    const double t = std::max(0.0, std::min(1.0, ship.throttle));
    const char* mode = t < 0.42 ? "BULK" : (t > 0.58 ? "BURN" : "OPTIMUM");
    std::snprintf(line, sizeof(line), "THROTTLE %.2F  %s%s", t, mode, driveLocked ? "  LOCKED" : "");
    drawText(renderer, thr.x - 40, thr.y - 14, line, driveLocked ? P.dim : P.text, 1);

    fillRect(renderer, thr.x, thr.y, thr.w, thr.h, {9, 14, 26, 245});
    strokeRect(renderer, thr.x, thr.y, thr.w, thr.h, P.border);
    // Отметка ценового оптимума ровно посередине шкалы.
    fillRect(renderer, thr.x + thr.w / 2, thr.y - 3, 1, thr.h + 6, P.dim);
    const int knob = thr.x + int(t * double(thr.w - 6));
    fillRect(renderer, knob, thr.y - 2, 6, thr.h + 4,
             driveLocked ? P.dim : (t < 0.42 ? P.cyan : (t > 0.58 ? P.amber : P.green)));
    drawText(renderer, thr.x - 40, thr.y + 2, "PROP", P.cyan, 1);
    drawText(renderer, thr.x + thr.w + 6, thr.y + 2, "FUEL", P.amber, 1);

    // --- Крейсерская скорость ---
    const SDL_Rect cru = holdCruiseRect(window);
    const double cf = std::max(0.2, std::min(1.0, ship.cruiseFraction));
    std::snprintf(line, sizeof(line), "CRUISE %.0F%%  %.3FC%s", cf * 100.0, shipCruiseSpeed(ship),
                  driveLocked ? "  LOCKED" : "");
    drawText(renderer, cru.x - 40, cru.y - 14, line, driveLocked ? P.dim : P.text, 1);
    fillRect(renderer, cru.x, cru.y, cru.w, cru.h, {9, 14, 26, 245});
    strokeRect(renderer, cru.x, cru.y, cru.w, cru.h, P.border);
    const int cknob = cru.x + int((cf - 0.2) / 0.8 * double(cru.w - 6));
    fillRect(renderer, cknob, cru.y - 2, 6, cru.h + 4, driveLocked ? P.dim : P.green);
    drawText(renderer, cru.x - 40, cru.y + 2, "SLOW", P.green, 1);
    drawText(renderer, cru.x + cru.w + 6, cru.y + 2, "FAST", P.red, 1);

    // --- Автоподбор под назначенную цель ---
    const int destStar = game.playerAgent >= 0 ? game.agents[game.playerAgent].destStar : -1;
    const bool canOptimise = destStar >= 0 && !ship.enRoute &&
                             destStar != game.agents[game.playerAgent].currentStar;
    drawButton(renderer, holdOptimalRect(window), "OPTIMAL", P.green, canOptimise);

    // --- Кредиты соседнему своему борту ---
    // Кошелёк лежит НА БОРТУ, а не у игрока: перевозить деньги приходится так
    // же, как груз. Кнопки живы только когда второй свой борт стоит рядом.
    const int mate = game.playerOtherShipHere();
    drawButton(renderer, holdGiveRect(window), "GIVE", P.amber, mate >= 0);
    drawButton(renderer, holdTakeRect(window), "TAKE", P.green, mate >= 0);
    if (mate >= 0) {
        std::snprintf(line, sizeof(line), "%s HAS %s CR",
                      game.agents[mate].ship.name.c_str(),
                      creditsLabel(game.agents[mate].money).c_str());
        drawText(renderer, holdGiveRect(window).x, holdGiveRect(window).y - 13, line, P.dim, 1);
    }
    // (§35) Автопилот соседнего борта: купленный корпус перестаёт быть мебелью.
    const bool mateAuto = mate >= 0 && game.agents[mate].autoTrade;
    drawButton(renderer, holdAutoRect(window), mateAuto ? "AUTO ON" : "AUTO OFF",
               mateAuto ? P.green : P.dim, mate >= 0);

    // Поле шага: сколько двигает ОДНО нажатие стрелки. То же число, что AMOUNT
    // в окне торговли, — одно понятие на все операции с веществом.
    const SDL_Rect step = holdStepRect(window);
    fillRect(renderer, step.x, step.y, step.w, step.h, {9, 14, 26, 245});
    strokeRect(renderer, step.x, step.y, step.w, step.h,
        state.tradeAmountEditing && active ? P.cyan : P.border);
    std::snprintf(line, sizeof(line), "STEP %s", tradeAmountLabel(state).c_str());
    drawText(renderer, step.x + 7, step.y + 7, line,
        state.tradeAmount.empty() ? P.dim : P.text, 1);
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
        } else if (window.kind == WindowKind::ShipFit) {
            drawShipFitWindow(renderer, game, window, active);
        } else if (window.kind == WindowKind::Cargo) {
            drawCargoWindow(renderer, game, window, active, state);
        } else if (window.kind == WindowKind::Transactions) {
            drawTransactionsWindow(renderer, game, window, active);
        } else if (window.kind == WindowKind::Colony) {
            drawColonyWindow(renderer, game, window, state, active);
        } else if (window.kind == WindowKind::Exotics) {
            drawExoticsWindow(renderer, game, window, state, active);
        } else if (window.kind == WindowKind::Exchange) {
            drawExchangeWindow(renderer, game, window, state, active);
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
    panel(renderer, x, y, w, 106);
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

    // Строки про добычу здесь не место: бурение живёт в локальном полёте и там
    // же себя и показывает (`MINING … GRADE …` над камнем). В обзоре скопления
    // это была вечная «ДОБЫЧА ВЫКЛ» — подсказка о клавише, которая тут не нужна.
    const double threshold = 100.0 + game.tech.cores * 40.0;
    const double rprog = clamp01(threshold > 0.0 ? game.tech.research / threshold : 0.0);
    std::snprintf(line, sizeof(line), "CORES %d", game.tech.cores);
    drawText(renderer, x + 10, y + 40, line, P.amber, 1);
    drawText(renderer, x + 92, y + 40, "RSCH", P.dim, 1);
    bar(renderer, x + 128, y + 39, w - 138, 7, rprog, P.cyan);

    const char* codes[7] = {"IN", "CH", "MA", "TA", "KI", "SE", "LU"};
    const double vals[7] = {game.tech.intellect, game.tech.charisma, game.tech.materials,
        game.tech.tactics, game.tech.kinematics, game.tech.sensors, game.tech.luck};
    for (int i = 0; i < 7; ++i) {
        const int col = i % 4, rowi = i / 4;
        char cell[24];
        std::snprintf(cell, sizeof(cell), "%s%.2F", codes[i], vals[i]);
        const SDL_Color cc = vals[i] > 1.0001 ? P.green : P.dim;
        drawText(renderer, x + 10 + col * 78, y + 58 + rowi * 12, cell, cc, 1);
    }
}

static void drawObjectivesPanel(SDL_Renderer* renderer, const Game& game, int x, int y, int w) {
    struct Obj { const char* text; bool done; };
    std::vector<Obj> objs;
    if (game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) return;
    const Agent& p = game.agents[game.playerAgent];
    const Ship& sh = p.ship;

    // ЛЕСТНИЦА, а не список. Раньше здесь лежали четыре пункта, и после них
    // игра не ставила целей никогда: про заказы, про покупку системы, про
    // экзотику и про акции игрок мог узнать только случайно. Панель показывает
    // окно вокруг первой незакрытой ступени — последнюю взятую и то, что
    // дальше, — поэтому она остаётся короткой, но никогда не кончается.
    double repTotal = 0.0;
    for (size_t f = 0; f < game.factionReputation.size(); ++f) repTotal += game.factionReputation[f];

    objs.push_back({"TRADE: BUY (B) / SELL (Q)", p.trades > 0});
    // Локальный полёт — самая зрелищная часть игры и при этом дальше всего
    // от глаз новичка: без явной цели о клавише L никто не узнаёт.
    objs.push_back({"FLY THE SYSTEM: PRESS L", game.everEnteredLocal});
    objs.push_back({"UPGRADE: SHIPYARD (U)", !sh.modules.empty()});
    objs.push_back({"SURVEY 10 MARKETS", game.playerSurveyedMarketCount() >= 10});
    objs.push_back({"TAKE A JOB: JOBS BOARD", repTotal > 0.0});
    objs.push_back({"RESEARCH A CHROMOCORE", game.tech.cores > 0});
    objs.push_back({"MEET THE LICENCE QUOTA", game.licencePeriodsMet > 0});
    objs.push_back({"BUY A SECOND HULL (E)", game.playerShipCount() > 1});
    objs.push_back({"BUY A SYSTEM (C)", game.boughtSystems > 0});
    objs.push_back({"FIT A CONTAINMENT BAY (Y)", game.playerRefitLevel(false) > 0});
    objs.push_back({"RUN EXOTIC MATTER (Y)", !game.exoticStocks.empty()});
    objs.push_back({"FORGE A CORE YOU CHOSE (Y)", game.coresForged > 0});
    objs.push_back({"BUY INTO A POWER (E)", game.playerShareValue() > 0.0});
    objs.push_back({"PLATE THE HULL IN NEUTRONIUM", game.playerRefitLevel(true) > 0});

    // Срочное — поверх лестницы: это не прогресс, а «сделай сейчас».
    std::vector<Obj> urgent;
    for (size_t i = 0; i < game.anomalies.size(); ++i) {
        if (game.anomalies[i].discovered && !game.anomalies[i].resolved) {
            urgent.push_back({"SCAN ANOMALY: PRESS K", false});
            break;
        }
    }
    if (sh.hullHP < sh.maxHullHP - 0.5) urgent.push_back({"REPAIR HULL: DOCK + PRESS J", false});

    // Окно лестницы: одна закрытая ступень позади (чтобы был виден прогресс)
    // и всё остальное — впереди.
    int first = 0;
    while (first < int(objs.size()) && objs[size_t(first)].done) ++first;
    const int start = std::max(0, first - 1);
    const int budget = 6 - int(urgent.size());
    std::vector<Obj> shown;
    for (int i = start; i < int(objs.size()) && int(shown.size()) < std::max(1, budget); ++i) {
        shown.push_back(objs[size_t(i)]);
    }
    for (size_t i = 0; i < urgent.size(); ++i) shown.push_back(urgent[i]);

    const int rows = int(shown.size());
    const int h = 30 + rows * 13;
    panel(renderer, x, y, w, h);
    drawText(renderer, x + 10, y + 9, "OBJECTIVES", P.amber, 1);
    for (int i = 0; i < rows; ++i) {
        const int ry = y + 26 + i * 13;
        drawText(renderer, x + 10, ry, shown[size_t(i)].done ? "[X]" : "[ ]",
                 shown[size_t(i)].done ? P.green : P.dim, 1);
        drawText(renderer, x + 34, ry, shown[size_t(i)].text,
                 shown[size_t(i)].done ? P.dim : P.text, 1);
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
        panel(renderer, 12, y, leftW, 62);
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

        // Лицензионная квота — главный таймер игры, поэтому живёт рядом с кредитами
        // и всегда на виду. Цвет = насколько всё плохо: зелёный (норма выполнена),
        // янтарный (успеваешь), красный (не успеваешь или лицензия отозвана).
        char quota[96];
        if (game.licenceRevoked) {
            std::snprintf(quota, sizeof(quota), "LICENCE REVOKED - BUY BACK %d CR (F2)",
                          int(std::ceil(game.licenceBuyback)));
            drawText(renderer, 22, y + 45, quota, P.red, 1);
        } else {
            const double target = game.licenceQuotaTarget();
            const double left = std::max(0.0, game.licencePeriodEnd - game.time);
            const bool met = game.licenceQuotaPaid + 1e-6 >= target;
            // «Успеваю ли» = хватит ли оставшихся лет при текущем темпе уплаты.
            const double elapsed = std::max(1.0, LICENCE_PERIOD_YEARS - left);
            const double pace = game.licenceQuotaPaid / elapsed;
            const bool onTrack = met || pace * LICENCE_PERIOD_YEARS >= target;
            std::snprintf(quota, sizeof(quota), "QUOTA %d/%d CR  %dY LEFT  TARIFF %.0F%%",
                          int(game.licenceQuotaPaid), int(target), int(std::ceil(left)),
                          game.licenceTariffRate * 100.0);
            drawText(renderer, 22, y + 45, quota, met ? P.green : (onTrack ? P.amber : P.red), 1);
        }
        y += 72;
    }

    drawShipTechPanel(renderer, game, 12, y, leftW);
    y += 116;

    drawStarPanel(renderer, game, selection.star, selection.element, 12, y, leftW);
    y += 160;
    drawAgentPanel(renderer, game, selection.agent, 12, y, leftW);

    drawFactionPanel(renderer, game, screenW - 260, 12, 248);
    {
        const int facH = 44 + std::min(6, int(game.factions.size())) * 19;
        drawObjectivesPanel(renderer, game, screenW - 260, 12 + facH + 10, 248);
    }
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

// Что реплика Тимертии делает с интерфейсом: куда ткнуть стрелкой и какое окно
// раскрыть под руку игроку. Раньше это были три out-параметра подряд; топливный
// блок добавил четвёртое окно, и список параметров перестал читаться.
struct TutorialCue {
    int arrow = 0;                              // см. таблицу координат в drawVisualNovel; 0 — без стрелки
    bool openWindow = false;
    WindowKind window = WindowKind::SystemInfo; // значимо только при openWindow
};

// Последний шаг вступительной новеллы. Дальше tutorialCompleted, и Тимертия
// говорит только по делу (шаг 100 — сводка по новой системе).
const int TUTORIAL_LAST_STEP = 27;

// Сколько ближайших систем смотрит совет. 128 — не потолок железа, а потолок
// смысла: мерка Cr/год топит дальние цели сама, и на замере 8192 систем
// победитель всегда лежал в двух-трёх световых годах. Счёт — 1 мс.
const int TUTORIAL_SCAN_SYSTEMS = 128;

// Глубокий просчёт по клавише V (§28). Тимертия здесь смотрит ЧЕТВЕРО дальше и
// мимо разведки: это фан-сервис, и ограничивает его не карта игрока, а реактор.
const int INSIGHT_SCAN_SYSTEMS = 512;

// ЭНЕРГОПЛАТА ЗА МОДЕЛИРОВАНИЕ. Просчёт — это работа реактора, а не бесплатная
// подсказка интерфейса, поэтому нажатие стоит топлива.
//
// Плата ПЛОСКАЯ и мерится долей БУНКЕРА, а не числом разобранных рынков: игрок
// видит выскочившее сообщение, а не смету, и цена должна читаться одним числом —
// «один процент бака за просчёт».
//
// ⚠️ Доля берётся от ПОЛНОГО бака, а не от остатка. От остатка получилась бы
// апория: каждый раз минус процент того, что есть, — бак не пустеет никогда, и
// чем хуже дела, тем дешевле совет. От полного бака цена постоянна: сотня
// нажатий честно осушает корпус (стартовый бункер 219.8 массы, нажатие 2.198).
// На сухом бункере просчёта нет вовсе — тот же закон, что у буровой (§20.7),
// которая на пустом бункере просто выключается.
const double INSIGHT_BUNKER_SHARE = 0.01;

// Элемент, которого здесь ЯВНЫЙ избыток: добывают больше, чем съедают. Нужен
// на тот случай, когда сравнивать не с чем (рынок пока один) — тогда честнее
// сказать «вот местный избыток, ищите, кому его не хватает», чем выдумывать цель.
// Синтетические сверхтяжёлые сюда не попадают: их не потребляют вовсе, и
// отношение у них не 1e12, а бессмыслица.
int localSurplusElement(const Game& game, int starIndex) {
    if (starIndex < 0 || starIndex >= int(game.markets.size())) return -1;
    const Market& m = game.markets[starIndex];
    const int elems = int(std::min(elementCount(), m.prices.size()));
    int best = -1;
    double bestPressure = 1.0;   // ниже единицы — это уже не избыток
    for (int e = 0; e < elems; ++e) {
        if (m.prices[e] <= 0.001 || m.supply[e].amount < 1.0) continue;
        const double cons = e < int(m.demandRate.size()) ? m.demandRate[e] : 0.0;
        const double prod = e < int(m.productionRate.size()) ? m.productionRate[e] : 0.0;
        if (cons <= 1e-6) continue;
        const double pressure = prod / cons;
        if (pressure > bestPressure) { bestPressure = pressure; best = e; }
    }
    return best;
}

std::string elementNameAt(int elementIndex) {
    const auto& defs = elementDefinitions();
    if (elementIndex < 0 || elementIndex >= int(defs.size())) return I18N::tr("isotopes");
    return I18N::tr(defs[elementIndex].name);
}

std::string getTutorialText(const Game& game, int step, TutorialCue& cue, VisualNovelState& vn) {
    cue = TutorialCue();
    switch (step) {
        case 0: return I18N::tr("Master, I am Timertia - your AI core Agent.");
        case 1: return I18N::tr("Congratulations on obtaining your trading licence!");
        case 2: cue.arrow = 1; return I18N::tr("You can view your balance here.");
        // Квота — главный таймер партии (§21), и до сих пор новелла о нём молчала:
        // игрок узнавал про отзыв лицензии только когда терминалы гасли.
        case 3: cue.arrow = 6; return I18N::tr("The licence is a lease, not a gift. The banks audit it every period: pay the QUOTA out of your tariff or the terminals go dark. That counter is the real clock of your life here.");
        case 4: return I18N::tr("You own 1 space ship unit for now.");
        case 5: cue.arrow = 2; return I18N::tr("My subagents will monitor its system states here.");
        case 6: {
            std::string starName = I18N::tr("Unknown Node");
            if (game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                int starId = game.agents[game.playerAgent].currentStar;
                if (starId >= 0 && starId < (int)game.cluster.stars.size()) {
                    starName = game.cluster.stars[starId].name;
                }
            }
            cue.openWindow = true;
            cue.window = WindowKind::SystemInfo;
            char buf[256];
            std::snprintf(buf, sizeof(buf), I18N::tr("Your vessel is currently at %s. You can access a model of local star system here.").c_str(), starName.c_str());
            return buf;
        }
        case 7: cue.openWindow = true; cue.window = WindowKind::Trade; return I18N::tr("With your trading licence you can perform HIGH-FREQUENCY BROKERAGE on local market.");
        case 8: return I18N::tr("A periodic table based on standard supersymmetrical model is common CONVENTION of interstellar market.");
        case 9: return I18N::tr("NASH EQUILIBRIUM proves it is best to buy on supply and sell on demand.");
        // Шаги 10–11 — ЕДИНСТВЕННОЕ место, где Тимертия смотрит дальше разведки
        // игрока (решение пользователя): первую цель партии она называет
        // по-настоящему, дальше сводка работает только по увиденному. Обе реплики
        // говорят про ОДИН рейс — он посчитан здесь и запомнен в `vn`.
        //
        // ⚠️ Мерка — Cr за ГОД ПОЛЁТА, а не масса запаса. Прежний код ранжировал
        // рынки МАССОЙ: у водорода её на порядки больше всех, поэтому «покупай»
        // всегда указывало на водород, «продавай» — на самую населённую систему
        // в 15 ly (у неё же и потребление максимальное), и обе половины сводки
        // сходились в ОДНУ систему. Хуже: цена в «лучшей» цели бывала НИЖЕ
        // домашней — совет отправлял торговать в убыток (замер: 2.38 -> 1.74).
        case 10: {
            if (!vn.hintComputed && game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                vn.hintRun = game.playerBestRun(game.agents[game.playerAgent].currentStar,
                                                TUTORIAL_SCAN_SYSTEMS, false);
                vn.hintComputed = true;
            }
            const TradeRun& run = vn.hintRun;
            if (!run.valid) return I18N::tr("The local model finds nothing here worth a hold, Master. That happens: a port can be poor. Read the prices yourself - what is cheap here is what you load.");
            char buf[512];
            if (run.pressure > 1.05) {
                std::snprintf(buf, sizeof(buf), I18N::tr("The local model says: take %s. This port digs up %.1f times more of it than it burns, so it goes for a mere %.2f Cr.").c_str(),
                              elementNameAt(run.element).c_str(), run.pressure, run.buyPrice);
            } else {
                std::snprintf(buf, sizeof(buf), I18N::tr("The local model says: take %s - here it goes for a mere %.2f Cr.").c_str(),
                              elementNameAt(run.element).c_str(), run.buyPrice);
            }
            return buf;
        }
        case 11: {
            const TradeRun& run = vn.hintRun;
            if (!run.valid || run.targetStar < 0 || run.targetStar >= (int)game.cluster.stars.size())
                return I18N::tr("Where to sell is a question the ledger answers, Master: press E for the exchange. It weighs every market we have seen - and so far we have seen only this one.");
            char buf[512];
            std::snprintf(buf, sizeof(buf), I18N::tr("In %s they pay %.2f for it - %.1f ly, %.0f years under way, about %.0f Cr of profit. Remember that name, Master: %s!").c_str(),
                          game.cluster.stars[run.targetStar].name.c_str(),
                          run.sellPrice, run.distanceLy, run.years, run.profit,
                          game.cluster.stars[run.targetStar].name.c_str());
            return buf;
        }
        // --- Топливный блок (§12) ---------------------------------------------
        // Двигательная модель — самая дорогая для новичка часть игры: маршрут
        // отказывается строиться, а почему — нигде не сказано. Поэтому окно
        // ТРЮМ/БАКИ открывается прямо здесь, как система и рынок выше.
        case 12: cue.openWindow = true; cue.window = WindowKind::Cargo;
            return I18N::tr("Nothing crosses the void for free, though. This is the hold: BUNKER on the left, CARGO in the middle, TANK on the right.");
        case 13: return I18N::tr("Fuel in the bunker is what BURNS. Propellant in the tank is what is THROWN. The drive spends the energy of the fuel to hurl the propellant astern - and only thrown mass moves a hull.");
        case 14: return I18N::tr("In port the short answer is the BUY FUEL+PROP button on the market window: I will choose sane elements and fill both for you.");
        case 15: return I18N::tr("The long answer is these arrows. Buy any element into the cargo, then pour it left into the bunker or right into the tank. Light elements throw best, energetic ones burn best.");
        case 16: return I18N::tr("THROTTLE decides what you spend: to the left the drive throws more propellant and spares fuel, to the right it burns fuel hard and spares propellant. CRUISE decides your speed, and speed is paid for out of both.");
        case 17: return I18N::tr("Name a destination, then press OPTIMAL and I will set both knobs at local prices. Once you are under way they LOCK: a route is costed by the engine you left port with.");
        case 18: return I18N::tr("Read the tanks before every hop, Master. A hull out of propellant does not drift home, it simply drifts - and if the route is beyond your tanks, the terminal will refuse to plot it at all.");

        // --- Заказы и репутация (§23, §24) ------------------------------------
        case 19: cue.openWindow = true; cue.window = WindowKind::Contracts;
            return I18N::tr("Ports also post JOBS: a cargo, a destination, a deadline. Deliver and your name grows; miss the date and it shrinks.");
        case 20: return I18N::tr("Weigh every job in credits per year of flight against what a free run would earn on the same road. Nothing else about it matters.");
        case 21: return I18N::tr("Your name is the ceiling of everything: better standing means heavier jobs offered and more hulls allowed in your fleet.");

        // --- Что ещё есть в игре ----------------------------------------------
        case 22: return I18N::tr("Press L to fall into the system itself. There you fly the hull by hand, mine rock with M and dock with K - the belts are where cheap matter comes from.");
        case 23: return I18N::tr("By the way, you can also upgrade your vessel and purchase more trading licenses.");
        case 24: return I18N::tr("When the vault allows it, press C and buy a system outright. It pays you rent, berths your fleet, and takes any name you care to write on it.");
        case 25: return I18N::tr("Finally, the new technology of applied color superconductivity has produced novel AI cores.");
        case 26: return I18N::tr("They are still prototypes and very rare. Be sure to privatise every one you find.");
        case 27: return I18N::tr("F1 lists every control. I am at your service with more insights at any time, Master. [V]");
        // Сводка по прибытии в новую систему. В отличие от шагов 10–11 здесь
        // всезнания НЕТ: считается только по разведанным рынкам, потому что
        // сводка — это ЖУРНАЛ увиденного, а не радар. Отсюда и честный ответ на
        // первой системе партии: сравнивать не с чем, и Тимертия так и говорит.
        case 100: {
            if (game.playerAgent < 0 || game.playerAgent >= (int)game.agents.size()) return "";
            const int here = game.agents[game.playerAgent].currentStar;
            const TradeRun run = game.playerBestRun(here, TUTORIAL_SCAN_SYSTEMS, true);
            char buf[512];
            if (run.valid) {
                std::snprintf(buf, sizeof(buf), I18N::tr("A market report, Master: %s goes for %.2f here and fetches %.2f in %s - %.1f ly, %.0f years, some %.0f Cr. Best of everything we have seen.").c_str(),
                              elementNameAt(run.element).c_str(), run.buyPrice, run.sellPrice,
                              game.cluster.stars[run.targetStar].name.c_str(),
                              run.distanceLy, run.years, run.profit);
                return buf;
            }
            const int surplus = localSurplusElement(game, here);
            const int surveyed = game.playerSurveyedMarketCount();
            if (surveyed <= 1) {
                if (surplus < 0) return I18N::tr("No report yet, Master: this is the only market we have seen, and there is nothing to weigh it against. Fly, and I will keep the ledger.");
                std::snprintf(buf, sizeof(buf), I18N::tr("No report yet, Master: this is the only market we have seen. They have a surplus of %s here at %.2f - fly and find who lacks it, and I will keep the ledger.").c_str(),
                              elementNameAt(surplus).c_str(),
                              here < (int)game.markets.size() ? game.markets[here].prices[surplus] : 0.0);
                return buf;
            }
            if (surplus < 0) {
                std::snprintf(buf, sizeof(buf), I18N::tr("Nothing worth the fuel, Master: of the %d markets we have seen, none pays for a run from here.").c_str(), surveyed);
                return buf;
            }
            std::snprintf(buf, sizeof(buf), I18N::tr("Nothing worth the fuel, Master: of the %d markets we have seen, none pays for a run from here. They do have a surplus of %s at %.2f - somewhere it will be wanted.").c_str(),
                          surveyed, elementNameAt(surplus).c_str(),
                          here < (int)game.markets.size() ? game.markets[here].prices[surplus] : 0.0);
            return buf;
        }
        // Глубокий просчёт по V (§28). В отличие от сводки по прибытии, эта
        // смотрит ЧЕТВЕРО дальше и мимо разведки — и платит за это реактором.
        // Топливо списывает `toggleVisualNovel`; сюда шаг доходит уже оплаченным.
        case 101: {
            if (game.playerAgent < 0 || game.playerAgent >= (int)game.agents.size()) return "";
            const int here = game.agents[game.playerAgent].currentStar;
            const TradeRun run = game.playerBestRun(here, INSIGHT_SCAN_SYSTEMS, false);
            if (!run.valid) return I18N::tr("I ran the model, Master, and it comes back empty: with this hold and this purse there is no run out of here worth its fuel.");
            char buf[512];
            std::snprintf(buf, sizeof(buf), I18N::tr("Deep model, Master - and the reactor pays for it: %s goes for %.2f here and fetches %.2f in %s, %.1f ly, %.0f years, some %.0f Cr. Nothing within reach beats it.").c_str(),
                          elementNameAt(run.element).c_str(), run.buyPrice, run.sellPrice,
                          game.cluster.stars[run.targetStar].name.c_str(),
                          run.distanceLy, run.years, run.profit);
            return buf;
        }
        // Бункер сух — считать не на чем. Тот же закон, что у буровой (§20.7):
        // нет топлива, нет и работы реактора.
        case 102:
            return I18N::tr("The bunker is dry, Master: there is nothing to think with. A model costs energy like everything else aboard.");
    }
    return "";
}

bool advanceVisualNovel(WindowState& state, Game& game, int winW, int winH) {
    auto& vn = state.vnState;
    if (!vn.active) return false;
    
    if (vn.textProgress < UI::textLength(vn.targetText)) {
        vn.textProgress = float(UI::textLength(vn.targetText));
        vn.currentText = vn.targetText;
    } else {
        if (!vn.tutorialCompleted) {
            vn.tutorialStep++;
            if (vn.tutorialStep > TUTORIAL_LAST_STEP) {
                vn.tutorialCompleted = true;
                vn.active = false;
            } else {
                TutorialCue cue;
                vn.targetText = getTutorialText(game, vn.tutorialStep, cue, vn);
                vn.arrowTarget = cue.arrow;
                vn.textProgress = 0.0f;
                if (cue.openWindow && game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                    const int star = game.agents[game.playerAgent].currentStar;
                    // Окно раскрывается ровно под ту реплику, которая о нём говорит:
                    // рассказывать про бункер и бак, пока их не видно, бесполезно.
                    switch (cue.window) {
                        case WindowKind::Trade:     openTradeWindow(state, star, winW, winH); break;
                        case WindowKind::Cargo:     openCargoWindow(state, star, winW, winH); break;
                        case WindowKind::Contracts: openContractsWindow(state, star, winW, winH); break;
                        default:                    openSystemWindow(state, star, winW, winH); break;
                    }
                }
            }
        } else {
            vn.active = false;
        }
    }
    return true;
}

// Клавиша V. Раньше она просто зажигала и гасила коробку с уже сочинённой
// репликой: игрок, нажавший V через сто лет после прилёта, читал столетний
// совет. Теперь нажатие ПЕРЕСЧИТЫВАЕТ сводку по нынешней обстановке — и платит
// за это топливом реактора, потому что просчёт есть работа, а не подсказка.
//
// Во время вступительной новеллы клавиша остаётся выключателем: перебивать
// ленту обучения сводкой нельзя, и брать за это топливо тем более.
void toggleVisualNovel(WindowState& state, Game& game) {
    auto& vn = state.vnState;
    if (!vn.tutorialCompleted || vn.active) {
        vn.active = !vn.active;
        return;
    }
    if (game.playerAgent < 0 || game.playerAgent >= int(game.agents.size())) {
        vn.active = true;
        return;
    }

    Ship& ship = game.agents[game.playerAgent].ship;
    // Массу платы переводим в энергию тем же законом, которым реактор её потом
    // сожжёт (`shipDrawReactorEnergy`): доля c² = МэВ/нуклон / энергия покоя
    // нуклона. Балласт в бункере энергии не даёт, и специально ловить это не
    // надо — `specificEnergy` смеси уже умножен на долю поджига.
    const MixSummary mix = shipFuelMix(ship);
    const double fullMass = mix.volume > 1e-9
        ? mix.mass / mix.volume * shipFuelTankVolume(ship)
        : mix.mass;
    const double wantMass = fullMass * INSIGHT_BUNKER_SHARE;
    const double want = wantMass * mix.specificEnergy / NUCLEON_REST_MEV;
    const double fuelBefore = shipFuelMass(ship);
    std::vector<Resource> ash;
    const double got = shipDrawReactorEnergy(ship, want, &ash);
    // Зола ложится в трюм ЦЕЛИКОМ, как и в полёте (consumeAndStoreAsh): она
    // заменяет собой сгоревшее топливо, поэтому масса корабля не растёт.
    for (size_t i = 0; i < ash.size(); ++i) {
        if (ash[i].amount <= 1e-9) continue;
        bool merged = false;
        for (size_t c = 0; c < ship.cargo.size(); ++c) {
            if (ship.cargo[c].element != ash[i].element) continue;
            ship.cargo[c].amount += ash[i].amount;
            merged = true;
            break;
        }
        if (!merged) ship.cargo.push_back(ash[i]);
    }

    TutorialCue cue;
    vn.tutorialStep = got > 0.0 ? 101 : 102;
    vn.targetText = getTutorialText(game, vn.tutorialStep, cue, vn);
    vn.arrowTarget = cue.arrow;
    vn.textProgress = 0.0f;
    vn.currentText = "";
    vn.active = true;

    // Плата видна игроку: иначе бункер тает молча, и «энергоплата» остаётся
    // шуткой в комментарии, а не механикой на экране.
    if (got > 0.0) {
        char line[128];
        std::snprintf(line, sizeof(line), "model run: %.3F fuel burned",
                      fuelBefore - shipFuelMass(ship));
        game.lastEvent = line;
    } else {
        game.lastEvent = "model run refused: bunker dry";
    }
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
                TutorialCue cue;
                vn.targetText = getTutorialText(game, 100, cue, vn);
                vn.arrowTarget = cue.arrow;
                vn.textProgress = 0.0f;
                vn.currentText = "";
                vn.active = true;
            }
        }
    } else {
        if (vn.targetText.empty() && vn.tutorialStep == 0) {
            TutorialCue cue;
            vn.targetText = getTutorialText(game, vn.tutorialStep, cue, vn);
            vn.arrowTarget = cue.arrow;
        }
    }
    
    // Машинка считает СИМВОЛЫ, а не байты: русская реплика в UTF-8 двухбайтовая,
    // и обрезка по байту рвала бы букву пополам (на кадре появлялся бы «тофу»).
    if (vn.textProgress < UI::textLength(vn.targetText)) {
        vn.textProgress += dt * 50.0f;
        const float full = float(UI::textLength(vn.targetText));
        if (vn.textProgress > full) vn.textProgress = full;
        vn.currentText = UI::textPrefix(vn.targetText, (size_t)vn.textProgress);
    }
}

// Перенос по словам. Длина считается в СИМВОЛАХ: кириллица в UTF-8 занимает два
// байта, и подсчёт по `word.length()` рвал бы русскую реплику вдвое раньше.
std::string wrapText(const std::string& text, int maxChars) {
    std::string result;
    int lineLen = 0;
    std::string word;
    int wordLen = 0;

    for (char c : text) {
        if (c == ' ' || c == '\n') {
            if (lineLen + wordLen > maxChars) {
                result += "\n";
                lineLen = 0;
            } else if (!result.empty() && result.back() != '\n') {
                result += " ";
                lineLen++;
            }
            result += word;
            lineLen += wordLen;
            word.clear();
            wordLen = 0;
            if (c == '\n') {
                result += "\n";
                lineLen = 0;
            }
        } else {
            word += c;
            if (((unsigned char)c & 0xC0) != 0x80) ++wordLen;
        }
    }
    if (!word.empty()) {
        if (lineLen + wordLen > maxChars && !result.empty() && result.back() != '\n') {
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
    
    // Координаты привязаны к левой колонке HUD (см. drawHud): панель СТАТУС
    // ИГРОКА стоит на y=84, строка квоты внутри неё — на y+45.
    int ax = 0, ay = 0;
    if (state.vnState.arrowTarget == 1) { ax = 350; ay = 94; }
    else if (state.vnState.arrowTarget == 2) { ax = 350; ay = 148; }
    else if (state.vnState.arrowTarget == 3) { ax = 350; ay = 278; }
    else if (state.vnState.arrowTarget == 4) { ax = screenW / 2 + 150; ay = 100; }
    else if (state.vnState.arrowTarget == 5) { ax = 350; ay = 250; }
    else if (state.vnState.arrowTarget == 6) { ax = 350; ay = 126; }
    
    if (ax > 0 && ay > 0) {
        if ((SDL_GetTicks() / 300) % 2 == 0) {
            drawText(renderer, ax, ay, "<-- TARGET", {255, 100, 100, 255}, 2);
        }
    }
}

}

