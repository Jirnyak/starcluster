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
  deterministic per-rock hash that never perturbs the RNG stream. And how *much* ore each
  rock holds now scales the same way — by class (metal densest) and by the system's mining
  richness (which formerly drove only macro mining) — so an ore-rich metallic system rewards
  heavier hauls, a redistribution that leaves both the belt's average tonnage and the RNG
  stream untouched;
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
- **wanted pirates carry a bounty**: some pirates are flagged *wanted* at generation, and
  destroying one pays a credit bounty (250–900 CR) on top of the normal kill reward, headlined by
  a `BOUNTY CLAIMED` toast. The flag comes from a deterministic index hash (no RNG-stream
  perturbation) and never touches a pirate's damage, AI, or targeting — it only adds reward — so
  it extends the "defend a convoy" payoff into a standalone reason to *hunt* pirates, unnerfed;
- **the bounty is legible from the cockpit**: a *wanted* pirate now wears a pulsing gold ring, its
  target panel gains a gold border, a `WANTED` chip, and a `BOUNTY N CR` readout, and an off-screen
  gold edge marker points to the nearest wanted pirate — the same treatment the SOS and escort cues
  get, at a distinct pulse rate so the near-amber gold reads apart from red distress and green escort;
  draw-only, so the flag is read and never written and the simulation is untouched (§5.13.25);
- **witnessed piracy writes back to the macro world**: an NPC-vs-NPC kill is no longer inert. When
  a pirate destroys a ship that mirrors a macro agent, that agent's ship is really lost in the
  persistent world (downgraded to an Escape Pod, cargo dropped, credits kept) — the same write-back
  the player's own kills already triggered (§5.13.14), now extended to the one place a ship could
  die without you. And a pirate that kills a non-pirate becomes *wanted* in real time (bounty
  300 CR, growing +200 per victim up to 1500), so the §5.13.24 bounty is now emergent as well as
  pre-generated: a raid you witness lights the gold cockpit cues on the raider and you can hunt it
  for the payout. Zero RNG, no new fields, and the soak's numeric determinism stays bit-for-bit
  identical — proven by a new `npcWriteBack` probe (§5.13.26);
- **the radar reads threat and distress in color**: the cockpit radar was the one panel blind to
  what a contact is *doing* — every blip was red-if-hostile-else-grey. Now each blip is colored from
  the ship's live state, in the same visual language as the cockpit cues: a *wanted* pirate is gold,
  a ship under attack pulses SOS-red, a patrol on the intercept is green, any other hostile is steady
  red, and neutral traffic is dim grey (priority wanted > SOS > escort > hostile, since a wanted
  pirate is also hostile). Because the radar reaches 1400 LU — farther than the single-nearest
  edge markers — the emergent §5.13.26 bounty now shows up system-wide at a glance, not only on the
  locked target. Draw-only: it reads existing fields, adds no field, touches no RNG, and the soak
  baseline is bit-for-bit unchanged (§5.13.27);
- **a star corona that erupts**: the plasma star's corona is no longer static — two active
  regions drift along the limb and periodically flare, brightening, bulging outward, and
  shifting toward a hot blue-white as they erupt. The cycle is fully deterministic (driven
  only by the effect clock) and vanishes to under 2% at rest, so a quiet star stays isotropic;
- **the law hunts the wanted**: patrols no longer only react to a raid in progress — a `CK_PATROL`
  now runs a priority-0 scan for the nearest *wanted* pirate within its 1400 LU distress radius and
  breaks off to intercept it, even when that pirate isn't attacking anyone right now. It's the same
  gold contact the radar already paints (§5.13.27), now actively hunted: the green *defending* patrol
  converges on the gold *wanted* blip. Firing stays gated by weapon range, so the patrol must close
  and fight at knife-range rather than snipe from 1400 LU, and only the patrol's *target choice*
  changes — pirate damage, AI, and aggression are untouched, so the combat invariant holds. It reads
  the live `wanted` flag with zero RNG and no new field. Because the headless soak never spawns a
  patrol, the branch never executes there and the 120k-frame numeric baseline is bit-for-bit
  unchanged — the AI is instead covered by a dedicated `patrolHuntsWanted` probe that proves
  priority-0 beats distance and the raider fallback still picks nearest (§5.13.28);
- **you can see the hunt**: the patrol pursuit from §5.13.28 is now drawn on-screen. The renderer
  re-derives the chased pirate directly from live fields — a `pursuitQuarry` lambda repeats the
  §5.13.28 priority-0 scan (the nearest live *wanted* pirate within the distress radius of a
  *defending* patrol) — and because priority-0 pre-empts every other target, that re-derivation
  matches the simulation's actual target *by construction*, so no private AI state is read and no
  new field is added. Three gold cues make the chase legible: a 3D marching-dash intercept vector
  with an arrowhead pointing patrol→quarry, a gold link between the pair's two radar blips (so you
  see *who* is hunting *whom*, not just that both contacts are hot), and an `IN PURSUIT <name>` line
  in the target panel of a locked defending patrol. It's pure draw-only — zero RNG, no new field, no
  sim change — and since the soak never calls the renderer, the 120k baseline stays bit-for-bit
  unchanged; a new `shot_law_pursuit` screenshot proves all three cues render (§5.13.29);
- **the pack defends its own**: the law-hunt now draws a response from the underworld. An idle
  pirate — one that has locked onto neither the player nor a trader — that hears the law closing in
  (a patrol within 1400 LU that is itself hunting a *wanted* pirate inside its own distress radius)
  breaks off and turns on that patrol: the pack rallies to defend the hunted outlaw. The "is this
  patrol hunting?" test is re-derived from live fields (kind, `wanted`, positions) exactly as the
  §5.13.29 renderer re-derives the chase — never from a same-frame `defending` flag — so the
  decision is independent of ship processing order and of the swap-pop that reindexes ships on death.
  It is strictly additive and the combat invariant is *strengthened*, not weakened: the idle gate
  never pulls a pirate off the player or a trader, it only converts a zero-damage idler into an
  attacker of the *law*, so there is more aggression aimed at the hunt, not less — bounty-hunting
  gets harder, never softer. Zero RNG, no new field (it reuses the distress radius). The headless
  soak spawns no patrol, so the block is a no-op by construction there and the 120k baseline stays
  bit-for-bit unchanged; a dedicated `packRalliesOutlaw` probe proves the rally fires when a patrol
  is hunting and stays gated off when it isn't (§5.13.30);
- **you can see the pack turn**: the pack-defense rally from §5.13.30 is now drawn on-screen. The
  renderer re-derives the rallying pirate directly from live fields — a `rallyTarget` lambda repeats
  the §5.13.30 decision: the idle gate (nothing targetable within 750 LU — neither the player nor any
  non-pirate) plus the rally scan (the nearest `CK_PATROL` within the 1400 LU distress radius that
  itself has a live *wanted* pirate inside its own distress radius) — the same re-derivation pattern
  as §5.13.29's `pursuitQuarry`, so no private AI state is read and no new field is added. Three
  hot-orange cues make the charge legible: a 3D marching-dash "ram" vector with an arrowhead pointing
  pirate→patrol (a faster pulse and march than the gold pursuit vector so it reads as aggression, not
  hunt, and drawn after the gold vector so orange sits on top where the two cross), an orange link
  between the pair's two radar blips, and an `UNDER PACK Xn` line in the target panel of a locked
  patrol being charged, counting how many idle pirates are rallying against it — it sits right below
  the §5.13.29 `IN PURSUIT` line, so a hunted patrol's panel now reads the whole law-cycle from its
  POV (`ESCORT` + `IN PURSUIT` + `UNDER PACK`). It's pure draw-only — zero RNG, no new field, no sim
  change — and since the soak never calls the renderer, the 120k baseline stays bit-for-bit unchanged;
  a new `shot_pack_rally` screenshot proves all three cues render (§5.13.31);
- **the law reinforces its own**: the pack's counter-attack now draws an answer back from the law.
  A free patrol that found no *wanted* target of its own (its §5.13.28 priority-0 scan came up empty)
  runs a new priority-0.5 tier — it looks for the nearest live pirate within its 1400 LU distress
  radius that is itself inside the distress radius of *another* hunting patrol (one that has a live
  *wanted* pirate in its own distress radius) and breaks off to attack it, flagging itself `defending`,
  so the whole patrol force converges on the brawl where a lone hunter is being dog-piled by the
  outlaw's pack. Whether that other patrol "is hunting" is re-derived from live fields (kind, `wanted`,
  positions) exactly as §5.13.29's `pursuitQuarry` and §5.13.30's rally re-derive their targets — never
  from the patrol's same-frame `defending`/`aiState` flag (valid only for lower indices) — so the
  decision is independent of craft processing order and of the swap-pop that reindexes ships on death.
  It is strictly additive and the combat invariant is *strengthened*, not weakened: the tier lives
  entirely in `CK_PATROL` target selection and reads or writes nothing on any pirate, and its target is
  always a pirate (never a patrol, so no friendly fire), so the law only acquires its enemy sooner and
  from farther — more aggression aimed at the hunt, not less, so bounty-hunting gets harder, never
  softer. Zero RNG, no new field (it reuses the distress radius and the existing `defending` marker).
  The tier can only fire with two or more patrols present — a besieged hunter plus a free responder —
  and the headless soak spawns none (and both existing patrol probes carry exactly one), so it is a
  no-op by construction there and the 120k baseline stays bit-for-bit unchanged; a dedicated
  `patrolBacksUpSwarmed` probe pins four craft (a hunter, a *wanted* outlaw that makes it a hunter, a
  pack pirate in the hunter's distress, and a free responder in the window) and proves the responder
  targets the pack when the outlaw is wanted and stays idle when it isn't (§5.13.32);
- **you can see the law answer**: the patrol backup from §5.13.32 is now drawn on-screen, closing the
  visual loop on the whole law-cycle. The renderer re-derives the responding patrol's target directly
  from live fields — a `backupTarget` lambda repeats the §5.13.32 decision: it takes a live `CK_PATROL`
  that is *not* itself a hunter (no live *wanted* pirate inside its own distress radius) and finds the
  nearest pirate that sits inside the distress radius of *another* hunting patrol — the same
  re-derivation pattern as §5.13.29's `pursuitQuarry` and §5.13.31's `rallyTarget`, so no private AI
  state is read and no new field is added (and, like the sim tier, it is deliberately *not* gated on the
  `defending` flag, which a patrol can also raise for raider defense). It returns the *pirate* index,
  matching the sim's actual target. Three cool blue-white cues make the relief legible: a 3D
  marching-dash reinforcement vector with an arrowhead pointing responder→pirate (a slower, steadier
  pulse and march than both the gold pursuit vector and the orange rally vector, so the law reads as
  discipline rather than fury, and drawn after both so blue sits on top where they cross), a blue link
  between the pair's two radar blips, and a `BACKUP INBOUND Xn` line in the besieged hunter's target
  panel, directly below the §5.13.31 `UNDER PACK` row, counting how many free patrols are inbound to its
  relief — so a hunted hunter's panel now reads the entire law-cycle from its POV (`ESCORT` +
  `IN PURSUIT` + `UNDER PACK` + `BACKUP INBOUND`). Because `backupTarget` returns the pirate and not the
  hunter, that count comes from a "returned pirate is within the hunter's distress radius" predicate, not
  a target-equality test. With the gold, orange and blue vectors all converging on one dog-piled hunter,
  a single frame now shows the whole emergent chain — hunt→rally→reinforcement (law→outlaw, pack→law,
  law→law). It's pure draw-only — zero RNG, no new field, no sim change — and since the soak never calls
  the renderer, the 120k baseline stays bit-for-bit unchanged; a new `shot_law_backup` screenshot proves
  all three cues render (§5.13.33);
- **notoriety makes the law faster**: the wanted bounty — until now a dormant number that only paid
  out on a kill — finally has a mechanical bite. A patrol actively running down a live *wanted* pirate
  (its priority-0 hunt from §5.13.28) now gets a top-speed boost that scales with that pirate's bounty:
  from ×1.0 at the 250 CR floor up to ×1.5 at the 1500 CR cap (`MANHUNT_SPEED_GAIN`). The louder a
  pirate's reputation, the more inescapable the law. A file-static `manhuntSpeedMult` re-derives the
  patrol's quarry exactly as the §5.13.28 scan does — the nearest live *wanted* pirate within the 1400 LU
  distress radius, the same pattern-D re-derivation the renderer lambdas use, so it matches the sim's
  target by construction — gated on `CK_PATROL` + `defending` so a patrol chasing the player or fleeing
  is excluded, and folds the multiplier into the movement loop's speed cap right beside the NPC boost.
  Strictly additive and the combat invariant is *strengthened*: the law is only ever faster on a
  big-bounty target, never slower (a floor bounty gives ×1.0 — exactly the old behavior, zero regression),
  and pirate speed/aggression/damage are untouched. Zero RNG, no new per-ship field (just one tuning
  constant). The `CK_PATROL` branch never runs in the headless soak, so the 120k baseline stays
  bit-for-bit unchanged; a new `manhuntScalesWithBounty` probe confirms a 1500-bounty patrol saturates at
  1.5× the speed of a 250-bounty patrol chasing the same quarry. No screenshot needed — this changes
  speed, not target choice, so the §5.13.29/31/33 render vectors stay correct for free and `localdraw.cpp`
  is untouched (§5.13.34);
- **you can see the heat**: the manhunt-speed rule from §5.13.34 is now legible from the cockpit. The
  gold pursuit vector from §5.13.29 (a `defending` patrol → its `wanted` quarry) runs hotter the bigger
  the quarry's bounty — the same "manhunt heat" that scales the patrol's speed in the sim. The renderer
  computes `heat = clamp((bounty − 250)/1250, 0, 1)` *byte-for-byte identical* to the sim's
  `manhuntSpeedMult`, reading the quarry's bounty straight off the index `pursuitQuarry` already resolved
  (the same pattern-D re-derivation), so the trail's color and the patrol's speed can never disagree.
  Three coordinated modulations, each `base + heat·delta`: the tone anneals from gold to white-hot
  (green 205→255, blue 60→255 at the cap), the base alpha brightens, and the marching dashes run up to
  ×1.5 faster (34→51) — mirroring the §5.13.34 speed gain. The target panel gains a `MANHUNT N CR` line
  below `IN PURSUIT` (the bounty read through the *law's* eyes, so you needn't retarget the pirate), both
  tinted by the same heat. At `heat = 0` (a floor-bounty pirate) all three collapse to the exact
  pre-change gold and march, so there is zero visual regression. Draw-only — no RNG, no sim field touched,
  the bounty read raw into a draw-time lerp (not a CHROMOCORE re-bake); an adversarial review returned
  SHIP on all eight checks. A new `shot_manhunt` screenshot (a 1500 CR quarry) pairs with
  `shot_law_pursuit` (650 CR) to prove the contrast, and the soak stays bit-for-bit unchanged since
  `renderLocalScene` never runs there (§5.13.35);
- **the law remembers who helped**: land the finishing blow on a *wanted* pirate that a patrol was
  actively hunting, and that patrol's faction now thanks you — `+6` reputation on top of the cash bounty
  from §5.13.24. It's the reputation-axis echo of the §5.13.11 CONVOY SAVED payoff, but for the *law*:
  the money was always there; now the co-operating kill also earns you standing. Because the killed pirate
  is already dead (its removal is deferred to end-of-frame), the code can't use the forward hunt-scan to
  learn which patrol was chasing it — so it *inverts* the scan (the same pattern-D re-derivation used
  elsewhere): among the `defending` patrols within the 1400 LU distress radius, it credits the one for
  which the dead pirate was the nearest *live* wanted quarry (no other live wanted pirate closer), which
  by construction is the patrol that was priority-0 hunting it. Strictly additive — the block runs only
  after the kill and touches nothing but faction rep, a news line and a `LAW GRATEFUL +REP` toast, so the
  combat invariant is untouched (no pirate damage, aggression or uptime is reduced). Zero RNG, no new
  field. The headless soak spawns no patrols, so the block never fires there and the 120k baseline stays
  bit-for-bit unchanged; a new `lawGratitude` probe supplies the coverage — a two-sided test that asserts
  `+6` when the patrol is in range and *no* change when it's 2000 LU away. An adversarial review returned
  SHIP on all eight checks (§5.13.36);
- **standing you can read from the cockpit**: lock a target that belongs to another faction and its
  panel now carries a `STANDING <WORD> ±N` line below the hull bar — your reputation with that faction,
  read live from `factionRelation(playerFaction, target.faction)` through the *same*
  `classifyFactionRelationTension` thresholds the macro layer uses (Enemy/Hostile red, Tense amber,
  Neutral grey, Friendly/Ally green). So the standing you bank from the §5.13.36 gratitude bonus, the
  §5.13.11 CONVOY SAVED payoff and faction reprisals is finally legible first-person — the number climbs
  green as it accrues and reddens under a feud. It's the render companion to §5.13.36: that `+6` is a
  transient event with no per-frame state, but its lasting *consequence* is the relation itself, and now
  the cockpit shows it. Draw-only — zero new fields, zero RNG, no CHROMOCORE rebake — and pirates are
  factionless, so the line never shows for them and the combat-uptime filter is trivially satisfied. The
  soak stays bit-for-bit unchanged since `renderLocalScene` never runs there; a new `shot_law_standing`
  screenshot reads `STANDING ALLY +96` by eye (§5.13.37);
- **kill the cop, raise your price**: when a wanted pirate guns down the very patrol that was hunting
  it, its bounty jumps `+500 CR` on top of the ordinary `+200` for a non-pirate kill — so murdering the
  law that hunts you makes the *next* manhunt faster (§5.13.34) and brighter (§5.13.35), not cooler. The
  killer is confirmed to be that patrol's priority-0 quarry by re-deriving it from live fields (the nearest
  live wanted pirate to the fallen patrol — a forward scan mirroring the manhunt-speed law), so a bystander
  that merely happened to land a patrol kill (e.g. a ram-pack pirate) gets only the ordinary `+200`.
  Sim-only and strictly additive — the bounty only ever climbs (same 1500 CR cap), no pirate
  hull/damage/aggression is read or reduced, zero RNG, no new per-ship field — and it *strengthens* the
  combat-uptime filter by making the ensuing law response hotter. The headless soak spawns no patrol, so
  the block is a no-op there and the baseline stays bit-for-bit unchanged; a two-sided `copKillRaisesBounty`
  probe (asserting `+700` for the true quarry and only `+200` when a closer decoy steals that role) supplies
  the coverage (§5.13.38);
- **standing you can read off the reticle**: the same faction standing §5.13.37 put in the target panel
  now tints the *reticle* itself — the target box in the 3D world and the lead-aim intercept pip both take
  the color of your standing tier with the locked target (Enemy/Hostile red, Tense amber, Friendly/Ally
  green), so you read where you stand without reading the line. A file-static `standingCue` helper returns
  a tier color only for a *non-neutral* standing with *another* faction; otherwise the caller keeps its
  default (cyan box, amber pip), and standing now outranks the pip's raw `hostile` fallback while
  preserving its alpha. Draw-only — zero new fields, zero RNG, no CHROMOCORE rebake — and pirates are
  factionless, so `standingCue` returns false for them and their reticle is byte-identical to before,
  leaving the combat-uptime filter trivially satisfied. A new `shot_standing_reticle` screenshot pins an
  enemy-standing (rep −100) *non-hostile* trader so the red reticle is provably caused by standing, not by
  `hostile`; it pairs with the ALLY-green `shot_law_standing`. An adversarial review returned SHIP on all
  ten checks (§5.13.39);
- **standing that flips the reward's sign**: the co-op kill that earns you standing (§5.13.36) now reads
  *oppositely* depending on where you already stand with the hunting patrol's faction. Help a friendly or
  neutral faction's manhunt and you still gain `+6` rep; but gun down a *hostile* faction's quarry in the
  middle of their operation (Enemy/Hostile tier — rep ≤ −48 by the same `classifyFactionRelationTension`
  thresholds the panel and reticle use) and you lose `6` instead, with a `FACTION REPRISAL -REP` toast in
  place of `LAW GRATEFUL`. The magnitude lives in one `LAW_GRATITUDE_REP` constant and the sign comes
  straight from the live `factionRelation`, so the whole emergent law-cycle now closes on the reputation you
  built. Strictly additive to combat — the pirate is already dead, so no damage/aggression/uptime is touched
  — with zero RNG and no new fields; `factionRelation` is bounds-guarded (any out-of-range faction reads
  Neutral → `+6`) and a same-faction patrol reads the pinned `128`/Ally diagonal through the `A==B` no-op, so
  it stays byte-identical to §5.13.36. The soak spawns no patrol, so the block is a no-op there and the
  baseline is bit-for-bit unchanged; a three-sided `factionReprisal` probe (Hostile −60 → −6, Friendly
  +40 → +6, Tense −30 → +6) pins the tier semantics so a naive "rep < 0 → −6" bug cannot pass. An
  adversarial review returned SHIP (§5.13.40);
- **a pursuit vector that shows the reckoning coming**: the gold chase trail from a hunting patrol to
  its wanted quarry (§5.13.29) now takes the color of where you *already* stand with that patrol's
  faction — so §5.13.40's reward sign is legible *before* you fire. Enemy/Hostile standing tints the
  world vector, its arrowhead and the radar link red (a reprisal `−6` is coming if you steal the kill);
  Friendly/Ally tints them green (gratitude `+6`); Tense amber; Neutral or a factionless patrol keeps
  the exact prior gold, so the neutral path is byte-identical. Only the hue changes — the §5.13.35
  manhunt heat, pulse and marching-dash alpha are preserved — and it reuses the same `standingCue`
  classifier the STANDING panel (§5.13.37) and reticle (§5.13.39) use, so all three readouts agree by
  construction. Draw-only: zero new fields, zero RNG, no sim change, and `factionRelation` is
  bounds-guarded so a garbage patrol faction falls back to gold. A `shot_reprisal_vector` (rep −100 →
  red) and `shot_gratitude_vector` (rep +96 → green) pin the two tinted paths against the gold
  `shot_law_pursuit` control on identical geometry. An adversarial review returned SHIP (§5.13.41);
- **the law you crossed hunts you back**: once your standing with a faction sinks to Enemy or Hostile
  (rep ≤ −48), its patrols stop waiting for you to shoot first — they lock *you* on sight, giving
  §5.13.40's reprisal real teeth: the reputation you burned now decides whether a patrol treats you as
  prey. It reads the same standing tiers as the STANDING panel (§5.13.37), the reticle tint (§5.13.39)
  and the pursuit vector (§5.13.41), so what you *see* and what a patrol *does* agree by construction.
  And it reuses the existing closest-target gate, so a hostile-standing patrol only peels off its
  wanted-pirate quarry for you if you are the *nearer* threat — exactly as a provoked patrol already
  did — so the §5.13.28 manhunt still runs when a pirate is closer. Strictly additive (design filter
  §0.2-G): pirates are factionless and untouched, and a patrol distracted onto you leaves the pirate
  *more* uptime, never less; zero RNG, no new field, `factionRelation` bounds-guarded. A two-phase
  `lawHuntsHostile` soak probe pins it — an Enemy-standing patrol hunts a non-firing player (target
  acquired via `aiState`) while a Neutral one stays idle on the identical single-craft scene — so the
  trigger is provably *standing*, not provocation. (The 120k soak main loop stays bit-for-bit: its
  fixed 10-star itinerary is disjoint from where `init` garrisons faction patrols, so zero co-locate
  there — empirically verified — and the probe injects its own.) An adversarial review returned SHIP
  (§5.13.42);
- **local kills that stick**: destroying a local NPC ship that mirrors a persistent macro agent
  now writes back to the macro world — that agent's ship is downgraded to an Escape Pod and its
  cargo jettisoned (credits are kept), a permanent consequence that outlasts the flight. Purely
  additive and deterministic; the faction reprisal for the kill is applied once, not double-counted;
- **a mining beam you can see**: while a rock is actually being extracted (target locked *and*
  within mining range, the same test the simulation uses to add ore), a laser reaches from the
  cockpit muzzle to the boulder, its halo **tinted by the rock's class color** (steel for metal,
  blue-white for ice, dark for carbon) over a hot near-white core, with a hum-pulse, an impact
  glow on the rock, and a muzzle spark. The cockpit now also reads the numbers out — the mine
  prompt names the target rock's class and ore *before* you drill, and the live overlay counts the
  remaining ore down as you extract (§5.13.23), so you choose your rock on worth, not guesswork.
  The pulse is deterministic (driven only by the effect
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
| `local.h` | Local-mode data structures (`LocalScene`, `LocalInput`, and `LocalRock` with its material `spec`) and the `buildLocalCamera` helper. Declares `rockAppearance` (asteroid palette/specularity by element) and `rockClass` (the shared ice/carbon/metal/silicate classifier), plus the `RockClass` enum, inline `rockYieldMult`/`rockClassName`, and the `MINE_RATE_BASE`/`MINE_YIELD_*` tuning constants that make composition drive mining speed. Also carries the `LocalCraft.wanted`/`wantedBounty` fields — a deterministically-assigned pirate bounty read at kill time (§5.13.24), surfaced in the cockpit HUD (§5.13.25) and on the radar as a gold blip (§5.13.27), raised at runtime when a pirate is caught raiding a non-pirate (§5.13.26), read by patrol AI so the law hunts the wanted pirate down (§5.13.28), and read again by idle-pirate AI so the pack rallies to defend a hunted outlaw (§5.13.30). Also defines `MANHUNT_SPEED_GAIN`, the tuning constant that lets a patrol running down a live *wanted* pirate accelerate in proportion to its bounty (§5.13.34). The same `wanted`/`faction` fields are read once more by the law-gratitude bonus, which grants the player faction rep for co-op killing a hunted outlaw beside its pursuing patrol (§5.13.36). Also defines `COP_KILL_BOUNTY`, the tuning constant added to a wanted pirate's bounty when it kills the patrol that was hunting it — strictly additive, same 1500 CR cap (§5.13.38). Also defines `LAW_GRATITUDE_REP`, the single constant for the magnitude of the co-op-kill rep swing, whose *sign* the sim derives from the player's standing tier with the hunting patrol's faction — `+6` for friendly/neutral, `−6` for hostile (§5.13.40). |
| `localgen.cpp` | Procedural generation of a local star system (star, planets, moons, belt, station, radio sources, NPCs). Seeds non-combat NPCs with an opening market-bound errand and randomizes the arrival timer. Defines `rockClass(element)` — a pure classification of each asteroid into ice/carbon/metal/silicate — and `rockAppearance(element)`, which routes through it for the base RGB + specularity, baked into the belt with a deterministic index-hash brightness jitter that does not perturb the RNG stream. The same `rockClass` is the single source of truth reused by mining, so appearance and yield can never disagree. Belt composition is no longer uniform: before the rock loop it builds per-class element pools and class weights from the host star's metallicity and `resourceFocus`, then picks each rock's class/element from a deterministic per-rock hash — the base `irand` element draws are preserved verbatim, so the RNG stream (and thus station/traffic/radio spawns) stays bit-for-bit identical (§5.13.20). Each rock's ore *quantity* then scales by `oreRichnessMult(rockClass, star.miningRichness)` (in `local.h`) — a class tonnage factor (metal 1.5 / silicate 1.0 / ice 0.9 / carbon 0.8, distinct from the extraction-rate factor) times the system's mining richness (previously read only by macro `mining.cpp`); it multiplies the already-drawn ore value, so it adds zero RNG draws and averages to ≈1.0 (redistribution, not inflation) (§5.13.22). In the pirate loop, ~38% of pirates are flagged `wanted` with a 250–900 CR `wantedBounty`, both chosen from a deterministic `(starIndex, pirateIndex)` hash — zero `lrng` draws, so the spawn stream stays bit-for-bit identical; the flag only adds a kill reward and never touches pirate damage or AI (§5.13.24). |
| `localsim.cpp` | Local flight simulation: ship flight, thrust, projectiles, mining/docking, NPC behavior, and the living-traffic lifecycle (errand state machine, edge-despawn of departing ships, timed arrivals up to a population cap). Emits a cyan "warp" FX signature (`emitWarp`) on arrival/departure and a thruster puff when a ship leaves a berth. Marks convoy distress: a pre-pass clears the flags, then a pirate attacking a non-pirate flags the victim `underAttack` and itself `threatConvoy`; raid onset raises a `CONVOY RAID` toast, and killing a threatening pirate grants a bonus + research + `CONVOY SAVED` toast + positive faction rep bump (strictly additive — pirates are not weakened). Escort patrols (`CK_PATROL`) hear raids from a wide awareness radius and prioritise intercepting the *raider* (a pirate near a non-pirate, detected from raw positions so it is order-independent), flagging themselves `defending`; firing is still gated by weapon range, so their awareness grew, not their guns. When the player destroys a local ship that mirrors a macro agent (`agentIndex >= 0`), it writes back to the persistent world via the shared `downgradeAgentToEscapePod` helper: the macro agent's ship becomes an Escape Pod with its cargo cleared (credits kept). The faction reprisal is already applied in that same kill block, so the write-back only downgrades — it does not re-touch relations. Mining extraction rate now scales with rock composition (`MINE_RATE_BASE` × `rockYieldMult(rockClass(...))`): metal ×1.60, ice ×1.25, carbon ×0.65, silicate ×1.00 (the prior flat rate, so the default class is unchanged), and the mining-start toast names the class (`MINING FE METAL`). When a mirror-trader (`kind == CK_TRADER`, `agentIndex >= 0`) completes the dock transition carrying cargo, it calls `localDockSellCargo` to run the deterministic macro `sellCargo` at the current market — a real sale in the persistent world (sell-only, no RNG; the macro sim is frozen during local flight so there is no double-count) — debounced behind a `TRADER SOLD <SYM>` toast (`tradeCooldown`/`tradesExecuted`). Strictly additive: purely-local craft (`agentIndex < 0`) and non-traders are untouched. Killing a `wanted` pirate pays its `wantedBounty` on top of the base reward (and on top of `CONVOY SAVED` if it was also raiding), with a `BOUNTY CLAIMED` toast — additive credits only, so the kill count and pirate combat are unchanged (§5.13.24). NPC-vs-NPC kills are no longer macro-inert either: at the single direct-DPS kill site, a slain ship that mirrors a macro agent triggers the same `downgradeAgentToEscapePod` write-back the player's kills do, and a pirate that kills a non-pirate becomes `wanted` in real time (bounty 300 CR, +200 per victim up to 1500, reusing the §5.13.24 fields) — zero RNG, no new fields, soak determinism bit-for-bit unchanged (§5.13.26). Patrols (`CK_PATROL`) now hunt proactively: before the raider scan, a priority-0 pass finds the nearest `wanted` pirate within the distress radius (no "beside a defenseless hull" condition) and targets it, flagging itself `defending`; if none is wanted, the prior raider→nearest-pirate fallback runs unchanged (wrapped in `else`). Firing stays gated by weapon range and only the patrol's target choice changes — pirate damage/AI/aggression untouched. Reads the live `wanted` flag with zero RNG and no new field; the `CK_PATROL` branch never runs in the headless soak, so the numeric baseline stays bit-for-bit unchanged (§5.13.28). Idle pirates now answer the hunt: at the tail of the `CK_PIRATE` branch, a pirate that has locked no target of its own (`atkCraft < 0 && !atkPlayer`) scans for a nearby hunting patrol — a `CK_PATROL` within the distress radius that has a live `wanted` pirate inside its own distress radius — and targets that patrol, so the pack rallies to defend the hunted outlaw. The "is it hunting" test is re-derived from live `kind`/`wanted`/positions (as in the §5.13.29 renderer), not read from a same-frame `defending` flag, so it is independent of ship order and death swap-pop. Strictly additive and the combat invariant is strengthened: the idle gate never pulls a pirate off the player or a trader — it only turns a zero-damage idler into an attacker of the law. Zero RNG, no new field (reuses the distress radius); the soak spawns no patrol so the block is a no-op there and the baseline is bit-for-bit unchanged (§5.13.30). Patrols also reinforce a swarmed comrade: when a free patrol's priority-0 `wanted` scan finds nothing, a new priority-0.5 tier (wrapping the raider scan + fallback in a further `else`) targets the nearest pirate within the distress radius that is itself within the distress radius of *another* patrol hunting a live `wanted` pirate, flagging itself `defending`, so the whole patrol force converges on a dog-piled hunter. Whether that other patrol "is hunting" is re-derived from live `kind`/`wanted`/positions (as in the §5.13.29 renderer and §5.13.30 rally), never from its same-frame `defending`/`aiState` flag, so it is order- and death-swap-pop-independent. Strictly additive: the tier lives entirely in `CK_PATROL` target selection, reads/writes nothing on any pirate, and always targets a pirate (never a patrol, so no friendly fire), so the law only acquires its enemy sooner and from farther. Zero RNG, no new field (reuses `PATROL_DISTRESS_R` and the `defending` marker); the tier needs two or more patrols to fire and the soak spawns none, so it is a no-op there and the baseline is bit-for-bit unchanged (§5.13.32). The law also runs down notorious targets faster: a file-static `manhuntSpeedMult` re-derives a patrol's priority-0 quarry (the nearest live `wanted` pirate within the distress radius, pattern-D so it matches the sim target by construction), gates on `CK_PATROL` + `defending` (excluding a player-override or fleeing patrol), and multiplies the movement loop's speed cap by `1 + MANHUNT_SPEED_GAIN · clamp((bounty − 250)/1250, 0, 1)` — so a patrol is faster the bigger its quarry's bounty. Only the patrol's speed changes (not its target choice, so the §5.13.29/31/33 render vectors stay correct for free); pirate speed/aggression are untouched, zero RNG, no new field, and the `CK_PATROL` branch never runs in the soak so the baseline is bit-for-bit unchanged (§5.13.34). The law also *thanks* you: when the player lands the finishing blow on a `wanted` pirate a patrol was hunting, that patrol's faction gains `+6` rep on top of the cash bounty (§5.13.24) — the reputation-axis echo of §5.13.11. Since the killed pirate is already dead (its removal is deferred to end-of-frame), the code inverts the priority-0 scan (pattern D): among the `defending` patrols within the distress radius it credits the one for which the dead pirate was the nearest *live* wanted quarry (no other live wanted pirate closer), which by construction is the patrol that was hunting it. Strictly additive — it runs only after the kill and touches only faction rep, a news line, a `LAW GRATEFUL +REP` toast and a shake; no pirate stat is read or written, zero RNG, no new field. The soak spawns no patrol, so the block is a no-op there and the baseline stays bit-for-bit unchanged; a two-sided `lawGratitude` probe (asserting `+6` in range and no change at 2000 LU) supplies the coverage (§5.13.36). The same NPC-vs-NPC death block also escalates a cop-killer's bounty: after the §5.13.26G bump, if a `wanted` pirate killed a patrol, it re-derives — via a `cWasWantedBefore` snapshot taken *before* the bump plus a forward priority-0 scan mirroring `manhuntSpeedMult` — whether that pirate was the patrol's priority-0 quarry (its nearest live `wanted` pirate within the distress radius), and if so adds `COP_KILL_BOUNTY` to `wantedBounty` (same 1500 cap); a non-quarry patrol-killer (e.g. a §5.13.30 ram-pack pirate) gets only the ordinary `+200`. Deliberately *not* gated on the patrol's `defending` flag (unlike §5.13.36), since `c` is the P0 quarry by geometry and a gate would falsely miss the "wore the patrol down until it fled, then finished it" case; the bounty only ever rises, so the combat invariant is strengthened for any patrol state. Zero RNG, no new field; the soak spawns no patrol so it is a no-op there and the baseline stays bit-for-bit unchanged, covered by the `copKillRaisesBounty` probe (§5.13.38). The law-gratitude bonus now also *reprises*: in that same block the `+6`/`−6` sign is chosen from the standing tier `classifyFactionRelationTension(factionRelation(playerFaction, patrolFaction))` — Enemy/Hostile (rep ≤ −48) yields `−LAW_GRATITUDE_REP` with a `FACTION REPRISAL -REP` toast, every other tier keeps the original `+LAW_GRATITUDE_REP`/`LAW GRATEFUL`, using the same inverted priority-0 scan and the panel/reticle classifier. Strictly additive (the pirate is already dead), zero RNG, no new field; `factionRelation` is bounds-guarded (an invalid faction reads Neutral → `+6`) and the pinned `128`/Ally diagonal plus the `A==B` no-op keep a same-faction patrol byte-identical to §5.13.36. No-op in the soak, covered by the three-sided `factionReprisal` probe (§5.13.40). Patrols now also hunt on *standing* alone: the pre-existing player-preference override in the `CK_PATROL` branch is broadened from `c.hostile` to `(c.hostile || factionHuntsPlayer)`, where `factionHuntsPlayer` is the Enemy/Hostile tier (rep ≤ −48) from the same `classifyFactionRelationTension` the panel/reticle/vector use — so a patrol whose faction you stand Enemy/Hostile with targets *you* with no fresh provocation, giving §5.13.40's reprisal real teeth. It reuses the existing `pd2 < atkBest` distance gate, so the patrol only switches to you if you are *closer* than its wanted quarry (exactly as a provoked patrol already does) — not a new priority tier, and the §5.13.28 hunt is intact when the pirate is nearer. Strictly additive and the combat invariant is strengthened: diverting a patrol onto the player only gives pirates more uptime. Zero RNG, no new field; `factionRelation` is bounds-guarded (a garbage or same faction reads Neutral → false). No-op in the soak, covered by the two-phase `lawHuntsHostile` probe (§5.13.42). |
| `localdraw.cpp` | Local-mode rendering: bodies, orbits, HUD, the per-pixel ray-sphere software star shader (with a deterministic corona flare-cycle: two drifting active regions that periodically erupt — brighter, bulging, hot blue-white — with zero effect at rest), lit ray-sphere planet/moon spheres, perspective gas-giant rings (ray↔plane annulus with Cassini gaps and planet shadow), lit asteroid boulders (`renderRockLit` — carved silhouette, craters, tumble, and a Blinn-Phong glint on shiny ice/metal types while carbon/silicate stay matte), and a volumetric nebula backdrop (`renderNebula` — per-pixel gas field on the celestial sphere: multi-octave noise, domain warp, density gradient, dark dust lanes that absorb the glow, and emission-brightened cores at the densest filaments, translucent) — all perspective view only. Also draws the pulsing amber berth ring for docked ships, the radar-panel `IN / DOCK` traffic tally, the convoy-distress cues (pulsing red SOS ring on an `underAttack` victim, off-screen SOS edge marker to the nearest victim, and a distinct SOS target-panel state), and the escort cues (pulsing green ring on a `defending` patrol drawn outside the cyan target box, off-screen green edge marker to the nearest defender, and an `ESCORT` target-panel state). The live mining overlay now names the rock class being extracted and counts the rock's remaining ore down toward depletion (`MINING METAL  +…  ORE …`), and the mine prompt reads the target's class and ore *before* you drill (`PRESS M TO MINE - METAL  ORE …`), so the §5.13.22 per-rock quantity is legible from the cockpit (§5.13.23). While a rock is being extracted (target locked, in mining range, not occluded by the star), it also draws a mining laser from the cockpit muzzle to the boulder — halo tinted by the rock's class color over a hot near-white core, with a deterministic hum-pulse, impact glow, and muzzle spark; draw-only and perspective-only, so it renders nothing when not mining. It also surfaces the wanted-pirate bounty (§5.13.25): a pulsing gold ring on a `wanted` pirate (drawn outside the cyan target box and the green escort ring, at a pulse rate distinct from the berth/SOS/escort rings), a gold off-screen edge marker to the nearest wanted pirate, and a gold target-panel state — gold border, a `WANTED` chip, and a `BOUNTY N CR` readout below the hull bar — mirroring the SOS and escort cues; draw-only, reading the flag without writing it. The radar panel is no longer state-blind: each contact blip is now colored from the ship's live state — a `wanted` pirate gold, an `underAttack` victim SOS-red, a `defending` patrol green, any other hostile steady red, else dim grey (priority wanted > SOS > escort > hostile, matching the cockpit cues' colors and pulse rates), so the emergent bounty and firefights read across the radar's full 1400 LU range; draw-only, reading existing fields with no RNG (§5.13.27). It also visualizes the patrol pursuit from §5.13.28: a `pursuitQuarry` lambda re-derives the chased pirate from live fields (repeating the priority-0 scan — the nearest live `wanted` pirate within the distress radius of a `defending` patrol), which matches the sim's actual target by construction since priority-0 pre-empts everything, so no private AI state or new field is needed. It draws three gold cues — a 3D marching-dash intercept vector with an arrowhead from patrol to quarry (skipped when either end is behind the camera or occluded by the star), a gold link between the two contacts' radar blips (drawn before the blips so they sit on top), and an `IN PURSUIT <label>` line in a locked defending patrol's target panel — all draw-only, reading only position/kind/wanted/defending/hull with no RNG and no sim change (§5.13.29). It likewise visualizes the pack-defense rally from §5.13.30: a `rallyTarget` lambda re-derives the rallying pirate from live fields (repeating the §5.13.30 idle gate — nothing targetable within 750 LU, neither the player nor any non-pirate — plus the rally scan for the nearest `CK_PATROL` within the 1400 LU distress radius that itself has a live `wanted` pirate inside its own distress radius), matching the sim's decision by construction, so no private AI state or new field is needed. It draws three hot-orange cues — a 3D marching-dash "ram" vector with an arrowhead from the rallying pirate to the patrol it charges (a faster pulse and march than the gold pursuit vector, and drawn after it so orange sits on top where the two cross), an orange link between the two contacts' radar blips, and an `UNDER PACK Xn` line — counting the idle pirates rallying against it — directly below the `IN PURSUIT` row in a locked charged patrol's target panel — all draw-only, reading only position/kind/wanted/aiState with no RNG and no sim change (§5.13.31). It finally visualizes the patrol backup from §5.13.32: a `backupTarget` lambda re-derives the responding patrol's target from live fields (a free `CK_PATROL` — one with no live *wanted* pirate inside its own distress radius, so not itself a hunter — targeting the nearest pirate that lies within the distress radius of *another* hunting patrol), matching the sim's decision by construction and returning the *pirate* index, so no private AI state or new field is needed and it is deliberately not gated on `defending`. It draws three cool blue-white cues — a 3D marching-dash reinforcement vector with an arrowhead from the responding patrol to that pirate (a slower, steadier pulse and march than both the gold pursuit and orange rally vectors, and drawn after both so blue sits on top where they cross), a blue link between the two contacts' radar blips, and a `BACKUP INBOUND Xn` line — counting the free patrols inbound to the relief — directly below the `UNDER PACK` row in the besieged hunter's target panel (counted by a "returned pirate within the hunter's distress radius" predicate, since `backupTarget` returns the pirate, not the hunter) — all draw-only, reading only position/kind/wanted/hull with no RNG and no sim change (§5.13.33). Finally, it visualizes the manhunt intensity from §5.13.34: the gold pursuit vector and its target-panel lines are modulated by `heat = clamp((quarry bounty − 250)/1250, 0, 1)` — byte-for-byte the sim's `manhuntSpeedMult`, reading the quarry index `pursuitQuarry` already resolved — so at the bounty cap the trail anneals gold→white-hot (green/blue channels to 255), its base alpha brightens, and the marching dashes run ×1.5 faster (34→51), mirroring the patrol's speed gain; a new `MANHUNT N CR` panel line under `IN PURSUIT` shows the bounty through the law's eyes (`ph += 28`). At `heat = 0` all three collapse to the exact pre-change gold and march (zero regression); draw-only, reading `wantedBounty` raw into a draw-time lerp with no RNG and no sim change (§5.13.35). Lastly, it surfaces faction standing in the target panel: when a locked target belongs to another faction (`t.faction >= 0 && playerFaction >= 0 && t.faction != playerFaction`), a `STANDING <WORD> ±N` line is drawn below the hull bar, its word and color derived from `game.factionRelation(playerFaction, t.faction)` through the shared `classifyFactionRelationTension` thresholds (the single source the macro layer uses) — Enemy/Hostile red, Tense amber, Neutral dim, Friendly/Ally green, `ph += 14` — so the reputation accumulated from §5.13.36/§5.13.11 is legible from the cockpit. Pirates are factionless, so the line never shows for them; draw-only, reading the relation raw at draw time with no RNG, no new field and no sim change (§5.13.37). That same standing also tints the *reticle*: a file-static `standingCue(const Game&, targetFaction, SDL_Color&)` returns a tier color only for a non-neutral standing with another faction (reusing the §5.13.37 classifier), recoloring both the 3D target box (was cyan) and the lead-aim intercept pip (was `hostile ? amber-red : amber`, alpha preserved) so standing reads straight off the crosshair; pirates are factionless so `standingCue` returns false and their reticle stays byte-identical, keeping the combat filter satisfied — draw-only, no new field, no RNG (§5.13.39). That same standing finally tints the *pursuit vector*: the gold §5.13.29 chase trail — the 3D world intercept vector, its arrowhead, and the radar link — is recolored by `standingCue` from the hunting patrol's faction, so §5.13.40's reward sign reads *before* the kill: Enemy/Hostile red, Friendly/Ally green, Tense amber, Neutral or a factionless patrol the exact prior gold (byte-identical). Only the hue changes — the §5.13.35 manhunt heat, pulse and marching-dash alpha are preserved (the tint writes RGB and keeps `gold.a`/`rc.a`) — and the world vector and radar link both read the one patrol faction (`scene.craft[i]`) so their hues cannot disagree. `factionRelation` is bounds-guarded so a garbage faction falls back to gold; draw-only, no new field, no RNG, no sim change (§5.13.41). |
| `shot_test.cpp` | Headless screenshot harness for local-mode scenarios (`make shots`). Its `shot_law_standing` case targets a friendly patrol whose faction the player has `+96` rep with, so the panel carries `STANDING ALLY +96` in bright green (§5.13.37). Its `shot_standing_reticle` case locks a *non-hostile* enemy-standing trader (rep −100) so the red target box and lead pip are provably caused by standing rather than by `hostile`, pairing with the ALLY-green `shot_law_standing` (§5.13.39). Its `shot_reprisal_vector` (patrol faction rep −100 → Enemy) and `shot_gratitude_vector` (rep +96 → Ally) frame an identical patrol-hunts-wanted-pirate broadside so the §5.13.29 pursuit vector renders red vs green purely from standing, against the gold `shot_law_pursuit` control (§5.13.41). |
| `soak_test.cpp` | Headless long-run soak harness for the local simulation (`make soak`): 120k deterministic frames under ASan/UBSan plus isolated probes for the player-death path, the player-kill macro write-back (§5.13.14), the mirror-trader sell write-back (§5.13.18), the NPC-vs-NPC macro write-back (§5.13.26, `npcWriteBack`), the patrol wanted-hunt AI (§5.13.28, `patrolHuntsWanted`), and the idle-pirate pack-rally AI (§5.13.30, `packRalliesOutlaw` — a pinned three-craft scene asserting an idle pirate targets a hunting patrol only when the patrol has a *wanted* pirate to hunt), and the patrol-backup AI (§5.13.32, `patrolBacksUpSwarmed` — a pinned four-craft scene asserting a free patrol targets a pack pirate only when another patrol is hunting a *wanted* outlaw, and stays idle otherwise), the manhunt-speed scaling (§5.13.34, `manhuntScalesWithBounty`), the law-gratitude rep bonus (§5.13.36, `lawGratitude`, a two-sided in-range/out-of-range scene), and the cop-kill bounty escalation (§5.13.38, `copKillRaisesBounty` — a two-sided pinned scene asserting `+700` when the killer is the fallen patrol's nearest `wanted` quarry and only `+200` when a closer decoy steals that role), and the faction-reprisal sign (§5.13.40, `factionReprisal` — a three-sided pinned scene seeding player↔faction rep to Hostile `−60`, Friendly `+40` and Tense `−30` and asserting the co-op-kill delta is `−6`, `+6` and `+6` respectively, so the sign follows the standing tier rather than the sign of the number), and the hostile-standing patrol hunt (§5.13.42, `lawHuntsHostile` — a two-phase single-craft scene pinning the player 300 LU from a non-provoked patrol of faction `F` and asserting the patrol targets the player at Enemy standing (rep −100) but stays idle at Neutral, so the trigger is provably *standing*, not provocation), each on a fresh `Game` so it never perturbs the main-loop baseline. |
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
  system-to-system. §5.13.22 then makes that composition pay off: a rock's *ore quantity*
  now scales by class (metal densest, carbon leanest — a separate axis from the extraction
  *rate* above) and by the host system's mining richness, a field that previously drove only
  macro mining and was ignored by the belt entirely, so a metallic rock in an ore-rich system
  carries more sellable mass. Both factors average to ≈1.0, so it is a redistribution toward
  metal, not economy-wide inflation, and the multiplier rides on the already-drawn ore value
  so the generation stream stays bit-for-bit identical. §5.13.23 then makes that quantity
  legible from the cockpit: the mine prompt reads the target rock's class and ore *before*
  you commit to drilling, and the live mining overlay counts the remaining ore down toward
  `ROCK DEPLETED`. Still open: the per-unit *market price* of ore still comes only from the
  element mapping, not a class premium at the till.
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
  before they can run from the cockpit. Separately, some pirates are now flagged *wanted* and pay
  a credit bounty on any kill (§5.13.24) — a standalone hunting incentive layered on the
  convoy-defense payoff — and the cockpit now surfaces that status with a gold ring, a `WANTED`
  target-panel state carrying a `BOUNTY N CR` readout, and an off-screen gold edge marker to the
  nearest wanted pirate (§5.13.25). And piracy you merely *witness* now has teeth: when one NPC
  destroys another that mirrors a macro agent, that agent is really downgraded in the persistent
  world — the write-back is no longer player-only — and a pirate caught raiding becomes *wanted* in
  real time, so bounties are emergent as well as pre-generated (§5.13.26). And the radar now reads
  that state in color — a wanted pirate shows gold, a ship under attack SOS-red, a responding patrol
  green — so a raid anywhere in the system is visible at a glance across the radar's full 1400 LU
  range, not just on the locked target (§5.13.27). And the law now *acts* on that contact: a patrol
  breaks off to hunt the nearest wanted pirate inside its distress radius, closing to weapon range to
  engage — so the gold blip is something the green patrols actively chase, not a passive marker. Only
  the patrol's target choice changes; pirate damage and aggression are untouched (§5.13.28). And
  that chase is now legible on-screen: the renderer re-derives the pursued pirate from live fields
  and draws a gold intercept vector and radar link from patrol to quarry, plus an `IN PURSUIT` line
  on the locked patrol — draw-only, so the soak baseline is untouched (§5.13.29).
- Non-player faction memory overlays are not exposed through a debug selector.
- The active build is POSIX/SDL2 via `sdl2-config`; the old platform makefiles
  are not synchronized with the current source list.
