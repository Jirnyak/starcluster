#include "faction.h"

namespace {

double clampDouble(double value, double low, double high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

double clamp01(double value) {
    return clampDouble(value, 0.0, 1.0);
}

double clampPressure(double value) {
    return clampDouble(value, -1.0, 1.0);
}

double positivePressure(double value) {
    const double clamped = clampPressure(value);
    return clamped > 0.0 ? clamped : 0.0;
}

double budgetWeight(double base, double pressurePenalty) {
    const double weight = base - pressurePenalty;
    return weight > 0.05 ? weight : 0.05;
}

} // namespace

FactionOrder::FactionOrder() {}

FactionOrder::FactionOrder(FactionOrderType type_, int originStar_, int targetStar_, double priority_, double createdAt_)
    : type(type_),
      originStar(originStar_),
      targetStar(targetStar_),
      priority(priority_),
      createdAt(createdAt_) {}

Faction::Faction(const std::string& name_)
    : name(name_) {}

Faction::Faction(const std::string& name_, int colorR_, int colorG_, int colorB_)
    : name(name_), colorR(colorR_), colorG(colorG_), colorB(colorB_) {}

double factionStrategicTreasury(const Faction& faction) {
    const double knownTreasury = faction.estimatedTreasury > 0.0 ? faction.estimatedTreasury : faction.treasury;
    return knownTreasury > 0.0 ? knownTreasury : 0.0;
}

FactionBudgetPlan computeFactionBudgetPlan(const Faction& faction) {
    FactionBudgetPlan plan;
    const double capital = factionStrategicTreasury(faction);
    if (capital <= 0.0) return plan;

    const double aggression = clamp01(faction.aggression);
    const double risk = clamp01(faction.riskTolerance);
    const double tradeBias = clamp01(faction.tradeBias);
    const double expansionBias = clamp01(faction.expansionBias);
    const double defenseBias = clamp01(faction.defenseBias);
    const double border = positivePressure(faction.borderPressure);
    const double raids = positivePressure(faction.raidPressure);
    const double trade = positivePressure(faction.tradePressure);
    const double diplomacy = positivePressure(faction.diplomacyPressure);

    const double militaryWeight = budgetWeight(
        0.60 + aggression * 1.20 + defenseBias * 1.10 + border * 0.90 + raids * 1.10,
        diplomacy * 0.25);
    const double tradeWeight = budgetWeight(
        0.60 + tradeBias * 1.40 + trade * 1.10 + risk * 0.30,
        raids * 0.35 + border * 0.15);
    const double colonyWeight = budgetWeight(
        0.60 + expansionBias * 1.30 + risk * 0.25,
        raids * 0.45 + border * 0.25);

    const double totalWeight = militaryWeight + tradeWeight + colonyWeight;
    if (totalWeight <= 0.0) return plan;

    plan.military = capital * militaryWeight / totalWeight;
    plan.trade = capital * tradeWeight / totalWeight;
    plan.colony = capital * colonyWeight / totalWeight;
    return plan;
}

void normalizeFactionStrategicFields(Faction& faction) {
    faction.aggression = clamp01(faction.aggression);
    faction.riskTolerance = clamp01(faction.riskTolerance);
    faction.tradeBias = clamp01(faction.tradeBias);
    faction.expansionBias = clamp01(faction.expansionBias);
    faction.defenseBias = clamp01(faction.defenseBias);
    faction.diplomacyPressure = clampPressure(faction.diplomacyPressure);
    faction.borderPressure = clampPressure(faction.borderPressure);
    faction.raidPressure = clampPressure(faction.raidPressure);
    faction.tradePressure = clampPressure(faction.tradePressure);

    const FactionBudgetPlan plan = computeFactionBudgetPlan(faction);
    faction.militaryBudget = plan.military;
    faction.tradeBudget = plan.trade;
    faction.colonyBudget = plan.colony;
}

FactionRelationTension classifyFactionRelationTension(int relation) {
    if (relation <= -96) return FactionRelationTension::Enemy;
    if (relation <= -48) return FactionRelationTension::Hostile;
    if (relation < -16) return FactionRelationTension::Tense;
    if (relation <= 16) return FactionRelationTension::Neutral;
    if (relation < 80) return FactionRelationTension::Friendly;
    return FactionRelationTension::Ally;
}

double factionRelationTension01(int relation) {
    const double clamped = clampDouble(static_cast<double>(relation), -128.0, 128.0);
    return (128.0 - clamped) / 256.0;
}
