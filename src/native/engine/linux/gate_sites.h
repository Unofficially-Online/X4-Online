/* The one Linux X4 build the Linux gate supports, and everything it touches.
 * X4 Foundations 9.00 (611726), Steam build 23660954, Linux ELF64 x86-64 ET_EXEC
 * (fixed image base 0x400000, GCC 14.2). Addresses are ELF virtual addresses of
 * that file only; the gate refuses any other file (size, SHA-256, build ID, the
 * site hashes below). Raw engine bytes are not stored, only their SHA-256.
 *
 * Windows counterparts (tools/dev/lockstep_linux_known_pairs.json) and evidence
 * (reports/LOCKSTEP-LINUX-GATE-CORE-20261029.txt). Every data address below is
 * referenced rip-relative from a pinned site, so the site pins also pin it; the
 * offline check tools/dev/check_lockstep_gate_linux_sites.py decodes each of those
 * references in the ELF file. Parsed by that check: keep one entry per line. */
#define LX_IMAGE_BASE 0x400000u
#define LX_FILE_SIZE 60459280ull
#define LX_FILE_SHA "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20"
#define LX_BUILD_ID "77cf7ac0ff32d88ffd3db7eb6ea2e025aecb711d"
#define LX_TEXT_VA 0x656000u          /* PT_LOAD R E holding .text */
#define LX_TLS_VADDR 0x3aac780u       /* PT_TLS: .tdata 8 bytes + .tbss; static TLS offsets in lockstep_tls.h */
#define LX_TLS_MEMSZ 0x2740u
#define LX_TLS_ALIGN 0x10u
/* Frame gate. Windows CALL_RVA 0x12ae9d2 / WORD_RVA 0x12ae9d0 (8-byte word): on
 * Linux the call's rel32 crosses an 8-byte boundary, so the patch is one 16-byte
 * compare-exchange over the aligned window holding it (like the Windows path hook). */
#define LX_CALL_VA 0x72fb16u          /* main loop: call X4::FrameTick(FrameState*, loading) */
#define LX_CALL_WINDOW 0x72fb10u      /* 16-byte aligned; rel32 at window+7..+10 */
#define LX_TICK_VA 0x2024fc0u         /* X4::FrameTick, Windows TICK_RVA 0xfa2d70 */
#define LX_IN_FLIGHT_VA 0x3d8d97bu    /* main-loop byte: 1 from FrameTick's return until semSimDone (Windows 0x31c20cc) */
#define LX_VBI_TABLE_VA 0x3dbc4d8u    /* XLib::VbiTable*, Windows TABLE_RVA 0x6cf1570 */
#define LX_VBI_LOADING 0x130u         /* int loading flag in the table, both builds */
#define LX_GAME_VA 0x3db82b0u         /* TimerBase game time (f64), Windows GAME_RVA 0x6b60e38 */
#define LX_REAL_VA 0x3db82b8u         /* TimerBase real time (f64), Windows REAL_RVA 0x6b60e40 */
#define LX_SPEED_VA 0x3db82d8u        /* TimerBase speed factor (f64), Windows SPEED_RVA 0x6b60e60 */
/* Fixed dt. Windows NOPs the movie-capture branch (0xfa3302 -> Linux 0x2025135) and
 * sets the capture frame time (0x6adfc60 -> Linux 0x4533d40). Linux uses the
 * natively validated x4mp-agent fixed60 patch of the non-capture path instead:
 * dt = 1/60 exactly; the capture flag must stay 0 on both. */
#define LX_MOVIE_FLAG_VA 0x4533d38u   /* Windows 0x6adfc58 */
#define LX_MOVIE_DT_VA 0x4533d40u     /* Windows 0x6adfc60 (not written on Linux) */
#define LX_DT_LOAD_VA 0x2025260u      /* movsd xmm1,[rip+d] (the 1.0 clamp at 0x2e244d0) */
#define LX_DT_MIN_VA 0x2025268u       /* minsd xmm1,xmm0 ; pxor xmm0,xmm0 */
#define LX_ONE_SIXTIETH_VA 0x2e24578u /* .rodata double 1/60 */
#define LX_DT_LOAD_ORIGINAL 0x00dff2680d100ff2ull /* f2 0f 10 0d 68 f2 df 00 */
#define LX_DT_MIN_ORIGINAL 0xc0ef0f66c85d0ff2ull  /* f2 0f 5d c8 66 0f ef c0 */
#define LX_DT_MIN_PATCHED 0xc0ef0f6600401f0full   /* 0f 1f 40 00 66 0f ef c0 */
/* World identities and world hash (gate_world.h; reports/LOCKSTEP-LINUX-WORLD-HASH-20261029.txt).
 * Windows hooks its import function 0x79f720 (saved UniverseID -> runtime ID into the import
 * map 0x31c2110 via the map insert 0x4795a0); Linux inlines that function into its callers,
 * each calling the map insert 0x10c8140 with the import map 0x3e1cac0, so the Linux gate
 * hooks the insert and keeps only that map. Both load functions clear the map through the
 * map clear (Windows 0x9eeddb/0x9f0804, Linux 0x1cdd737/0x1cddf54), which frees its nodes, so
 * the gate also hooks the clear and reads the nodes there, not at the first live frame.
 * Find, map and bank selectors as on Windows. */
#define LX_IMPORT_INSERT_VA 0x10c8140u  /* map<u64,u64>::operator[] (Windows 0x4795a0); node key +0x20, value +0x28 */
#define LX_IMPORT_MAP_VA 0x3e1cac0u     /* saved UniverseID -> runtime ID import map (Windows 0x31c2110) */
#define LX_IMPORT_CLEAR_VA 0x11e0b30u   /* map clear, frees the node blocks (Windows 0x942540) */
#define LX_FIND_VA 0xa68df0u            /* component find (map, runtime, 4) (Windows 0xced70) */
#define LX_COMPONENT_MAP_VA 0x3daa860u  /* component registry pointer (Windows 0x6cf1500) */
#define LX_BANK_READ_VA 0x3e4fea8u      /* movement bank read selector (Windows 0x38f4ee4) */
#define LX_BANK_WRITE_VA 0x3d8e254u     /* movement bank write selector (Windows 0x2f4b6f4) */
#define LX_IMPORT_ENTRY_ORIGINAL 0x8949544155415641ull /* push r14; push r13; push r12; mov r12,rsi (first 8 bytes) */
#define LX_IMPORT_PROLOGUE 6u           /* the three pushes, relocated into the trampoline */
#define LX_IMPORT_CLEAR_ORIGINAL 0x578b4840ec834853ull /* push rbx; sub rsp,0x40; mov rdx,[rdi+8] (first 8 bytes) */
#define LX_IMPORT_CLEAR_PROLOGUE 5u     /* push rbx; sub rsp,0x40, relocated into the trampoline */
#define LX_SHIELD_SOURCE_VTABLE 0x3b8d6d8u /* U::ShieldRechargeSource (Windows 0x2b19700), stored by shield_group_construct */
#define LX_SHIELD_HOLDER_VTABLE 0x3b8d668u /* the group's value holder at +0x58, stored by shield_group_construct: write +0x20, read +0x30 */
#define LX_SHIELD_MAX_VA 0x1293200u        /* the source's getters (slots +8, +0x10, +0x18), inlined into the read behind a slot test */
#define LX_SHIELD_DELAY_VA 0x1293230u
#define LX_SHIELD_RATE_VA 0x12932a0u
#define LX_SHIELD_WRITE_VA 0x1293310u      /* the holder's write: value = max, changed time 0 */
#define LX_SHIELD_PERCENT_VA 0x17d7b70u    /* GetShieldPercent (object, time) -> float (Windows 0x51f5b0): the read 0x1293330 */
/* Serial pathing and path determinism (gate_path.h, lockstep_path_determinism.h). */
#define LX_PATH_MANAGER_VA 0x3e0ced0u    /* PathingManager pointer (Windows 0x6cf21c8) */
#define LX_PATH_RELEASE_SITE 0x12125e3u  /* MoveWorker::Run: call of the release with manager+0x70 */
#define LX_PATH_RELEASE_ORIGINAL 0x008115f8e870c783ull /* the aligned word holding that call */
#define LX_SEM_RELEASE_VA 0x1a23be0u     /* semaphore release (semaphore, count) */
#define LX_SEM_TIMED_WAIT_VA 0x1a23d90u  /* semaphore wait (semaphore, ms): 1 taken, 0 timed out */
#define LX_PATH_REFRESH_VA 0x14498d0u    /* RefreshFromPathingState (Windows 0xea2a20) */
#define LX_PATH_SYNC_VA 0x1a3b900u       /* SyncThreadGameTime (Windows 0x14b8e20) */
#define LX_PATH_BUDGET 0x3a8u            /* manager: UpdateBatch budget in seconds (Windows +0x218) */
/* Hazard region updates (gate_region.h, lockstep_region_rng.h). */
#define LX_REGION_VTABLE 0x3d15408u         /* U::Region (Windows 0x2b2ad60) */
#define LX_REGION_SLOT_VA 0x3d16960u        /* its Move slot, +0x1558 (Windows 0x2b2c270, +0x1510) */
#define LX_REGION_MOVE_VA 0x12c7f80u        /* U::Region::Move (Windows 0x767410) */
#define LX_DAMAGE_FIELD_VTABLE 0x3b57b88u   /* U::Regions::DamageField (Windows 0x2c4b2f0) */
#define LX_DAMAGE_FIELD_UPDATE_VA 0x12d7a80u /* its per-frame update, slot +0x48 (Windows 0xee16a0, +0x40) */
/* Weapon shots on keyed streams (gate_weapon_fire.h, lockstep_weapon_fire.h). */
#define LX_WEAPON_VTABLE 0x3c6a440u          /* U::Weapon (Windows 0x2b68278) */
#define LX_TURRET_VTABLE 0x3d38dd8u          /* U::Turret (Windows 0x2b6e910) */
#define LX_WFIRE_SLOT 0x1fc0u                /* the shot's slot in both (Windows +0x1f30, slot 998) */
#define LX_WFIRE_SHOT_VA 0x1138c70u          /* the shot, U::Weapon::vfunc998 (Windows 0x8185b0) */
#define LX_WFIRE_LOOP_VA 0x1d698b0u          /* the fire loop, its only caller (Windows 0x817d20) */
/* Engine-parameter refreshes on keyed streams (gate_engine_rng.h, lockstep_engine_rng.h). */
#define LX_ERNG_LISTENER_VA 0x14382f0u       /* the UpdateEngineParametersEvent listener (Windows 0xea1d70) */
#define LX_ERNG_SITE_VA 0x1438348u           /* the inlined FloatRange: the aligned word starting movabs rax, the multiplier */
#define LX_ERNG_SITE_ORIGINAL 0xf42d4c957f2db848ull /* its 8 bytes */
#define LX_ERNG_ADDSD_VA 0x1438370u          /* addsd xmm1, fs:CurTime (replayed by the stub) */
#define LX_ERNG_ADDSD_LENGTH 10u
#define LX_ERNG_RESUME_VA 0x143838au         /* mov eax,eax: the draw goes on with the stepped word in rax */
/* Spline routes joined (gate_spline_join.h; lockstep_spline.h's spline_join_all). */
#define LX_SPLINE_VTABLE 0x3b5d6b8u          /* the spline model (Windows 0x2c4f480) */
#define LX_SPLINE_SLOT_VA 0x3b5d730u         /* its update slot +0x78 (Windows 0x2c4f4f0, +0x70) */
#define LX_SPLINE_UPDATE_VA 0x1363de0u       /* the update (Windows 0xeb7230) */
#define LX_SPLINE_DEQUE_CUR 0x478u           /* model: the request deque's start.cur (libstdc++ deque at +0x468) */
#define LX_SPLINE_DEQUE_FINISH 0x498u        /* model: its finish.cur */
#define LX_SPLINE_REQUEST_STATUS 0xb8u       /* request: 0 pending, 1 published (Windows +0xa8) */
#define LX_SPLINE_RESULT_MUTEX_VA 0x3e76960u /* XLib::Mutex the calculator publishes under (Windows 0x3d74938); LX_MUTEX_LOCK_VA/UNLOCK_VA */
#define LX_SPLINE_WORKER_VA 0x3e5b850u       /* the calculator worker* (Windows 0x6cf29c0), stored by 0x2001180 */
#define LX_SPLINE_WORKER_VTABLE 0x3b28b38u   /* its vtable (Windows 0x2c68f98); slot 2 is Run */
#define LX_SPLINE_WORKER_RUN_VA 0x10b0b30u   /* Run (Windows 0x1099650) */
#define LX_SPLINE_WORKER_RUNNING 0xb8u       /* worker: running byte, Run sets it to 1 (Windows +0x58) */
/* Mass traffic and region passes joined (gate_mass_traffic.h; lockstep_mass_traffic.h). Tasks: LX_FS_MASS_VA, LX_FS_REGION_VA. */
#define LX_SEM_WAIT_VA 0x1a23cc0u              /* XLib::Semaphore wait (semaphore) */
#define LX_CONDVAR_WAIT_VA 0x1a23f70u          /* XLib condition wait (condition, XLib::Mutex) */
#define LX_PTHREAD_COND_WAIT_PLT_VA 0x6589a0u  /* pthread_cond_wait's PLT entry */
#define LX_MT_WORKER_WAIT_CALL_VA 0x12125b5u   /* MoveWorker::Run frame start: E8 to the g_semSimStart wait (Windows 0xef45ea) */
#define LX_MT_WORKER_WAIT_BLOCK_VA 0x12125b0u  /* the aligned 16 bytes holding its rel32 */
#define LX_MT_TRAFFIC_WAIT_CALL_VA 0x11f9735u  /* traffic Run: E8 to the parked wait on +0xd8 */
#define LX_MT_TRAFFIC_WAIT_BLOCK_VA 0x11f9730u
#define LX_MT_REGION_WAIT_CALL_VA 0x12ef3bfu   /* region Run: E8 to pthread_cond_wait(+0x1a0, +0x140) */
#define LX_MT_REGION_WAIT_BLOCK_VA 0x12ef3c0u
#define LX_MT_TRAFFIC_LOCK 0x70u               /* traffic task: XLib::Mutex (Windows +0x60) */
#define LX_MT_TRAFFIC_PARKED 0xa0u             /* traffic task: parked byte, set when its pass is done (Windows ready +0x88) */
#define LX_MT_REGION_LOCK 0x138u               /* region task: XLib::Mutex (Windows +0x118) */
#define LX_MT_REGION_PARKED 0x168u             /* region task: parked byte (Windows ready +0x140) */
/* Per-ship RNG keys for every portable ship (gate_actor_all.h; lockstep_actor_all.h). */
#define LX_SHIP_MOVE_VA 0x12b81d0u            /* Ship Move (Windows 0x7cc810), slot +0x1558 of every ship vtable (Windows +0x1510) */
#define LX_SHIP_SMALL_MOVE_SLOT_VA 0x3d22ca0u /* U::ShipSmall 0x3d21748 (Windows SHIP_ENTRY 0x2b813c8) */
#define LX_SHIP_XL_MOVE_SLOT_VA 0x3d403e8u    /* U::ShipExtraLarge 0x3d3ee90 (Windows SHIP_XL_ENTRY 0x2b9af98) */
#define LX_AGENT_UPDATE_VA 0x1444c70u         /* the pathing Agent update (Windows 0xe8f250) */
#define LX_AGENT_CALL_VA 0x13e33a2u           /* its only call, in the batch job 0x13e32f0 (Windows 0xeaed6f in 0xeaecc0) */
#define LX_AGENT_CALL_BLOCK_VA 0x13e33a0u     /* the aligned 16 bytes holding its rel32 */
/* The player's fleet rebuild timer on game time (gate_player_timer.h; lockstep_player_timer.h). */
#define LX_PLAYER_MOVE_VA 0x138e6a0u          /* U::Player::vfunc674 (Windows 0x7113e0) */
#define LX_PLAYER_MOVE_SLOT_VA 0x3b60cf8u     /* its slot +0x1558 of U::Player 0x3b5f7a0 (Windows PT_ENTRY 0x2b33eb8) */
/* The pathing pass's tempzone requests (gate_tempzone_order.h; lockstep_tempzone_order.h). */
#define LX_TZ_FIND_VA 0x12981b0u              /* zone at a position, create=1 makes one (Windows 0x785db0; lookup 0x785b90 inlined) */
#define LX_TZ_ACTIVE_VA 0x1cb3f40u            /* zone active in the thread's view (Windows 0x69ed50) */
#define LX_TZ_RESET_VA 0x1dc8de0u             /* release the reference held in *ref, *ref = 0 (Windows 0x96940) */
#define LX_TZ_FLAG_VA 0x3eb9730u              /* one new tempzone pending (Windows 0x3d72b88) */
#define LX_TZ_ZONE_VA 0x3eb96f0u              /* that zone (Windows 0x3d72c40) */
#define LX_TZ_LOCK_VA 0x3eb9700u              /* their XLib::Mutex, LX_MUTEX_LOCK_VA/UNLOCK_VA (Windows 0x3d72c48) */
#define LX_TZ_VIEW_VA 0x3e7742cu              /* g_ComponentViewCacheIndex, the published view the jobs read (Windows 0x38f4eb4) */
#define LX_TZ_PATH_CREATE_RET_VA 0x14490c3u   /* return addresses of the request calls: Agent_UpdatePathing create=1 (Windows 0xe910a6) */
#define LX_TZ_PATH_FIND_CALL_VA 0x1449609u    /* create=0 (Windows 0xe9123c) */
#define LX_TZ_SWITCH_CREATE_RET_VA 0x142fa33u /* the zone switch 0x142efa0: create=1 (Windows 0xe890a6) */
#define LX_TZ_SWITCH_FIND_CALL_VA 0x142fb34u  /* create=0 (Windows 0xe89131) */
#define LX_TZ_OTHER_CALL_VA 0x12ec389u        /* 0x12eb620, from the region manager's Run 0x12ef310 (Windows 0xee7c7a) */
/* Warps on a keyed word, and the warp watch (gate_warp_rng.h; lockstep_warp_rng.h). */
#define LX_WR_DISPATCH_VA 0x16b3e30u          /* U::WarpEvent::Dispatch (Windows 0x9aa290) */
#define LX_WR_ENTRY_VA 0x3b83458u             /* its slot 3 of U::WarpEvent 0x3b83440 (Windows WR_ENTRY 0x2baa8d0, slot 2) */
#define LX_WR_CTOR_VA 0x16bbe40u              /* U::WarpEvent's constructor (Windows 0x9a9d00) */
#define LX_WR_GTRT_VA 0x18a0090u              /* U::Component::GetTransformRelativeTo (Windows 0x3db8a0) */
#define LX_WR_FRAME_CALL_VA 0x1615867u        /* the gate poster 0x1614ff0's call of it (Windows 0x5ce690's at 0x5cf1c6) */
/* The gate exit sum in the Windows order (gate_warp_exit.h). */
#define LX_WX_ENTRY_VA 0x1615324u            /* the poster's movss xmm0,[rdx+0x268]: the first of the exit sum (Windows 0x5ceb27..0x5ceb9d) */
#define LX_WX_RETURN_VA 0x1615353u           /* the instruction after the sum (movss xmm1,[rbp-0x1c8]) */
#define LX_FL_ENTRY_VA 0x148d812u            /* the flight limits' block: movss xmm3,[rip+...] (Windows 0x1406dcceb..0x1406dcdcf) */
#define LX_FL_RETURN_VA 0x148d05eu           /* the block's final jmp target */
#define LX_FT_ENTRY_VA 0x1481365u            /* the thrust words' block: movss [rbp+0x34],xmm11 (Windows 0x1406db3d2..0x1406db469) */
#define LX_FT_RETURN_VA 0x14813ddu           /* the instruction after the four words */
/* The region object search's camera test from the player's ship (gate_mining_camera.h; lockstep_region_camera.h mining). */
#define LX_MC_FUNC_VA 0x1f47b20u             /* the search (Windows 0x7884a0): rdi sector, rsi position, rdx frame, the object in rax */
#define LX_MC_SITE_VA 0x1f47f40u             /* its camera block, entered only from 0x1f47b9e (the player's cluster is the region's) */
#define LX_MC_NEAR_VA 0x1f47fc5u             /* near: release r15+0x38, return nothing */
#define LX_MC_FAR_VA 0x1f48130u              /* far: release r15+0x38, back to the search at 0x1f47bc0 */
#define LX_UNIVERSE_VA 0x3db6948u            /* g_Universe (Windows 0x3d198f8); its player object +0x238 (Windows +0x230) */
/* Build storage's randomized UpdateBuildEvent schedule (gate_build_rng.h). */
#define LX_BUILD_SCHEDULER_VA 0x1838e80u
#define LX_BUILD_CALL_VA 0x1838f16u
#define LX_BUILD_CALL_BLOCK_VA 0x1838f10u
#define LX_BUILD_FLOAT_RANGE_VA 0x1a491c0u
/* AI Wait restores (gate_ai_wait.h, lockstep_ai_wait_rng.h). */
#define LX_AI_WAIT_SLOT_VA 0x3b2a680u       /* U::AI::WaitAction vtable 0x3b2a650, restore slot +0x30 (Windows +0x28) */
#define LX_AI_WAIT_RESTORE_VA 0x10c12b0u    /* WaitAction::Reattach (Windows 0x10a50d0) */
#define LX_AI_WAIT_RETURN_VA 0x10df36bu     /* the AI importer's return from it (Windows 0x1069bfc) */
#define LX_AI_CONTEXT_VTABLE 0x3b28df0u     /* U::AI::AIScriptContext (Windows 0x2c6a270) */
/* MD cue delays restored during the load (gate_md_resume.h, lockstep_md_resume_rng.h). */
#define LX_MD_RESUME_CALLER_VA 0xf77160u    /* Cue_ResumeAfterLoad (Windows 0x1115960) */
#define LX_MD_RESUME_SITE_VA 0xf77325u      /* its E8 to the delay evaluator (Windows 0x1115bad) */
#define LX_MD_RESUME_DELAY_VA 0xf5b380u     /* cue delay evaluator (Windows 0x111f8c0) */
#define LX_CUE_VTABLE 0x3b0ee70u            /* MD::Cue (Windows 0x2c75170) */
/* The boot hold (gate_boot_hold.h, lockstep_boot_hold.h). */
#define LX_BOOT_HOLD_CALL_VA 0xfeb2bcu      /* UniverseFrameUpdate -> AIDirector::Move, its only caller (Windows 0x9fe093) */
#define LX_BOOT_HOLD_MOVE_VA 0x10e27a0u     /* AIDirector::Move (Windows 0x106d250) */
#define LX_BOOT_HOLD_DIRECTOR_VA 0x3dea760u /* the director (Windows 0x6cf15f0) */
#define LX_BOOT_HOLD_ENTRY_ORIGINAL 0x56415741e5894855ull /* push rbp; mov rbp,rsp; push r15; push r14 (first 8 bytes) */
#define LX_BOOT_HOLD_PROLOGUE 6u            /* push rbp; mov rbp,rsp; push r15, relocated into the trampoline */
/* Portable event delivery order (gate_event_order.h, lockstep_event_order.h). */
#define LX_EVENT_DELIVER_VA 0x16c5db0u      /* DeliverEvent (Windows 0x99c470) */
#define LX_EVENT_BATCH_VA 0x3eedfa0u        /* the due batch, 0x38-byte slots (Windows 0x39943e0) */
#define LX_EVENT_COUNT_VA 0x3eedf90u        /* its count (Windows 0x38f4bc8) */
#define LX_EVENT_CALL_WORD_VA 0x16c6b60u    /* the aligned qword ending in the delivery call's rel32 */
#define LX_EVENT_CALL_VA 0x16c6b63u         /* ProcessDueEvents' E8 to DeliverEvent (Windows 0x99c3d1) */
#define LX_EVENT_CALL_WORD_ORIGINAL 0xfffff248e828758bull /* 8b 75 28 | e8 rel32(DeliverEvent) */
#define LX_UI_ELEMENT_VTABLE 0x3d3b040u     /* U::UIElement (Windows 0x2b6cc38) */
/* The Gravidar (gate_gravidar.h, lockstep_ai_schedule.h). */
#define LX_GRAVIDAR_WORKER_VA 0x3dfa4a0u    /* AI::Gravidar (Windows 0x6cf1650) */
#define LX_GRAVIDAR_VTABLE 0x3b2cf08u       /* its vtable (Windows 0x2c68860) */
#define LX_GRAVIDAR_PUBLISH_VA 0x1109380u   /* publish (Windows 0x10b12c0) */
#define LX_GRAVIDAR_CALL_VA 0x12125a9u      /* the MoveWorker's E8 to it (Windows 0xef45d9) */
#define LX_GRAVIDAR_CALL_WORD_VA 0x12125a8u /* the aligned qword holding its rel32 at +2 */
#define LX_GRAVIDAR_UNIT_APPLY_VA 0x10fd7b0u /* ActiveGravidarUnit apply (Windows 0x10acd70) */
#define LX_GRAVIDAR_UNIT_VTABLE 0x3b2e138u  /* ActiveGravidarUnit (Windows 0x2c67610) */
#define LX_GRAVIDAR_UNIT_SLOT_VA 0x3b2e178u /* its slot +0x40 (Windows 0x2c67648, +0x38) */
#define LX_GRAVIDAR_RUN_VA 0x1106ee0u       /* Run (Windows 0x10ae0e0) */
#define LX_MUTEX_LOCK_VA 0x1a23650u         /* X4's mutex: lock */
#define LX_MUTEX_UNLOCK_VA 0x1a236a0u       /* unlock */
#define LX_GAME_TIME_LOCK_VA 0x3db8280u     /* the global game time's lock (Windows 0x6b60e10) */
#define LX_GAME_TIME_GLOBAL_VA 0x3db82b0u   /* the global game time (Windows 0x6b60e38) */
#define LX_MACRO_VTABLE 0x3ae6860u          /* U::Macro (Windows 0x2ba5430) */
#define LX_MACRO_NAME_GETTER_VA 0xa38880u   /* its slot 19, returns +0x20 (Windows slot 18 0xefb10) */
#define LX_TEXT_END_VA 0x2c730a9u           /* the end of PT_LOAD R E */
/* Gravidar factor-pass deadlines (gate_gravidar_deadlines.h). */
#define LX_SECTOR_VTABLE 0x3d1d850u         /* U::Sector (Windows 0x2b1ea48) */
#define LX_SECTOR_MOVE_SLOT_VA 0x3d1eda8u   /* its slot +0x1558 (Windows 0x2b1ff58, +0x1510) */
#define LX_SECTOR_MOVE_VA 0x127cf40u        /* U::Sector::Move (Windows 0x784230) */
/* The wingman's travel decision from a snapshot (gate_travel_snapshot.h). */
#define LX_TRAVEL_READ_WORD_VA 0x1446b38u   /* the qword holding the leader read's disp32 low byte, byte 5 */
#define LX_TRAVEL_READ_VA 0x1446b3bu        /* cmp byte [rdx+0xf9b],0 (Windows 0xe918d5) */
#define LX_PATH_JOB_VA 0x13c45c0u           /* the batch job thunk (Windows 0xf29cd0) */
#define LX_PATH_JOB_LEA_A_VA 0x13e3db0u     /* 16-byte window holding the first LEA's disp32 */
#define LX_PATH_JOB_LEA_A_DISP 0xcu
#define LX_PATH_JOB_LEA_B_VA 0x13e3e30u     /* likewise the second */
#define LX_PATH_JOB_LEA_B_DISP 0x0u
/* The same runtime IDs on every client (gate_runtime_ids.h, lockstep_runtime_ids.h). */
#define LX_ID_MAP_VA 0x3daa860u             /* AutoIDMap pointer (Windows 0x6cf1500) */
#define LX_ID_REMOVE_VA 0xee3850u           /* Remove (Windows 0xcbe70) */
#define LX_ID_REMOVE_ENTRY_ORIGINAL 0x5441554156415741ull /* push r15; push r14; push r13; push r12 (first 8 bytes) */
#define LX_ID_REMOVE_PROLOGUE 6u            /* push r15; push r14; push r13, relocated into the trampoline */
#define LX_ID_ASSIGN_VA 0xeed870u           /* AssignNewID (Windows 0xcc220), EnsureFreeSlots inlined */
#define LX_ID_ASSIGN_ENTRY_ORIGINAL 0x4855544155415641ull /* push r14; push r13; push r12; push rbp; the first byte of mov rbp,rdi */
#define LX_ID_ASSIGN_PROLOGUE 6u            /* push r14; push r13; push r12, relocated into the trampoline */
#define LX_ID_COMPONENT_RETURN_VA 0x189e28cu /* the return from AssignNewID in the constructor every U::Positional goes through */
#define LX_RID_ALLOCATE_VA 0x1e23dc0u        /* (size, alignment): what EnsureFreeSlots in AssignNewID allocates a block with (Windows SMem::Allocate 0x14b7140) */
/* The script RNG (gate_script_rng.h, lockstep_script_rng.h). */
#define LX_EXEC_RUN_VA 0xd5fbb0u            /* ExecState_Run (Windows 0xb58bb0) */
#define LX_EXEC_RUN_ENTRY_ORIGINAL 0xec8348fb89485355ull /* push rbp; push rbx; mov rbx,rdi; the start of sub rsp,8 */
#define LX_EXEC_RUN_PROLOGUE 5u             /* push rbp; push rbx; mov rbx,rdi, relocated into the trampoline */
#define LX_EXEC_RANGE_VA 0xd5fc30u          /* the run range (Windows 0xb58ce0): rdi Exec, rsi program, rdx begin, rcx end, r8d mode */
#define LX_EXEC_RANGE_ENTRY_ORIGINAL 0x5441554156415741ull /* push r15; push r14; push r13; push r12 */
#define LX_EXEC_RANGE_PROLOGUE 6u           /* push r15; push r14; push r13, relocated into the trampoline */
#define LX_AI_EVENT_SLOT_VA 0x3b29fa0u      /* U::AI::AIScriptContext vtable, slot 566 (Windows slot 565, 0x2c6b418) */
#define LX_AI_EVENT_VA 0x10d4270u           /* its event handler (Windows 0x1066750), the slot its only holder */
#define LX_STACK_VTABLE 0x3aedd58u          /* AI::AICallStackContext (Windows 0x2c690b8) */
#define LX_STACK_EVENT_SLOT_VA 0x3aeef08u   /* its slot 566 (Windows slot 565, 0x2c6a260) */
#define LX_STACK_EVENT_VA 0x10d4060u        /* its handler (Windows 0x1062f70), the slot its only holder */
#define LX_CUE_EVENT_SLOT_VA 0x3b10020u     /* MD::Cue slot 566 (Windows slot 565, 0x2c76318) */
#define LX_CUE_EVENT_VA 0xf799c0u           /* its handler (Windows 0x111f990), a tail jump into the check */
#define LX_CUE_CHECK_VA 0xf76110u           /* Cue_CheckConditions (Windows 0x111c290) */
#define LX_CUE_TIMED_SITE_VA 0xf79d18u      /* MDScheduler_Update's call (Windows 0x112208b) */
#define LX_CUE_WAIT_SITE_VA 0xf7650du       /* Cue_EnterWaiting's call (Windows 0x111d296); its rel32 crosses a 16-byte boundary */
#define LX_CUE_RAISE_SITE_VA 0xf78d33u      /* Cue_RaiseCueEvent's call (Windows 0x111e505) */
/* find_* candidate lists (gate_find_order.h, lockstep_find_order.h). */
#define LX_FIND_ORDER_VTABLE 0x3b00a68u      /* Scripts::FindComponentAction (Windows 0x2c0f3c8) */
#define LX_FIND_ORDER_EXECUTE_VA 0xd87ad0u   /* its Execute (Windows 0xb87a30), slot 4 */
#define LX_FIND_ORDER_SITE_VA 0xd87b33u      /* its call of the result store (Windows 0xb87ae3) */
#define LX_FIND_ORDER_ASSIGN_VA 0x1d2b330u   /* the result store (Windows 0xd2ab50) */
#define LX_FIND_SPACE_VTABLE 0x3b00918u      /* Scripts::FindSpaceInRangeAction (Windows 0x2c0f660) */
#define LX_FIND_SPACE_EXECUTE_VA 0xda9920u   /* its Execute (Windows 0xb8a890), slot 4, 0x23c0 bytes: the store inlined */
#define LX_FIND_SPACE_SITE_A_VA 0xdaa0e5u    /* the inlined store's two entries (Windows' call 0xb8af09 of 0xc602e0): */
#define LX_FIND_SPACE_SITE_B_VA 0xdaa348u    /* pxor xmm0,xmm0; movaps [rsp+0x160],xmm0; the range tree at rsp+0x120 */
/* Ship loadouts (gate_spawn_rng.h, lockstep_spawn_rng.h). */
#define LX_GENERATE_VA 0xd2a470u             /* U::ShipGenerator::Generate (Windows 0xc841b0) */
#define LX_GENERATE_ENTRY_ORIGINAL 0x56415741e5894855ull /* push rbp; mov rbp,rsp; push r15; the start of push r14 */
#define LX_GENERATE_PROLOGUE 6u              /* push rbp; mov rbp,rsp; push r15, relocated into the trampoline */
/* The navigation upkeep's deadlines (gate_nav.h, lockstep_nav_cadence.h). */
#define LX_NAV_MOVE_VA 0x144a9e0u            /* NavigationController::vfunc33 (Windows 0xea77f0) */
#define LX_NAV_MOVE_SLOT_A_VA 0x3b69928u     /* slot 40 of its vtable 0x3b697e8 (Windows 0x2c51260, slot 33) */
#define LX_NAV_MOVE_SLOT_B_VA 0x3b94cf0u     /* slot 40 of its second vtable 0x3b94bb0 */
#define LX_NAV_PARAMS_NEAR_VA 0x144aa6cu     /* ja, parameters within 1 km */
#define LX_NAV_PARAMS_FAR_VA 0x144aa79u      /* jbe, parameters beyond 5 km */
#define LX_NAV_PROBE_NEAR_VA 0x144ad1du      /* ja, probes within 1 km */
#define LX_NAV_PROBE_FAR_VA 0x144ad2au       /* jbe, probes beyond 5 km */
/* Objects' attention timer (gate_attention.h, lockstep_attention.h). */
#define LX_ATTENTION_VA 0x1465030u           /* PeriodicAttentionRefresh (Windows 0x6d1ff0) */
#define LX_ATTENTION_ENTRY_ORIGINAL 0xec8348fb89485355ull /* push rbp; push rbx; mov rbx,rdi; the start of sub rsp,0x18 */
#define LX_ATTENTION_PROLOGUE 5u             /* push rbp; push rbx; mov rbx,rdi, relocated into the trampoline */
#define LX_ATTENTION_DUE 0x2d8u              /* the object's next due (Windows +0x2e0) */
/* The Linear step trace (gate_step_trace.h, lockstep_step_trace.h; diagnostic). */
#define LX_ST_SLOT_VA 0x3b62558u             /* LinearFlightControlModel's vtable entry of its step (Windows 0x2c4f680, slot 14) */
#define LX_ST_STEP_VA 0x13ed050u             /* the step (Windows 0xeb27c0) */
/* FR8, the step's attitude block (gate_step_trace.h): three calls, each rel32 inside one 16-byte line. */
#define LX_SC_ASIN_TARGET_SITE_VA 0x13edc39u /* asinf of the target pitch (Windows 0xeb3892) */
#define LX_SC_ASIN_CUR_SITE_VA 0x13edcc5u    /* asinf of the current pitch (Windows 0xeb37ff) */
#define LX_SC_BUILDER_SITE_VA 0x13edecbu     /* the rotation builder (Windows 0xeb3912 -> 0x34bff0) */
#define LX_SC_ASINF_VA 0x6579c0u             /* asinf@plt */
#define LX_SC_BUILDER_VA 0x1e02200u          /* rdi rows (bank+0x10), xmm0 pitch, xmm1 yaw, xmm2 roll */
/* FR17: Formation::GetTargetPositionForWingMan (Windows 0xe99d70) at its two calls, each rel32 inside one 16-byte line. */
#define LX_SC_FM_VA 0x1e592d0u               /* rdi formation (+0x48 members' agents, +0x60 shape), rsi out, rdx index, xmm0 time */
#define LX_SC_FM_AGENT_SITE_VA 0x142862bu    /* in the agent's wrapper 0x14285e0 (formation agent+0xf40, index +0xf48; Windows 0xe8ba17) */
#define LX_SC_FM_OTHER_SITE_VA 0x1429795u    /* the other caller (Windows 0xe9a977) */
/* FR18: UpdatePathing 0x1431270 (Windows 0xe8c380) converting the agent's first path point ([rbx+0x6d0], rbx the agent)
 * for SetTarget 0x1431b5f; each rel32 inside one 16-byte line. */
#define LX_SC_PT_LOCAL_VA 0x14bd2d0u         /* rdi out, rsi point: its local transform (Windows 0xa32280) */
#define LX_SC_PT_LOCAL_SITE_VA 0x1431acau    /* ref id == frame id (or its parent's): out to SetTarget as it is */
#define LX_SC_PT_CONVERT_VA 0x141fa50u       /* rdi out, rsi point, rdx frame (Windows 0xe8bd20) */
#define LX_SC_PT_CONVERT_A_SITE_VA 0x14326d4u /* rdx r14, then to 0x1431acf */
#define LX_SC_PT_CONVERT_B_SITE_VA 0x14326ebu /* rdx r14, then the |pos| check to 0x1431b47 */
#define LX_SC_PT_MATH_VA 0x141f7e0u          /* rdi out, rsi velocity, rdx &ref, rcx &frame, r8d flag (Windows 0xe8a4a0) */
#define LX_SC_PT_MATH_SITE_VA 0x141fab0u     /* in 0x141fa50 (Windows 0xe8bdeb) */
/* FR20: the two other conversions in front of SetTarget 0x1431b5f (via 0x1431b47), rbx the agent at both. */
#define LX_SC_PT_CONVERT_C_SITE_VA 0x143235bu /* 0x141fa50(rdi rbp, rsi [rbx+0x6d0], rdx r14) (Windows 0xe8d502/0xe8d60f/0xe8d6bc) */
#define LX_SC_HIER_VA 0x18a2900u             /* rdi from, rsi inout mat4, rdx to, ecx 0 (Windows 0x3dde10) */
#define LX_SC_PT_OBJECT_SITE_VA 0x1432611u   /* 0x18a2900(rdi r15 the object, rsi &mat, rdx r14, ecx 0) (Windows 0xe8d5ea) */
/* FR19: the Linear model's SetTarget slot (vtable entry +0x20, Windows 0x2c4f628 -> 0xeaf620; no direct calls). */
#define LX_SG_SLOT_VA 0x3b62500u
#define LX_SG_FN_VA 0x141d140u               /* rdi model, rsi mat4 -> model+0x10, rdx -> +0x50, rcx != 0 -> +0x5c */
/* FR21: the Highway's point map (U::Highway vtable 0x3cd7150 +0x1568, Windows 0x2b091e0 +0x1520 -> 0x5f6480; the only
 * slot holding it), its spline at +0x5e8, its banking 0x15cf540(rdi the highway, xmm0 t; xmm0 out) at two sites. */
#define LX_HW_SLOT_VA 0x3cd86b8u
#define LX_HW_FN_VA 0x15cf8d0u
#define LX_SC_HW_BANK_VA 0x15cf540u
#define LX_SC_HW_BANK_SITE_VA 0x15cfa48u     /* the point map's (Windows 0x5f6643) */
#define LX_SC_HW_BANK2_SITE_VA 0x15d000au    /* the same function's other path */
/* FR24: the AddTargetPointToPathOrder queue 0x13e1040 (rdi manager, rsi the ship's runtime id, rdx the point, rcx the
 * issuer; Windows 0xeadf30) at its four calls, each rel32 inside one 16-byte line; the formation propagation's
 * (0x1447823, inlined in 0x1444c70: rbp its frame, O at rbp-0x140, r13 the leader's pending point; Windows 0xe99950). */
#define LX_SC_OC_VA 0x13e1040u
#define LX_SC_OC1_SITE_VA 0x1429c79u
#define LX_SC_OC_FM_SITE_VA 0x1447823u
#define LX_SC_OC3_SITE_VA 0x1c8d0c6u
#define LX_SC_OC4_SITE_VA 0x1cf0b86u
/* FR25 (lockstep_step_trace.h ST_SP_TAG): the create_position result stored by CreatePositionAction::Execute 0xde10c0
 * through 0xc93c90(rdi variable, rsi the context Value, rdx name, rcx the Value) at 0xde1116, the get_safe_pos result by
 * 0xde5640 likewise at 0xde56b7 (Windows 0xbe0c86 / 0xbe23aa); the object's transform 0x18a0090(rdi object, rsi matrix,
 * rdx ancestor, ecx flag, xmm0 time) inside the CreatePosition evaluation 0xd04a50 (rbp the context Value) at 0xd04c09;
 * the GetSafePos evaluation 0xd05ae0 (r12 the context Value) into Zone::GetSafePos 0x1155360 at 0xd05fe0 and the
 * sector's 0x1d36d90 at 0xd061c3; the solver 0x1a4b9f0 (rdi boxes, rsi point, rdx direction, ecx/r8d flags, r9 move
 * direction, xmm0 radius, xmm1, xmm2; Windows 0x14e4320) whose entry movabs rax (10 bytes, no rip operand) moves into a
 * trampoline. */
#define LX_SC_SETVAR_VA 0xc93c90u
#define LX_SC_SP_CV_SITE_VA 0xde1116u
#define LX_SC_SP_GV_SITE_VA 0xde56b7u
#define LX_SC_SP_TR_VA 0x18a0090u
#define LX_SC_SP_TR_SITE_VA 0xd04c09u
#define LX_SC_SP_ZONE_VA 0x1155360u
#define LX_SC_SP_GZ_SITE_VA 0xd05fe0u
#define LX_SC_SP_SECTOR_VA 0x1d36d90u
#define LX_SC_SP_GS_SITE_VA 0xd061c3u
#define LX_SC_SP_SOLVER_VA 0x1a4b9f0u
#define LX_SC_SP_SOLVER_PROLOGUE 10u
/* The flight parameter refresh (gate_fp_redirect.h, lockstep_fp_redirect.h; Option B slice 1). */
#define LX_FR_REFRESH_VA 0x14378c0u          /* the refresh (Windows 0xea1e80) */
#define LX_FR_ENTRY_ORIGINAL 0x5441554156415741ull /* push r15; push r14; push r13; push r12 */
#define LX_FR_PROLOGUE 6u                    /* push r15; push r14; push r13, relocated into the trampoline */
#define LX_FR_G4_SITE_VA 0x1437c0bu          /* its call of G4 (Windows 0xea22b3) */
#define LX_FR_G4_VA 0x15d1770u               /* G4, the engine getter for +0x4 (Windows 0x5f91b0) */
#define LX_FR_G0_SITE_VA 0x1437c1fu          /* its call of G0 (Windows 0xea22ca) */
#define LX_FR_G0_VA 0x15d18a0u               /* G0, for +0x0 (Windows 0x5f9340) */
#define LX_FR_M4_SITE_VA 0x14381e2u          /* its call of M for +0x4 (Windows 0xea28fd) */
#define LX_FR_M0_SITE_VA 0x1438213u          /* its call of M for +0x0 (Windows 0xea2932) */
#define LX_FR_M_VA 0x1429e80u                /* M (Windows 0xea3dc0) */
#define LX_FR_LERP_VA 0x1a48040u             /* L, called at 0x1437c79 (inlined on Windows) */
#define LX_FR_P_SITE_VA 0x1437911u           /* its first call, on every path (Windows 0xea1f2a) */
#define LX_FR_P_VA 0x148cb50u                /* that callee, writes +0x0..+0x2c, +0x100..+0x12c (Windows 0x6dc610) */
#define LX_FR_V_AT_VA 0x148d803u             /* in it: movsd xmm0,fs:-0x2728, the last instruction before the v call */
#define LX_FR_V_BACK_VA 0x148d812u           /* after the call 0x148d80d -> 0x18a15f0 (Windows 0x6dcce6 -> 0x3dc040) */
#define LX_FR_V_VA 0x18a15f0u                /* fills v at rsi = rsp+0x120 */
#define LX_FR_MULPS_AT_VA 0x148d940u         /* in it: the branch through 0x14811c0 (Windows 0x6dcbda -> 0x6db200) */
#define LX_FR_MULPS_BACK_VA 0x148d953u       /* after its call 0x148d94e -> the writer */
#define LX_FR_WRITER_VA 0x14811c0u           /* the writer (o20, D, A, slot) of +0x0..+0x44 (Windows 0x6db200) */
#define LX_FR_WRITER_AT_VA 0x148cd53u        /* the watched ships' path: mov rsi,[rsp+0x28] before its call (Windows 0x6dcaaf) */
#define LX_FR_WRITER_BACK_VA 0x148cd63u      /* after that call 0x148cd5e */
#define LX_FR_K_TABLE_VA 0x3db83c8u          /* the table pointer whose +0x318 is K (Windows [0x146cf1538]+0x300) */
/* Stations' loadout targets and macro order (gate_loadout_order.h, lockstep_loadout_order.h). */
#define LX_LL_WANTED_VA 0x1839ea0u           /* U::Container::GetWantedSupplyOrders (Windows 0x429ad0) */
#define LX_LL_WARES_SITE_VA 0x183a470u       /* its call of the ware sort (Windows 0x429c64) */
#define LX_LL_WARES_VA 0xb03680u             /* sorts and uniques the ware list by address (Windows 0x316f60) */
#define LX_LL_AMMO_SITE_VA 0x183ac7fu        /* its call of the ammunition target (Windows 0x42a40e) */
#define LX_LL_AMMO_VA 0x17e8bd0u             /* (Windows 0x505730) */
#define LX_LL_DEPLOYABLE_SITE_VA 0x183a537u  /* its call of the deployable target (Windows 0x42a6ee) */
#define LX_LL_DEPLOYABLE_VA 0x17e89d0u       /* (Windows 0x5046b0) */
#define LX_LL_UNIT_SITE_VA 0x183a6d7u        /* its call of the unit target (Windows 0x42a9a8) */
#define LX_LL_UNIT_VA 0x17e86d0u             /* (Windows 0x503c40) */
#define LX_LL_EXISTING_SITE_VA 0x17e9063u    /* the ammunition target's call of the existing-users walk (Windows 0x505ef8) */
#define LX_LL_EXISTING_VA 0x1def210u         /* (Windows 0x504970) */
#define LX_LL_PICK_SITE_VA 0x1defce1u        /* that walk's call of the candidate pick (Windows 0x504e20) */
#define LX_LL_PICK_VA 0x17b5f20u             /* (Windows 0x5012f0) */
#define LX_LL_REST_UNIT_VA 0x1e9e7b0u        /* the unit rest in map order, in 0x1e9e3d0 (Windows 0x503349 in 0x502ee0) */
#define LX_LL_REST_UNIT_EXIT_VA 0x1e9e6b2u   /* where that loop leaves: frees the list [rsp+0x60] if any, returns */
#define LX_LL_REST_DEPLOYABLE_VA 0x1df8193u  /* the deployable rest in map order, in 0x1df78e0 (Windows 0x5043ce in 0x504080) */
#define LX_LL_REST_DEPLOYABLE_EXIT_VA 0x1df8230u /* where that loop leaves: frees the list r14 if any, returns */
/* Non-blocking save by fork (gate_fork_save.h, native_gate_fork_save). The serializer and the globals it reads, the
 * locks and wait objects of the quiet point, and the bytes the child changes; every address is tied to a pinned site
 * by check_lockstep_gate_linux_sites.py. */
#define LX_FS_SAVE_VA 0x1cd3d30u             /* GameClass::Save(bool online) -> bool, its only caller 0xfecd77 (Windows 0x9f0dd0) */
#define LX_FS_POSSIBLE_VA 0x1de5ef0u         /* U::IsSavingPossible(bool online) -> 1 when possible */
#define LX_FS_PENDING_ONLINE_VA 0x3db68fcu   /* byte: the deferred-save step passes it to the serializer */
#define LX_FS_COUNTDOWN_VA 0x3db68f8u        /* int: frames until the engine's own deferred save; 0 = none pending */
#define LX_FS_FILE_NAME_VA 0x3db6900u        /* std::string: file name without folder and extension */
#define LX_FS_SAVE_NAME_VA 0x3db6920u        /* std::string: display name in the save header */
#define LX_FS_SAVE_DIR_VA 0x3e035c0u         /* std::string "save\\", relative to the personal folder */
#define LX_FS_COMPRESS_VA 0x3d8e080u         /* byte: compresssaves (.xml.gz, else .xml) */
#define LX_FS_FORMATTER_JOB_VA 0x3d8f111u    /* byte: XML formatting on a job thread (-nosavemultithreading clears) */
#define LX_FS_WRITER_THREAD_VA 0x3d8f112u    /* byte: compression, signature and writes on a job thread (likewise) */
#define LX_FS_MODIFIED_WARN_VA 0x3d8e25cu    /* byte: the one-time 'gameModified' dialog that fails one save */
#define LX_FS_ONLINE_DONE_VA 0x3db68a3u      /* byte: 0 = a successful save goes on to 0x1d66460 (online mode, save list) */
#define LX_FS_GAME_CLIENT_VA 0x3daab80u      /* X4::GameClient*: +0x3e0 OnlineManager (+0x1851c initialised, +0x1851d data sent) */
#define LX_FS_UNIVERSE_VA 0x3db6948u         /* the universe; the serializer refuses without one */
#define LX_FS_CSG_VA 0x3e50930u              /* ConstructionSequence manager*: XLib::Mutex +8, cond +0x38, pending list +0x68..+0x70 */
#define LX_FS_JTM_VA 0x3daab88u              /* JobThreadManager*: semaphore +0x558, running +0x5bc, queue XLib::Mutex +0x5c0 */
#define LX_FS_MASS_VA 0x3e59f60u             /* MassTrafficUpdateTask*: XLib::Mutex +0x70, parked +0xa0, wake cond +0xd8 */
#define LX_FS_REGION_VA 0x3e50148u           /* RegionManager*: XLib::Mutex +0x138, parked +0x168, wake cond +0x1a0 */
#define LX_FS_SOUND_VA 0x3db6c08u            /* the sound manager*: sound play 0x1a62520 returns at once when +0x78 is 0 */
#define LX_FS_SIM_START_VA 0x3e59e20u        /* XLib::Semaphore the MoveWorker waits on between frames */
#define LX_FS_TIMERBASE_VA 0x3db8280u        /* XLib::Mutex: TimerBase, also taken by every log line */
#define LX_FS_EVENT_LOCK_VA 0x3eedf60u       /* XLib::Mutex: the engine event queue */
#define LX_FS_MD_REGISTRY_VA 0x3e7e760u      /* XLib::Mutex: the MD name registry (finished cues at save entry) */
#define LX_FS_REGION_LOCK_VA 0x3eb3a60u      /* XLib::Mutex taken in 0x12dbe60 (RegionManager's closure) */
#define LX_FS_XML_LOCK_VA 0x3e74e80u         /* XLib::Mutex: the XML open-file table; writer busy byte +0x48, cond +0x50 */
#define LX_FS_RP_GLOBAL_VA 0x72f87e8u        /* rpmalloc's global spin word (heaps) */
#define LX_FS_RP_CACHE_VA 0x72f89a0u         /* rpmalloc's global span caches: a spin word at the start of each */
#define LX_FS_RP_CACHE_STRIDE 0x6410u
#define LX_FS_RP_CACHES 63u
typedef struct {const char *name;uint32_t va,length;const char *sha256;} lx_site;
static const lx_site lx_sites[]={
    {"build_id_note",0x4002fc,0x24,"563a875a9b060e74466af5da02d3ec88586b7b3f4305c4840fe4b70452d0c464"},
    {"main_loop_in_flight_check",0x72f638,0x1a,"c525558c3a509c9f81e553154c26796d821c557107303ac26485f377f112d7f9"},
    {"main_loop_sim_done_wait",0x72f745,0x6c,"0db4aedf3402446323a03bfe2e8ad1ac494d73f78cbfd9cfbf2c11704ad2d74b"},
    {"main_loop_frame_tick_call",0x72faf0,0x32,"d2dc7882f6f30bd1b9272eae441199355f71ee8c92367d7e3df48f6d13e4b285"},
    {"move_worker_run_entry",0x1212490,0x35,"b09c6bed2ccf279993e1f55142bfa88c501af14f776503a2cdba90023afd3c5d"},
    {"move_worker_frame_start",0x12125a0,0x80,"57db462588f8cf62bf9dbac327343157a2e2484b2e367a261bca245102f29d5c"},
    {"move_worker_vbi_dispatch",0x1212653,0x5d,"7ff3eedea62f3e1d21f05452ceea7f367f4edf5ab2b9655245ae69d32940680f"},
    {"thread_rng_seed",0x1a47fe0,0x53,"24eec37fc10f071a3bcf4386c56a1ff585875e5b9a0472cf76e76809178566e3"},
    {"frame_tick_head",0x2024fc0,0x20,"e88caba8ade7761b37c02ad9971fdad98bf860c6cd148a845679ccfe94651a14"},
    {"frame_tick_movie_branch",0x2025126,0x15,"7480f8d493097421addcea88ed3819e301e72c257fe188442412a93deb96215e"},
    {"frame_tick_time_advance",0x2025260,0x95,"77e65fe042991e20e6af69ae8304a7222b7330fd1eae4ca9e6d85c473c9ae149"},
    {"rodata_one_sixtieth",0x2e24578,0x8,"c6c9a57771f2313713756c05b0b8a1413e5ef99d747cabf716952b4f6137f34b"},
    /* World identities: the map insert (hooked) and one inline import site of the UniverseID map. */
    {"import_map_insert",0x10c8140,0x32b,"28ba29a7ead169fc58d632a2adc101e3b5f4944e48901fafaac2d3cc150d9891"},
    {"import_universe_site",0x128872c,0x22,"982fd4a0819c16949aa25c67d743595d8b67368f0790a46362e55e514001c24d"},
    {"sem_release",0x1a23be0,0x6a,"2f7e3764d17dc9ff4269fd115a22ed3ec472a7612846d55b6b33a0e771c7f7bd"},
    {"sem_timed_wait",0x1a23d90,0xe7,"01c00188fce9c32961ce0c59f7a017169a8ebedc2e1eeb564b294b6b421a8671"},
    {"path_sync_time",0x1a3b900,0x54,"74821eeba01b307a4a3967130c7847ae14845e58c63f8c3112216dbce922bab6"},
    {"path_update_batch",0x13e32f0,0x160,"3dc45d0c7969a2aa202cebd4ef72cafad7d8f76e44aa1c54aec019a4b7bf7706"},
    {"path_run_batches",0x13e3d86,0x116,"bf41201f69cd1aac19d13349ef89eec5ae943392b05a4db95b76dcb6b340579c"},
    {"path_budget_store",0x13e92a7,0x19,"e4b19dc95620f502ba09c0e1c23dc429ebfc7854ca9b2e67f5ea4e842829a7db"},
    {"region_move",0x12c7f80,0x92,"3d1a19b30dc2fd1f44b9bae101d1d1aacec7813d1544270246263d7204f1c8e8"},
    {"region_field_update",0x12d7a80,0xb0,"89dbf485bcf9b7d386c6ab92b95c3deb6fc2e437bd8846263a4ff44e5d657c98"},
    {"ai_wait_restore",0x10c12b0,0x40,"0a7fa69af396d07109816dea7ef818b5d2147c7e052178be246da4a0971ff8bb"},
    {"ai_wait_import_call",0x10df34b,0x28,"f7636150265b6d5bdaf903531a258ca26a8b38b505c22a69e4ceabfc4bb1e98c"},
    {"md_resume_caller",0xf77160,0x210,"128861905b15dec0ab064dbe73d3c8d7a5665e30ccbb3b78dde319cc860b56a2"},
    {"md_resume_delay",0xf5b380,0x60,"3e518da36da3a1b4a6a89fb1ef66fffab7715516e5d69872627c95cdda6e5c61"},
    {"cue_name_walk",0xf68af4,0x70,"0879d082533fffd424e7eee5f7174549e252ef6c4e70b8ce863662a89307fb83"},
    {"cue_name_walk_up",0xf68c68,0x78,"87def51e5e415f6247901815477730d21e4d08629a820fdc7c0b0f1380d74539"},
    {"cue_module_name",0xf48cba,0x20,"1c1ace52dcad1f611617d3db74b214839debc38115af00400db99e69cc32ff7f"},
    /* A static cue's path (lockstep_cue_name.h): its name +0xc8 (a libstdc++ string, its buffer +0xd8), script +0x150,
     * parent +0x158, static cue +0x160 and sub-cue slots +0x190, as the XML constructor (from 0xf3f840), a library
     * user's sub-cue copy (from 0xf584d0: its library cue's name, its parent's script, +0x100 the library cue) and the
     * MD cue import (from 0xf58c30) write them. */
    {"cue_path_xml",0xf3f9c0,0x3a0,"f2fb055604e3de3b3b5c66023ac7f4ea4c1a86b7880cf38db085b559534a0a21"},
    {"cue_path_copy",0xf58570,0x2f8,"e419b74aec2acbbd8e0fd389c11b3a13a303d5600ea86226e16f3bc30d0ba4a4"},
    {"cue_path_import",0xf58d70,0xd0,"7b595d1fc6fe4348c9eff0a2e1e2508efdca7ef3e7b90fb280724e221db9e27c"},
    {"boot_hold_call",0xfeb2a9,0x18,"a17c348e0eab113d7de29118cd8139aaaf564b90e38930d74152ad906764d3f5"},
    {"boot_hold_move_head",0x10e27a0,0x60,"ce934f2a5acdc39a4036bf8b98da31641a58eb5f5fca00d88a0b4e618cc8da0c"},
    {"event_delivery_loop",0x16c6b1b,0x6e,"3e5628a4ca1480546cb7b716889fb3d6f9e39fffe67b1e2956d951c078cd4fc9"},
    {"event_deliver_head",0x16c5db0,0x48,"9cd75f11838105a05707791b0ee376d3a0c64cbac3910515290e4c6e6d036dd2"},
    {"event_slot_release",0x16c37d0,0x40,"d7c0097cc0a4ea46222eea6e4aa547a4f33f6ab9098b186c4000932d25033fd4"},
    {"event_source_scan",0x16bb2ec,0x38,"bdf8ee0389af791142e0b31fd76c440552ab40b237d064e60c367460bfc89921"},
    {"gravidar_publish_head",0x1109380,0x70,"9cec27ac775d2cc5e7445a22d5e36ef6503903ab57d98b91da00bab2ca26382d"},
    {"gravidar_unit_apply_head",0x10fd7b0,0x30,"22b10b6b13f530dada31330ca1f94c9f43b046e23f7131a008769c804f231abd"},
    {"gravidar_run_head",0x1106ee0,0xf0,"4418563e8d7eecc497342e4dde372c35a0ef930451f0609ab788eeacbbab0b84"},
    {"gravidar_run_ready",0x1107020,0x38,"da4c2e44a3c7c3e23f3a03d8da9778576e0a2e7c2dc1b3f0757d5fc5e8b2bab0"},
    {"gravidar_round_robin",0x1108bc8,0xbc,"65ad5ddeda88bb6c108d6dd2901464f6b98fe767d0885b173385cadc938e2473"},
    {"gravidar_init_kick",0x110c808,0x14,"0b5dfdf8a6a1c82a0d5b04ef17bb92a55abc965cdd54b76fe72713da8f20388b"},
    {"x4_mutex_lock",0x1a23650,0x14,"038af7d8eb54c7ed6b55e30ca283090ef065f0c80d0622fc394eb9041748890d"},
    {"x4_mutex_unlock",0x1a236a0,0x14,"1748e005766dbd9545ae98502ba01e74e2e66656756e59a7c1865597bc3c00f4"},
    {"x4_mutex_recursive",0x1a23532,0x10,"c9eb90aad7498142cb0ec8c5267f8ccea08ef2cda023490bed6272b1d1979f9f"},
    {"macro_name_getter",0xa38880,0x5,"1bbfc61fec45130b4afe17e5efd62371ae676ae11fb9a3fa196df95327d962bc"},
    {"gravidar_list_lock",0x11087c3,0x10,"5827a69a2ff7297ad3e8a7ad787ff13303d95a82689452c5d332a1171776b1df"},
    {"sector_move_head",0x127cf40,0x60,"86931f83b684278351bc291b3d5301c4c1de9b365d1e19614ea21d3abbdd2fd8"},
    {"sector_move_schedule",0x127d0d0,0x7a,"a8aaededb498a572df7d44347899fcbca0f152748165b2b93e0173a4be90fb26"},
    {"travel_block",0x14458df,0x30,"404a99c4817174d3408fb1d0447daf2e472255a56aefc878bb37aee522f0a1a9"},
    {"travel_wingman_read",0x1446b2e,0x1a,"ba89f808b91d61b0bb782435ab4b6275450334538c87e54b18e2d87b2e973e78"},
    {"path_job_thunk",0x13c45c0,0x30,"9d2fe92b693d172f454ede0cbbf45112af18f309c9f6b6be5e8e73c32a303719"},
    {"path_fresh_list",0x13e37bd,0x10,"7ed1076574866b05ebbb7f845465539271129befbe5d9c655741819ab5316a0a"},
    {"id_remove",0xee3850,0x130,"4fc7c9013593aec0f7422271f58328ccc7099d2a1b7a7dc70f558123fde9b34c"},
    {"id_assign",0xeed870,0x240,"42f20383fa7c638d9536fcece3d03e5528e2db1b550cf15992b80bf0d1be7096"},
    {"nav_move_head",0x144a9e0,0x4c,"c31682e8f4977e36adad798bb52c33a5d6af349cddda5872e1963bb3dd41a0e1"},
    {"nav_params_block",0x144aa5d,0x4f,"59b4ad045f415d2a254af73447551b1783fc40b6b047776499cc2a9c214a8a08"},
    {"nav_probe_block",0x144ad0e,0x60,"3a76cc5c1712d543ae670bb7fdcef1072adefa6d8882edf64c4421f4a5c79b75"},
    {"nav_params_far",0x144b2f0,0x32,"ee2c1a62167795ceb5ff176f468bdf1ae8d73f0301f16cd3df547b6eb2baf3eb"},
    {"nav_probe_far",0x144b578,0x22,"ad3cacc2e2823d2e0fa59e577d7669cff003fe6eca3dbf6837e4c6c891ec8720"},
    {"attention_timer",0x1465030,0xf0,"6edf27dab4c745b10835ccc7a357f0932a176afbcce828ccd2f5a25db2093801"},
    {"ll_wares_call",0x183a470,0x5,"5430001687a6782074823ee887705c640e3b992c4edb2ea6ba4c0d3fac483c86"},
    {"ll_deployable_call",0x183a537,0x5,"cdb81bef15680e4cbd1381304d9ce77dcb3da3d55378a080e77b413118922a51"},
    {"ll_unit_call",0x183a6d7,0x5,"39c828e39d20766a856b17a983b81382237caeca4e4964cbe8bceca1d85f577d"},
    {"ll_ammo_call",0x183ac7f,0x5,"c682f725aa0f0b2e78dc8ffc9a05f417a39296d4cc3412106a5a925701afa5b3"},
    {"ll_existing_call",0x17e9063,0x5,"8cff4d0b9d1af891e7a1dd47a94fc9776e0f1197aa4640a9b977f1d88821f25b"},
    {"ll_pick_call",0x1defce1,0x5,"b5850b673fc0768af1bfd08c4578ddb15bc4de151bf07a8c676dd8aa40c6d9f4"},
    {"ll_rest_unit",0x1e9e7ad,0xf1,"75934bc056c71f6b8216701ddca6dbc1e44dd10f9c7ddebe92aa1904ac8a9446"},
    {"ll_rest_unit_exit",0x1e9e6b2,0x24,"2b38c5a371fc7792d11237b700c5cd3421f1875a0f8b41343db3b4db0e7501dc"},
    {"ll_rest_deployable",0x1df8187,0x12d,"cad223a2c9a29a015ebc98c65da29036bfa2e2cd1b80bfdb6395c486b4990734"},
    {"ll_rest_deployable_tail",0x1df8378,0x6,"87c100073f64384823bfb38deaee8ba4cb98c0c0a52e18554bd37068232f259d"},
    {"ll_rest_deployable_exit",0x1df7a02,0x12,"960d9436a602468076ecd0211e885fe3c07396c16a4c1b76d5b20bad272d93d5"},
    {"spawn_generate",0xd2a470,0x118,"68d3a7ddc2d7abd319bf25cb30636c9930d27423465d531181b61f974d347c97"},
    {"spawn_generate_core",0xd28fc0,0x50,"4a290f96608506efefe0d1b4699398bc192f1aacbc8e0aada945fa390645d2c5"},
    {"spawn_initialize_ship",0xd029a0,0x40,"3c9843531ecf2773b4a9bcbccdf09c10da69e0689c2d78f86cfdf67c60142df2"},
    {"gravidar_scan_groups",0x1caf009,0x30,"4bc72dbb1aaca2184158c8bf5512f273a0a0d6871b8e1de288f41646746a51a5"},
    {"gravidar_group_init",0x10ff73b,0x26,"dca043e4618cf83ee17c204d94dbe3fccc4dd0a8e2002d42dc122b6cd5c460a8"},
    {"find_order_execute",0xd87ad0,0x70,"5fc0b6a1bfb50182222ecd1133469332582f404ace3f96a5f4aa03262a0561f5"},
    {"find_space_execute",0xda9920,0x23c0,"437f08f2263e3255ed7ce4bfabb4ca4280a17b9e21cd588d014be0e3c1cda7b1"},
    {"flight_update",0x13e9d50,32,"09f259f5cb97414053102eb0715f69b31cc5e0216e8f3ec7cb8eda999553aa33"},
    {"agent_set_model",0x1432b80,0x1280,"8ba9e7c46cb10431a0632f3dd29e4ac1d1a3bce4666a2f2c5e07b61d8f37b8c1"},
    /* The flight-parameter builder (Windows 0xea1e80), whole: gate_battle.h's params_init. */
    {"params_build",0x14378c0,0xa30,"5df8e90bac02b27d74750b2bbcb5d25b3696d574ac5961e98c793847cdd061f5"},
    {"script_exec_run",0xd5fbb0,0x73,"ba595478c53852158f7ca0d93fc6a24ab3a4e0aa7442dfa3d74510d137c490f1"},
    {"script_exec_range",0xd5fc30,0x6f,"a75340d2594bb8d07cf693976b4630a66483a9ab63db2dd1411089540470b450"},
    {"script_ai_event",0x10d4270,0x50,"5a5288bf8d4649e9cbff9fa15b725393604cf377952019b6ed49ef84eef731dd"},
    {"script_stack_event",0x10d4060,0x60,"2e0531ca319aa15c081dcb0bd855e6e83ff2b7aac607bed8d63b1059e3741ea7"},
    {"script_cue_check",0xf76110,0x30,"153b266840dfa400d36232054984f1b58010920efa75ebf326353226cff42ae7"},
    {"script_cue_event",0xf799c0,0x5,"529c42874c3721ae4eacc50351b52aa4394e77063b350c61def1e5eb1db3e805"},
    {"script_cue_wait_site",0xf76500,0x18,"217674bfb2496a9da8474ac3eb379fca9e527c0ca686179a06c571053074c94a"},
    {"script_cue_raise_site",0xf78d30,0x10,"5022fe7ccee724d6de65038962de3a86f555b0754596ec99fd4eab81839d4da8"},
    {"script_cue_timed_site",0xf79d10,0x10,"2d08723110d795d5e2a465bd828319b1716e7997eb1e5d0bf7a1e8e0a7ae5674"},
    {"script_cue_value",0xf5b3e8,0x28,"6930ebececcbeb5e80609e832f9f430d56b252f333617cd04c8572f1a2fdf9b0"},
    {"import_map_clear",0x11e0b30,0xb0,"74e893b9c8b3765d6ecae61a3466a8bf0f869b82854838f383cd998e97b4b3b4"},
    {"import_clear_load_a",0x1cdd737,0xc,"bd1343601d67f21d4934ae7c5b41476a601f76c77d7c7a795fd03368ef658581"},
    {"import_clear_load_b",0x1cddf54,0xc,"412caedcd74e04b7f0dda787501c8c96ad44fc50ce894522964c6ab4f15d4a01"},
    /* World hash: find, registry, bank selectors and the layouts it reads (Windows offsets in gate_world.h). */
    {"component_find",0xa68df0,0x93,"6177d6aa24635956c7ed938eb80e67b6175ce95a1fb87bc1d0ef261eb9ef1684"},
    {"component_find_use",0xa3c096,0x13,"30981a421f027e8f70cf2a3b40809c57115b513f5a4a12eac3a32049122e657a"},
    {"move_bank_swap",0x13e3560,0x7d,"a5cd9f3ad84083aca642f843e6b6e803f87565c84adbfec928fce568b4b1efa5"},
    {"nav_select",0x14498d0,0x552,"24bcf9859bf3e139fb8214c95d64b9d11fa99a6a846f405054877911a33f2dac"},
    {"nav_agent_construct",0x13d1dd0,0x109d,"e93ba23b94d19e6d754822a11afcab0df82f653c1f74ae73084484cadc19ae3d"},
    {"nav_agent_commands",0x14286a0,0x31a,"d88d9de43b1ead89cb51ec811d68539d1e91fcadec733628f3df5aab41c20df0"}, /* up to the exact-reciprocal window 0x14289ba */
    {"component_controller",0x13142a0,0x1cf,"2593d770c68f51f6377bc734e995a76e2b1c0fdcc21a73042b271303b0812d91"},
    {"component_hull",0x11b4230,0x87,"e750d3e995fc0bc47b8a2932e43e8209490df0bb88ca9511f6e2eedc29fcb7b9"},
    {"component_flags",0x18c73b0,0x96,"fdeabdccb6836ba76a71270b78b48391a137f936ce803a750a9297950f765bfa"},
    {"component_fields",0x14670f0,0x1617,"49716fb3a8a863cfe3525cae32e22f96f8cd30068e62d4933c282b1717a4f392"}, /* around the window 0x1468707 */
    {"component_fields_after",0x146870f,0x1180,"0a1c2a890be795f3b0dfe3697dca6be6fa0e8b4bc6e0a83e9a1a6cfa19edfa56"},
    {"shield_percent",0x17d7b70,0xb6,"d8e75cdbda4450259bb1b2503407f66ceba721584f6e9384a64b4ae3b2bb6f30"},
    {"shield_recharge",0x1293330,0x32e,"0e5e820415c66333da71b051d131239375d0cb5ccc86a201b6e099ef4bdd5e20"},
    {"shield_group_construct",0x17be2e0,0x28e,"57b744a6fe7b8eca588d4104746b38b5559c1a831d9cfd8a78f659b6c247685c"},
    {"dock_evaluate",0x1758130,0x1268,"da65aa570ad47377504f25a3762a3a4a414fe6ffdbae4d52cd863baef75640c5"},
    {"dock_state",0x176d330,0x3e4,"e6dedf6013e69fe35cda4e01d71619f31063787ca7bdab0a7277d331436adfc3"},
    {"undock_state",0x118d1c0,0x17b,"79f1a74439df157a685c6f3beba3c329ccc1fc561abe265822d15939f79c2cb9"},
    {"static_value",0xd58000,0x27,"a062371a0ca71707d40dcd41e6ecfe0a219ba9e10ff58bd67f2136a3d5d76b6c"},
    {"movement_value_set",0xe7ed30,0x29,"1e1b2abccd23b0c40a35722ede28ca40d004e9cd032156a21ad90043a108f8f2"},
    {"movement_value_ref",0xd57f00,0x5,"fe5d0803355430c1264111b6534266cc4ee3a28ac617487a8aa094b3f344c0e9"},    /* Non-blocking save by fork (gate_fork_save.h). */
    {"fs_deferred_save_call",0xfecd70,0x10,"518c8e9be577872e9a547c40c65b934fbd0a1621906e59b62e13315a089916ab"},
    {"fs_save_countdown",0xfebe26,0x13,"a074e5c454ced03983f74a9d9ea2974dbe80dec87c15a3201b2fd8142e0b13fa"},
    {"fs_save_entry",0x1cd3d30,0xf1,"a17c371bce1fa3a5912b0dc8a0bb6468aa50dfa6b9cf99b8393949d6a73c475a"},
    {"fs_save_flags",0x1cd3f3d,0x50,"614a3224682e354f52f832af330dd126a34816177833cb57af417a7ab85fb622"},
    {"fs_save_pathing",0x1cd570b,0x90,"19a81ce818400b8d4de3bb6c663394d3a7eacb7eebc4ff82d89fc2226aef72c1"},
    {"fs_save_online",0x1cd6042,0x40,"a57b293f3e2a6087052696188105c161977a294e04478a224b14df6bcfa30cc9"},
    {"fs_save_finish",0x1cd61fc,0x50,"707b1a44a229bc7486d6c1bccc9fb06c1748e7577f00c6f5632393de0c0b321c"},
    {"fs_save_after_rename",0x1cd6365,0x52,"234510abdfc9215b1e2b86eb1476cd033bef156de51aa98c4a2661cea55220a5"},
    {"fs_modified_check",0xfb4b80,0xd2,"d340ee5309a7fbc5525accb9bf176474f450ced90aa1d4c17e58565d0df57c6f"},
    {"fs_saving_possible_head",0x1de5ef0,0x16,"d4f4984ff3792c5b3379a97a3970991da42503bc0fc415dff73d918b58fd69bc"},
    {"fs_move_worker_frame_end",0x1212756,0x81,"fe4c2ddfeaacb214d8340ed1c0515f4096bdf26dc68ba5c077dbf0ffa9092048"},
    {"fs_mass_park",0x11f96f9,0x41,"5e87c4b1e711ba3e1b2534555e57316dd3f3b23f0d5dbee29fe8e155edae7dc7"},
    {"fs_region_park",0x12ef383,0x41,"0f31a4aa1f1090d7288d60138ea188592412bc427eff1e6f04b634ebf0077be6"},
    {"fs_path_kick_wait",0x13e3679,0x9,"638179667ca1ed9799e2b5e79a668555d3eb8b0b91d727c2c7c75e73bf592a0f"},
    {"fs_jtm_worker_loop",0x1a54e26,0x58,"659e48e845de5aabaa698640ee50831f3bad6712c11decafb35799d9f9f29ef7"},
    {"fs_jtm_queue_head",0x1a55943,0x2f,"f1db8292c5aae2af3e6279515d8dbf6c85c9cb3d0decf365464b801035f01865"},
    {"fs_xml_open_lock",0x1068e38,0x91,"f4049390f8b7fb2e2ea5d48090e43f0412e1785a6ff14e954e257664ddc56694"},
    {"fs_rwgate_write",0x1a52bb0,0x24,"39775eff775d4c0a350b76bc188a2258c13d0b948c45de16fb1cc3d1abeb9b92"},
    {"fs_rwgate_write_wait",0x1a44df0,0x3a,"e647783f45394cd4c06e1b641bdc12d8c9093df963662c028e1f5b31d90f11f1"},
    {"fs_save_names",0x1cd4aa1,0x1f,"6692781c7674f786859c605135b3d1bb1619e68d9401d548393dd6b520718033"},
    {"fs_sem_wait",0x1a23cc0,0x4f,"9870fec4c21bad5e9a894157ab8866e1975c028bbb050de4319105d200371619"},
    {"fs_condvar_wait",0x1a23f70,0x1c,"7d184b24c9d31e3bb61eb32050bb342356deb00fe80742a91ffa0462f643ff85"},
    {"fs_event_lock",0x16b866e,0xc,"825492a2f0b6eaff781e250be92875d72f085406ca6ccae33ef8a91ee0e3ec05"},
    {"fs_md_registry_lock",0x11c69ce,0xf,"0cffcb1f6bc3b1544cb80ab72c319117c503db843121f16222ab391de08f906f"},
    {"fs_region_lock",0x12dbf5c,0xc,"08e122ec7d2cda34daf251e301c909189b41ada47b08e6e9a31e17a03d9fdd7f"},
    {"fs_rp_global_lock",0x24388a4,0x15,"4b804ac9fceb5fd2eef79ac814b37b798d0e8d7183e015a0020258d14dfb6f4a"},
    {"fs_rp_cache_select",0x2438bd7,0x31,"591e6fcb7e004d8e0c297bf20f96380ba9d13ea3dc4f8121bfb4011d6bd226c8"},
    {"fs_rp_cache_lock",0x2438cf0,0x20,"c2285541ee2e93dc7055b95d2d42ee38fb7c3bd7134c7183414ed879cdd8cf77"},
    {"fs_sound_play_head",0x1a62520,0x60,"224dae565497fc9f6fd890b0f9b79fee64de293038204f9323263da729fbf5ea"},
    {"fs_sound_manager_use",0xa69e6a,0x10,"032cfe9051445ae0fc464dc76e7ad7a776e67415c2fdff02bb5e185b18714393"},
    {"fs_save_dir_init",0x8a0d60,0x1d,"9636ee83c514904556579b8ea6712c5c29074ee53204809a25883ad0f17728d0"},
    {"wfire_shot_head",0x1138c70,0x110,"8cfb97d19dc638180e4124820a7d99da3c4c9eb1d66c587727d2db784ad41182"},
    {"wfire_fire_loop_call",0x1d69a10,0x80,"2d2595c5e3c41e67cbe91b7310c26c96a3fa18c872dd3d4bc0fada7c691e7528"},
    {"build_rng_scheduler",0x1838e80,0x180,"c6b5cd949298ae677d231ff345d92e22295830999b43a2a949b9027fc50ff6e7"},
    {"build_rng_float_range",0x1a491c0,0x7e,"bd0f5a41ee0f72ece023eec0b6ae4134c62e345874c57fe45dc17194d190c6ea"},
    {"economy_watch_import",0x18412e3,0xae,"03d28287744ceff1c336a17b4482cfb63ef156e6e51a33044e58b5c5d352bb4c"},
    {"economy_watch_guard",0x1878db0,0x80,"e570edab29e58f8e5c464e611cfafbbdca8f36811466ac8fd5c2ccf40dbae58b"},
    {"erng_listener_head",0x14382f0,0x58,"93b8240094f88e774f4ded9f45ec8cb805ddcdd9588218575a3e4a8a422de45c"},
    {"erng_listener_tail",0x1438350,0xc2,"2a0cbb338bb82c0a492b6b013e484597c6fd63233614e1f429c0d12edcc7ef18"},
    {"spline_update_head",0x1363de0,0x110,"53ce49e351fba194737e429eb0ebf71daa1d84a01a357241cd243b9382208ef7"},
    {"spline_worker_run",0x10b0b30,0x140,"c955dfb74cada4c8b9c88f2f6bba275f92a13600064ad440f1193368773ac0a1"},
    {"spline_worker_make",0x2001180,0x88,"ff6e4eb1570b0d3ec5d61eb773e2319cb2871ecc1cd493d3585b010352bb2616"},
    {"ship_move_head",0x12b81d0,0x80,"caef5e06d738d33e8e9fa58899f08d841f86c59dac77d949b6b9c1bac96ed3c1"},
    {"agent_update_head",0x1444c70,0x80,"d4d0dea0ff9f3a22a764f0db3bb2458fce330121a696f61a8f46a6b469e73d54"},
    {"player_timer_head",0x138e6a0,0x40,"1108f59e815cb16932eafd2abb90b3712f8b317593d8bcba99253e15182f3ca4"},
    {"player_timer_check",0x1390297,0xb0,"e7369926888691a76f43bb38861795dff5e9bf833ba7bccc7ced28aa9ca9f8b6"},
    {"player_timer_init",0x1daf7d9,0x3f,"b65ee778863f9825537abd0c5961d1263a7a1ae545b829d3143e02bb5ba5185f"},
    {"mt_region_kick",0x12d1680,0xb6,"fff0564cb5e327552abb9dc2a477d8360d7a36ab6acfde1d2f4e44319d4e9eca"},
    {"tempzone_find",0x12981b0,0x370,"d5f9f35b15c60ebb3d4030a0869335719f2672cb60f1097379604efaf9be5d3d"},
    {"tempzone_active",0x1cb3f40,0x63,"34908166d8fc2caa0857e17c563ebf6247642b20af7fbcc6e96e465f084e82a6"},
    {"tempzone_view",0x12021e0,0x1f,"2ef64c84b2b3fc6c1f518dae50e6506e023c192bfdb605616e811d45f3bbf513"},
    {"tempzone_reset",0x1dc8de0,0x44,"33edf50b7952aed29d92d6da68b8010080f86d69cff1a478f527f60b4ae6ad60"},
    {"tempzone_addref",0x1fa4030,0x1d,"e5af245e2067a61a22515f18edf95055890c87ca5adde31064b8bd358ec62658"},
    {"tempzone_path_create",0x144907d,0x14c,"481557ea5ea44e65b747f4e2e19e465bc9a019099a4459af5ee96c65fe12b827"},
    {"tempzone_path_store",0x1449577,0x40,"4b142b9c6e782c808ed3f137baf42c5f296a9d88b2c1ab8a47b32cf0622dbc9e"},
    {"tempzone_path_find",0x14495eb,0x2d,"df6aff5d24d3bde72af5eaf9ad8d1483b06dd70e39f8473c4abc73d9e805f632"},
    {"tempzone_switch_create",0x142f9e7,0x9f,"f4d4403453e5e9d129973c440d222b295ca6014cf30c98e9ae8fb7ea074810a9"},
    {"tempzone_switch_find",0x142fb14,0x32,"aba5058e48115c99e73f950ceee3521a4127ae5f1e03967e94ac04db7041989a"},
    {"tempzone_switch_store",0x142fc3b,0x34,"8760f6eb462b271cc5147c0788de2ad43fc11d52762c2a16287e16a2ed04356d"},
    {"tempzone_runpass_test",0x13e3d78,0xe,"93056c02490f3ab131659156e374f92165751f290dff0dedb773628a30f5f562"},
    {"tempzone_runpass_clear",0x13e4143,0x6b,"cadcdc7bca5a195e675118b40207062acee8c6532c9c53762594f9cf0e3c6f74"},
    {"tempzone_region_call",0x12ec381,0xd,"1fb1ad65f6b477df43785690f73bb6bda83dbdd35aed0bfcda2a42fbed8a1b51"},
    {"warp_dispatch",0x16b3e30,0x130,"8bc85c7f59452b43ab28bae7a0e145b69321a25ce9d0e554fe3b6253b135d752"},
    {"warp_place",0x14622c0,0xf3,"0dc370c5d6177a3d106e382ad1b18f5dcae05d0326a25ce92d22a812ee7fa42e"},
    {"warp_direction",0x11556e0,0x6a,"757b216752d8fae513aa82b7b76540bcca909173a4f2308ba6f04b6b488735ba"},
    {"warp_draws",0x11557f8,0x16,"3b4181770200ccb0e889f194966dc74e06d0c8c2084265b3809a04f4371ecdd8"},
    {"warp_frame_product",0x16159fc,0xdc,"19304b1b7f1635dbfe80dde7fafb98b4973558057950193979d2454a2c7028a4"},
    {"warp_frame_call",0x1615817,0x55,"6e5b13caa6d9d2bd929c9152e64dbf1b4e89125bd790404135eec06fa88c685a"},
    {"warp_gtrt",0x18a0090,0x3e0,"8489683612cc942bd00c117405c4b2ba6e02307198a653f05b574d2e62b08ccd"},
    {"warp_exit_sum",0x16152fc,0x57,"8796bf36163293c7d9ba764f473ccdd9b81cf114c9db8bdbc791f6884fa36436"},
    /* gate_flight_limits.h: the engine parameters' seven limit words, 0x148d812..0x148d936 (its final jmp included). */
    {"flight_limits_block",0x148d812,0x125,"ce57d1e551ae7ed42a7d15b25d61df1219c4c30de4648ff3b06e21f857ed9c6c"},
    /* gate_flight_limits.h: the thrust words' inputs, sum and words, 0x1481274..0x14813dc. */
    {"flight_thrust_block",0x1481274,0x169,"428540eafba90b0f5b303cfce6de0c438fb1969383b169ef2614f2c376f2d97c"},
    /* gate_body_mass.h: JPH::BodyManager::AllocateBody's head, and XPhys::XDefinedMassShape::GetMassProperties. */
    {"body_allocate",0x1a969d0,0x30,"5310a4438336ae1f8a354e1813c6b2d74fb0749d3177cad2cfd806f01fcf6407"},
    {"physics_update",0x1b1ce30,0x30,"3ac40c8da74d559187f9bea519425f0f8932cd8f501b2ec70463e2da02134eb9"},
    {"nav_set_velocity",0x141fe50,0x40,"9039dc81f5cd4579a91a949cb1137110ea5d37d71f480a78de4af518736793a7"},
    {"body_add_impulse",0x1a7fc90,0x30,"ec44503b5aab73848772bbd3cd72e770ad35ec077ea1bd78bba2642c99bbd178"},
    {"ik_step_block",0x1565fb0,0xc0,"dc6b7d7e6c1fb42a2694df394c9d89fa6574b6592787dff4c486e2ed66d93601"},
    {"ik_step_dt",0x2c76dd0,0x8,"4f5163b57f2e2c5276ec0ba17cb5eb4865417faf8c3590126592998fd1da3e42"},
    {"ik_wrap_tail",0x15661c0,0x60,"98e46a50b2cf8111eab6cda0ec549913e60a2356ce72f85cc33e6c00f7688f25"},
    {"body_defined_mass",0xed4cf0,0x8e,"f640dc2b2c63a1a363cc8f487544824b91ee72e7eadc3da0dcd7117084634874"},
    {"mining_search",0x1f47b20,0x6d0,"2793312d8762d21df2f389e8d00a52856fad88569264f6c63e0365e368a9ba07"},
    {"rid_ui_create_instance",0xb08820,0x280,"668801b166713825545657c8f611638dc1d2d0fedd503458e50f692d235a2f63"},
    {"rid_ui_controller_move",0xa758a0,0x25f0,"e28098824694f1b812c0c0ad5889df11d7feec884245cb8d7e5851d22f5e46e0"},
    {"rid_ui_game_render_move",0xfb0180,0x70,"fa4b6b5d88d285cb39eb2160f8382cb54b1b2b4e2578a5d9613c8fc0a4c9b59b"},
    {"rid_view_message_start",0xc78bc0,0x1d0,"3ac92db32210fe28cddfcb868a385e1b3cf7da8246ad036d7dda7710f45c6060"},
    {"rid_view_create_source",0xa70cd0,0xbf0,"40eb3dfb659d525c4cc16548a7fd1b31c9d30eee3bbcf228b344fe2f4323b2ea"},
    {"rid_allocate",0x1e23dc0,0x180,"9623eb2a8bdc9d4bdf8ab7ea34fd69b95557c42c29c90e1c1c0df037490dd282"},
};
#define LX_SITE_DT "frame_tick_time_advance" /* hashed with the two dt qwords normalised to the original */
