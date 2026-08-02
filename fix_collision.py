import sys

def modify_localsim():
    with open('localsim.cpp', 'r') as f:
        lines = f.readlines()
        
    start_idx = -1
    end_idx = -1
    
    for i, line in enumerate(lines):
        if '// --- Урон от короны звезды (игрок и NPC) ---' in line:
            start_idx = i
        if '// Убираем «мёртвые» снаряды (swap-pop) — каждый субшаг.' in line and start_idx != -1:
            end_idx = i - 1  # preserve empty line before it
            break
            
    if start_idx != -1 and end_idx != -1:
        new_block = """        // --- Урон от столкновения с небесными телами (Звезда и Планеты) ---
        auto isInsideBody = [&](double x, double y, double z) {
            if (scene.hasStar && scene.starIndex >= 0) {
                if (x*x + y*y + z*z < scene.starRadius * scene.starRadius) return true;
            }
            for (const auto& bd : scene.bodies) {
                double dx = x - bd.x, dy = y - bd.y, dz = z - bd.z;
                if (dx*dx + dy*dy + dz*dz < bd.radius * bd.radius) return true;
            }
            return false;
        };

        double dmgRate = 1200.0 * h; // очень быстрое разрушение при столкновении
        
        // Игрок
        if (playerValid && !scene.playerDestroyed && isInsideBody(scene.px, scene.py, scene.pz)) {
            Ship& ps = game.agents[game.playerAgent].ship;
            double before = ps.hullHP;
            applyDamage(scene.pShield, scene.pShieldTimer, ps.hullHP, dmgRate);
            if (ps.hullHP < before) {
                scene.playerHitFlash = 1.0;
                scene.toast = "TERRAIN COLLISION"; scene.toastTimer = 0.5;
                scene.shake = std::min(40.0, std::max(scene.shake, 12.0));
                if (before > 0.0 && ps.hullHP <= 0.0) {
                    scene.playerDestroyed = true;
                    std::mt19937 lr((uint32_t)(scene.fx.size() * 2654435761u) ^ (uint32_t)(uint64_t)(scene.localHours * 1000.0));
                    std::uniform_real_distribution<double> us(-1.0, 1.0);
                    std::uniform_real_distribution<double> u01(0.0, 1.0);
                    if ((int)scene.fx.size() < LocalCfg::FX_MAX) {
                        LocalFx f; f.x = scene.px; f.y = scene.py; f.z = scene.pz;
                        f.kind = FX_RING; f.size = 30.0; f.life = 1.0; f.maxLife = 1.0;
                        f.r = 255; f.g = 200; f.b = 120; f.a = 255; scene.fx.push_back(f);
                    }
                    for (int k = 0; k < 14; ++k) {
                        if ((int)scene.fx.size() >= LocalCfg::FX_MAX) break;
                        LocalFx f; f.x = scene.px; f.y = scene.py; f.z = scene.pz;
                        double sp = 25.0 + u01(lr) * 75.0;
                        f.vx = scene.pvx + us(lr) * sp; f.vy = scene.pvy + us(lr) * sp; f.vz = scene.pvz + us(lr) * sp * 0.6;
                        f.kind = FX_DEBRIS; f.size = 1.2 + u01(lr) * 2.0; f.life = 1.4 + u01(lr) * 0.8; f.maxLife = f.life;
                        if (u01(lr) < 0.5) { f.r = 120; f.g = 180; f.b = 255; } else { f.r = 255; f.g = (uint8_t)(160.0 + u01(lr) * 80.0); f.b = 70; }
                        f.a = 255; scene.fx.push_back(f);
                    }
                    scene.shake = 28.0;
                    scene.toast = "FATAL COLLISION"; scene.toastTimer = 3.0;
                }
            }
        }
        
        // NPC
        for (size_t i = 0; i < scene.craft.size(); ++i) {
            LocalCraft& c = scene.craft[i];
            if (c.hullHP <= 0.0) continue;
            if (isInsideBody(c.x, c.y, c.z)) {
                double before = c.hullHP;
                applyDamage(c.shield, c.shieldRegenTimer, c.hullHP, dmgRate);
                if (before > 0.0 && c.hullHP <= 0.0) {
                    spawnWreck(c.x, c.y, c.z, c.vx, c.vy, c.vz, c.r, c.g, c.b);
                }
            }
        }
"""
        lines[start_idx:end_idx] = [new_block]
        print(f"Replaced collision logic: {end_idx - start_idx} lines")

    with open('localsim.cpp', 'w') as f:
        f.writelines(lines)

modify_localsim()
