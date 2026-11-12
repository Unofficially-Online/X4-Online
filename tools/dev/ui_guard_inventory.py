"""UI guard inventory (native_gate_ui_guard, default off; reports/LOCKSTEP-UI-GUARD-20261101.txt).

Our own classification of the stock UI entry points that change the world, taken from the inventory of
reports/LOCKSTEP-UI-REPLAY-20261030.txt (P0 left-overs, P1, P2, P3, the Lua C globals) by reading which functions the
base game's menus call. Names only: the API identifiers X4 exports or registers. tools/dev/gen_ui_guard_table.py turns
this list into src/native/engine/lockstep_ui_guard_table.h against the pinned X4.exe.

Each entry: name -> (kind, group, covered). kind 'ffi' is a PE export, 'lua' a Lua C function the engine registers as
a global. covered: the UI replay families whose native capture sits at or below this entry; while one of them is armed
the guard leaves the entry to the replay. Anything else is refused in the local phases while the guard is armed.
What is NOT listed stays local on purpose: widgets, map display, filters, options (the settings fingerprint covers the
game options), cameras, read flags, logbook, player alerts, construction-map editing before its confirm, custom game
starts, guidance (native_gate_guidance_local), online services."""

TRAVEL, TARGET, ORDERS, AUTOPILOT, DOCK, WEAPONS, MISSILES, QUEUE = 1, 2, 4, 8, 16, 32, 64, 128
FLIGHT = 512   # the flight family: flight assist (0x710d50 below the export), the HUD's turret and drone toggles
TRADE = 1024   # the trade family (lockstep_ui_trade.h): the trade menus' Lua globals (wrapped) and station trade settings (hooked)

# Groups, in the order the generated table lists them (also the log's group names).
GROUPS = ('flight', 'orders', 'fleet', 'trade', 'faction', 'assets', 'build', 'conflict', 'missions', 'events',
          'conversation', 'session', 'cheat', 'editor')


def _ffi(group, names, covered=0):
    return {n: ('ffi', group, covered) for n in names.split()}


def _lua(group, names, covered=0):
    return {n: ('lua', group, covered) for n in names.split()}


# AddUITriggeredEvent calls that are UI-only, one per client, by (screen, control): the guard and the events family let
# them run on the client that made them (reports/LOCKSTEP-UI-GUARD-20261101.txt section 7). Each one: no MD cue of the
# base game or a DLC listens for that screen (only scenario_tutorials' unfiltered debug cue, tutorial start only). Mirrors:
# lockstep_ui_guard.h uiguard_ui_only, native_gate_ui_replay_core.lua K.UI_ONLY.
#   TopLevelMenu ''  Helper's menu show of the HUD's top level menu: menu_toplevel.lua opens it at the first game plan
#                    change after a load (every client at tick 1), after an undock, and Helper's closeMenu reopens it
#                    whenever a menu closes in flight.
UI_ONLY_EVENTS = (('TopLevelMenu', ''),)

INVENTORY = {}
for part in (
    # P0, flying the own ship: what the replay families take, and what nothing takes yet.
    _ffi('flight', 'StartPlayerActivity StopPlayerActivity', TRAVEL),
    _ffi('flight', 'SetSofttarget HandleTargetMouseClickOnRadar HandleTargetMouseDownOnRadar', TARGET),
    _lua('flight', 'SetSofttarget RemoveSofttarget', TARGET),
    _ffi('flight', 'ToggleAutoPilot', AUTOPILOT),
    _lua('flight', 'StartAutoPilot StopAutoPilot', AUTOPILOT),
    _ffi('flight', 'QuickDock RequestDockAt RequestDockAtReason UndockPlayerShip', DOCK),
    _ffi('flight', 'SetDefensibleActiveWeaponGroup SetWeaponGroup SetWeaponMode', WEAPONS),
    # The own ship's toggles the flight family replays in slot k's scope: each acts on the singleton's ship.
    _ffi('flight', 'ToggleFlightAssist CycleTurretMode CycleTurretMissile ToggleDrones CycleNextDroneMode', FLIGHT),
    # Deployables from the ship interact menu and the map's deploy tables (ship, macro name): the flight family replays
    # them at their entries (lockstep_ui_replay.h). The deploy keys stay refused (pstk_refuse_actions).
    _ffi('flight', 'LaunchMine LaunchNavBeacon LaunchResourceProbe LaunchSatellite LaunchLaserTower', FLIGHT),
    # A named ship's weapon, turret and drone settings (docked menu, map), a deployable switched or destroyed (interact
    # menu), cargo dropped (map): the flight family replays them at their entries.
    _ffi('flight', '''SetWeaponArmed SetAmmoOfWeapon SetTurretGroupMode2 SetTurretGroupArmed SetAllTurretModes SetAllTurretsArmed
        SetAllMissileTurretModes SetAllMissileTurretsArmed SetAllNonMissileTurretModes SetAllNonMissileTurretsArmed SetDroneMode
        SetDroneTypeArmed SetAllDronesArmed ActivateObject SelfDestructComponent DropCargo''', FLIGHT),
    _ffi('flight', '''SetAllowedWeaponSystems
        DropInventory TeleportPlayerTo TransportPlayerToTarget
        PerformCompSlotPlayerAction StartControlPanelHack TriggerCompSlotAnimation SetDockingBayReservation
        RemoveDockingBayReservation SetObjectCoverFaction SetPlayerLastCoverFaction'''),
    # The docked menu's Get Up (menu_docked.lua buttonGetUp): X4 posts a GetUpEvent to the player. Refused like the rest;
    # native_gate_pilot_foot claims it for slot 1 (lockstep_pilot_foot_guard.h: the GET_UP key in the stock record).
    _ffi('flight', 'GetUp'),
    # Not here any more: RequestSofttargetLock and RemoveSofttargetLockRequest (targetsystem.lua: the target system keeps its
    # soft target while the player steers hard at a small ship). Both only write the UI controller's own lock manager
    # (*0x6cf2788 + 0x78: the requesters' names and the locked flag of its current soft target entry); every reader of that
    # object is UI code. The soft target itself changes through SetSofttarget, which the target family replays. Refused,
    # the lock never engaged and the HUD asked again every frame (a playtest of 9 Nov: 102 refusals and their info lines).
    _lua('flight', 'PerformAction RaisePlayerInteractionEvent'),
    # P1, commanding ships.
    _ffi('orders', '''CreateOrder CreateOrder3 EnableOrder AdjustOrder RemoveOrder RemoveOrder2 ResetOrder
        EnablePlannedDefaultOrder RemovePlannedDefaultOrder CreateDeployToStationOrder''', ORDERS | QUEUE),
    _ffi('orders', 'SetOrderLoop SetCurrentLoopOrder', QUEUE),
    _ffi('orders', 'RemoveAllOrders RemoveAllOrders2', ORDERS),
    _lua('orders', 'CreateOrder', ORDERS),
    _lua('orders', 'SetOrderParam RemoveOrderListParam', ORDERS | QUEUE),
    _ffi('orders', '''ResetOrderLoop RemoveOrderFailure RemoveDefaultOrderFailure SetOrderSyncPointID RemoveOrderSyncPointID
        ReleaseOrderSyncPoint ReleaseOrderSyncPointFromOrder SetSyncPointAutoRelease SetSyncPointAutoReleaseFromOrder'''),
    _lua('orders', 'CopyDefaultOrderParamsForPlanning'),
    _ffi('fleet', '''RemoveCommander2 SetSubordinateGroupAssignment SetSubordinateGroupAttackOnSight SetSubordinateGroupDockAtCommander
        SetSubordinateGroupProtectedLocation SetSubordinateGroupReinforceFleet SetSubordinateGroupRespondToDistressCalls
        SetSubordinateGroupResupplyAtFleet SetFormationShape SetFleetUnitCommander SetFleetUnitSubordinateGroupAssignment
        SetFleetUnitLoadout AddFleetUnitSubordinate RemoveFleetUnit ReassignControllableToFleetUnit ReleaseDetachedSubordinateGroup
        SetFleetManagement SetFleetName SetFleetLogo SetShipStance SetControllableFightRule SetControllableBlacklist
        SetDefaultResponseToSignalForControllable ResetResponseToSignalForControllable SetDefensibleLoadoutLevel SetEntityToPost'''),
    # P2, economy and assets.
    # The trade family replays these (reports/LOCKSTEP-UI-TRADE-20261101.txt): the Lua globals wrapped in the Lua core, the FFI
    # setters hooked natively at their entries (lockstep_ui_trade.h takes their prefixes and pins from this table).
    _lua('trade', '''AddTradeToShipQueue TransferPlayerMoneyTo TransferMoneyToPlayer SetContainerWarePriceOverride ClearContainerWarePriceOverride
        SetContainerStockLimitOverride ClearContainerStockLimitOverride SetMinBudget SetMaxBudget SetVirtualCargoMode''', TRADE),
    _ffi('trade', '''AddTradeWare RemoveTradeWare ClearContainerBuyLimitOverride ClearContainerSellLimitOverride SetContainerBuildMethod
        SetContainerBuildPriceFactor SetContainerBuyLimitOverride SetContainerGlobalPriceFactor SetContainerSellLimitOverride
        SetContainerTradeRule SetContainerWareIsBuyable SetContainerWareIsSellable SetContainerWorkforceFillCapacity SetSupplyManual
        SetShipTradeLoopCargoReservationOverride RemoveShipTradeLoopCargoReservationOverride UpdateProductionTradeOffers''', TRADE),
    # Not replayed (the report's section 'stays refused'): the player inventory (the entity's), trade rules
    # (a new rule's ID is read back at once), supply overrides (a struct array), modules, player-wide settings, build tasks,
    # the cheat money, and globals no base menu calls.
    _lua('trade', '''ClearTradeQueue TransferPlayerInventoryTo TransferInventoryToPlayer ToggleFactionTradeRestriction MakeRepairPriority'''),
    _ffi('trade', '''CreateTradeRule UpdateTradeRule RemoveTradeRule SetPlayerTradeRuleDefault UpdateSupplyOverrides PauseProcessingModule
        PauseProductionModule SetPlayerTradeLoopCargoReservationSetting SetBuildTaskTransferredMoney AddPlayerMoney'''),
    _ffi('faction', '''CreateBlacklist2 UpdateBlacklist2 RemoveBlacklist CreateFightRule UpdateFightRule RemoveFightRule
        SetPlayerBlacklistDefault SetPlayerFightRuleDefault SetPlayerIllegalWare SetPlayerShipsWaitForPlayer
        SetPlayerTaxiWaitsForPlayer SetPlayerGlobalLoadoutLevel SetFactionBuildMethod SetFactionDefaultWeaponMode
        SetDefaultResponseToSignalForFaction2 ResetDefaultResponseToSignalForFaction SetPlayerFactionName SetPlayerLogo
        SetPlayerPaintTheme SetPlayerClothingTheme'''),
    _ffi('assets', '''AssignHiredActor ClaimShip CreateNPCFromPerson PerformCrewExchange2 PutShipIntoStorage ReassignPeople
        ReleasePersonFromCrewTransfer RemovePerson RequestShipFromInternalStorage2 SellPlayerShip TakeShipFromInternalStorage
        InstallEngineMod InstallGroupedWeaponMod InstallPaintMod InstallShieldMod InstallShipMod InstallWeaponMod
        DismantleEngineMod DismantleGroupedWeaponMod DismantleShieldMod DismantleShipMod DismantleWeaponMod'''),
    _lua('assets', '''SetComponentName AddAmmo AddCargo AddInventory AddUnits RemoveAmmo RemoveCargo RemoveInventory AddLicence
        AddKnownItem SetNPCBlackboard SetNPCSkill'''),
    _ffi('build', '''AddBuildTask6 SetConstructionSequenceFromConstructionMap ReserveBuildPlot ExtendBuildPlot PayBuildPlotSize
        RemoveBuildPlot2 RemoveBuildStorage SetStationModuleLoadoutLevel UpdateProduction ClearProductionItems'''),
    # Station construction (reports/LOCKSTEP-UI-CONSTRUCT-20261101.txt): cancelling a construction (interact menu, user
    # question; the build family claims it) and cancelling a delivery to a build storage or station (station configuration,
    # helper: a trade deal ID whose numbering is not shown to be the same on every client; refused).
    _ffi('build', 'CancelConstruction CancelPlayerInvolvedTradeDeal'),
    _ffi('conflict', '''CreateBoardingOperation StartBoardingOperation AbortBoardingOperation AddAttackerToBoardingOperation
        RemoveAttackerFromBoardingOperation UpdateAttackerOfBoardingOperation UpdateBoardingOperation StartResearch LearnBlueprint
        StartTerraformingProject AbortActiveTerraformingProject AbortActiveDiplomacyActionOperation SetAgentDiplomacyShip
        SetDiplomacyEventOperationAgent SetDiplomacyEventOperationOption SetRelationBoostToFaction'''),
    _lua('conflict', 'StartDiplomacyActionOperation'),
    # P3, bridges into scripts.
    # StartBriefing / StopBriefing raise MD cue events at once (event_briefing_started 0x63e, submission 0x641/0x642, cancelled):
    # the gm_* briefing cues create a presentation cluster and objects (shared IDs) on the client that opened the briefing.
    _ffi('missions', '''AbortMission SetActiveMission SignalObjectWithNPCSeed SignalObjectWithNPCSeedAndMissionID MissionBriefingInteractButtonClicked
        StartBriefing StopBriefing'''),
    _lua('events', 'AddUITriggeredEvent SignalObject'),
    _lua('conversation', '''StartConversationFromMenu StartSubConversationFromMenu ProceedFromMenu ReturnFromMenu CancelConversation
        UnsuspendConversation SelectDialogOption SelectBackOption'''),
    # A client that loads or starts another game leaves the session's world.
    _ffi('session', 'NewGame NewMultiplayerGame ConnectToMultiplayerGame'),
    _lua('session', 'LoadGame NewGame'),
    _ffi('cheat', 'AddResearch ForceBuildCompletion SetFactionRelationToPlayerFaction SpawnObjectAtPos'),
    _lua('cheat', 'AddMoney RepairDestructibles ExecuteDebugCommand'),
    _ffi('editor', '''AddCluster AddSector AddGateConnection RemoveGateConnection RemoveComponent SpawnObjectAtPos2 SpawnStationAtPos
        SpawnRegionAtPos SpawnLocalHighwayAtPos SetComponentOwner SetObjectSectorPos SetKnownTo SetComponentDescription
        SetObjectForcedRadarVisible ReplaceHighwaySpline SetRegionDefinition SetClusterBackground SetPositionalOffset
        SetCheckpointSequence'''),
):
    for name, value in part.items():
        key = (value[0], name)
        assert key not in INVENTORY, key
        INVENTORY[key] = value
