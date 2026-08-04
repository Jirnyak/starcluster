// Регрессия на маршрутизацию кликов между окнами.
//
// Баг: handleMouseDown поднимал кликнутое окно на передний план, а потом отдавал
// клик ПЕРВОМУ окну в векторе, содержащему точку, то есть самому НИЖНЕМУ. Окна
// системы открываются каскадом со сдвигом 22px и сильно перекрываются, поэтому
// GO/TRADE/CARGO верхнего окна попадали в нижнее, там ни в одну кнопку не
// приходились и молча съедались. Отсюда «GO в окне системы срабатывает не всегда»,
// притом что GO в нижней общей панели идёт другим путём и работает стабильно.
//
// Тест кликает по кнопкам ВЕРХНЕГО окна в точках, которые заведомо накрыты
// нижним окном, и проверяет, что действие выполнилось для верхнего окна.

#include <SDL.h>

#include "game.h"
#include "ui.h"

#include <cstdio>

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++gFailures;
}

// Геометрия нижнего ряда кнопок окна системы — зеркалит UI::systemLayout().
SDL_Rect routeRect(const SDL_Rect& r) { return {r.x + 10, r.y + r.h - 38, 65, 24}; }
SDL_Rect cargoRect(const SDL_Rect& r) { return {r.x + 10 + 280, r.y + r.h - 38, 60, 24}; }

bool contains(const SDL_Rect& r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

const int SCREEN_W = 1280;
const int SCREEN_H = 720;

// Две звезды, отличные от текущей позиции игрока и друг от друга.
void pickStars(const Game& game, int& a, int& b) {
    const int here = game.agents[game.playerAgent].currentStar;
    a = -1;
    b = -1;
    for (int i = 0; i < int(game.cluster.stars.size()); ++i) {
        if (i == here) continue;
        if (a < 0) a = i;
        else if (b < 0) { b = i; break; }
    }
}

// Открывает два перекрывающихся окна системы и возвращает точку на кнопке
// верхнего окна, гарантированно накрытую нижним окном.
struct Overlap {
    int x = 0;
    int y = 0;
    bool coveredByLower = false;
};

Overlap buttonPointUnderLowerWindow(const UI::WindowState& ui, SDL_Rect (*rectOf)(const SDL_Rect&)) {
    const SDL_Rect lower = ui.windows[0].rect;
    const SDL_Rect upper = ui.windows[1].rect;
    const SDL_Rect button = rectOf(upper);
    Overlap p;
    p.x = button.x + button.w / 2;
    p.y = button.y + button.h / 2;
    p.coveredByLower = contains(lower, p.x, p.y);
    return p;
}

void testCargoButtonGoesToTopWindow() {
    Game game;
    game.init(200);
    if (game.playerAgent < 0) { check(false, "player agent exists"); return; }

    int starA = -1, starB = -1;
    pickStars(game, starA, starB);

    UI::WindowState ui;
    UI::openSystemWindow(ui, starA, SCREEN_W, SCREEN_H);
    UI::openSystemWindow(ui, starB, SCREEN_W, SCREEN_H);
    check(ui.windows.size() == 2, "two cascaded system windows open");
    check(ui.windows[1].star == starB, "second-opened window is on top");

    const Overlap p = buttonPointUnderLowerWindow(ui, cargoRect);
    check(p.coveredByLower, "CARGO of top window sits over the lower window (bug precondition)");

    UI::HudSelection sel;
    UI::handleMouseDown(ui, game, sel, SCREEN_W, SCREEN_H, p.x, p.y, SDL_BUTTON_LEFT);

    int cargoStar = -2;
    for (const UI::Window& w : ui.windows) {
        if (w.kind == UI::WindowKind::Cargo) cargoStar = w.star;
    }
    check(cargoStar != -2, "CARGO click opened a cargo window");
    check(cargoStar == starB, "cargo window belongs to the TOP window's star, not the covered one");
}

void testGoButtonGoesToTopWindow() {
    Game game;
    game.init(200);
    if (game.playerAgent < 0) { check(false, "player agent exists"); return; }

    int starA = -1, starB = -1;
    pickStars(game, starA, starB);

    // Убираем законные причины отказа (топливо), чтобы тест мерил только маршрутизацию клика.
    game.agents[game.playerAgent].ship.fuel = 1.0e6;

    UI::WindowState ui;
    UI::openSystemWindow(ui, starA, SCREEN_W, SCREEN_H);
    UI::openSystemWindow(ui, starB, SCREEN_W, SCREEN_H);

    const Overlap p = buttonPointUnderLowerWindow(ui, routeRect);
    check(p.coveredByLower, "GO of top window sits over the lower window (bug precondition)");

    UI::HudSelection sel;
    UI::handleMouseDown(ui, game, sel, SCREEN_W, SCREEN_H, p.x, p.y, SDL_BUTTON_LEFT);

    const Agent& player = game.agents[game.playerAgent];
    check(player.ship.enRoute, "GO started a journey");
    check(player.ship.targetStar == starB, "journey targets the TOP window's star");

    bool topStillOpen = false;
    for (const UI::Window& w : ui.windows) {
        if (w.kind == UI::WindowKind::SystemInfo && w.star == starB) topStillOpen = true;
    }
    check(!topStillOpen, "successful GO closed its own window");
}

// Клик по кнопке НИЖНЕГО окна там, где верхнее окно его не перекрывает,
// по-прежнему должен доходить до нижнего окна.
void testUncoveredLowerWindowStillReceivesClicks() {
    Game game;
    game.init(200);
    if (game.playerAgent < 0) { check(false, "player agent exists"); return; }

    int starA = -1, starB = -1;
    pickStars(game, starA, starB);

    UI::WindowState ui;
    UI::openSystemWindow(ui, starA, SCREEN_W, SCREEN_H);
    UI::openSystemWindow(ui, starB, SCREEN_W, SCREEN_H);

    game.agents[game.playerAgent].ship.fuel = 1.0e6;

    // Каскад сдвигает верхнее окно на +22,+22, поэтому у нижнего открыта левая
    // полоса. Берём GO нижнего окна в этой открытой части.
    const SDL_Rect lower = ui.windows[0].rect;
    const SDL_Rect upper = ui.windows[1].rect;
    const SDL_Rect button = routeRect(lower);
    const int x = button.x + 2;
    const int y = button.y + button.h / 2;
    check(contains(button, x, y), "test point is on the lower window's GO button");
    check(!contains(upper, x, y), "that point is not covered by the top window");

    UI::HudSelection sel;
    UI::handleMouseDown(ui, game, sel, SCREEN_W, SCREEN_H, x, y, SDL_BUTTON_LEFT);

    const Agent& player = game.agents[game.playerAgent];
    check(player.ship.enRoute && player.ship.targetStar == starA,
          "uncovered lower window still handles its own click");
}

// Повторное открытие уже существующего окна должно поднимать его наверх, иначе
// оно остаётся погребённым под окном системы и по нему невозможно кликнуть.
// Заодно гоняет под ASan путь, где bringWindowToFront инвалидирует итератор
// range-for внутри самих open*-функций.
void testReopenRaisesExistingWindow() {
    Game game;
    game.init(200);
    if (game.playerAgent < 0) { check(false, "player agent exists"); return; }

    const int here = game.agents[game.playerAgent].currentStar;

    UI::WindowState ui;
    UI::openShipyardWindow(ui, here, SCREEN_W, SCREEN_H);
    UI::openSystemWindow(ui, here, SCREEN_W, SCREEN_H);
    check(ui.windows.back().kind == UI::WindowKind::SystemInfo, "system window buried the shipyard");

    UI::openShipyardWindow(ui, here, SCREEN_W, SCREEN_H);
    check(ui.windows.size() == 2, "reopening did not duplicate the shipyard window");
    check(ui.windows.back().kind == UI::WindowKind::Shipyard, "reopening raised the shipyard to the front");
    check(ui.activeId == ui.windows.back().id, "activeId follows the raised window");

    UI::openTransactionsWindow(ui, SCREEN_W, SCREEN_H);
    UI::openSystemWindow(ui, here, SCREEN_W, SCREEN_H);
    UI::openTransactionsWindow(ui, SCREEN_W, SCREEN_H);
    check(ui.windows.back().kind == UI::WindowKind::Transactions, "reopening raised the transactions window");

    UI::openShipFitWindow(ui, here, SCREEN_W, SCREEN_H);
    UI::openSystemWindow(ui, here, SCREEN_W, SCREEN_H);
    UI::openShipFitWindow(ui, here, SCREEN_W, SCREEN_H);
    check(ui.windows.back().kind == UI::WindowKind::ShipFit, "reopening raised the shipfit window");
}

}  // namespace

int main() {
    testCargoButtonGoesToTopWindow();
    testGoButtonGoesToTopWindow();
    testUncoveredLowerWindowStillReceivesClicks();
    testReopenRaisesExistingWindow();
    std::printf("%s (%d failures)\n", gFailures == 0 ? "PASS" : "FAILED", gFailures);
    return gFailures == 0 ? 0 : 1;
}
