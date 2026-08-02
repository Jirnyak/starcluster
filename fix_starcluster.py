import sys

def modify_localdraw():
    with open('localdraw.cpp', 'r') as f:
        lines = f.readlines()
    
    # 1. Remove renderStarPlasma (lines 196 to 379 approx)
    # Let's find the start and end by string matching
    start_idx = -1
    end_idx = -1
    for i, line in enumerate(lines):
        if 'static void renderStarPlasma(' in line:
            # Go up to the comment
            start_idx = i - 6
        if 'SDL_RenderCopy(renderer, tex, &src, &dst);' in line and start_idx != -1:
            end_idx = i + 2 # include closing brace
            break
            
    if start_idx != -1 and end_idx != -1:
        del lines[start_idx:end_idx]
        print(f"Removed renderStarPlasma: {end_idx - start_idx} lines")
        
    # 2. Replace the star rendering logic in renderLocalScene
    start_idx = -1
    end_idx = -1
    for i, line in enumerate(lines):
        if '(3) ЗВЕЗДА — ИЗОТРОПНАЯ АНАЛИТИЧЕСКАЯ СФЕРА' in line:
            start_idx = i
        if 'strokeCircle(renderer, sp.x, sp.y, std::min(3200, int(sr * 1.9 * gp)),' in line and start_idx != -1:
            end_idx = i + 3
            break

    if start_idx != -1 and end_idx != -1:
        new_block = """    // (3) ЗВЕЗДА — ОТРИСОВКА ПРИМИТИВАМИ
    //     Быстрый рендер через простые круги для орто-карты и перспективы,
    //     решает проблему лагов от софтверного рейтрейсера и квадратных краев.
    if (scene.hasStar && !sp.behind) {
        int sr = std::min(2000, std::max(3, radiusPx(scene.starRadius, sp.depth, view)));
        double gp = 1.0 + 0.04 * std::sin(scene.fxClock * 2.0);
        fillCircle(renderer, sp.x, sp.y, sr, rgba(scene.starR, scene.starG, scene.starB, 255));
        strokeCircle(renderer, sp.x, sp.y, std::min(2600, int(sr * 1.4 * gp)),
                     rgba(scene.starR, scene.starG, scene.starB, 70));
        strokeCircle(renderer, sp.x, sp.y, std::min(3200, int(sr * 1.9 * gp)),
                     rgba(scene.starR, scene.starG, scene.starB, 34));
    }
"""
        lines[start_idx:end_idx] = [new_block]
        print("Replaced renderLocalScene star logic")

    with open('localdraw.cpp', 'w') as f:
        f.writelines(lines)

modify_localdraw()
