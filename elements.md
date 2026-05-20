# Elements Model Plan

This document defines the target element model for Starcluster. It is a work
plan for implementation agents, not a shipped-behavior report.

## Goal

Elements are the root resource ids of the game. The final model should derive
gameplay properties from atomic number and small universal formulas, not from
per-element behavior branches.

The existing `resource.cpp` still contains several symbol-based category
helpers. Those are prototype debt. Future work should keep the symbol/name table
for UI, but derive traits from `atomicNumber`.

## Non-Negotiable Rules

- Element identity is `Z`, the proton count.
- Symbol and name are labels only.
- No runtime rule may say `if element == Au` or `if symbol == "H"`.
- Group-like behavior must come from a shell/octet model.
- Nuclear value must come from distance from the iron peak.
- Mass must come from a universal mass formula.
- Procedural run variation is allowed, but it must be seeded data, not special
case code.

## Core Data

Extend `ElementDefinition` toward:

```cpp
struct ElementDefinition {
    int atomicNumber;
    const char* symbol;
    const char* name;

    double atomicMass;        // derived from Z
    double abundanceWeight;   // derived baseline, then seeded/local modifiers
    double demandWeight;      // derived baseline
    double basePrice;         // derived from traits

    double valenceElectrons;
    double shellFill;
    double nobleStability;
    double oxidizerTrait;
    double reducerTrait;
    double metallicTrait;
    double structuralTrait;
    double conductorTrait;
    double catalystTrait;

    double fusionFuelTrait;
    double fissionFuelTrait;
    double nuclearStability;
    double activationCost;
    double handlingRisk;
};
```

This does not require storing every field permanently. Implementation may compute
traits once in `buildElements()` and keep only values used by current systems.

## First Implementation Status

The first implementation stores the core trait set directly in
`ElementDefinition`:

- `atomicMass`;
- `valenceElectrons`, `shellFill`, `nobleStability`;
- `oxidizerTrait`, `reducerTrait`;
- `metallicTrait`, `structuralTrait`, `conductorTrait`, `catalystTrait`;
- `fusionFuelTrait`, `fissionFuelTrait`, `nuclearStability`;
- `activationCost`, `handlingRisk`;
- derived `abundanceWeight`, `demandWeight`, `basePrice`.

`resource.cpp` derives these values from `atomicNumber`. The raw symbol/name
table remains only for UI labels and lookup.

`market.cpp` uses traits for role demand. `cluster.cpp` uses traits for local
resource reservoir variation. Future work should keep moving cargo, fuel, ship,
and UI logic toward these same traits instead of adding element-specific
branches.

## Approximate Atomic Mass

The game uses cargo mass units, not exact isotope tables. Use one formula:

```text
atomicMass(Z) = round(2 * Z - 1 / Z)
```

This gives:

```text
Z = 1  -> 1
Z = 2  -> 4
Z = 6  -> 12
Z = 26 -> 52
Z = 92 -> 184
```

This is intentionally approximate. It is stable, monotonic, and comes only from
`Z`. If a later model needs heavy neutron excess:

```text
neutronExcess = smoothstep(26, 118, Z) * 0.38
atomicMass = round(Z * (2 + neutronExcess) - 1 / Z)
```

Do not introduce isotope tables until gameplay needs them.

## Shell And Octet Model

Use a simplified shell filling model. Allowed shell capacities:

```text
shellCapacities = [2, 8, 8, 18, 18, 32, 32]
```

Given `Z`, fill shells in order.

```text
outerElectrons = electrons in last non-empty shell
outerCapacity = capacity of last non-empty shell
shellFill = outerElectrons / outerCapacity
nobleStability = 1 - min(outerElectrons, outerCapacity - outerElectrons) / (outerCapacity / 2)
```

The model is not exact quantum chemistry. It exists to generate universal game
traits:

```text
oxidizerTrait = max(0, (outerCapacity - outerElectrons) / outerCapacity)
reducerTrait  = max(0, outerElectrons / outerCapacity)
reactivity    = 1 - nobleStability
```

Metals and conductors can be approximated without naming elements:

```text
period = shell index
middleShell = 1 - abs(shellFill - 0.5) * 2
metallicTrait = smoothstep(3, 7, period) * (0.35 + 0.65 * middleShell)
conductorTrait = metallicTrait * (0.4 + 0.6 * nobleStability)
structuralTrait = metallicTrait * sqrt(Z) / sqrt(118)
```

This creates demand classes without hardcoding:

- reducers are useful as industrial reactants;
- oxidizers are useful for chemistry, life support, processing;
- noble-stable elements are useful where corrosion and reactivity are bad;
- heavy metallic/conductive elements are valuable in machinery, radiation
  shielding, electronics, catalysts, and high-reliability hardware.

## Nuclear Model

The iron peak is represented by `Z_iron = 26`.

Binding proxy:

```text
ironDistance = abs(Z - 26) / 92
bindingProxy = 1 - ironDistance^0.72
```

Fusion fuel trait:

```text
fusionFuelTrait =
    if Z < 26:
        ((26 - Z) / 25)^0.9 * lightMassBonus
    else:
        0

lightMassBonus = 1 / sqrt(atomicMass)
```

Fission fuel trait:

```text
fissionFuelTrait =
    if Z > 26:
        ((Z - 26) / 92)^1.15 * fissileFractionProxy
    else:
        0

fissileFractionProxy =
    smoothstep(70, 118, Z) * (0.35 + 0.65 * reactivity)
```

Activation cost:

```text
activationCost =
    0.3
  + nobleStability * 0.7
  + bindingProxy * 1.2
```

Energy usefulness:

```text
nuclearEnergyTrait =
    max(fusionFuelTrait, fissionFuelTrait) / activationCost
```

This makes light elements naturally valuable for fusion and very heavy elements
naturally valuable for fission, while elements near the iron peak are less useful
as fuel.

## Stability And Special Value Without Special Cases

"Gold-like" value should not be an element exception. It should emerge from:

```text
prestigeMaterialTrait =
    nobleStability
  * conductorTrait
  * structuralTrait
  * scarcityTrait
```

Any element with high stability, conductivity, density/structure, and scarcity
becomes valuable. Gold may score high because of its `Z`, not because code names
gold.

## Base Price Formula

Base price should combine scarcity, energy, chemistry, industry, and run noise.

```text
scarcityTrait = 1 / sqrt(abundanceWeight + epsilon)
chemicalValue =
    reactivity * 0.8
  + oxidizerTrait * 0.7
  + reducerTrait * 0.7
  + nobleStability * 0.5

industrialValue =
    structuralTrait * 1.1
  + conductorTrait * 1.0
  + catalystTrait * 0.8

energyValue =
    fusionFuelTrait * 2.0
  + fissionFuelTrait * 2.2

basePrice =
    baseScale
  * scarcityTrait
  * (1 + chemicalValue + industrialValue + energyValue)
  * runPriceNoise[Z]
```

`runPriceNoise[Z]` is generated once per game:

```text
runPriceNoise[Z] = exp(normal(0, 0.18))
```

This satisfies "some resources are expensive this run for procedural reasons"
without per-element hardcoding.

## Integration Steps

1. Add universal trait helper functions in `resource.cpp`.
2. Replace symbol-list category helpers with derived traits.
3. Keep symbol/name table only for display and indexing.
4. Update market demand to use trait vectors instead of named categories.
5. Update ship fuel and cargo mass to read `atomicMass` and fuel traits.
6. Add debug UI: selected element shows `Z`, mass, fuel traits, reactivity,
   stability, base price.
