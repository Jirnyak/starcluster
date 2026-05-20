#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "resource.h"

enum class ColonyConstructionEffect {
    None,
    Shipyard,
    Automation,
    Defense
};

struct ConstructionItem {
    std::string name;
    double cost = 0.0;
    double progress = 0.0;
    ColonyConstructionEffect effect = ColonyConstructionEffect::None;

    ConstructionItem();
    ConstructionItem(const std::string& name_, double cost_);
    ConstructionItem(ColonyConstructionEffect effect_, double cost_);
    ConstructionItem(const std::string& name_, double cost_, ColonyConstructionEffect effect_);
};

// Колония на звезде
class Colony {
public:
    std::string name;
    size_t population;
    std::string role; // habitat/refinery/shipyard/research/military/frontier
    std::vector<Resource> stockpile; // Запасы ресурсов
    int starIndex = -1;
    int ownerFaction = -1;
    double infrastructure = 1.0;
    double growth = 0.01;
    double automation = 0.0;
    double energyCapacity = 0.0;
    double defense = 0.0;
    int shipyardLevel = 0;
    double marketAccess = 1.0;
    double damage = 0.0;
    double localLedger = 0.0;
    double stockpileValue = 0.0;
    std::vector<ConstructionItem> constructionQueue;

    Colony(const std::string& name_, size_t population_, const std::string& role_);
    Colony(const std::string& name_, size_t population_, const std::string& role_, int starIndex_, int ownerFaction_, double infrastructure_);
};

ColonyConstructionEffect colonyConstructionEffectFromName(const std::string& name);
ColonyConstructionEffect colonyConstructionEffect(const ConstructionItem& item);
const char* colonyConstructionEffectName(ColonyConstructionEffect effect);
ColonyConstructionEffect colonySuggestedConstructionEffect(const Colony& colony);
double colonyConstructionCost(const Colony& colony, ColonyConstructionEffect effect);
ConstructionItem colonyConstructionItem(ColonyConstructionEffect effect, double cost);
ConstructionItem colonySuggestedConstruction(const Colony& colony);
bool colonyApplyConstructionEffect(Colony& colony, const ConstructionItem& item);
void colonyApplyRaidDamage(Colony& colony, double severity);

size_t colonyQueueCount(const Colony& colony);
std::string colonyQueueLabel(const Colony& colony);
double colonyQueueProgress(const Colony& colony);

int colonyShipHiringCapacity(const Colony& colony);
bool colonyHasShipyardCapacity(const Colony& colony);
