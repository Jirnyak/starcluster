import sys

with open("ui.cpp", "r") as f:
    content = f.read()

old_rect = """    } else if (kind == WindowKind::SystemInfo) {
        rect.w = 448;
        rect.h = 286;
        rect.x = std::max(352, std::min(screenW - rect.w - 280, 372 + cascade * 22));
        rect.y = std::max(100, screenH - rect.h - 150 - cascade * 22);
    } else {"""
new_rect = """    } else {"""
content = content.replace(old_rect, new_rect)

with open("ui.cpp", "w") as f:
    f.write(content)
