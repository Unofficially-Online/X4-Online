# Playtest: people in one lockstep session (`x4ls play`)

`x4ls play` runs a lockstep session for people rather than for tests. A desync is logged and the session runs on.
Nothing a client logs ends the session for the others: errors and actions the session does not share go to
`errors.log`, not to a teardown. A client that stops or crashes is dropped, and the others play on.

## A test game

- **Where:** two X4 windows on one PC, side by side. `x4ls` runs every client locally (see "Not yet" for two PCs).
- **World:** the owned snapshot 005, Argon Prime.
  - Player 1 (window 1) flies the snapshot's Argon scout.
  - Player 2 (window 2) flies a second Argon scout, which the session moves near player 1 at tick 20.
  - The scripted fixture fight (6 Argon against 6 Xenon) starts nearby at tick 10.
- **Every window is player 1:** each client shows its own ship with X4's own cockpit, HUD, camera, menus and map.
  - Controls are X4's own, with the default key map of a fresh profile.
  - A control input reaches every client 3 steps later (100 ms). Your own ship answers at once on your screen.
- **Rate:** 30 simulation steps per second, drawn at 60 frames per second.
- **Length:** the pilot window, 40 minutes by default (`--minutes`). Then the controls go back to X4 and each window
  plays on alone.

## Build once

```
python tools/dev/build_native_gate.py --zig <zig 0.15.2> --test --native-image <X4.exe>      # the gate DLL
python tools/build_play_overlay.py --snapshot <snapshot 005 save_009.xml.gz> \
    --gate-dll .local/native-lockstep/build/x4lan_gate_64.dll --out .local/overlays/play-1 [--minutes 40] [--mode coop|factions]
```

- **coop:** both ships belong to `player`: one faction, one wallet.
- **factions:** each player gets their own pool faction and account.

The builder also blocks `Pause`, `Unpause` and `SaveGame` (see "During the game").

## Start

```
python tools/x4ls.py doctor                      # X4 9.00 found, Steam pid registered (else add --steam-fix below)
python tools/x4ls.py play --overlay .local/overlays/play-1 --snapshot <the same save_009.xml.gz>
```

1. Both windows open one after the other, reach the start menu and load the snapshot at an agreed second
   (2 to 3 minutes).
2. The session starts at tick 0. Controls come at tick 40.
3. The console prints progress once a minute: the tick, game minutes and speed against real time, and "in sync" or
   "desync since tick N".

## During the game

**Shared** (every client runs the action at the same step, so the worlds stay the same):

- flying, guns and missiles, hits, death, the spacesuit and respawn
- docking and trading
- orders from the map
- missions and conversations
- station construction
- faction and player settings
- walking on foot

**Refused:** any other menu action that would change the world on one client only.

- X4 shows an info line ("... refused" or "... is off during a lockstep session") and nothing happens.
- Each refusal is one row in `errors.log`.
- Pausing and saving are refused this way. The options menu and the ship configuration menu then open without
  pausing.
- Why they are refused: X4's `Pause()` stops the gate on that client at once. In smoke play-sA (8 Nov), client 2
  paused at tick 900 and stopped with reason 3; the relay dropped it after 15 s and client 1 played on. A save
  pauses the same way. In smoke play-sB, with the refusal, the same `Pause()` left the game running and the session
  in sync.

**The Multiplayer session page:** Escape, then "Multiplayer session" (above Settings).

- It shows which player you are, the step and game time, whether you are still in the session, and the relay.
- Leave takes you out cleanly: the others play on after 15 s, and your game runs on alone.
- Live check 8 Nov (smoke play-sF): the page opened and drew in X4, with no Lua errors and the session in sync.

**Other players' ships** are drawn between the last two steps, about 33 ms behind (`render_motion`
'interpolate'). The default 'extrapolate' draws them ahead, and the map then shows their icons flicking back and
forth.

**Avoid:**

- SETA (time acceleration) and the Pause key. Both are untested under the gate.

## When something goes wrong

| What happens | In X4 | The session | Where it is logged |
|---|---|---|---|
| The worlds differ (desync) | nothing | runs on | `desyncs.log`: `DESYNC tick=N`, `RESYNC` when equal again |
| A client's clock or step splits | nothing | runs on | `desyncs.log`: `CLOCK_SPLIT` |
| An action the session does not share | an info line | runs on | `errors.log` |
| A probe error or a gate stop on one client | "the session stopped - ..." on that client, which then plays on alone | the others wait up to 15 s (`--stall-drop`), then the relay drops it and they go on; its ship stops answering | `errors.log` (`ERROR`, `STOP reason=N`), `desyncs.log` (`STALL`, `DROP`) |
| An X4 window is closed or X4 crashes | - | as above | `errors.log` (`X4 exited`), `clientK/x4lan-crash.txt` after a crash |

Nothing in this table ends the session for the clients that are still in it. In smoke play-sB (8 Nov), client 2's
X4 was killed at tick 2017; the relay reported the stall at 2031 and dropped it 15 s later, and client 1 played on to
the end of the window in sync.

## Start from the main menu (built 9 Nov; run with two X4 on one PC, not yet between two PCs)

Every PC runs the same command with the same overlay (one without open slots) and snapshot; a playtest kit
(`tools/make_playtest_kit.py`) has it as `PLAY.cmd`:

```
python tools/x4ls.py lobby --overlay .local/overlays/play-1 --snapshot <save> [--port 47000] [--bind <address>]
```

X4 opens in its main menu with a **Multiplayer** entry above Settings.

1. One player chooses **Host a game**. The page shows the invite code and the launcher puts it on the clipboard
   (this replaces what the clipboard held). Send it to the others.
2. Each other player copies the code and chooses **Join a game (invite code from the clipboard)**. The page says
   who has joined, or why the host refused (another kit, a wrong code, a full game).
3. When every slot is taken, every game loads the snapshot at the same second, 30 s later. From here on it is the
   session of "Start" and "Two PCs": the same relay, gate and overlay, the same logs in `.local/runs/<time>-lobbyN/`.

- The others reach the host on UDP 47000 (the session) and TCP 47001 (the lobby). `--bind` names the host's
  address the others reach (default: the PC's primary IPv4 address; a VPN address over the internet).
- The session's end closes X4. Cancel on the page takes back a Host or a Join before the start.
- X4 has to be started by this command. A start from Steam has no overlay and no launcher behind it.
- Two windows on one PC: `--bind 127.0.0.1` for the host and `--lane 2` for the other.
- lobby1 (9 Nov): two X4 on one PC, hosted and joined through the page's own requests (`--auto`, no mouse),
  3642 ticks, 0 desyncs, 0 errors. Not run yet: a mouse click on the buttons, three players, two PCs.

## A server everyone joins, and an installer (built 9 Nov)

With a server nobody hosts and there is no invite code: a player installs, starts, and chooses **Multiplayer**,
**Join**. The server needs Python and the kit's files, no X4.

```
python tools/make_playtest_kit.py --overlay .local/overlays/play-1 --snapshot <save> --out <kit>     --server <the server's IPv4 address> --python-embed <python-3.12.x-embed-amd64.zip from python.org>
python tools/deploy_playtest_server.py --kit <kit> --ssh root@<the server>
python tools/make_playtest_msi.py --kit <kit> --out <file.msi>
```

- **The kit** gets `server.json` (the address, the port and a new key) and a `PLAY.cmd` that starts
  `x4ls lobby --server-file server.json`. With `--python-embed` it also holds its own Python, so a player's PC
  needs X4 only. The address and the key are in the kit, not in this repository; keep the kit among the players.
- **The server** (`x4ls serve`, docs/LAUNCHER.md) admits the launchers that prove the key and hold the same kit.
  When as many players have joined as the overlay has slots, it starts the relay for their addresses and tells
  every game the load second. One game at a time: a joiner is told when a game is running. It listens on UDP
  47000 (the session) and TCP 47001 (the lobby and the logs).
  `deploy_playtest_server.py` copies the kit's checkout, overlay and `server.json` over ssh and sets up a systemd
  unit (`x4mp-lobby`) and the two ufw rules. Every new kit has a new key and is deployed again.
- **The logs** of every player go to the server by themselves when their game is over: the launcher's run folder
  (summary.json, errors.log, the game's log; no saves) as `runs/<time>-serve/players/<time>-playerK-<address>.zip`,
  next to the game's relay.log, native-barrier.json and desync-status.json. A launcher that never got into a game
  sends its folder to `runs/uploads/`. KIT.txt tells the players so.
- **The installer** (`make_playtest_msi.py`, Windows with Python 3.12: it uses `msilib`) installs the kit for the
  current user into `%LOCALAPPDATA%\X4 Multiplayer Playtest` with Desktop and Start menu shortcuts to `PLAY.cmd`.
  No administrator rights, a progress bar and no pages; a newer installer replaces an older one. It is not signed,
  so Windows may warn about an unknown publisher.
- The player count is the overlay's (`build_play_overlay.py --players N`): the game starts when N have joined.

### Play in Steam, then Multiplayer (built 9 Nov)

Installed from such an installer, a player starts nothing of ours: **Play in Steam**, and X4's main menu has
**Multiplayer**. Choosing **Join** there closes X4 and brings it back by itself in its multiplayer profile, joined.

- The kit holds `steam-menu/` (the lobby page as a small X4 extension, `x4mp_playtest_menu`) and `helper.pyw`
  (`lockstep/launcher/steamplay.py`). The installer starts the helper without a window and registers it to start
  with Windows (HKCU `...\Run`); the Start menu entry starts it again when it was ended.
- The helper copies the menu extension into X4's own `extensions` folder. No Egosoft file is touched, and nothing of
  the player's profile, saves or settings is read or written. While it is there X4 counts as **modified**, as with
  any extension ("Modified game detected", and X4 marks saves made meanwhile as modified). Removing the installer
  stops the helper and takes the extension out again.
- A Steam-started X4 runs the player's own profile, with X4's protected UI mode on and no log file. A session
  cannot run there. The page reaches the helper through named pipes (`\\.\pipe\x4mp-playtest-state`, `-join`),
  which X4's `loadfile` opens even in protected UI mode: the helper answers every open with the state, and which
  pipe was opened is the request. On Join the helper answers "restart", the page calls `QuitGame`, and the helper
  runs `x4ls lobby --server-file ... --auto join --show`: the disposable profile, the kit's overlay and snapshot.
- Without the helper the page says so and what to start. A session that cannot start is shown in a message box;
  `repo\.local\helper.log` and `helper-lobby.log` in the installed folder have the details.
- The helper shows nothing when it starts. The installer starts it through Explorer and its Start menu entry:
  what Windows Installer starts itself inherits its redirection guard, and an X4 started from there cannot follow
  the folder links of its game view (File I/O Win32 error 448, then "Failed to initialize the start menu").
- vps2 (9 Nov): an X4 with protected UI mode on and the menu extension as its only extension (a disposable profile
  standing in for the player's own) asked the installed helper to join; X4 quit by itself, came back in the
  multiplayer profile joined as player 1, a second X4 joined, and the 2-minute session ran through the server:
  3642 ticks, 0 mismatches, both players' logs stored on the server. The page's Join was chosen by a test switch
  (`lobby.auto`). Not run: a start from Steam itself in a player's own profile (AGENTS.md keeps our tests out of
  it), and a mouse click on the page's buttons.

## Two PCs (built 8 Nov; run on one PC through its LAN address, not yet between two PCs)

On the host PC (it runs the relay and slot 1), with UDP 47000 forwarded to it if the other PC is outside the LAN:

```
python tools/x4ls.py play --overlay .local/overlays/play-1 --snapshot <save>     --remote-slots 2 --bind <this PC's LAN address> --allow-from <the other PC's address> --port 47000     [--public-host <the router's public address>]
```

1. The host prints one `x4ls join ...` line for slot 2.
2. On the other PC, from a checkout of the same commit, with copies of the same overlay folder and snapshot, run
   that line within `--join-wait` seconds (default 240):

   ```
   python tools/x4ls.py join --relay <host>:47000 --token <t> --slot 2 --clients 2 --arm-at <utc>        --overlay <copy of play-1> --snapshot <copy of the save>
   ```

Smoke play-sR (8 Nov): the host relay on this PC's LAN address (port 47000) and the joiner on the same PC (`x4ls join ... --same-pc`, a
test switch) played to the end in sync, with 0 errors and 0 drops. A VPN address (Tailscale, Hamachi, ZeroTier) works as
`--bind` and `--allow-from` too, with no port forwarding. Both PCs load at the agreed UTC second, so their clocks must agree
within a few seconds. With a Linux PC in the session, build the host's overlay and every PC's overlay or bundle with
`--mixed-platform` (Option B's strict float recomputes): without it Windows and Linux part at the first steering
update. A Linux battle bundle (`tools/dev/linux_gate_live.py bundle`) also needs the Windows defaults its gate takes only
when named: `--ik-wrap --ik-step --body1-mass fallback --nav-rng --nav-initial-refresh --attention-rng
--attention-initial-refresh --shape-rng --shape-initial-refresh --shape-refresh view --gravidar --radar-rng
--radar-initial-refresh --steer-roll --avoid-angle --pursuit-speed` (9 Nov: equal to Windows through tick 132; a few ulps apart at tick 133,
floating-point order in the steering behaviours, still being chased). Each PC writes its own
`errors.log`; the host's run folder also holds the relay's logs.

## Joining a running game (built 8 Nov; in lockstep through x4ls on one PC, 9 Nov)

A slot can stay empty at the start and be taken later by a player on any PC the relay allows. Only the joiner
loads; the others play on.

1. Build the overlay with the open slots: `python tools/build_play_overlay.py ... --players 3 --open 3`.
2. Host: `python tools/x4ls.py play --clients 3 --overlay <overlay> --snapshot <save> --open 3`. For another PC,
   add `--bind`, `--allow-from` and `--port` as in "Two PCs". The host prints the join line for slot 3.
3. Joiner, at any time during the session, with a copy of the host's overlay folder:

   ```
   python tools/x4ls.py join --relay <host>:<port> --token <t> --slot 3 --overlay <copy of the overlay>
   ```

   Without `--arm-at` this is a hot join. The joiner asks the relay for the slot, and the host's clients save
   the world at the next join tick (a short pause for everyone). The joiner then downloads the join save and
   package from the host (TCP on the relay's port number; checked against the hashes the relay announced),
   builds its overlay from the host's recipe (`play-recipe.json`), and starts X4. It loads at once, catches up
   from the session's rows, and takes over its ship at the handover tick.

The joiner has 900 s from the save to report in (the relay's `--join-joiner-timeout`). The host serves the files
of its own clients only, so slot 1's PC must be one of the savers. With `--open` the relay hands over after 20 s
of catch-up and may pause everyone for up to 180 s while the joiner closes the gap.

Status (9 Nov, one PC). The whole join runs: the request, the save at the next join tick, the 176 MB download
(verified), the joiner's overlay and its checks, the load, the resume into the open pilot window, the catch-up,
the handover and the joined player to the end of the window. hj27 (x4mods-28) and hj28 (the plain playtest flow
above): the joiner equal to the session through the catch-up and the handover, then about 2,000 ticks of play after
it with 0 desyncs, 0 errors and 0 drops. On one PC the joiner is no faster than the session, so the handover pauses
everyone for about as long as the joiner took to load (hj28: 75 s); a faster joiner PC pauses less.

Missions across a join (hj32, 9 Nov night): a mission accepted before the join keeps the session's ID on the joiner
(`native_gate_join_cue_ids`, a join default: the join package's CUEI section gives every saved cue its live ID), and a
player's accept after the handover applies on every PC with equal mission owners; played to tick 5440, 0 desyncs, 0
errors. The package has one more section for it, so the host and the joiner need the same build of the kit. A UI
element made after the load (part animations) takes client-local IDs (`native_gate_runtime_ids_ui_elements`, a session
default; plain2: 1841 ticks of two-client play, 0 desyncs).

Still open: a join whose save tick falls while a fixture gun is engaging (hj24: T between 668 and 684 in the battle
fixture) can part within a few ticks; a sweep of join ticks is next.
A join is never refused for it: as for a play session's own desyncs, the relay hands over all the same
(`--join-observe`), counts the mismatches (the relay's `joined ... digest_mismatches=N` line) and the session's
desyncs go to `desyncs.log`.

## Choosing the starts (built 8 Nov; run through x4ls on one PC)

`python tools/x4ls.py starts` lists the game's starts from this PC's own game files, read-only and unpacked into
`.local/x4ls/gamedata`. For each it says whether it can be the session's start (slot 1, `session-base`) and
whether it can be a player's own start (slots 2..N, `player-start`), with the reasons when not.

```
python tools/x4ls.py new-session --base-start custom_creative --players 3 --mode coop --start 2=x4ep1_gamestart_fight \
    --gate-dll <x4lan_gate_64.dll> --out .local/sessions/fight3 [--open 3]
```

This captures the session save on the capture profile: X4's new game for the base start, the gate fixture and
one hull per player start at its start position. It then registers the save and builds the play overlay. Each
player's start kit (money, reputation, blueprints, research and map) is applied at the setup tick on every
client. It ends by printing the `x4ls play` line.

In coop every player shares the wallet and the reputation, so a player start whose kit brings its own inventory or
reputation is refused until you leave those parts out, for example `--omit 2=inventory,reputation`.

Smoke ns1/ns2 (8 Nov): `--base-start x4ep1_gamestart_discover --players 2 --mode coop --start 2=x4ep1_gamestart_fight
--omit 2=inventory,reputation`. The capture took 2.5 minutes. Both clients then played the 2-minute window in sync
(0 desyncs, 0 errors): slot 1 in the discover start's ship, slot 2 in its own fight hull at that start's position,
with the fight start's money (added to the shared wallet) and blueprints applied at tick 20 on both.

## Ending

- The session ends by itself at the end of the pilot window.
- Close the X4 windows when you are done. The launcher then collects the logs.
- To end it at once, press Ctrl+C in the console. The launcher closes both windows and collects the logs.
- To leave early alone, close your window. The others go on after 15 s.

## What to send back

The run folder `.local/runs/<time>/`:

- small files: `summary.json`, `errors.log`, `desyncs.log`, `desync-status.json`, `relay.log`
- the client logs: `clientK/engine-output.log` (play mode: about 5 MB per client-minute; `--full-logs` overlays
  write about 120 MB more for each world capture, every 1800 ticks)

`python tools/x4ls.py triage .local/runs/<time>` reruns a diverged session with diagnostic windows before its
first mismatch.

## Not yet

- **Two PCs: built and run on one PC through the LAN address, not yet between two PCs.** See "Two PCs" above.
  - Intel against AMD is untested: their reciprocal instructions differ, and a desync there would be silent and
    logged.
  - Windows against Linux: with `--mixed-platform` on both, a battle lane agrees to tick 27 (8 Nov); the Linux
    people stack is still being ported.
- **Two people on one PC.** Keyboard and mouse go to the focused window only. Whether X4 reads a gamepad in an
  unfocused window is unverified.
  - HUD markers and the radar (fixed 9 Nov). Your own ship is drawn about 100 ms ahead of the simulation so it answers
    the controls at once. The HUD's brackets and lead marks were still placed for the simulated camera, so while you
    steered they all slid off their targets in the same direction. They are now placed for the camera the picture is
    drawn with (`native_gate_own_predict_hud`, a default). Player 2's radar showed player 1's surroundings, because
    X4 attaches the radar to the player's ship once at load; each client now re-attaches its radar, crosshair and
    weapon panel to its own ship 10 ticks into the session, after a respawn and after a hot join
    (`native_gate_hud_ship_refresh`, a default). hud3: two clients, 900 ticks, 0 desyncs.
  - Markers, second pass, and target lock (9 Nov, after a played session). The picture is not drawn at the predicted
    pose itself: with other ships drawn between the last two steps, every frame is drawn up to one step behind it
    (the step's frame at -1, the frame between two steps at about -0.5). The markers now aim at the mean of the
    offsets the last step's frames were drawn with (hud5: -0.75 on both clients), so they are off by a quarter step
    at most. The stock HUD's soft-target lock (it keeps your target while you steer hard at a small ship) was refused
    102 times in that session with an info line each few seconds; it only touches the UI's own target manager and
    now runs. The HUD's own encyclopedia entries (AddKnownItem) are still not shared, but no longer print an info line.
  - The cursor (fixed 9 Nov, the focus rule in `lockstep_pilot_stock.h`). X4 reads the PC's one cursor in the
    unfocused window too. When the cursor is in the other window, X4 sees it at this window's edge. Before the fix,
    that edge position steered this window's ship on every client (cursor steering), aimed its guns there, and turned
    its camera. Now a window without the focus records neutral cursor steering and no aim, and skips the camera and
    menu inputs that would engage. A release always passes, so nothing stays held. The client's stock status counts
    each drop in `unfocused_drops=cursor/aim/local`.
- **Desync recovery.** None: a session that differs stays different.
  - Hot join: `x4ls play --open` with `x4ls join` (no `--arm-at`) stays in lockstep in hj27/hj28, but a join timed
    at an engaging gun can still part (see "Joining a running game"). Rejoin after a drop exists in lane runs only.
  - After a late join a few large ships still differ (reports/LOCKSTEP-HOTJOIN-20261101.txt).
- **Desync indicator in game.** None, so that desyncs stay silent. `tools/dev/native_gate_desync_watch.py --status
  .local/runs/<time>/desync-status.json` shows one in the console.
- **Up to 8 players.** The gate has 8 slots (`--players 2..8`), but natively it has only been run with 2 or 3.
  Stock piloting knows 4 cockpit ships, so slots 5..8 need a session with per-player starts (`x4ls new-session`).
- **Lobby, first form:** the main-menu Multiplayer page starts a session whose players are all there at the start
  (see "Start from the main menu"). A join into a running game still goes through `x4ls join`, and X4 has to be
  started by `x4ls lobby`, not from Steam.
- **Not built yet:** per-player story missions (in progress).
  - Personal missions per player, first form (9 Nov, on by default; `--set native_gate_missions_per_player=false`
    turns it off): a mission player 2 accepts is player 2's on every client. Its script sees player 2, their ship and their sector
    (mpp1: 900 ticks, 0 desyncs).
  - Each player has their own active mission, and each client's mission menus list its own player's missions (mpp2,
    9 Nov: two players, a mission each, 900 ticks, 0 desyncs).
  - Generic mission offers are made around every player, not only player 1: about every 3 minutes X4's own offer
    selection runs for each further player's sector, and the offers it makes follow that player (mpp5, 9 Nov: player
    2's selection made 8 offers on both clients, 2400 ticks, 0 desyncs). Rewards in factions mode are not per player
    yet.
