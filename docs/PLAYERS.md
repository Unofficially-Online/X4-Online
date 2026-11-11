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

With `--ui-relay` the Lua-global writes are relayed too (`native_gate_ui_lua_relay`). The
wrapper encodes the call and returns nil. Every client then makes the call at the apply tick,
carried in the 'ffi' section as a record with index 0xffff and the global's name
(`tools/dev/native_gate_ffi_relay_core.lua`). Each argument carries a tag. A LuaID userdata
or a uint64 cdata is a component wherever it appears and travels as its portable key
(`gate.key_of`). The receiving client rebuilds it in the same form from its own ID
(`gate.runtime_of`). A plain number is a component only where the global's kind string has
`c`: in `TransferPlayerMoneyTo(fee, container)` both are numbers, and the kind string is
`-c`. `LUA_RELAY_KINDS` in `tools/build_soak_overlay.py` lists 19 globals with kinds taken
from the 9.00 call sites. `AddTradeToShipQueue` and `RaisePlayerInteractionEvent` stay
blocked: their trade offer and interaction IDs are not components. So does
`StartDiplomacyActionOperation`, which takes a table. A call that cannot be encoded is
refused and counted (`relay_refused=`). One gap remains: a component passed as a plain
number in a place not marked `c`, such as one of SetOrderParam's values, would arrive as that
client's runtime number. The Lua write test (`native_gate_ui_exports_test={tick=T,
slot='pK', via='lua'}`) calls `TransferPlayerMoneyTo(500, ship)` with the ship as a plain
number. Native check (3 November 2026, two clients, seed snapshot, `--ui-relay`): client 2's
call was queued (`relayed=1`) and both clients ran it at tick 602 (`FFI_RELAY ...
lua=TransferPlayerMoneyTo ok=true`); the session passed. Money and the ship's account both
read 10,000 before and after on both clients: the player's own ship has no account of its
own, so a transfer to it moves nothing. A visible check needs a container with its own
account (a player station or a managed ship).

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

1. **Struct-argument writes.** The guard blocks or relays every game write the UI makes
   through the export table or a Lua global. The writes that take structs with pointers
   (`UILoadout2`, `CrewTransferInfo2`, `TradeRuleInfo`, ...) stay blocked until they have
   per-type marshalling.
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

### Stage D: per-slot wallets (done on main)

Silver's trade work (main, 3 November 2026) gives every pool faction `x4lan_player<k>` its own
money account: `<account amount="0" />` in the pool library (here
`src/lockstep/base/libraries/factions.xml`) and the account created once at the fleet tick on
every client. Without one, X4 answers a faction's money from a dummy account holding a large
random sum. Live trade runs moved the slot's wallet exactly as planned on both clients
(5,000,000 -> 4,900,000 -> 4,925,000 cents). Missions (offers, briefings, accept, abort) replay
the same way (`native_gate_ui_replay`). What stays open is the generic relay's money globals
(`TransferPlayerMoneyTo` and the like): they still move the `player` wallet, not slot K's.

### Each player's own inventory (family `inventory`, 11 November 2026)

Every slot has its own player entity, and with it its own inventory. X4's inventory functions take "the
player" from the universe's player singleton, and slot k's scope makes slot k's entity that singleton on every
client. The family `inventory` (`tools/dev/native_gate_ui_inventory_core.lua`, native part
`src/native/engine/lockstep_ui_inventory.h`; on in the session defaults, it needs the UI guard and, for slots
2..N, missions per player) turns each click on these stock buttons into one event of the client's slot. At the
apply tick every client checks the click against the world of that tick and then runs it whole, or not at all.

| Stock button | What runs at the apply | Where |
|---|---|---|
| Craft (inventory page, crafting bench) | resources out, product in, after X4's own resource check | the slot's scope |
| Confirm of an NPC trader or a lockbox | the price, then the wares each way; trader's stock, player's stock and money checked first | wares in the slot's scope, money in the slot's faction scope |
| Drop (inventory page) | X4's `DropInventory` for the slot's player, amounts checked first | the slot's scope |
| Install / dismantle an equipment mod | the build family's record, now also for slots 2..N | the slot's scope |
| Confirm of the blueprint or licence trader | everything bought and the price, after the money check | player faction (coop: every slot; factions: slot 1) |
| Research: start, cancel | `StartResearch` / `ClearProductionItems` on a module of the player's | as above |
| Terraforming: start, abort | X4's own `Can...` check, then the call | as above |
| Cancel on a station's ware reservation | the reservation both clients see (ship, ware, amount, direction) is found again and cancelled | as above |

A start's own inventory (kit part `inventory`) is added to the player who took the start: eight ticks into the
session, or twelve ticks after the hand-over for a player who joins. On a client of slots 2..N the menus read
that slot's own inventory (the presentation scope keeps slot 1's entity, so the two Lua readers answer per
slot there). An encyclopedia entry a menu or the HUD adds for what a player has seen (`AddKnownItem`) is sent once
and added on every client: one library, whoever saw it.

A player who joins a running session gets the others' inventories with the join data: the player entities of slots
2..N are the gate's own and in no save, so what each holds at the save tick rides the join package and the joiner
gives it to its own new entities before any inventory event runs.

Refused with an info line, nothing happens anywhere: the PAUSE, QUICKSAVE and QUICKLOAD keys and the save and
load menu keys while a session is live; a trader click in factions mode without the trade family; the
blueprint, licence, research and terraforming buttons for slots 2..N in factions mode (X4 keeps those for the
player faction alone); a trade of more than 60 different wares in one click (a trade of up to 60 goes out in
parts that change nothing by themselves and runs whole with its last event).

No stock menu of X4 9.00 calls `ClearTradeQueue`, `MakeRepairPriority`, `ToggleFactionTradeRestriction`,
`SetNPCSkill` or `AddUnits` (the UI Lua has no caller), so there is nothing to share there; the guard keeps
refusing them. Still open:
`CreateNPCFromPerson` (a crew member made pilot of a
ship without one: it needs `SetEntityToPost` of the fleet entries in the same click), ship building and crew for
slots 2..N in factions mode, and the paint mod list of the ship configuration on clients 2..N (the native reader
`GetInventoryPaintMods` answers slot 1's there; the equipment mods use the Lua reader and are per slot).

Evidence. Run inv1 (11 Nov, two X4 on one PC, slots 1 and 2 of the ns8 session, scripted clicks through the
same capture a person's click takes): the two player entities were 432020 and 1447949 on both clients; seeds
into each reached only that player; slot 2's craft of two first aid kits ran on both clients, slot 1's craft
without the resources was skipped on both and its next one ran; each client's Drop was captured in the guard's
hook and ran for its own player on both; the amounts read at the end were equal on both clients; 0 digest
mismatches to tick 900. The same run showed the two faults fixed afterwards: the PAUSE key stopped the pressing
client's gate (reason 3) and with it the session, and client 2's own menu read answered slot 1's inventory.

Run inv2 (11 Nov, the same two clients, the fixes in): the same steps gave the same amounts; client 2's own menu
read answered slot 2's inventory (3 bandages after the seed, where inv1 had shown slot 1's 1); real key presses of
Pause, F5 and F9 into each game window arrived as the input actions PAUSE, QUICKSAVE and QUICKLOAD and were refused
on that client (six `PILOT_NATIVE_REFUSED` rows), no save file appeared in either profile, and the session ran to
its end at tick 7240 with 0 digest mismatches and 0 drops.

The trade menu's amount sliders. A slider of the map's trade menu plans on the ship's virtual cargo: it calls
`AddCargo` / `RemoveCargo` with the fourth argument true (`AddAmmo` / `RemoveAmmo` with the fifth) and computes
with the answer. The guard refused those like every cargo change of a menu, a refused Lua function answers
nothing, and the slider's handler then failed on the missing number, so a person could not set a trade amount.
The family runs the virtual calls, and the craft button's room check, at once on the asking client. Run inv3
(11 Nov, two X4): after the trade menu's open (`SetVirtualCargoMode`, the trade family's event) a virtual
`AddCargo` of 5 and of 7 answered 5 and 7, one of 100000 answered the 536 that still fitted; each client's
planning copy differed from the other's for some 2,300 ticks until its close, and the session ended at tick
3640 with 0 digest mismatches. Not run: a person's own hand on a slider, and the real menu from open to
Confirm.

A start's inventory. Every stock start gives its player the spacesuit's repair laser and the object scanner
through the start's inventory (the intro start also five limpet mines and the demolition tool); with the part
left out of every kit, as it had to be, players 2..N had neither. Run inv4 (11 Nov, two X4, the ns8 session with
its kits made again by `native_gate_session_start.py kits`, the published DLL build-st32): at tick 44 slot 2 held
no repair laser and slot 1 one (the save's); `KIT_INVENTORY tick=48 slot=2 why=window wares=2 applied=1` on both
clients; at tick 100 slot 2 held one laser and one scanner on both and client 2's own menu read showed them;
0 digest mismatches to the end (tick 3640). Not run: the same for a player who joins (twelve ticks after the
hand-over).

Not run in the real game yet: the trader, the blueprint and licence trader, research, terraforming, a cancelled
trade deal, the mods of
slots 2..N, a trade in parts, the shared encyclopedia entries (mocked tests and the C tests
only), and any of it across a hot join (the inventories' ride in the join data is mocked-test only too: it needs
three X4, one more than this PC runs).
