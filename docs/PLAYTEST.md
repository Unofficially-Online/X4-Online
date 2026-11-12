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
  same way but not run in the real game yet: an NPC trader's wares, the blueprint and licence trader, research,
  terraforming, equipment mods for players 2 and up, the inventory your start brings (docs/PLAYERS.md has the
  table and the runs)

**Refused:** any other menu action that would change the world on one client only.

- X4 shows an info line ("... refused" or "... is off during a lockstep session") and nothing happens.
- Each refusal is one row in `errors.log`.
- Pausing and saving are refused this way. The options menu and the ship configuration menu then open without
  pausing. Since 11 Nov the Pause key, F5 (quick save) and F9 (quick load) are refused too ("no pause, save or
  load in a session"): in run inv1 the Pause key, which does not go through `Pause()`, stopped the pressing
  client and the session stood still for everyone; in run inv2 all three keys were refused on both clients and
  the session ran to its end in sync.
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
everyone for about as long as the joiner took to load (hj28: 75 s); a faster joiner PC pauses less. Since 10 Nov a
joiner replays without the 30-per-second hold. Measured with two X4 on one PC: a simulation step costs about 25 ms
there, so the replay ran at 35 steps a second and the pause was 62 s instead of 70 to 77 s. The hold was not the
limit on that PC; a joiner on its own PC should gain more, which is not measured yet.

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
- the client logs: `clientK/engine-output.log` (play mode: about 5 MB per client-minute; `--full-logs` overlays
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
- Aim assist: without X4's manual aim (no mouse steering) and with X4's aim assist option on, a player's guns track
  that player's selected target while it is in the gun's range (`native_gate_pilot_aim_assist`, a default). It is X4's
  own aim on a target, lead included. aim3 (with X4's range limit): the target was 13.8 km away against a reach of
  10.1 km and the guns stayed at rest. aim4 (the two players flew to each other first, 1700 ticks): with the target
  1.9 km away the guns turned to it and tracked it for the 800 ticks of the assist, the same readings on both PCs;
  0 desyncs, 0 errors.
- Not yet: the mounts follow the aim point only, not the target's speed, so fast crossing targets may be tracked a
  little late; fixed guns and turrets are as before.
- Host and joiners need the same kit: the input records changed.

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
  ticks after the join for a slot that joins). One story per start and session (the first slot with that start).
  Run on 11 Nov (two X4 on one PC, discover as the session's start, slot 2 on the Terran cadet start, 3 minutes, the
  same result twice): the cadet mission began for player 2 on both clients, was player 2's active mission and listed on
  client 2 only, the wingmate's lines were spoken on client 2 only, and it went from part 1 to part 2 when player 2's
  ship reached the satellite, at the same game time on both clients; 0 desyncs, world checks equal at every check.
  Not shown yet: a slot that joins a running session, any start but the Terran cadet, a person playing it through.
  What the cadet chain still needs for a player 2..N after the first parts: leaving the ship in a spacesuit and the
  repair laser, and long-range scan (the scan's charge runs in a per-frame update only player 1 has).
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
    "Autopilot Activated" is shown on every screen when one player engages theirs; with the autopilot engaged the ship
    does not answer the stick as it does in the stock game; the computer pilot still takes the seat when a player gets
    up, the start's relation changes are announced one by one, and the cockpit HUD stays while walking (all three are
    built and wait for their runs).
- **Not built yet:** stories other scripts run for the whole game (the HQ, faction plots) per player; they stay the
  session's (player 1's).
  - Personal missions per player, first form (9 Nov, on by default; `--set native_gate_missions_per_player=false`
    turns it off): a mission player 2 accepts is player 2's on every client. Its script sees player 2, their ship and their sector
    (mpp1: 900 ticks, 0 desyncs).
  - Each player has their own active mission, and each client's mission menus list its own player's missions (mpp2,
    9 Nov: two players, a mission each, 900 ticks, 0 desyncs).
  - Generic mission offers are made around every player, not only player 1: about every 3 minutes X4's own offer
    selection runs for each further player's sector, and the offers it makes follow that player (mpp5, 9 Nov: player
    2's selection made 8 offers on both clients, 2400 ticks, 0 desyncs). Rewards in factions mode are not per player
    yet.
