import sys

with open("ui.cpp", "r") as f:
    content = f.read()

sig = "std::string getTutorialText(const Game& game, int step, int& outArrowTarget, bool& outOpenTrade) {"
first_idx = content.find(sig)
second_idx = content.find(sig, first_idx + 1)

if second_idx != -1:
    end_idx = content.find("    return \"\";\n}", second_idx)
    if end_idx != -1:
        end_idx += len("    return \"\";\n}")
        content = content[:second_idx] + content[end_idx:]

with open("ui.cpp", "w") as f:
    f.write(content)

