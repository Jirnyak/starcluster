# Combat And Piracy Plan

Combat is required because the player loop includes robbery, escort, piracy,
war, and eventual cluster conquest. It must stay economical and data-driven, not
become an arcade subsystem.

## Goal

Combat should answer:

- can this fleet threaten that ship?
- what is the expected loss?
- can pirates profit from attacking?
- can military fleets protect trade lanes?
- can factions capture systems?

## Combat Entities

Ships use module-derived stats:

```text
weaponPower
armor
shieldOrPointDefense
sensorRange
signature
combatSpeed
repairState
cargoValue
```

Fleets can initially be one agent with aggregated stats.

## Detection

Normal remote detection is signal-based. Local detection:

```text
detectionScore =
    observerSensor / (distance3d^2 + 1)
  - targetSignaturePenalty
```

If detected, the agent can decide to flee, intercept, escort, or attack.

## Intercept

Interception should be approximate:

```text
interceptPossible =
    pursuerSpeed > targetSpeed
    and fuelEnough
    and distanceToRoute < interceptWindow
```

First implementation can restrict combat to local star systems. Later add
in-flight interception.

## Combat Resolution

Use a deterministic/probabilistic aggregate model:

```text
attack = weaponPower * readiness * tacticFactor
defense = armor + pointDefense + maneuverDefense
lossRate = attack / (attack + defense + epsilon)
```

Combat tick:

```text
damageToB = attackA * dt / defenseB
damageToA = attackB * dt / defenseA
```

Outcomes:

- flee;
- surrender cargo;
- disable ship;
- destroy ship;
- capture ship;
- retreat to star.

## Piracy

Pirate score:

```text
score =
    cargoValue
  - expectedDamageCost
  - fuelCost
  - bountyRisk
  - factionRetaliationRisk
```

Piracy creates events:

- cargo stolen;
- distress signal;
- bounty contract;
- relation penalty;
- local market shock.

## System Capture

Capture score:

```text
attackerPower = fleetPower + blockadePressure
defenderPower = colonyDefense + localFleets + militia
captureProgress += (attackerPower - defenderPower) * dt
```

Control changes locally first. Remote factions learn through signals.

## Implementation Steps

1. Add combat stats to ship/module plan.
2. Add local detection radius at player/current star.
3. Add pirate/adventurer robbery action.
4. Add simple local combat resolution.
5. Add cargo surrender/loot transfer.
6. Emit combat events and distress signals.
7. Add bounty contracts.
8. Add faction war fleets and system capture progress.

