# Per-player attention and sharding: a design study (26 Nov 2026)

Branch `contrib/ferrite-attn-split` (from `contrib/ferrite-p2-all` 92789912; gate DLL c7c7ece3 = rk18/rk19, no native
change). Question: can each client keep only its own player's surroundings at high attention, with other players'
regions at low attention and corrected from their owners, and what would it save? Treated as sharding: per-region
authority, in a peer variant and a server variant.

Tags: **[M]** measured on this PC (run named), **[F]** read in the pinned X4 9.00 binary or the shipped scripts (earlier
studies cited where they did it), **[S]** our own source, **[I]** inference or estimate.

## Verdict

**Not buildable as a soft-corrected mode on X4 9.00, in either sharding variant.** Measured today (two clients,
players in two sectors, each client dropping the other player's sector to low attention): the two worlds part
*outside* both players' sectors at once, and in the layer no region owner can correct:

- the shared runtime-ID stream at tick T+1, MD at T+20 (1,022 different cue runs), created objects at T+20;
- by the first census, 500 ticks later, 8,752 objects in 138 sectors that no player is in differ, nearly every
  moving ship of the universe; after 6 minutes 24,562 distinct outside objects have differed, 2,571 of them in
  topology (created, destroyed or docked on one client only);
- inside the remote region the low-attention copy drifts from its owner by ~100 m on average after 2 minutes and
  ~250 m after 6, single ships up to 84 km, and 250-2,000 of the owner's objects do not exist in the copy.

A correction of the remote region from its owner (the user's soft resync) bounds only the last point. The first two
are differences in global state (IDs, MD cue state, every agent's pathing schedule) with no owner to correct them
from; they do not shrink, and the only bound is a whole-game resync from one client (1-3 minutes with today's fast
join, about a minute of it a stop). So the condition for item 5's prototype is not met and none was built; section 7
says what a correction loop could and could not hold.

What it would save is small at today's player counts: a remote quiet bubble cost 1-2 ms of a 29 ms tick on 1 Nov, and
today the client that dropped Segaris saved about 1 ms of 35.7 ms and 0.2-0.5 GB, the other nothing measurable. The
owner's per-tick publication alone (H1's pass over its region's movers) cost 1.4-2.6 ms on the probe and ate that
saving. The cost that dominates, the universe at low attention plus the UI and the gate, is paid by every PC in every
variant.

**Recommendation:** keep full lockstep; get "cost flat with the player count" by making the shared per-bubble work
cheaper while staying identical (section 10: parallel physics scenes, presentation-only work out of the shared step,
a symmetric bubble size). If sharding is still wanted, **peer sharding first** (section 9): it keeps today's lockstep
as the fallback for every sector at every stage, keeps the player's own region local (no added latency), and each
stage is measurable with this branch's harness. Server sharding needs everything peer sharding needs at its server
boundaries, plus a presentation-only client (the authoritative fork's unsolved "selective suppression") and the
player's own ship a round trip away; it should not be started before peer sharding's stage 2 has passed, and the
measurements make stage 2 (outcomes as inputs, outside reads served from the owner's state) the expected stop.

## 1. What attention is, in one paragraph

X4 keeps an attention level per component (1..11; 7 = `visible` is the main switch). Above it an object is simulated in
full: a physics body in its zone's Jolt scene, the steering or spline flight model, physical shots, the AI scripts'
`visible` branches with 100-500 ms waits. Below it the same object runs another simulation: no body, the linear flight
model, scripted damage once a second, other script branches with waits of seconds. The change of level writes state
itself: a ship becoming high asks its zone for a free spot and is moved there, a station draws random numbers and builds
its region, interiors and props are generated at 9, and `ChangedAttentionEvent` reaches MD and AI
(reports/LOCKSTEP-ATTENTION-STUDY-20261101.txt sections 2-4 [F]). The stock scripts lean on it [F, counted for this
report in the resolved 9.00 scripts]: 31 of 178 base AI scripts have a separate `visible` branch and 26 test
`.attention` in expressions; 44 of 221 base MD files use attention, 18 of them listen to
`event_object_changed_attention`, among them global managers that are no player's: `cpu_ship_manager` (faction AI ships
leave their sleeping state when their sector reaches `insector`), `npc_instantiation` (NPC characters are created when
an object becomes `visible`), `signal_leaks`, and the war, trade and pirate subscription directors (they place objects
when a placement object reaches `nearby`). So two clients that hold one region at two levels are not computing a cheap
and an expensive picture of one world: they compute two worlds, and the global layer sees the difference at once.

The gate today makes attention a function of a shared view point set (every player's ship on every client,
`lockstep_view_points.h`), so every client simulates every player's bubble: n occupied sectors mean n physics scenes
stepped one after the other on the MoveWorker, n stand-in cameras, n traffic areas.

## 2. What was measured before (cited, not rerun)

| When | What | Result |
|---|---|---|
| 1 Nov, attm0-3 (build163, 2 clients on one PC, 30 Hz) [M, `LOCKSTEP-CENSUS-20261028.txt`] | X4's frame per tick with both players in one sector / slot 2 alone in Grand Exchange / that sector forced low on every client / no high attention at all | 29.1-29.8 / 27.9-30.8 / 26.3-29.2 / 23.9-24.3 ms: the first bubble ~4 ms, a second remote quiet bubble ~1-2 ms, ~24 ms the low-attention universe plus our sync work |
| 1 Nov, sampler (M1) [M, same] | where the frame goes | `ui_move` 11-12 ms (40%), universe update 6.5-7, positional move 2.4, AI director 1.9, zone move 1.0, render cameras 0.7-1.0, physics 0.2 |
| 1 Nov, hyba (hybrid slice H1, slot 2 owns Grand Exchange) [M, same] | the split itself, census mode, 1,803 ticks | outside the owned sector the clients already differ at the first capture (membership 3,548 vs 3,549); client 1's step 29.35 ms against 29.4 without the split: no saving within noise. Parked |
| 16-17 Nov, attention floor (whole universe at level 7) [M, memory note] | 440,000-449,000 components high | 245-285 ms per tick against 39 ms, 10.7-11.1 GB per client against 6.7: about 1.6 ms per sector |
| 17-19 Nov, phantom players (2 clients + 10 extra view points riding NPC ships in other sectors) [M, memory note] | 12 bubbles | 16 ticks/s, 8.3-9.1 GB per client (a normal client 6.5 GB); 9,026 ticks in sync |

The earlier studies' verdicts: asymmetric attention is "not feasible with lockstep on this engine"
(`LOCKSTEP-ATTENTION-STUDY-20261101.txt` section 1) and the hybrid model leaks across the border through channels the
engine has no boundary for (`LOCKSTEP-HYBRID-SECTORS-20261101.txt` section 3). This study measures again on today's
play stack (228 session defaults, 26 Nov) and asks the question the user asks now: not "can it stay identical" (no),
but "can the divergence be detected and corrected softly, and bounded".

## 3. Measured on today's code (26 Nov)

### 3.1 Setup

- Session: the release check's continued save (rk9-save `c3-t18000`, Terran cadet start, both players in
  `cluster_101_sector001`), 6 minutes = 10,841 ticks, 30 Hz, the full play stack (228 session defaults), gate c7c7ece3.
  Two X4 on this PC (profiles 45855/45856, below normal priority, display 3) beside one or two other sessions' X4.
- Placement: at plan tick 60 (gate tick 100) slot 2's ship is put 12 km from the Pioneers trade station OVL-841 in
  Segaris (`cluster_113_sector001`: 15 stations, ~130 ships in the save) by the test op `goto_obj` (an MD warp, the same
  on every client). Slot 1 stays in its sector (Terran cadet start: 3 stations, ~150 ships). Both players then turn and
  fire on a fixed schedule; slot 1 also throttles.
- Census every 900 ticks read in one step, the per-object records of every capture kept (`native_gate_world_record`),
  the digest's parts every tick (`native_gate_digest_parts`), memory sampled every 5 s.
- Variants, every client with the same overlay:

| Variant | Keys | What it is |
|---|---|---|
| `base` | - | today: both players' sectors at high attention on both clients |
| `hyb12` | `native_gate_hybrid={owners={1,2},tick=400}` | client k keeps only slot k's sector high; the other player's sector runs at low attention there (the user's proposal). The census declares the outside roots |
| `mask2` | `native_gate_view_points_mask=1` | slot 2 is no view point on any client: Segaris at low attention everywhere, symmetric, in sync. The clean price of one bubble |
| `hyb12f` | `hyb12` with a census every 100 ticks, 1 minute | the time course of the split at the start |
| `base_s`, `mask2_s` | `base` and `mask2` with the MoveWorker sampler (`native_gate_sample_profile=2`) | where a bubble's time goes |

(`base` and `hyb12` ran. `mask2`, `hyb12f` and the sampler pair are built into the driver and were queued, then
withdrawn when the PC went to the showcase video for the afternoon; `hyb1`, only client 2 dropping slot 1's sector,
was not queued. They are the next runs: `mask2` against `base` is the clean price of one bubble.)

Per object census counts of `base` (client 1's records): of ~550,000 objects, about 13,500 change in 30 s; about 12,500
of them outside both players' sectors (low-attention traffic), 100-1,900 in slot 1's sector and 250-540 in Segaris
(6,109 in the capture after the arrival, when Segaris was raised). Saved components: 3,443 in slot 1's sector, 6,114 in
Segaris, 429,158 elsewhere.

### 3.2 What diverges, how fast, how far (`hyb12` against `base`)

`base` (run 20261126-143215) stayed in sync: every digest part equal at all 10,841 ticks, the census equal at all 12
captures. `hyb12` (run 20261126-145108) split at tick 400. First differing tick of each declared part (DIGEST_PARTS
every tick):

| Part | First differing tick | Ticks differing (of 10,841) |
|---|---|---|
| `rid` (the shared runtime-ID head) | **401**, the first tick after the split | 10,441 |
| `navmesh` (navigation mesh receipt) | 523 | 10,319 |
| `world` (the census's outside roots: everything outside both players' sectors) | **900**, the first capture after the split | 9,942 |
| `input` (the input receipts) | 1,141 | 9,701 |
| `pilot` (the player ships' receipts) | 6,750 | 4,092 |
| `identity`, `sample` | never | 0 |

The global layer parted within the first status interval (rows every 60 ticks; the first after the split is 420):

| Tick | next runtime ID, client 1 minus 2 | live created objects | MD cue runs (cumulative) | find searches (cumulative) |
|---|---|---|---|---|
| 360 (before) | 0 | 1,279 / 1,279 | 20,020 / 20,020 | 11,098 / 11,098 |
| 420 | -781 | 1,324 / 1,305 | 22,055 / 23,077 | 12,616 / 12,704 |
| 900 | -697 | 1,641 / 1,623 | 36,754 / 37,862 | 26,487 / 26,649 |
| 1,800 | -28,824 | 2,195 / 2,208 | 63,598 / 67,229 | 54,332 / 54,192 |
| 5,400 | -23,150 | 4,406 / 4,453 | 164,698 / 161,596 | 145,088 / 143,821 |
| 10,800 | -20,677 | 6,435 / 7,084 | 282,384 / 280,071 | 281,486 / 282,787 |

Twenty ticks after the split the two clients had run 1,022 different MD cue executions and created different
objects. Who took the IDs in ticks 300-600 (the gate's table of shared allocations by stack and MD cue,
RUNTIME_IDS_CALLER): client 2, which dropped slot 1's sector, made 606 more shared allocations, and the cues among them
are stock global MD reacting to the drop: `npc_instantiation`'s `OnObjectInvisible` 126 against 40, `RemoveNPCs` and
`StopTrackingObject` 127 against 41, `OnRemoveInstantiatedNPC` 49 against 16 (NPC characters removed because their
objects became invisible on that client only), `signal_leaks`' `ObjectChangedAttention` 9 against 0, and the station NPCs'
state machines (`ChangeState_NPC_Ready`, `PerformPath_*`) on other ticks; the rest are engine stacks (zone and component
upkeep) without a cue. 514 status fields of 61 gate modules parted that never part in `base`; all but the per-process ones
first at tick 420: runtime IDs, created identities, script RNG (MD and AI scopes), find order (AI and MD searches),
event order, pathing determinism, navigation cadence and octree, mass traffic, region RNG, region camera, contact
orientation, temp zones, travel, the actor RNG of every ship, and the MD timed RNG.

The census by region (`attn_split_census.py`; S1 = slot 1's sector, S2 = Segaris, "outside" = every other sector):

| Capture | Objects differing | in S1 | in S2 | outside | unplaced (temp zones, created without a saved parent) |
|---|---|---|---|---|---|
| 900 | 18,257 | 3,270 | 6,106 | **8,752** | 129 |
| 1,800 | 14,538 | 3,004 | 421 | 10,740 | 373 |
| 5,400 | 14,204 | 228 | 295 | 12,838 | 843 |
| 10,800 | 14,993 | 109 | 395 | 13,783 | 706 |
| footprint (distinct objects ever listed) | 38,118 | 3,294 | 6,535 | 24,562 | 3,727 |

At the first capture, 500 ticks after the split, 8,752 objects outside both players' sectors differed, in 138
sectors: 5,338 small, 2,407 medium, 569 large and 207 XL ships, almost all in the motion domain only (8,297). That is
nearly every moving ship of the universe. By the end, 2,571 outside objects differed in every domain including
topology (created, destroyed or docked on one client only) and 3,757 had changed on one client only. The records
hold changed rows only, so these are lower bounds after 900.

How far the low-attention copy of a region drifts from its owner (H1's proxies: the owner publishes 4 poses a tick
round robin, every other client compares them with its own copy; the mean includes static members):

| Tick | Segaris on client 1 (owner: client 2) | Slot 1's sector on client 2 (owner: client 1) |
|---|---|---|
| 480 | mean 3 m, max 277 m | mean 4 m, max 429 m |
| 1,800 | 14 m, max 6.8 km | 49 m, max 36.6 km |
| 5,400 | 100 m, max 78.7 km | 165 m, max 83.5 km |
| 10,800 | 197 m, max 79.3 km; 253 published objects the copy does not have | 247 m, max 83.5 km; 2,002 missing |

Objects crossing the region borders in 6 minutes: 1,414 entered and 1,520 left slot 1's sector, 1,992 entered and
1,605 left Segaris (NPC traffic through gates and highways): every one a migration in sharding terms.

**What travels where.** The split did not stay in the two regions for one tick. The shared ID stream parted at once
(the owner builds bodies, shapes and effects its ghosts never build); MD parted at once (attention changes are MD
events, section 1); then every low-attention ship of the universe moved differently by the first capture, which points
at shared scheduling that every agent passes through rather than at anything a correction of the two regions could
reach. The likeliest channel is the pathing pass: with the session default `native_gate_path_budget=100` each batch
gives its first 100 agents from a round-robin cursor a full update and the rest a cheap one
(`lockstep_path_budget.h`), so a region whose agents differ changes batch membership and cursors, and other agents
everywhere get their full update on other ticks. Its receipts parted at 420 (PATH_DETERMINISM: 9,900 against 9,896
agents, another walk and order), as did the navigation cadence. The census difference of those ships is motion only, as
that mechanism predicts; it is not proven by an A/B run here. The economy and the
factions are not in the census; the created objects, cue runs and searches above are where they show.

### 3.3 What it saves: time and memory

Per tick after the split (ticks 700-10,841, capture ticks left out), the simulation thread's frame minus the probe's
own time plus the gate's pre-step work (FRAME_TIMING `sim_us - probe_us + pre_us`, median), the probe's own time, the
largest working set / private bytes, and the session's wall rate:

| Run | Client | Tick (ms) | Probe (ms) | Working set / private (GB) | Wall ticks/s |
|---|---|---|---|---|---|
| `base` | 1 | 35.7 | 0.7 | 7.24 / 11.59 | 23.2 |
| `base` | 2 | 39.1 | 0.7 | 7.54 / 11.96 | |
| `hyb12` | 1 (Segaris dropped) | 34.7 | 2.2 | 7.08 / 11.13 | 20.4 |
| `hyb12` | 2 (slot 1's sector dropped) | 42.7 | 3.6 | 7.31 / 11.96 | |

- Client 1 saved about 1 ms and 0.16-0.46 GB by not simulating Segaris (15 stations, ~130 ships) at high attention;
  client 2 did not save anything measurable (its run was slower, inside this PC's run-to-run noise of several ms with
  other sessions' X4 beside it). The owner's publication, H1's pass over its region's movers (2,800-5,900 of them),
  cost 1.4 and 2.6 ms a tick on the probe: as much as the saving. An owner has to read its region every tick to
  publish it, so some such cost belongs to any authority.
- Client 2's frame grew during the run (39.1 to 47.0 ms over the 1,500-tick blocks) where `base`'s stayed within
  36.5-41.3: the divergence does not make the ghost cheaper over time.
- Processor time per tick (every thread of an X4): 52.9 and 53.9 ms per client in `base`, 65.1 ms for the one client
  sampled in `hyb12`; on a shared PC this includes waiting threads that spin, so it does not separate the split's cost
  from the load beside it either.
- Everything else of the tick, the universe at low attention plus UI, probe and gate, is paid by both clients in every
  variant.

## 4. The model in Repast4Py's terms, and where X4 breaks it

Repast4Py (Argonne, MPI) decomposes space: each rank owns the agents in its part; ghosts are read-only copies of the
neighbours' agents in a buffer strip, refreshed by `synchronize()`; an agent crossing a border is serialized and rebuilt
on the neighbour rank (migration); one `synchronize()` per tick exchanges migrations and ghost updates; each rank has its
own seeded RNG stream; agent IDs are `(id, type, rank)` tuples, unique per rank by construction.

| Repast4Py | Peer sharding on X4 | Server sharding on X4 | What X4 has today |
|---|---|---|---|
| rank | a player's PC | a headless X4 (the VPS runs GPU-free Linux X4) | every PC simulates everything (one "rank") |
| owned space | the sectors its player is in | a set of sectors per server | - |
| agents | every component in the owned sectors | the same | ~440,000 saved components, ~535,000 census objects in a played save [M] |
| ghost | the other owners' sectors, run at low attention locally and corrected from their owner | every sector not owned by that server; on a player PC everything but what it presents | the H1 proxy table: published poses compared, nothing written (`lockstep_hybrid.h`) |
| buffer width | bounded interaction radius | the same | **none**: MD, trade, find searches, faction directors read any object in any sector in the same step |
| migration | ownership handoff at a gate, accelerator or superhighway, object state packaged like the hot-join package | the same between servers | hot join: X4's whole-game save plus a side table of 98 sections the loader drops, ~19,400 lines (`lockstep_join*.h`, `native_gate_join*.py`); no per-object or per-sector save exists |
| `synchronize()` | the tick barrier plus corrections as inputs | server-to-server and server-to-PC state streams | the relay's per-tick barrier; inputs only |
| per-rank RNG | per-owner streams | per-server streams | every engine draw keyed by tick and stable object identity (`*_rng.h`), stronger than per rank; the exception is the per-frame natural word, drawn in walk order |
| agent IDs | per-owner ID ranges | per-server ranges | one FIFO allocator for components, listeners, shapes, controllers (96 callers); the gate makes it a shared counter and keeps two local blocks (30 view, 31 UI) for client-only work (`lockstep_runtime_ids.h`) |

Where X4 breaks the pattern:

1. **No bounded interaction radius.** Repast4Py is correct because an agent only reads agents within the buffer. X4's
   MD and AI read objects anywhere synchronously (a cue's condition on a ship's hull, a trader's search over stations of
   several sectors, the faction managers' counts), and they run in the same step as the owner's simulation. A read of an
   owned region from outside sees the live object on its owner and a ghost elsewhere: the outside parts on the first read
   that differs. There is no hook for "any read of an object" (hybrid report 3c, 4/H4).
2. **Ghosts are not passive.** A Repast4Py ghost is a copy the framework refreshes. An X4 ghost is a full engine object
   that keeps simulating at its local level: it flies, trades, fights by script, takes IDs, raises MD events. Freezing it
   (no Move, no AI, no timers, no events of its own, yet readable) is a new engine mode for every object class (H3 in the
   hybrid report: large).
3. **Agents cannot be serialized one by one.** X4 saves only the whole game. Even a whole save does not load back into
   the running world: the hot join needed 98 side sections (AI wake tree, MD heap and cue instances, navigation banks,
   attention dues, shield groups, bodies, listeners, zone references, drones, ...) to make a load equal to the session, and
   its writers fix up the *same* simulation (same class, same controller kind, rounding-sized differences). They refuse,
   by design, an object whose controller or flight model differs, which is what a low-attention ghost against a
   high-attention owner always has.
4. **Global MD scripts.** MD state is one global cue tree; the faction managers, mission directors and subscription
   directors are nobody's and react to attention changes (section 1). Fed per client, they part at the first event.
5. **The shared runtime-ID allocator.** A creation on one client only moves every later ID on that client for good
   (`lockstep_runtime_ids.h`: "nothing here repairs that"). An owner creates what its ghosts never do (bodies' shapes,
   projectiles, effects, NPCs from `npc_instantiation`, mass traffic). Per-owner ID ranges are possible in principle (the
   map has 32 blocks of 2^20 slots, two already local), but every object visible to shared code needs the same ID on
   every client, and X4 orders and hashes by ID.
6. **Physics bodies exist only at high attention.** The ghost has none; the owner's body state (velocities, contacts,
   sleeping) has no counterpart to correct. A migration into a receiver raises attention there, and X4 then builds the
   body from the ghost's abstract state, relocates the ship to a free spot and restarts its scripts at a label: the
   receiver's high-attention state is constructed, not received.
7. **The level change is itself an event stream.** Ownership changes are attention changes on some client, which post
   events into MD and AI (section 1). Repast4Py's migration has no side effects on the rest of the model.

### 4.1 The global layer, item by item

What has to stay identical on every client whatever the regions do, whether it reads inside other players' regions,
and what would keep those reads identical:

| Item | Reads inside regions? | What would keep it identical | Hook point today |
|---|---|---|---|
| RNG streams | the keyed streams draw per (tick, object identity, call site): a draw inside a region moves no stream outside it [S, `*_rng.h`] | already region-scoped by construction; the per-frame natural word (walk-order draws, e.g. random impulses above `nearby`) needs a word per region | yes (the region RNG and script RNG scopes are the pattern) |
| Runtime IDs | every creation anywhere takes the next shared ID | allocations of a region's own work from a per-region block (as blocks 30/31 for view and UI work); an object that leaves the region or is seen by shared code needs one ID on every client, so its creation has to be an input | partly (the local-list mechanism); not for objects that cross |
| Created identities (portable keys) | keyed by parent, type, macro and a per-key ordinal per tick [S, `lockstep_world_created.h`] | already region-independent | yes |
| Event queue and AI schedule | events and wakes of a region interleave with the outside's; delivery is keyed [S] | per-region filters on delivery; the events themselves differ between owner and ghost | yes for ordering, no for content |
| MD (missions, stories, faction and subscription directors, NPC instantiation) | yes: cue conditions read object properties anywhere, and 18 base MD files listen to attention changes | every read of a region served from the owner's published state on every client, the owner included; attention events of regions withheld or fed as inputs | **no**: MD expressions read engine fields directly |
| Economy (trade offers, station cargo and money, faction money) | traders anywhere query stations anywhere; a region's ships trade on owner-decided ticks | trades of a region's ships published as inputs; station bookkeeping kept shared | no hook for the query side |
| Factions (relations, war, police) | kills and scans in a region change relations | the owner's kills and scans as inputs | partly (Gravidar applies are hooked, `lockstep_ai_schedule.h`) |
| Job engine (spawn quotas) | quotas count ships universe-wide | counts from published membership | no |
| Census and digest | the census can split its roots by region (H1 does) | outside roots compared by the relay, region roots between owner and ghosts for corrections | yes (H1) |

## 5. Variant 1: peer sharding

Each player's PC owns the sectors its player is in; every other sector it holds as a ghost at low attention; the rest of
the universe (no player) stays lockstep at low attention on every PC, as today.

**What flows.** Inputs stay lockstep (the relay's blobs). Each owner publishes, per tick, what the rest of the world can
observe of its region: movers' poses and velocities, hull and shield, objects created and destroyed, objects leaving the
region (with their orders, cargo and AI position), trades and money, MD-visible events (kills, scans, signals). For the
outside to stay identical, every client, the owner included, must apply these at the same tick t+d, and every read of
the region from outside must see the published value, not the live one. The second half has no hook in X4 (section 4,
point 1): this is the variant's central open problem, not an implementation detail.

**Seams: gates, accelerators, superhighways.** A gate jump is a discrete engine event: the ship enters the gate's
trigger, the gate's pass posts `EnteredGateEvent`, then a `WarpEvent` that the MoveWorker delivers a pass later moves
the ship into the destination gate's zone [F, `lockstep_pilot_transit.h`]. That is the natural point to migrate one
object: its state at the warp is small and well defined (destination zone, pose, speed, orders), and the receiver
would raise it to high attention in its own region. A superhighway is a zone of its own between two sectors (during the
trip the ship's zone is the highway, as the test op `place` reports it [S], moved by `Highway::Move` [F, attention
study 3c]), so a highway trip is a migration with a stay in
no-one's region in between, which is easier, not harder: the trip can be simulated by everyone (low attention, a
spline parameter) and taken over at the exit gate. NPC ships use the same transitions, so the seams cover them too.
What the seam does not cover: the ship's AI script position and waits, its MD cue instances, its navigation banks
and pending path jobs, its drones, its crew's NPC actions. The hot join captures exactly these, but for the whole
universe after a whole-game load, keyed to the same simulation (section 4, point 3).

**The global layer.** Two options, neither works on this engine as is:
- *Kept lockstep everywhere, fed with owners' results as inputs* (the hybrid report's H4). It needs (a) every global
  read of an owned region to see the published state on every client and (b) the global scripts' own attention events
  to be identical, which they cannot be because attention differs per client by construction. Section 3.2 shows the
  global layer parting within 20 ticks of the split.
- *A global shard* (one authority runs MD, economy, factions, job spawns): it must read every region's state each tick
  and write into every region (MD spawns, orders, warps, damage). That is the server model for the global layer, with
  the server needing each region's full state with no delay: the same problem one level up.

**Contested sectors.** A sector with two players must be computed identically by both (each player's own ship and
everything near it is high attention for its own client). Options: one owner and the other player as a "guest" whose
own ship and view come from the owner's stream (state replication with an authority inside one sector, the guest's
inputs take a round trip); or fall back to full lockstep for that sector, i.e. both clients simulate it at high
attention from the same state. The fallback needs the guest's ghost to become bit-identical to the owner's region at the
handoff tick: a per-sector hot join, which X4 cannot do (no per-sector save or load). The only exact handoff available is
a whole-game resync: every other client loads the owner's whole save. With two or more owners it is not exact either,
because the owner's save holds its own ghosts of the other owners' regions (no merge exists).

**Latency.** Own region: none added (the owner is the authority; X4's own player path). Global layer: unchanged, the
tick barrier still waits for every peer's inputs. Ghosts: corrections published at t and applied at t+d (d = 1 +
lookahead, 2-3 ticks = 67-100 ms) plus the transport; far away, so invisible to the local player as long as nothing
ties the two regions together.

**Cost per PC.** U + H_own + ghost upkeep + scope overhead, instead of today's U + n x H. Section 3 gives U and H.

## 6. Variant 2: server sharding

Headless X4 instances (GPU-free Linux X4 on the VPS) own sets of sectors; players' PCs present their region and send
inputs.

**What flows.** Inputs from each PC to the server owning its player's sector; state from each server to every PC that
presents part of its sectors (the PC's own ship included); between servers, the same boundary traffic as variant 1.

**Seams, global layer, contested sectors.** As in variant 1 between servers. Contested sectors are no problem *inside*
one server (both players are its clients); they move to the server boundaries, and a sector change of a player
between servers is a migration.

**The player's PC.** X4 cannot render a world it does not hold: a presenting PC still loads the whole universe and runs
X4's simulation unless every class's simulation is suppressed while staying readable and renderable. That is the
authoritative-server fork's unsolved core ("selective suppression"; the authoritative-server fork of this repository, status 26 Nov: in the
last human test the other player's ship appeared stationary and the shooter's damage did not reach the target's
screen; automated tests show movement and health corrections in single cases). Without it the PC pays U anyway, and its
local simulation fights the stream.

**Latency.** The player's own ship lives on the server: every input takes a round trip before its effect is real (the
VPS is ~20-40 ms from a European home link [I]), so the own ship needs local prediction and correction. X4 has no rewind,
so a misprediction can only be corrected forward (the same limit `lockstep_own_predict.h` works within for lockstep's
input delay). Shots and hits are server-side: a hit seen on the shooter's screen is a prediction.

**Cost.** Server: U + its owned bubbles' H, with no GPU. The only number from this VPS is a two-client census run under
software rendering at low CPU weight beside other services: 1,650 ticks in about 4 minutes, ~7 ticks/s [M, VPS notes];
one uncapped headless instance at 30 Hz is unmeasured. Each server holds the whole universe (U) whatever share of it it
owns, so n servers pay n x U. Player PC: U (the world is still loaded) + applying the stream, unless suppression
works.

## 7. Divergence: detection, soft correction, bounds

**Detection** is the cheap part and exists: the census can be split by region (H1 declares outside roots and an inside
root at each capture), the owner's record carries a digest of its movers every tick, the runtime-ID head is a
per-tick word, and `attn_split_census.py` names each differing object with its region and class.

**Correction inside a ghost region** needs writers for state that differs in kind (section 4 points 3 and 6). Poses of
pure transforms and linear controllers can be written today (the join's POSE and NAV writers), but a ghost ship flies
the linear model where its owner flies steering or spline with a body: writing the owner's pose into the ghost moves it
once; its own controller moves it on from its own state the next tick. Correction at a cadence bounds the ghost's
position error by the drift between corrections (section 3.2: a few metres a few seconds after a correction, ~100 m
after two minutes), not its AI state, orders, cargo or created objects.

Bandwidth is not the limit. Every member of a region every tick at H1's 36 bytes is 102-211 KB a tick for these two
sectors (2,800-5,900 movers, static parts included: 25-50 Mbit/s at 30 Hz); only what changed is far less: in `base`
100-1,900 objects of slot 1's sector and 250-540 of Segaris changed per 30 s, i.e. 3-60 objects a second, about
0.1-2 KB/s per region before events. The input wire carries 240 bytes per slot and tick (58 kbit/s) today, so
corrections need their own channel either way (hybrid report section 5).

**Bounds.** Inside a ghost region a correction can bound the error. The worst case is outside: anything the ghost does
that touches shared state (a ghost trader sells at a station outside, a ghost shot kills an outside ship, a ghost
creation takes runtime IDs, a ghost attention change wakes a faction manager) changes the outside on one client only,
and the outside has no owner to correct it from. Such a difference does not shrink: it propagates through the economy,
the MD and the ID stream. The only bound is a fallback that restores one world: a whole-game resync (every client loads
one save; today 1-3 minutes, about a minute of it stopped, with the fast join [M, `LOCKSTEP-FAST-JOIN-20261123.txt`]). So the fallback to full lockstep
for a region is not "turn its attention back up": the ghost has parted, raising it starts high-attention simulation from
a different state. It is a resync of the whole game from one client, every time any outside difference appears.

**Fallback rule, and what the measurements make of it.** A sound rule set would be: (1) any difference in the shared
ID head or the outside census roots between clients: resync the whole game from the session's host; (2) a player about
to enter an owned sector (its ship in the gate's trigger or a highway towards it): resync, then shared lockstep for
that sector; (3) a ghost region whose published digest and local copy differ beyond a bound: correct, and resync if the
corrections do not hold. Measured today, rule 1 fires at the first tick after every split and rule 2 at every
sector change of a player, so the mode would spend its time resyncing (a hot join takes 1-3 minutes, about a minute of it
a stop for everyone: the save ~9 s, the handover ~50 s with the fast join). Rule 1 can only stay quiet once stage 1
of section 9 has closed the shared channels.

### 7.1 The variants side by side

| | Today (full lockstep) | Peer sharding | Server sharding |
|---|---|---|---|
| Own ship, own region | simulated on every PC; input applied at t+d (2-3 ticks, 67-100 ms), the own ship predicted locally | simulated on the own PC only: no added delay | on the server: a network round trip plus a server tick; forward-only correction of a local prediction |
| Other players' regions | identical, simulated everywhere | ghosts, corrected t+d after their owner (far away) | streamed from their servers |
| Global layer (MD, economy, factions, IDs) | identical by construction | must stay identical with outcomes as inputs; measured: parts within 20 ticks | one owner per piece or a global shard; every region's state needed there each tick |
| Contested sector | nothing special | one owner (guest gets a stream) or a fallback to lockstep that needs a whole-game resync | inside one server: fine; across servers: migration |
| A player changes sector | nothing special | migration at the gate/highway warp; object state has no per-object serializer | the same between servers |
| Cost per PC | U + n x H | U + H_own + ghost upkeep + publication (1.4-2.6 ms measured for H1's) | PC: U unless the simulation can be suppressed; each server: U + its H |
| Fallback | - | whole-game resync from one client | the same |
| What exists | everything | H1 (split + publication + proxies, status only) | the fork's single-server prototype, not playable (26 Nov) |

## 8. Recommendation

1. **Keep full lockstep as the product.** The measured split leaves the regions within one tick and reaches the
   whole universe within 500 ticks; per-region correction cannot bound that (section 7), and what the split saves is a
   few ms per remote bubble.
2. **For the goal (many spread-out players at 30 Hz), build the symmetric cuts first** (section 10), measured with
   `mask2` against `base` (the price of a bubble, in sync) and the release check's perf.
3. **If sharding is still to be explored, peer sharding is the variant to start with**, for four reasons:
   - it is a strict extension of today: any sector it cannot handle stays lockstep, contested sectors are today's
     shared lockstep, and every stage can be switched off per session;
   - the player's own region stays on the player's PC: no added latency for what the player sees and flies, where
     server sharding puts the own ship a network round trip away with forward-only correction;
   - it needs no presentation-only client; server sharding does, and that is the authoritative fork's open problem;
   - its first two stages are exactly the questions server sharding would hit at every server boundary (IDs, MD,
     scheduling, outcomes as inputs), so a no at stage 2 answers both variants for the price of one.
   Server sharding's only advantage, a PC whose cost does not depend on the world at all, does not exist with X4: a PC
   that presents a region still loads and runs the universe (section 6).
4. **Go/no-go at stage 1:** if the quiet split (`hyb12` in two quiet sectors) does not keep the shared ID head and the
   outside census equal for 10,800 ticks once the scoped channels are closed, stop. The status rows of this study name
   the 61 modules that parted; the pathing budget and the MD attention events are the first to close.

## 9. Staged plan (if sharding is built anyway)

Every stage is a default-off switch on top of today's lockstep; the session defaults stay full lockstep, and any stage
whose check fails falls back to it (the region's owners' slots become view points again on every client, which is
exactly today's setup). A stage is only started when the one before passed its gate on this PC's harness
(`tools/dev/attn_split.py` and `attn_split_census.py`); the first failed gate ends the track.

| Stage | What is built | Proved by | Size [I] |
|---|---|---|---|
| 0 (this branch) | The split as a test switch (`native_gate_hybrid`, H1, built 1 Nov); the measurement driver and the census classifier by region | the numbers in section 3 | done |
| 1. Quiet split holds | Sector scopes on every client for the work of an owned or ghost sector, with the hook points the gate already has: runtime IDs from a per-sector local block (the `runtime_ids_local` pattern), the natural RNG word saved and replaced per sector subtree, job ordinals keyed by sector, cluster-level region content assigned to a sector, the pathing budget per sector; attention-change events of owned and ghost sectors withheld from listeners outside the sector | `attn_split.py run --variants hyb12` in two quiet sectors, 10,800 ticks: the shared ID head and the outside census equal at every capture (today: section 3) | 4-8 weeks of agent time, 3-6k lines of C with tests; the MD-event part changes stock script semantics and may not be acceptable |
| 2. Outcomes as inputs | Departures and arrivals at gates, accelerators and highways (the warp as the seam), trades with outside stations, kills, Gravidar and MD-visible signals, each an input op applied at t+d everywhere, the owner included; outside reads of the region served from the published state | hyb12 with NPC traffic across the border, 30 minutes: outside equal | open-ended. There is no hook for "a read of an object in the region" (MD expressions, AI searches read engine fields directly): this is the go/no-go of the whole track, and the expected outcome is no-go |
| 3. Ghosts held | Frozen proxies (ghost movers do not simulate, their poses written each tick from the owner's record) or corrections at a cadence; display proxies for a camera that looks into a ghost | ghost position error bounded (section 3: today's drift), no outside effect for 30 minutes | 2-3 months: a suppressed-but-readable mode per object class |
| 4. Handoff | A player entering an owned sector: first as a whole-game resync from the owner (the fast join: about a minute of stop), later per sector | crossings both ways, census equal after | weeks for the resync form; a per-sector form needs a per-sector save that X4 does not have |
| 5. N players | peer sharding with 4-8 players spread, against lockstep | release check perf: tick time flat with the player count | - |

A rough total for stages 1-4: 6-12 months of agent time with a high chance of stopping at stage 2. For comparison, the
hot join, which could lean on X4's own whole-game save and only had to make a load equal to the same simulation, took
25 days and ~19,400 lines and is still being extended.

**Server sharding** would add, on top of stages 1-4 at the server boundaries: a presentation-only client (every class's
simulation suppressed while it stays readable and renderable: the fork's open problem), own-ship prediction with
forward-only correction, and a server that holds a universe at 30 Hz without a GPU. It should not be started before
peer sharding's stage 2 has passed.

## 10. What to build instead for the same goal (cost flat with the player count)

The goal behind the idea is that a session with many spread-out players keeps 30 Hz. Today's cost model, from section 3:
tick = U + n x H, with U every client's universe at low attention plus the gate, probe and UI, and H the price of one
more occupied sector. Sharding can only remove (n - 1) x H per PC and leaves U. These keep every world identical:

1. **Step the physics scenes of different sectors in parallel.** Each occupied sector has its own Jolt scene
   (`lockstep_view_scenes.h`), and the scenes are stepped one after the other on the MoveWorker. Scenes of different
   sectors share no bodies; stepping them on worker threads with each scene's contact callbacks buffered and applied in
   scene order after the join keeps determinism by construction (attention study 6c). Its gain is the physics share of
   H: small in a quiet bubble (physics was 0.2 ms of the frame in the 1 Nov sampler), large in a battle. Measure the
   split of H first: `attn_split.py run --variants base_s,mask2_s` and `census_profile.py RUN --against RUN`.
2. **Take presentation-only high-attention work out of the shared step** where no simulation code reads it: effect
   objects, room props and interiors (level 9 and above), crowds, through the runtime IDs' local lists and the render
   RNG split that already exist. Piece by piece; each piece is checked with the census.
3. **A symmetric bubble-size knob** (the 50 km `visible` margin of zones and the 250 km body radius of ships), the same
   on every client: fewer objects per bubble, at the price of farther ships flying the linear model.
4. **U itself**: the UI and probe share of the frame (40% in the 1 Nov sampler), the universe update, and one X4 per PC
   (every number here is two X4 on one PC).
5. **Cap the spread by design** if needed: the session defaults cover eight sectors; a session can refuse a ninth
   occupied sector or merge players' bubbles (a player alone far away flies at low attention for everyone, the
   `view_points_mask` mode measured here as `mask2`, which stays in sync).

Each is measured with this branch's `mask2`/`base` pair (H) and `release_check.py perf` (U).

## 11. Risks

- The measured runs are two X4 on one PC with two other X4 on it; absolute times are higher than on a player's PC.
  The differences between variants on the same PC are the measure.
- One pair of sectors (the cadet start and Segaris, both quiet at the time). A battle in a remote bubble costs more
  (estimated 10 ms or more, not measured here); that raises what sharding could save, not its correctness.
- A divergence seen in the census is a lower bound: AI script state, MD cue state, cargo and money are not in it.
- The hybrid switch (H1) evaluates the outside without the owners' ships on every client, so the hybrid runs are not
  the base run minus a bubble; `mask2` is the clean cost measure and was not run (one run per variant, no repeat:
  the cost numbers carry this PC's run-to-run noise of a few ms; the divergence numbers do not depend on it).

## 12. What is on this branch

- `tools/dev/attn_split.py`: the measurement driver (`run --variants base,hyb12,hyb1,mask2[,base_s,mask2_s]` under the
  PC's slot locks; `report RUN...`). It continues the release check's rk9-save, moves slot 2 into Segaris at plan tick
  60 (`goto_obj`), runs a fixed flight plan, samples memory, keeps the rows it reads (`clientK/attn-rows.txt`) and
  removes the raw logs and saves.
- `tools/dev/attn_split_census.py`: each differing object of two clients' census records by region (the players'
  sectors, the rest) and class, per capture, and the footprint.
- The switches the runs use are all existing and default off: `native_gate_hybrid` (`lockstep_hybrid.h`, H1, stop 187),
  `native_gate_view_points_mask`, `native_gate_sample_profile`. No gate change: the DLL is rk18/rk19's c7c7ece3.
