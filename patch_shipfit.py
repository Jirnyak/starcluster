import sys

with open("ui.cpp", "r") as f:
    content = f.read()

# 1. Add handleShipFitWindowMouseDown
idx = content.find("bool handleCargoWindowMouseDown(")
if idx != -1:
    code_to_add = """bool handleShipFitWindowMouseDown(WindowState& state, Game& game, const Window& window, int mouseX, int mouseY) {
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

"""
    content = content[:idx] + code_to_add + content[idx:]

# 2. Add drawShipFitWindow
idx2 = content.find("void drawCargoWindow(")
if idx2 != -1:
    code_to_add2 = """void drawShipFitWindow(SDL_Renderer* renderer, const Game& game, const Window& window, bool active) {
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

"""
    content = content[:idx2] + code_to_add2 + content[idx2:]

# 3. Modify handleMouseDown
target3 = "if (w.kind == WindowKind::Cargo && handleCargoWindowMouseDown(game, w, mouseX, mouseY)) return true;"
replacement3 = target3 + "\n            if (w.kind == WindowKind::ShipFit && handleShipFitWindowMouseDown(state, game, w, mouseX, mouseY)) return true;"
content = content.replace(target3, replacement3)

# 4. Modify drawWindows
target4 = "} else if (window.kind == WindowKind::Cargo) {"
replacement4 = "} else if (window.kind == WindowKind::ShipFit) {\n            drawShipFitWindow(renderer, game, window, active);\n        " + target4
content = content.replace(target4, replacement4)

with open("ui.cpp", "w") as f:
    f.write(content)
