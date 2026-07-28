# Starcluster

Starcluster is a real-time C++/SDL2 space economy sandbox set inside one dense
globular star cluster. The simulation is deliberately sublight: ships travel in
3D space at fractions of light speed, signals move at light speed, and remote
information becomes stale instead of instantly updating a global map.

The player is not a unique hero object. The player starts as one ship and one
small faction inside the same market, travel, contract, colony, faction, signal,
and fleet rules used by NPC agents.

## Current Status

The current executable prototype contains:

- a deterministic 10,000 star cluster (`STAR_COUNT = 10000`);
- local markets for all 118 chemical elements;
- six NPC factions plus the player faction;
- traders, patrol ships, colonists, scouts, pirates, adventurers, and the player;
- 3D ship positions, velocity, acceleration, fuel, mass, cargo, and route travel;
- local market memory, faction knowledge, and light-speed signal packets;
- delivery, courier, scout, bounty, escort, raid, and colony supply contracts;
- player colony founding, reinforcement, construction queues, and ship hiring;
- faction relation drift, strategic orders, budgets, and influence overlay;
- save/load to `starcluster.save`;
- SDL2 map rendering, HUD panels, draggable system/trade/contract windows, and
  a smoke mode for short launch checks.

It also contains a **local flight mode** ("microworld") entered from the cluster
map. Inside a chosen star system the camera switches to a first-person / chase
perspective (the only place in the game that uses a perspective projection; the
cluster map stays orthographic) and the player flies a ship in 3D around:

- a procedurally generated system: one central star, rocky/gas/ice planets on
  Keplerian orbits, moons, an asteroid belt, a station, and radio sources;
- a giant star rendered as a real sphere of plasma by a per-pixel analytic
  ray-sphere **software shader** (isotropic — correct from every view direction,
  animated turbulence, limb darkening, corona), not a flat sprite or a fill;
- local NPC ships (traders, pirates), mining, docking, and scooping interactions.

The local mode is fully deterministic and self-contained: it draws its own seeded
RNG and never perturbs the global cluster simulation's RNG stream.

The prototype is still early. Some documents describe target systems that are
only partially implemented, especially full combat modules, ship upgrades,
diplomatic UI, OpenGL rendering, and large-scale performance passes.

## Build And Run

### Requirements

- C++ compiler with C++11 support.
- SDL2 development package providing `sdl2-config`.
- `make`.

Examples:

```bash
# macOS with Homebrew
brew install sdl2

# Debian/Ubuntu
sudo apt install build-essential libsdl2-dev
```

The active build path is the root `Makefile`. It compiles (C++11, `-O3`, warnings
clean):

```text
main.cpp game.cpp cluster.cpp resource.cpp market.cpp ship.cpp agent.cpp
colony.cpp faction.cpp ui.cpp mining.cpp combat.cpp spaceevents.cpp
anomaly.cpp modules.cpp chromo.cpp render2d.cpp localgen.cpp localsim.cpp
localdraw.cpp
```

Build and run:

```bash
make
./game
```

Clean generated executable:

```bash
make clean
```

### Smoke And Test Targets

```bash
# short launch check of the macro simulation
./game --smoke

# short launch check of the local flight mode (generation + a few sim frames)
./game --localsmoke

# useful on headless machines
SDL_VIDEODRIVER=dummy ./game --smoke

# AddressSanitizer + UBSan build; then run both smokes under it
make asan
./game_asan --smoke && ./game_asan --localsmoke

# long headless soak (120k frames of local sim over many generated systems)
make soak

# render a set of local-mode screenshots to PNG (headless, dummy video driver;
# BMP is converted to PNG via `sips` on macOS)
make shots
```

`0mac_make/Makefile` and `0windows_make/Makefile` are legacy build sketches that
still reference `uni.cpp`; they are not the current project build path.

## Controls

### View And Simulation

- `Left mouse`: select a star, agent, window, or UI button.
- `Right mouse`: route the player ship to the clicked star.
- `Middle mouse drag`: pan the camera.
- `Mouse wheel`: zoom.
- `A` / `D`: rotate yaw.
- `W` / `S` / `X`: rotate pitch.
- Arrow keys: pan.
- `0`: reset view.
- `Space`: pause or resume.
- `1`, `2`, `3`, `4`: set simulation speed to `1x`, `2x`, `5x`, `10x`.
- `Esc`: quit.

### Player Ship And Map

- `Tab`: select next visible agent.
- `P`: select and follow the player ship.
- `F`: follow selected agent.
- `I`: toggle faction influence overlay.
- `G`: route the player ship to the selected star.
- `[` / `]`: change selected element.

### Trade, Contracts, Colonies

- `B`: buy selected element at the player ship's current system.
- `V`: sell current cargo.
- `T`: run auto-trade for the player ship.
- `C`: found a colony at the current system, or reinforce an owned local colony.
- `H`: hire/build a ship at an owned local colony.
- `F5`: save to `starcluster.save`.
- `F9`: load `starcluster.save`.

System windows expose route, trade, and contract actions through mouse buttons.
The trade window uses the periodic table layout for element selection and an
amount field; an empty amount means `MAX`.

### Local Flight Mode ("microworld")

- `L`: enter local flight mode at the selected (or anchor) star; press `L` again
  or `Esc` to leave and return to the cluster map.
- `W`: thrust forward. `S`: brake / reverse thrust.
- `A` / `D`: yaw left / right.
- `R` / `F`: pitch up / down.
- `Q` / `E`: roll left / right.
- `Mouse`: free-look (shooter-style relative aim) while in the 3D cockpit; the
  pointer is captured and hidden. In map view the normal cursor returns.
- `Left Shift` / `Right Shift`: warp / boost.
- `Space` (or left mouse): fire.
- `M`: mine the targeted asteroid. `K`: dock at the targeted station.
- `Tab`: cycle the locked target.
- `C`: toggle between the 3D cockpit and the top-down system map.
- `+` / `-`: zoom the system map.

An on-screen action bar shows the currently available local actions (fire, mine,
dock, target, view, exit) and greys out the ones that are not applicable.

## Game Model

### Cluster

`cluster.cpp` generates a spherical 3D cluster with a dense core and sparse
outer halo:

- hard radius: about 100 light years;
- core radius: about 18 light years;
- deterministic RNG seed for reproducible starts;
- per-star role, population, industry, habitability, defense, owner, resources,
  demand bias, resource focus, and demand focus.

The current economic roles are:

```text
habitat, refinery, shipyard, research, military, frontier
```

### Elements And Resources

`resource.cpp` defines all 118 chemical elements. Element identity is atomic
number; symbol and name are labels. Gameplay traits are derived from universal
formulas rather than one-off resource branches:

- approximate atomic mass;
- shell fill and noble stability;
- oxidizer, reducer, metallic, structural, conductor, and catalyst traits;
- fusion and fission fuel traits;
- nuclear stability, activation cost, and handling risk;
- abundance weight, demand weight, and base price.

Markets, cluster generation, cargo mass, fuel selection, material requirements,
and route costs consume these traits.

### Markets

There is no global market. Every star has one local `Market`:

- supply and demand vectors for every element;
- production and demand rates;
- local role-driven demand;
- price pressure from scarcity;
- slow-ticked updates rather than a full update of all markets every frame.

Price is derived from local supply/demand pressure against each element's base
price. Trader scoring uses known or remembered destination markets, fuel cost,
route risk, stale information penalties, and cargo capacity.

### Ships

`Ship` stores:

- 3D position and velocity;
- max speed as a fraction of light speed;
- acceleration cap;
- dry mass, thrust, drive efficiency;
- fuel element, fuel amount, and fuel capacity;
- cargo vector and cargo capacity;
- owner faction, route target, and en-route state.

Cargo mass comes from resource amount and element atomic mass. Fuel is not cargo:
it is a dedicated reserve of a fuel element selected from element traits.
Acceleration depends on ship mass, and acceleration/braking burn fuel.

### Agents

An `Agent` is any moving actor with a ship and decision state. Active role ids
include:

```text
player, trader, military, colonist, scout, pirate, adventurer
```

Role profiles weight trade, delivery, courier, scout, exploration, patrol, raid,
bounty, escort, colony supply, and risk tolerance. NPC agents evaluate contracts,
trade opportunities, faction orders, scouting targets, colonization targets,
military targets, and piracy opportunities through the same route and fuel model.

### Factions

`Faction` stores identity, color, home star, controlled stars, fleet agents,
treasury estimates, budgets, strength, aggression, risk tolerance, trade bias,
expansion bias, defense bias, strategic pressure values, relations, and orders.

Relations live in a dense matrix:

```text
relations[factionA * factionCount + factionB] = -128..128
```

Factions generate slow-ticked strategic orders for scouting, colonization,
attacking/patrolling, and defending. The influence overlay samples known owned
systems into a coarse cached screen grid.

### Knowledge And Signals

Normal gameplay does not show omniscient truth. `Game` keeps separate knowledge
for factions and the player:

- owner knowledge per faction/star;
- market snapshots per faction/star/element;
- player owner overlay;
- local signal memory;
- pending `SignalPacket` queue.

Signal packets have an observed time, send time, arrival time, origin,
destination, hop star, recipient faction, subject star, and typed payload fields.
Arrival time is based on 3D light-year distance. Older owner and market reports
do not overwrite newer knowledge.

Current signal types:

```text
OwnerReport, MarketReport, ContractReport, CombatReport,
SettlementReport, DiplomacyReport
```

### Contracts

Contracts are local gameplay hooks and AI work items. Current types:

```text
Delivery, Courier, Scout, Bounty, Escort, Raid, ColonySupply
```

Contracts store issuer, origin, target, target agent, resource, amount, reward,
deposit, deadline, risk, progress, accepted agent, completion state, and signal
state. Cargo contracts use normal ship cargo. Scout and report-like contracts
interact with the signal system.

### Colonies And Control

Colonies attach long-term ownership and construction to stars. Current colony
state includes:

- population and role;
- star and owner faction;
- infrastructure, growth, automation, energy capacity, defense;
- shipyard level, market access, damage, local ledger, stockpile value;
- stockpile and construction queue.

The player can found a colony at a free/current system if credits and generated
material requirements are available. Reusing `C` on an owned local colony
reinforces it. Owned colonies can support ship hiring/building through `H`.

### Rendering And UI

The simulation is 3D. The current renderer in `main.cpp` projects the cluster to
an SDL2 2D orthographic map:

- depth fade for stars and agents;
- known/live owner coloring;
- market color for live local element pressure;
- route lines for visible agents;
- optional faction influence overlay;
- title-bar telemetry with time, selected element, selected star, selected
  agent, fuel, mass, cargo, money, and last event;
- custom pixel-font HUD and draggable panels in `ui.cpp`.

SDL_image and SDL_ttf binaries/frameworks exist in the repository folder, but
the active code path draws text with its own bitmap glyphs and links only SDL2.

The **local flight mode** renders differently. It is the one place that uses a
perspective camera (a shared projection helper in `camera.h`; the macro map stays
bit-for-bit orthographic). Bodies, orbits, the station, belt rocks, NPC ships,
and projectiles are drawn in `localdraw.cpp`. The central star is not a sprite: it
is produced by a per-pixel analytic **ray-sphere software shader**
(`renderStarPlasma`) that, for each screen pixel, reconstructs a world-space ray
from the eye, intersects the star sphere analytically, and shades from geometry —
multi-octave domain-warped turbulence, limb darkening, and a soft corona. Because
the shading is a pure function of the ray and the star's centre/radius, it is
isotropic: the star looks like a real ball of plasma from every direction, with no
"fill the screen yellow" shortcut. The shader writes into a half-resolution
streaming `SDL_Texture` (bounding-box limited when the star is far away), so it
stays SDL2-only and keeps the deterministic headless screenshot harness working —
there is still no OpenGL path.

## Source Map

| File | Responsibility |
| --- | --- |
| `main.cpp` | SDL2 window, event loop, camera, map projection, route drawing, influence overlay, frame pacing. |
| `game.h`, `game.cpp` | World composition, update order, saves, routes, markets, agents, contracts, factions, signals, player actions. |
| `cluster.h`, `cluster.cpp` | Star data and procedural cluster generation. |
| `resource.h`, `resource.cpp` | Element definitions, derived traits, resource ids. |
| `market.h`, `market.cpp` | Local supply/demand/production/pricing. |
| `ship.h`, `ship.cpp` | Ship mass, cargo mass, fuel, acceleration, route fuel estimates. |
| `agent.h`, `agent.cpp` | Agent state and role-profile weights. |
| `faction.h`, `faction.cpp` | Faction identity, budgets, relations, strategic fields and orders. |
| `colony.h`, `colony.cpp` | Colony state, construction effects, damage, shipyard capacity. |
| `contract.h` | Contract types and storage. |
| `mining.cpp` | Manual ore mining near a star while docked. |
| `combat.cpp` | In-transit skirmishes and hull repair (instant resolution). |
| `spaceevents.cpp` | Dynamic market events as transient price multipliers. |
| `anomaly.cpp` | Anomalies / derelicts and scanning rewards. |
| `modules.h`, `modules.cpp` | Ship module slots and the upgrade model. |
| `chromo.cpp` | "Chromocore" research cores and accumulation. |
| `ui.h`, `ui.cpp` | HUD, custom text, panels, trade window, contract window, mouse/keyboard UI handling. |
| `render2d.h`, `render2d.cpp` | Low-level 2D primitives and the 5x7 bitmap font, shared by `ui.cpp` and `localdraw.cpp`. |
| `camera.h` | Shared camera/projection: orthographic macro path plus the perspective path used only by local flight mode. |
| `local.h` | Local-mode data structures (`LocalScene`, `LocalInput`) and the `buildLocalCamera` helper. |
| `localgen.cpp` | Procedural generation of a local star system (star, planets, moons, belt, station, radio sources, NPCs). |
| `localsim.cpp` | Local flight simulation: ship flight, thrust, projectiles, mining/docking, NPC behavior. |
| `localdraw.cpp` | Local-mode rendering: bodies, orbits, HUD, and the per-pixel ray-sphere software star shader. |
| `shot_test.cpp` | Headless screenshot harness for local-mode scenarios (`make shots`). |
| `soak_test.cpp` | Headless long-run soak harness for the local simulation (`make soak`). |
| `architecture.md` | High-level architecture and data-oriented rules. |
| `lore.md` | Physical/economic canon and setting constraints. |
| `agents.md`, `merge_plan.md` | Early project identity and migration notes. |
| `elements.md`, `ship.md`, `economy.md`, `factions.md`, `ai.md`, `events.md`, `quests.md`, `combat.md`, `colonies.md`, `expansion_roadmap.md` | Subsystem plans with current status and future work. |

`civ.*`, `galaxy.*`, and `graphic.*` are legacy or inactive paths according to
`architecture.md`.

## Save Files

`F5` writes `starcluster.save` in a text format beginning with:

```text
STARCLUSTER_SAVE 5
```

The save stores RNG state, time, stars, markets, factions, relations, colonies,
contracts, agents, faction knowledge, player knowledge, pending signals, and
signal memory. `F9` loads the same file and rebuilds runtime caches.

## Design Constraints

Important rules from the project documents:

- no faster-than-light travel or communication;
- no instant global market or omniscient strategic map;
- all meaningful remote knowledge must be local, stale, signaled, or debug-only;
- elements are data derived from atomic number, not symbol-specific gameplay;
- systems should use numeric ids and contiguous vectors where practical;
- expensive world-wide work belongs in generation, slow ticks, caches, or
  bounded candidate searches;
- rendering reads state and draws; gameplay decisions stay in simulation code.

## Known Gaps

- Ship modules and an upgrade model exist (`modules.*`), but the full weapons,
  armor, and sensor progression with UI is still early.
- Combat exists through piracy, bounty, raid, threat, capture pressure, and
  instant in-transit skirmish resolution, but not as a detailed ship-module
  combat simulator.
- OpenGL is an architectural target, not the active renderer. Even the local
  flight mode's star is a *software* shader drawn into an SDL streaming texture.
- In local flight mode there is no physical collision with the star or bodies
  (flying "into" the star is cosmetic; only HP can end a flight); planets and gas
  giants are still drawn as flat discs while only the star is a full sphere.
- Non-player faction memory overlays are not exposed through a debug selector.
- The active build is POSIX/SDL2 via `sdl2-config`; the old platform makefiles
  are not synchronized with the current source list.
