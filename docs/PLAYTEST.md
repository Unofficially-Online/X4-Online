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
- your own inventory (11 Nov): every player has one; crafting and dropping items ran with two clients. Built the
  same way but not run in the real game yet: research, terraforming, equipment mods for players 2 and up, the
  inventory your start brings (docs/PLAYERS.md has the table and the runs)
- faction representatives and item traders (21 Nov, `tools/dev/action_tests.py reps` / `itemtrader`): player 2 alone
  docked at the Argon headquarters NLN-075, talked to the representative, its blueprint shop came up on player 2's
  screen and the blueprint went into the shared library (run 20261121-185246, in sync); player 2 bought an item from an
  item trader at MMJ-623 into its own inventory (run 20261121-192700, in sync). The representative has its office and
  its shop only while a player's ship is near its headquarters, as in the stock game; any player's ship counts. The
  face of the person a player talks to shows on that player's monitor only (21 Nov; before, on every player's monitor,
  and it stayed after the conversation: run 20261121-185246 against 20261121-203407).
- station control panels (hacking, 21 Nov): the panel's question and its Yes work for every player walking on a
  station; the hack uses that player's own Security Decryption System. Run 20261121-211253 (`action_tests.py hack
  --players 2 --ai-dock`): player 2 walked off its ship at CZF-533, hacked the watchdog terminal of the Security Office,
  its own decryption systems went 3 -> 2 on both clients, in sync. The test presses the question's Yes; a person aims
  at the terminal within 4 m first, as in the stock game. Open: right after a hack player k's own screen still offers
  that panel (its test reads player 1's memory of hacked panels); a second Yes does nothing.

**Refused:** any other menu action that would change the world on one client only.

- X4 shows an info line ("... refused" or "... is off during a lockstep session") and nothing happens.
- Each refusal is one row in `errors.log`.
- Pausing and loading are refused this way. The options menu and the ship configuration menu then open without
  pausing. Since 11 Nov the Pause key and F9 (quick load) are refused too: in run inv1 the Pause key, which does not
  go through `Pause()`, stopped the pressing client and the session stood still for everyone; in run inv2 the keys
  were refused on both clients and the session ran to its end in sync. Saving (the menu's Save game, F5) was refused
  the same way until 25 Nov; it is now a save for every player ("Saving yourself" below).
- The session saves by itself: every ten minutes each client's game stands still for about three seconds while
  its gate writes the world (`build_play_overlay.py --save-every`, docs/LAUNCHER.md Periodic saves), and once more each
  time a player completes a mission chapter of their story ("Milestones" below). Since 25 Nov any of them can be
  continued with every player kept (`x4ls continue --save`).
- **Saving yourself** (since 25 Nov): Save game in the menu (any slot or a new save) and the quick save key work. They save
  the game for every player at once, a few seconds later: you see "saving the game for every player..." and then
  "game saved (<name>)"; the others see their game stand still for about three seconds. The save waits while player 1
  is on foot. The run folder keeps it as `saves/player/<name>-t<tick>.xml.gz`; go on from it with `x4ls continue --save`.
  Load game stays off during a session (said once); a game is loaded by starting a session from the save.
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

**Bullets and the HUD radar** move with the picture between the steps (`render_shots`, `render_radar`, both on by
default, display only): a bullet is drawn where its last two steps put it at the frame's time, your own shots leave
the guns of your drawn ship, and the radar turns with the heading you see. `RENDER_SHOTS` and `RENDER_RADAR` in the
client log say what ran.

**Your shots at other ships** (24 Nov, every player): a bullet that meets a ship is X4's own hit on every client, so a
friendly or neutral ship takes the damage too, and the attack counts as yours the way X4 counts its player's
(`event_player_attacked_object`: the relation loss, the warning, the police), from your ship's guns, turrets and
missiles. Only a hit on another player's ship is still decided by a claim (the player being hit decides).
`HIT_CLAIMS_STATUS native_out=` (your hits on other ships) and `PILOT_FIRE_STATUS attacks=` (`slots=`: the attacks
counted per player, `told=`: those the shared scripts heard) say what ran.

**Making a pilot bail, claiming the ship, going aboard** (24 Nov, every player): shoot a ship until its shields are
down and its hull is below 75 %, and every 30 s while you keep hitting it X4 rolls whether its crew bails out (about
one in three for a fighter against a corvette, the same chance as player 1's: the roll counts your own ship as the
attacker). Once the pilot is gone, get out in your suit, hold the crosshair on the claim signal and use the repair
laser: the ship becomes yours. Ask it for docking and fly the suit to its spacesuit dock: you sit down at its
controls (player 1 lands on foot inside and walks to the seat). `PILOT_FIRE_STATUS owner=` (bail rolls heard),
`LEAK_CLAIMED` rows of the test script and `PILOT_DEATH ... enter=` (the last number: ships boarded) say what ran.

**Avoid:**

- SETA (time acceleration): untested under the gate. (The Pause key is refused since 11 Nov, see above.)

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
  so Windows may warn about an unknown publisher. A release kit (`tools/make_release_kit.py`, no session) becomes
  the product "X4 Online" instead (docs/NETPLAY.md, "Release notes").
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
- **Two PCs, 23 Nov (kit16-2p: this branch's commit eb2a0393, 2 players, both at the start, served by the VPS).**
  The desktop PC: X4 started from the game folder itself with every mod the player has there (kuertee's UI
  extensions replace X4's options menu, the old October lab extension is there too), in a disposable profile
  standing in for the player's own; the Start Menu has Multiplayer above Settings (with protected UI mode on or
  off). Clicks by window messages (`tools/dev/ui_window_input.py`): Multiplayer, then the page's Join. X4 quit by
  itself, the helper started it again in the multiplayer profile, joined. The Windows laptop joined through its
  launcher's page (`x4ls lobby --auto join`). Both loaded at the server's second and played 18,361 ticks together
  over the internet (about 24 ticks a second, the laptop's pace) with 0 mismatches and 0 clock splits, through the
  periodic save at tick 18,000. Nobody flew: the players sat in their cockpits while the universe ran.
- **Why the Multiplayer page did nothing on the desktop PC before (23 Nov):** the entry was there, but no helper
  answered behind it. Windows' Startup apps had the helper's autostart switched off (15 Nov), so the page said
  "The multiplayer helper is not running", and the installed kit (11 Nov) was older than the one the server ran,
  which the server refuses. X4's UI cannot start the helper itself: its `OpenWebBrowser` opens only http(s)
  addresses of a few hosts (egosoft.com and the like), so the helper must run before the player chooses Join.
- **Fixed 23 Nov: Documents in OneDrive.** OneDrive marks the folders of a Documents it backs up read-only, and
  Windows refuses to remove a read-only folder: the launcher stopped at the wipe of its disposable profile
  (PermissionError, WinError 5). `profile.prepare` now makes such entries writable and removes them. A running
  OneDrive also uploads the disposable profiles (a session's log is hundreds of MB): on a PC whose Documents is in
  OneDrive, pause OneDrive while you test.

### A game you can join while it runs, with a start of your choice (built 10 Nov; run on one PC)

A kit whose overlay has open slots (`build_play_overlay.py --open`, `x4ls new-session --start K=... --open K,...`)
makes the server's game a drop-in one: nobody waits for a full house.

- **The first player** chooses **Start the game** on the Multiplayer page. They get the slot that is not open
  (slot 1, the session's base start) and load the snapshot as in any game. The server starts the relay with the
  join arguments (`hotjoin.relay_join_args`, slot 1 saves the joins).
- **Everyone after that** sees one row per open start on the page (`hotjoin.open_starts` of the recipe; a taken
  start says so) and picks one. X4 restarts and the helper runs `x4ls join --server-file server.json --slot K`:
  the server admits the PC and names the session, and the rest is the hot join of "Joining a running game" against
  the server's relay. Only the joiner loads; the others have a short pause at the save and at the hand-over.
- **The join files** (`j<T>.x4join`, `j<T>.xml.gz`, about 190 MB) are written by player 1's X4. The server follows
  its relay's log; at the `saved` notice it asks player 1's launcher, which stays connected for this, and the
  launcher uploads the two files. The server checks them against the announced hashes and serves them on TCP
  `port` with `joinfiles.FileService`, where the joiner's `x4ls join` fetches them as it would from a host's PC.
  The upload takes as long as player 1's line needs for 190 MB, and every second of it is 30 ticks the joiner has
  to catch up; the relay gives a joiner 900 s from the save to its first report.
- A late joiner's address is not known when the relay starts: the server writes it into the relay's
  `--allow-file`, which the relay reads again when a datagram comes from an unknown address.
- **Limits of this first form:** player 1 has the fixed base start. Joining needs player 1 in the game (it is the
  only saver; whether a client that joined can save for the next one is not tested), and the page says so when
  player 1 has left. A slot whose ship is in another sector than player 1's still splits the runtime ID counter
  (the hot-join work has the fix in progress): do not hand out a kit with far starts before that.
- The server also listens on TCP `port` for this (`deploy_playtest_server.py` opens it).
- **The page in the launcher's X4 too.** `x4ls lobby --server-file` (PLAY.cmd, and the installer's Start menu entry
  "... (second window)" = `second.pyw`, for a second player on one PC, which Steam does not start) asks the server's
  status and shows the same rows. A pick closes that X4 and runs the join (`--same-pc` when another X4 runs here).
- **Far starts are marked.** The server's status names every open start with its sector; one in another sector than
  player 1's is shown as "far, experimental": it joins and plays, and the server counts desyncs after such a join.
- **When a game ends.** The relay does not end by itself: the server ends a game when the relay reports its last
  step, when a game that has stepped stops for 60 s (everyone left; 240 s while a joiner is brought in), or when
  nothing steps for `--idle-end` (420 s) after the start. Then it takes the next "Start the game".
- **Live, 10 Nov, two X4 on one PC with a local server:**
  - drop2 (capture-005, slot 2 open, `x4ls join --server-file`): save 702, hand-over 3315, catch-up 89.7 s,
    0 digest mismatches, to 5440 in sync.
  - drop3 (the same through the Steam-menu page of an X4 with protected UI mode, its helper and the pipe): save
    697, hand-over 3346, 0 mismatches, to 5440 in sync.
  - drop5 (the eight-start session ns8, the launcher's page in a second window picks slot 2, the start in the
    host's sector): save 679, hand-over 3472 with a pause of about 59 s, catch-up 82.9 s, 0 mismatches, to 5440
    in sync.
  - Not run: a join over a real uplink through a server on another machine, a join at an arbitrary tick, the far
    starts through this path (x4mods-b8's hj8: slot 3 joins and plays, the report differs from 52 ticks after the
    save).
  - Through a server on another machine (11 Nov, the VPS, two X4 on one PC, ns8 slot 2): the saver's upload of the
    207 MB took 19 to 20 s; from the save to the joiner's first step about 125 s. vps3 (save 676) and t1 (save 685)
    joined with digest mismatches from T+1: a bullet hit in the joiner's loading frames and one small ship's
    navigation timer; both were the save tick, not the server, and are fixed (X4-Online 0a41004c). vps4 (save 673,
    build-st26) joined with 0 mismatches, but its pause of 127 s was longer than the 120 s a client waited for the
    relay: player 1's gate stopped with "relay timeout" as the joiner arrived. vps5 (save 667, build-st27: clients
    wait 240 s; `x4ls serve` gives the relay `--join-catchup-limit 120` and a `--join-max-pause` 25 s under the
    clients' wait, at most 170): joined at 7336 after a pause of 118 s with 0 mismatches, then 60 s of play together
    to step 9002, desync false. On one PC the joiner did not gain during its catch-up (27.8 steps a second against
    the session's 30); on two PCs it has not been measured.
  - Open: a join saved at tick 680 parts about 100 ticks later (in work); the far starts.
  - click2 (11 Nov, real mouse input injected into a lobby-form X4, no test switches): the page's button takes one
    click ("Start the game" -> `LOBBY REQUEST JOIN` -> joined -> the overlay's files ran). A main-menu row took two
    clicks, the Multiplayer row and X4's own Settings row alike: the first selects the row, the second opens it.
    X4's "Modified game detected" notice is closed by the page in the launcher's X4 (the disposable profile); in a
    Steam-started X4 it is the player's own game and the notice is left alone.

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

What differs between two Windows PCs, measured on one PC by giving one client the other PC's value (21 Nov):
- **CPU thread count: split, fixed.** X4 makes its pathing batches from the job thread count, logical
  processors - 2 (a 32-thread PC: 10 + 20 batches of about 500 ships, a 24-thread PC 8 + 14), and the
  session's pathing budget gives each batch 100 full updates a pass, so the two PCs updated other ships. Client 2
  with a 24-thread PC's layout split at tick 36 (runtime IDs) and the players' mission results differed.
  `native_gate_worker_layout = 30` (a default, lockstep_worker_layout.h) lays every client out as for 30 job
  threads before the world loads, and gives Jolt the same count; the test key
  `native_gate_worker_layout_test = {slot, P, W}` lays one client out as a P-thread PC first.
- **Graphics and detail settings: no split seen.** Client 2 with `lodfactor` and `effectdistance` 1.0 (client 1:
  0.5), lowest quality, shadows and SSAO off and a 1440x900 window against 960 wide stayed in sync through
  3,547 ticks with combat (mission matrix flyto, collect, kill; `X4LS_CLIENT_CONFIG`, test only, sets one client's
  X4 settings).
- **A slower PC: no split seen.** Client 2 held to 4 logical processors (CPU affinity; X4 still makes its 30
  threads) stayed in sync through 3,599 ticks of the same tests.
- **All of it at once, with the fix:** client 2 laid out as a 24-thread PC and then by the fix, with the other
  settings and on 4 processors: the whole mission matrix (12 of 12 for both players) in sync through 14,479 ticks.
- **Time zone and region: nothing to fix.** Local time is read only by XNet's packet logger (0x15828b0), and
  no engine code reaches the C runtime's user-locale lookup (only `setlocale(..., "")` would), so numbers are
  printed and parsed in the "C" locale on every PC.
- **Instruction set: nothing to fix.** The engine's own `__isa_available` branches test for SSE4.2 only (every
  current CPU), the AVX2 test is in the CRT's string routines, approximate `rcpss`/`rsqrtss` are exact by default
  (`native_gate_exact_reciprocal`) and the CRT's FMA3 math goes through the portable library.

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
of its own clients only, so slot 1's PC must be one of the savers. With `--open` the relay hands a joiner that does not
gain on the session over after 10 s of catch-up (at most 20 s) and may pause everyone for up to 180 s while it closes the
gap; a joiner that gains catches up with no pause (below).

Status (9 Nov, one PC). The whole join runs: the request, the save at the next join tick, the 176 MB download
(verified), the joiner's overlay and its checks, the load, the resume into the open pilot window, the catch-up,
the handover and the joined player to the end of the window. hj27 (x4mods-28) and hj28 (the plain playtest flow
above): the joiner equal to the session through the catch-up and the handover, then about 2,000 ticks of play after
it with 0 desyncs, 0 errors and 0 drops. On one PC the joiner is no faster than the session, so the handover pauses
everyone for about as long as the joiner took to load (hj28: 75 s); a faster joiner PC pauses less. Since 10 Nov a
joiner replays without the 30-per-second hold. Measured with two X4 on one PC: a simulation step costs about 25 ms
there, so the replay ran at 35 steps a second and the pause was 62 s instead of 70 to 77 s. The hold was not the
limit on that PC; a joiner on its own PC should gain more, which is not measured yet.

### How long a join takes, and who waits (23 Nov)

A join stops the session twice: while the saver writes the join save and package (every client waits for its step),
and at the handover when the joiner is still behind (everyone waits until it reaches the handover tick). In between
the session plays on while the joiner fetches, loads and replays. Measured on one PC (the host's X4 and the joiner's,
other sessions' clients beside them; late joins at T about 6,000, 200 s into a session):

| | before (late boot, fjl2) | now (fjv1, fjv2) |
|---|---|---|
| save stop (everyone) | 9.4 s | 7.9-8.3 s |
| joiner behind at its first step | 2,304 ticks (81 s after the save) | 1,443-1,472 ticks (50-51 s) |
| handover stop (everyone) | 80.3 s | 49.6-52.1 s |
| save to handover | 181 s | 119-122 s |

- **X4 boots before the session saves.** `x4ls join` builds the joiner's overlay with stand-ins (`build_play_overlay.py
  --join-early`) and starts X4 at once; once it waits in its start menu (about 30 s) the relay is asked for the save, and
  the launcher fetches and checks the files, stages them and writes `native/join-ready.lua` (both SHA-256), which the
  waiting probe takes before it loads. The session plays on while X4 boots, so the joiner's gap is the download and the
  load alone (fjm1: 1,568 ticks, 55 s after the save). `--ask-first` asks as X4 starts instead: the joiner is in about
  10 s sooner, but its gap is a few seconds longer (fje5: 1,722 ticks, 60 s). `--late-boot` keeps the old order.
- **The package travels packed.** The host offers `j<T>.x4join.gz` (gzip level 1: 192 MB to about 30 MB in about a
  second) beside the raw package; the joiner takes it when offered and checks the unpacked bytes against the raw SHA-256
  the relay announced, else it fetches the raw file (an older host, a server that stores only raw files).
- **The saver writes the package off the frame.** The package's build, write and hash (about 2 s of the stop for 192 MB)
  run on a worker thread; the SAVED record follows once the file is complete (the relay waits up to 30 s past the lead for it). The
  rest of the stop is X4's own save (about 3 s) and the capture of the side tables; `JOIN_SAVER_TIMES` in the saver's log
  breaks it down (`cap_ms`: engine save/tables/identities/gone/fixture/world/build).
- **No pause for a joiner that gains.** The relay measures the joiner's steps over 10 s. One that steps at least 5 % faster
  than the session catches up with no pause at all, past `--join-catchup-limit` too, as long as it arrives within
  `--join-converge-limit` (600 s). One that does not gain is handed over as soon as its rate is known (10 s into the
  catch-up), because every further second of catch-up only lengthens the stop. The handover row says why (`why=gap`
  caught up, `not_gaining`, or `limit`).
- **Joins at some ticks parted** (relay digest from the handover on; the census motion domain from T+1), in the old order
  as in the new, each time by one medium ship in low attention. Joins from tick 6,183 of the test session: fixed
  (24 Nov: a ship drifting on a linear controller at T took the save's rounded start rotation; the package now carries it,
  fjf1 equal). Joins at T=6140..6147: still part (a ship with a navigation controller; open,
  reports/LOCKSTEP-FAST-JOIN-20261123.txt).
- **Why one PC still stops at the handover.** Both X4 are bound by the simulation step (about 31-35 ms on this PC with
  other clients beside them; drawing is about 1 ms of it, waiting for permits 0.1 ms), so a joiner on the same PC steps no
  faster than the session and the handover stop is its gap over its rate. A joiner on its own PC that steps faster than
  the session needs joins with no handover stop; that two-PC case is not measured yet.

What a joiner sees and hears (10 Nov, after a player's report of a flashing screen and of the host's combat chatter
on the joiner):
- While it replays the time since its save (about as long as its load took) its window shows the host's cockpit,
  moving through the session's past. Its own ship has no pilot seat before the handover, so its own view starts
  there. A proper "joining" screen is not built.
- Spoken lines are silent on a client while it replays, and on a client whose ship is in another sector than the
  host's (`native_gate_speech_local`, a default, Windows). X4 starts comm chatter for the one player it knows, the
  host, and every PC played it. Next to the host a client hears what the host hears. Other sounds of the replay
  (engines, guns near the host) still play. Seen live (hjs4): the joiner silent for the 2,818 steps of its replay and
  the 1,895 steps it then flew in the Terran sector; the host never.
- The flashing: after a join into another sector every other frame was drawn without that sector's background
  (planet, stars). The join set each object's attention level without telling the renderer's two copies. Fixed;
  frame bursts of the joiner's window alternated before and are steady now.

Missions across a join (hj32, 9 Nov night): a mission accepted before the join keeps the session's ID on the joiner
(`native_gate_join_cue_ids`, a join default: the join package's CUEI section gives every saved cue its live ID), and a
player's accept after the handover applies on every PC with equal mission owners; played to tick 5440, 0 desyncs, 0
errors. The package has one more section for it, so the host and the joiner need the same build of the kit. A UI
element made after the load (part animations) takes client-local IDs (`native_gate_runtime_ids_ui_elements`, a session
default; plain2: 1841 ticks of two-client play, 0 desyncs).

A join after a respawn (hjr1, 10 Nov): a player whose ship was destroyed and rebuilt before someone joins no longer
makes that join fail. The joiner takes the rebuilt ship for that slot and was in step for 650 ticks after its join;
then the rebuilt ship was pushed about 29 m once, at tick 1344 on the host and near 1600 on the joiner, and the
session counted desyncs from there. Fixed (10 Nov): the rebuilt ship was built at the start position, inside the
ship it replaced, which is still there when a ship is lost without exploding. X4 lets two bodies lie in each other
for 30 s from the moment a body is made and then pushes them apart; a joiner's bodies are made at its load, so its
push came later. Each rebuilt ship now stands 1 km above the one before, clear of the old one. hjr1 with it (join
save at tick 687): 0 mismatches in the joiner's replay, no desync to the end of the run, 0 errors. Two things
remain: the 30 s itself still counts from the load on a joiner, for any two bodies that lie in each other at a
join; and a join whose save is taken near tick 710 on this test still differs from tick 1011, a separate case in
the join code. Since 10 Nov every client also restarts the collision shape refresh of every body at a join (it did
so for the reference ships only), so those refreshes come at the same ticks on the joiner.

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

In coop every player shares the wallet and the reputation. A player start's own reputation is merged into the shared
one, upward only: for each faction the team takes the better of what it has and what the start gives. A team with a
Terran cadet is friendly with the Terrans and keeps the base start's friends; nobody's start makes the team worse off
(11 Nov: before this the start's reputation had to be left out, and a Terran cadet on an Argon base start saw the
Terrans as hostile). `--omit 2=reputation` still leaves it out by name. A start's inventory is the player's own in
both modes: every player has an inventory of their own, and the start's wares are added to it eight ticks into the
session, or twelve ticks after the hand-over for a player who joins later (a `KIT_INVENTORY` row on every client).
`--omit 2=inventory` still leaves it out. Every stock start's inventory holds the spacesuit's repair laser and the
object scanner, so without it players 2 and up have neither. A session built before 11 Nov left the inventory out
by name; `python tools/dev/native_gate_session_start.py kits --session session.json --out session-inv.json` (with
`--libraries` and `--scripts` as for `spec`) makes its kits again from the game data without a new capture: the
save and its hulls stay, only the kits the plan applies at every start change. Run inv4 (11 Nov, two clients):
player 2 had no repair laser at tick 44 and one laser and one scanner after the step at tick 48, on both clients,
in sync to the end. Not run: the same for a player who joins later.

Smoke ns1/ns2 (8 Nov): `--base-start x4ep1_gamestart_discover --players 2 --mode coop --start 2=x4ep1_gamestart_fight
--omit 2=inventory,reputation`. The capture took 2.5 minutes. Both clients then played the 2-minute window in sync
(0 desyncs, 0 errors): slot 1 in the discover start's ship, slot 2 in its own fight hull at that start's position,
with the fight start's money (added to the shared wallet) and blueprints applied at tick 20 on both.

### A headquarters and research per player faction (built 16 Nov; two clients on one PC, no person at the controls)

In coop the players are one faction: they share the one headquarters the first plot gives, and what it researched.
In factions mode each player's faction gets its own:

```
python tools/x4ls.py new-session --base-start x4ep1_gamestart_discover --players 2 --mode factions \
    --start 2=x4ep1_gamestart_discover --omit 2=known --plot 2=hq ...
```

- `--plot K=hq` gives player K an own copy of the stock headquarters plot. The copy is generated on each PC from its
  own game files (the overlay and a kit carry a recipe with hashes, never the game's script); it runs as player K and
  names player K's faction where the stock script names the player faction, so the station it makes is that
  faction's: a second, separate station object, placed by the game's own safe position (about 20 km from the first in
  the test). Refused in coop.
- Research belongs to a faction (`native_gate_research_per_faction`, on by default). A player with an own faction
  starts and cancels research at its own headquarters; when the research module finishes, the research is credited to
  that faction only, and each client's menus see only its own faction's research. In coop every player's missions read
  and add to the one shared list.
- Run hq11 (16 Nov): both headquarters made by the plot's own debug step, player 1 and player 2 each started a
  10-second research by script at their own headquarters. Both finished; player 1's list gained its ware, player 2's
  list holds only player 2's; client 1's menus answer "researched" for player 1's ware only, client 2's for player 2's
  only; 5,891 ticks in sync, 0 errors, and both clients' session saves hold each player's list.
- Two things this needed, which matter in coop too: a research start replayed on every client now starts the queued
  item (the game ends that call with a forced update of the station, which the lockstep's "menus do not update
  stations" rule had swallowed), and a small session script asks every player headquarters for its production every
  two seconds, because the game only advances a station's production when something asks and in lockstep the menus
  never do.
- The stock research menu lists "the player's" headquarters; on the client of a player with an own faction that
  list is answered with its own faction's (run hq12: the menu's own list call, made by script on both clients, gives
  client 2 player 2's headquarters and its research module, client 1 player 1's). Nobody has clicked through the
  menu as player K yet.
- Blueprints go with research (17 Nov): a faction's library is both lists. The game's own research step also gives
  the blueprints a research unlocks, so they have one owner. In factions mode a blueprint player K buys or unlocks
  is player K's faction's alone, and each client's menus show its own faction's blueprints.
- A start's blueprints and research are given in factions mode too (17 Nov; before, `--omit K=blueprints,research`
  was required): they go to player K's faction's own library, ten ticks into the session or fourteen ticks after a
  join. Only a start's map knowledge still has to be left out by name in factions mode (the map is shared).
- The game's notices about a faction's production go to the player whose faction it is (17 Nov): "production
  started", "finished", "cancelled" and "research unlocked" are what the headquarters plot waits for, and the game
  sends them only for player 1's faction. Now player K's own missions and plot hear player K's, and nobody else's.
  (The first form of 16 Nov credited the research to the right list but announced it as player 1's.)
- With `--plot K=hq` the stock headquarters plot is player 1's own: what the other players do no longer also
  advances it (scripts that belong to nobody hear every player, and the stock plot counted as one).
- Run hq13 (17 Nov, two clients, 6,538 ticks in sync, 0 errors): player 2's start gave its faction 18 blueprints and
  3 researches (player 1 keeps its own 26 and 4); both players researched by script at their own headquarters;
  a test mission of each player heard exactly its own player's three notices, on both clients; both clients' saves
  hold each player's own research and blueprints.
- Run hq14 (17 Nov, 6,091 ticks in sync, 0 errors): the same, plus a research player 2 started and cancelled (its
  mission alone heard "cancelled"), and both plots put into their research phase by the stock script's own debug
  step (`native_gate_plot_debug` codes 1101 and 1102): player 2's copy reached the same state as player 1's plot,
  cue for cue, read from the save.
- `x4ls resume` gives players 2..N their start's blueprints and research again, not what they researched in the
  first session (a resumed world keeps only player 1's player object, as before).
- A player who joins a running game gets the same (run hq17, 17 Nov: a hot join 12 s into the session, then 33,139
  ticks in sync, 0 errors): the joiner's start blueprints and research go to its faction's library fourteen ticks
  after it takes over, its own copy of the plot begins, and later its own headquarters, research and notices work
  as for a player who was there from the start. `x4ls join` takes `--display`, `--no-focus`, `--never-focus` and
  `--tile-skip` like `x4ls play` (the joiner's window used to open on the primary monitor with the focus).
- Seen in the join tests and not part of this feature: a join about 100 s into that test world (the join save at
  tick 6050) does not match the session from its first tick on, also in a plain session without a plot, scripted
  steps or the library per faction (runs hq18 to hq20; the joiner of hq20, started late and far behind, crashed
  while catching up); and two joins at ticks 698 and 724, each just after player 1's headquarters had been made,
  came apart about 50 s after the joiner took over, in the world part only (hq15, hq16), where the join at tick
  734 into a world without a headquarters did not (hq17).
- The late join was run again on the integration code without any of this work (17 Nov afternoon, a second
  worktree at the integration head with the integration's gate build, join save at tick 6080): the same result. From the
  joiner's first tick only the world part of the digest differs (sample, pilot, identity, runtime IDs, nav mesh
  and input agree), and at the handover (tick 8844) the runtime IDs differ too and the session reports a
  desync. What differs: the join save holds 1400 temporary zones; at tick 7200 the host's own save holds 1399 and
  the joiner's 1396. Three empty temporary zones, each with a kill check scheduled about two minutes later, are
  in the join save and still in the host's world, and gone from the joiner's after it loaded the save. So the
  likely cause is that X4 drops such zones when a save is loaded and keeps them until their check when it plays
  on; it is not fixed, and whether the two early joins above are the same thing is not known (a station made at
  a safe position gets a temporary zone).
- What it is (17 Nov evening, runs hq29 to hq32): not the load itself. An empty temporary zone is removed at its
  kill check unless a table of zone references holds a count for it or a recent time. That table is in no save: a
  load builds its own from what it loads. Read from a live pair, the session held 1,346 entries with their own
  times and the joiner 622, all stamped with its load; at their checks the session kept two zones the joiner
  removed, and from then on the two worlds made and numbered zones differently. The join package now carries the
  table (section ZREF), and with `native_gate_join_zref=true` a joiner takes the session's. Run hq32 with it: at
  tick 7200 the host's and the joiner's saves hold the same components (only local effect objects differ; before,
  the joiner lacked zones), and the runtime IDs agree through the handover and for about 190 ticks after it,
  where in hq29 the two had numbered new zones differently before tick 7200.
- Not solved by it, so the option is off unless asked for: the runtime IDs still part later (tick 9173 in hq32).
  The two tables were read again after that: 24 of 1,153 common zones differ, most of them by one count more on
  the joiner with the time of its load, so something on the joiner takes those references again in its first
  step after the load, as the session did after its own load at the start. And the world part of the digest
  differs from the joiner's first tick as before: the session's table holds 16 more created rows than the
  joiner's (the saves do not differ in them), which the session reports as a desync at the handover.
- The 16 rows (17 Nov, late evening): objects the session still held at the join tick that the join save does not
  hold (in a battle; the joiner counts as many session records it cannot place, and the session's next capture no
  longer finds them). The joiner's table took a row only for an object it has, so it stayed that many rows short
  for good. Now it takes an absent row for each, as it does for an object that is gone. Run hq37 (join at tick
  6060, 11 such objects): the two tables have the same number of rows from the first capture, and at tick 7200
  three of the four parts of the world digest agree (alive, hull and shield, timing), where all four had
  differed for good.
- Still open after both: the motion part of the world digest differs (so the session still reports a desync at
  the handover), and the runtime IDs part later (tick 7496 in hq37, 9173 in hq32). Which objects move
  differently is the next thing to find; the reading above that the joiner takes zone references again in its
  first step is not confirmed: a counter on the game's eight calls that take a reference showed none of them
  running on either client in 6,000 ticks, so the counts change some other way.
- Object by object (17 Nov night, runs hq38 and hq39, `native_gate_world_pages_window` on both sides): at the
  joiner's first tick every object the session had changed since its last capture, 10,379 of them, reads the
  same on the joiner in every field; the only difference is the zones the join save left out (11, all of them
  temporary zones on their way out). 550 ticks later 149 ships in 95 zones differ in their motion and nothing
  else; at tick 7200 about 6,700 do, and 25 have another parent. The raw navigation rows differ for every ship,
  but only in the buffer indices that are each client's own, and no more for the ships that drifted than for the
  others. So after a late join ships drift apart one by one for a reason the census does not hold; which state
  that is, is not found.
- A joiner with the zone table stopped twice in a walk over a list that held an object already gone (hq38, 470
  ticks after its join, in a script; hq42, 133 ticks after, in the radar's worker), three times it did not
  (hq32, hq37, hq39). The first form wrote the session's count over the joiner's own; it now never lowers a
  count and leaves the joiner's own extra entries alone. hq42 ran with that and stopped all the same, so the
  cause is not shown to be the table, and not ruled out: a reason more to leave the option off.
- Other ends of a late join in these runs, none looked into: in hq45 the joiner stopped itself eight ticks after
  its join with the pathing budget check (stop reason 79), and hq44 ended at the same point; in hq43 it was
  dropped for falling behind when both sides saved every 600 ticks. None of the night's late joins reached a
  clean handover.
- Why ships drifted after a late join (18 Nov morning, runs hq46 to hq61, all with the zone table on): the
  joiner's radar did not see what the session's saw. The chain that found it: the per-tick rows of shared
  runtime IDs named the first split, one AI script more on the joiner 36 to 44 ticks after the join; the script
  RNG rows of both clients showed `event_gravidar_has_scanned` running fight and movement handlers on the joiner
  alone from five ticks after the join (the event and its ship are in the row's key and can be recovered by
  trying keywords and nearby IDs); the radar post rows showed a unit's own place alternating between two poses
  on the joiner; the join watch showed why. A ship that the engine does not republish keeps its last published
  pose, attention and parent in both views, and the radar reads the view of the frame. Three cases, all fixed
  in the join (`native_gate_join_gravp_settled`, a join default):
  - A fighter that had left a busy sector 14 s earlier still published attention 6 and a pose 33 km behind,
    at a real attention of 2. The join put the session's poses back only for objects whose *published*
    attention was low, so the joiner's load wrote the live pose into one view and nothing took it back. The
    restore now also goes by the real attention when the two views agree.
  - A fighter stored inside a carrier whose one view still showed it on the pad: the session's radar sees it on
    every other frame. The joiner's load made both views show the storage when the join tick's parity hit the
    stale view. The session's cached parents are now kept through the load's first two frames, unless the
    ship really changed its parent in that frame.
  - A ship changing its zone in the very tick of the join has a publication in progress that the session
    completes one tick later (a one-shot cache specification of that frame). Those are left to the engine.
  The write of a cached parent was also a plain store of what is a counted reference in X4; the engine later
  gave back a reference the join had never taken. That fits the two joiners that stopped in a walk over a list
  holding an object already gone; it is not proven to be the cause. The store now counts as the engine does.
- Result (hq60, join at tick 6089): shared runtime IDs equal at all 714 ticks checked after the join, where they
  parted after 36 to 44 before; every digest part but the census equal at 715 ticks; of 7,994 radar posts in
  691 ticks none is the session's alone and 5 are the joiner's alone. Those five are one station and one ship
  settling on a dock right at the edge of the station's range, with a stale published attention of 6: the
  session republishes it once more, the joiner does not. A second run (hq61, join at tick 6047): runtime IDs
  equal at 601 ticks, all 9,441 radar posts the same on both clients, script entries different in one tick of
  351 (one capital ship's attack script, which resumes at another place at tick 6231 in every run). The census
  at tick 6600 there, object by object: 44 ships differ in motion and nothing else (149 before the fixes, at
  about the same distance from the join), and they are whole fleets: a battleship with its 19 escorts in one
  zone, a destroyer with seven, a carrier with four, and five single ships. So what is left is in how a fleet
  leader moves, not in the radar (found and fixed the same day: the next bullets but two). Also open: the
  join was not tried with the zone table off.
- Tried and dropped: `native_gate_join_viewf_bits=8` and `native_gate_join_pend` with
  `native_gate_join_specdrop` (the diagnostics that keep the joiner's load from republishing at all) each
  parted the runtime IDs two ticks after the join.
- Rig notes for a late join on one PC: a joiner takes its settings from the overlay's `play-recipe.json`, not
  from the host's config; the join watch and a per-object census of the joiner's first capture are slow enough
  to miss the relay's 15 s stall limit at the handover (`x4ls play --stall-drop 0` for such runs), which looks
  like a joiner that died; on a PC running six other clients the joiner catches up slower than the session
  runs and is dropped at the handover.
- What moved the fleets (18 Nov afternoon, hq63 to hq71, zone table on). The motion watch works on a joiner, so
  the 44 ships of the census went on its list with digests: every fleet's leader parted first, in its rotation
  only (target, path and flight parameters equal), in the tick it changed its sector, and every such tick lay
  within 255 ticks of the join. `move.gate` works out a ship's place and rotation behind a gate, waits six to
  eight seconds and warps it there. The two values are script values, and a script's positions and rotations
  are not saved as they are: a rotation is a matrix in memory and yaw, pitch and roll in degrees in the save,
  and of both kinds an attribute closer than 0.0001 to its default is left out. A joiner in that wait warped
  its ship with a yaw 2.4e-7 rad off. Fixed in the join (`native_gate_join_sref`, a join default, package
  section SREF): the saver notes every value its join save's two `<ref>` writers are given, the joiner's two
  readers put the session's bits back in the same order, each checked against what the text gave. Per join
  about 720 rotations, of which about 660 had other bits, and 40 positions of 12,250.
- One more of the kind: a drone on its way out of its ship is moved by a linear acceleration controller, whose
  start rotation the join's exact poses (POSE) did not cover because the census claims no motion for it. One
  XS ship made in the session came out of it 43 ticks after the join a few bits off, the one object of 11,676
  still different at the next census. POSE carries that controller's start transform now (form 3).
- Why late joiners stopped. With the first-chance crash line also outside the image (and the gate's own guarded
  reads kept apart) a joiner's stop 322 ticks after its handover was readable: the salvage search walked a
  region field's list of spawned objects and called through an object whose first word was 0xb. The join fills
  those lists with the session's objects (FOBJ), and it stored bare pointers where the engine's entries are
  counted references that the field's pass gives back when it drops an entry. So an object could go while the
  list still named it, and every dropped entry gave back a reference nobody had taken, on an object others
  still used: stops in other places too (one 560 ticks after a handover at an address outside the image, and
  it fits the two of 17 Nov). The insertion takes the reference as the engine does.
- Result (hq70, join at tick 6032, then hq71 at 6050): the census equal on both clients at 6600 and at every
  capture to 10800, the handover (8568, 8604) without a desync, no stop (alive 3,971 ticks after the handover;
  an earlier joiner without the form 3 fix ran 7,491). A join at 6086 was already whole without form 3: its
  drone had left its ship before the join.
- Open after that: in both runs the runtime IDs part at tick 11016, about 4,980 ticks after the join. The
  session starts an order script the joiner does not: one trader's `trade.find.commander` finds a trade on the
  session only (script RNG rows of 10800 to 11020; before it in that window: other draws of one mining and one
  trade routine, and `event_object_signalled` of one frigate reaching its two escorts at another place in the
  tick). Both clients' periodic saves of tick 7200, compared as bags of lines without the save's own numbering
  (`savebag.py` in the rig notes), differ in content the census does not hash: some ware amounts and trade
  logs, 577 travel factors, pending zone kill checks, about a thousand script lists the session's save holds
  and the joiner's does not. Which of these is the joiner's and which is any two clients' is not known: the
  control (two clients from the start, same saves) is not run yet.
- The split of 11016 was the scrap works. With a common save 67 ticks after the join (`native_gate_save_every`
  chosen so that one falls there) and the saves compared object by object (`savecomp.py`): the five stations'
  processing modules had run two seconds after the joiner's load and not on the session. A processing module
  keeps the time it is due next in memory only; the save holds when it last ran, so a loaded module is due at
  once, and from there the joiner's stations yield their wares at other ticks. Fixed in the join
  (`native_gate_join_pmod`, a join default, package section PMOD): the due time and the state's time of every
  processing module are carried. Run hq74 (join at 6075): 15 modules written, handover at 8584 without a
  desync, every digest part equal to tick 11999.
- Open after that: the census of tick 12000 differs in its health and motion roots (about 5,900 ticks after
  the join). And from the same comparison of saves, different and not carried: six builder ships' build
  resource shortages (the session keeps them from its start, the joiner's are gone within 67 ticks of its
  load), a pending kill check on 310 recyclables that only the joiner holds (an hour after its load), the
  travel factor of 339 ships (the joiner's start again), a travel-disabling influence on other ships (17).
  Shield groups differ in form only (a full group is written without its value on the joiner).
- The build shortages: fixed in the join (`native_gate_join_need`, a join default, package section NEED). A
  shortage is not what a load loses. A container (station, ship, build storage) keeps two maps of what it
  needs of each resource, made the first time something asks, and no save holds them. X4 brings a container up
  to date whenever something asks about it, and in that step it settles the build shortages before it makes
  the maps: with no map every shortage is dropped, and the next question puts it back with the time of that
  moment. A session's containers have their maps; a joiner's have none. Read from both clients with the
  session held (run hq79, `contcmp.py`, `contall.py`): the maps of 920 stations were unset on the joiner.
  The saver records the maps of every container that has them; the joiner lets X4's own routine make its
  maps where the session had them and then takes the session's amounts and the flag that says they are up to
  date. Run hq80 (join at 6032): 2,957 containers, 1,158 made, 1,818 set to the session's values, none with
  other wares; both clients' saves of tick 6120 hold the same 44 shortages (before: 44 and 38).
- Still open, hq80: the census of tick 12000 differs as before. Both clients' saves of tick 10200 (4,168 ticks
  after the join) differ in 560 of 44,423 objects. What is not numbering or form: 13 trade orders of eleven
  miners and traders carry the flags `sellermoneyvirtual|buyermoneyvirtual` on the joiner and not on the
  session (same wares, prices and amounts; other ships' orders at the same stations agree), nine stations'
  reference price counts are one lower on the joiner, and four ships' economy log has an offer time on the
  joiner only. The travel factors agree again by then; the kill checks of 308 recyclables are still the
  joiner's alone.
- The trade orders' flags: fixed in the join (`native_gate_join_acct`, a join default, package section ACCT).
  A container trades on an account. A ship or a build storage X4 makes while it runs takes its faction's
  account; after any load every one that does not own its account is on the default again. X4 makes a deal's
  money virtual when both sides are on one account. So a miner made during the session that sells to a
  station of the save is paid on the session and not on a joiner. Read from both clients at tick 9,000 (run
  hq82): 721 ships and build storages on a faction's account on the session, 173 on the joiner (the ones made
  after its load), each faction's account the same on both. The saver records the account of every container
  that is not on the default; the joiner's takes it. Run hq83 (join at 6065): 560 written, none unknown; in
  both clients' saves of tick 10200 the trade orders and reservations agree.
- Still open, hq83: the census of tick 12000 differs as before, with every join tick tried (6032 to 6087); what
  parts the clients happens at a game time, not a time after the join. Both clients' saves of tick 10200 still
  differ in: nine stations' reference price counts (one lower on the joiner; a count can agree again later),
  the transport drone mode of six large ships (`collectselected` on the session, `trade` on the joiner), a
  dock door close time on three small ships (joiner only), one sector's scrap yield (859,486 and 860,640),
  five ships' economy log offer time, the kill checks of 308 recyclables.
- What parted the clients at tick 12000 was one torpedo, and behind it the order of a ship's parts: fixed in
  the join (`native_gate_join_kids`, a join default, package section KIDS). With the script rows of ticks 11400
  to 12000 the first difference was one pilot's attack script at 11782, two draws apart: on the session a small
  ship fired a torpedo and its target answered with a flare, on the joiner neither. The stock attack script
  walks the ship's weapons and keeps `$firestorpedo` only when the last one is a torpedo launcher. An object
  lists its children in the order they were attached: a ship X4 builds while it runs in the build's order, the
  same ship loaded from a save in the save's order. Read from both clients with the session held (run hq86,
  `childorder.py`): 781 of 33,296 lists of two or more children in another order, all on ships. The saver
  records the order of every list with a parent or a child the session made; the joiner sets its lists to it.
  Run hq87 and hq88 (join at 6065): 786 lists set, none with other children; the script rows of 11400 to 12000
  are the same on both clients and so is the motion part of the census of 12000.
- Still open after that: the health part of the census of tick 12000 differs (not named yet). And once in six
  joins with this build (run hq89) the clients parted 135 ticks after the join: an on-foot character's
  animation started at tick 6176 on the session and 6178 on the joiner, then one more order script on the
  session at 6181. That is the walking characters' open item, not this change.
- The health part of the census of 12000 was one object: the storage module of a medium ship the session had
  built (run hq93, the object rows of ticks 11990 to 12010). Behind it the dock doors: fixed in the join
  (`native_gate_join_door`, a join default, package section DOOR). A container sets its dock doors up when it
  enters a state and takes them down when it leaves it, and a flag says which; the flag is not saved, and X4's
  load sets the doors up for every container. A ship built during the session and out of that state has them
  down on the session and up on a joiner, where they then open at a trade (both clients' saves of tick 12240,
  run hq94: a dock door close time on seven small and medium ships, on the joiner only). The saver records the
  containers whose doors are down; the joiner clears its flag there.
  The first form (the flag alone) did not do it: read from both clients with the session held (run hq97,
  `doorcmp.py`), the seven ships' flag was up on both, and what differed was the doors' map: an entry stands for
  a door that is open or closing, a session's map is mostly empty, and the set-up of a load puts one in for the
  container's own doors. The saver records the containers whose doors are down or whose map is empty; the joiner
  takes its map down there the way X4 does (and the flag where it is down). Run hq99 (join at 6065): 411 maps
  taken down, 1,500 equal, none unreadable; both clients' saves of tick 12240 hold the same 23 dock door times
  (before: 23 and 30).
- The rest of the health part of tick 12000: one shield group. With both clients paused at tick 12,012 and the
  census's health fields compared object by object (`pausejoin.sh`, `healthcmp.py`), the one difference was the
  storage module of medium ship TZN-626: its shield group read changed time 14.857 on the session and 0 on the
  joiner, with no shield in it (value and maximum 0, so the census's canonical form does not apply). A container
  keeps a shield group for each of its parts that has asked for one, and the join's SHGR section carries them;
  it was opt-in and missed this case twice over. The ship was being built in a wharf; X4's class test the
  capture used (container, 0x23) is false for a ship under construction, so none of its groups were in the
  package; and the joiner's load had made no group for the part, so there was nothing to write at the join.
  When the ship was finished (about tick 11,590) the session's part found its old group and the joiner's made a
  new one. Fixed: SHGR is a join default, tells containers by their class table, and keeps a record whose group
  the joiner lacks until the joiner's own engine makes it, then writes the session's values into it if nothing
  has touched it yet (status `shgr=on:written:equal:missing:waiting:late:left:expired`).
- Drone modes (`native_gate_join_dron`, a join default, package section DRON): a ship or station keeps a pending
  drone mode for each of its three kinds and the user's transport mode; the save writes the current modes in
  their places, so after a load nothing is pending. Read at the join (run hq106): 34 containers with a pending or
  user mode on the session, 2 on the joiner; a docked miner then switched its cargo drones to collecting on the
  session only, and seven more large ships later. The joiner now takes the session's pending and user modes.
- Wreck kill checks: every load posts each wreck a check an hour after its own time and the save keeps the ones
  before, so a joiner had 307 extra checks due an hour after it joined. The join's event section (EVQT) now
  rebuilds that event class as it does the update events: the joiner's are removed, the session's posted. A
  resource area keeps its own kill check, which the save holds whole; the join cannot look its event source up,
  so the joiner keeps the one it loaded (a first build removed it, run hq114). A source without a portable ID is
  recorded by its runtime ID, which the joiner's load keeps.
- Result, run hq114 (join at 6070, everything above on): handover at 8839, then no desync to tick 13,447 (the
  relay's desync record empty), every digest part equal from 6600 on, the census of 12000 equal in all 11,240
  objects. Run hq115 (join at 6075, the final build): no desync to 13,422, the census of 12000 equal; both
  clients' saves of tick 12240 differ in 89 objects instead of about 540, none of them in shield groups, drone
  modes, dock doors or kill checks.
- Prices (`native_gate_join_refp`, a join default, package section REFP): a station keeps its reference and override
  prices in hundredths of a credit, and the save writes them in whole credits, so a joiner's were up to a credit
  off (14 entries of mined wares apart in both clients' saves of tick 8160, run hq115). The saver records every
  price with hundredths; the joiner writes it where its own holds what a load of the save gives. Run hq117 (join
  at 6065): 164 prices written, all 1,221 reference entries equal at tick 12240, no desync to 13,429, 73 objects
  apart in the saves. Soak hq116 (the build before, join at 6050): no desync to tick 20,051.
- Field-spawned wrecks (`native_gate_join_spwn`, a join default, package section SPWN): a wreck or piece of debris
  drawn from a region's field remembers the field and the resource area its scrap comes from; when it enters a
  space, its scrap comes off that area. The save keeps only the region, so a loaded one remembers neither, and on
  a joiner nothing came off: wreck SBE-256, at the join in sector NSG-027, entered its space at tick 7197 and took
  1,154 from the scrap field there on the session alone (runs hq117 to hq124; a read-only poller on both
  clients). The saver records each such object's field and area by their places in the region's and the
  sector's lists; the joiner writes them into its loaded copy.
- Collisions (`native_gate_island_order`, a default, every client): a soak joined near tick 6050 stayed in sync to
  22,199, then the census of 22200 parted in one Paranid transport docking at a station. Its four contacts with the
  station's dock area were the same on both clients (bodies, normals, depths, points), yet solved in another order
  at tick 21921, and the ship's pose came out 1.7 cm apart. Jolt sorts each island's contacts by a hash of the bodies'
  IDs, which come from the body manager's free list in creation order: after the join 61 of 71 live bodies had other
  IDs on the joiner. The sort now runs on keys every client computes alike: the bodies' owners (portable ID and which
  physics record), the contact normal and the points. Run hq130 (join at 6074): no desync to tick 22,933, the
  transport equal throughout; six sorts on each client, no unkeyed body, no tie.
- Door maps that are not empty (package section DKEY, with `native_gate_join_door`): a container's map of open or closing
  doors gets an entry for the container itself at every load. DOOR takes the joiner's map down where the session's is
  empty, but left it where the session had an open door: Paranid frigate MVX-838, built eight seconds before the join,
  held one entry (a bay) on the session and that and its own on the joiner, and both clients' saves of tick 24000 held
  a door close time on the joiner only (run hq131). The saver now lists the keys of every map that is not empty, and the
  joiner removes the entries the session's map lacks through the game's own removal of a door entry.
- Deferred events at the join tick (package section DEFR, `native_gate_join_defr`, a join default): work that runs off
  the main thread, such as the transport drones' flights, posts its events onto a list the next tick hands to the event
  queues. The mass-traffic pass ends at the tick boundary, so the drones that land in the last pass before the join have
  their events on that list when the package is captured, and neither the save nor the event queue capture holds them. A
  join at 6058 caught the six flights of Paranid miner DIH-881's ice sale to DPR-229 landing there: the session finished
  the trade one tick later and undocked, while on the joiner the flights were gone, the trade started over and the
  runtime IDs parted at 6225 (runs hq136, hq152). The saver now records those posts (cargo arrivals by the trade's seller,
  buyer and partner, since a load renumbers trades; drones returning home), and the joiner posts them again at its
  admission. Run hq153 (join at 6058): no runtime ID split to tick 6713; both clients' saves of tick 6062 hold the trade
  complete (hq155; without DEFR, hq156, the joiner's still had 2709 of 3709 to move).
- Travel drive ramps (ENGR, a join default): an engine's travel factor ramps toward its target over the time since its
  last engine refresh, and a load restamps that time, so the first refresh after the join ramped the session's ships
  over seconds and the joiner's over two ticks (26 ships in travel at a join at 6058; BJL-112 at 3.37 on the session
  and 8.73 on the joiner four ticks later). ENGR, which carries the boost ramp for the same reason, now carries the
  travel ramp's time and factor too (run hq160: the engines equal). The first world capture after that join still
  differs by twelve dead temp zones the session has not deleted yet; from the next capture (6600) on the world parts
  are equal.
- A join at tick 6046 used to part the clients at 6182 every time (runs hq89, hq111). With DEFR and the travel ramps
  it no longer does (run hq163: runtime IDs equal to 6758, world parts equal from the 6600 capture), and neither do
  joins at 6040, 6049, 6052, 6055, 6061, 6064, 6067, 6070, 6076 and 6082 (runs hqs6040 to hqs6082, each to about 6800).
- People's running animations (package section BCLK, `native_gate_join_bclk`, a join default): the gate's body clock
  (`native_gate_body_clock`) ends a person's animation by the simulation time, from a note it makes when the
  animation is played. A joiner's load plays every person's current animation anew (the save keeps only its name)
  and the joiner had no notes, so those animations ended by its load time: at a join at 6046 two seated pilots played
  their next animation at 6111 and 6176 on the session and both at 6178 on the joiner, and stayed out of step. No
  digest part holds animations, but what follows from an animation's end (the people's scripts) can. The saver now
  sends the body clock's notes and the joiner takes them at its admission. Run hq165 (join at 6046): every
  animation play from 6111 to 6800 is the same on both clients (tick, motion, start offset), 21 notes taken.
- Still open: ships' economy log offer times and mission cue numbers still differ in the saves.
- Rig notes: a test overlay holds its own copy of the probe scripts, so a new config key needs them replaced
  there (and the manifest's lines), or the saver never arms it while the joiner does; the motion watch's
  `slotx` group hashes a vector's unused fourth lane and a padding byte, which hold leftovers on the session
  and 0 on a joiner (one ship of 44 differed in it from the join on and in nothing else).
- A joiner started on the same PC beside freshly started clients of other sessions twice stayed at 90 MB without
  a log (hq20, the first control run). `x4ls join --same-pc` now frees X4's instance lock in the other launcher
  clients before it starts its client, as `x4ls play` does; in the second control run it freed one and the joiner
  started.
- A player can begin with a headquarters, as the stock starts that come with one (17 Nov):
  `--plot K=hq_ready` gives player K the copy of the plot begun the way the game's own custom starts with the
  headquarters story done begin it: the copy's first step makes player K's faction's headquarters at a safe
  position in Grand Exchange, with the mentor in it and research unlocked, and the signal leak never begins.
  `--plot 1=hq_ready` does it for player 1, whose plot is the stock script: 90 ticks into the session the script's
  own cues make the station with the mentor and open research, and its signal-leak phase is cancelled (the script
  handles that itself). That form is allowed in coop: the session begins with the one headquarters the players
  share. Once per game (a resumed session and a joiner do not repeat it), and a faction that has a headquarters
  keeps it. The stock starts that come with a headquarters (their player begins on foot inside it) are still not
  selectable as a player's start; any other start can be combined with this.
- Run hq22 (17 Nov, coop, two clients, 4,415 ticks in sync, 0 errors): the session began with the shared
  headquarters; player 1 and then player 2 started a research at it by script; both went onto the one shared list
  (4, 5, then 6 wares on both clients) and both clients' menus see both.
- Run hq23 (17 Nov, factions mode, `--plot 1=hq_ready --plot 2=hq_ready`, 5,637 ticks): two headquarters from the
  start, player 2's faction's and player 1's, about 17 km apart, the same in both clients' saves; each player
  researched at its own, each list holds only its own, each test mission heard only its own notices; read from
  the save, player 1's plot has its signal-leak phase cancelled and research open, player 2's copy never began
  the signal leak and has research open. One thing to watch: the clients' digests differed for 36 ticks (1870 to
  1905) and then agreed again by themselves; the repeat run hq24 with the digest parts logged did not show it
  (7,679 ticks without a mismatch), nor did hq25 to hq27, so its cause is not known.
- A faction's library notices stay with that faction (17 Nov). The scripts that belong to nobody hear every
  player, and they took player K's "research unlocked", "production started, finished, cancelled" and
  "blueprint added" as the one player faction's: the stock research stories, the diplomacy script, the
  notifications and the tutorials would have acted on player 1's headquarters and list. Those five notices of a
  player with an own faction now go to that player's own missions and plot only (run hq25: six kept on both
  clients, each player's test mission still heard its own). Coop is unchanged.
- With `--plot K=...` the stock plot is player 1's own in every respect (17 Nov; before, it only no longer heard
  the others): its missions are in player 1's mission list only, and what it shows and says goes to player 1's
  client. Run hq26 had shown player 1's research mission in player 2's list too; in hq27 it is in player 1's only.
- Player 2's own plot by script, as far as a script can take it (run hq27, 17 Nov, 16,167 ticks in sync, 0
  errors): the copy was put into its claiming phase by the stock script's own shortcut. The mission is in player
  2's list only; the mentor's lines end for player 2's plot; player 2's ship, put at the three spots near the
  station by a test step, passed all three checks; the station changed to player 2's faction by the plot's own
  step (it already was that faction's in this test, because the shortcut makes it so); the plot went on to its
  "build a dock" tutorial and gave the wares for it. Read from the save, cue for cue. From there a person is
  needed: the dock is built in the station menu, then the plot wants a landing and the lift.
- What the game does outside the menus with "the player's" blueprints (read from the code, not run): the
  question "what can this shipyard, wharf or equipment dock build for faction F" uses the player's blueprints
  only when F is X4's own player faction. For player K's faction it answers as for an NPC faction: the wares of
  the customer's faction, by licence. The menus are right, because they ask inside player K's faction view. So a
  shipyard of player K would build for NPC customers without regard to player K's blueprints, and automatic
  orders of player K's own ships there may find nothing to build. Not changed; it needs a player-owned yard to
  show itself.
- Also seen: "Acquire a manager", the upkeep mission for a station without one, stood in both players' lists
  when each had a headquarters. Whether that is one mission shown to both or one per station was not looked at.
- Open: the plot played by a person as player K from the signal leak on (the leak, the anomaly, the dock built in
  the menu, the landing); the Terran headquarters story (it hands over the one station the galaxy has for it, so a
  copy for a second player needs its own station and its own copy of the main plot; not built); other stock
  stories keep reading player 1's plot (they are player 1's or the group's); `x4ls resume` still gives players
  2..N their start's library again.

### Several players on one start, and the story after a start's story (built 24 Nov; offline tests only, not run in the game yet)

The stock scripts keep one state per story, so a start's story script can be one player's only. Until 24 Nov a second
player on a start with `--story` was refused ("another player has the story"), and a session whose base start was that
start could give its story to player 1 only. Now each further player on that start plays the story as their own copy of
the script, for any players 2 to 8:

- Player 1 plays the base start's story with the stock scripts, as before. A player 2..N whose start is another start
  claims that start's stock script, as before, when they are the first player on it. Any player 2..N whose start is
  the base start or an earlier player's start gets a copy named after them, e.g. `GS_Terran1_P3`. Every PC makes the
  copy from its own installed game when it composes its client. The overlay and a kit carry only the recipe
  (`md-copies.json`: file, player, hashes), never Egosoft's text. These are the same rules as for the headquarters plot
  copies above.
- A copy is the stock script with these changes. It gets its own name, and its references to itself follow the name.
  Its root cue waits for its own signal (begun inside that player's scope 60 ticks into the session, or 30 ticks after a
  join) instead of the start's setup cue. It has no `module=` test, so it runs in a session of any base start. The
  start's other root cues that carry `module=` are left out: in the cadet script that is a fix for relations in old
  saves at every load, which the stock script runs once for the game when the base start is the cadet start. The other
  root cues wait until the copy has begun. Debug force names are removed. In factions mode the copy names the player's
  own faction where the script names the player faction. `player.module` is written as the player's start, so every
  test of "the game's start" in the copy is answered for that player.
- What a copy does not run: the gamestart's setup cues in `md.Setup_Gamestarts`. For the cadet start these are the
  Terran headquarters' protection in cluster 114 and the flag that tells the headquarters plot it is a Terran game.
  They belong to the game and run once for the base start. In a session whose base start is not the cadet start they
  never run. They are among the start's `world_omitted` parts.
- **The Terran story after the cadet story** (`Story_Yaki`, the Solborn militia and the Yaki, with the cadet's
  wingmate). In the 24 Nov playtest player 2 finished the cadet story, but this story never offered its first mission.
  Its stock script asks whether the game's start is the cadet start, and that is the session base start, player 1's.
  It then waits for player 1 in a Terran sector, and it listens for the stock cadet script's last step. Now a player who
  takes the cadet start with `--story K` also gets `Story_Yaki_P<K>`, by default. In that case the cadet story is
  always the player's own copy, even for the first player on the start, so that the two copies can name each other:
  `GS_Terran1_P<K>` takes the actors and the wingmate from `Story_Yaki_P<K>`, and `Story_Yaki_P<K>` waits for
  `GS_Terran1_P<K>`'s last step. The copy then offers its first mission when player K enters a Terran sector with
  enough Terran reputation, or ten minutes after the story is activated if player K is already in one.
  `--omit K=yaki` leaves it out. That player then has the story alone, and the first such player claims the stock
  cadet script.
- The stock scripts of every copy in the session (`GS_Terran1`, `Story_Yaki`) are player 1's alone (the overlay's
  `native_gate_plot_first`). Their missions appear in player 1's list only, what they show and say plays on client 1,
  and what players 2..N do no longer advances them. A stock script that a player 2..N claims stays that player's. In a
  session on the cadet start, client 2 therefore no longer hears player 1's wingmate once player 2 has a copy.
- Diagnostic without playing the cadet story through: `--set "native_gate_plot_debug={{900,202}}"` signals the
  activation step of player 2's `Story_Yaki_P2` at tick 900, the step that the cadet story's end signals. The code is
  the player's slot + 100 × the copy's place in that player's list: the story is 1, `Story_Yaki` is 2. For player 3 the
  code is 203. Code 101 is the stock script's, player 1's (201 with a headquarters copy in the session). The offer
  then comes at player K's next Terran sector. The same code plus 1000 (1202, 1203; 1101 for player 1's stock
  script) runs the story's own wingmate setup: the wingmate is made or brought to that player's ship and protects it.
- **The cadet's wingmate for players 2..N** (fixed 24 Nov, `tools/dev/native_gate_player_escort.xml`). The
  wingmate protects its player's ship with X4's ProtectShip order (`aiscripts/order.fight.protect.ship.xml`), and
  the copy gives it the right ship (player K's). But the order reads the protected ship's pilot: with a pilot the
  wingmate flies in formation behind the ship, keeps an escort group on the pilot and joins in when "the player's
  ship" attacks something. A stock player's ship holds its player in its pilot post all the time; a player 2..N's
  ship holds its player only inside that player's scope, and the wingmate's script runs outside every scope. So
  player 2's wingmate had a ship without a pilot: it flew loosely after it, never answered player 2's shots,
  attacked every armed enemy it saw (X4's rule for a ship without a pilot) and logged "Property lookup failed:
  $leaderpilot" at sector changes (rk8, on both clients). Now the session's player groups also keep, on every
  player's entity, which player controls which object (`$X4LAN_Pilots`), and the patch takes that player as the
  ship's pilot and that ship as "the player's ship" where the stock order has none. Player 1's ship has its own
  pilot, so its wingmate does what it did; without the gate the table is never set. The wingmate's colour does not
  change: X4 colours a target green only when the player's faction owns it (`GetRelationStatus4`, "own property"),
  and the cadet's wingmate is the Terran faction's ship in the stock game too (player 1's wingmate in the rk7 and
  rk8 saves: `owner="terran"`), so it is blue ("Friendly") for player 1 as well. Player 1's own ship is green on
  player 2's screen because in coop it belongs to the shared player faction.
  Test ops (in a slot's plan, `native_gate_ui_replay_script`): `escort_look` (canonical, MD `EscortLook`) logs
  `ESCORT_LOOK slot= ship= pilot= pilots= guards= guard=<id>/<owner>/<order>/<wingman|loose>/<leader>/<distance>/<target>`
  for every slot's ship; `escort_target` puts a Xenon scout without a pilot 500 m ahead of this player's ship as
  this player's target (`ESCORT_TARGET`); `escort_colour` (this client's HUD) logs X4's relation status of each slot's
  ship and its protectors (5 own property = green, 4 allied, 3 friendly, 2 neutral = blue), the flags the map's lists
  colour by (`own=`, `mission=`, `enemy=`, `hostile=`) and the HUD's faction-colour option (`factioncolour=`);
  `escort_front` (canonical, MD `EscortFront`) puts a slot's protectors 150 m ahead of its ship, so its player's camera
  shows them; `escort_select` makes this player's first protector the target (target box, target monitor);
  `escort_map` (the open map, read only) logs the colour the map's lists and info header give that protector, after
  closing the question the map may open with.
  Live (24 Nov, one X4: `x4ls play --solo 2` on the rk8 session, both players on the cadet start, plot debug 1202 and
  1101 at tick 900): outside every scope player 2's ship had `pilot=null` and the table named player 2
  (`pilots=0x1639b0`); after the setup both wingmates protected their own player's ship as formation wingmen
  (`guard=.../terran/ProtectShip/wingman/leader=ship/...`); player 2 flew off and its wingmate stayed 220 to 650 m
  away in formation; a Xenon scout put ahead of player 2 and shot by player 2's guns (`aim_fire`) made player 2's
  wingmate attack it (`guard=.../Attack/loose/leader=null/343.495/RER-226`) and then go back to the formation; on
  player 2's HUD both wingmates were status 3 (Friendly, blue: `guard:...=3/terran`), both players' ships 5. No X4
  script error.
  Player 1's own view, X4's own player with the stock scripts (24 Nov, `x4ls play --solo 1`, the same session): the
  stock wingmate "Velvet Thunder" put 150 m ahead (`escort_front`) and made the target (`escort_select`) has an azure
  target box, azure icons and azure distance and speed text on the HUD (RGB about 64,176,240), and an azure name in
  the target monitor; `escort_colour` gives `guard:252391=3/terran/own=false/mission=false/enemy=false/hostile=false`
  with `factioncolour=-1` (relation colours). The target monitor names it "Terran Protectorate, Friendly +10". On the
  map its icon is blue beside the two players' green ones, and `escort_map` (the map's own colour functions) gives it
  `list=255,255,255 info=255,255,255` (white) against the map's `player=77,255,77`. The stock game shows the cadet's
  wingmate blue, so player K's blue wingmate is the stock look.

```
# player 1 on discover, player 2 on the Terran cadet start with its story and the Terran story after it
python tools/x4ls.py new-session --base-start x4ep1_gamestart_discover --players 2 --mode coop \
    --start 2=x4ep1_gamestart_terran1 --omit 2=world --story 2 --gate-dll <x4lan_gate_64.dll> --out .local/sessions/cadet-p2
# players 1 and 2 both on the Terran cadet start, each with the cadet story (and each with the Terran story after it)
python tools/x4ls.py new-session --base-start x4ep1_gamestart_terran1 --players 2 --mode coop \
    --start 2=x4ep1_gamestart_terran1 --omit 2=world --story 2 --gate-dll <x4lan_gate_64.dll> --out .local/sessions/cadet-both
# three players on the cadet start (players without --start take the session start's ship and, with --story, its story)
python tools/x4ls.py new-session --base-start x4ep1_gamestart_terran1 --players 3 --mode coop --story 2,3 \
    --gate-dll <x4lan_gate_64.dll> --out .local/sessions/cadet-three
```

Each one ends by printing its `x4ls play` line. The `new-session` log names each player's copies
(`its own copies: story=GS_Terran1_P2, yaki=Story_Yaki_P2`). In the game, look for `STORY_MD slot=K plot=story` and
`plot=yaki` rows, `MPP_STORY ... signalled cues=2` and, on every client, the copies' cues claimed for player K.

What only a live run can show: that X4 loads the copies beside the stock scripts; that the cadet copy's wingmate is the
one `Story_Yaki_P<K>` hands over; that player K's sector change reaches the copy's offer cue; that the offer is in
client K's list only; and that two cadet stories in one session stay in sync. Open: some state is still one per game.
The copy writes its contact actor into the global table of persistent characters, and the stock script writes the same
entry. The two "do not disturb" lists of the Terran story are global, and so is the game's own story state "Yaki story
complete". Later chapters of each copy make the story's own objects again for their player (the Terran outpost, the
Yaki leader and their ships) and repeat its faction-wide steps (Yaki jobs switched off, relations set), as the
headquarters copies make a second station. Whether that is what a group wants has to come from a playtest. The
scripts that read the Terran story's state go on reading the stock one, player 1's:
`Story_Covert_Operations` (after the Terran story's chapter 9), `Story_HQ_Discovery` and `Story_Terraforming`.
`x4ls resume` keeps the copies with the saved world but does not claim them for their players again (the same as the
headquarters copies).

Other start stories that other stock plots continue, found in the scripts (not built). Each would need a plot entry
with `follows` in `tools/dev/native_gate_plot_copies.py`, and some need more than that:

- Pioneer start (`x4ep1_gamestart_terran2`; players 2..N cannot take it yet, because it starts on foot):
  `Story_HQ_Discovery` (it and `GS_Terran2` name each other; it tests the start) and `Story_Terraforming` (tests the
  start). The headquarters discovery hands over the galaxy's one Terran headquarters, so a copy would also need its own
  station.
- Boron starts (`boron1`, a full-screen cutscene; `boron2`, on foot): `Story_Boron` (36 references from `GS_Boron2`;
  `GS_Boron1` and it name each other; it tests the start) and `Story_Boron_Prelude`. `boron2` also uses the
  headquarters plot.
- Pirate starts: `pirate1` (a cutscene) with `Story_Pirate_Prelude` and `Story_Thefan`, which read `GS_Pirate1` and
  test the start; `pirate2` (on foot) with `Story_Criminal` (the two name each other) and `Setup_DLC_Pirate`.
- Split starts (`split1`, in a spacesuit; `split2`): `Story_Split` tests the start (three tests); there is no direct
  reference between the scripts.
- `x4ep1_gamestart_hyperion`: `Story_Hyperion` (one test of the start). `x4ep1_gamestart_dlc_mini_02`:
  `Story_Unbihexium` (twelve tests and one root cue tied to the start). `x4ep1_gamestart_scientist`: the
  headquarters plot.
- The cadet start is also tested by `Story_Boron`, `Story_Thefan`, `Story_HQ_Discovery` and `Story_Terraforming`.
  Those stay player 1's answers.

### Milestones: go on from the last completed mission (built 24 Nov; offline tests only, not run in the game yet)

A story is long: the Terran cadet story and the Terran story after it are 25 chapters. Every time a player completes a
chapter, every client saves the game at the same moment (an info line "milestone terran_cadet-M3-slot2 saved"), and
the next game can start from there with every player where they were.

- Which ones: `python tools/dev/native_gate_milestones.py list`. M1 is the cadet's satellites (the distress call
  begins), M2 the cadet story complete, M3 to M25 the chapters of the Terran story (escort, mines, patrol, ... the Yaki
  trade mission). The name says whose: `terran_cadet-M3-slot2` is player 2's third.
- The save waits while player 1 is on foot (it is made when player 1 sits again), and briefly while others are; X4 may
  hold it during a call. It is not made in a game with a Linux PC or open slots.
- After the game the run folder has `saves/milestones/<name>.xml.gz` and `<name>.json`. Keep both. To go on:

```
python tools/x4ls.py continue --milestone .local/runs/<time>/saves/milestones/terran_cadet-M2-slot2.xml.gz \
    --gate-dll <x4lan_gate_64.dll> --out .local/sessions/cadet-m2
python tools/x4ls.py play --clients 2 --overlay .local/sessions/cadet-m2/overlay --snapshot .local/sessions/cadet-m2/session-save.xml.gz
# netplay: host the continued game (joiners need nothing else)
python tools/x4ls.py host --milestone .local/runs/<time>/saves/milestones/terran_cadet-M2-slot2.xml.gz
```

- Every player's ship starts at rest: travel drive and boost off, no speed, the ship computer's autopilot off (a game
  saved on the way somewhere does not fly on before the player looks; `--keep-flight` keeps it flying).
- What comes back: player 1 exactly as saved. Players 2..N in the ship they flew (or last sat in), with their
  inventory (and in factions mode their research and blueprints), their story where it was and their missions their
  own again (by their scripts' names, before the first tick). What does not: the mission each player 2..N had selected
  (select it again in the mission menu), a mission a player 2..N had taken from a shared script such as a generic
  mission (everyone's after a continue: open, LAUNCHER.md), and what a player 2..N had learned in the encyclopedia and
  on the map of their own. The gate must be one that names a story for its slot (built 24 Nov or later: the probe
  refuses an older one at load, `native_gate_plot_owned: this gate names scripts for slot 1 only`).
- Any other save of the session goes the same way: a periodic one (`--save .local/runs/<time>/saves/c1-t18000.xml.gz`)
  or one a player made (`--save .local/runs/<time>/saves/player/<name>-t<tick>.xml.gz`). A periodic save from before
  25 Nov has no `.json` beside it: give the session's `session.json` and a folder with its overlay's `play-recipe.json`.
  Its players 2..N come back from their saved player objects (when they sat in a ship) and their stories by name:

```
# rk8 (24 Nov): player 2 had finished the cadet story; the periodic save of tick 18000 of player 2's client
python tools/x4ls.py continue --save .local/rk8/c3-t18000.xml.gz --session .local/rk8/session.json     --overlay .local/rk8/overlay --gate-dll <x4lan_gate_64.dll> --out .local/sessions/rk8-t18000
```

Live (24 Nov, two clients on one PC): rk8's save continued in sync for 10,841 ticks (player 2 in its saved ship
SIS-084 with its two wares, identity applied, a person's save and three periodic saves collected with their sidecars);
a periodic save of that game continued again with the stories named for slot 2 (`PLOT_OWNED_ARMED ...
GS_Terran1_P2=2,Story_Yaki_P2=2 ... owned=2` on both clients, 7,241 ticks in sync).

**Live check for the coordinator** (steps 4 to 6 run on 24 Nov with rk8's save, see above; every row named here is
written by this build):

1. A session with player 2 on the cadet start and its story:
   `python tools/x4ls.py new-session --base-start x4ep1_gamestart_discover --players 2 --mode coop --start 2=x4ep1_gamestart_terran1 --omit 2=world --story 2 --gate-dll <dll> --out .local/sessions/ms1`.
   Its overlay must have `md/x4lan_milestones.xml` and `native_gate_milestones = {save=true,...}`.
2. Play to M1 as player 2: `python tools/x4ls.py play --solo 2 --overlay .local/sessions/ms1/overlay --snapshot .local/sessions/ms1/session-save.xml.gz`
   (or two clients without `--solo`). Shortcut for the test: the cadet's first chapter is the four satellites;
   play them through.
3. Rows that prove the save (`client2/x4ls-c2.log`; with two clients the same on both): `MILESTONE_MD name=terran_cadet-M1-slot2`,
   `MILESTONE reached name=terran_cadet-M1-slot2 ... due=C save=T` (the same C and T on every client), `SAVE_SLOT
   ... slot=2 player=... ship=... inventory=...`, `SAVE_OWNERS name=... kept=N failed=0` (N >= 2: the two story
   roots), `MILESTONE_SAVE armed` and `MILESTONE_SAVE saved name=terran_cadet-M1-slot2 tick=T`, then on the console
   `milestone terran_cadet-M1-slot2 (tick T), client 2: kept as terran_cadet-M1-slot2.xml.gz`. No `MILESTONE_SAVE_FAILED`
   (one row when X4 first refuses the save; `WARN MILESTONE_SAVE_RETRY` rows after it are not in `errors.log`),
   `MILESTONE_SAVE_GAVE_UP` or `MILESTONE_LATE` in `errors.log`, and no desync in `desyncs.log` after T. With two clients the sidecar lists both
   clients' hashes (they may differ in view-local parts: `tools/dev/checkpoint_diff.py`).
4. Continue: `python tools/x4ls.py continue --milestone <run>/saves/milestones/terran_cadet-M1-slot2.xml.gz --gate-dll <dll> --out .local/sessions/ms1-m1`,
   then `play` it as in 2. Rows that prove each player kept what was theirs:
   - ship: the plan's ship step `slot=2 ship=<the slot's ship from the sidecar>` and player 2 in its ship at its saved place;
   - stories: `PLOT_OWNED_ARMED ... scripts=GS_Terran1_P2=2,Story_Yaki_P2=2 ... owned=2` before the first tick;
     player 2's mission list shows its story's mission, player 1's does not;
   - other missions: `SAVE_OWNERS dump ... signalled at=2`, `SAVE_OWNERS_MD dumped=N`, `SAVE_OWNERS restored
     ... heard=N of=N claimed=C refused=0 unreadable=U` at tick 32 (N = 0 from a save without a snapshot; U: the saved
     cues X4 gives Lua without an ID, open, see LAUNCHER.md);
   - story state: no `STORY_MD` begin row (the story is not begun again), `MILESTONE_MD` rows only for chapters after
     M1, and the next chapter's speech plays on client 2 only;
   - inventory: `CONTINUE_INVENTORY ... slot=2 ... applied=1 now=<the sidecar's wares>`;
   - player 1: its own ship, money and inventory as at the save (X4's own save).
   A `SAVE_OWNERS_FAILED` row (`heard` < `of`, or a readable entry `refused`) names what came in
   `first=<slot>:<type>:<value>:<id>:<claimed>`.
5. A person's save (any session overlay built since 25 Nov): in a two-client game press F5 on client 2 (or Escape,
   Save game, a slot). Rows on client 2: `PLAYER_SAVE asked from=key file=quicksave name=quicksave slot=2 ok=true`
   (`from=menu file=save_00N name=<typed>` for the menu). On every client: `SAVE_REQUEST_MD request=2|quicksave|quicksave`,
   `PLAYER_SAVE reached name=quicksave slot=2 ... due=C save=T` (the same C and T on both), `SAVE_SLOT name=quicksave-tT`,
   `SAVE_OWNERS name=quicksave-tT ... failed=0`, `PLAYER_SAVE armed`, `PLAYER_SAVE saved name=quicksave-tT tick=T ... slot=2
   save=quicksave`; the console line `save quicksave-tT (tick T), client 1: kept as quicksave-tT.xml.gz`; and the run's
   `saves/player/quicksave-tT.json` with `asked_by: 2`. Client 2 alone shows "game saved (quicksave)"; no desync after
   T. A `PLAYER_SAVE asked ... ok=false why=not_local` row means the key's request was not taken as a click: the probe's
   frame is not in a local phase there (S.click), and the key path needs another way in. Then
   `x4ls continue --save <run>/saves/player/quicksave-tT.xml.gz ...` as in 4. F9 and Load game in the menu: one line
   "loading a saved game is off during a session", nothing loads.
6. The periodic saves are prepared the same way (`SAVE_SLOT name=periodic-t18000`, `SAVE_OWNERS name=periodic-t18000`
   before `PERIODIC_SAVE armed tick=18000`) and have `saves/c<K>-t18000.json` beside them: `x4ls continue --save
   <run>/saves/c1-t18000.xml.gz` works without anything else.

### A joiner picks a start (built 10 Nov; not in lockstep yet)

A player who joins a running game takes one of the session's open slots, and each open slot plays the start the host
gave it when the session was made. So the host decides which starts are on offer, and a joiner picks one of those
that are still free:

```
python tools/x4ls.py new-session --base-start x4ep1_gamestart_discover --players 4 --mode coop \
    --start 2=x4ep1_gamestart_fight --start 3=x4ep1_gamestart_split2 --start 4=x4ep1_gamestart_terran1 \
    --omit 2=inventory,reputation --omit 3=world,inventory,reputation --omit 4=world,inventory,reputation \
    --open 2,3,4 --gate-dll <x4lan_gate_64.dll> --out .local/sessions/four
python tools/x4ls.py join --overlay <copy of the overlay> --starts          # the open slots and their starts
python tools/x4ls.py join --relay <host>:<port> --token <t> --overlay <copy of the overlay> --start x4ep1_gamestart_terran1
```

- `--start` takes the first free open slot that plays that start; when another player has it, the next slot with
  the same start is asked.
- Ship-only starts (`x4ls starts`: `player-start=ship-only` or `ship+story`, chosen with `--omit K=world`): the player gets that
  start's ship with its loadout, its place and its money, not its story. The session's world is the host's start.
  With the stock game that makes seven starts a player can take: discover (Argon scout), fight (Paranid fighter),
  split2 (Split fighter), terran1 (Terran fighter), trade (Teladi scout, 200,000 Cr), intro and tutorial (Argon
  fighters). A start that begins docked or on foot (trade, intro, tutorial) gets its ship in open space 3 to 8 km from
  that start's station. Starts without a ship of their own, in a spacesuit, or whose loadout this reader cannot build
  are not offered.
- ns4 (10 Nov): a session with discover, fight, split2 and terran1 was captured in 3 minutes; the three extra ships
  stood in Paranid, Split and Terran space, and the overlay lists them as open starts. ns5: the same with trade,
  intro and tutorial, their ships placed near those starts' stations. Starts in more than two sectors need nothing
  extra: the session defaults cover eight sectors. Neither session has been played yet.
- State (10 Nov, two X4 on one PC). The join itself always runs to the end: the joiner loads, catches up, takes
  the slot of the start it asked for, both play on, 0 errors, and every player's ship is at the same place on both
  PCs. It is not in lockstep yet for a start in another sector than the host's:
  - Object IDs: fixed. The joiner raised the far sector to the session's attention level after its ID counter was set
    and handed out 9,119 IDs the session never did; it now takes them from its own list (hjs1: the ID rows equal from
    the first tick after the join).
  - The reference ships: fixed (10 Nov). A ship in an evasive spiral at the join stopped early on the joiner, and a
    full shield's timestamp differed. hjs4 now: all twelve equal at every sampled tick from the join to tick 5400.
  - Still differing after such a join (hjs4, 10 Nov midday, four runs alike): the world census is equal at the
    join, at tick 1800 and at 3600, which is after the handover; the per-tick report differs from 51 ticks after the
    join in one part only, the navigation mesh receipt (the joiner built 17 meshes where the session built 25); and
    at tick 5400 one of the census' four parts differs. The session counts these as desyncs. In the join code, in
    work. The cause of the mesh difference is found: two animations near the joiner's start were running at the
    join; the session had started them 12 and 6 ticks before it, the joiner's load starts their clocks anew, so
    their end, and the mesh rebuild that follows it, comes 13 and 7 ticks later on the joiner. The join does not
    carry an animation's start time yet.
  - The standard playtest session (every player next to the host) joins clean with the same build.
  - Traffic near stations (hjs1, the fight start, 10 Nov): X4's background traffic is in no save, so a joiner made its
    own at its load. It showed when the joined player's ship was near a station: a few ticks after the handover traffic
    ships became real objects, nine on the session and eight on the joiner. Since 10 Nov every client restarts every
    zone's traffic in the same tick at a join, with X4's own rebuild. With it the same ships appear on both sides and
    the per-tick comparison stayed equal for 240 ticks after the handover (it parted after six before). Not finished:
    at the next world check eight of those small ships still differ in their motion. Seen by players: traffic near
    every player starts again when someone joins.
    State 11 Nov, after eight more runs of the same join: still open. Found and fixed: each zone's traffic has a
    0.2 s step whose phase the session kept from its start and the joiner had from its load (two ticks apart); it is
    put back at the restart and now falls on the same ticks. Not fixed: when the joined player's zone switches its
    traffic on (a few ticks after the handover, the same tick on both PCs, from the same eight docks and the same
    random state), the session builds 942 traffic lanes and the joiner 909: the first 431 are equal, the rest
    connect other waypoints inside the stations. The ships that then fly them differ, and one small ship more appears
    on the joiner; the comparison parts five ticks after the handover or at the next world check. Ruled out by runs:
    the published view caches the lane build reads directly (parents and transforms from the simulation instead
    changed nothing). Narrowed down from the lane lists: the zone has six traffic sources (three stations and their
    build storages); five build the same lanes on both PCs, all of the difference is in one station (371 lanes on
    the session, 338 on the joiner), and that station is still under construction in the session's save (45 planned
    modules, 44 built). So it is state of a station being built that the two PCs do not share after a join, not
    traffic as such; stations that are complete are equal. Open: which state. Diagnostic rows for it:
    `native_gate_viewpoint_policy_rows` sites 113, 116, 117 (the lanes a switch-on built).
  - Eight slots (11 Nov): a far start on the eight-slot session (slot 3, the fight start) also joins and plays. A
    squad of ships near that start (a corvette and five fighters) flew apart between the two PCs at another tick
    each run. That was not the join: the same session with both players present from the start and no join at all
    did it too (two runs of two). Cause: X4 updates its pathing agents in parallel jobs, and one agent's update can
    read what another agent's job writes in the same pass. Cure, a session default since 11 Nov: the jobs run one
    after another in a fixed order (`native_gate_travel_determinism`). With it five runs of five were equal at every
    world check (every 300 ticks to tick 5400), and a far join was equal from the join to the handover. Cost on one
    PC with two X4: about 1 ms more per pathing pass. A fifteen-minute run of the same session without a join (two
    X4 on one PC, 11 Nov) was equal at all 16 world checks to tick 27,000, with 0 errors. Ruled out on the way, by counts in a run: the published
    parents that pathing walks (0 of 2.3 million differ from the live ones), and the formation code's transforms
    between two zones (`native_gate_path_transforms = 'observe'` compares them, `'all'` answers them from the
    simulation; the default stays `'sim'`).
- Money in coop: the wallet is shared, and every slot's kit is applied when the session starts. So that the host
  does not begin with the money of every start on offer, an open slot's start money is taken out again right after
  the start and paid in when that slot's player joins (`native_gate_join_start_money`, a default). hjs4 (10 Nov, the
  four-start session, the joiner asking for the Terran start by name and getting slot 4): the wallet went from
  30,000 to 5,000 Cr five ticks into the session and to 20,000 Cr ten ticks after the joiner's handover, at the same
  game time on both PCs; all four ships stood equal on both at ticks 1200 and 3600; 0 errors. Blueprints, research
  and the known map of an open slot's start are there from the session's start.
- Not built: picking any start at join time without the host having prepared it, and joining from the game's
  Multiplayer page (that page starts a game only when every player is there).

## Ending

- The session ends by itself at the end of the pilot window.
- Close the X4 windows when you are done. The launcher then collects the logs.
- To end it at once, press Ctrl+C in the console. The launcher closes both windows and collects the logs.
- To leave early alone, close your window. The others go on after 15 s.

## What to send back

The run folder `.local/runs/<time>/`:

- small files: `summary.json`, `errors.log`, `desyncs.log`, `desync-status.json`, `relay.log`
- the client logs: `clientK/engine-output.log` (play mode: about 2 MB per client-minute, 24 Nov; `--full-logs` overlays
  write about 120 MB more for each world capture, every 1800 ticks)

`python tools/x4ls.py triage .local/runs/<time>` reruns a diverged session with diagnostic windows before its
first mismatch.

## Things to try with two people: your own inventory and the trade menu (built 11 Nov)

Scripted runs cover crafting, dropping, the start inventory and the slider's calls. These need a person's hands;
each line says what should happen. A menu does not show a change at once: the click travels to every client first
(a few steps), so reopen the page when a number looks old.

- Player info, inventory: each player sees their own items. Player 2 has a repair laser and an object scanner
  (new sessions only; in the suit the repair laser works).
- Craft something (for example a first aid kit from bandages, a needle and a sedative): the parts go and the
  product appears in your own inventory, not in the other player's.
- Drop an item from the inventory page: a crate appears next to your ship on both screens.
- Dock at a station, map, the station, Trade: move an amount slider, confirm. Before 11 Nov the slider did not
  move in a session.
- On a station, talk to a trader and buy or sell inventory items: your own items and the shared money change, on
  both screens. A trade of many different items at once works too.
- A faction representative: buy a licence or a blueprint. The player headquarters: start a research.
- Ship configuration at an equipment dock: install an equipment mod as player 2 (the parts come from player 2's
  inventory).
- The Pause key, F5 and F9: an info line "no pause, save or load in a session", and the game runs on.

If something else happens, send the run folder and say which line it was. Every refused action is one row in
`errors.log` (`UI_INVENTORY_REFUSED`, `UI_GUARD_REFUSED`, `PILOT_NATIVE_REFUSED`).

## Guns follow each player's cursor (built 9 Nov)

Each player's guns shot straight ahead, whatever the cursor pointed at: the multiplayer fire path gave the guns a
trigger but no aim. Now every player's own PC records where that player aims, and every PC turns that player's
gimballed guns there in the same tick (`native_gate_pilot_aim`, a default). It is X4's own manual aim: it needs mouse
steering, as in single player, and a gun turns as far as its mount allows.

- aim1 (two X4 on one PC, 900 ticks, a scripted aim for both players): the guns' mounts turned to the aim on both PCs
  with the same readings every 30 ticks, and back to rest when the aim ended; 0 desyncs, 0 errors. The second window had
  the focus, and its player's guns followed the real cursor there on both PCs.
- Aim assist (`native_gate_pilot_aim_assist`, a default) uses each player's own X4 option, Settings > Game > Aim assist.
  Without mouse steering, a player's guns track that player's selected target while it is in the gun's range. It is
  X4's own aim on a target, lead included. aim3 (with X4's range limit): the target was 13.8 km away against a reach of
  10.1 km and the guns stayed at rest. aim4 (the two players flew to each other first, 1700 ticks): with the target
  1.9 km away the guns turned to it and tracked it for the 800 ticks of the assist, the same readings on both PCs;
  0 desyncs, 0 errors.
- Not yet: fixed guns and turrets are as before.
- Host and joiners need the same kit: the input records changed.

### Locking onto the target with mouse steering (built 24 Nov)

Player 2 reported that the guns did not lock onto a target ship. With mouse steering on, no player's guns ever locked: the
multiplayer aim sent the aim assist only without mouse steering. In single player, with Aim assist "On", the guns lock
onto the target's lead mark as soon as the crosshair is within about 10 degrees of the target, mouse steering or not.
The third choice of the option assists only without mouse steering. Now it works the same for every player, players 2
to 8 included, each with their own target and their own option:

- With mouse steering and the crosshair near the selected target, the guns swing to the lead mark and follow it.
  Further away they follow the crosshair. X4's other rules apply as well: some targets do not take the aim assist,
  and a large ship closer than 50 m does not lock.
- The HUD's aim assist icon shows "active" while the guns are locked, as in single player.
- The mounts now take the target's speed into their turn, as in single player, so the guns no longer lag behind a
  fast crossing target.
- The aim that arrives a few ticks late on a ship that crosses the view is turned by that ship's motion, when the
  crosshair is near the selected target. It shows with the aim assist off: the shots land where the crosshair was on
  the target, about 1.7 degrees better on a ship crossing at 300 m/s 1 km away.
- Tested in C only so far (synthetic memory, slots 2, 3 and 4 aiming in the same tick). The live check is in the
  hand-over notes. Host and joiners need the same kit: the record changed (version 4).

### The guns' aim dots and the lead mark (built 24 Nov, after rk8)

Player 2 reported with rk8 that the guns still did not point at the lead mark of the target, and that the small dots
that show where locked guns aim were missing from the HUD. Player 2's log
showed the lock working (about 12,900 locks in the game) but nothing on the screen showed it:

- **The dots.** In single player X4 draws a small dot for each gimballed gun of the player's ship, where that gun
  points at the target's distance. Locked, the dots sit on the lead mark. X4 draws them only for a gun whose controller
  is the player's own, and in multiplayer every player's guns, player 1's included, are fired through another controller
  class (the fire lease), so no player ever saw a dot. Now each PC draws the dots of its own player's guns as X4 does:
  along each gun's barrel, as far as the selected target, in the in-range colour when the target is in range.
- **The lead.** The lock and the HUD's lead mark took X4's lead for AI pilots, not the player's: X4 makes an AI's lead
  worse against an evading target (a longer time of flight), and the HUD's mark also wrote that evasion clock into the
  target on that PC alone. Now both use X4's player lead: the guns go to the mark the player sees.
- **Not as in single player:** the dot's short pulse when the target comes into range is missing. A dot whose barrel
  points at something nearer than the target stays at the target's distance, where X4 puts it on that object.
- Every player, 2 to 8 included; nothing changes in the simulation except the lead of a locked gun, the same on every
  PC. Host and joiners need the same kit.
- Live (24 Nov, one X4 as player 2, `play --solo 2`, scripted aim at player 1's ship 688 m ahead): with the guns
  locked, one dot sat in the middle of the lead mark and the HUD's aim assist icon was lit; with the guns at rest, the
  dots of the outer guns sat left and right of the mark and the icon was dim. X4 asked for 16,506 dots by tick 1800 and
  drew each one, all on the frame thread; the HUD's lead mark took the player lead 1,505 times; no gate stop.

### Own guns without the input delay (built 24 Nov, after rk9)

Player 2 reported with rk9 that the drawn aim lagged behind the cursor, and asked for the same presentation without
the input delay that the own ship's movement has. The guns follow the aim a few ticks late on every PC, so that every PC fires the same shots;
the dots, the gun barrels and the aim assist icon showed that delay. Now each PC draws its own player's guns at that
player's latest aim, as it draws its own ship (`native_gate_pilot_aim_present`, a default; `false` turns it off):

- The dots, the barrels of the gimballed guns and the aim assist icon follow the cursor at once, with X4's own lock
  rule on the cursor now: near the target the dots jump onto the lead mark and the icon lights in the same frame.
- The shots and the hits keep the replayed aim, the same on every PC: a shot fired while the cursor is moving leaves
  along the barrel as it was a few ticks ago. Other players' guns are drawn as simulated.
- Not as in single player: the drawn barrel goes straight to the aim, without X4's turn rate.
- Every player, 1 to 8.
- Live (24 Nov, one X4 as player 2, `play --solo 2`, a scripted cursor jump from the lock on player 1's ship to 14
  degrees aside at tick 700): the drawn gimbal at its 10 degree limit from tick 701, the simulated one still on the
  lead until 703 and at the limit about tick 713; in the window grabbed at tick 702 the dots already sat left of the
  target with the feature on, and still on the lead mark with it off. The dots followed each scripted cursor, the icon
  went dim at the jump; every gun solved (no refusal), the DOF limits read as +-0.1745 rad. Not checked yet: two PCs
  (the census), the gun models themselves (not visible from the cockpit).

### Boxes and targets around players 2..N (built 24 Nov, after rk9)

Player 2 reported with rk9, in a game continued from a save, that ships could not be targeted and the blue boxes
around nearby objects were missing, while gates and some objects, such as the mission's target ship, still had
them. With rk8, in a fresh session, player 2 targeted ships fine.

- **Why.** X4's HUD draws a box for the objects of ONE zone and its neighbouring zones: the zone of the UI's player,
  which is X4's own player, player 1, on every PC. Gates and mission objectives come from other lists, which do not
  depend on the zone, so they kept their boxes. Player 2's HUD listed the ships around player 1. In rk8's fresh
  session both players started 300 m apart in one zone, so it did not show; the continued game put the two players
  in two zones of the sector, and player 2 then flew to another system.
- **Now** (`native_gate_pilot_present_hud_zone`, on by default, `lockstep_pilot_hud_zone.h`): each PC of player 2..8
  keeps those lists on its own player's zone, with X4's own routine, every frame its player's view is drawn. When
  player 1 changes zone X4 moves them to player 1's zone for one frame; they come back at the next. Only that PC's HUD
  changes; nothing in the game world does.
- **Not covered:** a section change on a highway inside one zone (X4 refreshes the list there; here the zone is the
  same, so the list stays until the next zone).
- Status: `hud_zone=on/passes/rescans/scanners/faults/other/nozone[/zone before>after]` in the `PILOT_INPUTS` row of
  that PC's log.
- Live (24 Nov, one X4 as player 2, `play --solo 2` of rk9's continued save `c3-t18000`, player 1 idle in its zone,
  player 2's ship on its way in travel mode): at the second frame the three lists held player 1's zone and were moved
  to player 2's (the zone X4 itself names for player 2 there); then they followed player 2 through 60 more zones
  (63 moves by tick 14,700, no fault). The same run with the module off: the 9 world hashes, the 8 world census files
  and the shared ID counter equal, so the game world does not change; two runs with it on: equal too. On the screen
  the ships around player 2 had boxes at the start with and without the fix (the two players' zones are neighbours
  there); far from player 1 the solo run's ship crossed empty space, so the boxes there are still to be seen in play.

### Gates, accelerators and superhighways for players 2..N (built 25 Nov, after rk11)

Player 2 reported in a two-PC game (24 Nov) that the effects of moving between sectors did not seem to work. Measured
first as X4's own player and then as player 2, one X4 each (`x4ls play --solo 1` / `--solo 2` of rk9's continued save
`c3-t18000`, the same script for both: Mars' accelerator to Venus, the Moon's superhighway, Neptune's jump gate), then
with both players in one game.

- **Accelerators and jump gates.** Both players see the gate's vortex, the white jump flash and the new sector.
  X4 also gives its own player the cockpit gate effect (`cpfx_gate_01`, on the cockpit) when its ship passes a gate
  or an accelerator; that test reads X4's own player, player 1, on every PC, so player 2..8 never had it. **Now**
  (`native_gate_pilot_present_transit`, on by default, `lockstep_pilot_transit.h`) each PC of player 2..8 gives its
  own player that effect, with X4's own routine, in the next frame of its own view; nothing in the game world changes
  (the effect is that PC's own, as X4 makes it for its one player). Player 1's PC is not patched.
- **Superhighways.** X4's own player rides the tube (blue walls, the highway HUD, its speed) and leaves it at the far
  gate 3.5 s later. Player 2's ship entered the highway and then sat at 0 m/s with no tube and no highway HUD, until a
  script moved it away (80 and 90 s). The highway's scene (its tube, lanes and traffic) follows "the player", which is
  player 1 on every PC, so player 2's ship was handed to it as traffic and placed relative to player 1. **Now**
  (`native_gate_highway_players`, on by default, `lockstep_highway_players.h`) each highway's scene follows the
  player in it: player 1 as before, a player 2..8 in its own highway, the same on every PC. Live, alone and with
  player 1 in the game: player 2 enters at the tick player 1 did, rides the tube and comes out at the far gate at the
  same tick as player 1 (1765 to 1870). Two players in one highway: the scene is the first one's (player 1 whenever
  it is there), the other rides as its traffic, as X4 treats every other ship near its player.
- **"Entering <sector>".** The ship computer says and shows "Entering <sector>" ("Entering system" between systems)
  two seconds after each sector change; player 2 never had it. X4's script waits those two seconds in a copy that the
  player's next sector change cancels, and for player 2 the gate handed the change to the shared scripts before X4's
  own post on player 2, which then cancelled the new copy at once. **Now** X4's post comes first. Live: player 2's
  line at every change, at the ticks player 1 has it (832 Venus, 1605 the Moon, 1924 Earth, 4305 Neptune), and
  "Entering system" after Neptune's jump gate (the vortex, the flash, the new system, as player 1), on player 2's PC
  only.
- **Other players' ships.** A player's ship passing a gate looks on the other PCs as an NPC ship's does (the gate's
  jump effect, the same on every PC); the cockpit effect stays its owner's (nothing on player 2's PC while player 1
  passed the accelerator).
- **The same world on both PCs:** two two-client runs of 7,241 ticks, in sync, the census equal at 1800, 3600, 5400
  and 7200.
- **Open:** at a superhighway's entry X4 disrupts its own player's travel mode ("Travel Mode disrupted"; player 1
  leaves the highway at 157 m/s); player 2's travel mode runs on (it leaves the highway and goes on at 2.2 km/s):
  X4's travel stop tells its own player only. And a pending "Entering" line of player 1 can still be cancelled by
  another player's sector change within its two seconds.

## Mission types, tested without a person (built 16 Nov; two clients on one PC)

`tools/dev/mission_matrix.py` runs X4's own mission libraries (`md/rml_*.xml`) once per player, one after the
other, and compares the players:

```
python tools/dev/mission_matrix.py list
python tools/dev/mission_matrix.py run --snapshot SESSION/session-save.xml.gz --session SESSION/session.json        --gate-dll x4lan_gate_64.dll [--types flyto,collect] [--players 2] [--display N] [--low-priority]
python tools/dev/mission_matrix.py report CLIENT1.log CLIENT2.log [--run RUNDIR]
```

- A test: an offer is made at one tick on every client, the player under test accepts it as the stock menus accept
  an offer, the accept starts one stock library on a target the test made (an ownerless scout 3 km away), a few
  seconds later the test does what a player would have to do, and the library's own end signal is the result
  (`MT_RESULT ... value=`, the library's feedback value; `timeout` after 25 s). MD: cue `MissionTest` in
  `tools/dev/native_gate_ui_replay.xml`; script ops `mt_offer` (every client's plan) and `mt_accept` (the player's).
- The verdict: player 1 is X4's own player and so the reference. Player k passes when its value equals player 1's
  and player 1's is the answer the library is known to give. Rows that differ between the clients, a desync or an
  error row of the run fail it too. Exit code 0 all passed, 1 a failure, 2 the run itself did not work.
- `run` starts its own windowed clients with disposable profiles (`x4ls play`), about a minute per type and two
  players plus the start, and stops them. Beside another session on the same PC: `--first-profile N`, `--lanes DIR`,
  `--beside`, `--display N`, `--tile-skip N`.
- The types: fly to, collect inventory, destroy (the target destroyed by nobody: the library's "destroyed, not by
  the player" answer), reputation, capture, target an object, deploy a satellite (a replayed call of the stock UI,
  `launch`), and kill (17 Nov): a wall of targets ahead of the player's ship, the player's own guns fire for seven
  seconds (script op `aim_fire`: the aim assist on the player's target and the trigger), and the library must
  count a kill by the player; and scan (17 Nov): the player's ship is put close to a module of the nearest
  station, looking at it, and the library must see the station's revealed share grow (a module is revealed by being
  close and looked at; the scan mode is not needed for it); and deepscan (17 Nov): the player's ship scans a ship
  300 m ahead (scan mode on, the scan started, two seconds on target) and the library must hear the scan finish.
  And lrs (17 Nov): the long-range scan. The mode's key, the secondary trigger held for a full charge (2 s; held
  0.2 s longer the charge is lost), the release; the scan's ping reaches a ship 5 km ahead, the test targets it,
  and the library that waits for the found object ends. And killmatch (17 Nov): the kill test's wall and guns,
  counted by the library that hears the "player killed an object" notice; each player's own mission got that
  player's notices. Twelve of twelve for both players.
- A plan's trigger goes through `aim_fire`. A trigger injected as an input state (`{tick,'input','state',24,1}`) is
  a key press, and a key press counts only in the window that has the Windows focus (two clients on one PC share
  one keyboard): it fires in one run and not in the next.
- Not covered, because it needs a menu or a tool MD cannot drive: repairing, docking, talking to a
  person. A test that passes says the library heard that player; it does not say a person can fly the mission.
- Scan mode (17 Nov, `native_gate_scan_mode`, on by default): the mode's key works for every player. Before, the
  key was refused for everybody in a session. Scripted run, two X4 on one PC: each player switched the mode on and
  off through the stock call and through the key; every client showed the same mode for each player at the same
  ticks; a player whose ship was destroyed lost the mode. The ship's deep scan works for every player too (the
  scan key on a target while the mode is on; two seconds with the target in range): scripted, each player's scan
  of a ship finished and X4's own scan mission step counted it. The scripted run starts the scan through the
  call the scan key ends in; the key itself, and the scan bar on the screen, need a person. The range rule is
  simpler than X4's: the target must stay in scan range, it need not stay ahead of the ship, and choosing
  another target does not stop the scan. Not there yet: the mode's cockpit tint and sounds for players 2 and up.
  Things to try as player 2: switch the scan mode on, target a ship within about a kilometre, press the scan key
  and keep close for two seconds; near a station, look for signal leaks. Say what you see and hear.
- The stock offers (`mission_matrix.py offers ...`, the same options as `run`): the offers X4 itself made in the
  session's world, the generic missions of the stations around the players, are accepted one after the other, by
  player 1 in one session and by player 2 in the next (`--accept-as 1,2`), each aborted again after five seconds. The
  table names for every offer whether a mission started and how many script errors came with it; an offer that does
  worse for a player than for player 1 fails. 16 Nov, the two-player test session: 13 offers (deliver, drop, destroy,
  board, intelligence, build, patrol, transport), a mission started for both players for each, no script error. It
  completes none of them. Script ops `offers_list`, `offer_accept_next`, `mis_new`, `mis_abort_new`. With `--look`
  it also prints what each started mission asks for: the objective's text, its progress and its targets.
- A real stock mission, finished (`mission_matrix.py real --offer NAME ...`, 17 Nov): one stock "destroy these
  objects" offer, named by a part of its name, is accepted by the player under test; round after round that
  player's ship is put near the first object of the mission's objective (the flight a person would make, here a jump
  into another sector), the object becomes its target and its own guns fire. The kill credit, the mission's end and
  the reward are the stock script's. 17 Nov, the two-player test session, the offer "Steady Trigger Finger
  Required" (five mines in a sector no player was in): player 2 destroyed each mine with the first hit and the
  mission ended as completed for player 2, reward paid, the same rows on both clients, no desync.
  What it found on the way: a player other than player 1 that came into a sector of its own in the middle of a
  session found the objects around its arrival without physics. The first runs fired at a mine 120 m ahead for
  twenty seconds and hit nothing, and the mine's proximity sensor never saw the ship; for player 1 the same kind of
  mine went off two seconds after the ship arrived. The cause was an order of two steps at the sector change (the
  surroundings were raised to full detail one step before the sector got its physics scene); fixed the same day.
  Players who meet and part (`mission_matrix.py meet [--mover 2 --to 1] [--leave OFFER] [--keep]`, the same day):
  one player's ship is put next to another player's, in that player's sector; with `--leave` the other one then
  jumps away with a stock offer. Read from both clients' memory after each: a ship that arrives has its physics
  in the scene of the sector it is in; when player 1 arrives where player 2 is, the sector keeps its scene and
  both ships are in it; when player 1 leaves a sector player 2 stays in, that sector keeps its scene and player
  1's new sector gets one of its own at the moment the ship arrives. Not covered: a flight through a gate
  instead of the test's jump, and more than two players.
  These tests first ended about ten seconds after the jump. Run longer, the one where player 1 leaves parted
  every time, twenty seconds after the jump: a ship in the arrival sector fires eight swarm missiles, and X4
  places the members of a swarm with a number it takes from a memory address, so each client flew the same
  missiles along other paths and removed them at other ticks. Fixed (`native_gate_swarm_seed`, a default; one
  byte of X4's formation code, so two swarms in the air at once now weave alike): the same sequences then ran
  2,500 ticks in sync, with player 2 parked at player 1's old place and without. How it was found: with
  `native_gate_created_rows` on, the two clients' rows were equal until the missiles' `retire` rows.
- Seen once (17 Nov, tick 14085, during player 2's station scan; not in the ten full runs of the matrix
  after it): client 2 alone rebuilt one station's interior walk mesh six times in one tick, a second after
  its player arrived there, and the clients parted from there. What is known: every such rebuild is asked
  for by an animation listener (a docking bay's fence, a cockpit, a dock area), which waits for an animation
  to reach a state. With breakpoints on both clients over whole runs (read-only, `bpwatch`-style logging of
  X4's own functions), the event that serves a listener is posted about 313 times a run, the same on both
  clients and always inside the simulation's frame; but the call that sets an animation's state runs about
  167,000 times a run and never equally, 37,000 to 41,000 of them from the render pass: each client sets the
  animation states of what it draws, and such a call served a waiting listener like any other. Since 18 Nov a
  call from a client's own part of the frame (render, UI, a player's own presentation) leaves the listeners
  waiting (`native_gate_anim_listeners`, a default; status `ANIM_LISTENERS held=`). In a normal run it holds
  nothing (626 calls with waiting listeners on both clients, none held, 14,433 ticks in sync). That the run
  of 17 Nov was this is the reading that fits; it was not captured, so it is not proven, and the run does not
  repeat on demand. What the change costs: a fence that moved while nobody watched is not followed by a mesh
  rebuild at the moment a player first sees it.
- A new type: an `Mt<Name>` cue beside the others (its `_Ref` cue names the library), a row in `TYPES` and, when the
  player has to act through the UI, a row in `OPS`.
- What it found on its first run: every step behind a `<delay>` of player 2's mission ran as player 1 (the MD
  scheduler's own calls were no frame of the mission's owner); fixed the same day (`lockstep_mission_owners.h`,
  status `sched=`).

## Stations: docking, walking on the dock, seeing each other (built 16 Nov; two scripted clients on one PC)

In the integration branch since 17 Nov. Run with scripted input, not by people.

- **Docking.** Each player flies to a station, asks for a dock (the station's menu, or the dock key) and X4 docks the
  ship. Before, the session stopped when X4 took a player's ship for the docking. Both players can do it at the same
  time and end on neighbouring pads. Each client gets its own "docked" menu; the other players keep flying.
- **Getting up and out.** Get up from the docked menu or with the key. A small ship is left through its rear: turn
  round and walk out, or walk backwards. A player 2..N who walked sits down again with the get-up key.
- **Undocking.** Every player undocks from its seat (the docked menu's Undock) and has its controls again when X4
  lets the ship go, about ten seconds later.
- **Seeing each other.** X4 draws no body for a player on foot (not even when another player's own object stands
  in the room: tried 17 Nov). Each player on foot gets a figure where it stands. Since 17 Nov the figure is the
  player's own character (the game start's player model; before, a crew character picked at random), and a client
  does not draw its own figure (before, you saw yourself in front of you while walking backward out of your ship).
  The figure walks to where its player is with X4's own NPC walk (asked again a few times a second), so it strides
  and turns as X4's people do; before, it was put at the player's place every step, which started its walk
  animation again each time: it slid along in a standing pose. It is a step or two behind a moving player and walks
  facing the way it goes (a player walking backward is seen walking forward the other way round). The figure
  stands half a metre behind its player, so the player's crosshair does not start inside it. Session default `native_gate_foot_avatars`.
- **Known faults.**
  - Rooms (17 Nov). A player 2..N who walks out of its ship is now in the station's rooms as player 1 is: X4's
    scripts see it there and hear the change of room and of ship/station. Before, it stayed counted in its cockpit.
    Sitting down is still the get-up key from anywhere on the dock (the player is put back in its seat).
  - The transporter for a player 2..N: since 21 Nov it moves players 2..N between a station's rooms, run with two
    scripted clients (scenario `rooms` of `tools/dev/action_tests.py`, below). The menu's "Travel to" of a walking
    player 2..N is taken like player 1's, and the gate puts that player at the target with X4's own routines.
  - Fixed 21 Nov: doors did not open for players 2..N (a playtest report: player 2 stood at the closed door of a
    dock bay's transporter booth). X4 opens a door when someone stands at it, counting the people of the station's
    lists; a player 2..N is not in them, and its figure is a person without a post, which X4 does not count. The
    gate now asks X4's own test for every walking player as well (`lockstep_pilot_foot_doors.h`, with the bodies'
    room change; status `doors=` in the bodies row). Doors open for players 2..N as for player 1 and close after
    them.
  - Fixed 21 Nov: a player 2..N could open a transporter's menu only at its own ship's keypad. A station's
    transporter booth (its button beside the door: "Use Transporter Room") now opens X4's menu for every player,
    and X4's own check decides as for player 1 (the player must stand in the booth).
  - Fixed 25 Nov: in a game with an open slot, a dropped player or a `--solo` run, docking by the autopilot stopped
    short. A station's navigation octree showed only once every slot had reported it built, and a slot nobody
    plays never reports, so no octree showed for the rest of the game. X4's autopilot to the dock it was given
    then ended 230 to 470 m in front of the dock (its last leg, to the dock's own approach point 31 m over the pad,
    needs the station's octree) and nothing landed; player 1 and player 2 measured alike at the trade station IDJ-703
    with the same fighter and the same dock. (The "500 m over the dock" of the claim runs of 24 Nov was the test
    tool's own climb after that stall.) The octrees now wait for the slots that play only (`lockstep_nav_octree.h`):
    the same runs then flew to the dock's approach point and landed, player 1 1,263 ticks after its grant and player 2
    1,254 ticks after its own, both on the same spot of the pad (runs p1d and p2d of 25 Nov, `NAV_OCTREE_STOP ...
    shown=9 agreed=9 waiting=0 ... silent=2 unheard=9` for player 1). X4's own docking order (the debug tool
    `--ai-dock`, order.dock) needs no shown octree: it docked both players there on the release gate and on the
    fixed one, player 1 2,977 ticks and player 2 3,012 ticks after the start (runs p1f, p2f, p2g). Its stop of the
    autopilot within 9 km leaves the travel drive on for a few seconds, for either player alike (720 to 760 m in
    the next half second); that is the likely carry of the claimed corvette of 24 Nov past its station, not
    reproduced with the own fighter.
    How X4 lands a player's ship, for every slot the same: U::Player's update docks the ship once it is within the
    docking computer's reach of its pad (`libraries/parameters.xml`, an S dock with the Mk2 computer: 50 m along
    each of the pad's axes, pitch below 89 degrees). Put 30 m over the pad by the test fixture `dock_warp`, player 1
    and player 2 landed at the same tick, on the same spot (runs p1c and p2c of 25 Nov).
  - Fixed 17 Nov: when player 1 had docked and stood up while player 2 was still flying in on its autopilot, player
    2's ship stopped 2.5 km out and never docked, and the clients' world check differed five minutes later. While
    anyone is on foot the other players' player objects are held outside X4's room tree, and X4 then ran them as
    something nobody is near; they keep their seat's attention now. Scenario `early` of the test.
  - Pointing at a figure still shows "Talk"; it starts no conversation (the figure is titled "Player N").
  - Near a station the clients' checks disagreed (play mode goes on silently). Four causes, fixed on 17 Nov:
    the station people's animation choices moved the simulation's random numbers (a traffic ship's pilot got a
    different face on the two clients, the world check of the next census differed); every animation start was
    keyed by this process's own asset numbers, so the people's animations ran out of phase between clients from
    their first tick; a new person's first animation waited for the loader thread, a tick longer on one client than
    on the other; and X4 steps a body's animation by what each client's camera shows of it, so an animation ended at
    different ticks (the dock marshal's wave up to three seconds apart) and the people's scripts ran that much
    apart. Now the starts are keyed and loaded alike and an animation's end goes by the simulation's time
    (`native_gate_move_rng`, `native_gate_body_clock`, the character play module; all defaults). After that the two
    clients started every body animation at the same tick (229 of 229 in a five minute run; before, 358 of 432 ticks
    differed), and the meet, walk and undock tests agreed in every digest part at every tick, the runtime ID word
    included. One earlier run with a real mouse over a test window had a world mismatch that is not explained. The
    Linux gate has none of these fixes yet.
  - One more cause, found on 17 Nov after a run that split while the PC was short of memory (one client had one
    traffic ship more than the other from then on). X4 keeps a station's traffic ship alive for as long as the
    client's own UI refers to it (a target, a menu) and afterwards destroys it instead of keeping it for reuse; what
    each client's UI refers to is that client's own business, so the two clients' stocks of reusable ships drifted
    apart, and the next ship of that model was reused on one client and built on the other. A client's UI no longer
    holds such ships in the simulation (`native_gate_ui_holds`, a default). In a 3600 tick run the two clients' UIs
    had asked for 6314 and 6578 holds, never the same. The split itself was not reproduced (stalling one client's
    threads or capping its memory did not bring it back), so that this was its cause is likely, not proven (`tools/dev/client_squeeze.py` is the tool that stalls or caps one
    test client). What it
    costs: a traffic ship you have targeted can leave while targeted.
  - And one that was caught with the diagnostics on (17 Nov evening, six clients of other tests on the PC): the
    pathing thread makes a small query box for each flight path it works out and throws it away again, and the box
    took a number from the list the simulation's objects are numbered from. On a busy PC the thread got to a request
    one step later on one client (tick 3347 against 3348), so every object made afterwards had a different number on
    the two clients: the runtime ID word apart for good, the world census at the next count. The box now takes its
    number from the client's own list, as that thread's query sphere already did (`native_gate_runtime_ids_local`, a
    default; no new key). The same test on the same busy PC then agreed at all 7571 ticks (its seven-tick ID
    difference near tick 7220 is something else: it was back in the next run). The Linux gate does not have this
    entry yet.
  - OPEN, the split at tick 3128 or 3129 of the earlier runs (seen four times on 17 Nov, only while other tests'
    clients loaded the PC): one police traffic ship that returns to its lane after a scan is taken out of the world
    (pilot removed, ship kept for reuse) a few ticks apart on the two clients (3024 and 3036, 3017 and 3019, 3019
    and 3024). When the next batch of traffic ships falls in between, one client reuses that ship and the other
    does not, and every object made afterwards has another number. Not the cause: what the UI holds (fixed above),
    the camera distance X4 takes from its render cache there (compared with the simulation's on a million calls,
    never another verdict), the police script (its random draws are the same on both clients up to the removal).
    Left: X4's test that the ship is back at its place in the lane (within 10 m). `native_gate_traffic_rows` with
    `native_gate_viewpoint_policy_rows` now writes that test for every traffic ship and tick (POLICY_ROW
    site=113), for the next time it shows; nine runs on a quiet PC and seven with the test's two clients pinned to
    a few cores did not show it.
  - A cause of that kind found and closed on 18 Nov, without proof that it is this one: X4 works out a ship's
    route on a thread of its own, from where the other ships are at the moment that thread gets to it. In this
    scenario 22 of 54 routes were worked out 24 to 32.5 ms after their tick began, with ticks 33.3 ms apart: on a
    busy PC such a route is worked out one tick later on one client, from other positions. Shown by holding that
    thread back on client 2 alone (`native_gate_spline_calc_delay={2,10}`, test only): 6 of 54 routes read other
    positions than client 1's, 21 with 40 ms. `native_gate_spline_phase` (written on 7 Nov for a hot join, now a
    session default) lets that thread work at two fixed points of every tick only: with it and the same 40 ms
    every route read the same positions on both clients. A police ship that flies back to its lane flies such a
    route, and no check covers where a traffic ship is, which is why this is the suspect; neither delayed run
    parted, so the split itself is still not reproduced. A long wait of that thread (no tick for 10 s: a session
    waiting for a stalled client) is counted, no longer a stop.
  - Also seen in a delayed run and once before: two object numbers handed out at tick 1454 on one client and 1455
    on the other, alike again from the next tick (about 3,400 are handed out in those two ticks). Not yet
    named; the per-tick rows now list the first and the last four of a tick
    (`head=`, `tail=`) so the next one is.
  - Not tried: three or more players, a hot join while someone is docked or on the dock, a docked ship's death,
    the Linux gate, two PCs.
- **The test** (starts its own windowed clients with disposable profiles, prints a check list, saves pictures of both
  windows, exit code 0 when every check passed):

      python tools/dev/station_walk_test.py run --snapshot SAVE.xml.gz --gate-dll x4lan_gate_64.dll --scenario meet

  The scenarios `meet`, `walk`, `undock`, `transport` and `early` passed on 17 Nov (two scripted clients on one PC).
  `meet`: both dock, get up, walk out, and player 2 walks 40 m across the dock up to player 1; the pictures show, on
  each client, the other player's figure three metres away. `walk`: get up, a few steps, player 2 sits down and
  undocks. `undock`: both undock from their seats and fly off. The snapshot is the two-player one (both ships side by
  side).

### Between a station's rooms (21 Nov; two scripted clients on one PC)

Scenario `rooms` of `tools/dev/action_tests.py`, station CZF-533 of snapshot 005: both players dock with X4's own
docking AI (`--ai-dock`), stand up and walk out backwards; each player's figure walks ahead with X4's NPC walk to its
dock bay's transporter booth (the booth's NPC stand place) and the player follows its trail, through the booth's
door, onto the pad. There the player looks at the booth's button until X4's crosshair shows "Use Transporter Room",
clicks, and in X4's TransporterMenu chooses the Security Office and presses "Travel to". Both players arrive in the
Security Office; the clients agree at every tick (run of 21 Nov 23:27, 7,464 ticks). `--direct` travels by the menu's
own call instead of the click and the menu; `--look` and `--calibrate` take pictures while looking round on the pad.

- Looking round on foot by script: the FP_YAW and FP_PITCH axes move the view one tick at a time (held longer, X4
  speeds the view up); mouselook moves nothing on foot. The scripted look starts from the booth's data and sweeps a
  few angles to the side (the button sat 20 to 27 degrees left of where the booth's data put it).
- Known: closing the menu after "Travel to" on player 2's client shows a refused ReturnFromMenu in `errors.log`;
  the menu closes and the travel happens.

## Building a station (17 Nov; two clients on one PC, the stock menus clicked with the real mouse)

In the integration branch since 17 Nov. The menus were clicked by a test rig that moves the real cursor
(`tools/dev/ui_clicks.py`), and in part by a person.

- **What was clicked, by both players, each for a station of its own:** the map, Manage Plots, "Create new plot", a
  click in the sector, the plot's size arrows, "Buy licence", "Continue", modules from the left list put into the
  plot, "Confirm module changes", "Assign or hire a Builder", the player's own construction vessel, "Select". Every
  one of these ran on both clients at the same tick with the same answer (the new plot's ID, the fee, the plan, the
  vessel's order).
- **Found and fixed on the way:**
  - A station was built at different times on the two clients. The station menu asks for the build's progress
    every frame, and that question itself moved the build forward, on the client that had the menu open: it took each
    portion of building material one minute before the other client and finished the module a minute earlier. A
    menu's question no longer moves a build (a shipyard's build queue is the same routine). With both players'
    station menus open, 112 readings of the stations (modules, tasks, the storage's wares) were the same on both
    clients and the module stood at the same reading.
  - After "Confirm module changes" the menu showed the station without the modules just confirmed, and the builder
    button stayed grey: the menu redrew before the shared step had happened. The same with a plot's new size and its
    fee. The menus now redraw when the step has happened, a few ticks after the click.
  - A player's clicks stopped working for the rest of the session after one large action (a person's plan of
    several modules after a plot extension): an action too long for one tick's room was never sent and held every
    later one back. Long actions now travel in smaller parts. Seen again afterwards with a plan of three modules:
    two parts, applied on both clients.
  - The right-click menu's rows began with a "0" where an order's icon belongs.
  - **An open map moved ships.** In several sessions a few moving ships (a construction vessel, freighters passing)
    were at different places on the two clients from one world check on. It needed nothing of the station: player 2
    opening the map was enough. While a map is open the game brings every ship it shows, and that is not next to a
    player, up to date with its engine every frame; without a map that happens every one to two seconds. So the
    client with the map open saw a ship switch its travel drive at once and the other client a little later, and
    the ship flew differently from then on. In a session the map no longer does this (it shows what the simulation
    last refreshed). The same was behind another test's ships that parted while a "Hire Staff" map stood open on
    one client. Mouse steering of the other player's ship and a resized window were tried on the way and did not
    part the clients.
  - **The session with all of these fixes** (run play6, 17 Nov): player 1 built a station, player 2 opened the map
    and left it open for 80 seconds, then built a station too; both kept their station menus open. The world was
    compared every ten seconds. Both clients agreed in every part at every one of 30,522 ticks, both modules stood
    at the same reading on both clients (ticks 25240 and 28840), and the map's refresh had been held back 9,554
    times on the client with the map.
- **Run on 18 Nov** (next section): a hired construction vessel and its fee, a planned module taken back, removing a
  plot, the station's budget, a second plan on a standing station, managers, the station's trade settings,
  subordinates. **Not run:** a faction per player.
- **The test:** `python tools/dev/station_build_test.py run --snapshot SAVE.xml.gz --gate-dll x4lan_gate_64.dll`
  starts two clients, clicks both players through and compares them until the modules stand. Since 18 Nov its clicks
  go to each X4 window as messages and leave the PC's mouse and keyboard alone (`--input mouse` for the real
  pointer); the click places are measured for 1600x900 windows, and a resized window moves every later click off its
  button. It passed from start to end by itself on 18 Nov (run t7): 15 steps per player, each taken at the first
  try, the eight build calls on both clients at the same ticks, 88 station readings the same on both, both modules
  standing from the same readings (ticks 24940 and 26140), in sync at all 26,655 ticks. The three tries of 17 Nov
  had each stopped for a reason outside the game (a click not taken, a person's clicks in its windows, a closed
  client).

## Managing a station (18 Nov; two clients on one PC, the stock menus clicked without the PC's mouse)

Branch `contrib/ferrite-station-build`. Two solar power plants of the player faction stood from the start of each run
(a test fixture: a stock plan that produces without supplies, a manager each, an account), so the menus had something
to manage. Each player's clicks went to its own X4 window as window messages (`tools/dev/ui_window_input.py`): the
PC's cursor and keyboard were not touched, a person used the PC meanwhile.

- **What was clicked, and ran on both clients at the same tick with the same result:**
  - *Logical Station Overview:* the station account (slider, Confirm), a ware's automatic pricing off and its price
    slider, automatic sell amount off, "Create Buy Offer", automatic storage allocation off, "Select Trade Wares"
    (two wares, Confirm), the drones' "restock automatically" off and their target amount, the supply trade rule's
    "use global setting", "Pause Production".
  - *The map's Information panel of a station:* the trade rules' "use global setting" boxes, the preferred build method.
  - *Station menu ("Plan Build") of a standing station:* one more module put into the plot and confirmed (the plan of
    seven modules went in four parts), "Assign or hire a Builder" with a construction vessel of the Argon (the fee of
    50,000 Cr left the account on both clients, the vessel took the order on both, the module stood at the same
    reading on both, 42040), the player's own vessel for the other station, "Cancel Builder Assignment", the money for
    construction (slider, Confirm), a planned module removed again and confirmed.
  - *Manage Plots:* a new plot and "Remove Plot" with its question.
  - *Crew:* the manager fired (with its question); the other station's manager sent to work at the first one ("Work
    somewhere else for me", the station, the role, "Assign").
  - *A freighter as a station's trader:* see below.
- **Found and fixed:**
  - **The station overview drew the state from before each click.** "Automatic pricing" stayed ticked and no price
    slider came, although the price was fixed on both clients; a new trade ware was missing from the chart. The menu
    draws right after its call, and in a session the call happens a few ticks later. It now draws again when its own
    player's call has happened.
  - **A ship could not be given to a station (or a ship) as a subordinate.** The click on "Trade for commander", then
    a group, did nothing: the entry was refused in a session although every call it makes is shared. It is open now
    (not for a fleet's replacement slots and not for "position defence", which make calls nothing shares yet); both
    players assigned a freighter as their station's trader, on both clients.
  - **"Remove Assignment" did nothing** for the same reason, and one of its calls was not shared. It is shared now:
    player 2 took its freighter from its station again, on both clients.
  - **An open map made a station produce on one client first.** The map draws the storage of the stations it shows,
    and that question brings the station's cargo and production up to the moment, on the client with the map alone.
    That client's station held one production cycle more than the other client's for hundreds of ticks (1050 against
    700 energy cells; no check covers a station's cargo), the two clients no longer updated trade offers the same
    number of times, and after some 6,000 ticks one client ran one script step the other did not: the clients had
    parted (run mg4, the runtime IDs at tick 8843). The one unexplained extra ID of 17 Nov (a client idle with a
    station menu open) fits this. In a session the map's question no longer moves a station; the map shows the
    storage the simulation last brought it to, as the menus do. With that, the same clicks and player 1's map open
    from tick 3,000 on: both clients agreed in every part at every one of 31,799 ticks (run mg5), and in the run
    after it (mg6) the two clients worked their stations up exactly the same number of times (366,362 at tick
    12,540) with the same storage, the map's question held back 20,138 times.
- **What a player should know:**
  - The list in "Select a Builder" holds only the construction vessels the map shows at that moment: zoom out and
    they appear. This is the stock game's behaviour.
  - The map and the menus show a far station's storage as the simulation last worked it out, which can be a few
    minutes old for a station nobody is near.
  - A second player's change to a station whose overview you have open shows when you next open a node or the menu.
- **The test rig:** `tools/dev/ui_window_input.py PID click FX FY` (also `rclick`, `drag`, `key`) reaches one X4 test
  client without the PC's mouse; `station_build_test.py` uses it by default (`--input mouse` for the real pointer).
  The mouse wheel and letter keys do not arrive this way; the map opens by the HUD's top bar.
- **Not run:** renaming a station (text entry); a defence or mining subordinate at work over a long time; the
  encyclopedia and the other entries of the module menu; three or more players. A faction per player: next section.

## Stations with a faction per player (19 Nov; two clients on one PC, the stock menus clicked without the PC's mouse)

Factions mode (`--mode factions`): each player is a faction of its own, with its own money, blueprints, stations
and ships. Each player had a station, a construction vessel and a freighter of its own faction from the start of the
run (test fixtures), and built a second station by clicks.

- **What was wrong, and is fixed:**
  - **Player 2's menus showed player 1's property.** The map's "Property Owned" listed player 1's station and ships,
    player 2's own station was drawn as a foreign one, and its Logical Station Overview showed "???" in every node.
    The stock menus ask for "the player's" faction, which was player 1's on every client. Now each client's menus
    and HUD see that client's own faction as the player's faction: player 2 sees its own stations and ships, its
    own money, its own station's data.
  - **The map stays the one map everybody shares:** sectors and other factions' stations a player 2 sees carry
    their names as they do for player 1 (before the second fix they were all "Unknown").
  - **Player 2 could not give its ships orders from the menus.** "Trade for commander" ran on both clients and
    the game refused the order (it takes orders for the player faction's ships only). Orders, order queues and
    their parameters now run as the ordering player's faction. Player 2 made its freighter its station's trader.
  - **Player 2's station plan vanished.** "Confirm module changes" ran on both clients, and the station had no
    planned module a moment later. The game logged why: "Failed to create 'manager' of
    'buildstorage_gen_base_01_macro' as no character definition was found". The game's list of people names "the
    player faction" and no faction of a session, so player 2's build storage had nobody to process its plans. A
    player's own faction now draws its people from the player faction's list, so its build storage, stations and
    ships get managers and crew as player 1's do.
- **What was clicked, and ran on both clients at the same tick with the same result:** player 2: the map and its
  property list, the station overview (a manual price, a trade ware), a plot with its licence fee paid from player
  2's own money, a module put into the plot and confirmed, its own construction vessel assigned, its freighter as
  the station's trader. Player 1 the same, as before.
- **Proof:** `station_build_test.py run --mode factions` passed end to end (run tf8): both players' 15 steps at the
  first try, both modules standing on both clients at the same reading (26440 and 30040), 101 readings equal, in
  sync for all 30,573 ticks. The coop test passes unchanged on the same gate.
- **What a player should know:**
  - Players see each other's stations in full (as in coop); what a station of yours can build is your faction's
    own blueprints.
  - The game's own station manager scripts do not yet treat a player 2+'s faction as a player: a station of player
    2 kept its whole account (2,000,000 Cr) where player 1's manager handed the surplus back to player 1.
- **Not run:** hiring another faction's construction vessel as player 2, the manager menu and the budget as
  player 2, a faction per player with three or more players, a player who joins later.

## Player actions, tested without a person (built 17 to 19 Nov; two scripted clients on one PC)

`tools/dev/action_tests.py` runs one scenario in a live two-client session: both players do a chain of player
actions, and the test checks that each action happened in the game and that the two clients stayed in sync.

```
python tools/dev/action_tests.py list
python tools/dev/action_tests.py run trade --snapshot SAVE.xml.gz --gate-dll x4lan_gate_64.dll [--first-profile N --lanes DIR --display N --tile-skip N]
python tools/dev/action_tests.py report trade CLIENT1.log CLIENT2.log [--run RUNDIR]
python tools/dev/action_tests.py plan trade
```

- **How a scenario runs.** Every client's plan holds the fixtures (X4's docking computer on the players' ships,
  money, fleet ships handed to the player faction, deployables and cargo for a ship) and one chain for its own
  player: steps that each begin when the one before has ended, because flying and docking take as long as they
  take. A step makes the calls a stock menu makes, or presses the menu's own handler (the function a button's click
  runs). Nobody moves a mouse: what a person sees on the way is covered only as far as the handlers go.
  (`tools/dev/station_build_test.py` clicks with a real mouse.)
- **What every run checks.** No client's gate stopped; each chain ran to its last step; the same readings taken at
  the same ticks on both clients are the same text; the world, the players' records and their inputs agree at every
  tick. The whole world is compared every 1,800 ticks (a capture); a run goes on to the capture after its last
  step, and a run without one fails. Every capture is logged per object, so a run whose world differs names the
  objects that differ; every tick's runtime ID allocations are logged by caller. Each scripted action also asks
  whether the menu entry it stands for is open to a person in the session: a script's own calls pass where the
  session refuses the click.
- **The scenarios** (the snapshot is the two-player one; exit code 0 when every check passed):
  - `trade` (passes): each player flies to the food factory MMJ-623, docks, opens the trade menu with the dock
    menu's Trade button, sets 20 food rations and presses Confirm, undocks when X4 grants it, flies to the shipyard
    NLN-075, docks and sells them the same way. Both players' holds and the account read the same on both clients;
    in sync at all 8,174 ticks of the run of 17 Nov.
  - `orders` (passes): each player tells a fleet fighter to fly to a place 28 km off, to explore the sector, to
    patrol it (the order queue), sets Patrol as its default behaviour, and sends a second ship to dock.
  - `orders2` (passes, 18 Nov): more of the interact menu's orders, each given to a fleet fighter by each player:
    follow another ship (within 2 km of it 15 to 19 s after that one arrived 6 km away), protect a station, collect
    the 20 energy cells the player dropped, pick up a satellite the player launched, attack whatever is in range at
    a place (two Xenon fighters lost their shields), withdraw from combat. In sync at all 5,627 ticks, three
    captures of the world; two runs gave the same ticks to the digit.
  - `shipbuy` (passes): each player buys a scout at the wharf AVK-777 as the ship configuration's Confirm does;
    both are paid for and built (about 160 s of game time), in sync at all 5,071 ticks.
  - `crew` (passes for both players since 19 Nov evening): fire a crew member, fire the captain, promote the crew
    member that is left to captain; then dock at a station and hire a person of the station for the fleet ship (the
    comm, the Hire choice, the map's Hire button; the fees were 479,200 and 1,213,800 Cr). Each player talks when
    nobody is in a conversation (X4 has one at a time), checks the conversation is its own and ends it after the
    hire. `--no-conversations` switches conversation owners off: player 2's hire is refused then, as before.
  - `refit` (passes for player 1; `refit --ai-dock` passes for both players since 21 Nov): dock at the wharf, order
    the wharf's high preset for the docked ship (travel engines mk2 to combat mk3, shield mk1 to mk3, lasers mk1 to
    mk2), undock when it is done. By the autopilot player 2's own ship does not reach its pad (see the wharf below);
    with `--ai-dock` both ships dock by X4's own AI, and both are refitted since the ship trader's fix below (run
    20261121-153401: exit 0, in sync at all 10,962 ticks). `refitorder` refits a fleet ship for both players.
  - `refitorder` (passes for both players, 20 Nov): each player orders the wharf's high preset for a fleet fighter
    out in space, as a person does it from the map (the ship selected, the wharf's "Upgrade", the configuration's
    Confirm). X4 gives the ship its Equip order with the build task, and the ship's own pilot flies it in and docks
    it (68 and 80 s after the order; both ships at the wharf at the same time), the wharf refits it (player 1's: combat
    engines mk3, shield mk3, lasers mk2; player 2's, which had those engines already: its second shield to mk3) and X4
    undocks it after. Two runs, in sync at all 9,239 and 9,238 ticks.
  - `repair` (passes, 18 Nov): each player sends a fleet fighter with half its hull to the wharf to dock and has it
    repaired there (the ship configuration's Confirm with a repair: 16,699 and 21,479 Cr). Both ships were docked at
    the wharf at the same time; each hull was whole within a second of its order. In sync at all 3,847 ticks.
    `repair --own` (passes, 21 Nov): each player's own ship instead, docked by X4's own AI (11,085 and 11,856 Cr).
  - `wing` (passes, 18 Nov): each player makes one fleet fighter the subordinate of another ("Assign as
    subordinate", assignment Defend), sends the leader 8 km off, orders the leader to attack a Xenon fighter, and
    removes the assignment. Ten seconds after each leader arrived its subordinate was 557 m and 976 m from it. The
    fight is a real one: player 2's target was destroyed after about 58 s, player 1's was down to 19 % shield when
    the test's 80 s were over. In sync at all 3,756 ticks, two captures of the world.
  - `shipsell` (passes, 18 Nov): each player sends a fleet fighter to the wharf to dock and sells it there (the
    map's "Sell ships" confirm). Sold for 1,701,612 and 1,202,466 Cr; the account went from 10,000 to 2,914,077 Cr
    on both clients; in sync at all 3,771 ticks.
  - `crewmove` (passes, 18 Nov): each player moves a service crew member from one fleet ship to another (the crew
    transfer's Confirm). X4 lists the person on the other ship at once and counts it aboard six to ten seconds
    later, also when the ships are 58 km apart. In sync at all 1,982 ticks.
  - `fleettrade` (passes, 18 Nov): each player has a fleet courier buy 20 food rations at MMJ-623 (a trade order
    from afar), sends it to the shipyard NLN-075 and has it sell them there. In sync at all 9,287 ticks, five
    captures of the world.
  - `deploy` (passes, 18 Nov; the last two steps 19 Nov): each player renames its own ship, launches a satellite,
    a nav beacon, a resource probe, a mine and a laser tower from it, drops 20 of the 50 energy cells in its hold,
    switches its mine off and on again and has its satellite destroy itself (the interact menu on a deployable).
    All ten deployables were out on both clients; each satellite went off and on and was no longer working half
    a second after its self-destruct. In sync at all 2,053 ticks.
  - `rules` (passes, 18 Nov): each player makes a blacklist, a fight rule and a trade rule for the faction (the
    global orders of the player information menu), sets the first two for its own ship, renames the blacklist and
    removes the trade rule. Both clients list the same rules with the same IDs; in sync at all 2,053 ticks.
  - `systems` (passes, 19 Nov): each player puts a weapon of its own ship into a second weapon group and makes
    that group the active one, switches flight assist off and on, travel mode on and off; then sets a fleet
    warship's turrets as the map's loadout page does: all turrets to "attack fighters", one turret (a frigate's)
    or one turret group (a destroyer's) to "missile defence" and disarmed, all disarmed, all armed, the defence
    drones to another mode and disarmed. Both clients read the same settings of all four ships throughout; in
    sync at all 1,967 ticks.
  - `settings` (passes, 19 Nov): the player information menu. Player 1 sets the faction's standing order for
    "attacked" (to its first answer, then its second), switches "ships wait for the player" off and on and names
    the faction; player 2 sets the standing order for a found abandoned ship, the resupply level, the turrets'
    default mode, the paint and the logo; each sets its own ship's answer to "attacked" and its resupply level.
    One faction, so both clients read all of it the same; in sync at all 2,012 ticks.
  - `storage` (passes, 19 Nov): each player has a fleet fighter dock at a fleet capital ship (a carrier, a resupply
    ship), puts it into the ship's internal storage and retrieves it ("Put into storage", "Retrieve from
    storage"). X4 greys the first entry out for some seconds after a docking; the script waits as a person would
    have to. In sync at all 3,786 ticks, two captures of the world.
  - `claim` (passes, 19 Nov): the fixtures give a fleet frigate two marines and put an ownerless scout 1.5 km from
    it; each player has its frigate claim it (the interact menu's "Claim"). A boarding pod flies over and the ship
    is the player faction's 18 seconds later, with the marine as its pilot. In sync at all 1,984 ticks. The
    scenario first sets the standing order for a found abandoned ship to "ignore" without the question;
    `--ask` leaves the question on (see the findings).
  - `wingset` (passes, 19 Nov, on the gate built that day): each player forms a wing of two fleet fighters,
    switches each of the group's five settings off and on (attack on sight, dock at the commander, reinforce the
    fleet, respond to distress calls, resupply at the fleet), changes the group's role from defence to attack,
    names the fleet and moves the subordinate to a second group. In sync at all 1,988 ticks.
  - `mining` (passes, 19 Nov): each player has a fleet miner drop two resource probes at places three kilometres
    off (the interact menu's "Deploy" on a place of the sector: an order in two parts, see the findings) and,
    where the sector yields ore, mine it ("Mine"). All four probes were out 20 to 24 seconds after their orders.
    The snapshot's sector yields nothing to mine, so the "Mine" order is reported as not given. In sync at all
    3,837 ticks, two captures of the world.
  - `routines` (passes, 19 Nov): each player gives a fleet courier automatic trading of food rations and a fleet
    miner automatic mining as their default behaviour (the map's behaviour page: the routine, its ware, this
    sector as its range, Confirm). All four ships have the behaviour on both clients. What a routine then finds
    is the game's: player 1's courier bought and sold on its own (the account went from 60,000 to 21,232 and on
    to 68,624 Cr), player 2's found no second deal in three minutes, and the miners stayed where they were. In
    sync at all 9,187 ticks, five captures.
  - `hostile` (passes, 19 Nov, gate of that afternoon): each player marks a neutral Argon freighter as hostile (the
    interact menu's "Mark as hostile" and the Yes of X4's question). Both freighters read hostile and enemy on
    both clients a moment later; in sync at all 2,020 ticks.
  - `exchange` (passes, 19 Nov): each player has a fleet courier hand 30 of its 50 energy cells to a fleet capital ship
    (the map's ware exchange between two ships of the player's: the trade menu in its exchange mode sets virtual
    cargo on both ships, lists the exchange offers, Confirm). The courier docks at the capital ship and the wares
    arrive there a few seconds after they leave its hold. In sync at all 5,618 ticks.
  - `tradeloop` (passes, 19 Nov): each player puts a fleet courier on repeat orders (the behaviour page) and gives it a
    buy of 20 food rations at MMJ-623 and a sale at NLN-075 (the trade menu's loop Confirm, see the findings). A fleet
    fighter first docks at NLN-075 so the faction knows its offer. Both couriers bought, sold and bought again on
    their own: the queue went round. In sync at all 14,635 ticks, eight captures of the world.
  - `claim --answer` (passes, 19 Nov, gate of that afternoon): the frigates' questions stay on; player 1 answers
    its frigate's question (the key on the notification) and picks "Claim it" in the conversation that follows,
    and the frigate sends its marine by itself; player 2 presses the key too and is refused, as every
    conversation is to players 2..N.
    Player 1's answer was raised on both clients at the same tick, and its frigate claimed the scout; player 2's
    was refused with a notice, and its claim by order after the questions landed. In sync at all 3,846 ticks.
  - `claim --answer --answer-by 2` (passes, 21 Nov): any player answers a fleet ship's question since 21 Nov.
    Player 2 answers the first question and picks "Claim it"; the answer took the conversation for player 2 (as a
    player's own conversation start does), so the conversation was player 2's, and frigate 1 claimed its scout by
    itself. Player 1 pressed the key two seconds later: nothing raised (the question was over). Player 1 then had
    frigate 2 claim the second scout by order. Player 2's answer raised on both clients at tick 993; in sync at all
    3,843 ticks. A second answer while a conversation runs is refused alike on every client ('busy').
  - `mapopen` (a diagnostic, passes): one client keeps the map open for 100 seconds and nothing else happens.
- **What the tests found and what was changed for it:**
  - The trade menu's Confirm button was refused to a person: it stood on the list of blocked order entries and
    nothing reopened it. The scripted trade passed all the same, because it made the button's calls itself. Open
    now with the trade family.
  - Two functions of the stock menus are named like order entries and order nothing: the trade menu's amount
    helper and the interact menu's icon text. Both were refused with the order entries, so the trade sliders set no
    amount and every order entry's text began with a 0 where its icon belongs. Both are left alone now.
  - The interact menu's Explore, Protect, Collect, Mining and the other order entries that make only the calls the
    orders family shares were refused to a person. Open now. Run since, by the calls the entries make: Explore,
    Follow, Protect station, Collect drops in radius, Collect (a deployable), Attack in range, Withdraw from combat.
  - Setting a ship's default behaviour did nothing: the parameter of the planned behaviour went to the wrong
    order, and the confirm's reset of the order loop was refused. Both fixed (the second in the gate DLL); the
    behaviour page's Confirm is open.
  - A ship purchase jammed the buyer's actions for good: its record went out in 200-byte parts, and beside the
    player's flight record only 185 bytes of a tick's packet are left for one. The part stayed first in line and
    nothing behind it was ever sent (found the same evening by the station test with a station plan; fixed there).
  - "Remove assignment" did half its work (18 Nov): X4's call that takes a ship out of its wing was not shared, so
    it was refused, while the two calls after it went through. The ship got its new default behaviour and kept its
    commander. "Assign as subordinate" was refused to a person although its calls for the plain case are all
    shared. The station test found the same two the same morning by clicking the menus and fixed them (the call is
    shared, both entries are open; the assignment "position defence" and a fleet's planned ships stay refused with
    a notice). That fix is the one in the code; `wing` passes on it.
  - X4 answers "trade computer busy" to an undock for about three seconds after a docked trade. An undock sent
    in that time is still replayed and goes through if the computer is free by then; the dock menu itself does not
    offer the button that early.
- **Found on 19 Nov, and what was changed for it:**
  - **A fleet ship that asked its player for orders parted the clients.** A frigate of the player's saw the
    abandoned scout and asked what to do: the stock script `player.interaction`, an interactive notification with
    the pilot's face. Its picture was started and ended by each client's own UI, at ticks of that client's own,
    and the script waits for both events. Runtime IDs differed from tick 1186, the inputs from 1465, and at the
    next capture 3,677 objects moved differently. The same script speaks for every fleet ship that reports an
    attack, a police halt or a lockbox with the question switched on. The picture of such a notification is now
    the simulation's, as a mission's call already was: it starts in the script's action on every client and ends
    with the script's stop. The same run (`claim --ask`) is in sync at all 9,222 ticks, five captures. Answering
    the question is still refused to every player (the UI's call is not shared): the ship acts on its standing
    order after ten seconds.
  - In that run one of the two claims failed, on both clients alike: the pod reached the scout while the
    notification's camera made the place "visible", and X4's claim script lost the pod when the place stopped
    being visible in the three seconds between attaching and claiming (the script's own comment warns of it).
    The game's, not the session's; without the question both claims land.
  - **"Deploy" on a place of the map was refused for a fleet ship.** The order carries its place, the macro and
    the amount: 223 bytes, and one event of a player's actions holds 160. It was refused as too long, so no fleet
    ship could be sent to drop a satellite, a probe or a mine. An order that long now travels in parts (up to
    four) and runs when the last has arrived, on every client at the same tick. `mining` gives two such orders
    per player.
  - **A wing's group settings were refused to every player** (listed as a known fault on 18 Nov): the five
    switches of a subordinate group, the group's role and a fleet's name are shared now (seven more calls of the
    faction family), and the interact menu's "Change assignment" is open, except for a fleet's planned ships and
    the position defence, which stay refused with a notice. `wingset` runs all of it.
  - **The trade menu's Confirm in its loop mode was refused to a person.** A fleet ship on repeat orders gets
    its buy and sale orders through it (SingleBuy, SingleSell); it was on the list of blocked order entries and
    nothing reopened it, although its calls are all shared. Open now; `tradeloop` runs it.
  - **Answering a fleet ship's question was refused to every player.** Player 1's answer (the key or the click on
    the notification) is shared now: the click names an interaction that is each client's own, so it travels as
    the notification's place among the ones the simulation started, and every client raises the answer for its
    own interaction of that notification at the same tick. The conversation that follows is player 1's, as every
    conversation is; players 2..N are refused with a notice. Note: while player 1 is in that conversation X4 lets
    no other ship ask (one question at a time, for the one MD player).
  - **"Mark as hostile" was refused to every player.** X4's call names another faction's object and passes two
    floats and a double; it is shared now as a faction call whose object may be anybody's but another player's
    ship.
  - The snapshot's sector yields nothing to mine (a scan of 1,681 places around each miner found no ore and no
    other resource), so the "Mine" order is not given there and a miner on automatic mining stays where it is.
- **The launcher's change of 18 Nov, run live** (19 Nov): a session started beside others on the first profile
  pair (`X4LS_BESIDE=1`, `X4LS_FIRST_PROFILE=0`) freed its own last client's instance mutex ("client 2: released 1
  instance mutex handle(s)"), and the other sessions' clients had none left to free; `rules` passed on it.
- **Regression on the gate of 19 Nov afternoon** (all of this section's changes): every scenario of this section
  run again, 28 runs in all, each in sync to its last tick (1,976 to 14,658 ticks); exit 0 for all but `crew`
  (the hire of player 2, known) and `claim --ask` (below). The first runs of `claim --answer`, `exchange` and
  `tradeloop` failed on mistakes of the tests (the moment of player 2's answer, the exchange mode's virtual cargo,
  a sale order with no offer in sight); fixed and run again.
- **Runs on the merged state of 19 Nov afternoon** (this branch with integration 43957e3f in it, merge 972482b8,
  gate built from that tree; integration took it as 9abd20d8): `hostile`, `claim --answer`, `wingset`, `mining`,
  `settings`, `exchange` and `tradeloop` pass. `trade` ran seven times: five in sync to the last tick, and twice
  a police traffic ship (`ship_arg_xs_police_01_a_macro`) moved differently on the two clients, from the capture
  of tick 3600 and from 9000. That is the open traffic ship fault below; the station walk work found the same
  first sign there (a ship's dock approach that starts some steps apart, from a ship that already moves
  differently). (`native_gate_zone_pending` was not on in any of these runs: it became a default only after.)
- **Fixed on 19 Nov evening: players 2..N talk and hire.** A conversation now belongs to the player who started it
  (`native_gate_conversation_slots`, a session default; the first form of 11 Nov, never merged until now, plus a fix
  found by the first live run: the gate's push of a conversation call still took player 1 alone). The start runs as
  that player's on every client, every conversation event reaches MD in that player's scope, the dialog choices, a
  menu's return and the hire fee are that player's alone. `crew` (run 20261119-195325): player 1 hired at the wharf,
  then player 2 at the shipyard through its own conversation; in sync at all 5,529 ticks. One conversation in the
  world at a time: another player's start is refused while one runs, the same on every client.
  - A conversation's dialog and the menus it opens show on its owner's screen only (the conversation itself runs on
    every client): before, player 1's hire map stayed open on player 2's screen. Run 20261119-202543: each client showed its own conversation's dialog and hire menu and held back the other player's; both players hired; in sync at all 5,539 ticks.
- **Three things the tests had to learn from the game** (no fault of the session):
  - A ship's sale is one unit: the money and the sale. The map's "Sell ships" confirm is shared as that unit. A
    script that made the two calls apart had its money refused (the session moves money only between a player and
    its own property outside such a unit) and sold both ships for nothing.
  - A station's trade offers are known to a player only while one of its ships is near the station. A freighter
    10 km from the shipyard could not be told to sell there: there was no offer to pick, for a person neither.
  - X4's count of a ship's satellites, mines and so on (`GetNumAllSatellites`...) counts the kinds, not the units.
- **Runs on the merged state** (this branch with the integration branch of 18 Nov 05:10 in it, gate built from that
  tree): `wing`, `repair`, `rules` and `trade` pass.
- **A client that went away, and why** (18 Nov): four times that night a session on this PC lost its second client:
  X4 gone with exit code 1, its log ending mid-run, no crash note of the gate, the first client playing on. Another
  session's join-test script took the first "pid N" of its join launcher's log for its own joiner and ended that
  process when the test was over. The first such line was "beside X4 pid N: released 1 instance mutex handle(s)":
  another session's client, whose instance mutex the join launcher had just freed from outside. Only a session on
  the first profile pair kept that mutex in its last client, so only those lost one. The script is corrected, and a
  session started beside others (`X4LS_BESIDE=1`) now frees its last client's mutex itself
  (`session.frees_last_client`), so no other launcher's log names its client. In a report the sign is "in sync at
  all N ticks both clients logged" with N far below the ticks of the chains.
- **Two desyncs these scenarios reproduced, and their fixes** (both fixes are the station test's; the scenarios
  proved them):
  - `refit`: only client 1 read the wharf's build tasks, and at the capture of tick 5400 a ship at the wharf
    existed on client 1 only. With the build update kept out of a menu's read (the fourth lazy target) the same
    run agrees at all 9,293 ticks.
  - `crew`: player 1's hire conversation opened the "Hire Staff" map on player 2's screen, where it stayed open,
    and from the capture of tick 3600 two or three NPC ships of one zone flew differently on the two clients, three
    runs of three. An open map refreshed the engine settings of every ship it showed, on that client only. With
    that refresh kept out of a session's steps (the fifth lazy target) the same run agrees at all 9,306 ticks.
- **Known faults.**
  - The script ops that remove a blacklist, a fight rule or a trade rule failed with "wrong number of arguments"
    (a Lua `assert` as a call's last argument passes its message too). Test code only; fixed, and `rules` runs one.
  - A desync, reproduced twice (17 and 18 Nov, `refit --together`): from the capture of tick 10800 one police
    traffic ship (`ship_arg_xs_police_01_a_macro`, made at tick 6568 in the wharf's zone) moves differently on the
    two clients, and client 2 removes it at tick 10888 while client 1 still has it 160 ticks later. Everything
    else in the world, the players' records and the inputs agree. In both runs a player's ship stood still near
    the wharf for minutes (see the next point). Of two runs with the players in turn, one was in sync for all
    18,297 ticks; in the other a traffic ship of that zone with its five parts existed on client 2 only from the
    capture of tick 7200 (runtime IDs apart from tick 7129). It matches the station walk test's open fault (a
    traffic ship that leaves at different ticks, after which one client reuses a ship and the other makes one);
    the runs are handed to that work.
  - The wharf AVK-777: all 18 pads for small ships are on its build module, 3.2 km from the station's centre, nine
    on each side. X4's autopilot reaches the first pad given out (it ends 74 m from it and X4's docking computer
    lands the ship). For the second pad (the next one, 40 m away) it ends 3.5 km short, 2.3 km from the station's
    centre, and ends at once whenever it is started again, whichever player gets that pad. Since 19 Nov the test
    then flies the autopilot to the ship docked nearest the pad (`tools.dock_neighbour`, the other player's ship
    on the next pad) and starts the approach again from there: the ship gets to 294 m of its pad and stops there;
    the dock key at that point gives the grant back. Both clients agree in every run (refit in sync at all 11,040
    and 11,070 ticks). The steering path jobs held at the phase points (dafdbc2e, the police ship's fix, gate
    build-sw58) do not change it: two runs of 19-20 Nov stop at 304 m in the same place, in sync at all 11,001
    and 11,006 ticks.
    Measured on 20 Nov (the dock step logs the ship's and the station centre's places in the pad's own frame, and
    the test takes pictures at the stall): the pads lie in a row inside the open frame of the build module, beams
    over them; the first pad is the row's end. Player 1's approach comes in from beyond the end of the module and
    along the row 65 m over the pads, and lands from 56 m over its pad. Player 2's approach and the detour both run
    the ship into the module's inner wall 200 m up (picture: 0 m/s, the wall filling the view). Since 20 Nov the
    dock step then flies the ship by its flight keys, as a person flies it (`hop`: speeds aimed at each point of a
    route in the pad's frame; the refit scenario gives player 1's lane, moved to the second pad): it reaches the
    approach point, X4's docking computer takes the ship over by itself and lifts it to 156 m straight over the
    pad, and there it holds, also after player 1 has left and the wharf builds nothing; the dock key gives the
    grant back, a new request gets the same pad. Both clients agree throughout (runs 20261120-012919 and -014328,
    in sync at all 11,042 and 11,014 ticks). Open: why the docking computer holds over that pad (X4's own docking;
    the first ship's descent from 152 m was not held). Ships that their own pilots fly in by an order (`refitorder`,
    `repair`, `shipsell`) dock without help, two at a time: in `refitorder` they sat in the docking bays 436224 and
    436225 (read as the bay a docked ship is in, `ship_look` bay=), so X4's ships were not given the pad 436331 the
    players' second ship gets.
    `refit --no-detour` keeps the old stranding, `--no-hop` the stop at the wall. The `crew` scenario sends player 2
    to the shipyard.
  - `--ai-dock` (any scenario; a test and debug tool, 21 Nov): the players' own ships dock by X4's own AI, the way a
    fleet ship docks when it is ordered to, instead of by the autopilot. X4's autopilot is not made to dock from end
    to end: it flies to a dock's approach point (`aiscripts/move.autopilot.xml`) and leaves the landing to the
    docking computer. X4 has an MD action, `dock_player_ship_at`, that no script of the base game uses: it asks the
    station for a dock and, when the dock is granted at once, gives the ship's steering to the AI and runs the fleet
    ships' dock order (`aiscripts/order.dock.xml`: the dock's entry path, its dock path, the landing) on the
    player's ship computer. The step (`ai_dock_station`) flies the autopilot to the station, stops it within 9 km and
    signals our MD cue `AiDock`; the signal travels as the player's other script calls do, and for players 2..N the
    cue runs in that player's scope, so it docks that player's ship on every client. No dock granted (all taken):
    asked again every 20 s. `ai_dock` alone sends the signal once.
    `refit --ai-dock` (run 20261121-143158): both players docked by the AI, player 2 at the wharf's second pad
    (436331) for the first time, at tick 3821; on both clients the cue docked the asking player's ship; in sync at
    all 16,146 ticks, eight captures of the world. Player 1's refit ran (ticks 3319 to about 6900); player 2's was
    ordered on both clients and stayed in the wharf's queue for the 9,000 ticks after player 1's was done. With
    `--lead 2` (player 2 first, alone at the first pad; player 1 docked by the AI at the second) player 2's refit
    stayed queued all the same and player 1's started at once: a players 2..N fault, not the pad's.
  - A wharf refits every player's own ship (fixed 21 Nov). The station's ship trader
    (`aiscripts/build.shiptrader.xml`) starts a queued upgrade of a docked ship when the ship's Equip order has
    started (its own pilot flew it in) or when the ship is `player.controlled`. The ship trader is the station's
    script and runs outside every player's scope, where that is player 1's ship; a player 2..N's own ship has an
    Equip order that nobody starts (`ship_look`: `Equip/ready`) and so its upgrade stayed queued for good: the dock
    menu said "there is a scheduled upgrade for this ship" and showed no timer. Now the session's player groups
    (`native_gate_player_groups`) keep the list of what every player controls on every player's entity, and a patch
    of the ship trader's test (`tools/dev/native_gate_player_refit.xml`, X4's own diff loading, one added clause)
    also takes a ship in that list. Without the gate the list is never set and the test is the stock one.
    `refit --ai-dock` (run 5 of 21 Nov): both players docked and refitted, player 2's upgrade with X4's timer in the
    dock menu ("Waiting for upgrade to finish"); the wharf has one build processor, so the second refit starts when
    the first is done. A refit of the wharf's high preset (two engines, the shield, two lasers) takes about 2 minutes
    of game time (3,600 ticks at 30 a second). `refit_why` (MD `RefitWhy`, read only) logs each queued build of a
    station with the parts of that test. A player's own ship is repaired at a wharf without such a patch: the dock
    menu's repair signals the station (`repairs_initiate`), and the ship trader repairs any ship docked there.
    `repair --own` (run 20261121-155134): both players' own ships, damaged to 50 % by the fixture, docked by the AI,
    paid and repaired to 100 %, undocked; in sync at all 5,575 ticks. The ship trader's other repair start (a ship
    that docks with a Repair order and is `player.controlled`) is not reached in a session: the replay refuses
    orders for any player's own ship.
  - `refit --lead 2` (17 Nov): the wharf took player 2's refit and left it queued for the remaining 8,000 ticks
    (player 1's, in the other runs, starts after about 350 ticks). Open.
  - Every player's own Plot course (fixed 21 Nov; scenario `guidance`, passes for both players). A course is its
    client's own: the map's Plot course and Remove course stay local and X4's Guidance script runs for that client.
    A player 2..N's course was made (the call runs in that client's presentation scope, where X4's player object
    holds that player's state) but not kept: the scope gives back every change of the player's active mission, so
    the HUD showed nothing and Shift+A had no target. Now a change made while the Guidance script ran in the scope is
    kept as that player's active mission on its own client (lockstep_mission_owners.h; the script RNG counts the
    script's frames), as player 1's own course has always been the canonical one on client 1 alone. A patch of the
    stock script (`tools/dev/native_gate_player_guidance.xml`, X4's own diff loading) notes whose course it is and
    checks arrival from that player's ship (from player 1, the course never ended), and in a session it no longer
    signals the ship computer that a course ended or moved: the course is one client's, the autopilot listening there
    runs on every client, and that signal on one client alone would stop or turn it there only. An autopilot started
    for a course flies on to its target and ends there; a new course while it flies wants Shift+A again.
    Fixed 24 Nov: a player 2..N's autopilot no longer ends unheard at a zone change. Its ship computer took its attention
    from the cockpit, which follows its ship; some zone changes dip the ship's attention below visible for one tick, and
    move.autopilot (which runs only from visible) then switched to an empty block and ended without its own end:
    autopilot on, the drop out of travel, the gate and the arrival unheard until Shift+A (the user's rk7 and rk8
    flights; the mission sweep's gm:GM_DeployInPlace at tick 3280 every time). X4 keeps the room of its own player
    inroom, so player 1 never had it; now a player 2..N's room is inroom too (`native_gate_pilot_slots_room_attention`,
    on by default, `lockstep_pilot_room_attention.h`; status `rat=` in PILOT_INPUTS). Run of 24 Nov on the diagnostic
    branch (gm:GM_DeployInPlace, both players, 9,763 ticks, 0 mismatches): player 2's computer inroom from tick 37, no
    dip at 3280, its script heard the approach at 4627 and ended the flight itself.
    `guidance` (run of 21 Nov): each player plots a course to a different station (player 1 to NLN-075, player 2 to
    MMJ-623), sees it on its own client, flies it by Shift+A (147 and 158 km), the course ends by itself at the
    station, a second course is cleared by Remove course; in sync at all 3,841 ticks.
  - When a player 2..N's own course ends, X4 makes another mission the active one; that could be another player's
    (two-PC game of 24 Nov: player 2's course ended and player 1's Terran Cadet story became player 2's active
    mission, so player 2's next Shift+A flew to player 1's story target). Now the course's end keeps only that
    player's own missions, or none (`guidance=kept/refused` in MISSIONS_PER_PLAYER). Unit-tested; not yet run live.
  - A travel drive an autopilot left running (same game: player 2 stopped the autopilot while it charged the drive,
    the drive took the ship to 1,420 m/s with no autopilot, and the travel key only "started" the travel it already
    had). With no autopilot and no activity of the player's own, the travel key now ends such a drive at once
    (`travel_left=` in UI_REPLAY_STATUS). Why the drive outlives the autopilot for players 2..N is the ship
    computer's missing AI frames (PILOT_AI rows, open). Unit-tested; not yet run live.
  - Open: player 2's HUD showed player 1's active mission while player 2 had none (same game, before the course
    above). The HUD's mission panel (ui/core monitors.lua) reads GetMissionInfo3, which reads the manager's active
    mission (swapped in the presentation scope), only when X4 sends a mission update. Likely cause, not confirmed:
    a panel drawn before the first presentation scope and not updated since.
  - Ship Interactions for players 2..N (fixed 24 Nov; scenario `interact`). In the two-PC games of 24 Nov (rk7, rk8)
    player 2's click on the HUD's top level bar's Ship Interactions in flight did nothing: the bar (or the map)
    closed and no menu opened (`UI_DOCKED_MENU kept=N ... scope=false` right after each click). The probe keeps X4's
    automatic opens of that menu (after a conversation, a cinematic camera, a menu closing) to the client whose own
    player is docked or sits at a post; it asked that question of every open, also a person's, and in flight the
    answer is no. The Enter key opened the menu all along (X4 opens it natively, not through the Lua `OpenMenu`).
    Now the two routes a person takes, the top level bar or tabs (`Helper.closeMenuAndOpenNewMenu`) and Back from a
    menu opened out of it (`Helper.closeMenu` 'back' with its return record), open it as in stock on that player's
    own client (`UI_DOCKED_MENU kept=0 passed=0 person=N via=tab|back`; `UI_DOCKED_MENU_ARMED slot=K helper=true`
    at the start). What the menu then does (flight assist, modes, deployables, autopilot, Dock, Get Up, weapons) goes
    through the calls the replay already shares, as for player 1. `interact`: each player opens it from the bar,
    presses its Flight Assist, opens it from the map's tabs, goes to Ship Information and Back, presses Flight Assist
    again, while the other client's dock menu is watched to stay closed. Unit-tested for slots 2, 3 and 8; run live
    as player 2 alone (`x4ls play --solo 2` of a coop session, 24 Nov): the `interact` chain passed through every step
    (the menu on player 2's own ship, flight assist off and on by the replay, Back to it), and window-message clicks on
    the HUD bar's and the map's Ship Interactions, the menu's Flight Assist and the map's Back did what they do for
    player 1. The two-client `interact` run (the other client's menu kept closed) is not run yet.
  - Not shared, so refused to every player (from the session's list of calls, not run): a wing's formation, a
    fleet's planned ships (fleet units), a group's protected position, boarding. Shared since 19 Nov: the
    settings of a wing's groups and a fleet's name.
  - The answer to a fleet ship's question: any player's since 21 Nov (`claim --answer --answer-by 2` above); the
    conversation that follows is the answering player's, its dialog on that player's screen.
  - `claim --ask`, three runs of three: player 2's claim fails on both clients alike, player 1's lands; the clients
    agree throughout (9,222, 9,269 and 9,204 ticks). Read on 19 Nov with a watch of the scouts (the fixture logs each
    second their owner and attention and every ship within 3 km): a frigate's question on the target monitor raises
    the attention around that frigate to "nearby" for its ten seconds. Pod 2 began in X4's claim script
    (`aiscripts/move.claim.xml`) at low attention, was switched to the high branch when frigate 2's question came
    up, attached itself to the scout, and the question ended before the claim: the script went back to its low
    branch at its "move" label, which never claims an attached pod, and the scout stayed ownerless for the four
    minutes watched. Pod 1 crossed the other way (high, then low before attaching) and claimed. That is X4's own
    script with X4's own question picture, the same for a single player; not patched here.
  - `trade`, one run of five: the world differed from the capture of tick 3600 with the PC loaded by other
    sessions; not reproduced, objects not logged in that run, and it was before the two fixes above.
  - A fleet ship that is part of the snapshot's battle fixture stops every client when it begins to dock (the
    fixture's state sampling has no docking state in a play session). The dock order is tested on other ships.
  - Not tried: three or more players, factions mode, a hot join during any of this, the Linux gate, two PCs.


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
  - Throttle, boost and travel drive (24 Nov, user report: speed changes and engaging or disengaging travel mode
    answered late). The speed bar asked the simulation (Lua's GetPlayerSpeed): its arrow, number and
    boost/travel colour came the input delay after the key, and a wheel step or a stop moved the drawn ship that late
    too. The bar now answers from your own latest inputs, in the frame of the key: the set speed by X4's throttle rules
    (step sizes fitted from the simulation), boost and travel at once, a refused one (no boost energy, a jammed drive)
    gone again within a few ticks; the speed number is the speed of the ship on the screen, and the drawn ship takes the
    predicted throttle (`native_gate_own_throttle`, a default; `false` turns it off; lockstep_own_throttle.h). Still on
    the simulation's time: the boost energy bar, the travel charge sound, tint and messages, engine sounds and effects.
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
- **Do not resize or maximise an X4 window during a session.** X4 reloads its UI on a resize; the session's script then
  starts again in the middle of the session, is refused and that client stops (11 Nov, twice). `x4ls play --borderless`
  starts the clients of this PC in X4's borderless window, which cannot be resized. Picking up after a UI reload is not
  built.
- **A story start as the session's own start:** the Terran cadet start can be captured since 11 Nov (X4 allows no save
  while the wingmate's first line plays; the capture waits for it). Its mission chain belongs to player 1. First hand-on
  play: the step "replace the satellite" was blocked, deploying a satellite was refused; deployables are shared since
  11 Nov (the ship menu's and the map's deploy entries; the deploy keys stay refused).
- **A start's story for the player who took that start, first form (11 Nov, by name):** until now only player 1 had a
  start's story; a player in slots 2..N who took, say, the Terran cadet start got its ship and place and none of its
  missions. `x4ls new-session ... --start K=<start> --omit K=world --story K` gives slot K the start's own script
  (`x4ls starts`: `player-start=ship+story`): X4's script runs unchanged as that player, 60 ticks into the session (30
  ticks after the join for a slot that joins). Since 24 Nov a further player on the same start plays their own
  copy of the story ("Several players on one start" above).
  Run on 11 Nov (two X4 on one PC, discover as the session's start, slot 2 on the Terran cadet start, 3 minutes, the
  same result twice): the cadet mission began for player 2 on both clients, was player 2's active mission and listed on
  client 2 only, the wingmate's lines were spoken on client 2 only, and it went from part 1 to part 2 when player 2's
  ship reached the satellite, at the same game time on both clients; 0 desyncs, world checks equal at every check.
  Not shown yet: a slot that joins a running session, any start but the Terran cadet, a person playing it through.
  What the cadet chain still needs for a player 2..N after the first parts: leaving the ship in a spacesuit and the
  repair laser, and long-range scan (works for every player since 17 Nov, see the mission tests).
- **After a person's session with that story (11 Nov, the same evening): what was fixed, with three scripted runs** (two
  X4 on one PC, discover as the session's start, slot 2 on the Terran cadet start with its story, 3640 ticks each; slot
  2 gets up from its seat, walks forward and sits down again; scripted input, not a person's keys):
  - A map open on one client no longer splits the session. Stock scripts ask whether a full-screen menu is open, and
    each PC gave its own answer: one client queued a crew scene the other started. While a session runs, scripts now
    get "no menu open" on every client. Side effect: a hint or an ambient line that waits for a menu to close can come
    while a map is open. (All three runs armed it; that the split is gone needs a person with a map open.)
  - Help texts, notifications, a menu a cue opens, the cockpit tint, a screen fade, music and achievements of a cue
    that belongs to one player (a mission they accepted, their start's story) run on that player's client only. In the
    runs client 1 skipped one such action of player 2's story (its music) and client 2 ran it; runtime IDs and world
    checks stayed equal.
  - Coop relations: a player's start can only raise the shared relations, not lower them. With discover as the
    session's start and a Terran cadet in slot 2, Terran went from hostile to friendly on both clients.
  - A player 2..N who gets up can walk: their client asks for the walking controls although X4 counts only player
    1's seat. The scripted run shows the request on client 2 and none on client 1; the keys of a person are the proof
    still owed.
  - A player's body that is parked while they walk keeps its place for the scripts: the cadet story stayed in its
    part while player 2 walked, and the script error about a distance to the player did not come back.
  - Autopilot and "is this player the pilot" for players 2..N: inside a player's own scripts their ship now answers
    that this player is its pilot (before: the hull's computer pilot, so the autopilot refused with "you are not
    piloting this ship"). Run by its author: player 2's autopilot flew 40 km to a station and stopped on both clients.
    In the get-up run the answer went back to the hull's own pilot while player 2 walked and returned at the sit-down.
  - **Known from these runs:** moving the mouse while a player 2..N walks makes the clients disagree about that
    player's walking record until they sit down (454 ticks in one run; the world checks and runtime IDs stayed equal
    and the session went on). `x4ls new-session --unpiloted K` (a hull without the computer pilot who otherwise
    appears in the seat when the player gets up) does not work yet: the session stops at its start (reason 161, the
    slot's ship is not resolved without a pilot).
  - Still open from that session: the spacesuit terminal, long-range scan for players 2..N, the mission counter on
    another client's HUD counting a mission that is not theirs.
- **A person's second session the same evening (20 game minutes, player 2 on the cadet story, 0 mismatches, no errors):**
  the story ran parts 1 to 6 for player 2 and stops at part 6, which needs the player to leave the ship in a spacesuit
  (not built for players 2..N yet). Found there:
  - **The comm monitor of a story call showed a bare hull instead of the speaker.** Fixed, a session default on the
    Windows gate: the camera of a call the simulation itself starts now counts as a viewer on every client, so the
    speaker's ship has its interior and the speaker is there to film. Two scripted runs (two X4, the wingmate's two calls,
    screenshots of both windows): her face in her cockpit on the monitor, on both clients, 0 mismatches, world checks and
    runtime IDs equal. A Linux client still films the hull (the port is owed), and a player who joins while a call runs
    does not get that call's picture.
  - Known and open: the call's monitor and subtitles also appear on the other players' screens (the voice does not);
    "Autopilot Activated" is shown on every screen when one player engages theirs; the computer pilot still takes the
    seat when a player gets up, the start's relation changes are announced one by one, and the cockpit HUD stays while
    walking (all three are built and wait for their runs).
  - The stick while the autopilot flies (24 Nov, action test `apsteer` on the diagnostic branch, the stick's yaw axis
    held for 3 s three times): it turns neither player 1's ship (X4's own player) nor player 2's while the autopilot is
    engaged, before travel and in it, and turns both after Shift+A. X4 9.00 hands the ship to its pathing while the
    autopilot is on (the PlayerController's flight targets come from the agent, the HUD's steering strength reads 0),
    so player 2 flies as player 1 does here.
- **Not built yet:** stories other scripts run for the whole game (faction plots) per player; they stay the
  session's (player 1's). The headquarters plot has a first form per player in factions mode (`--plot K=hq`,
  "A headquarters and research per player faction" above).
  - Personal missions per player, first form (9 Nov, on by default; `--set native_gate_missions_per_player=false`
    turns it off): a mission player 2 accepts is player 2's on every client. Its script sees player 2, their ship and their sector
    (mpp1: 900 ticks, 0 desyncs).
  - Each player has their own active mission, and each client's mission menus list its own player's missions (mpp2,
    9 Nov: two players, a mission each, 900 ticks, 0 desyncs).
  - Generic mission offers are made around every player, not only player 1: about every 3 minutes X4's own offer
    selection runs for each further player's sector, and the offers it makes follow that player (mpp5, 9 Nov: player
    2's selection made 8 offers on both clients, 2400 ticks, 0 desyncs). Rewards in factions mode are not per player
    yet.
