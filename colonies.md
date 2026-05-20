# Colonies And Control Plan

Colonies connect local markets, faction ownership, system control, and the
player's long-term sandbox progression.

## Goal

The player starts as one ship, then can:

- found a colony;
- reinforce it;
- build infrastructure;
- hire/build fleets;
- control local markets;
- capture other colonies;
- eventually control large parts of the cluster.

The same rules apply to factions.

## Colony Data

Current `Colony` has:

```text
name
population
role
starIndex
ownerFaction
infrastructure
```

Target additions:

```text
automation
energyCapacity
defense
shipyardLevel
marketAccess
damage
localLedger
stockpileValue
constructionQueue[]
```

## Founding Cost

Founding a colony should require:

- credits;
- construction materials;
- fuel/energy stock;
- local habitability or sufficient infrastructure;
- time.

First implementation can keep a credit cost, then add material requirements.

## Growth

```text
populationGrowth =
    population
  * habitability
  * infrastructure
  * supplySatisfaction
  * growthRate
```

Infrastructure growth:

```text
infrastructure += constructionInvestment * efficiency
```

Supply satisfaction comes from local market shortages.

## Ownership

`ClusterStar.ownerFaction` is truth for system control. `Colony.ownerFaction`
must match unless there is a contested state.

Contested system:

```text
ownerFaction = current administrative owner
occupyingFaction = military occupier
captureProgress = -1..1
```

Add contested state only when combat capture begins.

## Player Progression

Player faction unlocks:

- colony founded: first controlled star;
- local taxes/tariffs;
- ship hiring;
- construction queue;
- faction influence overlay;
- patrol orders.

No separate "empire mode" code. The player faction is just a faction.

## Implementation Steps

1. Separate founding from reinforcement UI.
2. Add material requirements using element/resource traits.
3. Add local ledger and colony income.
4. Add construction queue.
5. Add ship hiring/building at shipyard systems.
6. Add capture/contested state after combat.
7. Add colony supply contracts.

