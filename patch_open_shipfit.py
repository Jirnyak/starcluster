import sys

with open("ui.cpp", "r") as f:
    content = f.read()

target = "void openShipyardWindow(WindowState& state, int starIndex, int x, int y) {"
code_to_add = """void openShipFitWindow(WindowState& state, int starIndex, int screenW, int screenH) {
    int cascade = 0;
    for (auto& w : state.windows) {
        if (w.kind == WindowKind::ShipFit) {
            state.activeId = w.id;
            return;
        }
        if (w.kind == WindowKind::Cargo || w.kind == WindowKind::ShipFit) cascade++;
    }
    Window w;
    w.id = state.nextId++;
    w.kind = WindowKind::ShipFit;
    w.star = starIndex;
    w.rect = defaultWindowRect(WindowKind::ShipFit, screenW, screenH, cascade);
    state.windows.push_back(w);
    state.activeId = w.id;
}

"""
content = content.replace(target, code_to_add + target)

with open("ui.cpp", "w") as f:
    f.write(content)
