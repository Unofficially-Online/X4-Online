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
--attention-initial-refresh --shape-rng --shape-initial-refresh --shape-refresh view` (9 Nov: equal to Windows at every
compared tick through 120; a few ulps apart at tick 150, still being chased). Each PC writes its own
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

Still open: a join whose save tick falls while a fixture gun is engaging (hj24: T between 668 and 684 in the battle
fixture) or right after some part animations start (hj23) can part within a few ticks; x4mods-28 is closing those.
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
- **Not built yet:** an in-game lobby and join menu (the start choice and the join go through `x4ls`), and
  per-player story missions (in progress).
  - Personal missions per player, first form (9 Nov, on by default; `--set native_gate_missions_per_player=false`
    turns it off): a mission player 2 accepts is player 2's on every client. Its script sees player 2, their ship and their sector
    (mpp1: 900 ticks, 0 desyncs). The mission list is still one per PC, so every client's HUD lists every player's
    missions, and offers are still made around player 1.
