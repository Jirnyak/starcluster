import sys

with open("ui.cpp", "r") as f:
    content = f.read()

old_code = """                vn.tutorialStep = 100;
                vn.targetText = getTutorialText(game, 8, vn.arrowTarget, vn.tutorialCompleted); // dummy bool
                bool dummy;
                std::string p1 = getTutorialText(game, 8, vn.arrowTarget, dummy);
                std::string p2 = getTutorialText(game, 9, vn.arrowTarget, dummy);
                vn.targetText = p1 + " " + p2;
                vn.textProgress = 0.0f;
                vn.currentText = "";"""

new_code = """                vn.tutorialStep = 100;
                bool dummy;
                vn.targetText = getTutorialText(game, 100, vn.arrowTarget, dummy);
                vn.textProgress = 0.0f;
                vn.currentText = "";"""

content = content.replace(old_code, new_code)

with open("ui.cpp", "w") as f:
    f.write(content)

