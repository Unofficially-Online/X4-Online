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
| `play --overlay DIR --snapshot FILE [--stall-drop 15] [--timeout 0]` | A session for people (docs/PLAYTEST.md): desyncs are silent (relay `--observe --keep-going`), error rows go to `errors.log` instead of ending the session, a client that stops or exits is dropped after `--stall-drop` seconds and the others play on; lookahead, tick rate and length come from the overlay (`tools/build_play_overlay.py`) |
| `play ... --remote-slots K --bind IP --allow-from IP --port P` | The same with slot K on another PC: the relay listens on this PC's address, and the launcher prints the `join` line for the other PC (docs/PLAYTEST.md "Two PCs"); `--close-when-over` ends an unattended run with its session |
| `play --solo K --overlay DIR --snapshot FILE [--clients N]` | One X4 only, for player K of the overlay's N-player session: the relay stands in for every other slot from the start, so what player K sees and hears can be tested without more X4 windows (see "One X4 for one player" below) |
| `join --relay HOST:PORT --token T --slot K --clients N --arm-at UTC --overlay DIR --snapshot FILE` | One client on this PC for slot K of that session (the same overlay and snapshot as the host), logged as `play` does |
| `lobby --overlay DIR --snapshot FILE [--port 47000] [--bind IP] [--lane 1]` | This PC's X4 opens in its main menu with a **Multiplayer** entry (Host a game, Join a game). The same command on every PC; see "Lobby" below |
| `lobby ... --server-file server.json` | The same with a kit's server: the page has **Join** only, the relay runs on the server, and the run folder is sent there when the game is over |
| `lobby [--kit DIR] [--master URL \| --no-master] [--request TEXT]` | Netplay (every kit without a server; docs/NETPLAY.md): Host a game, Join with a code or an address, the public and LAN lists; a release kit needs no `--overlay`/`--snapshot` |
| `host --players N [--name NAME] [--public]` | `lobby` with Host already chosen: the game at the master server (else on the LAN), its code on the clipboard |
| `join --code CODE \| --address HOST[:PORT] \| --game-id ID` | `lobby` with that join already chosen (a join code, a long invite code, an address of the LAN, a VLAN or a VPN, a game of the public list) |
| `games [--lan] [--json]` | The master server's public games (and with `--lan` the games announced on the LAN); no X4 |
| `serve --overlay DIR --server-file server.json [--bind 0.0.0.0] [--runs DIR]` | The server of such a kit (Linux or Windows, Python only): admits the launchers that prove the kit's key, starts the relay when the overlay's player count has joined, stores the players' logs, then takes the next players |
| `join --server-file server.json --overlay DIR --slot K \| --start GAMESTART` | Join the game that runs on a kit's server in an open slot (a drop-in game: `serve` with an overlay that has open slots); the server admits this PC and hands out the saver's join files |
| `session ... --checkpoint-at T` | Every client saves the world right after tick T (`clientK/save_001.xml.gz`); the session ends there (outcome `checkpointed`) |
| `play ... [--keep-saves 3]` | With an overlay that saves periodically (`tools/build_play_overlay.py --save-every MINUTES`, default 10): every client's saves are collected in `RUN/saves/c<K>-t<TICK>.xml.gz` (see Periodic saves below) |
| `resume --save RUN/saves/c1-t<TICK>.xml.gz --spec SESSION/spec.json --overlay PLAY_OVERLAY --gate-dll DLL --out NEW [--minutes M] [--display N] [--no-focus]` | A new session from the world of a periodic save: prepares the save (one client), registers it and builds its play overlay in `NEW`; then `play --overlay NEW/overlay --snapshot NEW/session-save.xml.gz` |
| `continue --save SAVE --gate-dll DLL --out NEW [--overlay FIRST] [--minutes M]` | A new session from any save of a session, every player as saved (see "Milestones" below): a periodic save `RUN/saves/c<K>-t<T>.xml.gz`, a person's save `RUN/saves/player/<name>-t<T>.xml.gz` or a milestone save `RUN/saves/milestones/<story>-M<n>-slot<k>.xml.gz` (`--milestone` is the same), with its `.json` beside it; no X4 runs, the save is used as it is; then `play --overlay NEW/overlay --snapshot NEW/session-save.xml.gz`. `resume --continue --save FILE` is the same |
| `continue --save SAVE --session SESSION.json --overlay FIRST ...` | The same for a save without a sidecar (a periodic save of a build before 25 Nov): the session.json and a folder with the play recipe stand in for it; slots 2..N from their saved player objects, the stories claimed again by their scripts' names |
| `host --milestone SAVE [--name NAME] [--public]` | Netplay: host the game continued from that save (any kind with its sidecar; `continue` makes the session once, kept in the kit's `sessions/continue-r-<save>-<gate>`); joiners download it as any hosted session. A save without a sidecar: `--session SESSION.json --overlay-recipe DIR` as for `continue`. Every player's ship starts at rest; `--keep-flight` keeps the saved travel, speed and autopilot (also for `continue`) |
| `triage RUN [--steam-fix]` | Reruns a diverged session with diagnostic windows over the 300 ticks before its first mismatch and writes `RUN-triage.txt`; `--report-only RERUN` only reports |
| `clean` | Stops processes and removes views left behind by a crashed session |
| `steam-fix [--restore]` | See "Steam" below |

## Lobby

`x4ls lobby` is `play --remote-slots` and `join` without the command lines: who plays and when the snapshot loads
is chosen in X4's main menu.

- Every PC runs the same command with the same play overlay (one without open slots) and snapshot. X4 starts in
  its start menu; the overlay is installed, but only `tools/dev/native_gate_lobby.lua` runs. It adds **Multiplayer**
  above Settings.
- **Host a game:** the launcher makes a token, listens on TCP `port + 1` and puts the invite code
  `X4MP-<address>-<port>-<token>` on the clipboard (the page shows it too). `--bind` names the address the other PCs
  reach; the default is this PC's primary IPv4 address.
- **Join a game:** the launcher takes the invite code from the clipboard, connects to the host and proves the token
  and that it holds the same overlay and snapshot. The host refuses another kit, a wrong code and a full game, and
  the page says why.
- When every slot is taken the host starts the relay for the joiners' addresses, picks the load second
  (`--load-wait`, 30 s) and tells every launcher. Each one writes the session into its `config.lua` and its game runs
  the overlay's files. From here on it is the session `play` and `join` make: the same relay, gate and overlay.
- The session's end closes X4 (there is no way back into the lobby yet). A second X4 on the same PC takes `--lane 2`.
- X4's UI Lua has no `io` library and refuses the Win32 calls of LuaJIT's `ffi` ("Access to FFI function ... is
  restricted"), and `require` does not find extension files. `loadfile` works and reads a changed file again. So the
  game sends its choice as a log row (`[X4LAN] LOBBY REQUEST HOST`), which the launcher follows, and reads the
  launcher's answer from `extensions/x4_lan_lab/lobby/state.lua`. The launcher reads and writes the clipboard.
- X4 has to be started by this command (`PLAY.cmd` in a playtest kit). A start from Steam has no overlay and no
  launcher, so no Multiplayer entry.
- **Netplay** (`src/lockstep/launcher/netlobby.py`, docs/NETPLAY.md "Launcher notes"): a kit without a server gets the
  netplay page. Hosting uses the kit's own session or one `x4ls new-session` makes once (X4 closes for its capture
  client and comes back with the session); the game goes to the master server (its ports mapped, the relay on this PC
  when the master reaches it, else the master's), or to the LAN lobby without one. Joining downloads the host's
  session, checks it against this release and builds the overlay here with this kit's own gate DLL; X4 starts again
  with it before the player counts as ready.

## One X4 for one player (`play --solo K`)

`x4ls play --solo 2 --overlay SESSION/overlay --snapshot SESSION/session-save.xml.gz` starts the relay and client 2
only. Use it for what one player sees and hears: player 2's sound, call video, HUD, gun gimbals, the stand-up. The
overlay and snapshot are the session's own (`x4ls new-session` or `tools/build_play_overlay.py`); nothing is rebuilt.
`--clients` defaults to the overlay's player count. K may be any slot, 1 included.

- **What the relay does.** It runs with `--stand-in` for every other slot (`DropBarrier` in
  `tools/dev/native_gate_relay.py`). From tick 0 each of those slots gets a stand-in row: client K's game time, real
  time and digest, and an empty input blob. From the first permit that carries proposals (tick k+1, proposal tick 0)
  every permit names those slots in its drop mask, `k/<mask>`. This is the protocol 4 drop. In live sessions it is
  used for a player whose X4 stopped. Here it starts at proposal tick 0, before anyone has played.
- **What client K's gate does.** It treats the drop as it treats any drop (`lockstep_lookahead.h`,
  `lockstep_pilot_inputs.h` `pilot_drop_note`). Each stood-in slot counts as dropped from proposal tick 0. From tick
  `pilot_delay` on it needs no record, which is long before the pilot window starts at 40. The seat stays and is
  neutral: no input, no view, no trigger, its hits native, its ship still watched for death. Slot 1 is X4's own player
  (MD's player). It sits idle in its ship, and the stock path replays nothing for it. Slots 2..N other than K are
  built and seated at the take as in every session, and then idle.
- **No gate or probe change.** The slot is per client (`@X4LANE_SLOT@`). The overlay does not say who else plays.
  Client K's pilot window, presentation scope (`lockstep_pilot_present.h`, client local) and per-player missions are
  the same as in an N-client session. The other slots' players exist in the world, so other players' ships and figures
  can be seen too.
- **Why not the hot join's absent slots.** An absent slot's player is built and seated at its handover. Slot 1's
  player is the save's own, so an absent slot 1 is refused by the overlay builder (`--join-absent`: 2..8), the probe
  (`native_gate_join.absent`) and the gate (`pilot_absent_arm2` refuses bit 0). The drop path covers slot 1. It was run
  live mid-session on 4 Nov (d3drop1, protocol 5), not yet from tick 0.
- **Refused** (before anything starts):
  - A protocol 3 or 5 overlay: the drop mask is protocol 4's, and the defaults' lookahead 1 is protocol 4.
  - An overlay with open slots (`--open`): its absent slots belong to the hot join's relay, which never stands in for
    slot 1.
  - `--remote-slots`.
  - `--clients` other than the overlay's players.
  - A slot outside 1..N.
- **What it cannot show:**
  - Desyncs. The stand-ins copy client K's digest, so the relay compares K with itself.
  - Anything that needs another person: a trade between players, player 1 hailing player 2, two players at one
    console.
  - Client 1's own fixes (`pres_plan_canon` runs on client 1 only).
- **Look for** these rows in `client<K>/x4ls-c<K>.log` (here for `--solo 2` of 3). At the end:
  `LOOKAHEAD_STOP ... dropped=05`. In the `PILOT_INPUTS` and `PILOT_INPUTS_STOP` rows:
  `dropped=5 drop_skips=<n> drop1=0 drop3=0`. Both mean the gate took slots 1 and 3 as dropped at proposal tick 0.
  `relay.log` starts with `NATIVE_GATE RELAY STAND_IN slots=1,3 from=0 played=2`. `summary.json` has `solo` and
  `stand_in`.

## How a client is isolated

- **Game view:** `<game drive>:\x4ls-lanes\cK` holds hard links to the
  installation's top-level files and junctions to its folders, so nothing is
  copied. `extensions\` is a real folder with junctions to the official DLCs and
  this client's own `x4_lan_lab`. Third-party mods in the installation are not
  linked. The probe finds the gate DLL relative to the working directory, so it
  has to run from the view. Removing a view unlinks; it never enters a junction.
  Links are found by their reparse attribute (`game.is_link_or_reparse`), not by
  `Path.is_junction`, which Python 3.10 and 3.11 lack. Before 24 Nov 2026 a view
  removed under those versions deleted the game's files through its junctions.
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
relay-failed, or timeout; `played` for `x4ls play`). `x4ls play` adds `errors.log`
(every row a test session fails on, with its client), `desyncs.log` (the relay's
DESYNC, RESYNC, CLOCK_SPLIT, STALL and DROP notices) and `clientK/x4lan-crash.txt`
after a crash. Every session client also keeps the census record of its last eight
world captures, `clientK/x4lan-census-<k>.txt` (`native_gate_world_record`): after a
world split, `python tools/dev/census_record_diff.py RUN/client1 RUN/client2 --x4 X4.exe`
names the objects that differ at the first differing capture and in which domains.

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
- **Periodic saves.** A play overlay sets `native_gate_save_every` (`build_play_overlay.py --save-every MINUTES`,
  default 10, `0` for none): every client's gate saves the world after each step T that is a multiple of the
  interval, with the join save's routine at the frame boundary (`lockstep_periodic_save.h`), into
  `<profile>/save/mpsave/s<T>.xml.gz`, and the session runs on. Each client stands still while it writes (2.8-3.0 s
  for a 30 MB save, two clients on one PC, 16 Nov); all clients save at the same step, so nobody waits for
  long. `x4ls play` moves its clients' saves to `RUN/saves`, checks each against the SHA-256 the gate announced,
  lists them in `RUN/saves/saves.jsonl` and keeps the newest `--keep-saves` ticks (3); once the relay names a
  first desync tick, the saves before it stay for good and of the later ones only the first. A client keeps
  `--save-keep` (3) files in its own profile (a remote PC's are not collected). X4 refuses a save while a call or
  cutscene of the simulation runs (`is_saving_possible`, seen 30 s into a cadet start on both clients alike): the
  save is tried again 300 ticks later, five times. A save has effects on the world that made it; a save only some
  clients made is therefore named (`PERIODIC SAVE UNEVEN` on the console, `PERIODIC_SAVE_FAILED` rows in
  `errors.log`). Off by default with `--mixed-platform` (the Linux gate has no such save; its fork save
  `native_gate_fork_save` does not stop the client but is Linux only) and with `--open` (not run with a join yet).
  Live, two clients, a save every 30 s: 16 saves, 15,880 ticks, no desync; `tools/dev/checkpoint_diff.py` on the
  two clients' saves of one tick shows view-local differences only (animation times near each player, listeners).
- **Resume from a periodic save.** `x4ls resume` (`src/lockstep/launcher/resume.py`) makes a new session whose world
  is such a save, so a split an hour into a session can be looked for from minutes before it. A periodic save is
  not a session start as it is: (1) it holds a player object per slot and X4 without the gate refuses it
  (`UniverseClass::Import(): Invalid number of players`), so the other slots' player objects are taken out of a
  copy; (2) the twelve fixture ships have fought by then, so one client loads the copy, the MD cue `Respawn` puts
  fresh ones beside the player's ship, and the per-player hulls are created again
  (`native_gate_snapshot_from_save`); (3) the result is registered with the first session's spec, without its
  stories, and the play overlay is built from the first overlay's recipe. It is the saved world on every client,
  not the session continued: the gate's own state is not in a plain save, slots 2..N fly new hulls from their
  start places with their kits applied again, and the prepare step runs the world a few seconds without the
  gate. Live, 16 Nov: a save of minute 8.5 of a two-player session, prepared in about three minutes, then a
  two-client session from it: 9,500 ticks in sync, no error row, its own periodic saves written (then stopped by hand).
- **Milestones.** A play session from a session with a story (`x4ls new-session`, `--session`) saves the world at every
  completed mission chapter of it, so a playthrough goes on from its last chapter instead of from its start (user
  request, 24 Nov). The catalogue is `tools/dev/native_gate_milestones.py` (`list`, `check --gamedata`): per story its
  chapters in order, each the stock chapter cue that its own clean-up cancels (where X4 also completes the chapter's
  mission); the Terran cadet start's story is `terran_cadet`, M1 and M2 the cadet's own two chapters (`GS_Terran1`
  `Ch1_Maintenance`: Ch2_Distress begins; `Ch2_Distress`: the cadet mission complete), M3..M25 `Story_Yaki` Ch1_Escort
  .. Ch23_Yaki_Trade (`check` reads every one in the game data: 25 milestones, 0 wrong on 9.00). A player 2..N plays it
  in its own copies (`GS_Terran1_P<k>`, `Story_Yaki_P<k>`) or a stock script it claims; player 1 in the stock scripts.
  - **In the game.** The overlay's `md/x4lan_milestones.xml` has a root cue per (slot, milestone): `event_cue_cancelled`
    of that slot's chapter cue. Its frame runs at the same tick on every client; it records the milestone in
    `md.$X4LAN_Milestones` and raises `X4LAN_Milestones.Reached<i>` with `player.age`. The probe (native_gate_milestones)
    turns that game time into one save tick T on every client (the first tick 5 s later, counted from the client's
    first tick and its game time, plus 60 ticks) and saves through the periodic save's gate routine
    (`lockstep_periodic_save.h`, no C change). At T-25 a player on foot postpones T by 300 ticks (player 1 until it sits:
    a session starts it in its ship; players 2..N at most 20 times); then one `SAVE_SLOT` row per slot (player and
    ship IDs, on foot, and for 2..N their inventory and research), then the gate's mission owner table into the MD (see
    below). X4's refusals (a call of the simulation) retry 300 ticks on, 90 times. A periodic save within 90 ticks of a
    milestone save is left out. Not with `--mixed-platform` or `--open` (`--milestone-saves off`: logged only), as the
    periodic saves; on otherwise, also with `--save-every 0`.
  - **Collected** by `x4ls play` (`saves.py`): `RUN/saves/milestones/<story>-M<n>-slot<k>.xml.gz` (the lowest local
    client's file; the others are hashed and deleted) and `<name>.json`: the milestone and its tick, every local
    client's SHA-256, the slots' rows, the session.json, spec.json and play recipe of the session. Never pruned. A
    remote PC's clients are not seen.
  - **Mission owners in the save.** The gate's owner table (lockstep_mission_owners.h) is keyed by cue runtime IDs,
    which a load gives anew (a save writes a cue's ordinal). So at T-25 the probe signals `x4lan_owners_reset` and one
    `x4lan_owner_<k>` per claimed cue on the galaxy (X4's own SignalObject, the cue as its Lua ID); the MD keeps them as
    cue values in `md.$X4LAN_Owners` / `$X4LAN_OwnerSlots`, which X4 saves and loads like any script's cue reference.
    Chosen over an MD table updated at each claim: the gate claims in C (a story's first frame, every root instance a
    slot's frame makes), where no MD runs, and only a save needs the table. **Open:** X4 hands a cue to Lua as nil and
    formats one as its bare name (live continue of a periodic save, 24 Nov: `first=2:string:Avatar:nil:0,...`), so the
    probe cannot name a saved cue to the gate. Those entries are counted (`SAVE_OWNERS restored ... unreadable=N`) and
    stay nobody's: a mission a player had taken from a shared script (a generic mission) is in every player's list
    after a continue. The stories do not depend on it (below).
  - **The stories are their slots' by name.** A continued session's overlay names each script of slot k's own (its plot
    copies `GS_Terran1_P<k>`/`Story_Yaki_P<k>`, a stock script it claimed) for slot k before the first tick
    (`native_gate_plot_owned = {{'<Script>',k},...}`, `gate.mpp_first(script, k)`, lockstep_mission_owners.h
    `mpp_first_add_slot`): every cue of it is slot k's, as the claim of its root cue at the story's begin made them.
    The script's name is the same in every save, so this needs no snapshot. Row `PLOT_OWNED_ARMED slot=<own>
    scripts=GS_Terran1_P2=2,Story_Yaki_P2=2 first=N/... owned=N`. A gate without this (`owned=` missing from
    `gate.mpp_first()`) is refused at load: an older one would make the copies player 1's.
  - **Continue** (`x4ls continue`, `src/lockstep/launcher/milestone.py`). X4 loads a save with one player object only,
    so the copy keeps X4's own (player 1, everything as saved) and takes the others out, reading their inventory,
    blueprints and research first. session.json: the first session's with this save, no fixture, player 1's ship from
    the save, slot k's ship from its `SAVE_SLOT` row (else the ship its saved player object sat in), no kits
    (money, standing and the shared library are in the save) and a `continue` block (their inventory; in factions
    mode their research and blueprints). The overlay is the first session's recipe; the story files and copies are
    there as before, but no story begins again (no `native_gate_story`). The probe (`native_gate_continue`): at tick 2
    the MD dumps its saved owner table (`x4lan_owners_dump`; the story roots' state as debug rows), at tick 32 every
    client claims them (`gate.mpp_claim`), at the window's tick 8 slots 2..N get their inventory back, at tick 10
    (factions) their research and blueprints. Rows: `SAVE_OWNERS restored ... heard=N of=N claimed=N refused=0`,
    `CONTINUE_INVENTORY ... applied=1`.
  - **Every save of a session can be continued** (25 Nov). The periodic save is prepared the same way when it arms
    (`SAVE_SLOT name=periodic-t<T>` rows, the owner snapshot), and `x4ls play` writes `RUN/saves/c<K>-t<T>.json` beside
    each kept file (pruned with it). A save of an older build has no sidecar and no snapshot: `--session` and `--overlay`
    stand in; slot k's ship and inventory come from its saved player object (one per slot 2..N that sat in a ship), and
    the stories are their slots' by name (above); missions of other scripts a player had taken stay nobody's.
  - **What the copy leaves out besides players 2..N** (found in the first live continue, rk8, 24 Nov): the identity
    setup's MD state (`X4LAN_PlayerIdentity`: its step counter was the first session's, the continued session's step 1 was
    refused and both probes stopped at tick 20), and the player movement (`<movement ... class="player">`, X4's
    PlayerController) of every ship but player 1's: the gate makes slot k's controller at the take and refuses a ship that
    has one (stop 161 `ship_controlled` at tick 35). Those ships start at rest at their saved place.
  - **Every player's ship starts at rest** (24 Nov, default; `continue --keep-flight` and `host --milestone ...
    --keep-flight` keep the saved flight). Found in release rk9, continued from rk8's c3-t18000: player 2's ship was saved
    in travel mode at full speed on the way to a sector border, the continued game went on with it at once, and the ship
    flew far into the void before the player saw it. In the copy, for the ship every player object sits in and the ship
    of every slot's row (player 1's too): the movement's velocity and acceleration are emptied, and in the player
    movement the autopilot's path (`<targetpoints>`, the ship computer flying to a target). The engine's
    `<travel active="1" .../>` and the travel drive's two effect objects (exhaust, bubble) are left out, `<boost start=.../>`
    becomes `<boost/>` and `<sound travel="1"/>` goes. X4's own player's `<autopilot target=.../>` loses the target. Throttle is
    not saved (X4 takes it from the speed). Orders in a ship's queue stay (X4's AI scripts refer to them by ID): a player's
    ship runs them only on the autopilot, which is off. It is the same file on every client, so the same world at the load.
    The console says `flight: N player ship(s) at rest (...)`, and `continued-from.json` keeps the counts (`flight`). A kit
    keeps a hosted session per form (`sessions/continue-r-...` at rest, `continue-f-...` flying), so one made before this
    is not taken again.
  - **A person's save** (25 Nov, on in every session overlay). The menu's Save game (`SaveGame`, held by the Lua guard) and
    the quick save key (refused by the gate's input layer) become a save of the session by every client: the asking
    client raises the UI event `X4LAN_Saves`/`Request` {slot, file, name} as a click (the UI replay's events family runs
    it on every client at its apply tick; a call outside a click is refused, never run on one client alone); the MD
    (`SaveRequest`) answers with `X4LAN_Saves.Request` and `X4LAN_Saves.RequestTime` (player.age), and the probe makes
    the save as a milestone's (2 s after that time plus 60 ticks; waits while player 1 walks; one person's save at a
    time). Rows `PLAYER_SAVE asked / reached / armed / saved / postponed / dropped`, `ERROR PLAYER_SAVE_FAILED`; the asking
    player sees "saving the game for every player..." and "game saved (<name>)" on their own screen only. Collected as
    `RUN/saves/player/<name>-t<T>.xml.gz` and `.json` (`asked_by`, `save_name`, `save_file`). The save menu key opens the
    menu's save page. Load stays off in a session: `LoadGame` is held by the Lua guard too (play overlays since 25 Nov)
    and the load keys are refused, each said once ("loading a saved game is off during a session"). With `save=false`
    (Linux clients, open slots) a person who asks is told that saving is off in this game.
  - **What is not kept** (the gate's own state): players 2..N's player objects (new ones at the take, in their saved
    ships; a player on foot sits again; their encyclopedia, map knowledge and blackboard values of their own are not
    carried, the Terran stories keep none on the player), the active mission each player 2..N chose, owned frames
    before tick 32 (they run as no one's), factions' relations between the players' factions (set again), and every
    per-tick gate state (a new session from tick 0). Refused: a save whose player 1 walks or flies the suit, a ship of
    another faction, two players in one ship, a sidecar whose hash is not the save's, sessions with open slots.
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
