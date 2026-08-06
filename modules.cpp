#include "modules.h"
#include "game.h"
#include "ship.h"
#include "drive.h"
#include "colony.h"
#include <algorithm>
#include <string>

// Таблица модулей корабля (plans_1). Бонусы запекаются в поля корабля при
// установке; список ship.modules хранит индексы для отображения и сохранения.

const std::vector<ModuleDef>& moduleDefs() {
    //                 name                slot               price    mass  cargo  propVol fuelVol  spd    acc  heavy light armor hull  util  rig drive SY  blurb
    static const std::vector<ModuleDef> defs = {
        {"Cargo Pod I",       ModuleSlot::Cargo,    900.0,   6.0,  40.0,    0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0,  0.0, -1, 0, "+40 cargo capacity"},
        {"Propellant Tank I", ModuleSlot::Fuel,     700.0,   4.0,   0.0,  140.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0,  0.0, -1, 0, "+140 propellant volume"},
        {"Bunker Pod I",      ModuleSlot::Fuel,     900.0,   3.0,   0.0,    0.0,  22.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0,  0.0, -1, 0, "+22 fuel bunker volume"},
        {"Point Defense",     ModuleSlot::Weapon,  1200.0,   3.0,   0.0,    0.0,   0.0, 0.0,  0.0,   0.0,  8.0,  0.0,   0.0,  0.0,  0.0, -1, 0, "+8 light weapons"},
        {"Ablative Plating I",ModuleSlot::Defense, 1100.0,   8.0,   0.0,    0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  6.0,  40.0,  0.0,  0.0, -1, 0, "+6 armor, +40 hull"},
        {"Survey Array",      ModuleSlot::Sensor,  1400.0,   2.0,   0.0,    0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  6.0,  0.0, -1, 0, "+6 sensors (utility)"},
        // (§20.11) Буровые. Ставятся в Special, потому что это не оружие и не сенсор:
        // отдельный потребитель мощности реактора. Дают ТЕМП (тонн в час) при неизменной
        // цене тонны — грызёшь быстрее, но и топлива в час жжёшь ровно во столько же раз
        // больше. Дешёвая ступень намеренно доступна с верфи 0: бур — это то, чем игрок
        // выбирается из нищеты, в том числе на спасательной капсуле.
        {"Mining Laser I",    ModuleSlot::Special,  800.0,   5.0,   0.0,    0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0,  2.0, -1, 0, "+2x drilling rate"},

        {"Cargo Pod II",      ModuleSlot::Cargo,   3200.0,  12.0, 120.0,    0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0,  0.0, -1, 1, "+120 cargo capacity"},
        {"Mining Laser II",   ModuleSlot::Special, 4600.0,  11.0,   0.0,    0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0,  6.0, -1, 1, "+6x drilling rate"},
        {"Propellant Tank II",ModuleSlot::Fuel,    2800.0,  10.0,   0.0,  480.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0,  0.0, -1, 1, "+480 propellant volume"},
        {"Bunker Pod II",     ModuleSlot::Fuel,    3400.0,   8.0,   0.0,    0.0,  75.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0,  0.0, -1, 1, "+75 fuel bunker volume"},
        {"Railgun Battery",   ModuleSlot::Weapon,  5200.0,  10.0,   0.0,    0.0,   0.0, 0.0,  0.0,  20.0,  0.0,  0.0,   0.0,  0.0,  0.0, -1, 1, "+20 heavy weapons"},
        {"Composite Hull",    ModuleSlot::Defense, 4800.0,  16.0,   0.0,    0.0,   0.0, 0.0,  0.0,   0.0,  0.0, 18.0, 140.0,  0.0,  0.0, -1, 1, "+18 armor, +140 hull"},

        {"Cargo Hold III",    ModuleSlot::Cargo,  12000.0,  30.0, 400.0,    0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0,  0.0, -1, 2, "+400 cargo capacity"},
        {"Ore Refinery Rig",  ModuleSlot::Special,19000.0,  34.0,   0.0,    0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0, 18.0, -1, 2, "+18x drilling rate"},
        {"Deep-Field Scanner",ModuleSlot::Sensor,  9000.0,   4.0,   0.0,    0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0, 24.0,  0.0, -1, 2, "+24 sensors (utility)"},
        {"Aegis Bulwark",     ModuleSlot::Defense,18000.0,  40.0,   0.0,    0.0,   0.0, 0.0,  0.0,   0.0,  0.0, 80.0, 600.0,  0.0,  0.0, -1, 2, "+80 armor, +600 hull"},
        {"Spinal Lance",      ModuleSlot::Weapon, 22000.0,  24.0,   0.0,    0.0,   0.0, 0.0,  0.0,  90.0, 20.0,  0.0,   0.0,  0.0,  0.0, -1, 3, "+90 heavy, +20 light"},

        // --- Двигатели. Слот эксклюзивный: движок ровно один. Дефолтный
        // Thermal Core I стоит на корпусе бесплатно и слота НЕ занимает;
        // всё, что ниже, его вытесняет и занимает один слот апгрейда.
        {"Thermal Core II",   ModuleSlot::Drive,   6500.0,   6.0,   0.0,    0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0,  0.0,  1, 1, "hotter chamber: more exhaust speed"},
        {"Ion Grid I",        ModuleSlot::Drive,  12000.0,   5.0,   0.0,    0.0,   0.0, 0.0, -0.06, 0.0,  0.0,  0.0,   0.0,  0.0,  0.0,  3, 1, "electric: huge range, tiny thrust"},
        {"Thermal Core III",  ModuleSlot::Drive,  34000.0,  14.0,   0.0,    0.0,   0.0, 0.0,  0.02,  0.0,  0.0,  0.0,   0.0,  0.0,  0.0,  2, 2, "refractory chamber, wide fuel range"},
        {"Ion Grid II",       ModuleSlot::Drive,  58000.0,  11.0,   0.0,    0.0,   0.0, 0.0, -0.05, 0.0,  0.0,  0.0,   0.0,  0.0,  0.0,  4, 2, "high voltage: extreme range"},
        {"Fusion Torch",      ModuleSlot::Drive, 260000.0,  26.0,   0.0,    0.0,   0.0, 0.05, 0.06,  0.0,  0.0,  0.0,   0.0,  0.0,  0.0,  5, 3, "fuel is the exhaust; no propellant tank"},
    };
    return defs;
}

bool moduleSlotIsExclusive(ModuleSlot slot) {
    return slot == ModuleSlot::Drive;
}

const char* moduleSlotLabel(ModuleSlot slot) {
    switch (slot) {
        case ModuleSlot::Drive: return "DRIVE";
        case ModuleSlot::Cargo: return "CARGO";
        case ModuleSlot::Fuel: return "FUEL";
        case ModuleSlot::Weapon: return "WEAPON";
        case ModuleSlot::Defense: return "DEFENSE";
        case ModuleSlot::Sensor: return "SENSOR";
        default: return "SPECIAL";
    }
}

void applyModuleToShip(Ship& ship, const ModuleDef& def) {
    ship.dryMass += def.mass;
    ship.cargoCapacity += def.cargoBonus;
    ship.propellantVolume += def.propellantVolumeBonus;
    ship.fuelVolume += def.fuelVolumeBonus;
    if (def.slot == ModuleSlot::Drive && def.driveIndex >= 0) ship.driveIndex = def.driveIndex;
    ship.speed = std::min(0.5, ship.speed + def.speedBonus);
    ship.acceleration += def.accelBonus;
    ship.heavyWeapons += def.heavyBonus;
    ship.lightWeapons += def.lightBonus;
    ship.armor += def.armorBonus;
    ship.maxHullHP += def.hullBonus;
    ship.hullHP = std::min(ship.maxHullHP, ship.hullHP + def.hullBonus);
    ship.utility += def.utilityBonus;
    ship.miningRig += def.miningBonus;
}

int Game::shipyardLevelAtStar(int starIndex) const {
    if (starIndex < 0 || starIndex >= int(cluster.stars.size())) return 0;
    int level = 0;
    const std::string& role = cluster.stars[starIndex].economyRole;
    if (role == "shipyard") level = 3;
    else if (role == "military" || role == "refinery") level = 1;
    for (size_t i = 0; i < colonies.size(); ++i) {
        if (colonies[i].starIndex == starIndex) level = std::max(level, colonies[i].shipyardLevel);
    }
    return level;
}

void removeModuleFromShip(Ship& ship, const ModuleDef& def) {
    ship.dryMass -= def.mass;
    ship.cargoCapacity -= def.cargoBonus;
    ship.propellantVolume = std::max(1.0, ship.propellantVolume - def.propellantVolumeBonus);
    ship.fuelVolume = std::max(1.0, ship.fuelVolume - def.fuelVolumeBonus);
    // Снятый движок возвращает корпус на дефолтный, а лишнее из баков выливается.
    if (def.slot == ModuleSlot::Drive) ship.driveIndex = defaultDriveIndex();
    // Ёмкость упала — лишнее выливаем за борт пропорционально составу смеси.
    shipTrimTanks(ship);
    ship.speed = std::max(0.1, ship.speed - def.speedBonus);
    ship.acceleration -= def.accelBonus;
    ship.heavyWeapons -= def.heavyBonus;
    ship.lightWeapons -= def.lightBonus;
    ship.armor -= def.armorBonus;
    ship.maxHullHP -= def.hullBonus;
    ship.hullHP = std::min(ship.maxHullHP, ship.hullHP);
    ship.utility -= def.utilityBonus;
    ship.miningRig = std::max(0.0, ship.miningRig - def.miningBonus);
}

bool Game::buyModule(int agentIndex, int defIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    const std::vector<ModuleDef>& defs = moduleDefs();
    if (defIndex < 0 || defIndex >= int(defs.size())) return false;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute) { lastEvent = "cannot refit in transit"; return false; }
    const ModuleDef& def = defs[defIndex];
    if (shipyardLevelAtStar(agent.currentStar) < def.minShipyard) {
        lastEvent = "need shipyard lvl " + std::to_string(def.minShipyard) + " for " + def.name;
        pushNews(lastEvent, 0);
        return false;
    }
    if (agent.money < def.price) { lastEvent = "not enough credits for " + def.name; return false; }
    if (shipCargoMass(agent.ship) + def.mass > agent.ship.cargoCapacity) {
        lastEvent = "not enough cargo space for " + def.name;
        pushNews(lastEvent, 0);
        return false;
    }
    
    agent.money -= def.price;
    std::string modName = "Module: " + def.name;
    bool found = false;
    for (auto& r : agent.ship.cargo) {
        if (r.element == modName) {
            r.amount += 1.0;
            found = true;
            break;
        }
    }
    if (!found) {
        agent.ship.cargo.push_back(Resource(modName, 1.0));
    }
    
    agent.lastAction = "bought " + def.name;
    lastEvent = "bought " + def.name;
    pushNews("Bought " + def.name, 4);
    return true;
}

bool Game::equipModule(int agentIndex, int defIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    const std::vector<ModuleDef>& defs = moduleDefs();
    if (defIndex < 0 || defIndex >= int(defs.size())) return false;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute) { lastEvent = "cannot refit in transit"; return false; }
    
    const ModuleDef& def = defs[defIndex];

    // Эксклюзивный слот (движок): новый вытесняет старый, а не суммируется с ним.
    // Раньше проверялось только общее число модулей, поэтому на корабль можно
    // было навесить три двигателя сразу и сложить их бонусы.
    //
    // ВСЕ проверки идут ДО снятия старого модуля. Иначе неудачная замена
    // (не та верфь, модуля нет в трюме) снимала работающий движок и оставляла
    // корабль вообще без двигателя.
    int replaceListIndex = -1;
    if (moduleSlotIsExclusive(def.slot)) {
        for (size_t i = 0; i < agent.ship.modules.size(); ++i) {
            const int existing = agent.ship.modules[i];
            if (existing < 0 || existing >= int(defs.size())) continue;
            if (defs[existing].slot != def.slot) continue;
            if (existing == defIndex) {
                lastEvent = def.name + " already installed";
                return false;
            }
            replaceListIndex = int(i);
            break;
        }
    }

    // Замена освобождает слот, поэтому упираемся в лимит только при добавлении.
    if (replaceListIndex < 0 && int(agent.ship.modules.size()) >= agent.ship.maxModules) {
        lastEvent = "no upgrade slots available";
        pushNews(lastEvent, 0);
        return false;
    }

    if (shipyardLevelAtStar(agent.currentStar) < def.minShipyard) {
        lastEvent = "need shipyard lvl " + std::to_string(def.minShipyard) + " for " + def.name;
        pushNews(lastEvent, 0);
        return false;
    }

    std::string modName = "Module: " + def.name;
    int cargoSlot = -1;
    for (size_t i = 0; i < agent.ship.cargo.size(); ++i) {
        if (agent.ship.cargo[i].element == modName && agent.ship.cargo[i].amount > 0.0) {
            cargoSlot = int(i);
            break;
        }
    }
    if (cargoSlot < 0) {
        lastEvent = def.name + " not in cargo";
        return false;
    }

    // Проверки пройдены — только теперь трогаем корабль.
    if (replaceListIndex >= 0 && !unequipModule(agentIndex, replaceListIndex)) return false;

    // unequipModule мог вернуть снятый модуль в трюм и сдвинуть индексы.
    cargoSlot = -1;
    for (size_t i = 0; i < agent.ship.cargo.size(); ++i) {
        if (agent.ship.cargo[i].element == modName && agent.ship.cargo[i].amount > 0.0) {
            cargoSlot = int(i);
            break;
        }
    }
    if (cargoSlot < 0) return false;
    agent.ship.cargo[cargoSlot].amount -= 1.0;
    if (agent.ship.cargo[cargoSlot].amount <= 0.0) {
        agent.ship.cargo.erase(agent.ship.cargo.begin() + cargoSlot);
    }

    agent.ship.modules.push_back(defIndex);
    applyModuleToShip(agent.ship, def);
    
    agent.lastAction = "equipped " + def.name;
    lastEvent = "equipped " + def.name;
    return true;
}

bool Game::unequipModule(int agentIndex, int moduleListIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return false;
    Agent& agent = agents[agentIndex];
    if (agent.ship.enRoute) { lastEvent = "cannot refit in transit"; return false; }
    if (moduleListIndex < 0 || moduleListIndex >= int(agent.ship.modules.size())) return false;
    
    const std::vector<ModuleDef>& defs = moduleDefs();
    int defIndex = agent.ship.modules[moduleListIndex];
    const ModuleDef& def = defs[defIndex];
    
    if (shipyardLevelAtStar(agent.currentStar) < def.minShipyard) {
        lastEvent = "need shipyard lvl " + std::to_string(def.minShipyard) + " to remove " + def.name;
        pushNews(lastEvent, 0);
        return false;
    }
    
    if (shipCargoMass(agent.ship) + def.mass > agent.ship.cargoCapacity) {
        lastEvent = "not enough cargo space to unequip " + def.name;
        pushNews(lastEvent, 0);
        return false;
    }
    
    removeModuleFromShip(agent.ship, def);
    agent.ship.modules.erase(agent.ship.modules.begin() + moduleListIndex);
    
    std::string modName = "Module: " + def.name;
    bool found = false;
    for (auto& r : agent.ship.cargo) {
        if (r.element == modName) {
            r.amount += 1.0;
            found = true;
            break;
        }
    }
    if (!found) {
        agent.ship.cargo.push_back(Resource(modName, 1.0));
    }
    
    agent.lastAction = "unequipped " + def.name;
    lastEvent = "unequipped " + def.name;
    return true;
}
