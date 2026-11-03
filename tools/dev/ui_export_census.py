"""Static census of the UI's FFI writes: which X4.exe exports each UI script calls, and their family.

  python tools/dev/ui_export_census.py [--game DIR] [--out .local/ui-export-census.json]

Reads the installed game read-only: the export table of X4.exe and the UI Lua inside the .cat/.dat
archives (unpacked to a temporary directory that is removed afterwards; nothing of the game is
written to the checkout). Prints, per family, how many non-query exports the UI calls and which
scripts call them, and writes the full per-export map (names, families, calling scripts) to --out
under .local/. Families come from the name (FAMILIES): 'presentation' exports change only what this
client shows; everything else is a candidate for the relay (docs/PLAYERS.md stage A). The runtime
census (native_gate_ui_exports = 'observe') confirms what the menus actually call in play.
"""
import argparse
import collections
import json
from pathlib import Path
import re
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src'))
QUERY = re.compile(r'^(Get|Is|Has|Can|Are|Was|Does|Find|Count|Check|Query|Compare|Convert|Should|Will|Read)(?![a-z])')
FAMILIES = [  # first match wins
    ('presentation', r'Widget|Table|EditBox|Slider|Frame|Cell|Button|Graph|Icon|Font|Color|Clipboard|Browser|Cursor|Scroll|'
                     r'Flow|Chart|Menu|Map(?!Editor)|Holo|Camera|Option|Setting|Sound|Music|Video|Voice|Subtitle|Hud|HUD|'
                     r'Tooltip|Highlight|Notification|Monitor|Render|Graphic|Screen|Window|Joystick|Mouse|Key|Input|'
                     r'Gamepad|Language|Help|Tutorial|Show|Fade|Compass|Message|Encyclopedia|Log|Indicator|Crosshair|'
                     r'UI|Startmenu|StartAnimation|Promo|Glyph|DirectInput|Radar|Outline|Fill|Detailmonitor|Cutscene|'
                     r'Track|Head|Gpu|GPU|FrameRate|Error|Assert|Cheat|Debug|Profile|Save|Load|Quit|Online|Steam|'
                     r'Achievement|Stat|Export|Import|UserData|CustomGameStart|NewGame|Multiplayer|Venture|Extension|Scenario'),
    ('orders', r'Order|Assign|Fleet|Subordinate|Command|Behaviou?r|Formation|Waypoint|Autopilot|AutoPilot|Stance|Response'),
    ('trade', r'Trade|Ware|Buy|Sell|Price|Cargo|Storage|Inventory|Drop'),
    ('money', r'Money|Credit|Account|Budget|Pay|Transfer'),
    ('build', r'Build|Construction|Module|Plan|Station|Blueprint|Dismantle|Recycl|Plot'),
    ('ships', r'Ship|Loadout|Equipment|Weapon|Turret|Shield|Engine|Paint|Crew|Npc|NPC|Pilot|Drone|Deploy|Launch|Ammo|Mod\b|Mod[A-Z]'),
    ('missions', r'Mission|Guidance|Faction|Relation|Reputation|Diplomacy|Notoriety|Offer|Boarding|Research|Terraform'),
    ('player', r'Player|Dock|Undock|Boost|Travel|Target|Select|Interact|Transport|Walk|Teleport|Jump|Hack'),
]


def exports(exe):
    data = exe.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    opt = pe + 24
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    osz = struct.unpack_from('<H', data, pe + 20)[0]
    secs = [struct.unpack_from('<IIII', data, opt + osz + 40 * i + 8) for i in range(nsec)]

    def off(rva):
        for vs, va, rs, raw in secs:
            if va <= rva < va + max(vs, rs):
                return raw + rva - va
        raise ValueError(hex(rva))
    e = off(struct.unpack_from('<I', data, opt + 112)[0])
    _, nn, _, an, _ = struct.unpack_from('<IIIII', data, e + 20)
    names = []
    for i in range(nn):
        nr = struct.unpack_from('<I', data, off(an) + 4 * i)[0]
        names.append(data[off(nr):off(nr) + 128].split(b'\0')[0].decode())
    return names


def ui_lua(game, into):
    """Unpack ui/**/*.lua of every archive into `into`; the later archive wins for the same path."""
    count = 0
    for cat in sorted(game.glob('*.cat')) + sorted(game.glob('extensions/*/*.cat')):
        if cat.name.endswith('_sig.cat') or not cat.with_suffix('.dat').exists():
            continue
        offset = 0
        with cat.open(encoding='utf-8', errors='replace') as index, cat.with_suffix('.dat').open('rb') as dat:
            for line in index:
                parts = line.rstrip('\n').rsplit(' ', 3)
                if len(parts) != 4:
                    continue
                path, size = parts[0], int(parts[1])
                if path.startswith('ui/') and path.endswith('.lua'):
                    dat.seek(offset)
                    target = into.joinpath(*path.split('/'))
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(dat.read(size))
                    count += 1
                offset += size
    return count


# FFI argument types -> relay kind letters (lockstep_ui_exports.h); anything else (structs, pointers) has none.
KIND = {'UniverseID': 'U', 'MissionID': 'q', 'BuildTaskID': 'q', 'TradeID': 'q', 'FleetUnitID': 'q', 'NPCSeed': 'q',
        'uint64_t': 'q', 'int64_t': 'q', 'int': 'i', 'int32_t': 'i', 'uint32_t': 'i', 'bool': 'b', 'float': 'f',
        'double': 'd', 'const char*': 's'}


def declarations(lua_texts):
    """{function: argument list text} from the UI scripts' ffi.cdef blocks."""
    out = {}
    for text in lua_texts:
        for block in re.finditer(r'ffi\.cdef\s*\[(=*)\[(.*?)\]\1\]', text, re.S):
            for m in re.finditer(r'[A-Za-z_][\w \*]*?\b([A-Z]\w*)\s*\(([^;{}]*?)\)\s*;', block.group(2)):
                out.setdefault(m.group(1), m.group(2).strip())
    return out


def kinds(arguments):
    """The relay kind string of an argument list, or None when an argument has no kind."""
    if arguments in ('', 'void'):
        return ''
    letters = []
    for arg in arguments.split(','):
        arg = re.sub(r'\s+', ' ', arg.strip())
        arg = re.sub(r'\s*\*\s*', '* ', arg).strip()
        name = arg.rsplit(' ', 1)[0].strip() if ' ' in arg else arg
        letter = KIND.get(name.replace('* ', '*'))
        if letter is None:
            return None
        letters.append(letter)
    return ''.join(letters)


def family(name):
    for fam, pattern in FAMILIES:
        if re.search(pattern, name):
            return fam
    return 'other'


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--game', type=Path)
    p.add_argument('--out', type=Path, default=ROOT / '.local/ui-export-census.json')
    args = p.parse_args(argv)
    if not args.game:
        from lockstep.launcher import game as x4game
        args.game = x4game.find_install()
    writes = [n for n in exports(args.game / 'X4.exe') if not QUERY.match(n)]
    hooked = set(writes)
    callers = collections.defaultdict(set)
    texts = []
    with tempfile.TemporaryDirectory() as tmp:
        files = ui_lua(args.game, Path(tmp))
        for lua in Path(tmp).rglob('*.lua'):
            text = lua.read_text(encoding='utf-8', errors='replace')
            texts.append(text)
            for m in re.finditer(r'\bC\.([A-Za-z_]\w*)\s*\(', text):
                if m.group(1) in hooked:
                    callers[m.group(1)].add(lua.name)
    decl = declarations(texts)
    table = {n: {'family': family(n), 'scripts': sorted(callers.get(n, ())), 'kinds': kinds(decl[n]) if n in decl else None}
             for n in sorted(hooked)}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(table, indent=1) + '\n')
    print(f'{len(writes)} non-query exports, {sum(1 for v in table.values() if v["scripts"])} called by the UI ({files} UI scripts)')
    fams = collections.defaultdict(list)
    for n, v in table.items():
        if v['scripts']:
            fams[v['family']].append(n)
    for fam, names in sorted(fams.items(), key=lambda kv: -len(kv[1])):
        scripts = collections.Counter(s for n in names for s in table[n]['scripts'])
        print(f'{fam:13} {len(names):4}  mostly from {", ".join(s for s, _ in scripts.most_common(4))}')
    relayable = [n for n, v in table.items() if v['scripts'] and v['family'] != 'presentation' and v['kinds'] is not None]
    print(f'{len(relayable)} game writes with scalar, ID or string arguments (relay kinds known)')
    print(f'full map: {args.out}')


if __name__ == '__main__':
    main()
