"""Default-off coefficient topology observation; preserves stock return values."""
import hashlib
from pathlib import Path
import re

import linux_ui_camera_refill_bundle as previous

SOURCE_VERSION = 4
ACTIVE = 'X4 STOCK VIEW FOV PRELOAD ACTIVE version=1 mode=observe\n'
REWRITE_ACTIVE = 'X4 STOCK VIEW FOV PRELOAD ACTIVE version=1 mode=rewrite\n'
PREFIX = 'X4 STOCK_VIEW_FOV_OBSERVE '
HEADERS = ('gate_player_stock_view_fov.h', 'player_stock_view_state.h',
           'player_stock_view_fov_preload.h', 'player_stock_view_fov_preload_impl.h',
           'player_stock_view_fov_preload_pin.h', 'player_stock_view_fov_cfi.h',
           'player_stock_view_ship_preload.h', 'player_stock_view_ship_preload_impl.h',
           'player_stock_view_ship_preload_pin.h')
SOURCES = tuple('src/native/engine/linux/' + name for name in HEADERS) + (
    'src/native/engine/linux/player_stock_view_fov_bridge.S',
    'src/native/engine/linux/player_stock_view_ship_bridge.S',
    'tools/dev/build_player_view_query_preload.py',
    'tools/dev/linux_stock_view_fov_observer.py')


def enabled(options):
    value = options.get('stock_view_fov_observer', False)
    if type(value) is not bool:
        raise ValueError('stock VIEW FOV observer must be boolean')
    if value and (options.get('player_startup_phases') != 'v1-t40' or
                  type(options.get('player_ui_session_frames')) is not int or
                  not 180 <= options['player_ui_session_frames'] <= 900 or
                  options.get('player_view_query_observer') is not True or
                  options.get('independent_keyboard_frames') != options['player_ui_session_frames']):
        raise ValueError('stock VIEW FOV observer requires T40 finite independent physics and sealed view-query preload')
    return value


def environment(env, active):
    result = dict(env)
    result.pop('X4_DEV_STOCK_VIEW_FOV', None)
    result.pop('X4_DEV_STOCK_VIEW_COMPONENT_POLICY', None)
    result.pop('X4_DEV_VIEW_XS', None)
    result.pop('X4_DEV_STOCK_VIEW_FOV_OBSERVE', None)
    result.pop('X4_DEV_RENDER_FOV_OBSERVE', None)
    result.pop('X4_DEV_RENDER_FOV_CAPTURE', None)
    if active:
        result['X4_DEV_STOCK_VIEW_FOV_OBSERVE'] = '1'
    return result


def source_paths(repo):
    paths = [Path(repo) / name for name in SOURCES]
    if any(path.is_symlink() or not path.is_file() for path in paths):
        raise ValueError('stock VIEW observer source closure incomplete')
    return paths


def validate_bundle(root, manifest, network):
    active = enabled(manifest)
    version = manifest.get('view_query_source_version')
    evidence = manifest.get('stock_view_fov_observer_evidence_version')
    if (active and (type(evidence) is not int or evidence != 1)) or (not active and evidence is not None):
        raise ValueError('stock VIEW observer evidence version mismatch')
    if type(version) is not int or version != SOURCE_VERSION:
        if active:
            raise ValueError('stock VIEW observer requires source version 4')
        return previous.validate_bundle(root, manifest, network)
    for name in SOURCES:
        key = 'source-receipts/' + name
        path = Path(root) / key
        if (path.is_symlink() or not path.is_file() or
                manifest.get('files', {}).get(key) != hashlib.sha256(path.read_bytes()).hexdigest()):
            raise ValueError('missing or changed stock VIEW source: ' + name)
    if active and network is not True:
        raise ValueError('stock VIEW observer requires disposable independent network physics')
    return previous.validate_bundle(root, dict(manifest, view_query_source_version=3), network)


def collect(stderr, trace, active, physics, *, rewrite=False):
    paths = {'stderr_sha256': Path(stderr), 'native_fd_sha256': Path(trace)}
    hashes, data, errors = {}, {}, []
    for key, path in paths.items():
        try:
            raw = path.read_bytes()
            hashes[key] = hashlib.sha256(raw).hexdigest()
            data[key] = raw.decode('utf-8', errors='strict').splitlines(keepends=True)
        except (OSError, UnicodeError) as error:
            if active or rewrite or key == 'stderr_sha256':
                errors.append('stock VIEW input unavailable: ' + str(error))
    lines = data.get('stderr_sha256', [])
    rows = [(i, row) for i, row in enumerate(lines) if 'X4 STOCK_VIEW_FOV_OBSERVE' in row]
    activations = [i for i, row in enumerate(lines) if row == ACTIVE]
    preload_rows = [row for row in lines if 'STOCK VIEW FOV PRELOAD' in row]
    result = dict(hashes, enabled=active, performed=False, errors=errors,
                  raw_receipts=[row for _, row in rows], reader_exclusion_proven=False,
                  foreign_calls='lower_bound', stock_values_rewritten=False, readiness_proven=False,
                  rewrite_mode_admitted=False, rewrite_effects_proved=False)
    if type(rewrite) is not bool or (active and rewrite):
        errors.append('invalid simultaneous stock VIEW observe/rewrite collection mode')
    if not active:
        if rewrite is True:
            if rows or activations or preload_rows != [REWRITE_ACTIVE]:
                errors.append('unexpected or missing admitted stock VIEW rewrite activation')
            if physics.get('pass') is not True:
                errors.append('stock VIEW rewrite activation lacks accepted physics evidence')
            for key, value in hashes.items():
                expected = physics.get('log_sha256' if key == 'native_fd_sha256' else key)
                if expected != value:
                    errors.append('stock VIEW rewrite proof hash mismatch: ' + key)
            result['rewrite_mode_admitted'] = not errors
        elif rows or activations or preload_rows:
            errors.append('unexpected disabled stock VIEW observer evidence')
        result['pass'] = not errors
        return result
    if physics.get('pass') is not True:
        errors.append('stock VIEW observation lacks accepted physics evidence')
    for key, value in hashes.items():
        expected = physics.get('log_sha256' if key == 'native_fd_sha256' else key)
        if expected != value:
            errors.append('stock VIEW observation proof hash mismatch: ' + key)
    frames = physics.get('frames', [])
    last_frame = frames[-1] if frames else physics.get('last_frame', {})
    final_tick = last_frame.get('tick')
    if type(final_tick) is not int:
        errors.append('stock VIEW observation lacks the accepted final physics tick')
    if (sum('STOCK VIEW FOV PRELOAD' in row for row in lines) != 1 or
            len(activations) != 1 or len(rows) != 10 or not rows or not activations[0] < rows[0][0]):
        errors.append('missing, repeated or misordered stock VIEW activation/receipts')
        result['pass'] = False
        return result
    counts = []
    number = r'(0|[1-9][0-9]{0,19})'
    for ordinal, (_, row) in enumerate(rows[:-1]):
        grammar = (PREFIX + f'v=1 site={ordinal // 3} slot={ordinal % 3 + 1}' +
                   ''.join(' ' + key + '=' + number for key in
                           ('owner_outside', 'owner_parked', 'owner_scoped', 'foreign_lower')) + '\n')
        match = re.fullmatch(grammar, row)
        if not match or any(int(value) > 0xffffffffffffffff for value in match.groups()):
            errors.append('malformed or reordered stock VIEW counter row')
        else:
            counts.append(dict(zip(('owner_outside', 'owner_parked', 'owner_scoped', 'foreign_lower'), map(int, match.groups()))))
    close = f'{PREFIX}v=1 stage=close tick={final_tick} coverage=bounded foreign=lower_bound unpatched=0\n'
    release = [i for i, row in enumerate(lines) if row == f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={final_tick} tail=1 event=enter\n']
    if rows[-1][1] != close or len(release) != 1 or not release[0] < rows[0][0]:
        errors.append('stock VIEW final worker-tail close invalid')
    terminal = data.get('native_fd_sha256', [])
    if not terminal or terminal[-1] != 'X4LAN LIFECYCLE_DISCARD result=pass reason=physics_frames_returned exit=223 restored=0 resumed=0\n':
        errors.append('stock VIEW lacks final native discard receipt')
    result.update(counts=counts, performed=not errors, **{'pass': not errors})
    return result
