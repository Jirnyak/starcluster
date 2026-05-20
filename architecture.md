# Starcluster Architecture

Starcluster is a 3D real-time full-simulation space economy game in C++ with SDL2 rendering and an OpenGL path as the long-term renderer. Stars, ships, routes, travel time, visibility radius, and market distance use `x/y/z` coordinates in light years. The current SDL2 renderer projects that 3D state to a mouse-oriented 2D screen map; rendering projection is UI, not simulation truth.

The design target is order of 10k stars, order of 10k agents/fleets, and order of 100 factions. The player is one agent inside the same simulation rules as everyone else.

Core taste: physmath, data-driven, data-oriented, procedural, modular, elegant, minimal code with maximum functionality. No hardcoding of content-specific behavior into shared systems.

## Active Project Map

- `resource.*`: element definitions, abundance weights, demand weights, base prices.
- `cluster.*`: generated spherical 3D cluster stars, `x/y/z` coordinates, local resources, population, industry, habitability, defense, ownership.
- `faction.*`: faction identity, color, strength, treasury, aggression, controlled star ids.
- `colony.*`: colony state attached to stars and factions.
- `market.*`: local supply, demand, production, consumption, prices.
- `ship.*`: physical ship state, 3D position/velocity, max speed, acceleration, cargo.
- `agent.*`: simulation actors: player, traders, military fleets, colonizers.
- `game.*`: simulation composition and systems update order.
- `main.cpp`: SDL2 window, mouse input, 3D orthographic map projection, debug/visible map rendering.
- `ui.*`: SDL2 HUD primitives and panels.

Legacy `civ.*`, `galaxy.*`, and `graphic.*` are not the active simulation path.

## Expansion Planning Documents

Detailed implementation plans for future subsystem work:

- `elements.md`: universal element traits from atomic number.
- `ship.md`: ship mass, fuel, modules, and route fuel cost.
- `economy.md`: local markets, prices, stale snapshots, tariffs.
- `factions.md`: faction budgets, relations, fleets, influence overlay.
- `ai.md`: traders, scouts, couriers, military, adventurers.
- `events.md`: relativistic events and signal propagation.
- `quests.md`: player and AI contracts.
- `combat.md`: piracy, combat, capture.
- `colonies.md`: founding, construction, control.
- `expansion_roadmap.md`: suggested subagent split and integration order.

## Architecture Levels

### 1. Cluster Stars

Stars are the base spatial layer.

Each star is generated procedurally and owns:

- 3D position in light years;
- generated resources by element;
- economy role;
- local population and industry;
- habitability and defense;
- current owner faction id or free state.

Stars do not run behavior. They are data. Systems read and update their fields.

Current prototype cluster generation is a truncated Plummer-like spherical distribution:

```text
R_cluster = 100 ly
core_radius = 18 ly

r = core_radius / sqrt(u^(-2/3) - 1)
if r > R_cluster:
    r = R_cluster * random()^(1/3)

direction = random unit sphere direction
position = direction * r
```

This gives a dense core and a sparse outer halo while keeping a hard gameplay radius.

The cluster must scale to order of 10k stars. Generation can do full-cluster work; per-frame systems must not repeatedly scan every star unless the system is explicitly slow-ticked or cached.

### 2. Factions

Factions are strategic actors controlling stars as colonies.

Each faction owns:

- controlled star ids;
- colonies and local infrastructure through star/colony ids;
- treasury, strength, aggression, policy fields;
- spawned agents/fleets;
- future dynamic relation row against every other faction.

Faction relations should be a dense signed table:

```text
relations[factionA][factionB] = -128..128
```

Negative means enemies, zero means neutral, positive means allies. Relations are dynamic and affected by war, trade, raids, treaties, border pressure, and market dependence.

Factions farm resources through colonies and local markets. They trade, tax, colonize, build fleets, defend controlled systems, fight wars, and can make politics emerge from the relation matrix.

Faction logic must be data-driven. A faction system should consume faction parameters and relations, not special-case named factions.

### 3. Agents

Agents are moving actors in physical space.

Agent classes include:

- player ship;
- independent traders;
- faction traders;
- war fleets;
- colonizers;
- future scouts, diplomats, couriers, mercenaries.

Agents own ship state and orders. Fleets are agents with larger military/cargo capacity, not a separate simulation category unless scale forces it.

Agents move between stars at physical speeds. Travel takes years. Acceleration and deceleration matter enough for gameplay and route choice, but the model should stay minimal and stable.

Current movement math is 3D:

```text
dist3d = sqrt(dx*dx + dy*dy + dz*dz)
speed3d = sqrt(vx*vx + vy*vy + vz*vz)
stopping_distance = speed3d^2 / (2 * acceleration)
velocity += direction3d * acceleration * dt
if speed3d > max_speed:
    velocity *= max_speed / speed3d
position += velocity * dt
```

Agents can:

- buy and sell local market goods;
- carry cargo;
- colonize or reinforce stars;
- attack, defend, blockade, escort, or flee;
- act under faction orders or player commands.

The player is not special in simulation terms. The player is special only in input, camera, HUD, and visibility.

## Economy

The game is an economic trade simulation first.

There is no global market. Every star has a local market:

- local supply;
- local demand;
- production rates;
- consumption rates;
- local price pressure;
- tariffs by owner faction.

Prices emerge from local supply/demand and role needs. Trade emerges from price spreads, distance, travel time, tariffs, ownership, risk, and agent cargo capacity.

Resources are data definitions. Elements, weights, demand categories, and role effects belong in data-like modules, not in renderer or input code.

## Visibility And Signals

There is no omniscient player view in normal play.

Signals travel at light speed. Player information about remote systems is stale by distance. The player sees:

- star positions known from astronomy/navigation;
- delayed public information where communication exists;
- local owner, market, colonies, and present agents only after arrival or receiving a signal;
- other agents only if they are local, detected, or reported by delayed intelligence.

This creates permanent fog of war. The player never simply sees all agents everywhere in real time.

Debug mode may show all stars, all owners, all markets, and all agents. Debug visibility must stay clearly separated from normal gameplay visibility.

### Player Information Knowledge Overlay

The player has a knowledge overlay separate from simulation truth.

For each star, the overlay stores last known information such as:

- whether owner is known;
- last known owner faction id or known free state;
- simulation time when that ownership was observed;
- whether the system has been visited or otherwise scanned.

The overlay is updated when:

- the game starts, for ownership within an initial navigation radius of about 10 light years;
- the player is physically present in a system;
- a future light-speed signal, scout report, treaty, or purchased data packet arrives.

When the player leaves a system, the map continues to show the last known owner for convenience. That information is stale. The real owner may already have changed in the simulation. UI must label stale ownership as last-known rather than live.

Normal rendering uses this overlay for ownership colors and local contacts. It must not read live remote ownership or live remote agents directly. Debug overlays may read the truth, but must be explicit.

## Systems

Runtime behavior is organized as systems over data:

- market production/consumption/price update;
- colony growth and extraction;
- faction policy and orders;
- relation matrix drift and diplomacy;
- agent AI and order execution;
- ship movement and arrival resolution;
- combat and control transfer;
- signal propagation and fog-of-war knowledge;
- rendering and HUD.

Systems should be small, deterministic where possible, and composable. Shared systems must not hardcode one content module or named faction.

Preferred update shape:

1. advance simulation time;
2. update slow economic/colony/faction systems on coarse intervals;
3. update movement and arrivals;
4. resolve local market/war/colonization events;
5. update visibility/signals for the player;
6. render current known/debug state.

## Data-Oriented Rules

The target scale requires data-oriented programming.

Use:

- numeric ids for stars, factions, agents, resources;
- contiguous vectors and typed arrays where useful;
- dense relation matrices for factions;
- sparse maps only for rare optional state;
- cached candidate lists and dirty flags for expensive searches;
- slow ticks for strategic systems;
- spatial bins or local indexes for nearby-agent queries.

Avoid:

- per-frame full-world scans;
- per-agent heap churn in hot loops;
- object graphs with pointer ownership tangles;
- string comparisons in hot paths;
- rendering-side gameplay decisions;
- hardcoded named factions, named stars, or one-off content branches.

## Rendering

The game simulation is 3D. SDL2 owns the current window/input/HUD path and renders a 2D orthographic projection of the 3D cluster. OpenGL is the intended renderer for high-density maps and effects when SDL2 primitives become limiting.

Current map projection:

```text
relative = star_or_ship_position - camera_center
rotate relative by camera yaw and pitch
screen_x = window_w / 2 + projected_x * camera_scale
screen_y = window_h / 2 - projected_y * camera_scale
depth = projected_depth
```

Mouse picking uses projected screen distance with a small depth penalty. Gameplay must never use projected 2D distance: travel, signal delay, route choice, risk, and market scoring use 3D positions.

Rendering must draw simulation objects, not fake strategic information. Decorative background stars are not part of normal rendering. If a dot is rendered as a star on the map, it should correspond to a generated cluster star or an explicit debug layer.

Allowed visual layers:

- known/generated stars;
- ownership markers;
- colonies/defense/industry indicators;
- local/detected agents;
- routes and fleet trails;
- fog-of-war overlays;
- debug overlays.

HUD is a view over simulation state. It must not own gameplay rules.

## Optimization Direction

Current prototype constants:

- `STAR_COUNT = 10000`;
- `RESOURCE_TYPES = 118`;
- faction count is derived from star count and currently capped small;
- trader/military/colonist counts are derived from star count and currently capped for the SDL prototype.

The intended scale is:

- about 10k stars;
- about 10k agents/fleets;
- about 100 factions;
- 118 element resources initially.

Implications:

- 10k stars x 118 resources is acceptable as dense data if stored compactly and updated on slow ticks.
- 100 x 100 relation matrix is trivial and should be dense.
- 10k agents require route, market, and combat decisions to use candidate windows, caches, or scheduled updates.
- Renderer should cull by viewport and fog-of-war knowledge.
- Debug full-map rendering can be heavier than normal play but should still stay interactive.

Performance exists to buy better simulation: denser markets, stronger faction emergence, more fleets, clearer feedback.

## Modularity Contract

New features should enter at the narrowest correct level:

- new resource facts: `resource.*`;
- new star generation fields: `cluster.*`;
- new faction strategic fields: `faction.*`;
- new colony state: `colony.*`;
- new agent capabilities: `agent.*` / `ship.*`;
- new local economy behavior: `market.*`;
- new simulation interaction: `game.*` or a future dedicated system module;
- new visual presentation: `main.cpp` / `ui.*` / future render module.

If a feature touches several levels, add a generic hook once and keep content data separate.

## Save/Load Policy

Starcluster is in active simulation development. Save files exist for debugging,
long-run testing, and continuing the current playable prototype, not for
preserving legacy formats.

Rules:

- no backward compatibility guarantee for old saves;
- no migration burden for obsolete save shapes;
- legacy systems such as `civ.*`, `galaxy.*`, and `graphic.*` are not saved;
- only the active simulation path is serialized;
- when state layout changes, it is acceptable to bump the save version and
  invalidate older saves;
- do not add compatibility layers unless they directly help current debugging
  or current playable testing.

This keeps save/load small and aligned with the real architecture instead of
turning prototype history into permanent API surface.

## Design Rule

No fake global knowledge in normal gameplay. No fake stars. No hardcoded outcomes. The player should discover a dynamic procedural economy and political simulation through travel, trade, delayed signals, and local encounters.
