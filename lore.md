# Starcluster Lore And Simulation Canon

This document defines the physical and economic assumptions of the game world.
It is not flavor prose. It is a constraint document for generation, UI, economy,
factions, signals, and future content.

## Scope

The game takes place inside one dense globular star cluster.

Baseline scale:

- cluster radius: order of 100 light years;
- generated stars: order of 10k;
- active mobile agents and fleets: order of 10k;
- strategic factions: order of 100;
- normal ship cruise speed: about 0.1c;
- high-end ship speed: up to about 0.5c;
- signal speed: c.

Current prototype scale is smaller for iteration:

- current generated stars: `STAR_COUNT = 10000`;
- current implemented cluster radius: `R_cluster = 100 ly`;
- current renderer: SDL2 orthographic projection of the 3D simulation state.

The dense cluster is required for gameplay. In a sparse galactic disk region,
typical travel times would push routine routes into many decades or centuries.
The globular cluster lets the game keep real light-year distances and sublight
travel while still producing frequent economic decisions.

## Stellar Population

Most real globular clusters are old and metal-poor. This cluster is an unusual
case used by the setting:

- it is old enough to have a dense, stable cluster-scale economy;
- it contains a statistically unusual fraction of younger or rejuvenated stars;
- it has enough enriched material for rocky systems, asteroid belts, industrial
resources, and habitable or marginally habitable colonies;
- it is not representative of an average globular cluster.

This assumption exists to support gameplay without introducing faster-than-light
travel or nonphysical shortcuts.

## Stellar Classes

Stellar spectral classes are not a primary gameplay system by default.

Reason:

- the economy trades system resources, not material extracted from stellar cores;
- most useful game resources come from bodies orbiting stars, not from the star
  itself;
- tying every element directly to stellar class would create false precision and
  reduce procedural variety;
- gameplay needs local resource variation, market pressure, distance, ownership,
  industry, and demand more than it needs a star taxonomy.

If stellar classes are added later, they should be generation inputs only:

- luminosity and thermal zone;
- long-term habitability weight;
- radiation and operating cost;
- expected disk/remnant structure;
- survey uncertainty.

They should not hardcode local market outcomes.

## Elements And Resources

The game uses chemical elements as resource ids. Element identity is treated as
atomic number: a resource is defined by proton count, symbol, base abundance
weight, demand weight, and base price.

Resources are not assumed to be mined from stars directly.

Resource sources include:

- asteroid and comet belts;
- metallic planetesimals;
- rocky planets and moons;
- industrial waste streams;
- old supernova and neutron-capture enrichment in the local formation material;
- captured bodies and debris fields;
- processed stockpiles held by colonies and factions.

Elements above carbon are valid in the game because systems inherit enriched
material from earlier stellar generations and rare high-energy nucleosynthesis
events. Heavy elements should usually be scarcer, more localized, and more
price-sensitive than light elements.

Resource generation should therefore be procedural and local:

- every system has its own resource weights;
- every system has its own demand weights;
- some systems are rich in specific element pockets;
- some systems have structural shortages;
- industry and population reshape demand over time;
- ownership, war, tariffs, blockades, and travel time alter effective value.

No element should be globally special-cased unless it is defined as data.

## Technology Level

The setting is a grounded future of human-level civilization roughly 3000-5000
years ahead of the present.

Allowed baseline technologies:

- dense automation;
- pervasive AI planning and control;
- high-temperature and engineered superconductivity;
- fusion energy;
- high-efficiency electric, fusion, or beamed propulsion;
- large-scale asteroid, moon, and orbital extraction;
- local closed-cycle industry;
- autonomous fleets;
- long-lived machine maintenance;
- relativistic communication protocols;
- slow institutional control over interstellar colonies.

Excluded baseline technologies:

- faster-than-light travel;
- faster-than-light communication;
- time travel;
- aliens;
- magical artifacts;
- Dyson spheres as normal infrastructure;
- instant global banking;
- omniscient strategic maps;
- narrative-only resources that do not exist in simulation data.

The setting may contain advanced machinery, but gameplay should present it as
infrastructure and data, not as unexplained fantasy.

## Relativity And Time

The game does not violate special relativity.

Rules:

- ships move below light speed;
- signals move at light speed;
- remote information is delayed by distance;
- there is no instant strategic command over the whole cluster;
- the player sees local truth only when physically present or when delayed data
  arrives.

The simulation uses player proper time as the displayed game time. Relativistic
time dilation is ignored for gameplay. At common speeds around 0.1c the effect
is small enough for this abstraction. At rare speeds near 0.5c it is still
ignored unless a future system explicitly models it.

Simulation tick:

- one gameplay tick is 0.01 years;
- 100 ticks equal 1 year;
- the game runs in real time;
- the player can pause with Space to make decisions.

Pause is an input/UI feature. It is not a physical property of the setting.

## Signals

All nonlocal information is a signal with origin, destination, send time, and
arrival time.

Signal travel time:

```text
arrival_time = send_time + distance_ly
```

because distance is measured in light years and signal speed is c.

Signal-backed systems include:

- ownership reports;
- market summaries;
- bank settlement messages;
- faction orders;
- treaty updates;
- war declarations;
- scout reports;
- public price feeds;
- player knowledge overlay.

The UI may show stale information for usability, but it must label it as
last-known or delayed. Normal UI must not silently read live remote truth.

Current prototype has the knowledge overlay but not the full signal queue yet.
Implemented player owner knowledge per star:

```text
ownerKnown
ownerFaction
ownerKnownAt
visited
```

Initial navigation data learns owners inside a 10 light-year 3D radius around
the starting system without marking those systems as visited. A system becomes
visited when the player is physically present there.

## Banking And Credit

Every inhabited system has financial terminals connected through a
relativistic banking network. The network is not instant. It is a set of
light-speed settlement messages between systems.

Player-level trading can use local terminal credit for immediate usability.
The more important simulation rule is at faction scale:

- transactions happen locally at a system;
- local ledgers update immediately;
- settlement messages propagate at light speed;
- faction treasury is an estimate assembled from known settled ledgers;
- remote faction capital can be stale;
- strategic decisions may use delayed capital data;
- local colonies can spend local credit before cluster-wide consolidation.

Suggested accounting model:

```text
local_balance[system_id][faction_id]
pending_settlements[]
known_faction_capital[faction_id][observer_or_capital_system]
```

Transaction flow:

1. A local trade, tax, repair, build, or military expense changes a local ledger.
2. A settlement message is emitted from that system.
3. The message arrives at other relevant systems after light-speed delay.
4. Faction-level capital estimates update when the message arrives.

This gives factions non-instant financial awareness without making player
trading tedious.

## Economy

The game is primarily an economic trade simulation.

There is no global market.

Each system has:

- local supply by element;
- local demand by element;
- production rates;
- consumption rates;
- stockpiles;
- price pressure;
- owner faction;
- tariffs and fees;
- colony and infrastructure effects;
- local banking and settlement state.

Prices are data outputs of local state.

Trade value depends on:

- buy price;
- sell price;
- cargo capacity;
- travel time;
- acceleration and cruise speed;
- market drift during travel;
- route risk;
- faction ownership;
- tariffs;
- blockade state;
- stale information age;
- opportunity cost of capital.

The player interacts with this through tables, numbers, route choices, and
local system windows. The game should prefer explicit values over descriptive
text.

## Factions

Factions are economic and strategic controllers of colonies.

They should be simulated as data:

- controlled systems;
- colonies;
- local ledgers;
- known capital;
- fleets and agents;
- relation matrix;
- aggression and policy weights;
- trade dependencies;
- military capacity;
- delayed information.

Faction relations use a dense table:

```text
relations[a][b] = -128..128
```

Relations change through trade, raids, conquest, border pressure, treaties,
market dependence, and delayed reports.

## Agents

Agents are ships or fleets moving through the cluster.

Agent types include:

- player ship;
- trader;
- military fleet;
- colonizer;
- future courier;
- future scout;
- future escort;
- future blockade fleet.

Agents are physical. They have position, velocity, current order, cargo,
owner, and local knowledge. They do not teleport, and they do not receive
instant perfect information from the whole cluster.

## Player Perspective

The player is one agent inside the same simulation rules.

The player can know:

- fixed stellar positions from navigation data;
- initial ownership information within a starting radius of about 10 light
  years;
- live local system state while present;
- delayed reports that have arrived;
- last-known data from previously visited systems.

When the player leaves a system, the map may continue to show that system as it
was last known. That display is not truth. Ownership, market state, fleets, and
war may have changed.

Debug mode may show live truth, but debug visibility must be separate from
normal player information.

## Content Restrictions

Do not add:

- aliens;
- time travel;
- faster-than-light travel;
- faster-than-light communication;
- magical economics;
- instant global ownership knowledge;
- decorative fake stars in normal map rendering;
- narrative events that bypass local markets, signals, travel, or faction data;
- human drama as a primary simulation layer.

Human factors can exist as aggregate parameters such as population, stability,
labor cost, demand, policy, and risk. The core game is not character drama. It
is a procedural economic and political simulation.

## Design Direction

The game should expose its world through technical context:

- price;
- supply;
- demand;
- distance;
- speed;
- acceleration;
- travel time;
- stale data age;
- owner;
- tariff;
- risk;
- cargo;
- capital;
- relation score;
- production rate;
- consumption rate.

UI text should be concise and numeric. When a system needs explanation, prefer
labels and tables over lore paragraphs.

The setting exists to support data-driven emergence. Content should be
procedural, local, and measurable.

## Extended Physical Model Canon

The sections below expand the setting into a model bible for future systems.
They are intentionally technical. They define simplified physical models that
are acceptable for gameplay and implementation.

The guiding rule is:

```text
physical enough to constrain systems
simple enough to simulate at 10k stars and 10k agents
data-driven enough to avoid hardcoded content
```

No model in this document should require per-frame full-world scans. Expensive
quantities can be generated once, cached, or updated on slow ticks.

## Units

Canonical units:

```text
distance        = light years
position        = vec3 light years
velocity        = vec3 fraction of c
time            = years
signal_speed    = 1 ly/year
ship_speed      = fraction of c
acceleration    = c/year
mass            = abstract cargo mass units unless a system specifies kg
energy          = abstract energy credits derived from physical formulas
money           = local credit units
temperature     = Kelvin where stellar or thermal systems need it
map_projection  = 3D simulation state projected to a 2D screen
```

Gameplay tick:

```text
dt_game = 0.01 years
100 ticks = 1 year
```

Distances and travel times should be computed from 3D positions. The current
map renderer is a UI projection and must not define simulation distance.

## Implementation Doctrine For Physics

Prefer scalar fields and small formulas:

- `massSolar`
- `luminositySolar`
- `temperatureK`
- `metallicityFeH`
- `alphaFe`
- `radiation`
- `diskRetention`
- `volatileRetention`
- `rockyReservoir`
- `gasReservoir`
- `debrisReservoir`
- `localDensity`
- `encounterStress`
- `routeCentrality`

Avoid:

- full orbital N-body;
- exact quantum chemistry;
- exact isotope chains;
- per-molecule atom simulation for bulk materials;
- named-element exceptions inside systems;
- star class directly hardcoding market outcomes.

Tables are allowed when they are data. Hardcoding behavior in systems is not.

## Globular Cluster Model

The simulated place is a dense cluster-scale economy inside one unusual globular
cluster.

Recommended cluster parameters:

```text
R_tidal        = 100 ly
R_core         = 8..25 ly
R_half         = 25..40 ly
N_sim          = about 10k catalogued economic systems
N_background   = optional, not normally rendered, not simulated economically
```

`N_sim` means economically relevant systems, not necessarily every physical
star. Background stellar population can exist in lore and generation seeds, but
normal rendering must not draw fake stars. If a point appears as a star in the
normal map, it should correspond to a generated system or an explicit debug
layer.

### Density Profile

The cluster should not be generated as a uniform sphere. Use a truncated
Plummer-like or King-like profile.

Current implementation:

```text
R_cluster = 100 ly
core_radius = 18 ly

r = core_radius / sqrt(u^(-2/3) - 1)
if r > R_cluster:
    r = R_cluster * random()^(1/3)
direction = random unit vector
position = direction * r
```

This is a compact truncated Plummer-like sampler with a uniform fallback for
out-of-radius samples. It is good enough for the current game because it gives
a dense economic core, a sparse halo, and true `x/y/z` positions.

Minimal Plummer density:

```text
rho(r) = rho0 * (1 + r^2 / a^2)^(-5/2)
```

Enclosed mass fraction:

```text
M(<r) / M = r^3 / (r^2 + a^2)^(3/2)
```

Sampling:

```text
u = random(0, 1)
r = a / sqrt(u^(-2/3) - 1)
reject and resample if r > R_tidal
direction = random unit vector
position = direction * r
```

This gives:

- dense core;
- useful mid-radius trade belt;
- sparse outer systems;
- natural route hubs;
- central encounter pressure.

### Local Cluster Fields

Each star/system should eventually have:

```text
radiusFromCore
localDensity
neighborCount5ly
neighborCount10ly
encounterStress
routeCentrality
clusterPopulationId
```

Suggested formulas:

```text
localDensity = rho(r) / rho0
encounterStress = localDensity * ageGyr
```

`encounterStress` is not magic danger. It is a proxy for long-term close
encounters, disk stripping, comet-cloud disruption, and historical orbital
instability. It should influence:

- survival of wide debris belts;
- volatile retention;
- frequency of stripped systems;
- route hazard;
- old infrastructure density;
- local defense need.

It should not simply delete resources. Dense old regions can be poor in
volatiles but rich in debris, salvage, remnant metals, infrastructure, and
market traffic.

## Rare Enriched Globular Cluster

The cluster is not a typical old, metal-poor globular cluster. The cleanest
physical justification is that it is the stripped nucleus of a dwarf galaxy or
an unusually massive globular cluster that retained material through several
formation episodes.

Canonical population mix:

```text
ancient population:
    fraction = 75..88%
    age      = 9..12 Gyr
    [Fe/H]   = -1.1..-0.5

enriched population:
    fraction = 10..22%
    age      = 2..6 Gyr
    [Fe/H]   = -0.5..0.0

blue stragglers / rejuvenated stars:
    fraction = 0.5..3%
    age tag  = collision/merger product
    hot      = true
    rare     = true

remnant/debris systems:
    fraction = generated by age, mass, encounterStress
    types    = white dwarf, neutron star, black hole candidate, debris field
```

Gameplay consequences:

- there are enough metals for rocky systems and industry;
- young or rejuvenated stars justify habitable or semi-habitable systems;
- dense old regions justify remnant/debris markets;
- rare r-process and s-process pockets justify heavy-element trade;
- no FTL or exotic civilization premise is required.

## Stellar Classes As Generation Inputs

Spectral class is not a market type. It is a physical input to generation.

A star class can affect:

- luminosity;
- habitable zone;
- radiation;
- flare risk;
- disk retention;
- volatile retention;
- lifetime stability;
- operating cost;
- survey uncertainty.

It must not directly say:

```text
M star = cheap iron
G star = food world
A star = research market
```

Those outcomes should emerge from reservoirs, population, industry, trade
routes, ownership, and demand.

### Suggested Stellar Fields

```text
formationPopulationId
ageGyr
massSolar
luminositySolar
temperatureK
spectralClassId
remnantTypeId
metallicityFeH
alphaFe
radiation
flareRisk
habitableZoneAU
lifetimeStability
diskRetention
volatileRetention
rockyReservoir
gasReservoir
debrisReservoir
```

### Minimal Stellar Formulas

Main-sequence lifetime:

```text
t_ms_gyr = 10 * massSolar^(-2.5)
```

If:

```text
ageGyr > t_ms_gyr
```

then the system becomes a remnant candidate. The exact remnant type can be
generated from mass, age, and random seed.

Approximate luminosity:

```text
if M < 0.43:
    luminositySolar = 0.23 * M^2.3
else if M < 2.0:
    luminositySolar = M^4.0
else:
    luminositySolar = 1.5 * M^3.5
```

Approximate temperature:

```text
temperatureK = 5772 * M^0.55
```

Habitable zone center:

```text
habitableZoneAU = sqrt(luminositySolar)
```

Temperature bins:

```text
M: < 3700 K
K: 3700..5200 K
G: 5200..6000 K
F: 6000..7500 K
A: 7500..10000 K
B: 10000..30000 K
O: > 30000 K, practically absent in this setting
```

Gameplay interpretation:

- M/K: common, long-lived, lower luminosity, possible flare and tidal-lock cost.
- G/F: good colonial candidates, less common.
- A/B: rare young/rejuvenated systems, high radiation, short stability.
- Remnant: low habitability, high debris/metal/salvage potential.

## System Reservoir Model

Resources are generated from system reservoirs, not from stellar cores.

Every system can have:

```text
diskMass
rockyReservoir
volatileReservoir
gasReservoir
debrisReservoir
industrialStockpile
salvageReservoir
```

Suggested formula:

```text
diskMass =
    diskBase
  * massSolar^1.2
  * (0.2 + 10^[Fe/H])
  * diskRetention
  * lognormal(0, 0.35)
```

Reservoir split:

```text
rockyReservoir =
    diskMass * refractoryRichness

volatileReservoir =
    diskMass * volatileRetention

gasReservoir =
    diskMass * gasGiantChance * gasRetention

debrisReservoir =
    diskMass * (0.2 + encounterStress)

salvageReservoir =
    infrastructureAge * conflictHistory * recoveryFactor
```

Disk retention:

```text
diskRetention =
    exp(-k_strip * encounterStress) * lognormal(0, sigma)
```

Volatile retention:

```text
volatileRetention =
    diskRetention
  * exp(-k_lum * sqrt(luminositySolar))
  * outerSystemBonus
```

This model makes central dense regions less friendly to fragile volatile belts
but not empty.

## Element Trait Model

Each element should eventually derive or store traits beyond base abundance and
base price.

Recommended traits:

```text
Z
symbol
name
abundanceWeight
demandWeight
basePrice
nucleosynthesisGroup
volatility01
refractory01
bioUse01
industrialUse01
radioactive01
toxicity01
catalytic01
structural01
electronics01
energyUse01
```

Nucleosynthesis groups:

```text
primordial
volatile
alpha
ironPeak
sProcess
rProcess
actinide
synthetic
```

These groups are data tags. Systems should use tags and scalar weights, not
element-specific branches.

## Resource Generation Formula

For system `s` and element `e`:

```text
amount[s,e] =
    abundanceWeight[e]
  * reservoirScale[s,e]
  * metallicityScale[s,e]
  * nucleosynthesisBias[s,e]
  * retention[s,e]
  * localPocket[s,e]
  * lognormalNoise
```

Metallicity:

```text
metallicityScale =
    1          for H, He
    10^[Fe/H] for Z > 2
```

Alpha enhancement:

```text
alphaBias =
    10^[alpha/Fe] for O, Ne, Mg, Si, S, Ca, Ti
```

Retention:

```text
retention =
    mix(volatileRetention, refractoryRetention, refractory01[e])
```

Rare pockets:

```text
sProcessPocket =
    Bernoulli(0.06) ? lognormal(log(3), 0.5) : 1

rProcessPocket =
    Bernoulli(0.015) ? lognormal(log(10), 0.8) : 1

actinidePocket =
    rProcessPocket * lognormal(0, 0.25)
```

Pocket fields should be generated per system, not per frame.

## Habitability Model

Habitability is a physical input to settlement, not destiny. A system with low
habitability can still be industrially important.

Suggested formula:

```text
habitability =
    clamp01(
        0.30 * rockyReservoirNorm
      + 0.35 * volatileRetention
      + 0.20 * lifetimeStability
      - 0.25 * radiation
      - 0.15 * encounterStress
      + noise
    )
```

Lifetime stability:

```text
lifetimeStability = clamp01((t_ms_gyr - ageGyr) / 5)
```

Settlement score:

```text
settlementScore =
    1.2 * habitability
  + 0.8 * log(1 + industrialResourceValue)
  + 0.6 * routeCentrality
  - 0.5 * radiation
  - 0.3 * encounterStress
```

Population:

```text
population =
    popScale * sigmoid(settlementScore) * lognormal(0, sigma)
```

Industry:

```text
industry =
    industryBase
  * roleFactor
  * (0.5 + resourceValue)
  * infrastructureAge
```

## Local Market Formula

Markets consume generated resources and local demand.

For element `e`:

```text
need[e] =
    (population * popNeed[e] + industry * industryNeed[e])
  * roleDemand[role,e]
  * proceduralDemandBias[e]
```

Production:

```text
productionRate[e] =
    extractionEfficiency[role,e]
  * infrastructure
  * recoverableAmount[e]
  / depletionTime[e]
```

Price:

```text
price[e] =
    basePrice[e]
  * ((demandStock[e] + demandRate[e] * T + eps)
    / (supplyStock[e] + productionRate[e] * T + eps))^elasticity
```

There is no global price. A global-looking price is only an aggregate of delayed
local reports.

## Nuclear Energy Model

Nuclear energy is not "energy of an element." It is energy released when nuclear
matter moves toward the peak of binding energy per nucleon.

The peak is around Fe/Ni. Therefore:

- light nuclei can release energy through fusion;
- heavy nuclei can release energy through fission;
- nuclei near Fe/Ni are energy-poor as fuel;
- Fe/Ni remain valuable as industrial materials;
- reactors pay activation and handling costs;
- markets trade elements, but reactors use effective isotope fractions.

### Nuclear Data Fields

Recommended per-element nuclear fields:

```text
Z
symbol
A_ref
B_ref
stable_fraction
fissile_fraction
fertile_fraction
fusion_fraction
activation_mev
handling_risk
decay_heat
radiation_risk
```

Definitions:

```text
A_ref             = representative mass number
B_ref             = binding energy per nucleon, MeV/nucleon
stable_fraction   = long-lived useful stock, 0..1
fissile_fraction  = directly fission-usable isotope fraction, 0..1
fertile_fraction  = breedable isotope fraction, 0..1
fusion_fraction   = fusion-usable isotope fraction, 0..1
activation_mev    = generic activation/handling cost
handling_risk     = radiological and engineering penalty, 0..1
```

`B_ref` can be stored in data. If not available, it can be approximated by a
smooth binding curve. Prefer data over code branches.

### Binding Energy Peak

Canonical constants:

```text
B_peak = 8.79 MeV/nucleon
Z_peak = 26.5
```

Derived values:

```text
binding_deficit(Z) =
    max(0, B_peak - B_ref[Z])

iron_distance(Z) =
    abs(Z - Z_peak) / Z_peak
```

The smaller `binding_deficit`, the lower the theoretical nuclear fuel value.

### Fusion Model

Fusion is useful mainly for light elements.

```text
light_factor(Z) =
    clamp((Z_peak - Z) / Z_peak, 0, 1)
```

Raw fusion output:

```text
fusion_raw_mev_per_nucleon(Z) =
    max(0, B_peak - B_ref[Z]) * light_factor(Z)
```

Activation cost:

```text
fusion_activation_mev(Z) =
    fusion_base
  + charge_barrier * Z^2 / A_ref[Z]^(1/3)
  + isotope_separation_cost[Z]
  + handling_risk[Z] * risk_cost
```

Net energy:

```text
fusion_net_energy(Z, mass) =
    mass
  * fusion_fraction[Z]
  * max(0, fusion_raw_mev_per_nucleon(Z) - fusion_activation_mev(Z))
  * fusion_efficiency
```

Interpretation:

- H, deuterium-like H stock, He-3-like He stock, and Li are high-value fusion
  inputs.
- C/O/Si fusion is difficult and poor for normal industry.
- Fe/Ni fusion is not a useful energy source.
- Higher `Z` raises confinement and activation burden.

### Fission Model

Fission is useful mainly for heavy nuclei, and only a fraction of an element is
useful fuel.

```text
heavy_factor(Z) =
    clamp((Z - Z_peak) / (118 - Z_peak), 0, 1)
```

Approximate fragment target:

```text
Z_frag = max(1, Z / 2)
```

Raw fission output:

```text
fission_raw_mev_per_nucleon(Z) =
    max(0, B_ref[Z_frag] - B_ref[Z]) * heavy_factor(Z)
```

Alternative when `Z_frag` is not available:

```text
fission_raw_mev_per_nucleon(Z) =
    max(0, B_peak - B_ref[Z])
  * heavy_factor(Z)
  * fragment_quality
```

with:

```text
fragment_quality = 0.55..0.85
```

Activation:

```text
fission_activation_mev(Z) =
    fission_base
  + neutron_start_cost
  + isotope_separation_cost[Z]
  - fissile_fraction[Z] * chain_reaction_bonus
  + handling_risk[Z] * risk_cost
```

Net energy:

```text
fission_net_energy(Z, mass) =
    mass
  * fissile_fraction[Z]
  * max(0, fission_raw_mev_per_nucleon(Z) - fission_activation_mev(Z))
  * fission_efficiency
```

### Fissile Fraction

Default fissile fraction can be generated without named-element logic:

```text
fissile_fraction_default(Z) =
    heavy_factor(Z)^p
  * instability_window(Z)
  * isotope_access(Z)
```

where:

```text
p = 1.5..2.5
```

Instability window:

```text
instability_window(Z) =
    clamp((Z - 82) / 12, 0, 1)
  * clamp((112 - Z) / 20, 0, 1)
```

Breeding:

```text
effective_fissile_fraction =
    fissile_fraction
  + fertile_fraction * breeder_efficiency
```

Consequences:

- very heavy elements can have high potential;
- lead-like heavy stable material is not automatically fuel;
- fertile material becomes valuable if the local tech chain supports breeding;
- short-lived synthetic stocks have high handling risk and logistics penalty.

### Energy Gameplay Interpretation

The economic model should treat nuclear fuel as:

```text
element stock
* useful isotope fraction
* local processing tech
* reactor efficiency
* handling/risk cost
```

Fuel value is local. A system with good enrichment industry can make a mediocre
stock useful. A frontier system may have raw actinides but lack processing.

## Reactor And Power Model

Power plants and ship drives should not simply consume "fuel" as a generic
resource. They should consume a bundle:

```text
fuel_element
effective_isotope_fraction
reactor_mode
activation_energy
thermal_capacity
radiator_capacity
maintenance_materials
```

Reactor modes:

```text
fusion_thermal
fusion_electric
fission_thermal
breeder_fission
hybrid_startup
storage_discharge
```

Power output:

```text
gross_power =
    fuel_mass_rate
  * nuclear_net_energy
  * reactor_efficiency
```

Useful power:

```text
net_power =
    gross_power
  - activation_power
  - containment_power
  - processing_power
```

Waste heat:

```text
heat_generated =
    gross_power * (1 - reactor_efficiency)
  + activation_power
  + drive_loss
  + industry_loss
```

Thermal stress:

```text
thermal_stress =
    max(0, heat_generated - radiator_capacity) / heat_capacity
```

Gameplay consequences:

- faster travel raises heat and maintenance;
- high-output reactors need radiator materials;
- hot ships are easier to detect;
- industrial colonies consume radiator, coolant, and structural materials;
- cold outer systems are useful for storage and computation;
- dense/radiation-heavy systems have higher operating cost.

## Propulsion Model

Ships remain sublight.

Recommended fields:

```text
dryMass
cargoMass
drivePower
reactorPower
radiatorCapacity
acceleration
cruiseSpeed
thermalStress
maintenanceState
```

Travel approximation:

```text
distance = dist3d(origin, destination)
cruiseSpeed = clamp(ship.designSpeed, 0, localLimit)
travel_time = distance / cruiseSpeed
```

Current prototype acceleration estimate:

```text
accel_distance = max_speed^2 / acceleration

if distance <= accel_distance:
    travel_time = 2 * sqrt(distance / acceleration)
else:
    travel_time = distance / max_speed + max_speed / acceleration
```

Fuel/energy cost:

```text
drive_energy_cost =
    distance
  * ship_mass
  * speed^2
  * drive_loss_factor
```

This is a gameplay approximation. It keeps speed economically meaningful:
going twice as fast should cost more than twice as much.

## Chemistry Model

Chemistry is a simplified combinatorial model. It should produce plausible
materials and molecules from element properties without hardcoded recipes.

It is not quantum chemistry.

Each element derives:

- nearest inert gas shell;
- valence electrons;
- octet need;
- radius;
- electronegativity;
- reactivity;
- metallicity;
- bond affinity;
- material role.

## Shell And Octet Model

Game shell sizes:

```text
periodSizes = [2, 8, 8, 18, 18, 32, 32]
nobleZ = cumulative(periodSizes)
```

For element `Z`:

```text
period = first p where Z <= nobleZ[p]
periodStart = nobleZ[p - 1] + 1, or 1
slot = Z - periodStart + 1
periodSize = periodSizes[p]
```

Nearest lower noble gas:

```text
coreZ = nobleZ[p - 1], or 0
outerElectrons = Z - coreZ
```

Compressed main shell slot:

```text
mainSlot = compressed slot inside outer s/p shell
valenceElectrons = clamp(mainSlot, 1, 8)
```

Transition and inner shells:

```text
innerLoad = max(0, periodSize - 8)
transitionFactor = innerLoad / periodSize
```

Octet:

```text
octetNeed = 8 - valenceElectrons
octetExcess = max(0, valenceElectrons - 4)
covalentCapacity = min(valenceElectrons, 8 - valenceElectrons)
```

First period uses duet behavior:

```text
duetNeed = 2 - valenceElectrons
capacity = min(valenceElectrons, duetNeed)
```

Interpretation:

```text
1 valence electron  = donor, alkali-like
2 valence electrons = donor, structural metal-like
3 valence electrons = network/ligand/metal mix
4 valence electrons = framework element
5 valence electrons = donor-acceptor behavior
6 valence electrons = divalent acceptor
7 valence electrons = strong acceptor
8 valence electrons = inert gas behavior
```

## Electronegativity And Radius

Game electronegativity:

```text
periodNorm = (period - 1) / maxPeriod
slotNorm = (valenceElectrons - 1) / 7
metalPenalty = transitionFactor * 0.25

electronegativity =
    0.6
  + 2.8 * slotNorm
  - 0.6 * periodNorm
  - metalPenalty

electronegativity = clamp(electronegativity, 0.5, 4.0)
```

Noble gases:

```text
if valenceElectrons == 8:
    reactivity = near 0
```

Radius:

```text
radius =
    baseRadius
  + period * periodScale
  - slotNorm * contraction
  + transitionFactor * metalExpansion
```

Example:

```text
radius = 40 + 18 * period - 12 * slotNorm + 10 * transitionFactor
```

Radius affects:

- density;
- packing;
- molecule size;
- alloy compatibility;
- melting point;
- brittleness;
- transport cost for bulk materials if represented.

## Bond Model

For elements `A` and `B`:

```text
dEN = abs(EN[A] - EN[B])
sizeFit = 1 - abs(radius[A] - radius[B]) / max(radius[A], radius[B])
octetFit = min(openValence[A], openValence[B]) / max(openValence[A], openValence[B])
reactivityFit = reactivity[A] * reactivity[B]
```

Bond affinity:

```text
bondAffinity =
    reactivityFit
  * (0.45 + 0.35 * sizeFit + 0.20 * octetFit)
```

Bond type:

```text
if both metallic:
    metallic
else if dEN > 1.6:
    ionic
else if dEN > 0.4:
    polar_covalent
else:
    covalent
```

Bond strength:

```text
bondStrength =
    bondAffinity
  * ionic_bonus
  * covalent_octet_bonus
```

Where:

```text
ionic_bonus =
    1.0 + 0.35 * dEN for ionic, else 1.0

covalent_octet_bonus =
    1.0 + 0.25 * octetFit for covalent, else 1.0
```

## Molecule Constructor

The molecule constructor is a future system that generates useful compounds
from available elements.

Each atom has:

```text
openValence = covalentCapacity
```

Generation:

1. Select seed element by availability, price, role, biome, or industry.
2. Score candidate elements by `bondAffinity`.
3. Add bonds that close valence slots.
4. Stop at `atoms <= techLimit` or environment limit.
5. Validate stability.

Stability:

```text
stable =
    totalOpenValence <= allowedDangling
and averageBondStrength >= threshold
and abs(chargeBalance) <= chargeTolerance
```

Charge tendency:

```text
chargeTendency = valenceElectrons - 4
moleculeCharge = sum(chargeTendency * ionicWeight)
```

Molecule properties:

```text
molecularMass = sum(atomicMassProxy)
reactivity = exposedOpenValence * environmentAffinity
volatility = inverse(bondStrength * molecularMass)
toxicity = weighted element toxicity
energyUse = weighted nuclear/chemical usefulness
industrialValue = function(properties, local demand)
```

Molecules should be small, legible, and useful in UI. Large bulk materials
should use the material constructor instead.

## Material Constructor

Materials can be statistical compositions rather than explicit molecules.

Material classes:

```text
alloy
salt
ceramic
covalent_lattice
glass
amorphous_solid
refractory
superconductor_candidate
catalyst
radiator_material
shielding_material
reactor_material
```

Composition scores:

```text
metallicity =
    average(1 - slotNorm) + average(transitionFactor)

ionicity =
    average pair dEN

networkPotential =
    average(covalentCapacity)
  * average(bondStrength)
  * sizeFit

glassPotential =
    networkPotential
  * compositionDiversity
  * disorder

alloyPotential =
    metallicity
  * radiusCompatibility
  * valenceSmearing
```

Classification:

```text
high metallicity              -> alloy / conductor
high ionicity                 -> salt / ceramic
high networkPotential         -> covalent lattice
high glassPotential           -> glass / amorphous solid
high transitionFactor + dense -> refractory material
```

Material properties:

```text
atomicMassProxy = Z * massScale

density =
    sum(atomicMassProxy) / packingVolume

meltingPoint =
    averageBondStrength * packingEfficiency * massFactor

conductivity =
    metallicity * electronMobility - latticeDisorder

brittleness =
    ionicity + networkRigidity - metallicity

reactivity =
    exposedOpenValence * environmentAffinity

radiatorQuality =
    thermalConductivity * highTempStability / density

reactorQuality =
    radiationTolerance * meltingPoint * corrosionResistance
```

This creates recipes procedurally. A system can ask for a material class and
the constructor can search local stocks for a composition that satisfies it.

## Chemistry Non-Goals

The chemistry model must not:

- hardcode every common real molecule;
- require exact orbital simulation;
- require named-element exceptions in runtime systems;
- create unbounded molecule graphs in hot loops;
- hide material properties behind flavor names.

Allowed hardcoding:

- universal constants;
- period sizes;
- noble gas shell boundaries;
- data table values for elements;
- recipe seeds for tutorial/debug if clearly marked.

## 3D Cluster Dynamics

The game and engine are physically 3D. Stars, ships, velocities, route lengths,
signal delays, and visibility radii use three coordinates. The current SDL2 map
is a 2D projection of 3D simulation state.

Normal distance:

```text
dist3d(a, b) =
    sqrt((ax - bx)^2 + (ay - by)^2 + (az - bz)^2)
```

Map projection is UI. Travel, signal delay, route risk, and market distance use
3D positions.

## Smooth Cluster Potential

Do not simulate 10k-star N-body gravity. Use a smooth central potential.

Plummer-like acceleration:

```text
accel(p) =
    -mu * p / (dot(p, p) + core_radius^2)^(3/2)
```

Near the core this behaves like a soft spring toward the center of mass. In the
outer cluster it behaves more like orbital motion in a shared potential.

This is preferable to literal springs:

- no pairwise force scans;
- stable long-term motion;
- dense core is natural;
- star movement can be analytic;
- gameplay remains deterministic.

## Analytic Star Motion

Recommended star motion fields:

```text
star_pos0[id]
orbit_axis[id]
orbit_phase[id]
orbit_omega[id]
orbit_eccentricity[id]
radial_phase[id]
radial_omega[id]
```

Orbital frequency:

```text
omega(r) =
    sqrt(mu / (r^2 + core_radius^2)^(3/2))
```

Position:

```text
pos(t) =
    rotate_around_axis(pos0, orbit_axis, orbit_phase + omega * t)
```

Optional radial pulse:

```text
r(t) =
    r0 * (1 + e * sin(radial_phase + radial_omega * t))
```

Star motion should be slow. Over a few years the map is almost fixed. Over
decades or centuries, route distances and old catalog data can drift enough to
matter.

## Signal Arrival With Moving Stars

Base rule:

```text
arrival_time = send_time + distance_ly
```

For moving destinations, use one refinement step:

```text
t0 = send_time
d0 = dist(pos(origin, t0), pos(dest, t0))
t1 = t0 + d0
d1 = dist(pos(origin, t0), pos(dest, t1))
arrival = t0 + d1
```

Every delayed information payload should store:

```text
observed_time
send_time
arrival_time
origin_system
destination_system
payload_type
payload_id
```

Signal-backed data:

- ownership reports;
- market prices;
- bank settlement;
- faction orders;
- combat reports;
- treaty state;
- route hazards;
- scout reports;
- player knowledge overlay.

## Knowledge Uncertainty

The UI can show stale data, but should expose uncertainty.

For market data:

```text
infoAge = now - observedTime
confidence = exp(-infoAge / tauMarket[e])
```

Displayed price range:

```text
shownPriceLow =
    lastKnownPrice * exp(-volatility[e] * sqrt(infoAge))

shownPriceHigh =
    lastKnownPrice * exp( volatility[e] * sqrt(infoAge))
```

For ownership:

```text
ownerConfidence =
    exp(-infoAge / tauPolitical)
```

For fleet reports:

```text
positionUncertainty =
    lastKnownSpeedMax * infoAge + sensorError
```

Normal play should show:

- live local data;
- last-known remote data;
- age of data;
- confidence or uncertainty range.

Debug mode may show truth.

## Route Evaluation

Route value is not only price spread.

Expected profit:

```text
expected_profit =
    cargo_value_delta
  - energy_cost
  - maintenance_cost
  - tariff
  - banking_cost
  - time_cost_of_capital
  - risk_loss
  - stale_price_penalty
```

Travel:

```text
distance_3d = dist3d(origin, dest)
travel_time = distance_3d / cruise_speed
signal_delay = distance_3d
data_age = now - observed_time
```

Risk:

```text
route_risk =
    1 - exp(-sum(local_hazard_rate * segment_time))
```

Hazard components:

- war state;
- piracy or raiding;
- blockade;
- hostile owner relation;
- high-value cargo;
- stale route reports;
- no friendly stations;
- radiation;
- remnant field debris;
- core encounter stress.

Risk loss:

```text
risk_loss =
    cargo_value * route_risk * loss_severity
```

Stale price penalty:

```text
stale_price_penalty =
    cargo_value * (1 - confidence)
```

## Relativistic Banking Model

Money is local first and settled later.

Fields:

```text
local_balance[system][faction]
pending_settlements[]
known_balance[observer_system][faction]
unsettled_exposure[system][faction]
credit_limit[system][faction]
```

Transaction flow:

1. A local trade, tax, repair, build, or fleet expense changes local balance.
2. A settlement signal is emitted.
3. The signal moves at light speed.
4. Receiving systems update known balance.
5. Faction AI uses known balances, not instant truth.

Credit limit:

```text
credit_limit =
    baseTrust
  * relationTrust
  * exp(-dataAge / tauCredit)
  * collateral
  * securityFactor
```

Liquidity stress:

```text
liquidityStress =
    max(0, local_spend_commitments - local_balance - credit_limit)
```

Consequences:

- remote capital is stale;
- local colonies can be rich while faction capital estimate is outdated;
- wars can create settlement shock;
- delayed reports can cause overextension;
- player trading stays usable through local terminal credit.

## Faction Capital Model

Faction treasury should eventually split into:

```text
local balances
settled capital known at capital system
unsettled receivables
unsettled obligations
credit capacity
```

Strategic spending should use:

```text
available_capital =
    known_local_balance
  + credit_limit
  + matured_settlements
  - committed_spending
```

The current simple `treasury` can remain as a temporary aggregate, but the lore
canon favors delayed distributed ledgers.

## Thermal Economy

Heat is a universal cost of power.

For ships:

```text
heat_generated =
    reactor_loss
  + drive_power_loss
  + weapon_loss
  + processing_loss

thermal_stress =
    max(0, heat_generated - radiator_capacity) / heat_capacity
```

For colonies:

```text
industrial_heat =
    industry_output * inefficiency

cooling_margin =
    radiator_capacity + environmental_sink - industrial_heat
```

Consequences:

- high-speed ships pay maintenance and detection penalties;
- energy-rich systems still need radiator materials;
- cold and dark systems can host data centers or storage;
- dense industrial systems consume structural and thermal materials;
- thermal bottlenecks create demand for high-conductivity and refractory goods.

## Detection Model

No omniscient remote fleet view.

Detection can depend on:

```text
distance
ship_heat
drive_power
local_sensor_network
background_radiation
ownership
signal_age
```

Local detection:

```text
detectionScore =
    sensorStrength
  + log(1 + targetHeat)
  + log(1 + drivePower)
  - distancePenalty
  - backgroundNoise
```

Remote reports are signals. A detected fleet at time `t` becomes remote
knowledge only after the report arrives.

## Warfare Physical Constraints

War is an economic process with physical delays.

Constraints:

- fleets must travel;
- orders are delayed unless preplanned locally;
- blockades affect local market access;
- conquest changes ownership locally first;
- remote faction knowledge updates by signal;
- defense depends on infrastructure, local fleets, and stale intelligence.

A war system should not instantly flip global knowledge or global capital.

## Procedural Role Model

System roles are economic summaries, not hardcoded destiny.

Roles can be generated from scores:

```text
habitatScore =
    habitability
  + volatileReservoir
  + routeCentrality
  - radiation

refineryScore =
    rockyReservoir
  + debrisReservoir
  + energyAvailability
  - coolingCost

shipyardScore =
    metalSupply
  + reactorMaterialSupply
  + routeCentrality
  + defense

researchScore =
    rareElementAccess
  + coldSink
  + signalCentrality
  + highSkillPopulation

militaryScore =
    routeChoke
  + borderPressure
  + industrialBase
  + defensePosition

frontierScore =
    lowPopulation
  + highUncertainty
  + resourcePotential
```

Pick role by weighted score and noise. Do not cycle roles by star index in the
final generator except as a temporary placeholder.

## Data-Oriented Field Sets

Future high-scale implementation should move toward flat arrays.

Current object fields are already shaped around the same data:

```text
ClusterStar:
    x, y, z
    name
    economyRole
    population
    industry
    habitability
    defense
    ownerFaction
    resources[]
    demandBias[]
    resourceFocus[]
    demandFocus[]

Ship:
    x, y, z
    vx, vy, vz
    speed
    acceleration
    cargoCapacity
    ownerFaction
    targetStar
    enRoute
```

Stars:

```text
stars.x[]
stars.y[]
stars.z[]
stars.massSolar[]
stars.luminositySolar[]
stars.temperatureK[]
stars.metallicityFeH[]
stars.alphaFe[]
stars.localDensity[]
stars.encounterStress[]
stars.habitability[]
stars.ownerFaction[]
```

Star motion:

```text
stars.pos0x[]
stars.pos0y[]
stars.pos0z[]
stars.orbitAxisX[]
stars.orbitAxisY[]
stars.orbitAxisZ[]
stars.omega[]
stars.phase[]
stars.eccentricity[]
```

Markets:

```text
market.supply[system][resource]
market.demand[system][resource]
market.price[system][resource]
market.productionRate[system][resource]
market.demandRate[system][resource]
```

Signals:

```text
signals.origin[]
signals.destination[]
signals.observedTime[]
signals.sendTime[]
signals.arrivalTime[]
signals.payloadType[]
signals.payloadIndex[]
```

Agents:

```text
agents.x[]
agents.y[]
agents.z[]
agents.vx[]
agents.vy[]
agents.vz[]
agents.ownerFaction[]
agents.currentOrder[]
agents.destStar[]
agents.arrivalTime[]
agents.heat[]
agents.cargoMass[]
```

Banking:

```text
localBalance[system][faction]
knownBalance[observerSystem][faction]
pendingSettlement.amount[]
pendingSettlement.origin[]
pendingSettlement.destination[]
pendingSettlement.arrivalTime[]
```

## Model Priority

Implementation should happen in this order:

1. Keep the current element market and local resource system working.
2. Add star/system physical fields as generation data.
3. Refine the current truncated Plummer-like generation with local density and route-centrality fields.
4. Add spectral class fields as generation inputs.
5. Add resource reservoirs and element traits.
6. Add delayed signals for ownership and market reports.
7. Add route profit including data age and travel time.
8. Add nuclear energy traits for fuel and reactor economics.
9. Add distributed banking.
10. Add chemistry/material constructor for industry demands.
11. Add analytic star motion if routes and signal delay benefit from it.

This order keeps the game playable while increasing physical depth.

## Canonical Non-Hardcoding Rule

The final game should not contain code that says:

```text
if element == "Fe": do special economy
if spectralClass == "G": make habitat
if factionName == "Aster Compact": give market bonus
if starIndex % 6 == 0: refinery forever
```

Acceptable:

```text
elementTraits[Fe].structural01 is high
spectralClass.G affects luminosity and lifetime stability
faction policy weights affect trade and aggression
role score chooses refinery from local resource and route data
```

This is the difference between content data and hardcoded outcomes.
