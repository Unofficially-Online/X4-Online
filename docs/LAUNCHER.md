# x4ls launcher

`python tools/x4ls.py <command>` runs lockstep clients on one Windows PC. Every
client is a separate, windowed X4 process with its own game view and its own
disposable profile.

## Commands

| Command | What it does |
|---|---|
| `doctor [--overlay DIR]` | Finds X4 through Steam's library folders, verifies the pinned 9.00 `X4.exe`, and checks that directly started clients can reach Steam |
| `snapshot --out FILE [--gamestart custom_creative]` | One client starts a new game, spawns the 12-ship gate fixture and saves it; FILE is the starting snapshot |
| `session --clients N --overlay DIR [--snapshot FILE] [--ticks T] [--observe] [--lookahead 1] [--fast] [--steam-fix]` | Starts the relay and N clients, tiles their windows, waits for the relay to finish, collects logs into `.local/runs/<time>/` and cleans up |
| `session ... --checkpoint-at T` | Every client saves the world right after tick T (`clientK/save_001.xml.gz`); the session ends there (outcome `checkpointed`) |
| `triage RUN [--steam-fix]` | Reruns a diverged session with diagnostic windows over the 300 ticks before its first mismatch and writes `RUN-triage.txt`; `--report-only RERUN` only reports |
| `clean` | Stops processes and removes views left behind by a crashed session |
| `steam-fix [--restore]` | See "Steam" below |

## How a client is isolated

- **Game view:** `<game drive>:\x4ls-lanes\cK` holds hard links to the
  installation's top-level files and junctions to its folders, so nothing is
  copied. `extensions\` is a real folder with junctions to the official DLCs and
  this client's own `x4_lan_lab`. Third-party mods in the installation are not
  linked. The probe finds the gate DLL relative to the working directory, so it
  has to run from the view. Removing a view unlinks; it never enters a junction.
- **Profile:** `-personalfolderid` uses 45850, 45851, 45853 to 45858 for clients,
  and 45841 for snapshot capture only. The profile is wiped before each run,
  then gets a windowed `config.xml` (autosave off, protected UI off) and the
  snapshot in `save_009`. Any other profile id, including your own, is refused.
- **Extension:** `src/lockstep/base` plus the overlay, with
  `@X4LANE_PORT@ / TOKEN / SLOT / CLIENTS@` filled in per client. The overlay's
  manifest is checked first.

## Steam

X4's `steam_api64.dll` finds the running Steam client through
`HKCU\Software\Valve\Steam\ActiveProcess\pid`. Current Steam clients can leave
that at 0, and then every directly started X4 stops with "Steam client is
required". Steam's own launch works but allows only one instance, and it asks
you to confirm the extra arguments.

`x4ls steam-fix` writes the running Steam's process id into that value and
records the old one. `x4ls steam-fix --restore` puts it back. The session
command never changes it by itself; `doctor` tells you when it is needed. Steam
still checks ownership as usual. Only `SteamAppId`/`SteamGameId` are added to a
client's environment. Copying a Steam-launched game's whole environment
crashed the Steam client once in testing.

Use `session --steam-fix` to register Steam's process id before each client and
restore the old value when the session ends.

## Several X4 processes

X4 creates the named mutex `EGOSOFT_X4_INSTANCE`. A second process shows "Game is
already running!" and exits with code 50 when that box is closed. The launcher
starts clients one after another. Once client k logs `Entering startmenu`, the
launcher closes client k's handle to that mutex (`DuplicateHandle` with
`DUPLICATE_CLOSE_SOURCE`, `instance.py`), and then starts client k+1. Only
processes the launcher started are touched.

All snapshot clients must arm and load together: after the load X4 runs its UI on a new
thread, and the gate adopts that thread only once the first tick is issued, which needs
every client. x4ls therefore writes one absolute time, `native_gate_arm_at` (UTC seconds,
read in the probe through `GetCurrentUTCDataTime`), into every client: start time +
`--startup` (70 s) per client + 20 s. It fails fast if a client reaches the start menu
too late. `native_gate_relay_timeout_ms = 120000` gives the gates slack for different
load times.

Check an overlay offline before a launch: `python tools/dev/gate_preflight.py <overlay>`
runs the probe's arming under LuaJIT with a permissive fake gate and reports the first
refused option.

## Overlays

Build one with the existing builders, for example the two-human sandbox:

```
python tools/dev/build_native_gate_overlay.py --mode battle --gate-dll .local/native-lockstep/build/x4lan_gate_64.dll --snapshot <save> --session-clients ... --out .local/overlays/base
python tools/dev/native_gate_sandbox.py overlay --base .local/overlays/base --gate-dll ... --probe tools/dev/native_gate_probe.lua --out .local/overlays/sandbox
```

Overlays are frozen: a builder refuses to write into an existing directory, and
`x4ls` refuses an overlay whose files no longer match its manifest.

## Outputs

`.local/runs/<time>/`: `relay.log`, `native-barrier.json` (relay report),
`native-inputs.jsonl` (input journal), `desync-status.json` (observe mode),
`clientK/x4ls-cK.log` (each X4 log), and `summary.json` (passed, failed,
relay-failed, or timeout).

## Soak profile and determinism findings (1 November 2026)

Soak overlays (`tools/build_soak_overlay.py`) are bare: `native_gate_defaults = false`, so a soak runs
exactly `src/lockstep/profiles/soak.lua` and the builder's flags. `--defaults` puts the session default
stack (`tools/dev/native_gate_defaults.lua`: piloting, prediction, on foot, wire crypto, Silver's UI
guard) on top, for a soak of the full session.

`python tools/build_soak_overlay.py --snapshot SAVE --gate-dll DLL --out DIR` builds the
validated soak overlay (`src/lockstep/profiles/soak.lua` plus base flags and scripted
pilots). Switches: `--window`, `--play-mode`, `--no-pace`, `--long`, `--world-schema`,
`--drop KEY`, `--set KEY=VALUE`.

- **Session seed:** every client needs the same `-seed:` (x4ls passes 276944566).
  Without it, X4 seeds each job's generator from a per-process seed and the clients
  spawn different job ships from tick 0.
- **Pathing bank:** without real-time pacing the clients may run with the pathing
  bank offset by one. Schema `WORLD_IMPORTED_1` hashes the raw bank index and
  reports ~10,000 ships at tick 1. It is equivalent state (census finding 9); use
  `WORLD_IMPORTED_2`, which the builder defaults to.
- **Camera anchor:** with `native_gate_camera_anchor = 'own'` client 2 allocated two
  more runtime IDs at tick 4693, reproducibly; without it the same run held. Own
  cameras are not yet simulation-neutral.
- **No pacing:** unpaced runs diverged once at a timing-dependent tick (3919, not
  reproduced); paced runs have held. Pacing costs little (0.85-0.92x vs 0.99x).
- **Open: a wall-clock dependency around minute 2-3.** On the seed-276944566
  snapshot, paced, shared camera: plain runs diverge at tick 4953 (twice, with
  native and with device controls); runs with diagnostic rows diverge at 4073;
  moving the row window off 4073 makes the same run pass it. The diverging
  allocations are on the MoveWorker in X4's AI script engine
  (AIDirector::Move -> ExecState_Run -> AI::RunOrderScriptAction /
  AI::RunScriptAction -> new script instance): one NPC starts an order script
  three times on one client and once on the other. Script keysets (SCRIPT_RNG)
  agree until 4260 in that run. Because per-frame cost changes the outcome, an
  engine worker the gate does not join (asset/collision loading is the census's
  known case) still decides what the AI sees. Next: find that worker (census_jobs,
  job_rng 'observe', loader joins) rather than adding more per-tick logging.
- **Lazy station updates (2 November 2026).** X4 brings a station's cargo/production
  (`0x41a910`), trade offers (`0x4079e0`) and units (`0x4d8110`) up to date when
  something asks, and those updates draw from the *calling* thread's RNG word. The
  first actor to ask decided the station's stream: a TradeCompletedEvent at tick
  4658 and an UpdateTradeOffersEvent due 9 s apart at tick 5424 split the clients.
  `native_gate_ui_lazy = 'key'` (the soak default) blocks UI queries as `'block'`
  does and seeds each simulation call by the station's portable key (cargo: plus
  the target game time; trade/units: plus tick and the state that decides the
  work), restoring the caller's word afterwards. With it plus the nav_player fix the
  seed snapshot held 16,000 ticks (8.9 min) with no digest mismatch and no
  EVENT_RNG/UI_LAZY status difference; before: digest mismatch at 4906, 6230, 7500.
  Remaining tick-1 status differences (gravidar sectors, region word, script cache
  hits, schedule `moved`, pathing bank) are local bookkeeping and also appear in
  passing runs. A UIElement KillEvent (no draws) can land one frame apart; it only
  changes the EVENT_RNG keyset.
- **First 2-hour soak with 'key' (2 November 2026):** digest mismatch at 17,700
  (9.8 min). Every drawing event row agreed to the end; the first simulation
  difference was one extra formation transform (GetTargetPositionForWingMan, a
  pathing pass job, `not_ancestor`) on client 2 between 17,400 and 17,460. A rerun
  of the same overlay with the formation row window
  (`native_gate_path_transforms_rows = {from, to}`) passed 17,800 ticks with no
  PATH_TRANSFORMS difference: this split is timing-dependent, not reproducible.
- **Cause: a race between pathing batch jobs.** Five more runs to 17,800 showed two
  outcomes at 17,700 (world hash 7504… or 3827…), picked per client roughly 50/50.
  Per-tick status (`native_gate_trace_window`) and formation rows with return chains
  put the difference at tick 17,441: a ship just built at a Xenon shipyard
  (dock 2c023) gets one extra GetTargetPositionForWingMan call from 0xeb27c0's
  own-target branch (0xeb2f07, taken when the agent's +0xf20 formation link is
  set) on one client. Script RNG, build, created and event rows all agree; the batch
  lists agree. The jobs of one pass run in parallel, so which batch writes before
  another reads depends on thread timing. `travel_snapshot` only covers the leader's
  +0xf7b. `native_gate_travel_determinism = true` (now the soak default) runs the
  batches inline on the pathing thread in RunPass order with a job-thread TLS
  context: the next run passed 17,800 ticks with no PATH_TRANSFORMS or ACTOR
  difference. Cost: about 8 ms per pass, 0.78x real time paced (was 0.92x).
  Making the pass deterministic and parallel again is open speed work. At tick 27,360 of a
  soak the inline pass averages 9.7 ms (22 batches, 10,252 agents) out of about 43 ms per
  tick. AssignAgentToBatch 0xeaf2b0 deals agents round-robin (cursor +0x1b8 over the
  batches at +0x180; agents whose flight model reports type 3 go to +0x198 with cursor
  +0x1c0), so a leader and its wingmen usually sit in different batches: that is the race.
  Keeping each formation in one batch (moving an agent when it joins or leaves a formation)
  would let the batches run in parallel again if formation reads are the only cross-batch
  reads in the pass; the census found no others so far.
- **Unkeyed engine draws (tick 35,601).** With travel determinism the seed snapshot held
  35,700 ticks. Then a police ship launched three drones and each pilot's character macro
  (an engine draw on the MoveWorker's own, process-local word) differed. `seed_rng` (the
  soak default now) seeds that word per admitted frame; a draw watch
  (`native_gate_draw_watch_frame`) over 3,600 ticks found all 7,244 frame-level draws
  identical on both clients. The next 2-hour soak (seed_rng, ui_lazy 'key', travel
  determinism) stayed in sync to tick 54,489 (30.3 game minutes), past 35,700, and then
  stopped on both clients at the same tick: strict physics read the body of a fixture
  wreck that X4 had just cleaned up (a probe assert, not a desync; fixed).
- **Soak E (seed_rng, wreck fix): 94,800 ticks (52.7 game minutes)** before the world hash
  parted, with no status or event-row difference before it. Its triage rerun parted
  earlier, at 39,060, and so did the next soak (F) at the same tick: reproducible with the
  later overlays. Both show NAV_OCTREE `snap_waits` differing at 35,520 (the octree builder
  thread's 50 ms waits: timing, local) and the first simulation lead inside 39,001-39,060:
  one more agent draw (ACTOR_RNG_ALL `agent_drawn`). UI_LAZY_CALLERS (simulation lazy
  updates per call site, always on) agreed to the end in F, so the earlier extra
  UpdateTradeOffers call of E's rerun (26,820) did not recur. Two triage reruns of F: the
  first parted at 39,066 (just past its row window, which then ended at the mismatch; all
  rows equal inside it, PATH_TRANSFORMS `spaces`/walk and agent draws differ by 39,120), the
  second, with the window running 60 ticks past the mismatch, passed 39,360 ticks. So 39,060
  is timing-dependent too, and the soak profile now keeps every drawing actor scope as a row
  (`native_gate_actor_rng_rows`, about 3.5 per tick, written every tick) so that a soak's own
  logs name the actor.
- **Seed 1111111: speech clock.** The campaign's first new seed parted at tick 3,749 (relay
  mismatches at 3,749 and 3,750, equal again afterwards), and its triage rerun parted at the
  same tick. The lead was MD_TIMED_RNG `raise_calls` at 3,748. The script rows named the scopes:
  `NPC_UseCases.OnSpeakFinished` and its listeners (`Setup.LastSpeak_Update`, the mentor's
  `Mentor_Speak_Helper`) ran at 3,748 on client 1 and at 3,750 on client 2. Both clients had
  started the line at 3,636. The speak timer already ends every line at start + duration on
  the unpaused clock (0x6b60e48, a double advanced by the fixed step each frame). That clock's
  value at the first gated frame is each client's boot time, though: 134 s and 108 s here. A
  deadline on a frame boundary then compares differently. `native_gate_speech_clock`
  (`lockstep_speech_clock.h`, now in the soak profile) sets the clock to 65536.0 on every
  client at the first gated frame. Its status shows the clock bit-identical on both clients
  from tick 0. The rerun passed 3,749 in sync. Only the speech code reads the clock (6 call
  sites of its getter, 5 in EntitySpeakData). The same frame update advances two more
  clocks: 0x6b60e38 (scaled) and 0x6b60e40 (running while paused too). Each has more than 20
  readers and still starts at each client's boot time. A later split with a timer lead
  should check those first.
  A client that joins a running session needs the session's clock value, not 65536.0: the
  same per-frame double additions of the fixed step, replayed from tick 0 (see the header).
  The hot join carries it: the saver's value at T rides the package's GATE record, and the
  joiner's admission writes it and marks the clock set (status `unpaused=session:before`).
- **Base seed with the speech clock: 43,052 ticks (23.9 game minutes).** The soak passed 39,060
  and parted at 43,052. The always-on actor rows name the split: three Argon fighters
  (73e9c, 73ead, 73ee3, queued to dock at carrier 73dce) drew their agent updates at 43,051 on
  client 2, one tick before client 1 (equal before that). Event rows were equal. The triage
  rerun passed its window with every row family equal, so this split depends on timing too.
  The only earlier status difference was NAV_OCTREE `hides`/`rerequests` at 35,640, from
  lookups of a station under construction (33e98) whose octree is rebuilt as it grows. A
  hidden lookup behaves as a miss by design, and an extra aggregated row (miss and hidden in
  one frame) fits that count. Lookup rows had only been written on stride ticks; soaks now
  write every one, and triage compares them.
- **Seed 2222222: 100,200 ticks (55.7 game minutes),** the longest soak so far (speech clock
  on). Actor rows were equal to the end. In the stride 100,080..100,140, client 1 made two more
  script draws (SCRIPT_RNG `draws`) and client 2 more pathing work (PATH_TRANSFORMS `spaces`
  +9, `walk_end` +20, `objects` +11). At the 100,200 capture only the fourth world hash part
  differed: the agents' navigation state (`lockstep_world_nav.h`), with positions equal. Soak
  F's split also showed PATH_TRANSFORMS `spaces` differing. The pathing budget is a count
  (`native_gate_path_budget`), not the clock, so the extra work comes from somewhere else.
  The triage rerun parted earlier, at 92,400 (world hash only), so this split depends on
  timing as well. Its row windows came after that split and show only the aftermath. Both
  ticks are world capture ticks (multiples of 300): the navigation part is hashed at the
  captures, so the split lies in the 300 ticks before. When a rerun parts before its window,
  triage now reruns once more, windowed at the rerun's split, and appends that report.
- **Seed 3333333: 86,100 ticks (47.8 game minutes).** Like seed 2222222: no status line or actor row differed
  before the split, and only the world hash differed, at a capture tick (a multiple of 300). The navigation part
  is hashed at the captures only, so these splits sit somewhere in the 300 ticks before; a shorter capture
  stride for that part would narrow them. Triage was stopped to free the clients for the merge checks.
- **Seed 1111111, merged build: 49,038 ticks (27.2 game minutes).** Past its old 3,749 split. One Argon S fighter's
  agent (a6432) drew at 49,037 on client 1 and at 49,038 on client 2, the same class as the base seed's split.
  Every octree lookup row agreed except hidden versus miss, by design. The first triage rerun parted earlier, at
  37,891, and the second rerun, windowed there, passed: timing again. All three runs read the octree of an Antigone
  container transport (8939b) as hidden on one client and missing on the other at 37,896..37,900, from the creation
  site 75b0db; the first rerun parted five ticks before that. A lead to follow, not a cause yet.
- **Nav-only hash (`native_gate_world_nav_stride`, soak profile: 30).** Between the 300-tick census captures the
  navigation parts the last capture read (about 10,000 on the seed snapshots) are hashed again every 30 ticks and
  the last hash joins every tick's digest, so a navigation split stops the relay within 30 ticks. One hash takes
  about 24 ms (a hitch once a second; the soak ran at 0.84x real time). Seed 1111111, 12,003 ticks: 360 hashes,
  equal on both clients.
- **Base seed with the nav hash: 90,630 ticks (50.4 game minutes),** its best since the speech clock. The nav hash
  stopped it: equal at 90,600, different at 90,630, while actor rows, octree lookup rows and every status line
  agreed and the census would have seen it only at 90,900. So a navigation state parts within one 30-tick stride,
  with no other trace. Next: name the object. Page hashes of the nav index every nav tick, and a short in-memory
  history of per-object hashes written at the stop, would give the first differing page and then its objects.
- **Seed 2222222 with the nav hash: 109,050 ticks (60.6 game minutes),** the first soak past an hour. The nav hash
  stopped it (a 30-tick multiple). Actor rows were equal; in the next stride PATH_TRANSFORMS on client 2 composed
  `spaces` +9 and `diverted`/`objects` +11, the same counts as this seed's split at 100,200 in the earlier soak. So
  one recurring event, at different game times: most likely one group of 11 objects (a formation) whose path
  transforms run once more on one client. A per-tick digest of the diverted objects would name the tick, and
  their keys the group.
- **Memory.** A loaded seed snapshot holds about 9 GB committed per client from the first minutes on (two
  clients about 18 GB), steady over a session; earlier figures near 3.4 GB were read before the load. On
  3 November at 19:00 another program's parallel Rust build exhausted the machine's commit limit (128 GB
  physical + 8 GB page file): its rustc processes crashed, the desktop compositor exited, and the campaign's
  next process start failed, ending seed 2222222's triage rerun and seed 3333333's overlay build. Run soaks
  when nothing else large is running, or raise the page file.
- **Checkpoints.** `--checkpoint-at T` makes every client save after tick T; 150 ticks before,
  the MD cue `Respawn` creates a fresh 12-ship fixture beside the player (the battle leaves
  wrecks, and a snapshot needs twelve live ships). X4 pauses while saving, so the gate stops
  (reason 3, `UNOWNED *(double*)(image+SPEED_RVA)!=1.0`); continuing through a save needs
  pause-aware gating. `tools/dev/checkpoint_diff.py A B` compares the world part of two saves
  (everything above `<script>`; the script pools below use process-local allocation ids),
  listener runs as sets, animation index parts left out. Two clients' saves at tick 1,200
  were identical except the local player's target listener (client-local UI state). A
  checkpoint holds more saved objects than a fresh start (525,870 at tick 1,200): the world
  identity table now has 4M slots.
- **Open: sessions from a busy checkpoint split within 15-50 ticks.** From the tick-1,200
  checkpoint the clients part at ticks 14-19 (an extra MD cue raise, weapon fire, AI
  scripts of the old fixture's survivors receiving StopBoost/ArrivedAtWaypoint one tick
  earlier on one client) or at 49. Script, event and UI-triggered events agree before it.
  Not the loading frames: both clients release the same number of pathing passes before
  tick 1 (`PATH ... boot_passes_menu=3 boot_passes_load=0`), and a run whose read banks
  differed at tick 1 passed. `x4ls triage` on a split run: at tick 2 one client alone posts
  and delivers a StopBoostEvent (due = tick 2's game time) for 6c448, a survivor of the
  old fixture (after the respawn no longer a fixture actor, so outside the fixture-only
  flight modules); at tick 3 the survivors' attack scripts react on that client only. The
  event comes from Agent_UpdatePathing 0xe8f250's travel/boost decision (inputs: the
  agent's waypoint flag +0xf7b, the formation leader's flags, the flight model, the +0xf40
  throttle). The world census finds every object equal at tick 1, so the difference is in
  agent state the world hash does not cover. Loading a busy save deterministically is also
  what join and resync need.
- **Triage.** `x4ls triage RUN` does the manual loop: rebuild the run's overlay with per-tick
  status, world pages, row windows (script RNG, event deliveries, formation transforms with
  return chains, UI lazy calls) and created rows over the 300 ticks before the first
  mismatch, rerun, and report the first differing tick of each row family. On the 35,700 run
  it names the drone pilots' character macros directly.
- **Earliest divergence is far before the digest.** `tools/dev/census_first_status.py`
  compares every module's status line between the clients. On the seed snapshot
  with the full profile (ui_lazy 'block', UI made no queries in the soak), real
  simulation differences start early: ACTOR_RNG_ALL at tick 45 (one ship's keyed
  Move draws once on one client, twice on the other), EVENT_RNG at 600 (one more
  due-event delivery with draws), WARP_RNG at 1140; the digest (fixture rows,
  world hash every 300 ticks, runtime-ID word) only catches the consequences near
  4900. Counters named moved/reordered and the pathing bank differ by design.
