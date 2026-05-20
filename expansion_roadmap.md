# Expansion Roadmap For Subagents

This document coordinates implementation work across the detailed plans.

## Primary Documents

- `elements.md`: universal element traits from atomic number.
- `ship.md`: mass, fuel, modules, route fuel cost.
- `economy.md`: local markets, base price, stale snapshots, tariffs.
- `factions.md`: faction budgets, relations, fleets, influence overlay.
- `ai.md`: traders, scouts, couriers, military, adventurers.
- `events.md`: relativistic event/signal propagation.
- `quests.md`: contracts for player and AI agents.
- `combat.md`: piracy, local combat, capture.
- `colonies.md`: founding, growth, construction, control.

## Necessary Additional Work

The user listed the main systems. The remaining systems that are also mandatory:

1. Combat and piracy.
   Without this, "attack and rob another ship" and faction war cannot exist.

2. Colonies and construction.
   Without this, late-game ownership is only a flag, not gameplay.

3. Player knowledge and market snapshots.
   Without this, fog of war leaks truth and trade becomes omniscient.

4. UI windows for ship, contracts, faction, and colony management.
   The current window system is enough as a base, but each system needs a
   specific mouse-oriented panel.

5. Save/load or deterministic restart.
   Once systems become deeper, testing and iteration need persistence.

## Suggested Subagent Split

### Agent A: Elements And Economy

Files:

- `resource.*`
- `market.*`
- `economy.md`
- `elements.md`

Deliverable:

- universal trait formulas;
- no symbol-based runtime category helpers;
- base price from traits;
- market demand from traits.

### Agent B: Ship Physics

Files:

- `ship.*`
- `game.cpp`
- `ui.*`
- `ship.md`

Deliverable:

- cargo mass;
- total ship mass;
- fuel element/reserve;
- acceleration and route fuel cost;
- UI display.

### Agent C: Events And Knowledge

Files:

- `game.*`
- new `events.*` if needed;
- `events.md`

Deliverable:

- local event struct;
- signal queue;
- ownership and market snapshots;
- player knowledge updates via local observation/signals.

### Agent D: Factions And Influence

Files:

- `faction.*`
- `game.*`
- `main.cpp`
- `factions.md`

Deliverable:

- relation matrix;
- player faction as normal faction;
- optional influence overlay;
- strategic budget fields.

### Agent E: AI And Contracts

Files:

- `agent.*`
- `game.*`
- `ai.md`
- `quests.md`

Deliverable:

- role scorer;
- trader/courier/scout behavior;
- local contracts;
- AI can accept contracts.

### Agent F: Combat And Colonies

Files:

- `ship.*`
- `agent.*`
- `colony.*`
- `game.*`
- `combat.md`
- `colonies.md`

Deliverable:

- pirate attack;
- loot transfer;
- bounty event;
- capture progress;
- colony construction fields.

## Integration Order

1. Elements traits.
2. Ship mass/fuel.
3. Economy price/demand from traits.
4. Events/signals/knowledge snapshots.
5. Faction relations and influence overlay.
6. Contracts.
7. AI role scorers using known data.
8. Combat/piracy.
9. Colonies/construction/capture.
10. Save/load and performance pass.

## Performance Rules

- No per-frame full-cluster scans for AI decisions.
- Use slow ticks for faction/economy/quest generation.
- Use candidate lists for trade and military target selection.
- Use coarse cached grid for influence overlay.
- Keep normal gameplay truth separated from debug truth.

