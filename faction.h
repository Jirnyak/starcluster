#pragma once
#include <string>
#include <vector>

enum class FactionOrderType {
    Trade,
    Patrol,
    Colonize,
    Scout,
    Raid,
    AttackSystem,
    DefendSystem,
    Courier
};

enum class FactionRelationTension {
    Enemy,
    Hostile,
    Tense,
    Neutral,
    Friendly,
    Ally
};

struct FactionOrder {
    FactionOrderType type = FactionOrderType::Scout;
    int originStar = -1;
    int targetStar = -1;
    int targetFaction = -1;
    double priority = 0.0;
    double createdAt = 0.0;
    int assignedAgent = -1;
    bool completed = false;

    FactionOrder();
    FactionOrder(FactionOrderType type_, int originStar_, int targetStar_, double priority_, double createdAt_);
};

struct FactionBudgetPlan {
    double military = 0.0;
    double trade = 0.0;
    double colony = 0.0;
};

// Фракция
class Faction {
public:
    std::string name;
    int colorR = 255;
    int colorG = 255;
    int colorB = 255;
    int homeStar = -1;
    double treasury = 0.0;
    double estimatedTreasury = 0.0;
    double militaryBudget = 0.0;
    double tradeBudget = 0.0;
    double colonyBudget = 0.0;
    double strength = 1.0;
    double aggression = 0.5;
    double riskTolerance = 0.5;
    double tradeBias = 0.5;
    double expansionBias = 0.5;
    double defenseBias = 0.5;
    double diplomacyPressure = 0.0;
    double borderPressure = 0.0;
    double raidPressure = 0.0;
    double tradePressure = 0.0;
    int relationRowOffset = -1;
    std::vector<int> controlledStars; // Индексы звёзд
    std::vector<int> fleetAgents; // Индексы агентов
    std::vector<FactionOrder> orders;
    Faction(const std::string& name_);
    Faction(const std::string& name_, int colorR_, int colorG_, int colorB_);
};

double factionStrategicTreasury(const Faction& faction);
FactionBudgetPlan computeFactionBudgetPlan(const Faction& faction);
void normalizeFactionStrategicFields(Faction& faction);
FactionRelationTension classifyFactionRelationTension(int relation);
double factionRelationTension01(int relation);
