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
    // (§48.8) Годы БЕСПОМОЩНОГО ДРЕЙФА: копятся, когда двигатель не даёт тяги
    // вовсе (кончился любой из двух расходников), и обнуляются, как только
    // тяга есть. Через TOW_WAIT_YEARS борт слышат. Не сохраняется: после
    // загрузки застрявший начинает ждать заново, и это честнее, чем притворяться,
    // будто в сейве лежит возраст сигнала.
    double driftYears = 0.0;
    int targetFaction = -1;
    bool playerControlled = false;
    // (§35) Борт под АВТОПИЛОТОМ-ТОРГОВЦЕМ. Флаг собственности —
    // `playerControlled`, он стоит у всего флота; этот — про то, ведёт ли борт
    // себя сам. Пилотируемый борт (`playerAgent`) не трогается никогда, даже с
    // поднятым флагом: руль отнимать нельзя.
    bool autoTrade = false;
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
