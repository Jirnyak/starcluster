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
  animated turbulence, limb darkening, corona), not a flat sprite or a fill —
  with an O→M spectral color contrast (hot stars blue-white, cool stars deep
  red-orange), a chromospheric limb rim, slowly-drifting corona prominences, and
  subtle sunspot cells;
- lit planets and moons drawn by the **same ray-sphere technique**, with the star
  at the system origin as the light source: a real day/night terminator gives them
  phases and crescents, plus per-type surfaces (gas-giant bands, rocky mottling,
  icy poles and albedo, lunar maria) and an atmospheric limb glow;
- **gas-giant rings in perspective**, drawn as a per-pixel ray↔plane annulus in the
  planet's equatorial plane: Cassini gaps, radial banding, a soft planet shadow cast
  on the rings, edge-on opacity gain, and a depth composite so the near arc passes in
  front of the planet while the far arc is occluded behind it — and, reciprocally, the
  rings now cast their own soft shadow band back across the planet's lit face, with the
  Cassini gap letting a bright sliver of light through (a full Saturn light model);
- **lit asteroid boulders**: nearby belt rocks use the same ray-sphere technique with a
  Lambert day/night terminator, an irregular chipped silhouette (carved inward so it
  never overflows its sprite), craters and surface mottling, and a slow tumble that
  turns the surface detail with the rock — distant rocks fall back to cheap phase-lit
  discs. Each boulder is also **typed by its element's composition**: `rockAppearance`
  sorts it into ice/volatile, carbonaceous, metallic, or silicate by atomic number and
  metallicity, giving it a distinct palette and a specularity value — ice and metal
  catch a Blinn-Phong glint on their lit side, carbon and silicate stay matte (silicate
  is the default and matches the belt's prior tan look, so it is a zero-regression base).
  The same four-way classifier also sets how fast each rock mines — metal is the richest
  ore, carbon the poorest — so the glint doubles as a gameplay beacon for the good ore. And
  the **belt's overall makeup now reflects the host system**: metal-rich stars grow metallic
  belts, metal-poor stars icy ones, biased further by the system's economic focus — via a
  deterministic per-rock hash that never perturbs the RNG stream;
- **a volumetric nebula backdrop** in perspective: a per-pixel gas field painted on the
  celestial sphere (color and density are a function of the world ray direction, so it
  turns in lockstep with the skybox stars), built from multi-octave product noise (puffy
  clumps plus filaments), a domain warp, and a large-scale density gradient, plus dark
  dust lanes that absorb the glow and emission-brightened cores at the densest filaments;
  it is translucent so the stars shine through it;
- **a living, goal-driven NPC population**: non-combat ships (traders, civilians, idle
  patrols) run errands — cruise to a market body, linger "docked" around it, then pick a
  new errand — while purely-local ships occasionally depart to the system edge and despawn,
  and fresh traders warp in from the edge on a timer up to a population cap, so traffic flows
  in and out and the system stays alive (pirates keep hunting — their aggression is untouched).
  This traffic is **legible from the cockpit**: arrivals and departures fire a cyan warp
  flash (expanding ring + bright core + sparks), a ship leaving a berth puffs its thrusters,
  a docked ship wears a pulsing amber station-keeping ring, and the radar panel shows an
  `IN / DOCK` tally of how many ships are inbound to a market versus parked;
- **convoy raids you can act on**: when a pirate presses an attack on a trader or other
  non-pirate, the victim raises a distress beacon — a pulsing red **SOS** ring, an off-screen
  edge marker pointing to it, a `CONVOY RAID` toast, and a distinct target-panel state — and
  destroying a pirate while it is threatening a convoy pays out a bonus, research, a
  `CONVOY SAVED` toast, and a positive reputation bump with the victim's faction. Purely
  additive: pirates are **not** nerfed, so the choice to intervene stays a real risk;
- **escort patrols that answer distress**: patrol ships now hear a raid from far off (a wide
  awareness radius) and prioritise intercepting the *raider* — the pirate actually pressing an
  attack on a nearby convoy — instead of chasing whichever pirate is closest; a patrol on an
  intercept wears a pulsing green **escort** ring, an off-screen green edge marker signals that
  help is inbound, and the target panel shows an `ESCORT` state. The patrol still has to fly in
  and fight at weapon range — its awareness grew, not its guns — so pirates stay unnerfed;
- **a star corona that erupts**: the plasma star's corona is no longer static — two active
  regions drift along the limb and periodically flare, brightening, bulging outward, and
  shifting toward a hot blue-white as they erupt. The cycle is fully deterministic (driven
  only by the effect clock) and vanishes to under 2% at rest, so a quiet star stays isotropic;
- **local kills that stick**: destroying a local NPC ship that mirrors a persistent macro agent
  now writes back to the macro world — that agent's ship is downgraded to an Escape Pod and its
  cargo jettisoned (credits are kept), a permanent consequence that outlasts the flight. Purely
  additive and deterministic; the faction reprisal for the kill is applied once, not double-counted;
- **a mining beam you can see**: while a rock is actually being extracted (target locked *and*
  within mining range, the same test the simulation uses to add ore), a laser reaches from the
  cockpit muzzle to the boulder, its halo **tinted by the rock's class color** (steel for metal,
  blue-white for ice, dark for carbon) over a hot near-white core, with a hum-pulse, an impact
  glow on the rock, and a muzzle spark. The pulse is deterministic (driven only by the effect
  clock, no RNG), and the beam is draw-only — it renders nothing when you are not mining or drift
  out of range, closing the "see it → know its worth → *watch yourself take it*" loop;
- **traders that actually trade**: when a local trader mirroring a persistent macro agent finishes
  docking at a market, it now **sells its cargo for real** — the same deterministic sale the macro
  economy runs when a trader arrives, moving actual credits, cargo, market prices, and faction
  treasury in the persistent world (a `TRADER SOLD <SYM>` toast marks it). It is sell-only (no buying
  or route-planning, which would touch the global RNG), and the macro simulation is frozen while you
  fly locally, so there is no double-counting; the living economy now **writes** to the world instead
  of only reading from it. Purely additive: purely-local ships and non-traders are untouched;
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
multi-octave domain-warped turbulence (with a second-order warp), limb darkening,
and a soft corona. Colour follows an O→M spectral proxy (blue-white hot cores vs
deep red-orange cool limbs); a thin chromospheric rim rides the limb, the corona
breaks into slowly-drifting prominence plumes, and a low-frequency field darkens
occasional sunspot cells. Two of those active regions drift along the limb and
periodically erupt as bright, hot blue-white flares — a fully deterministic cycle
that fades to nothing when the star is at rest, so a quiet star stays isotropic.
Because the shading is a pure function of the ray and the
star's centre/radius, it is isotropic: the star looks like a real ball of plasma
from every direction, with no "fill the screen yellow" shortcut. The shader writes into a half-resolution
streaming `SDL_Texture` (bounding-box limited when the star is far away), so it
stays SDL2-only and keeps the deterministic headless screenshot harness working —
there is still no OpenGL path. The same ray-sphere method now lights the planets
and moons (`renderBodySphere`): the star at the system origin acts as the light, so
each body shows a real day/night terminator (phases and crescents) with a per-type
surface (gas-giant latitude bands, rocky mottling, icy poles, lunar maria) and an
atmospheric limb glow. Each body's shading is seeded deterministically from its
orbit, so the screenshots stay reproducible; only small or map-view bodies fall
back to flat discs, and the orthographic map is left bit-for-bit unchanged.
Gas-giant rings are drawn in the same `renderBodySphere` pass: each pixel's ray is
intersected with the annulus lying in the planet's equatorial plane (its normal is
the same spin axis that orients the latitude bands), giving Cassini gaps, radial
banding, a soft impact-parameter planet shadow on the rings, and an edge-on opacity
gain. The reciprocal shadow is cast too: each lit surface point traces a ray back to the
star and, if it crosses the opaque annulus between surface and star, is darkened by the
same density model — so the rings lay a soft shadow band across the planet's face, broken
by a bright sliver where the Cassini gap lets light through (§5.13.21).
The sphere and ring roots are compared per pixel, so the near ring arc blends
over the planet while the far arc is occluded behind it, and the star's sphere
occludes both — no separate billboard or depth pass. Nearby asteroids
(`renderRockLit`) reuse the same machinery: a Lambert terminator from the origin
star, an irregular silhouette carved inward from the nominal sphere (so it always
stays inside its bounding box), quartic-sharpened craters over low-frequency
mottling, and a deterministic tumble that rotates the surface noise about the rock's
spin axis. Each rock's base color and specularity come from `rockAppearance(element)`,
a pure function that classifies the boulder by its element's atomic number and
metallicity into one of four real asteroid classes — ice/volatile (bright bluish
white, shiny), carbonaceous (dark charcoal, matte), metallic (steel grey, shiny), or
silicate (tan-brown matte, the default that preserves the old belt look). Shiny
classes add a Blinn-Phong highlight (`pow(N·H, 24)·spec`, star at the origin, view
along the eye ray) on the lit side; matte classes get none. The per-rock brightness
jitter is drawn from a deterministic index hash rather than the local RNG stream, so
adding material types left the generation stream byte-identical (stations, radio, and
craft still spawn exactly as before). That classification lives in a shared `rockClass`
helper that is the single source of truth for both this palette and the mining
extraction rate (metal ×1.60, ice ×1.25, carbon ×0.65, silicate ×1.00), so a rock's
look and how fast it mines can never fall out of sync. To bound the number of texture locks in a dense
belt, only rocks larger than a few pixels get the shader; smaller ones stay cheap
phase-lit discs (which read the same typed base color), and the whole path is gated on
the perspective view so the orthographic map is untouched. That same class color is
reused one more time when you mine: while a rock is being extracted, a laser is drawn
from the cockpit muzzle to the boulder with its halo tinted by `rock.r/g/b` over a hot
near-white core, so the beam's color is the rock's class is its yield — one visual
language across look, worth, and the act of taking it. The beam is gated on the exact
condition the simulation uses to add ore (locked target within `radius + MINE_RANGE`,
not occluded by the star) and is pure draw — it touches no simulation state and renders
nothing when you are not mining, so it is a zero-regression overlay. Its hum-pulse is a
`sin` of the effect clock, deterministic with no RNG.

The nebula backdrop (`renderNebula`) uses the same idea one more time, on the sky
itself: for each pixel the world ray direction is fed to multi-octave product noise
(low-frequency products make puffy localized clumps rather than the stripes a sum of
sines would give, with mid and high octaves adding filaments), pre-distorted by a
domain warp so nothing aligns to an axis. A large-scale "bank" gradient — density
rising across the sky along an axis derived deterministically from the nebula's tint —
makes it denser on one side, and filament cores are nudged toward white. Because color
and density are a pure function of the ray direction, the field is fixed to the
celestial sphere and turns in lockstep with the skybox stars; it is drawn first and
kept translucent so those stars shine through it, with a slow `fxClock` drift. As with
every other lit body this is perspective-only — the orthographic map keeps its former
flat tint plus three hashed blobs, byte-for-byte.

## Source Map

| File | Responsibility |
| --- | --- |
| `main.cpp` | SDL2 window, event loop, camera, map projection, route drawing, influence overlay, frame pacing. |
| `game.h`, `game.cpp` | World composition, update order, saves, routes, markets, agents, contracts, factions, signals, player actions. Also the free function `downgradeAgentToEscapePod(Agent&)`: the shared "macro agent death" helper (ship → Escape Pod, cargo cleared, credits kept), factored out of `robAgent` and reused by the local-flight write-back. And `localDockSellCargo(Game&, agentIndex, starIndex)`: a thin bridge letting local flight run the deterministic macro `sellCargo` when a mirror-trader docks (it lives outside the anonymous namespace for external linkage, but still reaches the internal `sellCargo` by unqualified lookup in the same translation unit). |
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
| `local.h` | Local-mode data structures (`LocalScene`, `LocalInput`, and `LocalRock` with its material `spec`) and the `buildLocalCamera` helper. Declares `rockAppearance` (asteroid palette/specularity by element) and `rockClass` (the shared ice/carbon/metal/silicate classifier), plus the `RockClass` enum, inline `rockYieldMult`/`rockClassName`, and the `MINE_RATE_BASE`/`MINE_YIELD_*` tuning constants that make composition drive mining speed. |
| `localgen.cpp` | Procedural generation of a local star system (star, planets, moons, belt, station, radio sources, NPCs). Seeds non-combat NPCs with an opening market-bound errand and randomizes the arrival timer. Defines `rockClass(element)` — a pure classification of each asteroid into ice/carbon/metal/silicate — and `rockAppearance(element)`, which routes through it for the base RGB + specularity, baked into the belt with a deterministic index-hash brightness jitter that does not perturb the RNG stream. The same `rockClass` is the single source of truth reused by mining, so appearance and yield can never disagree. Belt composition is no longer uniform: before the rock loop it builds per-class element pools and class weights from the host star's metallicity and `resourceFocus`, then picks each rock's class/element from a deterministic per-rock hash — the base `irand` element draws are preserved verbatim, so the RNG stream (and thus station/traffic/radio spawns) stays bit-for-bit identical (§5.13.20). |
| `localsim.cpp` | Local flight simulation: ship flight, thrust, projectiles, mining/docking, NPC behavior, and the living-traffic lifecycle (errand state machine, edge-despawn of departing ships, timed arrivals up to a population cap). Emits a cyan "warp" FX signature (`emitWarp`) on arrival/departure and a thruster puff when a ship leaves a berth. Marks convoy distress: a pre-pass clears the flags, then a pirate attacking a non-pirate flags the victim `underAttack` and itself `threatConvoy`; raid onset raises a `CONVOY RAID` toast, and killing a threatening pirate grants a bonus + research + `CONVOY SAVED` toast + positive faction rep bump (strictly additive — pirates are not weakened). Escort patrols (`CK_PATROL`) hear raids from a wide awareness radius and prioritise intercepting the *raider* (a pirate near a non-pirate, detected from raw positions so it is order-independent), flagging themselves `defending`; firing is still gated by weapon range, so their awareness grew, not their guns. When the player destroys a local ship that mirrors a macro agent (`agentIndex >= 0`), it writes back to the persistent world via the shared `downgradeAgentToEscapePod` helper: the macro agent's ship becomes an Escape Pod with its cargo cleared (credits kept). The faction reprisal is already applied in that same kill block, so the write-back only downgrades — it does not re-touch relations. Mining extraction rate now scales with rock composition (`MINE_RATE_BASE` × `rockYieldMult(rockClass(...))`): metal ×1.60, ice ×1.25, carbon ×0.65, silicate ×1.00 (the prior flat rate, so the default class is unchanged), and the mining-start toast names the class (`MINING FE METAL`). When a mirror-trader (`kind == CK_TRADER`, `agentIndex >= 0`) completes the dock transition carrying cargo, it calls `localDockSellCargo` to run the deterministic macro `sellCargo` at the current market — a real sale in the persistent world (sell-only, no RNG; the macro sim is frozen during local flight so there is no double-count) — debounced behind a `TRADER SOLD <SYM>` toast (`tradeCooldown`/`tradesExecuted`). Strictly additive: purely-local craft (`agentIndex < 0`) and non-traders are untouched. |
| `localdraw.cpp` | Local-mode rendering: bodies, orbits, HUD, the per-pixel ray-sphere software star shader (with a deterministic corona flare-cycle: two drifting active regions that periodically erupt — brighter, bulging, hot blue-white — with zero effect at rest), lit ray-sphere planet/moon spheres, perspective gas-giant rings (ray↔plane annulus with Cassini gaps and planet shadow), lit asteroid boulders (`renderRockLit` — carved silhouette, craters, tumble, and a Blinn-Phong glint on shiny ice/metal types while carbon/silicate stay matte), and a volumetric nebula backdrop (`renderNebula` — per-pixel gas field on the celestial sphere: multi-octave noise, domain warp, density gradient, dark dust lanes that absorb the glow, and emission-brightened cores at the densest filaments, translucent) — all perspective view only. Also draws the pulsing amber berth ring for docked ships, the radar-panel `IN / DOCK` traffic tally, the convoy-distress cues (pulsing red SOS ring on an `underAttack` victim, off-screen SOS edge marker to the nearest victim, and a distinct SOS target-panel state), and the escort cues (pulsing green ring on a `defending` patrol drawn outside the cyan target box, off-screen green edge marker to the nearest defender, and an `ESCORT` target-panel state). The live mining overlay now names the rock class being extracted (`MINING METAL  +…`). While a rock is being extracted (target locked, in mining range, not occluded by the star), it also draws a mining laser from the cockpit muzzle to the boulder — halo tinted by the rock's class color over a hot near-white core, with a deterministic hum-pulse, impact glow, and muzzle spark; draw-only and perspective-only, so it renders nothing when not mining. |
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
  (flying "into" the star is cosmetic; only HP can end a flight).
- Gas-giant rings are now projected in first person (a Saturn-like ellipse with the
  back arc occluded by the planet) and cast a soft shadow band back onto the planet's
  lit face — with the Cassini gap letting light through — completing the reciprocal of
  the planet's own shadow on the rings (§5.13.21). Still treated as an infinitely thin
  plane, though: there is no vertical ring thickness at the edge-on grazing angle, and
  no moon-resonance gap structure beyond the Cassini division.
- Asteroids are lit ray-sphere boulders only when they are close enough to fill more
  than a few pixels; smaller and distant belt rocks are still phase-lit discs. The
  boulders are spheres carved *inward* for an irregular silhouette, so there are no
  true concavities, overhangs, or contact-binary shapes yet. Rocks are typed by
  composition (ice/carbon/metal/silicate) via a shared classifier that now drives
  appearance (palette + specular glint), mining extraction rate (metal is richest,
  carbon poorest), *and* belt spawn composition — §5.13.20 weights each rock's class by
  the host star's metallicity (metal-rich systems yield metallic belts, metal-poor ones
  icy) and its economic `resourceFocus`, via a deterministic per-rock hash that leaves the
  RNG stream untouched, so the glint reads as a gameplay beacon and belts now vary
  system-to-system. Still open: the *market value* of what you mine comes only from the
  element mapping, not a class premium.
- The nebula backdrop is a single translucent layer painted on the celestial sphere. It
  now has dark dust lanes that absorb the glow and emission-brightened cores at the densest
  filaments (§5.13.19). Still open: the emission is procedural, not yet keyed to the bright
  skybox stars embedded in the nebula, and there is only one layer — no parallax depth.
- Local NPC traffic is now goal-driven (errands to markets, timed arrivals and departures)
  and legible from the cockpit (warp-in/out FX, berth ring, `IN / DOCK` tally). Pirate raids on
  traders are now a readable, actionable event: victims raise a distress beacon (SOS ring +
  off-screen edge marker + `CONVOY RAID` toast), and killing the attacking pirate pays a
  `CONVOY SAVED` bonus plus a faction-standing bump. Patrol ships now answer that distress —
  they hear a raid from far off and prioritise intercepting the raider (green escort ring +
  edge marker + `ESCORT` panel), though they still fly in and fight at weapon range rather than
  sniping. Killing a local ship that mirrors a macro agent now writes back to the macro
  simulation: that agent's ship is downgraded to an Escape Pod (cargo jettisoned, credits kept) —
  a permanent consequence carried into the persistent world (§5.13.14). Mirror-traders now also
  **sell** on docking — a real deterministic sale that moves cargo, credits, market prices, and
  faction treasury in the persistent world (§5.13.18) — so the economy finally writes back, not
  just reads. What is still missing is the *buy* side and full round-trips (buy low here → sell
  high there): those touch the global RNG and macro location, so they need an RNG-safe local path
  before they can run from the cockpit.
- Non-player faction memory overlays are not exposed through a debug selector.
- The active build is POSIX/SDL2 via `sdl2-config`; the old platform makefiles
  are not synchronized with the current source list.
