#pragma once

enum class ContractType {
    Delivery,
    Courier,
    Scout,
    Bounty,
    Escort,
    Raid,
    ColonySupply
};

struct Contract {
    int id = 0;
    ContractType type = ContractType::Delivery;
    int issuerFaction = -1;
    int originStar = -1;
    int targetStar = -1;
    int targetAgent = -1;
    int resource = -1;
    double amount = 0.0;
    double reward = 0.0;
    double deposit = 0.0;
    double postedTime = 0.0;
    double deadline = 0.0;
    double risk = 0.0;
    double progress = 0.0;
    bool reportSignalPending = false;
    bool reportDelivered = false;
    bool escortArrived = false;
    int acceptedByAgent = -1;
    bool completed = false;
    bool failed = false;
};
