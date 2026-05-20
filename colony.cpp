#include "colony.h"
#include <algorithm>

namespace {

bool colonyRoleIs(const std::string& role, const char* value) {
    return role == value;
}

double roleAutomationBonus(const std::string& role) {
    if (colonyRoleIs(role, "research") || colonyRoleIs(role, "shipyard")) return 0.18;
    if (colonyRoleIs(role, "refinery")) return 0.14;
    if (colonyRoleIs(role, "military")) return 0.08;
    return 0.05;
}

double roleMarketAccess(const std::string& role) {
    if (colonyRoleIs(role, "frontier")) return 0.55;
    if (colonyRoleIs(role, "military")) return 0.72;
    if (colonyRoleIs(role, "refinery") || colonyRoleIs(role, "shipyard")) return 0.92;
    return 0.85;
}

double roleDefenseBonus(const std::string& role) {
    if (colonyRoleIs(role, "military")) return 2.4;
    if (colonyRoleIs(role, "shipyard")) return 1.1;
    if (colonyRoleIs(role, "frontier")) return 0.4;
    return 0.0;
}

int initialShipyardLevel(const std::string& role, double infrastructure) {
    if (!colonyRoleIs(role, "shipyard")) return 0;
    return std::max(1, int(infrastructure / 1.4));
}

double reduceToFloor(double value, double scale, double floor) {
    if (value <= floor) return value;
    return std::max(floor, value * scale);
}

ColonyConstructionEffect preferredConstructionEffect(const Colony& colony) {
    if (colonyRoleIs(colony.role, "shipyard") && colony.shipyardLevel < 3) return ColonyConstructionEffect::Shipyard;
    if (colony.automation < 0.55) return ColonyConstructionEffect::Automation;
    return ColonyConstructionEffect::Defense;
}

void initializeColonyState(Colony& colony) {
    const double populationScale = double(colony.population) * 0.00002;
    colony.automation = std::max(0.0, colony.infrastructure * 0.06 + roleAutomationBonus(colony.role));
    colony.energyCapacity = std::max(1.0, colony.infrastructure * 28.0 + populationScale * 12.0 + colony.automation * 18.0);
    colony.defense = std::max(0.0, colony.infrastructure * 0.45 + populationScale + roleDefenseBonus(colony.role));
    colony.shipyardLevel = initialShipyardLevel(colony.role, colony.infrastructure);
    colony.marketAccess = roleMarketAccess(colony.role);
    colony.damage = 0.0;
    colony.localLedger = std::max(0.0, colony.infrastructure * 120.0 + double(colony.population) * 0.01);
    colony.stockpileValue = 0.0;
}

}

ConstructionItem::ConstructionItem() {}

ConstructionItem::ConstructionItem(const std::string& name_, double cost_)
    : name(name_), cost(cost_), effect(colonyConstructionEffectFromName(name_)) {}

ConstructionItem::ConstructionItem(ColonyConstructionEffect effect_, double cost_)
    : name(colonyConstructionEffectName(effect_)), cost(cost_), effect(effect_) {}

ConstructionItem::ConstructionItem(const std::string& name_, double cost_, ColonyConstructionEffect effect_)
    : name(name_), cost(cost_), effect(effect_) {}

Colony::Colony(const std::string& name_, size_t population_, const std::string& role_)
    : name(name_), population(population_), role(role_) {
    initializeColonyState(*this);
}

Colony::Colony(const std::string& name_, size_t population_, const std::string& role_, int starIndex_, int ownerFaction_, double infrastructure_)
    : name(name_), population(population_), role(role_), starIndex(starIndex_), ownerFaction(ownerFaction_), infrastructure(infrastructure_) {
    initializeColonyState(*this);
}

ColonyConstructionEffect colonyConstructionEffectFromName(const std::string& name) {
    if (name == "SHIPYARD") return ColonyConstructionEffect::Shipyard;
    if (name == "AUTOMATION") return ColonyConstructionEffect::Automation;
    if (name == "DEFENSE") return ColonyConstructionEffect::Defense;
    return ColonyConstructionEffect::None;
}

ColonyConstructionEffect colonyConstructionEffect(const ConstructionItem& item) {
    if (item.effect != ColonyConstructionEffect::None) return item.effect;
    return colonyConstructionEffectFromName(item.name);
}

const char* colonyConstructionEffectName(ColonyConstructionEffect effect) {
    switch (effect) {
    case ColonyConstructionEffect::Shipyard:
        return "SHIPYARD";
    case ColonyConstructionEffect::Automation:
        return "AUTOMATION";
    case ColonyConstructionEffect::Defense:
        return "DEFENSE";
    case ColonyConstructionEffect::None:
        break;
    }
    return "";
}

ColonyConstructionEffect colonySuggestedConstructionEffect(const Colony& colony) {
    return preferredConstructionEffect(colony);
}

double colonyConstructionCost(const Colony& colony, ColonyConstructionEffect effect) {
    switch (effect) {
    case ColonyConstructionEffect::Shipyard:
        return 900.0 + colony.shipyardLevel * 700.0;
    case ColonyConstructionEffect::Automation:
        return 620.0 + colony.automation * 900.0;
    case ColonyConstructionEffect::Defense:
        return 520.0 + colony.defense * 90.0;
    case ColonyConstructionEffect::None:
        break;
    }
    return 0.0;
}

ConstructionItem colonyConstructionItem(ColonyConstructionEffect effect, double cost) {
    return ConstructionItem(effect, cost);
}

ConstructionItem colonySuggestedConstruction(const Colony& colony) {
    const ColonyConstructionEffect effect = colonySuggestedConstructionEffect(colony);
    return colonyConstructionItem(effect, colonyConstructionCost(colony, effect));
}

bool colonyApplyConstructionEffect(Colony& colony, const ConstructionItem& item) {
    bool knownEffect = true;
    switch (colonyConstructionEffect(item)) {
    case ColonyConstructionEffect::Shipyard:
        colony.shipyardLevel += 1;
        break;
    case ColonyConstructionEffect::Automation:
        colony.automation += 0.08;
        break;
    case ColonyConstructionEffect::Defense:
        colony.defense += 0.8;
        break;
    case ColonyConstructionEffect::None:
        knownEffect = false;
        break;
    }
    colony.infrastructure += 0.04;
    return knownEffect;
}

void colonyApplyRaidDamage(Colony& colony, double severity) {
    const double s = std::max(0.0, std::min(1.0, severity));
    if (s <= 0.0) return;

    if (colony.damage < 0.95) {
        colony.damage = std::min(0.95, std::max(0.0, colony.damage) + s * 0.28);
    }
    colony.marketAccess = reduceToFloor(colony.marketAccess, 1.0 - s * 0.35, 0.12);
    colony.localLedger = reduceToFloor(colony.localLedger, 1.0 - s * 0.45, 0.0);
    colony.defense = reduceToFloor(colony.defense, 1.0 - s * 0.40, 0.0);
    colony.automation = reduceToFloor(colony.automation, 1.0 - s * 0.08, 0.0);
    colony.energyCapacity = reduceToFloor(colony.energyCapacity, 1.0 - s * 0.10, 1.0);
    colony.infrastructure = reduceToFloor(colony.infrastructure, 1.0 - s * 0.04, 0.25);
}

size_t colonyQueueCount(const Colony& colony) {
    return colony.constructionQueue.size();
}

std::string colonyQueueLabel(const Colony& colony) {
    if (colony.constructionQueue.empty()) return "IDLE";
    const ConstructionItem& item = colony.constructionQueue.front();
    const ColonyConstructionEffect effect = colonyConstructionEffect(item);
    if (effect != ColonyConstructionEffect::None) return colonyConstructionEffectName(effect);
    if (!item.name.empty()) return item.name;
    return "CUSTOM";
}

double colonyQueueProgress(const Colony& colony) {
    if (colony.constructionQueue.empty()) return 0.0;
    const ConstructionItem& item = colony.constructionQueue.front();
    if (item.cost <= 0.0) return 1.0;
    return std::max(0.0, std::min(1.0, item.progress / item.cost));
}

int colonyShipHiringCapacity(const Colony& colony) {
    if (colony.shipyardLevel <= 0) return 0;
    const double health = std::max(0.0, 1.0 - colony.damage);
    const int levelSlots = colony.shipyardLevel * 2;
    const int infrastructureSlots = int(std::max(0.0, colony.infrastructure) / 3.0);
    const int energySlots = int(std::max(0.0, colony.energyCapacity) / 80.0);
    return std::max(0, int((levelSlots + infrastructureSlots + energySlots) * health));
}

bool colonyHasShipyardCapacity(const Colony& colony) {
    return colonyShipHiringCapacity(colony) > 0;
}
