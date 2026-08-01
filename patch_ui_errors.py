import sys

with open("ui.cpp", "r") as f:
    content = f.read()

target1 = "bool handleTradeWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int mouseX, int mouseY, int button);"
replacement1 = "bool handleTradeWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int mouseX, int mouseY, int button, int screenW, int screenH);"
content = content.replace(target1, replacement1)

target2 = "bool handleTradeWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int mouseX, int mouseY, int button) {"
replacement2 = "bool handleTradeWindowMouseDown(WindowState& state, Game& game, const Window& window, HudSelection& selection, int mouseX, int mouseY, int button, int screenW, int screenH) {"
content = content.replace(target2, replacement2)

target3 = "if (w.kind == WindowKind::Trade && handleTradeWindowMouseDown(state, game, w, selection, mouseX, mouseY, button)) return true;"
replacement3 = "if (w.kind == WindowKind::Trade && handleTradeWindowMouseDown(state, game, w, selection, mouseX, mouseY, button, screenW, screenH)) return true;"
content = content.replace(target3, replacement3)

target4 = "game.installModule(game.playerAgent, i - nShips - 1);"
replacement4 = "game.buyModule(game.playerAgent, i - nShips - 1);"
content = content.replace(target4, replacement4)

with open("ui.cpp", "w") as f:
    f.write(content)
