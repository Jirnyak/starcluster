import sys

with open("ui.cpp", "r") as f:
    content = f.read()

import re

# Find the start of the function
start_idx = content.find("bool handleSystemWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int screenW, int screenH, int mouseX, int mouseY) {")
if start_idx == -1:
    print("Could not find start_idx")
    sys.exit(1)

# Find the next function
end_idx = content.find("SDL_Rect contractButtonRect(const Window& window, int row) {", start_idx)
if end_idx == -1:
    print("Could not find end_idx")
    sys.exit(1)

new_func = """bool handleSystemWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int screenW, int screenH, int mouseX, int mouseY) {
    const SystemLayout layout = systemLayout(window);
    if (contains(layout.route, mouseX, mouseY)) {
        // Explicitly matched with GO implementation
        if (window.star >= 0) {
            bool success = game.commandAgentToStar(game.playerAgent, window.star);
            printf("DEBUG UI: GO button clicked. Target star: %d. Success: %s\\n", window.star, success ? "true" : "false");
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
        if (playerMarketStar(game) == window.star) openTradeWindow(state, window.star, screenW, screenH);
        return true;
    }
    if (contains(layout.contracts, mouseX, mouseY)) {
        if (game.playerCanOpenContractsAt(window.star)) openContractsWindow(state, window.star, screenW, screenH);
        return true;
    }
    if (contains(layout.colony, mouseX, mouseY)) {
        if (game.playerAtStar(window.star) && game.playerFoundColony()) {
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
    if (contains(layout.shipFit, mouseX, mouseY)) {
        if (game.playerAgent >= 0) openShipFitWindow(state, window.star, screenW, screenH);
        return true;
    }
    return true;
}

"""

content = content[:start_idx] + new_func + content[end_idx:]

with open("ui.cpp", "w") as f:
    f.write(content)
