# Quests And Contracts Plan

Contracts are gameplay hooks for the player and player-like agents. They must
use the same economy, travel, risk, and signal systems as everything else.

## Current Implementation

First playable layer:

- `ContractType::Delivery`;
- local job board window opened from system info;
- contracts are visible only when the player is docked in the origin system;
- generation is slow-ticked and capped per origin system;
- delivery cargo is loaded into the normal ship hold on accept;
- completion requires physical arrival at the target system with matching cargo;
- reward is derived from route distance, local price spread, and target scarcity;
- late delivery still completes but pays less.

Not implemented yet:

- signal propagation for remote contract visibility;
- AI contract acceptance;
- scout, bounty, escort, courier, colony supply, and raid contract types;
- deposits, reputation, and faction relation effects.

## Goal

Contracts create decisions beyond simple arbitrage:

- safe delivery for low margin;
- high-risk delivery for high reward;
- scouting unknown systems;
- escorting vulnerable ships;
- clearing pirates;
- attacking/blockading faction targets;
- supplying colonies;
- founding or reinforcing colonies;
- couriering data with time pressure.

AI adventurers can accept contracts too.

## Contract Data

```cpp
enum class ContractType {
    Delivery,
    Courier,
    Scout,
    SurveyMarket,
    Escort,
    Bounty,
    ClearPirates,
    Raid,
    ColonySupply,
    BuildOutpost,
};

struct Contract {
    int id;
    ContractType type;
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
    int acceptedByAgent = -1;
    bool completed = false;
};
```

Contracts are local objects. Their visibility spreads by signals.

## Contract Generation

Sources:

- local market shortages;
- faction strategic orders;
- colonies lacking inputs;
- military threats;
- stale intelligence gaps;
- ships needing escort;
- adventurers posting offers;
- bank/faction courier tasks.

Generation should be slow-ticked and capped per system.

## Reward Formula

```text
reward =
    basePay
  + distancePay
  + timePressurePay
  + riskPay
  + scarcityPay
  + relationPay
```

Delivery:

```text
scarcityPay = amount * targetPricePressure * resourceBasePrice * k
```

Scout:

```text
reward = unknownValue + strategicValue + distancePay - expectedSignalDelayPenalty
```

Bounty:

```text
reward = targetThreatValue + issuerWealthFactor + relationModifier
```

## Acceptance Rules

An agent can accept if:

- physically at a star where contract is visible;
- has enough cargo/fuel/ship capability;
- can pay deposit if required;
- is not hostile to issuer unless contract is black-market;
- expected score beats current plan.

AI scoring:

```text
score =
    reward
  - fuelCost
  - opportunityCost
  - riskExpectedLoss
  - deadlinePenalty
```

## Completion

Completion is local and may need a signal:

- delivery completes at target market/colony;
- scout completes when data signal arrives at issuer, not merely when scout
  visits the star;
- bounty completes when kill/capture report reaches issuer;
- courier completes on physical arrival.

This makes signals gameplay-relevant.

## Failure

Failure cases:

- deadline missed;
- cargo lost;
- target no longer exists;
- issuer destroyed/captured;
- agent abandons contract;
- relation changes make completion illegal.

Failure should have economic consequences, not hard fail states:

- lost deposit;
- relation penalty;
- reputation penalty;
- bounty/piracy flag.

## UI

Local contract window:

```text
TYPE
ISSUER
TARGET
DISTANCE 3D
DEADLINE
REWARD
RISK
FUEL ESTIMATE
REQUIRED CARGO
ACCEPT BUTTON
```

Remote contract data must show age.

## Implementation Steps

1. Add `Contract` vector to `Game`.
2. Generate delivery contracts from market shortages.
3. Add local contract window.
4. Let player accept and complete delivery/courier contracts.
5. Add AI scoring for adventurer agents.
6. Add scout contracts using signal completion.
7. Add bounty/escort contracts after combat exists.
8. Add reputation and relation effects.
