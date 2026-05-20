#pragma once
#include <string>
#include "contract.h"
#include "ship.h"

// Агент (логика поведения)
class Agent {
public:
    std::string type; // trader, military, colonist, etc
    Ship ship;
    int currentStar = 0;
    int homeStar = 0; // Базовая звезда
    int destStar = 1; // Целевая звезда
    bool toDest = true; // В какую сторону летит
    double money = 1000.0; // Баланс агента
    double cargoCost = 0.0;
    double lastProfit = 0.0;
    int trades = 0;
    double missionCooldown = 0.0;
    int targetFaction = -1;
    bool playerControlled = false;
    double tradeBias = 1.0;
    double questBias = 0.35;
    double piracyBias = 0.0;
    double scoutBias = 0.0;
    double riskTolerance = 0.45;
    std::string lastAction;
    Agent(const std::string& type_, const Ship& ship_);
};

struct AgentRoleProfile {
    double tradeWeight = 0.0;
    double deliveryWeight = 0.0;
    double courierWeight = 0.0;
    double scoutWeight = 0.0;
    double exploreWeight = 0.0;
    double patrolWeight = 0.0;
    double raidWeight = 0.0;
    double bountyWeight = 0.0;
    double escortWeight = 0.0;
    double colonizeWeight = 0.0;
    double colonySupplyWeight = 0.0;
    double riskTolerance = 0.0;
    double fuelReserveBias = 0.0;
    double combatAvoidance = 0.0;
    double opportunityCostBias = 1.0;
};

AgentRoleProfile agentRoleProfile(const Agent& agent);
double agentContractRoleWeight(const AgentRoleProfile& profile, ContractType type);
