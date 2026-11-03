"""Python mirror of the lockstep session defaults (tools/dev/native_gate_defaults.lua, the one source of truth).

The Lua file holds the table; this module parses it (one entry per line) and applies it to a config text the way
D.apply does in the game, for the offline tools: check_gate_options.py validates the effective stack of a session
config, native_gate_players.py writes only the lines that differ from it. reports/LOCKSTEP-DEFAULTS-20261101.txt.

  python tools/dev/native_gate_defaults.py table            # key, default, needs, why
  python tools/dev/native_gate_defaults.py effective CONFIG # the config with its defaults applied, then the report
"""
import argparse
import re
import sys
from pathlib import Path

LUA = Path(__file__).resolve().parent/'native_gate_defaults.lua'
ENTRY = re.compile(r"^\s*\{'(?P<key>native_gate_[a-z0-9_]+)',(?P<value>.*),'(?P<needs>[^']*)','(?P<why>[^']*)'\},\s*$")
PAIR = re.compile(r"^\s*\{'(?P<key>native_gate_[a-z0-9_]+)','(?P<why>[^']*)'\},\s*$")
LINE = re.compile(r'^X4LAN_CONFIG\.([a-z0-9_]+)\s*=\s*(.*?)\s*$')
PLACEHOLDER = re.compile(r'^"?@X4LANE_[A-Z]+@"?$')


def _block(text, name):
    start = text.index(f'\nD.{name}={{\n')
    end = text.index('\n}\n', start)
    return text[start:end].splitlines()[2:]


def load(path=LUA):
    """The table: version, ON entries [(key, value text, needs tokens, why)], DIAGNOSTIC and EXCLUDED [(key, why)],
    SESSION keys, the UI replay families and what each needs, the conflict notes."""
    text = Path(path).read_text(encoding='utf-8')
    on, diagnostic, excluded, tuning = [], [], [], []
    for line in _block(text, 'ON'):
        if line.strip().startswith('--'):
            continue
        m = ENTRY.match(line)
        if not m:
            raise ValueError(f'native_gate_defaults.lua: unparsable D.ON line: {line}')
        on.append((m['key'], m['value'], tuple(m['needs'].split()), m['why']))
    for name, out in (('DIAGNOSTIC', diagnostic), ('EXCLUDED', excluded), ('TUNING', tuning)):
        for line in _block(text, name):
            m = PAIR.match(line)
            if not m:
                raise ValueError(f'native_gate_defaults.lua: unparsable D.{name} line: {line}')
            out.append((m['key'], m['why']))
    session = re.findall(r"'(native_gate_[a-z0-9_]+)'", text[text.index('D.SESSION={'):text.index('}', text.index('D.SESSION={'))])
    families = re.findall(r"'([a-z]+)'", text[text.index('D.FAMILIES={'):text.index('}', text.index('D.FAMILIES={'))])
    needs = dict(re.findall(r"([a-z]+)='([a-z_:]+)'", text[text.index('D.FAMILY_NEEDS={'):text.index('}', text.index('D.FAMILY_NEEDS={'))]))
    conflicts = dict(re.findall(r"^ (native_gate_[a-z0-9_]+)='([^']*)',$", text[text.index('D.CONFLICTS={'):text.index('\n}\n', text.index('D.CONFLICTS={'))],
                                re.M))
    version = int(re.search(r'local D=\{VERSION=(\d+)\}', text)[1])
    return dict(version=version, on=on, diagnostic=diagnostic, excluded=excluded, tuning=tuning, session=session, families=families,
                family_needs=needs, conflicts=conflicts)


# What a session overlay ships for the default stack (the overlay builders copy these from tools/dev): the defaults module
# first in ui.xml.files (the adapters read X4LAN_CONFIG while loading), the adapters and cores before the probe, the MD.
UI_FILES = ('ui/native_gate_defaults.lua', 'ui/native_gate_inputs_core.lua', 'ui/native_gate_player_identity_core.lua',
            'ui/native_gate_player_identity.lua', 'ui/native_gate_ui_replay_core.lua', 'ui/native_gate_ui_build_core.lua',
            'ui/native_gate_fleet.lua', 'ui/native_gate_orders.lua')
MD_FILES = ('md/native_gate_battle.xml', 'md/native_gate_player_identity.xml', 'md/native_gate_ui_replay.xml',
            'md/native_gate_pilot_respawn.xml')

TABLE = load()
ON_KEYS = [e[0] for e in TABLE['on']]
DIAGNOSTIC_KEYS = [k for k, _ in TABLE['diagnostic']]
EXCLUDED_KEYS = [k for k, _ in TABLE['excluded']]
TUNING_KEYS = [k for k, _ in TABLE['tuning']]


def ship_files(out, ui_files):
    """Copies the default stack's files into an overlay directory and returns ui.xml.files' new order: the defaults
    module first, then the default adapters, then the overlay's own entries (each once)."""
    source = LUA.parent
    for name in UI_FILES + MD_FILES:
        (out/name).parent.mkdir(parents=True, exist_ok=True)
        (out/name).write_bytes((source/Path(name).name).read_bytes())
    rest = [name for name in ui_files if name not in UI_FILES]
    return list(UI_FILES) + rest


def lua_literal(text):
    """A Lua constant (nil, booleans, numbers, strings, tables with [\"k\"]=, k= and positional items) as Python
    (None, bool, int/float, str, list or dict); placeholders such as @X4LANE_CLIENTS@ stay strings."""
    text = text.strip()
    pos = 0

    def ws():
        nonlocal pos
        while pos < len(text) and text[pos] in ' \t\r\n':
            pos += 1

    def value():
        nonlocal pos
        ws()
        c = text[pos]
        if c in '\'"':
            end = pos + 1
            out = []
            while text[end] != c:
                if text[end] == '\\':
                    end += 1
                out.append(text[end])
                end += 1
            pos = end + 1
            return ''.join(out)
        if c == '{':
            pos += 1
            items, named = [], {}
            while True:
                ws()
                if text[pos] == '}':
                    pos += 1
                    break
                if text[pos] == '[':
                    pos += 1
                    key = value()
                    ws()
                    assert text[pos] == ']'
                    pos += 1
                    ws()
                    assert text[pos] == '='
                    pos += 1
                    named[key] = value()
                else:
                    m = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)\s*=(?!=)').match(text, pos)
                    if m and m[1] not in ('true', 'false', 'nil'):
                        pos = m.end()
                        named[m[1]] = value()
                    else:
                        items.append(value())
                ws()
                if text[pos] in ',;':
                    pos += 1
            if named and items:
                named.update({i + 1: v for i, v in enumerate(items)})
                return named
            return named if named else items
        m = re.compile(r'-?(?:0x[0-9a-fA-F]+|\d+(?:\.\d*)?(?:[eE][-+]?\d+)?)|true|false|nil|@[A-Z_]+@').match(text, pos)
        if not m:
            raise ValueError(f'not a Lua constant: {text}')
        pos = m.end()
        word = m[0]
        if word in ('true', 'false'):
            return word == 'true'
        if word == 'nil':
            return None
        if word.startswith('@'):
            return word
        return int(word, 0) if re.fullmatch(r'-?(?:0x[0-9a-fA-F]+|\d+)', word) else float(word)

    result = value()
    ws()
    if pos != len(text):
        raise ValueError(f'trailing text after a Lua constant: {text}')
    return result


def lua_text(v):
    """Python -> Lua constant text (strings single-quoted, lists in order)."""
    if v is None:
        return 'nil'
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, (int, float)):
        return str(v)
    if isinstance(v, str):
        return "'" + v.replace('\\', '\\\\').replace("'", "\\'") + "'"
    if isinstance(v, list):
        return '{' + ','.join(lua_text(x) for x in v) + '}'
    raise TypeError(v)


def strip_comment(value):
    """The value text without a trailing Lua comment (outside strings)."""
    quote = None
    for i, c in enumerate(value):
        if quote:
            if c == quote:
                quote = None
        elif c in '\'"':
            quote = c
        elif value.startswith('--', i):
            return value[:i].strip()
    return value.strip()


def parse(text):
    """key -> value text, last assignment wins (as Lua runs the file)."""
    values = {}
    for line in text.splitlines():
        m = LINE.match(line)
        if m:
            values[m[1]] = strip_comment(m[2])
    return values


def on(v):
    return v is not None and v not in ('nil', 'false')


def bare(v):
    return v.strip('\'"') if isinstance(v, str) else v


def modes(v):
    """A list value's items (stripped of quotes), or the single value."""
    if v is None:
        return []
    if v.startswith('{') and v.endswith('}'):
        return [m.strip().strip('\'"') for m in v[1:-1].split(',') if m.strip()]
    return [bare(v)]


def is_session(values):
    """native_gate_test with a slot pN, a port and a token (lane placeholders count: they are filled per client)."""
    slot, port, token = values.get('slot'), values.get('port'), values.get('token')
    return (on(values.get('native_gate_test')) and slot is not None and (re.fullmatch(r'[\'"]p\d+[\'"]', slot) or PLACEHOLDER.match(slot))
            and port is not None and token is not None and bare(token) != '')


def _conditions(values):
    clients = values.get('native_gate_clients')
    return {'@wire3': (clients is not None and bool(re.fullmatch(r'[2-8]|@X4LANE_CLIENTS@', clients)),
                       'needs the protocol 3 input wire (native_gate_clients)'),
            '@plan': ((values.get('native_gate_player_identity_plan') or '').startswith('{'), 'needs native_gate_player_identity_plan'),
            '@fixed': (bare(values.get('native_gate_lookahead') or '') != 'adaptive',
                       'protocol 5 (native_gate_lookahead=adaptive) excludes it')}


def unmet(values, needs):
    """None when every token holds, else the first reason (as D.unmet)."""
    conditions = _conditions(values)
    for token in needs:
        if token in conditions:
            if not conditions[token][0]:
                return conditions[token][1]
        elif token.startswith('!'):
            key = token[1:]
            if on(values.get(key)):
                note = TABLE['conflicts'].get(key)
                return f'excluded by {key} (on)' + (f': {note}' if note else '')
        else:
            m = re.fullmatch(r'([a-z0-9_]+)([=~]?)(.*)', token)
            key, op, want = m[1], m[2], m[3]
            v = values.get(key)
            if not on(v):
                return f'needs {key} (off)'
            if op == '=' and bare(v) != want:
                return f'needs {key}={want} (is {bare(v)})'
            if op == '~' and want not in modes(v):
                return f'needs {key} with {want}'
    return None


def plan_ships(values):
    text = values.get('native_gate_player_identity_plan')
    try:
        plan = lua_literal(text) if text else None
    except (ValueError, AssertionError, IndexError):
        return None, 'native_gate_player_identity_plan is not a constant table'
    slots = plan.get('slots') if isinstance(plan, dict) else None
    if not isinstance(slots, list) or not slots:
        return None, 'needs native_gate_player_identity_plan with slots'
    ships = []
    for k, slot in enumerate(slots, 1):
        ship = slot.get('ship') if isinstance(slot, dict) else None
        if not isinstance(ship, str) or not re.fullmatch(r'[0-9a-f]{16}', ship):
            return None, f'plan slot {k} has no saved ship (created ships cannot be piloted yet)'
        ships.append(ship)
    return ships, None


def derive(name, values, off, drop):
    """D.DERIVE: the derived default as Lua text, or (None, why)."""
    if name in ('@plan_ships', '@plan_stock', '@plan_nodevice'):
        ships, why = plan_ships(values)
        if not ships:
            return None, why
        return lua_text({'@plan_ships': ships, '@plan_stock': ['stock'] * len(ships), '@plan_nodevice': [False] * len(ships)}[name]), None
    if name == '@plan_window':
        try:
            plan = lua_literal(values.get('native_gate_player_identity_plan') or '{}')
        except (ValueError, AssertionError, IndexError):
            plan = {}
        tick = plan.get('tick') if isinstance(plan, dict) and isinstance(plan.get('tick'), int) else 20
        return lua_text([max(40, tick + 16), 72000]), None
    if name == '@families':
        guard = values.get('native_gate_ui_guard')
        groups = set(modes(guard)) if guard and guard.startswith('{') else None
        out = []
        for family in TABLE['families']:
            need, why = TABLE['family_needs'].get(family), None
            if need == 'native_gate_pilot_fire' and not on(values.get('native_gate_pilot_fire')):
                why = 'needs native_gate_pilot_fire (off)'
            elif need and need.startswith('guard'):
                if 'native_gate_ui_guard' in off:
                    why = 'needs native_gate_ui_guard (off)'
                elif groups is not None and need != 'guard' and need[6:] not in groups:
                    why = f'needs native_gate_ui_guard with the {need[6:]} group'
            if why:
                drop('native_gate_ui_replay:' + family, why, None)
            else:
                out.append(family)
        return lua_text(out), None
    raise ValueError(f'unknown derived default {name}')


def apply(values):
    """D.apply on parsed values (a new dict is returned). Report: bare, applied, explicit, off, skipped [(key, why, feature)],
    errors, and the effective values."""
    values = dict(values)
    report = dict(bare=False, applied=[], explicit=[], off=[], skipped=[], errors=[], session=False)
    if not on(values.get('native_gate_test')):
        return values, None
    if values.get('native_gate_defaults') == 'false':
        report['bare'] = True
        return values, report
    if not is_session(values):
        report['errors'].append('native_gate_test without a lockstep session: slot pN, port and token are required')
        return values, report
    report['session'] = True
    for key in ON_KEYS:
        if values.get(key) in ('false', "'off'", '"off"'):
            values.pop(key)
            report['off'].append(key)
    off = set(report['off'])

    def drop(key, why, feature):
        report['skipped'].append((key, why, feature))
    for key, value, needs, why_on in TABLE['on']:
        if key in off:
            continue
        if values.get(key) not in (None, 'nil'):
            report['explicit'].append(key)
            continue
        why = unmet(values, needs)
        literal = value
        if not why and value.startswith("'@"):
            name = value.strip("'")
            if name == '@credential':
                why = ('no credential in the session config (native_gate_wire_crypto = x4c1:...; relay --wire-crypto): the relay path is '
                       'plain UDP with the shared token')
            elif name == '@configured':
                why = 'not configured (an opt-in of every PC and the relay)'
            else:
                literal, why = derive(name, values, off, drop)
        if why:
            drop(key, why, why_on)
        else:
            values[key] = literal
            report['applied'].append(key)
    if on(values.get('native_gate_wire_crypto')) and bare(values.get('native_gate_lookahead') or '') == 'adaptive':
        report['errors'].append('native_gate_wire_crypto covers protocols 3 and 4; native_gate_lookahead=adaptive is protocol 5 (the relay refuses both)')
    if on(values.get('native_gate_wire_crypto')) and on(values.get('native_gate_peer_window')):
        report['errors'].append('native_gate_peer_window with native_gate_wire_crypto: ' + TABLE['conflicts']['native_gate_wire_crypto'])
    return values, report


def effective(config_text):
    """(effective config text, report): explicit 'off' lines become false, every applied default is appended. A config
    that is no session, or the bare gate (native_gate_defaults = false), comes back unchanged."""
    values, report = apply(parse(config_text))
    if not report or report['bare'] or not report['session']:
        return config_text, report
    lines = []
    off = set(report['off'])
    for line in config_text.splitlines():
        m = LINE.match(line)
        if m and m[1] in off:
            line = f'X4LAN_CONFIG.{m[1]} = false  -- explicitly off'
        lines.append(line)
    lines.append(f'-- session defaults (tools/dev/native_gate_defaults.lua, version {TABLE["version"]})')
    lines += [f'X4LAN_CONFIG.{key} = {values[key]}' for key in report['applied']]
    return '\n'.join(lines) + '\n', report


def same(a, b):
    """Two Lua value texts denote the same constant."""
    try:
        return lua_literal(a) == lua_literal(b)
    except (ValueError, AssertionError, IndexError):
        return a.strip() == b.strip()


def overrides(lines, base_text=''):
    """The lines (X4LAN_CONFIG.k = v) of a session block that differ from what the defaults would give with the base
    and the rest of the block: a line equal to its default is dropped. Comments and non-gate lines stay."""
    parsed = [(line, LINE.match(line)) for line in lines]
    context = parse(base_text)
    for _, m in parsed:
        if m:
            context[m[1]] = strip_comment(m[2])
    if not is_session(context) or context.get('native_gate_defaults') == 'false':
        return list(lines)
    kept = []
    for line, m in parsed:
        if m and m[1] in ON_KEYS:
            without = {k: v for k, v in context.items() if k != m[1]}
            values, report = apply(without)
            if report and m[1] in report['applied'] and same(values[m[1]], strip_comment(m[2])):
                continue
        kept.append(line)
    return kept


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    sub.add_parser('table', help='print the defaults table')
    e = sub.add_parser('effective', help='apply the defaults to a config and print it')
    e.add_argument('config', type=Path)
    args = p.parse_args(argv)
    if args.command == 'table':
        for key, value, needs, why in TABLE['on']:
            print(f'ON   {key} = {value}  needs: {" ".join(needs) or "-"}  ({why})')
        for key, why in TABLE['diagnostic']:
            print(f'DIAG {key} off ({why})')
        for key, why in TABLE['excluded']:
            print(f'EXCL {key} off ({why})')
        for key, why in TABLE['tuning']:
            print(f'TUNE {key} ({why})')
        return 0
    text, report = effective(args.config.read_text(encoding='utf-8'))
    print(text, end='')
    if report:
        for key, why, _ in report['skipped']:
            print(f'-- DEFAULT_SKIPPED {key}: {why}')
        for why in report['errors']:
            print(f'-- DEFAULTS_REFUSED {why}')
    return 1 if report and report['errors'] else 0


if __name__ == '__main__':
    sys.exit(main())
