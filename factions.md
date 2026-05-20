# Factions Plan

Factions are the strategic layer above ships, colonies, markets, and signals.
The player is also a faction, starting with one ship and no real territory.

## Current Implementation

First faction-memory layer is implemented:

- every faction, including the player faction, has a flat `faction * star`
  ownership memory overlay;
- player owner overlay is now backed by the same faction memory model;
- initial faction memory is seeded around each faction home and owned systems;
- NPC ship observations emit delayed owner-report signals to the nearest
  faction relay instead of directly mutating global faction memory;
- NPC ship observations emit delayed market-report signals with captured price
  snapshots;
- player-local observation still updates player faction memory immediately;
- strategic colonist and military target scoring uses remembered owner state,
  not direct live ownership truth;
- trade auto-routing now ignores destination systems unknown to the acting
  faction's owner-memory and discounts stale routes.

Still missing:

- delayed signal propagation for market snapshots, contracts, and diplomacy;
- faction market snapshots;
- relation memory and diplomatic reports;
- per-agent memory separate from faction memory;
- UI/debug selector for viewing non-player faction memory overlays.

## Goal

Factions should behave like large player-like entities:

- own systems and colonies;
- hold local and estimated capital;
- spawn and command fleets;
- trade, colonize, patrol, raid, fight, and negotiate;
- expand influence over nearby stars;
- react through delayed information, not instant omniscience.

## Current State

`Faction` currently has:

```text
name
color
homeStar
treasury
strength
aggression
controlledStars[]
```

This is enough for prototype ownership, but not enough for strategic play.

## Target Fields

```cpp
struct Faction {
    std::string name;
    int colorR, colorG, colorB;

    int homeStar;
    std::vector<int> controlledStars;
    std::vector<int> fleetAgents;

    double estimatedTreasury;
    double militaryBudget;
    double tradeBudget;
    double colonyBudget;

    double aggression;
    double riskTolerance;
    double tradeBias;
    double expansionBias;
    double defenseBias;

    int relationRowOffset; // into dense relation matrix
};
```

Relations live outside the faction object for cache locality:

```text
relations[factionA * factionCount + factionB] = int8 -128..128
```

## Player Faction

The player starts as a faction with:

```text
controlledStars = []
fleetAgents = [playerAgent]
estimatedTreasury = player.money or linked account
```

When the player founds or captures a colony, the faction gains territory. When
the player hires ships, those ships become faction agents.

No special player-only empire code.

## Capitalization

Faction value:

```text
capitalization =
    sum(localLedger controlled systems)
  + sum(colonyInfrastructureValue)
  + sum(fleetReplacementValue)
  + sum(stockpileValue)
  + estimated cash
```

Treasury is spendable liquidity. Capitalization is power/score.

## Influence Overlay

The player requested a visible faction-colored neighborhood around controlled
stars, like a strategic influence map. It must be optional.

Influence field:

```text
influence(faction, point) =
    sum over controlled stars:
        starInfluenceStrength / (distance3d(point, star)^2 + core^2)
```

For rendering in the current SDL projection:

1. Sample a coarse screen grid, for example 24x18 or 32x24 cells.
2. For each grid sample, unproject approximately into the current camera plane.
3. Evaluate influence from nearby controlled stars only.
4. Choose the strongest faction.
5. If neighboring cells have the same strongest faction, draw blended patches.
6. If two same-faction stars overlap, the colored region naturally merges.

Do not draw this as fake stars. It is a UI overlay layer.

Performance rule:

- recompute overlay when camera changes or ownership changes;
- use coarse cells and alpha;
- later add spatial bins for controlled stars.

## Borders

Border pressure:

```text
borderPressure(A, star) =
    influence(enemy, star.position) - influence(A, star.position)
```

Use it for:

- patrol placement;
- defensive spending;
- war target selection;
- relation drift;
- tariff changes.

## Strategic Orders

Faction systems should generate orders:

```text
TradeOrder
PatrolOrder
ColonizeOrder
ScoutOrder
RaidOrder
AttackSystemOrder
DefendSystemOrder
CourierOrder
```

Orders are data. Agents consume orders based on role and ship capability.

## Relations

Relation range:

```text
-128 enemy
0 neutral
128 ally
```

Relation drift inputs:

- border pressure;
- trade dependency;
- piracy/raids;
- attacks;
- aid contracts;
- treaty signals;
- ideology/personality weights;
- delayed knowledge age.

Relations should not update every frame. Slow tick yearly/monthly equivalent.

## War

War is economic and spatial:

- fleets must travel;
- local control changes before remote knowledge updates;
- blockades affect markets;
- conquest damages infrastructure;
- faction capital and replacement time matter.

War target score:

```text
targetValue =
    resourceValue
  + routeValue
  + colonyValue
  - defenseCost
  - travelCost
  - relationCost
  - staleInfoRisk
```

## Implementation Steps

1. Add dense relation matrix to `Game`.
2. Make player faction fully participate in ownership and controlledStars.
3. Add faction order structs.
4. Add optional influence overlay in renderer.
5. Add slow strategic tick: budgets, priorities, order generation.
6. Add patrol/defend/colonize/trade fleet assignment.
7. Add war declaration and blockade states.
8. Add delayed relation and war reports through `events.md`.
