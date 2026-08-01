import sys

with open("ui.cpp", "r") as f:
    content = f.read()

target = """    if (contains(layout.contracts, mouseX, mouseY)) {
    if (contains(layout.cargo, mouseX, mouseY)) {"""

replacement = """    if (contains(layout.contracts, mouseX, mouseY)) {
        if (game.playerCanOpenContractsAt(window.star)) openContractsWindow(state, window.star, screenW, screenH);
        return true;
    }
    if (contains(layout.colony, mouseX, mouseY)) {
        if (game.playerAtStar(window.star)) {
            bool hasColony = window.star < int(game.colonies.size()) && game.colonies[window.star].population > 0;
            if (hasColony) {
                // handle colony interact
            } else {
                // handle colonize (if colony items)
            }
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
    }"""

content = content.replace(target, replacement)

with open("ui.cpp", "w") as f:
    f.write(content)
