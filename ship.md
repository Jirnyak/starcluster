# Ship Mechanics Plan

This document defines the target ship model. It keeps the current minimal 3D
movement, but adds mass, fuel, upgrade pressure, and a reason to choose between
cargo, speed, range, armor, weapons, and sensors.

## Goal

Ships are expandable physical agents. A ship is not only a sprite and max speed:
it is a bundle of mass, thrust, fuel, cargo, modules, crew/automation, and
orders. The player and AI use the same ship rules.

The desired gameplay pressure:

- bigger cargo means more profit per trip but slower acceleration;
- faster ships need better drives and burn more fuel;
- armed or armored ships sacrifice cargo/range;
- fuel is a normal resource, bought and sold like everything else;
- route planning becomes a physical and economic decision.

## Current State

`Ship` currently has:

```text
name
x, y, z
speed
vx, vy, vz
acceleration
cargo[]
cargoCapacity
ownerFaction
targetStar
enRoute
```

This is already 3D. The missing layer is mass/fuel/modules.

## First Implementation Status

The first implementation adds the physical layer without changing `cargo` into a
general inventory:

- `Ship` has `dryMass`, `driveThrust`, `driveEfficiency`, `fuelElement`,
  `fuel`, and `fuelCapacity`.
- Cargo mass is derived from `Resource.amount * ElementDefinition.atomicMass`
  with a small gameplay scale.
- Fuel is not stored in `ship.cargo`; it is a separate reserve of one element.
- Ship total mass is computed from dry mass, cargo mass, and fuel mass.
- Effective acceleration is `min(design acceleration cap, driveThrust / totalMass)`.
- Acceleration and braking consume fuel through `shipConsumeFuelForDeltaV`.
- Route start checks estimated fuel and attempts local refuel before departure.
- Trade scoring subtracts estimated route fuel cost.
- UI shows cargo mass, total mass, fuel percentage, and a local `FUEL` button in
  the trade window.

Deferred:

- modules;
- fuel switching;
- heat/radiators;
- weapons/armor mass;
- shipyard upgrades;
- save/load migration.

## Target Fields

Add gradually:

```cpp
struct Ship {
    double dryMass;
    double moduleMass;
    double cargoMass;
    double fuelMass;
    double maxCargoMass;
    double fuelCapacityMass;

    double driveThrust;       // mass * c / year
    double driveEfficiency;   // 0..1
    double maxSpeed;          // fraction of c
    double heatCapacity;
    double radiatorPower;

    int fuelElement = -1;     // element index used by current drive
    double fuelReserve;       // amount units, converted to mass by element

    std::vector<ShipModule> modules;
};
```

Keep the current `speed` field temporarily as `maxSpeed` compatibility.

## Cargo Quantity And Mass

The final model should separate quantity from mass. Current `Resource.amount`
acts like a cargo amount. Migration plan:

```text
cargoMass(resource) =
    resource.amount
  * element.atomicMass
  * cargoMassScale
```

Start with:

```text
cargoMassScale = 0.01
```

The scale is a gameplay unit bridge. It lets current markets continue to work
while ship physics begins using mass.

Total mass:

```text
shipMass =
    dryMass
  + moduleMass
  + cargoMass
  + fuelMass
```

## Acceleration

Use Newton-like proportionality without exact SI units:

```text
availableAcceleration = driveThrust / max(minMass, shipMass)
acceleration = min(designAccelerationCap, availableAcceleration)
```

This is enough for gameplay:

- cargo haulers accelerate slowly when full;
- empty ships handle better;
- warships are heavy if armored;
- high-thrust drives are expensive and fuel-hungry.

## Fuel As Resources

Fuel is not a separate abstract meter. Fuel is cargo/resource stock consumed by
the drive.

Any element can theoretically be fuel. Practical usefulness comes from the
universal nuclear traits in `elements.md`:

```text
fuelEnergyDensity =
    element.fusionFuelTrait * fusionDriveFactor
  + element.fissionFuelTrait * fissionDriveFactor

fuelHandlingLoss =
    1 + element.handlingRisk * 0.8

effectiveFuelEnergy =
    fuelEnergyDensity * driveEfficiency / fuelHandlingLoss
```

If `effectiveFuelEnergy` is too low, the drive can still burn it but the route
becomes uneconomical.

## Movement Fuel Cost

During acceleration:

```text
deltaV = acceleration * dt
kineticCost =
    0.5 * shipMass * deltaV^2
fuelMassConsumed =
    kineticCost / max(epsilon, effectiveFuelEnergy)
```

During braking, choose one of two models:

1. Conservative simple model: braking consumes fuel symmetrically.
2. Later advanced model: partial regenerative braking for special drives.

Use option 1 first. It is easier to understand and creates real route cost.

## Route Viability

Before accepting a route, estimate:

```text
routeDistance = dist3d(origin, destination)
deltaVBudget = routeDistanceFactor(routeDistance, maxSpeed, acceleration)
fuelNeeded = estimateFuelForDeltaV(shipMass, deltaVBudget, fuelElement)
```

If current fuel is insufficient:

- player route UI shows "insufficient fuel";
- AI either buys fuel, chooses another route, or waits;
- emergency drift can be added later but should be rare.

## Speed Cap

Max speed remains a design cap:

```text
if speed3d > maxSpeed:
    velocity *= maxSpeed / speed3d
```

Mass affects acceleration and fuel consumption, not the absolute speed cap.
This keeps the model readable.

## Ship Modules

Modules are data-driven:

```cpp
struct ShipModuleDef {
    std::string id;
    double mass;
    double price;
    double cargoBonus;
    double fuelCapacityBonus;
    double thrustBonus;
    double efficiencyBonus;
    double sensorBonus;
    double weaponPower;
    double armor;
};
```

No inheritance tree is needed. A ship sums module fields.

Initial module categories:

- cargo bay;
- fuel tank;
- drive;
- reactor;
- radiator;
- sensor;
- armor;
- weapon;
- automation core.

## Player Decisions

Ship UI should expose:

```text
mass dry/cargo/fuel/total
max cargo
fuel resource
fuel remaining in years/routes
acceleration empty/full
max speed
estimated fuel cost to selected star
profit after fuel cost
```

This makes trading about route economics, not just price spread.

## AI Requirements

AI route scoring must include:

```text
expectedProfit
- fuelCost
- timeCost
- riskCost
- opportunityCost
```

AI must not pick routes it cannot fuel.

## Implementation Steps

1. Add `atomicMass` to elements.
2. Add `shipMass()` and `cargoMass()` helpers.
3. Add fuel fields to `Ship`, defaulting to a valid starter fuel.
4. Consume fuel during acceleration and braking.
5. Prevent player/AI routes when fuel is insufficient.
6. Add local fuel buying UI in trade window.
7. Add basic module definitions and ship stat recomputation.
8. Add shipyard market behavior and upgrade screen.
