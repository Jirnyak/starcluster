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
    bool showHelp = true;   // легенда хоткеев (F1); по умолчанию видна новичку
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
    Exchange
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
};

struct WindowState {
    std::vector<Window> windows;
    int nextId = 1;
    int activeId = -1;
    int draggingId = -1;
    std::string tradeAmount;
    bool tradeAmountEditing = false;
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
void closeWindow(WindowState& state, int id);
bool handleMouseDown(WindowState& state, Game& game, HudSelection& selection, int screenW, int screenH, int mouseX, int mouseY, int button);
void handleMouseMove(WindowState& state, int screenW, int screenH, int mouseX, int mouseY);
void handleMouseUp(WindowState& state);
void handleTextInput(WindowState& state, const char* text);
bool handleKeyDown(WindowState& state, SDL_Keycode key);
void drawHud(SDL_Renderer* renderer, const Game& game, int screenW, int screenH, const HudSelection& selection);
void drawWindows(SDL_Renderer* renderer, const Game& game, int screenW, int screenH, const HudSelection& selection, const WindowState& state);
void drawStarPanel(SDL_Renderer* renderer, const Game& game, int starIndex, int elementIndex, int x, int y, int w);
void drawAgentPanel(SDL_Renderer* renderer, const Game& game, int agentIndex, int x, int y, int w);
void drawFactionPanel(SDL_Renderer* renderer, const Game& game, int x, int y, int w);
void drawControlHints(SDL_Renderer* renderer, int screenW, int screenH);
bool advanceVisualNovel(WindowState& state, Game& game, int winW, int winH);
void updateVisualNovel(WindowState& state, Game& game, double dt, int screenW, int screenH);
void drawVisualNovel(SDL_Renderer* renderer, const WindowState& state, int screenW, int screenH, SDL_Texture* tex);

}
