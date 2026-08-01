#include "modules.h"
#include "game.h"
#include "ship.h"
#include "colony.h"
#include <algorithm>
#include <string>

// Таблица модулей корабля (plans_1). Бонусы запекаются в поля корабля при
// установке; список ship.modules хранит индексы для отображения и сохранения.

const std::vector<ModuleDef>& moduleDefs() {
    //                 name                slot                price    mass  cargo  fuel  spd    acc   heavy light armor hull  util  SY  blurb
    static const std::vector<ModuleDef> defs = {
        {"Cargo Pod I",       ModuleSlot::Cargo,    900.0,   6.0,  40.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0, 0, "+40 cargo capacity"},
        {"Fuel Cell I",       ModuleSlot::Fuel,     700.0,   4.0,   0.0,1200.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0, 0, "+1200 fuel"},
        {"Tuned Injectors",   ModuleSlot::Drive,   1500.0,   2.0,   0.0,   0.0, 0.0,  0.05,  0.0,  0.0,  0.0,   0.0,  0.0, 0, "+accel"},
        {"Point Defense",     ModuleSlot::Weapon,  1200.0,   3.0,   0.0,   0.0, 0.0,  0.0,   0.0,  8.0,  0.0,   0.0,  0.0, 0, "+8 light weapons"},
        {"Ablative Plating I",ModuleSlot::Defense, 1100.0,   8.0,   0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  6.0,  40.0,  0.0, 0, "+6 armor, +40 hull"},
        {"Survey Array",      ModuleSlot::Sensor,  1400.0,   2.0,   0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  6.0, 0, "+6 sensors (utility)"},

        {"Cargo Pod II",      ModuleSlot::Cargo,   3200.0,  12.0, 120.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0, 1, "+120 cargo capacity"},
        {"Fusion Tank",       ModuleSlot::Fuel,    2800.0,  10.0,   0.0,4000.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0, 1, "+4000 fuel"},
        {"Ion Thruster",      ModuleSlot::Drive,   4200.0,   4.0,   0.0,   0.0, 0.03, 0.06,  0.0,  0.0,  0.0,   0.0,  0.0, 1, "+speed, +accel"},
        {"Railgun Battery",   ModuleSlot::Weapon,  5200.0,  10.0,   0.0,   0.0, 0.0,  0.0,  20.0,  0.0,  0.0,   0.0,  0.0, 1, "+20 heavy weapons"},
        {"Composite Hull",    ModuleSlot::Defense, 4800.0,  16.0,   0.0,   0.0, 0.0,  0.0,   0.0,  0.0, 18.0, 140.0,  0.0, 1, "+18 armor, +140 hull"},

        {"Cargo Hold III",    ModuleSlot::Cargo,  12000.0,  30.0, 400.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0,  0.0, 2, "+400 cargo capacity"},
        {"Deep-Field Scanner",ModuleSlot::Sensor,  9000.0,   4.0,   0.0,   0.0, 0.0,  0.0,   0.0,  0.0,  0.0,   0.0, 24.0, 2, "+24 sensors (utility)"},
        {"Aegis Bulwark",     ModuleSlot::Defense,18000.0,  40.0,   0.0,   0.0, 0.0,  0.0,   0.0,  0.0, 80.0, 600.0,  0.0, 2, "+80 armor, +600 hull"},
        {"Antimatter Drive",  ModuleSlot::Drive,  16000.0,   8.0,   0.0,   0.0, 0.06, 0.08,  0.0,  0.0,  0.0,   0.0,  0.0, 3, "+speed, +accel"},
        {"Spinal Lance",      ModuleSlot::Weapon, 22000.0,  24.0,   0.0,   0.0, 0.0,  0.0,  90.0, 20.0,  0.0,   0.0,  0.0, 3, "+90 heavy, +20 light"},
    };
    return defs;
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
    ship.fuelCapacity += def.fuelBonus;
    ship.fuel = std::min(ship.fuelCapacity, ship.fuel + def.fuelBonus);
    ship.speed = std::min(0.5, ship.speed + def.speedBonus);
    ship.acceleration += def.accelBonus;
    ship.heavyWeapons += def.heavyBonus;
    ship.lightWeapons += def.lightBonus;
    ship.armor += def.armorBonus;
    ship.maxHullHP += def.hullBonus;
    ship.hullHP = std::min(ship.maxHullHP, ship.hullHP + def.hullBonus);
    ship.utility += def.utilityBonus;
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
    ship.fuelCapacity -= def.fuelBonus;
    ship.fuel = std::min(ship.fuelCapacity, ship.fuel);
    ship.speed = std::max(0.1, ship.speed - def.speedBonus);
    ship.acceleration -= def.accelBonus;
    ship.heavyWeapons -= def.heavyBonus;
    ship.lightWeapons -= def.lightBonus;
    ship.armor -= def.armorBonus;
    ship.maxHullHP -= def.hullBonus;
    ship.hullHP = std::min(ship.maxHullHP, ship.hullHP);
    ship.utility -= def.utilityBonus;
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
    
    if (int(agent.ship.modules.size()) >= agent.ship.maxModules) {
        lastEvent = "no upgrade slots available";
        pushNews(lastEvent, 0);
        return false;
    }
    
    const ModuleDef& def = defs[defIndex];
    if (shipyardLevelAtStar(agent.currentStar) < def.minShipyard) {
        lastEvent = "need shipyard lvl " + std::to_string(def.minShipyard) + " for " + def.name;
        pushNews(lastEvent, 0);
        return false;
    }
    
    std::string modName = "Module: " + def.name;
    bool found = false;
    for (size_t i = 0; i < agent.ship.cargo.size(); ++i) {
        if (agent.ship.cargo[i].element == modName && agent.ship.cargo[i].amount > 0.0) {
            agent.ship.cargo[i].amount -= 1.0;
            if (agent.ship.cargo[i].amount <= 0.0) {
                agent.ship.cargo.erase(agent.ship.cargo.begin() + i);
            }
            found = true;
            break;
        }
    }
    
    if (!found) return false;
    
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
