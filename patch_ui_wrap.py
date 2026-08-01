import sys

with open("ui.cpp", "r") as f:
    content = f.read()

# 1. Add wrapText before drawVisualNovel
insert_idx = content.find("void drawVisualNovel")
if insert_idx == -1:
    print("Could not find drawVisualNovel")
    sys.exit(1)

wrap_func = """std::string wrapText(const std::string& text, int maxChars) {
    std::string result;
    int lineLen = 0;
    std::string word;
    
    for (char c : text) {
        if (c == ' ' || c == '\\n') {
            if (lineLen + word.length() > maxChars) {
                result += "\\n";
                lineLen = 0;
            } else if (!result.empty() && result.back() != '\\n') {
                result += " ";
                lineLen++;
            }
            result += word;
            lineLen += word.length();
            word.clear();
            if (c == '\\n') {
                result += "\\n";
                lineLen = 0;
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        if (lineLen + word.length() > maxChars && !result.empty() && result.back() != '\\n') {
            result += "\\n";
        } else if (!result.empty() && result.back() != '\\n') {
            result += " ";
        }
        result += word;
    }
    return result;
}

"""
content = content[:insert_idx] + wrap_func + content[insert_idx:]

# 2. Modify drawText call in drawVisualNovel
old_draw = "drawText(renderer, boxX + 20, boxY + 60, state.vnState.currentText, {214, 228, 238, 255}, 2);"
new_draw = """int maxChars = (boxW - 40) / 12;
    std::string wrapped = wrapText(state.vnState.currentText, maxChars);
    drawText(renderer, boxX + 20, boxY + 60, wrapped, {214, 228, 238, 255}, 2);"""
content = content.replace(old_draw, new_draw)

# 3. Change System Info window position
old_rect = """    } else {
        rect.w = 448;
        rect.h = 286;
        rect.x = std::max(352, std::min(screenW - rect.w - 280, 372 + cascade * 22));
        rect.y = 82 + cascade * 22;
    }"""
new_rect = """    } else if (kind == WindowKind::SystemInfo) {
        rect.w = 448;
        rect.h = 286;
        rect.x = std::max(352, std::min(screenW - rect.w - 280, 372 + cascade * 22));
        rect.y = std::max(100, screenH - rect.h - 150 - cascade * 22);
    } else {
        rect.w = 448;
        rect.h = 286;
        rect.x = std::max(352, std::min(screenW - rect.w - 280, 372 + cascade * 22));
        rect.y = 82 + cascade * 22;
    }"""
content = content.replace(old_rect, new_rect)

# 4. Change Trade window position
old_trade = """    if (kind == WindowKind::Trade) {
        rect.w = std::min(1000, std::max(740, screenW - 220));
        rect.h = std::min(520, std::max(420, screenH - 180));
        rect.x = std::max(300, (screenW - rect.w) / 2 + cascade * 18);
        rect.y = std::max(70, screenH - rect.h - 28 - cascade * 10);"""
new_trade = """    if (kind == WindowKind::Trade) {
        rect.w = std::min(1000, std::max(740, screenW - 220));
        rect.h = std::min(520, std::max(420, screenH - 180));
        rect.x = std::max(300, (screenW - rect.w) / 2 + cascade * 18);
        rect.y = std::max(20, 20 + cascade * 10);"""
content = content.replace(old_trade, new_trade)

with open("ui.cpp", "w") as f:
    f.write(content)

