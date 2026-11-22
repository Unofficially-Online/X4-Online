"""Action parity: in an action test (tools/dev/action_tests.py) player 1 and player 2 each run the same chain of player
actions on their own client. This compares, player against player, what those actions gave and how they went: not
whether the clients stayed in sync (action_tests checks that), but whether player 2's actions are player 1's.

    python tools/dev/action_parity.py RUN_DIR [RUN_DIR...] [--all] [--width N]

It reads a run's two client logs (RUN_DIR/client1/engine-output.log is client 1's, the one where player 1's chain ran)
and prints, section by section, what differs:

  steps     each step of the two chains: did it end the same way, did it answer the same (the same words and keys, a
            value of the same kind: 'none' against 'some' for an object, '-' against 'Recon' for a word), how it got
            there (the kinds of answers on the way, e.g. autopilot -> ai_dock -> docked) and how long it took
  calls     the calls the session replayed for each slot (UI_REPLAY_APPLIED, UI_BUILD_APPLIED, ..._REFUSED rows with
            slot=): the same calls in the same order, with the same marks
  counters  each client's own counters of the action families at the last tick both logged (its UI's captures, refusals,
            guard passes, conversation and screen counts): client k's UI is player k's, so a counter that differs is a
            difference of process between the two players' actions
  refused   what each player's client was refused
  game      the game's own log lines (script errors, UI warnings) on one client only
  gate      the gate's ERROR rows on one client only

A difference is FLAG (a result or a process that is not the same) or note (a value: a price, a distance, a time).
Some scenarios give the two players different ships or places on purpose (action_tests.py --swap exchanges them):
a FLAG that stays with the player when the targets are swapped is the session's, one that moves with the target is the
world's. Exit code: 0 no FLAG, 1 a FLAG, 2 the logs could not be read.
"""
import argparse
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import action_tests as at  # noqa: E402  (parse: a client log's chain rows, refusals)
import station_walk_test as rig  # noqa: E402  (read_lines)

NUMBER = re.compile(r'-?\d+(?:\.\d+)?')
# keys whose values are objects or places: compared only as none/some (0 or an ID) and by shape, never as numbers
ID_KEYS = {'ship', 'name', 'pilot', 'player', 'container', 'occupied', 'controlled', 'room', 'walkable', 'station', 'bay', 'dock',
           'commander', 'id', 'target', 'object', 'owner_id', 'zone', 'sector', 'host', 'leader', 'sub', 'subs_id', 'wreck',
           'satellite', 'probe', 'beacon', 'mine', 'tower', 'person', 'npc', 'entity', 'buyer', 'seller', 'from', 'to',
           'ware_id', 'module', 'order_id', 'mission', 'conversation', 'actor', 'captain', 'marine', 'pod', 'detour'}
POSITION_KEYS = {'at', 'ship_at', 'pos', 'place', 'ship_yaw', 'yaw', 'rel', 'frame', 'hop_end', 'centre', 'dock_rel'}
STEP_FLAG_TICKS = 150          # a step 5 s longer for one player, and half again as long, is a FLAG of process
STATUS = ('UI_REPLAY_STATUS', 'UI_GUARD_STATUS', 'UI_BUILD_STATUS', 'UI_TRADE_STATUS', 'UI_MISSIONS_STATUS',
          'UI_FACTION_STATUS', 'UI_INVENTORY_STATUS', 'UI_INVENTORY_SCOPES', 'UI_BUILD_SCOPES', 'CONVERSATION_OWNER',
          'OWNER_PRESENTATION', 'PLAYER_SETTINGS_STATUS', 'UI_REPLAY_SLOTS', 'UI_LAZY', 'UI_HOLDS', 'MD_MEDIA',
          'GUIDANCE_LOCAL', 'UI_LUA')
# counters of a client's own view rather than of its player's actions (frames drawn, reads of the HUD...): notes
VIEW_FIELDS = {('PLAYER_SETTINGS_STATUS', 'reads'), ('MISSIONS_PER_PLAYER', 'frames'), ('MISSIONS_PER_PLAYER', 'entered'),
               ('MISSIONS_PER_PLAYER', 'swaps'), ('MISSIONS_PER_PLAYER', 'shown'), ('UI_LAZY', 'reads'),
               ('UI_LUA', 'calls')}
# rows of what a client's screen shows and reads (UI frames held, lazy UI reads blocked): their counts are notes
VIEW_TAGS = {'UI_HOLDS', 'UI_LAZY', 'UI_LUA', 'MISSIONS_PER_PLAYER'}
SLOT_ROW = re.compile(r'NATIVE_GATE ([A-Z][A-Z_0-9]+) (?:tick=(\d+) )?(.*?\bslot=(\d+)\b.*)$')
SLOT_TAGS = re.compile(r'_(APPLIED|REFUSED|DROPPED|GENERATED|DENIED|LOST)(_[A-Z]+)?$|^PLAYER_KILLED_SLOT$')


def kind(value):
    """A value's shape (its numbers as N) and its numbers."""
    return NUMBER.sub('N', value), [float(x) for x in NUMBER.findall(value)]


def tokens(text):
    """An answer as its words (in order) and its key=value pairs."""
    words, keys = [], {}
    for tok in re.sub(r'\s+scope=\d+\s*$', '', text).split():
        if '=' in tok and not tok.startswith('='):
            k, v = tok.split('=', 1)
            keys[k] = v
        else:
            words.append(tok)
    return words, keys


def outline(text):
    """What kind of answer a row is: its first word or the first key, for the way a step went."""
    words, keys = tokens(text)
    head = text.split()[0] if text.split() else ''
    return NUMBER.sub('N', head.split('=')[0] if '=' in head else head)[:40]


def compare_answers(a, b):
    """Differences of two answers: (flags, notes), each a list of short texts."""
    flags, notes = [], []
    wa, ka = tokens(a)
    wb, kb = tokens(b)
    sa, na = kind(' '.join(wa))
    sb, nb = kind(' '.join(wb))
    if sa != sb:
        flags.append('words "%s" / "%s"' % (' '.join(wa)[:60], ' '.join(wb)[:60]))
    elif na != nb:
        notes.append('"%s" / "%s"' % (' '.join(wa)[:40], ' '.join(wb)[:40]))
    for key in list(ka) + [k for k in kb if k not in ka]:
        if key not in ka or key not in kb:
            flags.append('%s only for player %d (%s)' % (key, 1 if key in ka else 2, (ka.get(key) or kb.get(key))[:40]))
            continue
        va, vb = ka[key], kb[key]
        if va == vb:
            continue
        (sha, nua), (shb, nub) = kind(va), kind(vb)
        if key in POSITION_KEYS:
            continue
        if sha != shb:
            flags.append('%s=%s / %s' % (key, va[:50], vb[:50]))
            continue
        zero_a, zero_b = [x == 0 for x in nua], [x == 0 for x in nub]
        if key in ID_KEYS or re.fullmatch(r'[0-9a-f]{16}', va):
            if zero_a != zero_b:
                flags.append('%s=%s / %s (none against some)' % (key, va[:40], vb[:40]))
            continue
        if zero_a != zero_b:
            flags.append('%s=%s / %s' % (key, va[:40], vb[:40]))
            continue
        big = any(abs(x - y) > 0.25 * max(abs(x), abs(y)) and abs(x - y) > 1 for x, y in zip(nua, nub))
        notes.append('%s=%s / %s%s' % (key, va[:30], vb[:30], ' (far apart)' if big else ''))
    return flags, notes


def step_table(clients):
    """Each step of both chains in chain order: name -> {k: {rows, end_at, ok, gave_up}}."""
    order, steps = [], {}
    for k in at.PLAYERS:
        for name, rows in clients[k]['steps'].items():
            n = rows[0]['n']
            if name not in steps:
                steps[name] = {}
                order.append((n, name))
            ordered = sorted(rows, key=lambda r: r['at'])
            steps[name][k] = {'rows': ordered, 'end_at': ordered[-1]['at'], 'n': n,
                              'ok': all(r['ok'] for r in ordered), 'gave_up': ordered[-1]['text'].startswith('gave_up')}
    return [name for _, name in sorted(order)], steps


def steps_section(out, clients, width):
    order, steps = step_table(clients)
    ends = {k: clients[k]['end'] for k in at.PLAYERS}
    for k in at.PLAYERS:
        if not ends[k]:
            out.flag('chain', 'player %d: no chain end row (still running at the end of the log)' % k)
    if ends[1] and ends[2]:
        e1, e2 = ends[1], ends[2]
        same = (e1['ok'], e1['done'], e1['of'], e1['why']) == (e2['ok'], e2['done'], e2['of'], e2['why'])
        (out.note if same else out.flag)('chain', 'player 1: %s %d/%d%s; player 2: %s %d/%d%s' % (
            'ended' if e1['ok'] else 'FAILED', e1['done'], e1['of'], ' ' + str(e1['why']) if e1['why'] else '',
            'ended' if e2['ok'] else 'FAILED', e2['done'], e2['of'], ' ' + str(e2['why']) if e2['why'] else ''))
    previous, seen = {1: 0, 2: 0}, {}
    for name in order:
        s = steps[name]
        if set(s) != {1, 2}:
            only = next(iter(s))
            out.flag('step', '%s: only player %d ran it (%s)' % (name, only, s[only]['rows'][-1]['text'][:width]))
            continue
        a, b = s[1], s[2]
        last_a, last_b = a['rows'][-1]['text'], b['rows'][-1]['text']
        if a['ok'] != b['ok'] or a['gave_up'] != b['gave_up']:
            out.flag('step', '%s: player 1 %s, player 2 %s' % (name, 'gave up' if a['gave_up'] else 'ok' if a['ok'] else 'ERROR',
                                                                'gave up' if b['gave_up'] else 'ok' if b['ok'] else 'ERROR'))
        flags, notes = compare_answers(last_a, last_b)
        for f in flags:          # the same difference in several steps (a ship read again and again) is one FLAG
            seen.setdefault(f, {'steps': [], 'detail': (last_a[:width], last_b[:width])})['steps'].append(name)
        if notes:
            out.note('value', '%s: %s' % (name, '; '.join(notes)[:width + 60]))
        way_a = [x for i, x in enumerate(map(outline, (r['text'] for r in a['rows']))) if i == 0 or x != outline(a['rows'][i - 1]['text'])]
        way_b = [x for i, x in enumerate(map(outline, (r['text'] for r in b['rows']))) if i == 0 or x != outline(b['rows'][i - 1]['text'])]
        if way_a != way_b:
            out.flag('way', '%s: player 1 %s; player 2 %s' % (name, ' -> '.join(way_a)[:width // 2], ' -> '.join(way_b)[:width // 2]))
        took = {1: a['end_at'] - previous[1], 2: b['end_at'] - previous[2]}
        previous = {1: a['end_at'], 2: b['end_at']}
        slow, fast = (1, 2) if took[1] > took[2] else (2, 1)
        if took[slow] - took[fast] > STEP_FLAG_TICKS and took[slow] > 1.5 * max(took[fast], 1):
            out.flag('time', '%s: player %d took %d ticks, player %d %d (%.1f s more)' % (name, slow, took[slow], fast, took[fast], (took[slow] - took[fast]) / 30))
        elif abs(took[1] - took[2]) > 60:
            out.note('time', '%s: player 1 %d ticks, player 2 %d' % (name, took[1], took[2]))
    for f, got in seen.items():
        out.flag('answer', '%s: %s' % (', '.join(got['steps'][:6]) + (' (+%d steps)' % (len(got['steps']) - 6) if len(got['steps']) > 6 else ''), f),
                 detail=got['detail'])
    if ends[1] and ends[2] and ends[1]['ok'] and ends[2]['ok']:
        total = {k: max((r['at'] for rows in clients[k]['steps'].values() for r in rows), default=0) for k in at.PLAYERS}
        out.note('time', 'whole chain: player 1 %d ticks (%.0f s), player 2 %d (%.0f s)' % (total[1], total[1] / 30, total[2], total[2] / 30))


def slot_rows(path):
    """Per slot: the session's replayed calls and refusals for it, in order: [(tick, tag, what)]."""
    by = {}
    for line in rig.read_lines(path):
        m = SLOT_ROW.search(line)
        if not m or not SLOT_TAGS.search(m.group(1)):
            continue
        tag, tick, rest, slot = m.group(1), int(m.group(2) or 0), m.group(3), int(m.group(4))
        rest = re.sub(r'\bslot=\d+\s*', '', rest)
        rest = re.sub(r'\bseq=\d+', 'seq=N', rest)
        rest = re.sub(r'\(([^()]*)\)', lambda m: '(' + re.sub(r'\b\d+(\.\d+)?\b', '#', m.group(1)) + ')', rest.strip())   # call arguments: amounts, IDs
        by.setdefault(slot, []).append((tick, tag, re.sub(r'\d{3,}', '#', rest)[:160]))
    return by


def calls_section(out, logs, width):
    by = slot_rows(logs[0])
    other = slot_rows(logs[1])
    if {k: [x[1:] for x in v] for k, v in by.items()} != {k: [x[1:] for x in v] for k, v in other.items()}:
        out.note('calls', "the two clients' logs list different replayed calls (the session's rows should be alike on both): client 1's are compared")
    one, two = [x[1:] for x in by.get(1, [])], [x[1:] for x in by.get(2, [])]
    out.note('calls', 'slot 1: %d rows, slot 2: %d rows' % (len(one), len(two)))
    # each call as often for slot 2 as for slot 1 (the order between calls of the two players' own times may differ)
    count = [Counter(one), Counter(two)]
    first = [{}, {}]
    for i, rows in enumerate((by.get(1, []), by.get(2, []))):
        for tick, tag, what in rows:
            first[i].setdefault((tag, what), tick)
    differ = [x for x in sorted(set(count[0]) | set(count[1])) if count[0][x] != count[1][x]]
    for tag, what in differ:
        x = (tag, what)
        out.flag('calls', '%s %s: slot 1 x%d%s, slot 2 x%d%s' % (tag, what[:width], count[0][x], ' (from tick %d)' % first[0][x] if x in first[0] else '',
                                                                count[1][x], ' (from tick %d)' % first[1][x] if x in first[1] else ''))
    if not differ and one != two:
        out.note('calls', 'the same calls for both slots, in another order')

def status_rows(path):
    """Each status row's fields at every tick it was written: tag -> tick -> {field: value}."""
    out = {}
    want = re.compile(r'NATIVE_GATE (%s) tick=(\d+) (.*)$' % '|'.join(STATUS + ('MISSIONS_PER_PLAYER',)))
    for line in rig.read_lines(path):
        m = want.search(line)
        if m:
            fields = dict(re.findall(r'(\w+)=(\S+)', m.group(3)))
            if not fields:
                fields = {'value': m.group(3).strip()}
            out.setdefault(m.group(1), {})[int(m.group(2))] = fields
    return out


def counters_section(out, logs, ends, width):
    rows = [status_rows(path) for path in logs]
    # the last tick both wrote each status, after both chains ended when they did (the counters then hold the whole chain)
    for tag in sorted(set(rows[0]) | set(rows[1])):
        a, b = rows[0].get(tag, {}), rows[1].get(tag, {})
        both = sorted(set(a) & set(b))
        if not both:
            out.note('counters', '%s: only client %d writes it' % (tag, 1 if a else 2))
            continue
        tick = both[-1]
        fa, fb = a[tick], b[tick]
        differ = [f for f in list(fa) + [f for f in fb if f not in fa] if fa.get(f) != fb.get(f)]
        if not differ:
            continue
        view = [f for f in differ if (tag, f) in VIEW_FIELDS or tag in VIEW_TAGS]
        action = [f for f in differ if f not in view]
        if action:
            out.flag('counters', '%s at tick %d: %s' % (tag, tick, ', '.join('%s %s / %s' % (f, fa.get(f, '-'), fb.get(f, '-')) for f in action))[:width * 2])
        if view:
            out.note('counters', '%s at tick %d (view): %s' % (tag, tick, ', '.join('%s %s / %s' % (f, fa.get(f, '-'), fb.get(f, '-')) for f in view))[:width * 2])


def lines_of(path, gate):
    """The game's own log lines (gate=False) or the gate's ERROR rows (gate=True), digits as N, with counts."""
    counts, session = {}, False
    for line in rig.read_lines(path):
        line = line.rstrip()
        if not session:          # lines of the start and the load are each PC's own; the session's begin at its first tick
            session = 'NATIVE_GATE DIGEST_PARTS' in line
            continue
        mine = '[X4LAN]' in line
        if not line or re.sub(r'^\[[^\]]*\]\s*[\d.]+\s*', '', line).strip('=') == '':
            continue
        if gate:
            m = re.search(r'NATIVE_GATE ERROR (.*)$', line)
            if not m or 'DEGRADED' in m.group(1):
                continue
            text = m.group(1)
        else:
            if mine:
                continue
            text = re.sub(r'^\[[^\]]*\]\s*[\d.]+\s*', '', line)
        text = re.sub(r'0x[0-9a-f]+', '0xN', text)
        text = re.sub(r'\d+', 'N', text)[:220]
        counts[text] = counts.get(text, 0) + 1
    return counts


def lines_section(out, logs, gate, width):
    a, b = (lines_of(path, gate) for path in logs)
    name = 'gate' if gate else 'game'
    for text in sorted(set(a) | set(b)):
        if a.get(text, 0) != b.get(text, 0):
            # client 1 is player 1's screen and UI: a line there and not on client 2 is a step of player 1's actions'
            # way that player 2's did not take (or one of client 2's own view)
            out.flag(name, 'client 1 x%d, client 2 x%d: %s' % (a.get(text, 0), b.get(text, 0), text[:width + 40]))


class Out:
    def __init__(self, show_notes):
        self.flags, self.notes, self.show_notes = 0, 0, show_notes

    def flag(self, section, text, detail=None):
        self.flags += 1
        print('  FLAG %-8s %s' % (section, text))
        if detail:
            print('                p1: %s\n                p2: %s' % detail)

    def note(self, section, text):
        self.notes += 1
        if self.show_notes:
            print('  note %-8s %s' % (section, text))


def compare(run_dir, show_notes=True, width=110):
    logs = [Path(run_dir) / ('client%d' % k) / 'engine-output.log' for k in at.PLAYERS]
    if not all(p.exists() for p in logs):
        print('%s: no client logs (client1/engine-output.log, client2/engine-output.log)' % run_dir)
        return None
    clients = {k: at.parse(path) for k, path in zip(at.PLAYERS, logs)}
    out = Out(show_notes)
    print('== %s' % run_dir)
    print('-- steps (client 1 runs player 1\'s chain, client 2 player 2\'s)')
    steps_section(out, clients, width)
    print('-- calls the session replayed per slot')
    calls_section(out, logs, width)
    print('-- each client\'s own counters of the action families')
    counters_section(out, logs, {k: clients[k]['end'] for k in at.PLAYERS}, width)
    print('-- refused')
    for k in at.PLAYERS:
        if clients[k]['refused']:
            print('  ', 'player %d: %s' % (k, ', '.join('%s x%d' % x for x in sorted(clients[k]['refused'].items()))))
    if clients[1]['refused'] != clients[2]['refused']:
        out.flag('refused', 'the two players were refused different calls')
    print('-- the game\'s own log lines on one client only')
    lines_section(out, logs, False, width)
    print('-- the gate\'s ERROR rows on one client only')
    lines_section(out, logs, True, width)
    print('== %d FLAG, %d notes' % (out.flags, out.notes))
    return out.flags


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('runs', nargs='+', type=Path, help='run directories (.local/runs/NAME)')
    p.add_argument('--flags', action='store_true', help='FLAG lines only, no notes')
    p.add_argument('--width', type=int, default=110)
    args = p.parse_args(argv)
    worst = 0
    for run in args.runs:
        got = compare(run, not args.flags, args.width)
        worst = max(worst, 2 if got is None else 1 if got else 0)
    return worst


if __name__ == '__main__':
    sys.exit(main())
