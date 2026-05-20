# Relativistic Events And Signals Plan

This document defines the event system. Starcluster should not use a generic
instant global event bus for gameplay. Events are observations and messages that
exist in simulation time and travel through the cluster at light speed unless
they are strictly local.

## Current Implementation

First signal layer:

- `SignalType::OwnerReport`;
- `SignalType::MarketReport`;
- `SignalPacket` queue in `Game`;
- `arrivalTime = observedTime + distance3d(originStar, destinationStar)`;
- destination is the nearest current faction relay/control star, with player
  fallback to the player's current star;
- signals apply to faction owner memory only when `arrivalTime <= game.time`;
- market reports carry a captured local price snapshot and apply to faction
  market memory only when they arrive;
- older reports cannot overwrite newer faction memory;
- NPC ship arrival/scouting sends delayed owner reports instead of directly
  mutating faction memory, and delayed market reports instead of sharing live
  prices;
- local player observation still updates player faction memory immediately;
- capture/settlement of a system by a faction applies immediately when that
  system becomes a local relay.

Still missing:

- typed payload stores beyond owner reports;
- public/local event history;
- supply/demand pressure snapshots beyond prices;
- contract posting/completion signals;
- bank settlement signals;
- multi-hop relay propagation and dedupe event ids.

## Goal

One universal mechanism handles:

- scout reports;
- ownership changes;
- market price feeds;
- combat reports;
- courier messages;
- bank settlements;
- faction orders;
- quest offers;
- distress calls;
- piracy reports;
- player knowledge updates.

## Core Concept

There are two layers:

1. Local event: something happened at a position/time.
2. Signal packet: information about that event travels to recipients.

Local events are immediate only to local observers. Remote knowledge updates
when a signal arrives.

## Data Model

```cpp
enum class EventType {
    StarObserved,
    MarketObserved,
    OwnershipChanged,
    ShipDeparted,
    ShipArrived,
    ShipAttacked,
    ShipDestroyed,
    CargoStolen,
    ColonyFounded,
    ColonyCaptured,
    ContractPosted,
    ContractAccepted,
    ContractCompleted,
    BankSettlement,
    FactionOrder,
};

struct LocalEvent {
    EventType type;
    double observedTime;
    double x, y, z;
    int star = -1;
    int sourceAgent = -1;
    int sourceFaction = -1;
    int targetAgent = -1;
    int targetFaction = -1;
    int payloadIndex = -1;
};

struct SignalPacket {
    EventType type;
    double observedTime;
    double sendTime;
    double arrivalTime;
    int originStar = -1;
    int destinationStar = -1;
    int sourceFaction = -1;
    int recipientFaction = -1;
    int payloadIndex = -1;
};
```

Payloads are stored in typed arrays/vectors by type.

## Signal Travel

```text
arrivalTime = sendTime + dist3d(origin, destination)
```

If origin or destination is a moving ship, use current position at send time for
the first implementation.

## Local Observation

When an event occurs:

1. Apply local simulation truth immediately.
2. Update player/agent knowledge only if observer is local.
3. Create signal packets for eligible recipients.
4. Insert packets into an arrival queue sorted by `arrivalTime`.

## Arrival Queue

Use a vector plus periodic sort at first:

```text
pendingSignals[]
```

For scale:

- keep sorted by arrival time;
- process while `arrivalTime <= game.time`;
- use bucket queues if signal count grows high.

No per-frame scan of all historical events.

## Knowledge Updates

Signal payload applies to recipient knowledge:

- player overlay;
- faction intelligence;
- agent memory;
- market snapshots;
- quest boards;
- bank ledgers.

Signals should not mutate impossible truth. Example: receiving an old ownership
report should update "known owner at observed time", not overwrite current
simulation ownership.

## Event Propagation

Some signals propagate:

- public market feeds from inhabited systems;
- faction relays between controlled systems;
- distress calls to nearby systems;
- scout reports to nearest relay then faction network.

Propagation must remain bounded:

```text
maxHops
recipient filters
dedupe event id
```

## UI

Player HUD should distinguish:

- local/live;
- received signal;
- stale snapshot;
- debug truth.

Event log line:

```text
Y 31.4 SIGNAL ARRIVED: SCOUT REPORT STAR_902 OWNER LAST SEEN HELION LEAGUE AGE 14.2Y
```

## Implementation Steps

1. Add event and signal structs.
2. Add `Game::emitLocalEvent`.
3. Add `Game::sendSignal`.
4. Add pending signal processing in `Game::update`.
5. Route player knowledge updates through signal/local observation.
6. Add market and ownership payloads.
7. Use signals for scout reports.
8. Use signals for quest posting and completion reports.
9. Use signals for bank settlement and faction orders.
