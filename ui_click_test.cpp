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
    // Баки под пробку: тест кликов не про дозаправку.
    {
        Ship& s = game.agents[game.playerAgent].ship;
        s.fuelVolume = 1.0e9;
        s.propellantVolume = 1.0e9;
        s.fuel.clear();
        s.fuel.push_back(Resource("Th", 1.0e6));
        s.propellant.clear();
        s.propellant.push_back(Resource("H", 1.0e6));
    }

    UI::WindowState ui;
    UI::openSystemWindow(ui, starA, SCREEN_W, SCREEN_H);
    UI::openSystemWindow(ui, starB, SCREEN_W, SCREEN_H);

    const Overlap p = buttonPointUnderLowerWindow(ui, routeRect);
    check(p.coveredByLower, "DESTINATION of top window sits over the lower window (bug precondition)");

    UI::HudSelection sel;
    UI::handleMouseDown(ui, game, sel, SCREEN_W, SCREEN_H, p.x, p.y, SDL_BUTTON_LEFT);

    // Кнопка НАЗНАЧАЕТ цель и не стартует: вылет отдельным GO на карте, чтобы
    // между выбором системы и стартом можно было настроить двигатель под неё.
    const Agent& player = game.agents[game.playerAgent];
    check(player.destStar == starB, "DESTINATION targets the TOP window's star");
    check(!player.ship.enRoute, "DESTINATION does not depart by itself");

    // Окно остаётся открытым: цель выбрана, дальше игрок настраивает режим.
    bool topStillOpen = false;
    for (const UI::Window& w : ui.windows) {
        if (w.kind == UI::WindowKind::SystemInfo && w.star == starB) topStillOpen = true;
    }
    check(topStillOpen, "setting a destination keeps the window open");

    // И только теперь GO на карте действительно отправляет корабль.
    check(game.commandAgentToStar(game.playerAgent, player.destStar), "map GO departs to the destination");
    check(player.ship.enRoute && player.ship.targetStar == starB, "journey targets the destination");
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

    // Баки под пробку: тест кликов не про дозаправку.
    {
        Ship& s = game.agents[game.playerAgent].ship;
        s.fuelVolume = 1.0e9;
        s.propellantVolume = 1.0e9;
        s.fuel.clear();
        s.fuel.push_back(Resource("Th", 1.0e6));
        s.propellant.clear();
        s.propellant.push_back(Resource("H", 1.0e6));
    }

    // Каскад сдвигает верхнее окно на +22,+22, поэтому у нижнего открыта левая
    // полоса. Берём GO нижнего окна в этой открытой части.
    const SDL_Rect lower = ui.windows[0].rect;
    const SDL_Rect upper = ui.windows[1].rect;
    const SDL_Rect button = routeRect(lower);
    const int x = button.x + 2;
    const int y = button.y + button.h / 2;
    check(contains(button, x, y), "test point is on the lower window's DESTINATION button");
    check(!contains(upper, x, y), "that point is not covered by the top window");

    UI::HudSelection sel;
    UI::handleMouseDown(ui, game, sel, SCREEN_W, SCREEN_H, x, y, SDL_BUTTON_LEFT);

    const Agent& player = game.agents[game.playerAgent];
    check(player.destStar == starA,
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


// --- Окно HOLD: стрелки перелива ---------------------------------------------
// Два круга правок подряд интерфейс перелива был нерабочим: сначала кнопки
// всегда серые (ёмкость держала один сорт), потом кликабельные строки БЕЗ
// единой видимой кнопки. Тест бьёт ровно по геометрии стрелок и проверяет,
// что вещество поехало в нужную сторону.

// Зеркалит UI::holdArrowRect() — если разъедется, тест это и поймает.
SDL_Rect holdArrow(const SDL_Rect& win, int column, int row, bool left) {
    const int TITLE = 22;
    const int COL_W = 232, ROW_H = 18, ARROW_W = 18;
    const int colX = win.x + 12 + column * COL_W;
    SDL_Rect r;
    r.x = left ? colX : colX + COL_W - 32 - ARROW_W;
    r.y = win.y + TITLE + 74 + row * ROW_H - 2;
    r.w = ARROW_W;
    r.h = ROW_H - 3;
    return r;
}

// Зеркалит UI::holdJettisonRect().
SDL_Rect holdJettison(const SDL_Rect& win, int row) {
    const int TITLE = 22;
    const int COL_W = 232, ROW_H = 18, ARROW_W = 18;
    const int colX = win.x + 12 + 1 * COL_W;
    SDL_Rect r;
    r.x = colX + COL_W - 32 - ARROW_W - 22;
    r.y = win.y + TITLE + 74 + row * ROW_H - 2;
    r.w = 20;
    r.h = ROW_H - 3;
    return r;
}

void clickHold(UI::WindowState& ui, Game& game, const SDL_Rect& win, int column, int row, bool left) {
    UI::HudSelection sel;
    const SDL_Rect a = holdArrow(win, column, row, left);
    UI::handleMouseDown(ui, game, sel, SCREEN_W, SCREEN_H, a.x + a.w / 2, a.y + a.h / 2, SDL_BUTTON_LEFT);
}

double amountOf(const std::vector<Resource>& list, const char* symbol) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].element == symbol) return list[i].amount;
    }
    return 0.0;
}

void testHoldArrowsMoveMatter() {
    Game game;
    game.init(200);
    if (game.playerAgent < 0) { check(false, "player agent exists"); return; }

    Ship& ship = game.agents[game.playerAgent].ship;
    ship.cargoCapacity = 1.0e5;
    ship.fuelVolume = 1.0e4;
    ship.propellantVolume = 1.0e4;
    ship.fuel.clear();
    ship.propellant.clear();
    ship.cargo.clear();
    ship.cargo.push_back(Resource("Xe", 20.0));   // строка 0 трюма

    UI::WindowState ui;
    UI::openCargoWindow(ui, game.agents[game.playerAgent].currentStar, SCREEN_W, SCREEN_H);
    const SDL_Rect win = ui.windows[0].rect;

    // Колонки: 0 = бункер, 1 = трюм, 2 = бак.
    clickHold(ui, game, win, 1, 0, true);    // трюм: стрелка влево -> в бункер
    check(amountOf(ship.fuel, "Xe") > 0.0, "cargo LEFT arrow loads the bunker");

    clickHold(ui, game, win, 0, 0, false);   // бункер: стрелка вправо -> обратно в трюм
    check(amountOf(ship.fuel, "Xe") == 0.0 && amountOf(ship.cargo, "Xe") > 0.0,
          "bunker RIGHT arrow drains back to cargo");

    clickHold(ui, game, win, 1, 0, false);   // трюм: стрелка вправо -> в бак
    check(amountOf(ship.propellant, "Xe") > 0.0, "cargo RIGHT arrow loads the tank");

    clickHold(ui, game, win, 2, 0, true);    // бак: стрелка влево -> обратно в трюм
    check(amountOf(ship.propellant, "Xe") == 0.0 && amountOf(ship.cargo, "Xe") > 0.0,
          "tank LEFT arrow drains back to cargo");

    // Сброс за борт — выход из перегруза вдали от рынка.
    {
        UI::HudSelection sel;
        const SDL_Rect j = holdJettison(win, 0);
        UI::handleMouseDown(ui, game, sel, SCREEN_W, SCREEN_H, j.x + j.w / 2, j.y + j.h / 2, SDL_BUTTON_LEFT);
    }
    check(amountOf(ship.cargo, "Xe") == 0.0, "cargo X button jettisons the row");

    // Ручка режима: щелчок по левой и правой трети шкалы. Зеркалит
    // UI::holdThrottleRect(); разъедется — тест это и поймает.
    SDL_Rect thr;
    thr.x = win.x + win.w - 168;
    thr.y = win.y + win.h - 78;
    thr.w = 116;
    thr.h = 12;
    {
        UI::HudSelection sel;
        UI::handleMouseDown(ui, game, sel, SCREEN_W, SCREEN_H, thr.x + 4, thr.y + 6, SDL_BUTTON_LEFT);
    }
    const double low = ship.throttle;
    {
        UI::HudSelection sel;
        UI::handleMouseDown(ui, game, sel, SCREEN_W, SCREEN_H, thr.x + thr.w - 8, thr.y + 6, SDL_BUTTON_LEFT);
    }
    const double high = ship.throttle;
    check(low < 0.15 && high > 0.85, "throttle bar sets the drive mode by click position");

    // Шкала крейсерской скорости — зеркалит UI::holdCruiseRect() (throttle + 32).
    SDL_Rect cru = thr;
    cru.y += 32;
    {
        UI::HudSelection sel;
        UI::handleMouseDown(ui, game, sel, SCREEN_W, SCREEN_H, cru.x + 4, cru.y + 6, SDL_BUTTON_LEFT);
    }
    const double slow = ship.cruiseFraction;
    {
        UI::HudSelection sel;
        UI::handleMouseDown(ui, game, sel, SCREEN_W, SCREEN_H, cru.x + cru.w - 8, cru.y + 6, SDL_BUTTON_LEFT);
    }
    const double fast = ship.cruiseFraction;
    check(slow < 0.3 && fast > 0.9, "cruise bar sets the cruise speed by click position");

    // Кнопка OPTIMAL — зеркалит UI::holdOptimalRect().
    SDL_Rect opt;
    opt.x = win.x + win.w - 210;
    opt.y = win.y + win.h - 28;
    opt.w = 88;
    opt.h = 22;
    game.agents[game.playerAgent].destStar = -1;
    game.lastEvent.clear();
    {
        UI::HudSelection sel;
        UI::handleMouseDown(ui, game, sel, SCREEN_W, SCREEN_H, opt.x + opt.w / 2, opt.y + opt.h / 2, SDL_BUTTON_LEFT);
    }
    check(game.lastEvent.find("no destination") != std::string::npos,
          "OPTIMAL without a destination explains itself");
}

int main() {
    testCargoButtonGoesToTopWindow();
    testGoButtonGoesToTopWindow();
    testUncoveredLowerWindowStillReceivesClicks();
    testReopenRaisesExistingWindow();
    testHoldArrowsMoveMatter();
    std::printf("%s (%d failures)\n", gFailures == 0 ? "PASS" : "FAILED", gFailures);
    return gFailures == 0 ? 0 : 1;
}
