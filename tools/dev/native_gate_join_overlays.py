"""Overlays of the hot join live tests J1 and J2 (reports/LOCKSTEP-HOTJOIN-20261101.txt section 9).

savers  The session overlay of the present clients: a session of N slots (default 3) of which the lane session runs the
        first N-|absent| (the lane tool gives its clients p1, p2 in --lanes order; the absent slots are the last ones).
          build_native_gate_overlay.py --gate-dll DLL --snapshot SNAP --mode battle --session-clients --world-identities
                                       --join report|strict --join-absent K --out OUT.base
          native_gate_players.py overlay --base OUT.base --out OUT --gate-dll DLL --probe tools/dev/native_gate_probe.lua
                                 --players N --mode coop --source stock --defaults-only [--set ...]
        then native_gate_clients is pinned to N: the lane tool's @X4LANE_CLIENTS@ is its client count (the lanes), not
        the session's slots. The relay runs --clients N --join --absent K.
joiner  The overlay of the joining slot: the same recipe with the join save as the snapshot and the saver's package
        (--join-package, --join-package-sha, --join-mode); the same N, the same identity plan, the same --set keys, the same
        absent slots (--join-absent: the handoff's, else --absent).
Both name the absent slots in native_gate_join.absent: every per-slot module (pilot inputs, stock, slots, bodies, death,
presentation, fire, UI replay, own prediction, hit claims) treats them as neutral until their HANDOVER.
Rejoin (roadmap 15): savers --rejoin K (and --absent '' for none) names slots played from the start that the relay may
drop (its --rejoin, --stall-drop-ms): native_gate_join.rejoin on both sides; the session then runs all N slots on lanes and
the joiner takes slot K again after its drop (the handoff names absent and rejoin slots).
        From the session driver's handoff (native_gate_join_driver.py; --wait for it while the session runs) or from
        explicit --save/--package/--package-sha. The package is verified first (every hash, the save it names).
        Every saved ID of the joiner's effective config (the session defaults applied: the snapshot fixture, the identity
        plan's ships, starts and sectors, the pilot slots, watch lists) must be a saved identity in the package's IDS
        section, else it refuses naming each key and ID (the overlay is renamed OUT.refused). X4 renumbers what it saves; the
        joiner's import keys every object of the join save by the session's portable ID, so these IDs stay valid on the
        joiner only for objects the session had at T. With the snapshot fixture configured, the package must carry the
        session's view of each fixture actor at T (lockstep_snapshot_fixture.h: nav, drift = a wreck's drift controller,
        bare = no controller), else it refuses as the gate's join_load would (fixture_view_missing, fixture_view:<i>:<view>).
        The probe's state the joiner restores in frame T+1 (its LUA section: the v2 fixture state, the world state between
        captures, the pilot window and a T before its take-2) must be there, else it refuses as the probe would
        (lua_state:<provider>:<why>; reports/LOCKSTEP-HOTJOIN-20261101.txt 1.5).

--join-census (both roles; reports/LOCKSTEP-HOTJOIN-20261101.txt 1.6): native_gate_world_hash_join = true, one more world
capture at the join's T+1 on every session client and on the joiner (needs a --set native_gate_world_hash_stride), so that
census_compare.py SESSION JOINER --tick T+1 --left-client 1 --right-client K compares the world right after the save and the
load, before any dynamics. Give it to both roles (the capture enters every client's declared state alike).
The joiner prints the package's version and world side table (version 2: HEALTH and POSE records, lockstep_join_world.h;
version 3 SHIELD, NAV, ATTN, KEYS; version 4 PATH and REGION); an older package refuses a strict joiner (package_v1..v3), as
the gate's join_load does.

Both refuse an existing --out (and its .base); both write x4lane-join.json (documentation, not installed).
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_gate_join as join                    # noqa: E402
import native_gate_join_driver as driver           # noqa: E402
import native_gate_defaults as session_defaults    # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DEV = ROOT/'tools/dev'
CLIENTS_TOKEN = '@X4LANE_CLIENTS@'


def sha256(path):
    return driver.sha256_file(path)


ID_TOKEN = re.compile(r'(?<![0-9a-f])[0-9a-f]{16}(?![0-9a-f])')
# keys whose 16-hex-digit values are no saved IDs: native_gate_join_watch and native_gate_join_refwatch name portable IDs (saved or
# created), the same on every side; the gate resolves them through its own ID table (lockstep_join_world.h join_watch)
NOT_SAVED_IDS = frozenset({'native_gate_join_watch', 'native_gate_join_refwatch', 'native_gate_join_jobdump'})


def saved_id_keys(config_text):
    """{key: [16-hex-digit IDs]} of the effective config, as the probe runs it (the session defaults applied)."""
    values, report = session_defaults.apply(session_defaults.parse(config_text))
    if report and report['errors']:
        raise ValueError(f'the joiner config is refused by the session defaults: {report["errors"]}')
    found = {}
    for key, value in values.items():
        if key in NOT_SAVED_IDS or not isinstance(value, str):
            continue
        ids = sorted(set(ID_TOKEN.findall(value)))
        if ids:
            found[key] = ids
    return found


def check_saved_ids(config_text, package):
    """Every saved ID the joiner's config names must be a saved identity (class 0) of the package; returns {key: count}.
    ValueError names every key and ID that is not."""
    saved = {portable for _, portable, klass, _ in package['sections']['ids'] if klass == 0}
    keys = saved_id_keys(config_text)
    missing = {key: [i for i in ids if int(i, 16) not in saved] for key, ids in keys.items()}
    missing = {key: ids for key, ids in missing.items() if ids}
    if missing:
        raise ValueError('saved IDs of the joiner config are no saved identity of the package (the session had no such object '
                         'at T; X4 renumbers what it saves and only the package maps it back): '
                         + '; '.join(f'{key}: {",".join(ids)}' for key, ids in sorted(missing.items())))
    return {key: len(ids) for key, ids in keys.items()}


BINDABLE_VIEWS = frozenset({1, 2, 3})   # nav, drift, bare (native_gate_join.FIXTURE_VIEWS)


def check_fixture_views(config_text, package):
    """With the snapshot fixture configured, the session's twelve views at T (GATE fixture) must be present and each one the
    joiner can bind; returns them as view names ('' without a fixture). ValueError as lockstep_join.h join_config_refused."""
    if 'native_gate_snapshot_fixture' not in saved_id_keys(config_text):
        return ''
    views = package['sections']['gate'].get('fixture', join.NO_FIXTURE)
    if views == join.NO_FIXTURE:
        raise ValueError('fixture_view_missing: the package carries no view of the snapshot fixture actors (a saver build '
                         'before the views, or a session without a bound fixture)')
    for actor, digit in enumerate(views, 1):
        if int(digit) not in BINDABLE_VIEWS:
            raise ValueError(f'fixture_view:{actor}:{join.FIXTURE_VIEWS[int(digit)]}: the session saw a fixture actor the '
                             'joiner cannot bind')
    return ','.join(join.FIXTURE_VIEWS[int(digit)] for digit in views)


def check_lua_state(config_text, package):
    """The probe's state providers the joiner's config needs (native_gate_probe.lua; reports/LOCKSTEP-HOTJOIN-20261101.txt
    1.5) must be in the package's LUA section, in the form this build restores; returns the provider names. ValueError
    'lua_state:<provider>:<why>' as the joiner's probe would refuse it in frame T+1 (a saver build before 1.5: fixture v1,
    no world_hash or pilot_window line)."""
    values, _ = session_defaults.apply(session_defaults.parse(config_text))
    lines = dict(line.split('=', 1) for line in package['sections'].get('lua', []) if '=' in line)
    tick = package['sections']['gate']['tick']
    if session_defaults.on(values.get('native_gate_snapshot_fixture')) and (values.get('native_gate_hash') or '').strip('\'"') == 'battle':
        fixture = lines.get('fixture')
        if not (fixture or '').startswith('v2|'):
            raise ValueError(f"lua_state:fixture:{'missing' if fixture is None else fixture[:3] or 'empty'}: the package carries "
                             'no v2 fixture state (the session wrecks and guns: a saver build before it)')
    if session_defaults.on(values.get('native_gate_world_hash_stride')) and lines.get('world_hash', '-') == '-':
        raise ValueError('lua_state:world_hash:missing: the package carries no world state between captures')
    if session_defaults.on(values.get('native_gate_pilot_inputs')):
        window = re.fullmatch(r'\{\s*(\d+)\s*,\s*(\d+)\s*\}', values.get('native_gate_pilot_window') or '{30,600}')
        if not window:
            raise ValueError(f"lua_state:pilot_window:config: native_gate_pilot_window {values.get('native_gate_pilot_window')}")
        start, stop = int(window[1]), int(window[2])
        if lines.get('pilot_window') != f'v1|{start},{stop}':
            raise ValueError(f"lua_state:pilot_window:{lines.get('pilot_window', 'missing')}: the session pilot window is not "
                             f'this config window {start},{stop}')
        if tick+1 > start-5 and not session_defaults.on(values.get('native_gate_join_in_window')):
            raise ValueError(f'lua_state:pilot_window:late: T={tick} is past the window take-2 ({start-5}); the window Lua '
                             'state is not in the package')
    return ','.join(sorted(lines))


def check_world_table(package, mode):
    """The package's world side table: 'v<version> health=<n> pose=<n>', from version 3 'shield=<n> nav=<n> attn=<n> keys=<n>',
    from version 4 'path=<batched agents>/<new agents>/<batches> region=<n>', from version 5 'flight=<linear>/<other models>
    spline=<n>', from version 6 'agent=<n>', from version 7 'route=<heads>/<points>', from version 8 'param=<n> dock=<n>', from
    version 9 'steer=<models>/<path points>', from version 10 'solver=<active>/<dormant> bullet=<n>'. ValueError package_v1 .. package_v9
    for a strict joiner of an older package
    (lockstep_join.h join_version_refused)."""
    version = package['header']['version']
    if version < join.PACKAGE_VERSION and mode == 'strict':
        what = 'no world side table' if version < 2 else 'no SHIELD, NAV, ATTN or KEYS sections' if version < 3 else \
            'no PATH or REGION sections' if version < 4 else 'no FLIGHT or SPLINE sections' if version < 5 else 'no AGENT section' \
            if version < 6 else 'no ROUTE section' if version < 7 else 'no PARAM or DOCK sections' if version < 8 else \
            'no STEER or STEERPT sections' if version < 9 else 'no SOLVER or BULLET sections' if version < 10 else             'no GRAV, GRAVC or GRAVS sections' if version < 11 else 'no SPLM, SPLME, SPLMSEG, GRAVN, GRAVP or GRAVQ sections' if version < 12 else 'no JOBE, EVQT or GRAVD sections'
        raise ValueError(f'package_v{version}: the package carries {what} (a saver build before them); a strict joiner refuses it')
    sections = package['sections']
    names = ('health', 'pose')+(('shield', 'nav', 'attn', 'keys') if version >= 3 else ())
    text = f'v{version} '+' '.join(f'{name}={len(sections.get(name, []))}' for name in names)
    if version >= 4:
        kinds = [r[1] for r in sections.get('path', [])]
        text += (f' path={kinds.count(join.PATH_MEMBER)}/{kinds.count(join.PATH_PENDING)}/{kinds.count(join.PATH_BATCH)}'
                 f' region={len(sections.get("region", []))}')
    if version >= 5:
        flags = [r[2] for r in sections.get('flight', [])]
        other = sum(1 for f in flags if f & join.FLIGHT_OTHER)
        text += f' flight={len(flags)-other}/{other} spline={len(sections.get("spline", []))}'
    if version >= 6:
        text += f' agent={len(sections.get("agent", []))}'
    if version >= 7:
        kinds = [r[1] for r in sections.get('route', [])]
        text += f' route={kinds.count(join.ROUTE_HEAD)}/{kinds.count(join.ROUTE_POINT)}'
    if version >= 8:
        text += f' param={len(sections.get("param", []))} dock={len(sections.get("dock", []))}'
    if version >= 9:
        text += f' steer={len(sections.get("steer", []))}/{len(sections.get("steerpt", []))}'
    if version >= 10:
        active = sum(1 for r in sections.get('solver', []) if r[1] & join.SOLVER_ACTIVE)
        text += f' solver={active}/{len(sections.get("solver", []))-active} bullet={len(sections.get("bullet", []))}'
    if version >= 11:
        text += f' grav={len(sections.get("grav", []))}/{len(sections.get("gravc", []))}/{len(sections.get("gravs", []))}'
    if version >= 12:
        text += f' splm={len(sections.get("splm", []))}/{len(sections.get("splme", []))}/{len(sections.get("splmseg", []))}'
        text += f' gravn={len(sections.get("gravn", []))} gravp={len(sections.get("gravp", []))}'
        text += f' gravq={len(sections.get("gravq", []))}'
    if version >= 13:
        text += f' jobe={len(sections.get("jobe", []))} evqt={len(sections.get("evqt", []))} gravd={len(sections.get("gravd", []))}'
        text += f' viewk={len(sections.get("viewk", []))}/{len(sections.get("viewkc", []))} viewf={len(sections.get("viewf", []))}'
        text += f' viewp={len(sections.get("viewp", []))} shoot={len(sections.get("shoot", []))} attk={len(sections.get("attk", []))} livef={len(sections.get("livef", []))} spec={len(sections.get("spec", []))} navc={len(sections.get("navc", []))} jobs={len(sections.get("jobs", []))} gone={len(sections.get("gone", []))} body={len(sections.get("body", []))} bupd={len(sections.get("bupd", []))} ldc={len(sections.get("ldc", []))} navt={len(sections.get("navt", []))} nmesh={len(sections.get("nmesh", []))} noct={len(sections.get("noct", []))} agtt={len(sections.get("agtt", []))} agcb={len(sections.get("agcb", []))} pbnk={len(sections.get("pbnk", []))} pend={len(sections.get("pend", []))} gravb={len(sections.get("gravb", []))} viewl={len(sections.get("viewl", []))} ikv={len(sections.get("ikv", []))} rcnt={len(sections.get("rcnt", []))} ecos={len(sections.get("ecos", []))} shpw={len(sections.get("shpw", []))} trdn={len(sections.get("trdn", []))} npca={len(sections.get("npca", []))} jobg={len(sections.get("jobg", []))} code={len(sections.get("code", []))} engr={len(sections.get("engr", []))}'
    return text


def census_sets(args):
    """--join-census: native_gate_world_hash_join = true; it needs the census stride among the --set keys."""
    sets = list(args.set or [])
    if not getattr(args, 'join_census', False):
        return sets
    if not any(re.fullmatch(r'native_gate_world_hash_stride=\d+', text.replace(' ', '')) for text in sets):
        raise ValueError('--join-census needs --set native_gate_world_hash_stride=N (the census whose capture it adds at T+1)')
    return sets+['native_gate_world_hash_join=true']


def precise_sets(sets):
    """native_gate_join_precise = true on savers and joiners (the join save at full precision, lockstep_join_precise.h; j1-115: the
    census at T+1 equal) unless a --set names it."""
    if any(text.replace(' ', '').startswith('native_gate_join_precise=') for text in sets):
        return sets
    return sets+['native_gate_join_precise=true']


def refresh_sets(sets):
    """native_gate_join_refresh_builds = true on savers and joiners (every station's builds brought to one frame time at the join's
    T+1 on every client, native_gate_join_shipyards.xml; j1-134: the job ships' pick at 648 alike, the 900 census 420 -> 172) unless a
    --set names it."""
    if any(text.replace(' ', '').startswith('native_gate_join_refresh_builds=') for text in sets):
        return sets
    return sets+['native_gate_join_refresh_builds=true']


def ids_sets(sets):
    """native_gate_join_ids = true on savers and joiners (the joiner's saved objects at the session's runtime IDs; every J1 since
    j1-117 ran with it, and an in-window joiner needs it: the UI replay and the slots' runtimes are keyed by runtime ID, w1y
    refused slot_respawned without it) unless a --set names it."""
    if any(text.replace(' ', '').startswith('native_gate_join_ids=') for text in sets):
        return sets
    return sets+['native_gate_join_ids=true']


# The admission's opt-ins that earned their place (5 November), on savers and joiners unless a --set names them:
#   native_gate_join_rcnt          the session's reference counts of its created objects (J1 j1-193/195: first mismatch 720 -> 819,
#                                  no crash through 1799). EXPERIMENT: lowering a count by the surplus would free an object early if
#                                  a load holder ever released its reference; none seen, and the refwatch can check.
#   native_gate_join_trdn          a container's next trade offer update (j1-199: 639 equal; w1z34: the window equal at 660)
#   native_gate_join_passes_admit  the joiner's pass of T at its admission (lockstep_join_passes.h; every in-window run since w1z6)
#   native_gate_join_pctl          X4's own PlayerController's values at T (lockstep_join_pctl.h; j1-196 neutral, the window's slot 1)
#   native_gate_join_ecos          the containers' economy schedules, a due time (w1z34 with it against w1z35 without: 900 30 / 36)
#   native_gate_join_jobg          the jobs' own generator words (j1-214: 900 0 from 42; w1z37: the window equal 660..780, 900 4)
#   native_gate_join_engr          the engines' boost ramps (w1z49: the window equal 660..960, 900 0 from w1z37's 4)
#   native_gate_join_shape_refresh every client refreshes the fixture body shapes at T+2 (j1-250: 1011 -> 1020; w1z50: the
#                                  window equal 660..1020, the motion at 1080 gone)
# Off by default: SHPW and specdrop (negative), PEND and VIEWL (no gain measured).
JOIN_DEFAULT_ON = ('native_gate_join_rcnt', 'native_gate_join_trdn', 'native_gate_join_passes_admit', 'native_gate_join_pctl',
                   'native_gate_join_ecos', 'native_gate_join_jobg', 'native_gate_join_engr',
                   'native_gate_join_shape_refresh')


def default_sets(sets):
    """JOIN_DEFAULT_ON = true on savers and joiners unless a --set names the key."""
    named = {text.replace(' ', '').split('=', 1)[0] for text in sets}
    return sets+[f'{key}=true' for key in JOIN_DEFAULT_ON if key not in named]


def check_absent(clients, absent, rejoin=()):
    """The absent slots must be the last ones: the lane tool's clients are p1..pL in --lanes order. Without absent slots
    the session has rejoinable ones (2..N, not absent)."""
    if not 2 <= clients <= 4:
        raise ValueError('--clients 2..4 (the identity plan drives at most four slot ships)')
    absent, rejoin = sorted(set(absent)), sorted(set(rejoin))
    if rejoin and (rejoin[0] < 2 or rejoin[-1] > clients or set(rejoin) & set(absent)):
        raise ValueError(f'--rejoin needs slots 2..{clients}, none of them absent')
    lanes = clients-len(absent)
    if (not absent and not rejoin) or lanes < 1 or (absent and absent != list(range(lanes+1, clients+1))):
        raise ValueError(f'--absent must be the last slots ({lanes+1}..{clients} for {lanes} lane(s)): the lane tool gives its '
                         f'clients p1..p{max(lanes, 1)} in --lanes order')
    if lanes > 3:
        raise ValueError('the released lane tool runs 2 or 3 clients per session')
    return absent, lanes


def run_tool(argv, what):
    proc = subprocess.run([sys.executable, *map(str, argv)], capture_output=True, text=True)
    if proc.returncode:
        raise ValueError(f'{what} refused (exit {proc.returncode}): {(proc.stderr or proc.stdout).strip()[-1500:]}')
    return proc.stdout


def pin_clients(out, clients):
    """native_gate_clients = N in the overlay config (and nowhere a lane client count); the manifest is rewritten."""
    config = out/'ui/config.lua.append'
    text = config.read_text(encoding='utf-8')
    text = text.replace(CLIENTS_TOKEN, str(clients))
    values = [line.split('=', 1)[1].split('--')[0].strip() for line in text.splitlines()
              if line.startswith('X4LAN_CONFIG.native_gate_clients ') or line.startswith('X4LAN_CONFIG.native_gate_clients=')]
    if not values or any(v != str(clients) for v in values):
        raise ValueError(f'native_gate_clients is not {clients} in {config}: {values}')
    config.write_text(text, encoding='utf-8')
    manifest = {str(f.relative_to(out)).replace('\\', '/'): sha256(f)
                for f in sorted(out.rglob('*')) if f.is_file() and f.name != 'x4lane-manifest.json'}
    (out/'x4lane-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return text


def build(args, role, snapshot, base_extra, record):
    out = Path(args.out)
    base = out.with_name(out.name+'.base')
    for path in (out, base):
        if path.exists():
            raise ValueError(f'{path} exists; refusing to overwrite (a new directory keeps earlier inputs frozen)')
    run_tool([DEV/'build_native_gate_overlay.py', '--gate-dll', args.gate_dll, '--snapshot', snapshot, '--mode', 'battle',
              '--session-clients', '--world-identities', *base_extra, '--out', base], 'build_native_gate_overlay.py')
    sets = [f'native_gate_arm_timeout_ms={args.arm_timeout_ms}'] + default_sets(ids_sets(refresh_sets(precise_sets(census_sets(args)))))
    notes = run_tool([DEV/'native_gate_players.py', 'overlay', '--base', base, '--out', out, '--gate-dll', args.gate_dll,
                      '--probe', args.probe, '--players', str(record['clients']), '--mode', 'coop', '--source', 'stock',
                      '--defaults-only', *[a for s in sets for a in ('--set', s)]], 'native_gate_players.py overlay')
    text = pin_clients(out, record['clients'])
    record.update(role=role, snapshot=str(Path(snapshot).resolve()), snapshot_sha=sha256(snapshot), base=str(base.resolve()),
                  sets=sets, plan=next((line.split('plan ', 1)[1] for line in notes.splitlines() if 'plan ' in line and line.startswith('identity:')), None))
    (out/'x4lane-join.json').write_text(json.dumps(record, indent=1, sort_keys=True)+'\n')
    if f'native_gate_snapshot_sha = "{record["snapshot_sha"]}"' not in text:
        raise ValueError('the overlay does not boot the given snapshot')
    return out, record


def savers(args):
    absent, lanes = check_absent(args.clients, args.absent, args.rejoin)
    extra = (['--join-absent', ','.join(map(str, absent))] if absent else [])+(['--join-rejoin', ','.join(map(str, args.rejoin))] if args.rejoin else [])
    out, record = build(args, 'savers', args.snapshot, ['--join', args.join, *extra],
                        {'clients': args.clients, 'absent': absent, 'rejoin': sorted(args.rejoin), 'lanes': lanes, 'join': args.join})
    slots = ','.join(str(s) for s in range(1, lanes+1) if s not in args.rejoin)
    print(f'JOIN_OVERLAY role=savers out={out} clients={args.clients} absent={",".join(map(str, absent)) or "-"} '
          f'rejoin={",".join(map(str, sorted(args.rejoin))) or "-"} join={args.join} '
          f'census_t1={int(bool(args.join_census))} snapshot_sha={record["snapshot_sha"]} plan={record["plan"]}')
    print(f'lane session: --clients {lanes} --lanes <{lanes} lanes, slot order> --overlay {out} (native_gate_lane.py --snapshot-in {args.snapshot})')
    relay = (f'--absent {",".join(map(str, absent))} ' if absent else '')+(
        f'--rejoin {",".join(map(str, sorted(args.rejoin)))} --stall-drop-ms MS ' if args.rejoin else '')
    print(f'relay: --clients {args.clients} --join {relay}--join-savers {slots} (not --clients {{clients}}: '
          f'that is the lane count {lanes})')
    return 0


def wait_for(path, seconds):
    deadline, said = time.monotonic()+seconds, 0.0
    while not Path(path).is_file():
        if time.monotonic() >= deadline:
            raise ValueError(f'no handoff {path} after {seconds:g} s (JOIN_DRIVER FILES/HANDOFF in the session server.log?)')
        if time.monotonic()-said >= 30:
            print(f'JOIN_OVERLAY waiting for {path}', flush=True)
            said = time.monotonic()
        time.sleep(2)


def joiner(args):
    if args.handoff:
        if args.save or args.package or args.package_sha:
            raise ValueError('--handoff or --save/--package/--package-sha, not both')
        if args.wait:
            wait_for(args.handoff, args.wait)
        h = driver.load_handoff(args.handoff, verify=True)
        save, package, sha, clients, slot = Path(h['save']), Path(h['package']), h['package_sha'], h['clients'], h['slot']
        rejoin = h.get('rejoin') or []
        absent = h.get('absent') or ([] if rejoin else [slot])
    else:
        if not (args.save and args.package and args.package_sha):
            raise ValueError('joiner needs --handoff, or --save, --package and --package-sha')
        save, package, sha, clients, slot = Path(args.save), Path(args.package), args.package_sha.lower(), args.clients, None
        absent = [int(v) for v in args.absent.split(',') if v.strip()]
        rejoin = [int(v) for v in args.rejoin.split(',') if v.strip()]
    either = absent+rejoin
    if not either or min(either) < 2 or max(either) > clients or set(absent) & set(rejoin) or (slot is not None and slot not in either):
        raise ValueError(f'absent slots {absent}, rejoin slots {rejoin}: 2..{clients}, apart, the joining slot among them')
    if not str(save).endswith('.xml.gz'):
        raise ValueError('the join save must be a .xml.gz (the lane snapshot slot takes no other)')
    data = package.read_bytes()
    try:
        loaded = join.read_package(data)
    except join.JoinError as error:
        raise ValueError(f'join package refused: {error}') from None
    if loaded['sha'] != sha:
        raise ValueError('join package refused: package_sha_announced')
    if sha256(save) != loaded['header']['save_sha']:
        raise ValueError('join package refused: save_sha (the save is not the one the package names)')
    gate = loaded['sections']['gate']
    if gate['clients'] != clients:
        raise ValueError(f"join package refused: the package's session has {gate['clients']} slots, not {clients}")
    if loaded['header']['flags'] & join.FLAG_DIAGNOSTIC and args.join_mode == 'strict':
        raise ValueError('a diagnostic package (saved in report mode or with unaudited modules) refuses a strict joiner: '
                         '--join-mode report')
    world = check_world_table(loaded, args.join_mode)
    extra = (['--join-absent', ','.join(map(str, sorted(set(absent))))] if absent else [])+(
        ['--join-rejoin', ','.join(map(str, sorted(set(rejoin))))] if rejoin else [])
    out, record = build(args, 'joiner', save, ['--join-package', package, '--join-package-sha', sha, '--join-mode', args.join_mode,
                                               *extra],
                        {'clients': clients, 'slot': slot, 'absent': sorted(set(absent)), 'rejoin': sorted(set(rejoin)),
                         'save_tick': gate['tick'],
                         'saver': loaded['header']['slot'],
                         'package': str(package.resolve()), 'package_sha': sha, 'join_mode': args.join_mode,
                         'diagnostic': loaded['header']['flags'] & join.FLAG_DIAGNOSTIC, 'world': world})
    try:
        config_text = (out/'ui/config.lua.append').read_text(encoding='utf-8')
        checked = check_saved_ids(config_text, loaded)
        views = check_fixture_views(config_text, loaded)
        providers = check_lua_state(config_text, loaded)
    except ValueError:
        out.rename(out.with_name(out.name+'.refused'))
        raise
    record['saved_ids'], record['fixture_views'], record['lua_state'] = checked, views, providers
    (out/'x4lane-join.json').write_text(json.dumps(record, indent=1, sort_keys=True)+'\n')
    print(f'JOIN_OVERLAY saved_ids={sum(checked.values())} keys={",".join(sorted(checked)) or "-"} (every one a saved identity of the package)'
          f' fixture_views={views or "-"} lua_state={providers or "-"}')
    print(f'JOIN_OVERLAY role=joiner out={out} clients={clients} slot={slot or "-"} T={gate["tick"]} saver={loaded["header"]["slot"]} '
          f'mode={args.join_mode} diagnostic={record["diagnostic"]} world={world} census_t1={int(bool(args.join_census))} '
          f'snapshot_sha={record["snapshot_sha"]} plan={record["plan"]}')
    if args.handoff:
        print(f'lane run: python tools/dev/native_gate_lane.py --lane-tool <x4lane.py> --join-session {args.handoff} -- run '
              f'--lane <lane> --overlay {out} ...')
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument('--gate-dll', required=True, type=Path)
    common.add_argument('--out', required=True, type=Path)
    common.add_argument('--probe', type=Path, default=DEV/'native_gate_probe.lua')
    common.add_argument('--arm-timeout-ms', type=int, default=120000,
                        help='native_gate_arm_timeout_ms (1000..120000, default 120000): every client waits this long for a permit, '
                             'so the save pause at T and the final pause at H must stay below it')
    common.add_argument('--set', action='append', metavar='native_gate_KEY=LUA_VALUE',
                        help='census keys for native_gate_players.py (repeatable); give the joiner the same keys as the savers')
    common.add_argument('--join-census', action='store_true',
                        help='native_gate_world_hash_join: one more world capture at T+1 on the session and the joiner (give it to both)')
    s = sub.add_parser('savers', parents=[common], help='the session overlay of the present clients')
    s.add_argument('--snapshot', required=True, type=Path, help='the owned battle snapshot (save_009.xml.gz)')
    s.add_argument('--clients', type=int, default=3, help='session slots N (default 3)')
    s.add_argument('--absent', default='3', help="absent slots, the last ones (default 3; '' for none with --rejoin)")
    s.add_argument('--rejoin', default='', help='rejoinable slots: played from the start, dropped and joined again (roadmap 15)')
    s.add_argument('--join', choices=('report', 'strict'), default='report',
                   help='report (J1): save with unaudited modules armed, the package marked diagnostic; strict: refuse that save')
    j = sub.add_parser('joiner', parents=[common], help='the overlay of the joining slot')
    j.add_argument('--handoff', type=Path, help="the session driver's handoff.json (or the replay's)")
    j.add_argument('--wait', type=float, default=0, help='seconds to wait for --handoff to appear (the session runs)')
    j.add_argument('--save', type=Path)
    j.add_argument('--package', type=Path)
    j.add_argument('--package-sha')
    j.add_argument('--clients', type=int, default=3, help='with --save/--package: session slots N (default 3)')
    j.add_argument('--absent', default='3', help="with --save/--package: the session's absent slots, the joining one among them "
                                                 '(default 3; a handoff names them itself)')
    j.add_argument('--rejoin', default='', help="with --save/--package: the session's rejoinable slots (a handoff names them itself)")
    j.add_argument('--join-mode', choices=('strict', 'report'), default='strict')
    args = p.parse_args(argv)
    if not 1000 <= args.arm_timeout_ms <= 120000:
        p.error('--arm-timeout-ms must be 1000..120000 (native_gate_arm_timeout_ms)')
    args.gate_dll, args.out, args.probe = args.gate_dll.resolve(), args.out.resolve(), args.probe.resolve()
    try:
        if args.command == 'savers':
            args.absent = [int(v) for v in args.absent.split(',') if v.strip()]
            args.rejoin = [int(v) for v in args.rejoin.split(',') if v.strip()]
            return savers(args)
        return joiner(args)
    except (ValueError, OSError, driver.HandoffError) as error:
        p.error(str(error))


if __name__ == '__main__':
    sys.exit(main())
