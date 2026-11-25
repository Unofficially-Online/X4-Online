"""Story milestones: one per completed mission chapter, so a playthrough continues from its last one (x4ls continue).

User request, 24 Nov: milestones through the story, one per completed mission, so that a playthrough can go on from a
save instead of playing the whole mission chain again each time.

The catalogue (STORIES) lists, per story, its milestones in order. A milestone is one chapter of a stock story script that
is over: the chapter's root cue is cancelled by the chapter's own clean-up cue, which is also where X4 completes the
chapter's mission (remove_mission type="completed") when the chapter ends one. Egosoft waits for the same thing in the
stock scripts (Story_Yaki's Ch22 clean-up: event_cue_cancelled of its sub-chapters), so the marker is X4's own:
    event_cue_cancelled cue="md.<script>.<chapter>"
A chapter cue is cancelled once per game (each one has a single canceller in the stock files; check() reads them).

A session runs the scripts of a story for its players in three forms (tools/dev/native_gate_session_start.py,
tools/dev/native_gate_plot_copies.py): slot 1 the stock scripts of its start (the base start's story runs for slot 1 by
X4's module rule, the stock plots after it too); a slot 2..N the stock script it claims (spec slot 'story'), or its own
copies <Script>_P<k> (spec slot 'plots': GS_Terran1_P2, Story_Yaki_P2). watches() turns a session's slots into the
milestones to listen for, each with the script that is that slot's. The listener MD (milestones_md, md/x4lan_milestones.xml
of every play overlay with a session) has one root cue per watched milestone; its frame runs at the same tick on every
client (MD is the simulation's), records the milestone in md.$X4LAN_Milestones (in every later save) and tells the probe
(raise_lua_event X4LAN_Milestones.Reached<i>, param player.age). The probe turns that game time into one save tick for
every client and saves through the periodic save's gate path (native_gate_probe.lua 'milestones'); x4ls play collects
the save as RUN/saves/milestones/<story>-M<n>-slot<k>.xml.gz with a JSON sidecar (src/lockstep/launcher/saves.py).

Mission ownership in the save. The gate's table of which slot owns which MD cue (lockstep_mission_owners.h) is the
gate's own and keyed by runtime IDs, which a load gives anew (a cue's id in a save is the save's ordinal). So it is kept
the way X4 keeps cue references: right before a milestone save the probe copies the gate's table into MD lists of cue
values (md.$X4LAN_Owners, md.$X4LAN_OwnerSlots: signals x4lan_owners_reset and x4lan_owner_<k> on the galaxy, the same
tick on every client), and X4 saves and loads those like any script's cue reference. A continued session's probe asks
the MD for them (x4lan_owners_dump: raise_lua_event X4LAN_Milestones.Owner<k> per entry, then OwnersEnd with the count)
and claims each cue for its slot again (gate.mpp_claim), at the same tick on every client. Open: X4 hands a cue to Lua
as nil and formats one as its bare name (live, 24 Nov), so a saved cue reaches the probe without an ID and stays
nobody's (counted as unreadable; a text with '<inst:hex>' would be claimed). The session's own stories need no
snapshot: each script of slot k's (its plot copies, a stock
script it claimed) is slot k's by name before the first tick (owned_lines: native_gate_plot_owned, the gate's
mpp_first_add_slot), as the claim of its root cue made every cue of it; also from a periodic save or an older build's
save. Why not a table updated at every claim: the
gate claims in C (a story's first frame, every root instance a slot's frame makes), where no MD runs; the snapshot at
the save takes all of them, and only a save needs them.

Another story is added as one more STORIES entry: its stock plots (extension, file, script) and its milestones (plot,
chapter cue, end cue). check() verifies every one against this PC's game data; the format never changes.

    python tools/dev/native_gate_milestones.py list
    python tools/dev/native_gate_milestones.py check --gamedata <X4LAN_GAMEDATA>
    python tools/dev/native_gate_milestones.py md --session <session.json>
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_gate_plot_copies as plot_copies  # noqa: E402

FORMAT = 'X4LAN_MILESTONES_1'
SIDECAR_FORMAT = 'X4LAN_MILESTONE_SAVE_1'
MD_FILE = 'md/x4lan_milestones.xml'
SCRIPT = 'X4LAN_Milestones'
REACHED = 'X4LAN_Milestones.Reached'   # + the watch's index (1-based): param player.age
OWNER = 'X4LAN_Milestones.Owner'       # + the slot: param the cue
OWNERS_END = 'X4LAN_Milestones.OwnersEnd'
SIGNAL_RESET, SIGNAL_OWNER, SIGNAL_DUMP = 'x4lan_owners_reset', 'x4lan_owner_', 'x4lan_owners_dump'
SAVE_SCREEN, SAVE_CONTROL = 'X4LAN_Saves', 'Request'   # a person's save: the UI event the asking client raises as a click
SAVE_REQUEST, SAVE_TIME = 'X4LAN_Saves.Request', 'X4LAN_Saves.RequestTime'
NAME = re.compile(r'[A-Za-z][A-Za-z0-9_]*')
STORY_ID = re.compile(r'[a-z][a-z0-9_]{0,31}')
MAX_SLOTS = 8

# The story of the Terran cadet start (ego_dlc_terran): GS_Terran1 (the cadet's own two chapters), then Story_Yaki (the
# Terran story, which the cadet story's last step activates). Read from ego_dlc_terran/md/gs_terran1.xml and story_yaki.xml
# of game 9.00 (check() confirms each line): 'cue' is the chapter's root cue under Start, 'end' its clean-up cue (it cancels
# the chapter: the milestone's moment), 'mission' True where 'end' completes the chapter's mission (remove_mission
# type="completed"); the other chapters are steps of the mission that a later chapter completes.
TERRAN_CADET = [
    ('story', 'Ch1_Maintenance', 'Ch1_Clean_Up', False, 'cadet part 1: the satellites (Ch2_Distress begins)'),
    ('story', 'Ch2_Distress', 'Ch2_Clean_Up_Terran_Cadet_Gamestart_Mission', True, 'cadet complete: the distress call (25,000 Cr)'),
    ('yaki', 'Ch1_Escort', 'Ch1_Clean_Up', False, 'Terran war intro 1: escort the transport'),
    ('yaki', 'Ch2_Mines', 'Ch2_Clean_Up', False, 'Terran war intro 1: the mines'),
    ('yaki', 'Ch3_Patrol', 'Ch3_Clean_Up', False, 'Terran war intro 1: the patrol'),
    ('yaki', 'Ch4_Erratic_Miner', 'Ch4_Clean_Up_Terran_War_Intro_Mission_1', True, 'Terran war intro 1 complete: the erratic miner'),
    ('yaki', 'Ch5_Upgrade', 'Ch5_Clean_Up', True, 'the upgrade at the equipment dock'),
    ('yaki', 'Ch6_Scout', 'Ch6_Clean_Up', False, 'Terran war intro 2: scouting'),
    ('yaki', 'Ch7_Infrastructure', 'Ch7_Clean_Up', False, 'Terran war intro 2: the infrastructure'),
    ('yaki', 'Ch8_Investigate_Signal', 'Ch8_Clean_Up_Terran_War_Intro_Mission_2', True, 'Terran war intro 2 complete: the signal'),
    ('yaki', 'Ch9_Secret_Service', 'Ch9_Clean_Up_Secret_Service_Contact_Mission', True, 'the secret service contact'),
    ('yaki', 'Ch10_Yaki_Investigation_Intro', 'Ch10_Clean_Up', False, 'Yaki investigation 1: the briefing'),
    ('yaki', 'Ch11_Find_Traces', 'Ch11_Clean_Up', False, 'Yaki investigation 1: the traces'),
    ('yaki', 'Ch12_Ambush_Wreck', 'Ch12_Clean_Up', False, 'Yaki investigation 1: the ambush wreck'),
    ('yaki', 'Ch13_Track_Yaki', 'Ch13_Clean_Up', False, 'Yaki investigation 1: tracking the Yaki'),
    ('yaki', 'Ch14_Rescue_Pioneers', 'Ch14_Clean_Up_Yaki_Investigation_Mission_1', True, 'Yaki investigation 1 complete: the Pioneers'),
    ('yaki', 'Ch15_Pioneers_Intro', 'Ch15_Clean_Up', False, 'Yaki investigation 2: the Pioneers'),
    ('yaki', 'Ch16_Conversation_Puzzle', 'Ch16_Clean_Up', False, 'Yaki investigation 2: the conversations'),
    ('yaki', 'Ch17_Follow_Yaki_Civilian', 'Ch17_Clean_Up', False, 'Yaki investigation 2: following the civilian'),
    ('yaki', 'Ch18_Asteroid_Base', 'Ch18_Clean_Up_Yaki_Investigation_Mission_2', True, 'Yaki investigation 2 complete: the asteroid base'),
    ('yaki', 'Ch19_Discover_Yaki', 'Ch19_Clean_Up_Yaki_Investigation_Mission_3', True, 'Yaki investigation 3: the Yaki home sector'),
    ('yaki', 'Ch20_Arrival', 'Ch20_Clean_Up', False, 'Yaki investigation 4: the arrival'),
    ('yaki', 'Ch21_Yaki_Station', 'Ch21_Clean_Up_Yaki_Investigation_Mission_4', True, 'Yaki investigation 4 complete: the Yaki station'),
    ('yaki', 'Ch22_Decide_Outcome', 'Ch22_Clean_Up_Yaki_Investigation_Mission_5', True, 'Yaki investigation 5: the outcome'),
    ('yaki', 'Ch23_Yaki_Trade', 'Ch23_Clean_Up_Mission', True, 'the Yaki trade mission'),
]

STORIES = {
    'terran_cadet': {
        'what': "the Terran cadet start's story (GS_Terran1) and the Terran story after it (Story_Yaki)",
        'gamestart': 'x4ep1_gamestart_terran1',
        # the plot ids of native_gate_plot_copies.py: 'story' is the start's own script, 'yaki' the plot that follows it
        'plots': {'story': {'extension': 'ego_dlc_terran', 'file': 'gs_terran1.xml', 'script': 'GS_Terran1', 'cue': 'Start'},
                  'yaki': {'extension': 'ego_dlc_terran', 'file': 'story_yaki.xml', 'script': 'Story_Yaki', 'cue': 'Start'}},
        'milestones': [{'plot': p, 'cue': c, 'end': e, 'mission': m, 'title': t} for p, c, e, m, t in TERRAN_CADET],
    },
}


class MilestoneError(ValueError):
    """A catalogue, session or milestone file that cannot be used as asked (the text names it)."""


def check_catalogue(stories=None):
    """The catalogue's own form (no game data): ids, plots, names, one milestone per chapter."""
    stories = STORIES if stories is None else stories
    for story_id, story in stories.items():
        if not STORY_ID.fullmatch(story_id):
            raise MilestoneError(f'story id {story_id!r}')
        plots = story.get('plots') or {}
        if not plots or not all(NAME.fullmatch(p.get('script') or '') and NAME.fullmatch(p.get('cue') or '') for p in plots.values()):
            raise MilestoneError(f'{story_id}: its plots need a script and a root cue')
        seen = set()
        for n, m in enumerate(story.get('milestones') or [], 1):
            if m.get('plot') not in plots or not NAME.fullmatch(m.get('cue') or '') or not NAME.fullmatch(m.get('end') or ''):
                raise MilestoneError(f'{story_id} M{n}: plot, cue and end cue')
            key = (plots[m['plot']]['script'], m['cue'])
            if key in seen:
                raise MilestoneError(f'{story_id} M{n}: {key[0]}.{key[1]} twice')
            seen.add(key)
        if not seen:
            raise MilestoneError(f'{story_id}: no milestones')
    return stories


def save_name(story, n, slot):
    """The milestone save's name: <story>-M<n>-slot<k> (RUN/saves/milestones/<name>.xml.gz)."""
    return f'{story}-M{int(n)}-slot{int(slot)}'


NAME_FORM = re.compile(r'([a-z][a-z0-9_]{0,31})-M(\d{1,3})-slot([1-8])')
SAVE_NAME = re.compile(r'[A-Za-z0-9_-]{1,64}')   # any session save's name: a milestone's, <label>-t<T>, periodic-t<T>, c<K>-t<T>


def parse_name(text):
    m = NAME_FORM.fullmatch(text or '')
    return (m[1], int(m[2]), int(m[3])) if m else None


def milestone(story, n, stories=None):
    stories = STORIES if stories is None else stories
    if story not in stories or not 1 <= int(n) <= len(stories[story]['milestones']):
        raise MilestoneError(f'no milestone {story} M{n}')
    return stories[story]['milestones'][int(n) - 1]


# ---------------------------------------------------------------- a session's watches

def script_owners(slots):
    """{script name: slot} of a session's slots: a slot's claimed stock script and its own copies. Every other stock
    script is slot 1's (the base start's story by X4's module rule; a plot nobody claimed)."""
    owners = {}
    for k, slot in sorted((slots or {}).items(), key=lambda kv: int(kv[0])):
        story = slot.get('story')
        if story:
            owners[story['script']] = int(k)
        for e in slot.get('plots') or []:
            owners[e['copy']] = int(k)
    return owners


def slot_scripts(slots, k):
    """{stock script: the script slot k runs it as}: its copies by their stock name, a claimed stock script as itself;
    slot 1 every stock script nobody claimed (as itself)."""
    k = int(k)
    if k == 1:
        claimed = {s['script'] for s in ((v.get('story') or None) for v in (slots or {}).values()) if s}
        return lambda stock: None if stock in claimed else stock
    slot = (slots or {}).get(str(k)) or {}
    mine = {e['script']: e['copy'] for e in slot.get('plots') or []}
    if slot.get('story'):
        mine[slot['story']['script']] = slot['story']['script']
    return mine.get


def story_in(session, story):
    """Whether a session has this story: its base start is the story's start, or a slot runs one of its scripts."""
    scripts = {p['script'] for p in story['plots'].values()}
    slots = (session or {}).get('slots') or {}
    return (session or {}).get('base_start') == story['gamestart'] or any(
        ((v.get('story') or {}).get('script') in scripts) or any(e.get('script') in scripts for e in v.get('plots') or [])
        for v in slots.values())


def watches(session, stories=None):
    """The milestones a session listens for: [{index, story, n, slot, script, cue, end, mission, title, name}], slot by
    slot, each story's milestones in order. A slot watches a story's milestones in the scripts that are its own; slot 1
    the stock scripts nobody claimed, of a story the session has at all: its base start's, or one a slot runs (so its DLC
    is there; a listener naming a script X4 does not have would be an MD error). Slot 1's stock script of a start that is
    not the base start's never begins: those listeners just wait."""
    stories = check_catalogue(stories)
    players = int((session or {}).get('players') or 0)
    if not 1 <= players <= MAX_SLOTS:
        raise MilestoneError(f'a session of {players} players')
    slots = (session or {}).get('slots') or {}
    out = []
    for k in range(1, players + 1):
        if k > 1 and str(k) not in slots:
            continue
        script_of = slot_scripts(slots, k)
        for story_id, story in stories.items():
            if k == 1 and not story_in(session, story):
                continue
            for n, m in enumerate(story['milestones'], 1):
                script = script_of(story['plots'][m['plot']]['script'])
                if not script:
                    continue
                if not NAME.fullmatch(script):
                    raise MilestoneError(f'slot {k}: script name {script!r}')
                out.append({'index': len(out) + 1, 'story': story_id, 'n': n, 'slot': k, 'script': script, 'cue': m['cue'],
                            'end': m['end'], 'mission': m['mission'], 'title': m['title'], 'name': save_name(story_id, n, k)})
    return out


def story_roots(session):
    """[(slot, '<Script>.<Cue>')] of the stories the session gives slots 2..N: the root cue the gate claimed for that slot
    at the story's begin (native_gate_session_start.py story_md), in slot order (a claimed stock script, then its copies)."""
    roots = []
    for k, slot in sorted(((session or {}).get('slots') or {}).items(), key=lambda kv: int(kv[0])):
        story = slot.get('story')
        if story:
            roots.append((int(k), f"{story['script']}.{story['cue']}"))
        for e in slot.get('plots') or []:
            roots.append((int(k), f"{e['copy']}.{e['cue']}"))
    for k, path in roots:
        if not all(NAME.fullmatch(part) for part in path.split('.')) or path.count('.') != 1:
            raise MilestoneError(f'slot {k}: story root {path!r}')
    return roots


# ---------------------------------------------------------------- the listener MD

def milestones_md(rows, roots=(), players=MAX_SLOTS):
    """md/x4lan_milestones.xml for these watches (watches()) and story roots (story_roots()): generated per session, the
    same on every client and in every overlay of the session (a joiner's, a continued session's)."""
    root = ET.Element('mdscript', {'name': SCRIPT, 'xmlns:xsi': 'http://www.w3.org/2001/XMLSchema-instance',
                                   'xsi:noNamespaceSchemaLocation': 'md.xsd'})
    cues = ET.SubElement(root, 'cues')
    for w in rows:
        # A root cue, not instantiated: once per game, complete in every later save (a continued session never hears a
        # milestone of its past again: the chapter's cancel was before the save).
        cue = ET.SubElement(cues, 'cue', {'name': f"{w['story']}_M{w['n']}_S{w['slot']}"})
        ET.SubElement(ET.SubElement(cue, 'conditions'), 'event_cue_cancelled', {'cue': f"md.{w['script']}.{w['cue']}"})
        actions = ET.SubElement(cue, 'actions')
        new = ET.SubElement(actions, 'do_if', {'value': 'not md.$X4LAN_Milestones?'})
        ET.SubElement(new, 'set_value', {'name': 'md.$X4LAN_Milestones', 'exact': '[]'})
        ET.SubElement(actions, 'append_to_list', {'name': 'md.$X4LAN_Milestones',
                                                  'exact': f"['{w['story']}', {w['n']}, {w['slot']}, player.age]"})
        ET.SubElement(actions, 'debug_text', {'text': f"'[X4LAN] MILESTONE_MD name={w['name']} script={w['script']} cue={w['cue']} "
                                                      f"time=%s'.[player.age]"})
        ET.SubElement(actions, 'raise_lua_event', {'name': f"'{REACHED}{w['index']}'", 'param': 'player.age'})
    # The gate's mission owners as cue values, for the saves (see the module text): reset, then one entry per claim.
    reset = ET.SubElement(cues, 'cue', {'name': 'OwnersReset', 'instantiate': 'true'})
    ET.SubElement(ET.SubElement(reset, 'conditions'), 'event_object_signalled', {'object': 'player.galaxy', 'param': f"'{SIGNAL_RESET}'"})
    actions = ET.SubElement(reset, 'actions')
    ET.SubElement(actions, 'set_value', {'name': 'md.$X4LAN_Owners', 'exact': '[]'})
    ET.SubElement(actions, 'set_value', {'name': 'md.$X4LAN_OwnerSlots', 'exact': '[]'})
    for k in range(1, int(players) + 1):
        own = ET.SubElement(cues, 'cue', {'name': f'Owner{k}', 'instantiate': 'true'})
        ET.SubElement(ET.SubElement(own, 'conditions'), 'event_object_signalled',
                      {'object': 'player.galaxy', 'param': f"'{SIGNAL_OWNER}{k}'"})
        actions = ET.SubElement(own, 'actions')
        new = ET.SubElement(actions, 'do_if', {'value': 'not md.$X4LAN_Owners?'})
        ET.SubElement(new, 'set_value', {'name': 'md.$X4LAN_Owners', 'exact': '[]'})
        ET.SubElement(new, 'set_value', {'name': 'md.$X4LAN_OwnerSlots', 'exact': '[]'})
        known = ET.SubElement(actions, 'do_if', {'value': 'event.param2'})   # a cue that is gone arrives as null
        ET.SubElement(known, 'append_to_list', {'name': 'md.$X4LAN_Owners', 'exact': 'event.param2'})
        ET.SubElement(known, 'append_to_list', {'name': 'md.$X4LAN_OwnerSlots', 'exact': str(k)})
    # A person's save (the probe's ticklog.save_request): the asking client's UI event, run on every client at its apply
    # tick by the UI replay's events family; the probe makes the save tick from the time (two events, in this order).
    ask = ET.SubElement(cues, 'cue', {'name': 'SaveRequest', 'instantiate': 'true'})
    ET.SubElement(ET.SubElement(ask, 'conditions'), 'event_ui_triggered', {'screen': f"'{SAVE_SCREEN}'", 'control': f"'{SAVE_CONTROL}'"})
    actions = ET.SubElement(ask, 'actions')
    ET.SubElement(actions, 'set_value', {'name': '$text', 'exact': "'%s|%s|%s'.[event.param3.$slot, event.param3.$file, event.param3.$name]"})
    ET.SubElement(actions, 'debug_text', {'text': "'[X4LAN] SAVE_REQUEST_MD request=%s time=%s'.[$text, player.age]"})
    ET.SubElement(actions, 'raise_lua_event', {'name': f"'{SAVE_REQUEST}'", 'param': '$text'})
    ET.SubElement(actions, 'raise_lua_event', {'name': f"'{SAVE_TIME}'", 'param': 'player.age'})
    dump = ET.SubElement(cues, 'cue', {'name': 'OwnersDump', 'instantiate': 'true'})
    ET.SubElement(ET.SubElement(dump, 'conditions'), 'event_object_signalled', {'object': 'player.galaxy', 'param': f"'{SIGNAL_DUMP}'"})
    actions = ET.SubElement(dump, 'actions')
    ET.SubElement(actions, 'set_value', {'name': '$count', 'exact': '0'})
    for k, path in roots:   # the session's story roots: their scripts are their slots' by name (owned_lines); their state
        ET.SubElement(actions, 'debug_text', {'text': f"'[X4LAN] SAVE_OWNERS_MD root slot={int(k)} cue=%s state=%s'.[md.{path}, md.{path}.state]"})
    have = ET.SubElement(actions, 'do_if', {'value': 'md.$X4LAN_Owners? and md.$X4LAN_OwnerSlots?'})
    each = ET.SubElement(have, 'do_all', {'exact': 'md.$X4LAN_Owners.count', 'counter': '$i'})
    alive = ET.SubElement(each, 'do_if', {'value': 'md.$X4LAN_Owners.{$i} and md.$X4LAN_OwnerSlots.{$i}'})
    # X4 gives a cue to Lua as nil (live, 24 Nov): a cue goes as its text (its bare name there, so unreadable: open)
    cue = ET.SubElement(alive, 'do_if', {'value': 'typeof md.$X4LAN_Owners.{$i} == datatype.cue'})
    ET.SubElement(cue, 'raise_lua_event', {'name': f"'{OWNER}%s'.[md.$X4LAN_OwnerSlots.{{$i}}]", 'param': "'%s'.[md.$X4LAN_Owners.{$i}]"})
    other = ET.SubElement(alive, 'do_else')
    ET.SubElement(other, 'raise_lua_event', {'name': f"'{OWNER}%s'.[md.$X4LAN_OwnerSlots.{{$i}}]", 'param': 'md.$X4LAN_Owners.{$i}'})
    ET.SubElement(alive, 'set_value', {'name': '$count', 'exact': '1', 'operation': 'add'})
    ET.SubElement(actions, 'debug_text', {'text': "'[X4LAN] SAVE_OWNERS_MD dumped=%s time=%s'.[$count, player.age]"})
    ET.SubElement(actions, 'raise_lua_event', {'name': f"'{OWNERS_END}'", 'param': '$count'})
    ET.indent(root, ' ')
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<!-- Generated by tools/dev/native_gate_milestones.py for one session (overlay only, never in git). -->\n'
            + ET.tostring(root, encoding='unicode') + '\n')


def lua_word(text):
    if not re.fullmatch(r'[A-Za-z0-9_]{1,64}', str(text)):
        raise MilestoneError(f'unsafe word {text!r}')
    return f"'{text}'"


def config_lines(rows, save=True):
    """X4LAN_CONFIG.native_gate_milestones of a session overlay (every one: its saves, milestones or not): the watches in
    index order (the probe registers X4LAN_Milestones.Reached<i>), and whether this session saves (Windows clients only, no
    open slots: the periodic save's rule; a person's Save game too)."""
    watch = ','.join('{' + f"{lua_word(w['story'])},{int(w['n'])},{int(w['slot'])}" + '}' for w in rows)
    return [f"X4LAN_CONFIG.native_gate_milestones = {{save={'true' if save else 'false'},watch={{{watch}}}}}"]


CONTINUE_FORMAT = 'X4LAN_CONTINUE_1'
WARE = re.compile(r'[a-z][a-z0-9_]{0,63}')
CONTINUE_WARES = 512      # wares (or research, blueprints) per slot a continued session gives back
AMOUNT_MAX = 10 ** 9


def check_continue(block):
    """A session's 'continue' block (src/lockstep/launcher/milestone.py writes it; a joiner gets it from the host's session.json,
    so every value that reaches the config as Lua is checked here): the milestone's name and per slot 2..N its saved
    inventory, blueprints and research. Returns it."""
    if not isinstance(block, dict) or block.get('format') != CONTINUE_FORMAT or not SAVE_NAME.fullmatch(str(block.get('name'))):
        raise MilestoneError('continue: not a continue block of a session save')
    slots = block.get('slots') or {}
    if not isinstance(slots, dict):
        raise MilestoneError('continue: slots')
    for k, slot in slots.items():
        if not (isinstance(k, str) and k.isdigit() and 2 <= int(k) <= MAX_SLOTS and isinstance(slot, dict)):
            raise MilestoneError(f'continue: slot {k!r}')
        items = slot.get('inventory') or []
        if not isinstance(items, list) or len(items) > CONTINUE_WARES or not all(
                isinstance(i, dict) and WARE.fullmatch(str(i.get('ware'))) and type(i.get('amount')) is int and 1 <= i['amount'] <= AMOUNT_MAX
                for i in items) or len({i['ware'] for i in items}) != len(items):
            raise MilestoneError(f'continue: slot {k}: inventory')
        for part in ('blueprints', 'research'):
            wares = slot.get(part) or []
            if not isinstance(wares, list) or len(wares) > CONTINUE_WARES or not all(isinstance(w, str) and WARE.fullmatch(w) for w in wares)                     or len(set(wares)) != len(wares):
                raise MilestoneError(f'continue: slot {k}: {part}')
    return block


def continue_lines(session):
    """X4LAN_CONFIG.native_gate_continue of a continued session (session.json 'continue'), [] for any other session."""
    block = (session or {}).get('continue')
    if not block:
        return []
    check_continue(block)
    parts = []
    for k in sorted(block.get('slots') or {}, key=int):
        slot = block['slots'][k]
        inventory = ','.join(f"{{'{i['ware']}',{i['amount']}}}" for i in slot.get('inventory') or [])
        fields = [f'inventory={{{inventory}}}'] + [f"{part}={{{','.join(repr(w) for w in slot.get(part) or [])}}}" for part in ('blueprints', 'research')]
        parts.append(f'[{int(k)}]={{' + ','.join(fields) + '}')
    return [f"X4LAN_CONFIG.native_gate_continue = {{name='{block['name']}',slots={{{','.join(parts)}}}}}"]


def owned_lines(session):
    """X4LAN_CONFIG.native_gate_plot_owned of a continued session: every script of slot k's own (its plot copies, a stock
    script it claimed) is slot k's again by name before the first tick (lockstep_mission_owners.h mpp_first_add_slot).
    Only a continued session: in a new one the stories begin and the gate claims their root cues then."""
    if not (session or {}).get('continue'):
        return []
    rows = [(path.split('.')[0], k) for k, path in story_roots(session)]
    if not rows:
        return []
    return ['X4LAN_CONFIG.native_gate_plot_owned = {' + ','.join(f"{{{lua_word(script)},{int(k)}}}" for script, k in rows) + '}']


def write_files(out, session, save=True):
    """The listener MD in an overlay folder and its config lines; ([], []) for a session without watches."""
    rows = watches(session)
    if not rows:
        return [], []
    path = Path(out) / MD_FILE
    if path.exists():
        raise MilestoneError(f'{path} exists; the milestones file never replaces an overlay file')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(milestones_md(rows, story_roots(session), session['players']), encoding='utf-8')
    return [MD_FILE], config_lines(rows, save)


# ---------------------------------------------------------------- the game data

def _tree(root, plot):
    tree, _ = plot_copies.stock_tree(root, dict(plot, id='story', what=''))
    return tree


def _cues(tree):
    parent, by_name = {}, {}

    def walk(element, above):
        for child in element:
            if child.tag == 'cue':
                name = child.get('name')
                if name:
                    by_name.setdefault(name, child)
                    parent[name] = above
                walk(child, child)
            else:
                walk(child, above)
    walk(tree.find('cues'), None)
    return by_name, parent


def check(root, stories=None):
    """Every milestone against this PC's unpacked game data (gamedata.unpack, X4LAN_GAMEDATA): its chapter cue is a cue of
    the stock script, its end cue exists and cancels that chapter (in its actions or its sub-cues'), it is the chapter's
    only canceller, and 'mission' says whether the end completes a mission. Returns [(story, n, text)] of what is wrong."""
    stories = check_catalogue(stories)
    wrong = []
    for story_id, story in stories.items():
        trees = {}
        for plot_id, plot in story['plots'].items():
            try:
                trees[plot_id] = _cues(_tree(root, plot))
            except plot_copies.PlotError as error:
                wrong.append((story_id, 0, str(error)))
        for n, m in enumerate(story['milestones'], 1):
            if m['plot'] not in trees:
                continue
            by_name, _ = trees[m['plot']]
            chapter, end = by_name.get(m['cue']), by_name.get(m['end'])
            if chapter is None or end is None:
                wrong.append((story_id, n, f"{m['cue'] if chapter is None else m['end']}: no such cue"))
                continue
            cancellers = [c.get('name') for c in by_name.values()
                          if any(a.get('cue') == m['cue'] for part in c if part.tag == 'actions' for a in part.iter('cancel_cue'))]
            under_end = {c.get('name') for c in end.iter('cue')}
            if not cancellers or not set(cancellers) <= under_end:
                wrong.append((story_id, n, f"{m['cue']} is cancelled by {cancellers or 'nothing'}, not by {m['end']}"))
            completes = any(a.get('type') == 'completed' for part in end if part.tag == 'actions' for a in part.iter('remove_mission'))
            if completes != bool(m['mission']):
                wrong.append((story_id, n, f"{m['end']} {'completes' if completes else 'completes no'} mission"))
    return wrong


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    sub.add_parser('list', help='the catalogue: every story and its milestones')
    c = sub.add_parser('check', help='every milestone against the unpacked game data')
    c.add_argument('--gamedata', default=os.environ.get('X4LAN_GAMEDATA'), help='unpacked game data (default X4LAN_GAMEDATA)')
    m = sub.add_parser('md', help="a session's watches and its listener MD")
    m.add_argument('--session', required=True, help='session.json (or spec.json)')
    m.add_argument('--out', help='write the MD here (default: print the watches only)')
    args = p.parse_args(argv)
    try:
        if args.command == 'list':
            for story_id, story in check_catalogue().items():
                print(f"{story_id}: {story['what']} (start {story['gamestart']})")
                for n, ms in enumerate(story['milestones'], 1):
                    script = story['plots'][ms['plot']]['script']
                    print(f"  M{n:<2} {script}.{ms['cue']} cancelled by {ms['end']}{' (mission complete)' if ms['mission'] else ''}: {ms['title']}")
        elif args.command == 'check':
            if not args.gamedata:
                p.error('--gamedata (or X4LAN_GAMEDATA) is required')
            wrong = check(args.gamedata)
            for story_id, n, text in wrong:
                print(f'{story_id} M{n}: {text}')
            print(f"{sum(len(s['milestones']) for s in STORIES.values())} milestones, {len(wrong)} wrong")
            return 1 if wrong else 0
        else:
            session = json.loads(Path(args.session).read_text(encoding='utf-8'))
            rows = watches(session)
            for w in rows:
                print(f"{w['index']:>3} slot {w['slot']} {w['name']}: {w['script']}.{w['cue']}")
            if args.out:
                Path(args.out).write_text(milestones_md(rows, story_roots(session), session['players']), encoding='utf-8')
                print(f'wrote {args.out}')
    except (MilestoneError, plot_copies.PlotError, OSError, ValueError) as error:
        p.error(str(error))
    return 0


if __name__ == '__main__':
    sys.exit(main())
