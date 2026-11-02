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
  Making the pass deterministic and parallel again is open speed work.
- **Earliest divergence is far before the digest.** `tools/dev/census_first_status.py`
  compares every module's status line between the clients. On the seed snapshot
  with the full profile (ui_lazy 'block', UI made no queries in the soak), real
  simulation differences start early: ACTOR_RNG_ALL at tick 45 (one ship's keyed
  Move draws once on one client, twice on the other), EVENT_RNG at 600 (one more
  due-event delivery with draws), WARP_RNG at 1140; the digest (fixture rows,
  world hash every 300 ticks, runtime-ID word) only catches the consequences near
  4900. Counters named moved/reordered and the pathing bank differ by design.
