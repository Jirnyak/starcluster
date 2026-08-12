#pragma once

#include "game.h"
#include <SDL.h>
#include <string>
#include <unordered_set>
#include <vector>

namespace UI {

struct HudSelection {
    int star = -1;
    int agent = -1;
    int element = 0;
    bool paused = false;
    bool followAgent = false;
    bool showHelp = false;  // карточка управления (F1); по умолчанию свёрнута —
                            // её показывает оболочка перед стартом партии
    double simSpeed = 1.0;
    double simYearsPerSecond = 1.0;
};

enum class WindowKind {
    SystemInfo,
    Trade,
    Contracts,
    Shipyard,
    Cargo,
    ShipFit,
    Transactions,
    Exchange,
    Colony,
    Exotics
};

struct Window {
    int id = 0;
    WindowKind kind = WindowKind::SystemInfo;
    int star = -1;
    SDL_Rect rect = {0, 0, 0, 0};
    bool dragging = false;
    int dragX = 0;
    int dragY = 0;
    int scrollOffset = 0;
};

struct VisualNovelState {
    bool active = false;
    bool tutorialCompleted = false;
    int tutorialStep = 0;
    float textProgress = 0.0f;
    std::string targetText = "";
    std::string currentText = "";
    std::unordered_set<int> visitedSystems;
    int arrowTarget = 0;
    // Совет обучения (шаги 10 и 11) считается ОДИН раз и живёт здесь ЦЕЛИКОМ.
    // Реплики «берите X» и «продайте в Y» обязаны говорить про ОДИН рейс, а между
    // ними проходит столько времени, сколько игрок читает: мир идёт, цены ползут.
    // Пересчёт на втором шаге назвал бы другой товар — или, хуже, оставил бы имя
    // системы от первого расчёта, а цифры взял от второго.
    TradeRun hintRun;
    bool hintComputed = false;
    // (§37.2) Про хайтек-этаж Тимертия говорит НЕ во вступлении, а тогда, когда
    // игрок впервые причалил туда, где этот рынок есть. Вступление и так самое
    // длинное в игре (28 реплик), а знание об экзотике до первого её появления
    // бесполезно и потому не запоминается.
    bool exoticIntroDone = false;
};

struct WindowState {
    std::vector<Window> windows;
    // Кэш биржевой сводки. Пересчёт при полной разведке стоит ~40 мс — это четыре
    // кадра, поэтому считаем не каждый кадр, а по изменению обстановки (см.
    // updateExchangeBoard). Здесь же живёт прокрутка списка.
    std::vector<ArbitrageDeal> exchangeBoard;
    int exchangeStar = -1;
    double exchangeBuiltAt = -1.0e18;
    int exchangeElement = -1;      // фильтр: -1 = все элементы, иначе индекс элемента
    int exchangeBoardElement = -1; // с каким фильтром собран кэш
    bool exchangeTable = false;    // показывать таблицу Менделеева вместо списка
    // Биржа держав (§33): тот же приём, что и таблица Менделеева, — не новое
    // окно, а другой режим доски. Место одно и то же: сюда игрок и так ходит
    // за лицензией и счётом, а акции — ровно про те же деньги.
    bool exchangeShares = false;
    int shareFaction = -1;         // выделенная держава в списке акций
    int nextId = 1;
    int activeId = -1;
    int draggingId = -1;
    std::string tradeAmount;
    bool tradeAmountEditing = false;
    // Переименование своей системы (§25): индекс звезды, чьё имя правится
    // прямо сейчас, и набранная основа. -1 — никто не правится.
    int renameStar = -1;
    std::string renameText;
    int confirmBuyShipIndex = -1;
    VisualNovelState vnState;
};

void openSystemWindow(WindowState& state, int starIndex, int screenW, int screenH);
void openTradeWindow(WindowState& state, int starIndex, int screenW, int screenH);
void openContractsWindow(WindowState& state, int starIndex, int screenW, int screenH);
void openShipyardWindow(WindowState& state, int starIndex, int screenW, int screenH);
void openCargoWindow(WindowState& state, int starIndex, int screenW, int screenH);
void openShipFitWindow(WindowState& state, int starIndex, int screenW, int screenH);
void openTransactionsWindow(WindowState& state, int screenW, int screenH);
void openExchangeWindow(WindowState& state, int starIndex, int screenW, int screenH);
// Окно собственности: цена системы с разбором, покупка, касса колонии.
void openColonyWindow(WindowState& state, int starIndex, int screenW, int screenH);
// Хайтек-этаж (§31): рынок экзотической материи, переоснастка корпуса и
// кузница хромокоров. Открывается только там, где такой рынок вообще есть.
void openExoticsWindow(WindowState& state, int starIndex, int screenW, int screenH);
// Держит кэш сводки свежим. Зовётся каждый кадр, пересчитывает редко.
void updateExchangeBoard(WindowState& state, const Game& game);
// Прокрутка списков колесом мыши. true — колесо съедено окном (не зумим карту).
bool handleMouseWheel(WindowState& state, int mouseX, int mouseY, int dy);
void closeWindow(WindowState& state, int id);
bool handleMouseDown(WindowState& state, Game& game, HudSelection& selection, int screenW, int screenH, int mouseX, int mouseY, int button);
void handleMouseMove(WindowState& state, int screenW, int screenH, int mouseX, int mouseY);
void handleMouseUp(WindowState& state);
void handleTextInput(WindowState& state, const char* text);
// Game нужен, потому что Enter в поле имени системы СРАЗУ применяет
// переименование — иначе клавиатуре пришлось бы возвращать наружу «что нажали».
bool handleKeyDown(WindowState& state, Game& game, SDL_Keycode key);
void drawHud(SDL_Renderer* renderer, const Game& game, int screenW, int screenH, const HudSelection& selection);
void drawWindows(SDL_Renderer* renderer, const Game& game, int screenW, int screenH, const HudSelection& selection, const WindowState& state);
void drawStarPanel(SDL_Renderer* renderer, const Game& game, int starIndex, int elementIndex, int x, int y, int w);
void drawAgentPanel(SDL_Renderer* renderer, const Game& game, int agentIndex, int x, int y, int w);
void drawFactionPanel(SDL_Renderer* renderer, const Game& game, int x, int y, int w);
// Высота панели фракций. Наружу вынесена, потому что под ней стоит панель
// целей: `drawHud` считал её высоту СВОЕЙ формулой (44 + rows*19 против
// настоящих 44 + rows*30), промахивался на 66 пикселей при шести фракциях, и
// последняя строка репутации вместе со шкалой тира оказывалась перекрыта.
int factionPanelHeight(const Game& game);
// Карточка управления по центру экрана: F1 в игре и финальный экран оболочки.
void drawControlsCard(SDL_Renderer* renderer, int screenW, int screenH);
// Перенос по словам. Наружу вынесен ради скриншот-харнеса: он меряет, во
// сколько строк ложится реплика Тимертии, — коробка диалога держит только пять.
std::string wrapText(const std::string& text, int maxChars);
// Одна реплика Тимертии по номеру шага. Наружу вынесена РАДИ ХАРНЕСА: коробка
// диалога держит пять строк, и реплики вне ленты обучения (сводка по прибытии,
// просчёт по V, хайтек-лента §37.2) иначе никак не измерить — они включаются
// обстановкой, а не порядком нажатий.
std::string tutorialLine(const Game& game, int step);
// Число из поля ОБЪЁМ (пусто = «сколько влезет»). Наружу вынесено, чтобы
// клавиша `B` покупала ровно столько же, сколько кнопка BUY: до этого клавиша
// звала «на все деньги», а панель целей учила именно ей.
double requestedAmount(const WindowState& state);
// Клавиша V: гасит коробку, а если она погашена — ПЕРЕСЧИТЫВАЕТ сводку по
// нынешней обстановке и жжёт на это топливо реактора (§28). Во время обучения
// остаётся простым выключателем.
void toggleVisualNovel(WindowState& state, Game& game);
bool advanceVisualNovel(WindowState& state, Game& game, int winW, int winH);
void updateVisualNovel(WindowState& state, Game& game, double dt, int screenW, int screenH);
void drawVisualNovel(SDL_Renderer* renderer, const WindowState& state, int screenW, int screenH, SDL_Texture* tex);

}
