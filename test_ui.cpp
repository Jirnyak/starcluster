#include <SDL2/SDL.h>
#include <iostream>

#include "ui.h"
#include "game.h"
#include "local.h"

int main() {
    Game game;
    game.init(100);
    UI::WindowState ui;
    UI::openSystemWindow(ui, 0, 1280, 720);

    // Initial window rect
    SDL_Rect r = ui.windows[0].rect;
    std::cout << "Window: " << r.x << "," << r.y << " " << r.w << "x" << r.h << "\n";
    
    // Simulate close button click
    int cx = r.x + r.w - 22 + 8; // middle of close button
    int cy = r.y + 4 + 8;
    
    UI::HudSelection sel;
    bool handled = UI::handleMouseDown(ui, game, sel, 1280, 720, cx, cy, SDL_BUTTON_LEFT);
    std::cout << "Handled: " << handled << "\n";
    std::cout << "Windows count: " << ui.windows.size() << "\n";
    
    return 0;
}
