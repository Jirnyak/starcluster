#include "agent.h"

Agent::Agent(const std::string& type_, const Ship& ship_)
    : type(type_), ship(ship_) {}

double agentContractRoleWeight(const AgentRoleProfile& profile, ContractType type) {
    switch (type) {
    case ContractType::Delivery:
        return profile.deliveryWeight;
    case ContractType::Courier:
        return profile.courierWeight;
    case ContractType::Scout:
        return profile.scoutWeight;
    case ContractType::Bounty:
        return profile.bountyWeight;
    case ContractType::Escort:
        return profile.escortWeight;
    case ContractType::Raid:
        return profile.raidWeight;
    case ContractType::ColonySupply:
        return profile.colonySupplyWeight;
    }
    return 0.0;
}

AgentRoleProfile agentRoleProfile(const Agent& agent) {
    AgentRoleProfile profile;
    profile.tradeWeight = agent.tradeBias;
    profile.deliveryWeight = agent.tradeBias * 0.7;
    profile.courierWeight = agent.questBias;
    profile.scoutWeight = agent.scoutBias;
    profile.exploreWeight = agent.scoutBias * 0.8 + agent.questBias * 0.3;
    profile.raidWeight = agent.piracyBias;
    profile.bountyWeight = agent.piracyBias * 0.4;
    profile.escortWeight = agent.questBias * 0.35;
    profile.colonySupplyWeight = agent.tradeBias * 0.45;
    profile.riskTolerance = agent.riskTolerance;
    profile.fuelReserveBias = 0.5;
    profile.combatAvoidance = 1.0 - agent.riskTolerance;
    profile.opportunityCostBias = 1.0;

    const std::string& role = agent.type;
    if (agent.playerControlled || role == "player") {
        profile.tradeWeight += 0.5;
        profile.deliveryWeight += 0.5;
        profile.courierWeight += 0.4;
        profile.scoutWeight += 0.4;
        profile.exploreWeight += 0.5;
        profile.patrolWeight += 0.2;
        profile.raidWeight += 0.1;
        profile.bountyWeight += 0.3;
        profile.escortWeight += 0.4;
        profile.colonizeWeight += 0.1;
        profile.colonySupplyWeight += 0.3;
        profile.fuelReserveBias += 0.2;
        profile.opportunityCostBias = 0.9;
    } else if (role == "trader") {
        profile.tradeWeight += 1.2;
        profile.deliveryWeight += 1.0;
        profile.courierWeight += 0.4;
        profile.scoutWeight += 0.1;
        profile.exploreWeight += 0.1;
        profile.escortWeight += 0.1;
        profile.colonySupplyWeight += 0.8;
        profile.fuelReserveBias += 0.3;
        profile.combatAvoidance += 0.4;
        profile.opportunityCostBias = 1.2;
    } else if (role == "adventurer") {
        profile.tradeWeight += 0.4;
        profile.deliveryWeight += 0.5;
        profile.courierWeight += 0.8;
        profile.scoutWeight += 0.7;
        profile.exploreWeight += 0.9;
        profile.patrolWeight += 0.45;
        profile.raidWeight += 0.65;
        profile.bountyWeight += 0.85;
        profile.escortWeight += 0.5;
        profile.colonySupplyWeight += 0.3;
        profile.fuelReserveBias += 0.1;
        profile.opportunityCostBias = 0.8;
    } else if (role == "military") {
        profile.deliveryWeight += 0.1;
        profile.courierWeight += 0.2;
        profile.scoutWeight += 0.2;
        profile.exploreWeight += 0.1;
        profile.patrolWeight += 1.55;
        profile.raidWeight += 1.25;
        profile.bountyWeight += 1.55;
        profile.escortWeight += 1.1;
        profile.combatAvoidance -= 0.3;
        profile.opportunityCostBias = 0.7;
    } else if (role == "colonist") {
        profile.tradeWeight += 0.2;
        profile.deliveryWeight += 0.2;
        profile.courierWeight += 0.3;
        profile.scoutWeight += 0.2;
        profile.exploreWeight += 0.2;
        profile.escortWeight += 0.2;
        profile.colonizeWeight += 1.4;
        profile.colonySupplyWeight += 1.2;
        profile.fuelReserveBias += 0.5;
        profile.combatAvoidance += 0.2;
        profile.opportunityCostBias = 1.1;
    } else if (role == "scout") {
        profile.deliveryWeight += 0.1;
        profile.courierWeight += 0.2;
        profile.scoutWeight += 1.4;
        profile.exploreWeight += 1.2;
        profile.patrolWeight += 0.2;
        profile.bountyWeight += 0.1;
        profile.escortWeight += 0.2;
        profile.fuelReserveBias += 0.4;
        profile.opportunityCostBias = 0.75;
    } else if (role == "pirate") {
        profile.tradeWeight += 0.1;
        profile.deliveryWeight += 0.1;
        profile.scoutWeight += 0.3;
        profile.exploreWeight += 0.2;
        profile.patrolWeight += 0.35;
        profile.raidWeight += 1.85;
        profile.bountyWeight += 0.55;
        profile.escortWeight += 0.1;
        profile.fuelReserveBias += 0.1;
        profile.combatAvoidance -= 0.5;
        profile.opportunityCostBias = 0.6;
    }

    if (profile.deliveryWeight < 0.0) profile.deliveryWeight = 0.0;
    if (profile.courierWeight < 0.0) profile.courierWeight = 0.0;
    if (profile.scoutWeight < 0.0) profile.scoutWeight = 0.0;
    if (profile.exploreWeight < 0.0) profile.exploreWeight = 0.0;
    if (profile.patrolWeight < 0.0) profile.patrolWeight = 0.0;
    if (profile.raidWeight < 0.0) profile.raidWeight = 0.0;
    if (profile.bountyWeight < 0.0) profile.bountyWeight = 0.0;
    if (profile.escortWeight < 0.0) profile.escortWeight = 0.0;
    if (profile.colonizeWeight < 0.0) profile.colonizeWeight = 0.0;
    if (profile.colonySupplyWeight < 0.0) profile.colonySupplyWeight = 0.0;
    if (profile.opportunityCostBias < 0.0) profile.opportunityCostBias = 0.0;
    if (profile.combatAvoidance < 0.0) profile.combatAvoidance = 0.0;
    if (profile.combatAvoidance > 1.5) profile.combatAvoidance = 1.5;
    if (profile.riskTolerance < 0.0) profile.riskTolerance = 0.0;
    if (profile.riskTolerance > 1.0) profile.riskTolerance = 1.0;
    return profile;
}
