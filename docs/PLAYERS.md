# Players: actions and identities

Scope of the player side of lockstep (3 November 2026). Hotjoin and player presence
(seeing each other, walking around stations) are Silver's; this document covers what
players *do* and *who* they are.

## Principle

Lockstep keeps the world identical on every machine. That world contains several
players: each is a separate entity (ship, faction, money, reputation), and every machine
simulates all of them. A player's action is an input: it crosses the relay and is
applied on every client at the same tick. Anything a UI changes directly on one machine
is a desync.

## What exists

| Area | State | Where |
|---|---|---|
| Identity per slot | Slot k's ship is owned by `x4lan_player<k>` (16 pool factions, allied to `player`) or by `player` (coop, at most one slot). A session plan applies faction, ship, start, relations through MD at gated ticks; tested natively with three clients (factions and coop) | `tools/dev/native_gate_player_identity*.{lua,py,xml}`, `src/lockstep/base/libraries/factions.xml`, `reports/LOCKSTEP-PLAYER-IDENTITY-20261029.txt` |
| Input wire | Protocol 3: one blob (up to 240 bytes) per client per tick, sections multiplexed: pilot, hits, octree, orders `P`, stock piloting `S`, UI replay `U` | `docs/LOCKSTEP-NSLOT-WIRE.txt`, `tools/dev/native_gate_inputs_core.lua` |
| Player orders | move, attack, follow, protect, dock, hold, buy, sell, assign: captured from the interact/map menus, relayed, executed as MD `create_order` | `tools/dev/native_gate_player_orders*.{lua,xml}`, `reports/LOCKSTEP-PLAYER-ORDERS-20261029.txt` |
| UI replay | travel, soft target, autopilot, dock/undock, weapon groups, order queue edits: X4's own functions captured and replayed in the slot's scope | `src/native/engine/lockstep_ui_replay*.h`, `tools/dev/native_gate_ui_replay_core.lua`, `reports/LOCKSTEP-UI-REPLAY-20261030.txt` |
| Virtual players | X4's own player control path run once per slot with that slot's player block: steps 1-4 and 7 built (flying, own camera and HUD), 5 partial (weapons), 6 open (MD activities) | `src/native/engine/lockstep_virtual_players.h`, `lockstep_pilot_slots.h`, `lockstep_pilot_present.h`, `reports/LOCKSTEP-VIRTUAL-PLAYERS-RE-20261029.txt` |
| UI write census | **new**: every non-query `X4.exe` export the UI calls through FFI, outside the relayed-input bracket, counted with rows | `src/native/engine/lockstep_ui_exports.h` (`native_gate_ui_exports = 'observe'`) |

## The surface

X4's Lua UI reaches the engine only through FFI exports of `X4.exe` (and through a few
Lua globals such as `AddUITriggeredEvent`, which feeds MD cues). 9.00 exports 2,378
named functions: 1,576 read as queries (Get/Is/Has/...), 802 do not. Of these, about 306
are presentation (maps, menus, camera, sound, options); the rest change the game:

| Family | Exports | Examples |
|---|---|---|
| Orders, fleet | 51 | `CreateOrder*`, `AdjustOrder`, `AddFleetUnitSubordinate`, `AssignHiredActor` |
| Build, stations | 45 | `AddBuildTask*`, construction plans, `Dismantle*` |
| Ships, equipment | 74 | loadouts, `ClaimShip`, crew, drones, turret modes |
| Trade, wares | 30 | `AddTradeWare`, `CreateTradeRule`, buy/sell limits, `DropCargo` |
| Missions, relations | 27 | `AbortMission`, guidance, diplomacy |
| Player control | 40 | `MakePlayerOwnerOf`, `MovePlayerToSectorPos`, player alerts |
| Money | 6 | `AddPlayerMoney`, `TransferPerson` |
| Other | ~200 | research, boarding, terraforming, sectors |

`tools/dev/ui_export_census.py` reads the installed game read-only (export table, UI Lua
unpacked to a temporary directory) and maps every non-query export to the UI scripts
that call it (full map in `.local/ui-export-census.json`). Of 804 such exports the 81 UI
scripts call 668: about 406 presentation, 62 unclassified, and the relay worklist:

| Family | Called by the UI | Mostly from |
|---|---|---|
| Orders | 48 | map, interact menu, helper, player info |
| Ships, equipment | 46 | map, docked menu, interact menu, ship configuration |
| Missions, relations, boarding, research | 30 | map, diplomacy, interact menu, player info |
| Trade, wares | 28 | helper, station overview, player info, map |
| Player control | 23 | player info, interact menu, docked menu, platform undock |
| Build | 21 | station and ship configuration, map, station overview |
| Money | 4 | map, blueprint/licence trader, inventory trader |

Of those 262, the existing relay code (UI replay, player orders) names 28: the order
queue (`CreateOrder*`, `AdjustOrder`, `EnableOrder`, `RemoveOrder*`, loops, planned
default orders), docking (`QuickDock`, `RequestDockAt*`, `UndockPlayerShip`), guidance,
weapon groups and modes, soft target, autopilot, flight assist, player activities. The
other 234 are uncovered; some of them are pure functions with query-like effects
(`Filter*`, `Generate*Text`) that the name rule misses and the runtime census will drop.

The first native run of the census (600 ticks, scripted pilots, no human input) saw the
idle UI call 10 non-query exports, all presentation: `SetWidgetAssociatedElement`,
`SetMonitorExtents`, `SetCrosshairMessage`, `MarkIndicatorShown`, `TrackMenu`, ...

## The guard and the generic relay

`native_gate_ui_exports` (`lockstep_ui_exports.h`) redirects the export table entries of
every non-query code export (802 on 9.00) to 16-byte stubs; the common handler records
the call and tail-jumps to the original, so engine functions run with no stub frame.

- `'observe'`: every UI call outside the relay bracket in an admitted frame is counted,
  first calls as `UI_EXPORT_ROW` (name, tick, thread).
- `'block'`: exports named in `native_gate_ui_exports_block` are refused (the handler
  returns 0 without calling them). `tools/build_soak_overlay.py --ui-guard` blocks every
  game write the static census finds that the relay does not carry (234 on 9.00).
- relay (`native_gate_ui_relay = {Name='kinds'}`, with `'block'`): the call is refused and
  captured. Kinds come from the export's FFI declaration (U UniverseID, q 64-bit, i 32-bit,
  b bool, f float, d double, s string); UniverseIDs become portable keys (saved, or created).
  The probe sends the records in input section `0x58 'X'` (Lua, Python, C mirrors); at the
  apply tick every client, the caller too, maps keys back to its runtime IDs
  (`gate.runtime_of`) and calls the export through its original entry with a pointer typed
  from the kinds. X4 refuses some FFI symbols to extension code ("Access to FFI function ...
  is restricted!"); the pointer avoids symbol resolution.

Native check (3 November 2026, two clients, `native_gate_ui_exports_test`): client 1's UI
calls `AddPlayerMoney(100000)` at tick 600. `'observe'` counts it on client 1 only (and
the money then differs); `'block'` refuses it (`blocked=1`); with the relay
(`native_gate_ui_relay = {AddPlayerMoney='q'}`) it is captured on client 1, crosses the
relay, and both clients run it at tick 602 (`FFI_RELAY tick=602 slot=1 ... ok=true` on
each): 10,000 -> 11,000 credits on both at tick 605, session in sync.

`tools/dev/ui_export_census.py` derives the kinds from the UI's cdefs: 199 of the UI's
game writes take only scalars (`size_t` included), IDs and strings, and
`tools/build_soak_overlay.py --ui-relay` relays them (the 28 the relay code already carries
excepted) and blocks the rest. The rest take structs; only 14 of those are plain data
(`UIPosRot`, `Coord3D`: map editor spawns, build plots), the others carry pointers
(`UILoadout2`, `CrewTransferInfo2`, `TradeRuleInfo`, ...) and need per-type marshalling.
The return value of a relayed call is 0 on the calling client at the time of the click (the
action happens a few ticks later on every client); menus that use the return value (a
new build task's ID, say) need a follow-up read.

The UI also calls C functions registered as Lua globals (`GetPlayerMoney`,
`TransferPlayerMoneyTo`, `AddUITriggeredEvent`, ...). They do not pass the export table;
the UI looks them up by name on every call, so a Lua wrapper on the global is the guard
for them (the probe already wraps `AddUITriggeredEvent` for the census). X4's UI Lua has
no debug library, so the census lists all global functions and the C ones are told apart
offline (names the UI scripts do not define).

Relaying the Lua-global writes (not only blocking them) needs per-function argument
kinds too: the UI passes components to them both as LuaID userdata
(`AddTradeToShipQueue(ConvertStringToLuaID(...))`, recognizable by type) and as plain
numbers (`TransferPlayerMoneyTo(fee, ConvertStringTo64Bit(...))`), which look like the fee
beside them. With a kind list per global (component, number, string, boolean, table) the
'ffi' section can carry them as records with index 0xffff and the global's name.

## Stage A: a human census session

Both clients run on one PC (two windows); one person can play each window in turn.
Shared faction (`coop`), ships flown by wire, both guard layers observing:

```
python tools/build_soak_overlay.py --snapshot .local/snapshots/seed276944566.xml.gz --gate-dll .local/native-lockstep/build/x4lan_gate_64.dll --out .local/overlays/human-census --play-mode --drop native_gate_camera_anchor --set "native_gate_ui_exports='observe'" --set "native_gate_ui_lua='observe'" --players coop --players-arg=--source --players-arg=wire --players-arg=--orders --players-arg=none --window 216000
python tools/x4ls.py session --clients 2 --overlay .local/overlays/human-census --snapshot .local/snapshots/seed276944566.xml.gz --ticks 216002 --observe --lookahead 1 --timeout 12000 --steam-fix
```

Then, in each window: fly, dock, open the trade menu and buy, accept a mission, give a
ship an order, open the map. The logs (`clientK/engine-output.log`) list every UI write as
`UI_EXPORT_ROW` / `UI_LUA_ROW` with its tick; what is not presentation goes on the relay
list (or stays blocked).

## Player-activity soak

`tools/build_soak_overlay.py --players factions` (identities, script pilots, scripted orders
through the relay, the soak profile's determinism keys) on the seed snapshot: slot 1 keeps
`player` (the occupied ship), slot 2 owns its ship as `x4lan_player2`. 3 November 2026:
36,003 ticks (20 game minutes) in sync on two native clients; slot 1's attack order for slot
2's ship was refused on both (`refused_not_owner`), slot 2's own applied on both at tick 43;
the UI write census saw only presentation calls.

## Gaps

1. **Unwrapped FFI writes.** The orders and UI replay capture work on known menu
   functions; a direct FFI call elsewhere is not intercepted. The export census closes
   the observation gap; a `block` mode (refuse a write outside the relay bracket unless
   the export is on a presentation allowlist) is the next step once the allowlist exists.
2. **Missions, conversations, trade menus, research, diplomacy:** not captured.
3. **Money.** One `player` wallet, slot 1's. Pool factions' accounts are unused, so buy
   and sell for slots 2..N fail (deterministically).
4. **Player-centric MD.** Missions, encounters and notoriety centre on `faction.player`
   and `player.entity`, i.e. slot 1.
5. **About 1,041 engine functions read the player singleton** (virtual players report).

## Plan

| Stage | Goal | Work |
|---|---|---|
| A | Census of real play | Run the UI write census during a human session on each client (fly, dock, trade, accept a mission, order a ship). Classify every export seen: presentation (allowlist) or simulation (must be relayed). |
| B | Guard | `native_gate_ui_exports = 'block'`: a simulation export called outside the relay bracket fails (returns its failure value), counted. Turns every missing relay into a visible refusal instead of a silent desync. |
| C | Co-op actions | One shared faction (`coop`): relay the families stage A finds, in order of use: orders (exists), trade (buy/sell menus), docking services, mission accept/abort, money transfers. Each as a `P`/`U` section op executed through X4's own function in the slot's scope. |
| D | Separate factions | Per-slot faction with its own account: wallet operations on the slot's faction; relations already per faction. |
| E | Per-player missions and reputation | Through virtual players (step 6: MD activities per slot) or per-slot mission ownership; the hardest stage. |
| Test | Player-activity soak | Both clients run scripted actions through the command path (trades, docking, mission accepts, orders) at different ticks, with the census on: any write outside the bracket or any digest difference names the hole. |

Stage A needs a person at each client; B and the test soak can be built and run
unattended.
