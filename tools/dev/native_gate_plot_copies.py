"""Per-player copies of stock plot scripts (the player headquarters plot first).

User decision, 16 Nov: in factions mode every player's faction gets its own headquarters from its own missions, as its own
station object, with its own research. X4 has one headquarters plot per game (md X4Ep1_Mentor_Subscriptions: the signal
leak, the anomaly, Boso Ta, the station made by create_station with a safe position, the research missions), one script
with one state. A start's story is given to one slot by claiming the script's static root cue for it
(native_gate_session_start.py story_patch; lockstep_mission_owners.h 'story'); an instance per player is not possible
(inside an instance a cue name of a branch not started yet is the static cue's). So a second player gets a second
script: a copy of the stock file under another script name, begun and claimed for that slot like a start's story.

A copy is the stock script (the base file with the stock extensions' patches of it applied, as X4 runs it) with three
changes: the script's name (<Script>_P<slot>), the root cue's conditions (its own signal instead of the game start's setup
cue: X4LAN_Story.Begin gives it inside the slot's scope), and the script's references to itself by md name; its other root
cues wait until the root cue has begun, and its debug force names are left to the stock script. A copy for a slot with its
own faction (factions mode: x4lan_player<slot>) also names that faction where the stock script names the player faction:
'faction.player' is a fixed faction in a script, not what a mission scope's player faction is (hq4, 16 Nov: the station
made by slot 2's copy, in slot 2's scope, was the plain player faction's), and 'player.headquarters' becomes that
faction's headquarters. So the station a slot's plot makes is that slot's faction's. References to
it from OTHER stock scripts stay the first script's: for those the plot's state is slot 1's (open: the plots that wait for
the headquarters).

The copy is Egosoft's text, so it is never in git, in an overlay or in a kit: overlays carry the recipe only
(md-copies.json: which file, which slot, and the SHA-256 of each source file and of the result), and every PC's launcher
makes the file from its own installed game (src/lockstep/launcher/gamedata.py's cache) when it composes a client, refusing
sources or a result that are not the session's.

A start's own story is copied the same way (plot id 'story'; the catalog names its file, native_gate_gamestarts.py
Catalog.story): a slot whose start is another slot's or the session base start's plays that start's story as its own
copy, e.g. GS_Terran1_P2, since the stock script is already the other player's (the base start's runs for slot 1 by
X4's module rule). Its root cue loses the module attribute (the session may have another base start) and its other
root cues that are tied to a start by module are left out (game-wide fixes of that start: in GS_Terran1 a relation
patch of old saves at every load, which the stock script runs once for the game when the base start is that start).
A copy never runs the gamestart's setup cues of md.Setup_Gamestarts (they are the game's, run once for the base start;
their world parts are the start's 'world_omitted'): it begins at its own signal instead.

Plots that continue a start's story (PLOTS[...]['follows']; follow_on) are copied for the slot that plays that start's
story, as one set with it (story_entries): each copy of the set names the slot's own scripts of the set where the stock
text names the stock ones ('links': the Terran cadet story GS_Terran1 reads the actors and the wingmate library of
Story_Yaki, and Story_Yaki waits for GS_Terran1's last step), and answers 'player.module' as the slot's start
('module'), the way 'faction.player' becomes the slot's faction: the stock Story_Yaki offers its first mission only
when the game's start is the Terran cadet start and the player is in a Terran sector, and the game's start is the
session base start, player 1's.
"""
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

RECIPE = 'md-copies.json'
RECIPE_FORMAT = 'X4LAN_MD_COPIES_1'
NAME = re.compile(r'[A-Za-z][A-Za-z0-9_]*')
STOCK_EXTENSIONS = ('ego_dlc_boron', 'ego_dlc_mini_01', 'ego_dlc_mini_02', 'ego_dlc_pirate', 'ego_dlc_split', 'ego_dlc_terran',
                    'ego_dlc_timelines')   # the catalog's order (src/lockstep/launcher/gamedata.py)
# The plots a slot can run as its own copy: id -> the stock script (its extension's cache folder, file, script and root cue).
PLOTS = {
    'hq': {'extension': 'base', 'file': 'x4ep1_mentor_subscription.xml', 'script': 'X4Ep1_Mentor_Subscriptions', 'cue': 'Start',
           'what': 'the player headquarters plot (signal leak, anomaly, Boso Ta, the station, research)',
           # the stock script's own debug cues, what the diagnostic native_gate_plot_debug signals in the first script for
           # slot 1 and in a slot's copy: the one that makes the station (create_station, owner faction.player, safepos),
           # then the one that begins the plot's research phase (the code of the d-th cue is 1000*(d-1) higher), then
           # the script's shortcut to the phase in which the player flies to the station and it becomes theirs
           'debug': ('Debug_Create_HQ', 'Debug_Unlock_Research', 'SHORTCUT_Claiming_The_HQ')},
    # The same plot for a player who begins with the headquarters (a start that comes with one): the stock script has its
    # own path for that, taken by the game's custom starts with the headquarters story done. Its root cue reads the
    # start's story state into a variable; the copy sets that variable ('set'), so the copy's first step finds or makes
    # the slot's faction's headquarters at a safe position, with the mentor and research unlocked, and never begins
    # the signal leak. Player 1 has no copy (the stock script is its plot, begun with the game): 'first' names the
    # stock script's own cues that bring it there: signalled at once, signalled as their own frame after that, and the
    # phase that is over then (the script itself handles that phase's cancelling).
    'hq_ready': {'extension': 'base', 'file': 'x4ep1_mentor_subscription.xml', 'script': 'X4Ep1_Mentor_Subscriptions', 'cue': 'Start',
                 'what': 'the player headquarters plot begun with the headquarters there (the mentor, research unlocked)',
                 'set': {'$story_hq_boso': 'true'},
                 'first': {'instantly': ('Debug_Create_HQ_With_Boso',), 'signal': ('Debug_Unlock_Research',),
                           'cancel': ('Phase_SignalLeak',)},
                 'debug': ('Debug_Create_HQ', 'Debug_Unlock_Research', 'SHORTCUT_Claiming_The_HQ')},
    # The Terran story after the Terran cadet start's story (ego_dlc_terran): the stock script begins with every game in
    # the main galaxy and is activated by the cadet story's last step (for the cadet) or by Terran relations (for
    # anyone else); the cadet story reads its actors and its wingmate library. A slot that plays the cadet story gets it
    # as its own copy by default (x4ls new-session --story K; --omit K=yaki leaves it out).
    'yaki': {'extension': 'ego_dlc_terran', 'file': 'story_yaki.xml', 'script': 'Story_Yaki', 'cue': 'Start',
             'what': "the Terran story after the Terran cadet start's (Mission Command, the Yaki, the wingmate)",
             'follows': 'x4ep1_gamestart_terran1',
             # the stock script's own step that activates the story (what the cadet story's end signals there), for a
             # diagnosis without playing the cadet story through: its offer comes at the player's next Terran sector;
             # then its own wingmate setup (the wingmate made or brought to the player's ship, protecting it), for the
             # wingmate's tests (native_gate_player_escort.xml; script ops escort_look, escort_target, escort_colour)
             'debug': ('Activate_Story', 'DEBUG_Setup_Wingmate')},
}
STORY = 'story'   # the plot id of a start's own story played as a slot's copy (the catalog's start script)
STORY_FILE = re.compile(r'gs_[a-z0-9_]+\.xml')
START = re.compile(r'[a-z][a-z0-9_]*')   # a gamestart id
MODULE = re.compile(r'(?<![A-Za-z0-9_$.])player\.module(?![A-Za-z0-9_])')
HEADER = ("<!-- Made on this PC by tools/dev/native_gate_plot_copies.py from the installed game's own files for one session: "
          "never in git, an overlay or a kit. -->\n")


class PlotError(ValueError):
    pass


def copy_name(script, slot):
    return f'{script}_P{int(slot)}'


def target(plot_id, slot):
    return f'md/x4lan_plot_{plot_id}_p{int(slot)}.xml'


def sha256_text(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def stock_tree(root, plot):
    """The stock script as X4 runs it on this PC: the base file with every stock extension's patch of it applied in the
    catalog's order (the pirate DLC patches the headquarters plot). Returns (tree, [[extension, sha256 of its file]])."""
    import native_gate_gamestarts as gamestarts
    path = Path(root) / plot['extension'] / 'md' / plot['file']
    if not path.is_file():
        raise PlotError(f'{path}: the game file is not in the unpacked game data')
    data = path.read_bytes()
    sources = [[plot['extension'], hashlib.sha256(data).hexdigest()]]
    try:
        tree = ET.fromstring(data)
    except ET.ParseError as error:
        raise PlotError(f'{path}: not XML ({error})') from None
    for extension in STOCK_EXTENSIONS if plot['extension'] == 'base' else ():
        patch = Path(root) / extension / 'md' / plot['file']
        if not patch.is_file():
            continue
        data = patch.read_bytes()
        try:
            diff = ET.fromstring(data)
        except ET.ParseError as error:
            raise PlotError(f'{patch}: not XML ({error})') from None
        if diff.tag != 'diff':
            raise PlotError(f'{patch}: an extension replaces {plot["file"]} instead of patching it')
        try:
            gamestarts.apply_diff(tree, diff, str(patch))
        except gamestarts.CatalogError as error:
            raise PlotError(str(error)) from None
        sources.append([extension, hashlib.sha256(data).hexdigest()])
    return tree, sources


FACTION = re.compile(r'x4lan_player[0-9]+')


def outside_strings(value, change):
    """value with change() applied to its parts outside MD's single-quoted string literals (a name inside a debug text
    stays text)."""
    parts = value.split("'")
    return "'".join(change(part) if i % 2 == 0 else part for i, part in enumerate(parts))


def check_links(links, script, slot):
    """An entry's links: {stock script: the slot's copy of it}, other scripts of the slot's copy set only."""
    if links is None:
        return {}
    if not (isinstance(links, dict) and all(isinstance(k, str) and NAME.fullmatch(k) and k != script and
                                            v == copy_name(k, slot) for k, v in links.items())):
        raise PlotError(f'{script}: links {links!r} name no copies of slot {slot}')
    return dict(sorted(links.items()))


def copy_text(tree, script, cue, slot, faction=None, sets=None, module=None, links=None):
    """The copy of a stock script (its patched tree; changed in place) for one slot, as text; refuses another shape.
    faction: the slot's own faction (factions mode), written where the stock script names the player faction.
    sets: {variable: value} the copy's root cue sets instead of what the stock root cue sets them to (a plot's 'set').
    module: the slot's start: the copy's 'player.module' is that start's id (a test of the game's start answered as the
    slot's). links: {stock script: the slot's copy}: the other scripts of the slot's copy set, named so in the copy."""
    if faction is not None and not FACTION.fullmatch(faction):
        raise PlotError(f'copy for faction {faction!r}')
    if not (NAME.fullmatch(script or '') and NAME.fullmatch(cue or '') and type(slot) is int and 2 <= slot <= 8):
        raise PlotError(f'copy of {script!r}.{cue!r} for slot {slot!r}')
    if module is not None and not (isinstance(module, str) and START.fullmatch(module)):
        raise PlotError(f'copy answering player.module as {module!r}')
    links = check_links(links, script, slot)
    cues = tree.find('cues')
    if tree.tag != 'mdscript' or tree.get('name') != script or cues is None:
        raise PlotError(f'not the MD script {script}')
    roots = [c for c in cues.findall('cue') if c.get('name') == cue]
    if len(roots) != 1 or roots[0].get('instantiate') == 'true' or roots[0].find('conditions') is None:
        raise PlotError(f'{script}: needs exactly one static root cue {cue} with conditions')
    new = copy_name(script, slot)
    own = re.compile(r'\bmd\.' + re.escape(script) + r'(?![A-Za-z0-9_])')
    player_faction = re.compile(r'(?<![A-Za-z0-9_$.])faction\.player(?![A-Za-z0-9_])')
    headquarters = re.compile(r'(?<![A-Za-z0-9_$.])player\.headquarters(?![A-Za-z0-9_])')

    linked = [(re.compile(r'\bmd\.' + re.escape(stock) + r'(?![A-Za-z0-9_])'), 'md.' + mine) for stock, mine in links.items()]

    def rewrite(value):
        value = own.sub('md.' + new, value)
        for pattern, name in linked:
            value = pattern.sub(name, value)
        if faction:
            value = player_faction.sub('faction.' + faction, value)
            value = headquarters.sub(f'faction.{faction}.headquarters', value)
        if module:
            value = outside_strings(value, lambda part: MODULE.sub(f"'{module}'", part))
        return value
    for element in tree.iter():
        for key, value in list(element.attrib.items()):
            if 'md.' in value or 'player' in value:
                element.set(key, rewrite(value))
        if element.text and ('md.' in element.text or 'player' in element.text):
            element.text = rewrite(element.text)
    actions = roots[0].find('actions')
    for name, value in sorted((sets or {}).items()):
        found = [a for a in (list(actions) if actions is not None else []) if a.tag == 'set_value' and a.get('name') == name]
        if len(found) != 1 or 'exact' not in found[0].attrib:
            raise PlotError(f'{script}: the root cue {cue} does not set {name} once')
        found[0].set('exact', value)
    # The head as our own generated MD has it (the schema attributes written plainly).
    tree.attrib.clear()
    tree.attrib.update({'name': new, 'xmlns:xsi': 'http://www.w3.org/2001/XMLSchema-instance', 'xsi:noNamespaceSchemaLocation': 'md.xsd'})
    conditions = roots[0].find('conditions')
    for child in list(conditions):
        conditions.remove(child)
    ET.SubElement(conditions, 'event_cue_signalled')
    # A start's script runs only in a game of that start (module=): the copy runs in a session of any base start, begun
    # by its signal. The script's other root cues tied to a start are that start's game-wide work (GS_Terran1: a relation
    # fix of old saves at every load), the stock script's when the base start is that start: the copy has none.
    roots[0].attrib.pop('module', None)
    for other in cues.findall('cue'):
        if other is not roots[0] and 'module' in other.attrib:
            cues.remove(other)
    # The script's other root cues (its patches at a game load, its listeners) take for granted that the root cue has run,
    # as it has in the stock script from the game's first frame: here they wait until the copy has begun (hq1, 16 Nov: the
    # copy's Patch_Userdata ran at the load, before its begin, and read a table the root cue had not made yet).
    for other in cues.findall('cue'):
        if other is not roots[0] and other.find('conditions') is not None:
            ET.SubElement(other.find('conditions'), 'check_value', {'value': f'{cue}.state != cuestate.waiting'})
    # Debug force names are one per game: the stock script keeps them (X4 warns about every second use of a name).
    for parent in tree.iter():
        for force in parent.findall('force'):
            parent.remove(force)
    return '<?xml version="1.0" encoding="utf-8"?>\n' + HEADER + ET.tostring(tree, encoding='unicode') + '\n'


def plot_of(e):
    """The stock script an entry copies (extension, file, script, cue, and its plot's other fields), or None: a plot of
    PLOTS, or a start's own story (id 'story'), whose start script the entry names (a gs_*.xml of a stock extension)."""
    if not isinstance(e, dict):
        return None
    if e.get('id') != STORY:
        return PLOTS.get(e.get('id'))
    ok = ((e.get('extension') == 'base' or e.get('extension') in STOCK_EXTENSIONS) and isinstance(e.get('file'), str) and
          STORY_FILE.fullmatch(e['file']) and isinstance(e.get('script'), str) and NAME.fullmatch(e['script']) and
          isinstance(e.get('cue'), str) and NAME.fullmatch(e['cue']))
    return {k: e[k] for k in ('extension', 'file', 'script', 'cue')} if ok else None


def _entry(plot_id, plot, slot, root, faction=None, module=None, links=None):
    tree, sources = stock_tree(root, plot)
    out = {'id': plot_id, 'slot': int(slot), 'extension': plot['extension'], 'file': plot['file'], 'script': plot['script'],
           'cue': plot['cue'], 'copy': copy_name(plot['script'], slot), 'target': target(plot_id, slot), 'sources': sources,
           'faction': faction}
    if module is not None:   # entries without these fields (made before 24 Nov) are copied as they were
        out['module'] = module
    if links:
        out['links'] = check_links(links, plot['script'], int(slot))
    out['sha256'] = sha256_text(copy_text(tree, out['script'], out['cue'], out['slot'], faction, plot.get('set'), module, links))
    return out


def entry(plot_id, slot, root, faction=None, module=None, links=None):
    """The recipe entry of plot `plot_id` for `slot`, made from this PC's unpacked game data (root: gamedata's cache).
    faction: the slot's own faction in factions mode; module and links: see copy_text."""
    plot = PLOTS.get(plot_id)
    if plot is None:
        raise PlotError(f'unknown plot {plot_id!r} (known: {", ".join(sorted(PLOTS))})')
    if plot.get('follows') and module != plot['follows']:
        raise PlotError(f"plot {plot_id} continues the story of {plot['follows']}: it is copied with that story (story_entries)")
    return _entry(plot_id, plot, slot, root, faction, module, links)


def follow_on(start):
    """The plots that continue the story of `start` (PLOTS[...]['follows']), in id order."""
    return sorted(pid for pid, plot in PLOTS.items() if plot.get('follows') == start)


def story_entries(story, start, slot, root, faction=None, follow=()):
    """The entries of a slot that plays the story of `start` (story: the catalog's start script) as its own copy, and the
    plots of `follow` that continue it (follow_on), as one set: every copy names the slot's own scripts of the set
    where the stock text names the stock ones (Story_Yaki_P2 waits for GS_Terran1_P2's last step, GS_Terran1_P2 reads
    Story_Yaki_P2's actors), and answers player.module as `start`. The story first, then the plots in the given order
    (the order the story MD begins them in, within one frame)."""
    plot = plot_of(dict(story or {}, id=STORY))
    if plot is None or not START.fullmatch(start or ''):
        raise PlotError(f'not a start script of {start!r}: {story!r}')
    unknown = [pid for pid in follow if pid not in follow_on(start)]
    if unknown:
        raise PlotError(f'{", ".join(unknown)}: no plot that continues the story of {start}')
    names = {plot['script']: copy_name(plot['script'], slot)}
    names.update({PLOTS[pid]['script']: copy_name(PLOTS[pid]['script'], slot) for pid in follow})
    if len(names) != 1 + len(follow):
        raise PlotError(f'slot {slot}: two copies of one script')

    def links(script):
        return {s: n for s, n in names.items() if s != script}
    return ([_entry(STORY, plot, slot, root, faction, start, links(plot['script']))] +
            [_entry(pid, PLOTS[pid], slot, root, faction, start, links(PLOTS[pid]['script'])) for pid in follow])


FIRST_STEPS = ('instantly', 'signal', 'cancel')


def first_entry(plot_id, root):
    """Plot `plot_id` for player 1, whose plot is the stock script: the script's own cues that bring it to the plot's
    result (PLOTS[...]['first']), checked against this PC's game data. No game text: cue names only."""
    plot = PLOTS.get(plot_id)
    if plot is None or not plot.get('first'):
        known = ', '.join(sorted(k for k, v in PLOTS.items() if v.get('first')))
        raise PlotError(f"plot {plot_id!r} has no form for player 1, whose plot is the stock script (known: {known})")
    tree, _ = stock_tree(root, plot)
    names = {c.get('name') for c in tree.iter('cue')}
    missing = sorted(n for step in FIRST_STEPS for n in plot['first'].get(step, ()) if n not in names)
    if tree.get('name') != plot['script'] or missing:
        raise PlotError(f"{plot['file']}: not the script {plot['script']} with the cues {missing or ''}")
    return dict({'id': plot_id, 'script': plot['script']}, **{step: list(plot['first'].get(step, ())) for step in FIRST_STEPS})


def first_plots(data):
    """The checked 'first_plots' of a spec or a session: player 1's plots begun with their result, in the given order."""
    entries = (data or {}).get('first_plots') or []
    for e in entries:
        plot = PLOTS.get(e.get('id')) if isinstance(e, dict) else None
        if not (plot and plot.get('first') and e.get('script') == plot['script'] and
                all(e.get(step) == list(plot['first'].get(step, ())) for step in FIRST_STEPS)):
            raise PlotError(f'not a first plot entry: {e!r}')
    if len({e['script'] for e in entries}) != len(entries):
        raise PlotError('player 1: two plots of one script')
    return list(entries)


def check_entry(e):
    plot = plot_of(e)
    ok = (plot is not None and type(e.get('slot')) is int and 2 <= e['slot'] <= 8 and
          all(e.get(k) == plot[k] for k in ('extension', 'file', 'script', 'cue')) and
          e.get('copy') == copy_name(plot['script'], e['slot']) and e.get('target') == target(e['id'], e['slot']) and
          e.get('faction') in (None, f"x4lan_player{e['slot']}") and
          (e.get('module') is None or (isinstance(e['module'], str) and START.fullmatch(e['module']))) and
          (not plot.get('follows') or e.get('module') == plot['follows']) and
          (e['id'] != STORY or e.get('module') is not None) and
          re.fullmatch(r'[0-9a-f]{64}', e.get('sha256') or '') and isinstance(e.get('sources'), list) and e['sources'] and
          all(isinstance(x, list) and len(x) == 2 and isinstance(x[0], str) and (x[0] == 'base' or x[0] in STOCK_EXTENSIONS) and
              re.fullmatch(r'[0-9a-f]{64}', x[1] or '') for x in e['sources']))
    if ok:
        try:
            check_links(e.get('links'), plot['script'], e['slot'])
        except PlotError:
            ok = False
    if not ok:
        raise PlotError(f'not a plot copy entry: {e!r}')
    return e


def slot_plots(slots):
    """{slot: [entries]} of a spec's or a session's slots that run their own plot copies, in slot order."""
    found = {}
    for k, slot in sorted((slots or {}).items(), key=lambda item: int(item[0])):
        entries = [check_entry(e) for e in (slot.get('plots') or [])]
        if any(e['slot'] != int(k) for e in entries) or len({e['script'] for e in entries}) != len(entries):
            raise PlotError(f'slot {k}: its plot copies name another slot or one script twice')
        stories = [e for e in entries if e['id'] == STORY]
        if len(stories) > 1 or (stories and slot.get('story')):
            raise PlotError(f"slot {k}: its start's story both as the stock script and as a copy, or two of them")
        mine = {e['script'] for e in entries}
        if any(set(e.get('links') or {}) - mine for e in entries):
            raise PlotError(f'slot {k}: a copy links a script the slot has no copy of')
        if entries:
            found[int(k)] = entries
    return found


def recipe_text(plots):
    """md-copies.json of an overlay: the entries of every slot (no game text)."""
    entries = [e for _, group in sorted(plots.items()) for e in group]
    return json.dumps({'format': RECIPE_FORMAT, 'copies': entries}, indent=1, sort_keys=True) + '\n'


def read_recipe(overlay):
    path = Path(overlay) / RECIPE
    if not path.is_file():
        return []
    data = json.loads(path.read_text(encoding='utf-8'))
    if not (isinstance(data, dict) and data.get('format') == RECIPE_FORMAT and isinstance(data.get('copies'), list)):
        raise PlotError(f'{path}: not a plot copy recipe')
    return [check_entry(e) for e in data['copies']]


def generate(entries, root):
    """{target path inside the extension: text} of the copies, made from this PC's game data; refuses game files or a
    result that are not the ones the session's recipe names (another game version, another set of DLCs, a mod that changes
    the plot, another launcher version)."""
    files = {}
    for e in entries:
        check_entry(e)
        plot = plot_of(e)
        tree, sources = stock_tree(root, plot)
        if sources != e['sources']:
            raise PlotError(f"{e['file']}: this PC's game files are not the session's (another game version or set of DLCs: "
                            f"{[x[0] for x in sources]} here, {[x[0] for x in e['sources']]} in the session)")
        made = copy_text(tree, e['script'], e['cue'], e['slot'], e.get('faction'), plot.get('set'), e.get('module'), e.get('links'))
        if sha256_text(made) != e['sha256']:
            raise PlotError(f"{e['file']}: the copy made on this PC is not the session's (another launcher version)")
        files[e['target']] = made
    return files


def write(files, extension_dir):
    """Write generate()'s files into a composed client extension (never over a file)."""
    for name, text in sorted(files.items()):
        path = Path(extension_dir) / name
        if path.exists():
            raise PlotError(f'{path} exists; a plot copy never replaces a file')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode('utf-8'))
    return sorted(files)
