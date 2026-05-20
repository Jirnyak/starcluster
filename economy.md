# Economy Plan

This document defines the target economy. The game is an economic trade
simulation first: local prices, local scarcity, fuel costs, travel delay, stale
information, and faction control produce decisions.

## Current Implementation

First market-memory layer:

- each faction has `faction * star` market snapshot metadata;
- each faction has flat `faction * star * element` remembered prices;
- local observation captures current market prices immediately for the player;
- NPC observations emit delayed `MarketReport` signals to the nearest faction
  relay;
- trader and player auto-trade route scoring use remembered destination prices,
  not live remote market truth;
- unknown destination markets are ignored by auto-trade;
- stale market snapshots add route scoring penalty.

Still missing:

- supply and demand pressure snapshots;
- confidence ranges for stale market prices;
- local ledgers and bank settlement signals;
- tariffs from relation matrix;
- blockade/piracy risk effects on route scoring.

## Goal

The player starts as one trader trying to get rich. The same systems must scale
to faction treasuries, autonomous traders, war logistics, colonies, quests, and
ship upgrades.

Core loop:

```text
observe local market
compare with known/stale remote markets
buy cargo and fuel
choose a route
spend years traveling
sell, refuel, accept contracts, upgrade
eventually found/capture systems and control fleets
```

## Market Rules

There is no global market. Each star has one local market:

```text
supply[resource]
demand[resource]
price[resource]
productionRate[resource]
consumptionRate[resource]
localTariff
blockadeFactor
lastUpdateTime
```

Price:

```text
scarcity = (demand + demandRate * lookahead + 1) /
           (supply + productionRate * lookahead + 1)

price = basePrice * scarcity^elasticity * localModifiers
```

Suggested:

```text
elasticity = 0.55..0.75
lookahead = 60..100 ticks
```

## Base Price

Base price comes from `elements.md`:

```text
basePrice =
    scarcityTrait
  * (energyValue + chemicalValue + industrialValue + stabilityValue)
  * runPriceNoise
```

Hydrogen, helium, heavy fission resources, oxidizers, reducers, stable
conductors, and carbon-chain-like elements should become valuable because of
derived traits, not because the market names them.

## Local Demand

Demand is generated from system role and state:

```text
demandWeight =
    populationNeed
  + industryNeed
  + shipyardNeed
  + militaryNeed
  + lifeSupportNeed
  + energyNeed
  + constructionNeed
  + luxuryPrestigeNeed
```

Each term consumes element traits:

```text
energyNeed       -> nuclearEnergyTrait, lowMassFuelTrait
lifeSupportNeed  -> oxidizerTrait, reducerTrait, volatileTrait
constructionNeed -> structuralTrait, metallicTrait
electronicsNeed  -> conductorTrait, stabilityTrait
militaryNeed     -> energyTrait, densityTrait, structuralTrait
prestigeNeed     -> prestigeMaterialTrait
```

All local demand terms should be coefficients in data or generated fields, not
hardcoded per element.

## Trade Profit

Route value:

```text
grossSpread =
    remoteSellPrice - localBuyPrice

expectedProfit =
    grossSpread * cargoAmount
  - fuelCost
  - maintenanceCost
  - tariffs
  - riskExpectedLoss
  - capitalTimeCost
  - staleInfoPenalty
```

Travel time:

```text
distance = dist3d(origin, destination)
travelTime = shipTravelEstimate(distance, shipMass, driveStats)
```

Fuel cost must be included before a route looks profitable.

## Stale Market Knowledge

Normal player and AI should not read all live markets unless they have debug
truth. Market reports are signals.

Market snapshot:

```cpp
struct MarketSnapshot {
    int star;
    double observedTime;
    std::vector<float> prices;
    std::vector<float> supplyPressure;
    std::vector<float> demandPressure;
};
```

Displayed uncertainty:

```text
age = now - observedTime
confidence = exp(-age / marketMemoryTau)
priceLow  = knownPrice * exp(-volatility * sqrt(age))
priceHigh = knownPrice * exp( volatility * sqrt(age))
```

AI can take risks using stale data, but the scoring must penalize age.

## Faction Economy

Faction budget is not a magic global number. It is the result of local ledgers
and delayed settlements:

```text
localLedger[star][faction]
pendingSettlement[]
estimatedTreasury[faction]
```

Local colonies spend local funds immediately. Strategic faction AI uses
estimated treasury, which can be stale.

## Taxes And Tariffs

Each faction controls tariff policy:

```text
tariff =
    baseTariffByOwner
  + relationTariff
  + warTariff
  + blockadeTariff
```

The player faction follows the same rule.

## Production And Consumption

Production should be slow-ticked, not per frame.

```text
supply += productionRate * dt
demand += consumptionRate * dt
```

Production rate depends on:

- local resource reservoir;
- colony infrastructure;
- energy availability;
- labor/automation;
- imports of required industrial inputs;
- war/blockade damage.

## Implementation Steps

1. Move element category pricing to universal traits.
2. Add route fuel and time cost.
3. Add market snapshots to player knowledge.
4. Make player auto-trade use known snapshots, not omniscient market truth.
5. Add tariffs by owner faction.
6. Add local ledgers for factions.
7. Add settlement signals for faction capital.
8. Add blockade and piracy effects on route risk and prices.
