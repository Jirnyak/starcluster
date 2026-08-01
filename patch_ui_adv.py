import sys

with open("ui.cpp", "r") as f:
    content = f.read()

insert_idx = content.find("void updateVisualNovel")

if insert_idx == -1:
    print("Could not find updateVisualNovel")
    sys.exit(1)

new_code = """
bool advanceVisualNovel(WindowState& state, Game& game, int winW, int winH) {
    auto& vn = state.vnState;
    if (!vn.active) return false;
    
    if (vn.textProgress < vn.targetText.length()) {
        vn.textProgress = vn.targetText.length();
        vn.currentText = vn.targetText;
    } else {
        if (!vn.tutorialCompleted) {
            vn.tutorialStep++;
            if (vn.tutorialStep > 12) {
                vn.tutorialCompleted = true;
                vn.active = false;
            } else {
                bool openTrade = false;
                vn.targetText = getTutorialText(game, vn.tutorialStep, vn.arrowTarget, openTrade);
                vn.textProgress = 0.0f;
                if (openTrade && game.playerAgent >= 0 && game.playerAgent < (int)game.agents.size()) {
                    openTradeWindow(state, game.agents[game.playerAgent].currentStar, winW, winH);
                }
            }
        } else {
            vn.active = false;
        }
    }
    return true;
}

"""

content = content[:insert_idx] + new_code + content[insert_idx:]

with open("ui.cpp", "w") as f:
    f.write(content)

