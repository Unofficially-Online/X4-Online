"""native_gate_figure_macros.py: the walking players' figures look like the players.

X4 has no body for a player that walks (its own player is seen from the inside), so every walking player gets a
figure on the other clients: an NPC placed where the player stands (tools/dev/native_gate_ui_replay.xml, cue Avatar).
That NPC was a crew character picked at random. A player has a character of its own, though: the game start's player
macro (libraries/character_macros.xml, class "player": a head and a torso model, a race, a name). The stock game
shows such a character as an NPC by a second macro of class "npc" with the NPC component of the same body and the
same models (its own example: character_player_timelines_hub_argon_male_npc_clone_macro beside
character_player_timelines_hub_argon_male_macro).

This module writes that second macro for every player macro of the installed game, on this PC, from its own game
files (the launcher's unpacked library cache): <extension>/libraries/character_macros.xml with one macro
x4lan_figure_<player macro> each. Nothing of the game's files is copied into the repository or a kit; the file holds
names and model paths the game already has. The cue makes a player's figure from the macro named after that player's
own macro and falls back to a crew character when there is none (a modded start, a player macro without models).

    generate(root)          the file's text from an unpacked library cache ('' when there is nothing to write)
    write(text, extension)  into a composed client extension (never over a file)
"""
import re
from pathlib import Path

PREFIX = 'x4lan_figure_'
TARGET = 'libraries/character_macros.xml'
MACRO = re.compile(r'<macro\b([^>]*)>(.*?)</macro>', re.S)
COMPONENT = re.compile(r'<component\b([^>]*?)/?>')
ATTRIBUTE = re.compile(r'([\w:]+)\s*=\s*"([^"]*)"')
COMMENT = re.compile(r'<!--.*?-->', re.S)
KEPT = ('name', 'race', 'female', 'page')      # of <identification>: not the faction, not the briefing image


def attributes(text):
    return dict(ATTRIBUTE.findall(text))


def libraries(root, name):
    """The texts of libraries/<name> of the base game and every stock extension in the cache, base first."""
    root = Path(root)
    folders = sorted((p for p in root.iterdir() if p.is_dir()), key=lambda p: (p.name != 'base', p.name)) if root.is_dir() else []
    out = []
    for folder in folders:
        path = folder / 'libraries' / name
        if path.is_file():
            out.append(COMMENT.sub('', path.read_text(encoding='utf-8', errors='replace')))
    return out


def npc_components(root):
    names = set()
    for text in libraries(root, 'character_components.xml'):
        for found in COMPONENT.finditer(text):
            a = attributes(found.group(1))
            if a.get('class') == 'npc' and a.get('name'):
                names.add(a['name'])
    return names


def clones(root):
    """{figure macro name: its XML} for every player macro that has a body an NPC can wear."""
    npc = npc_components(root)
    out = {}
    for text in libraries(root, 'character_macros.xml'):
        for found in MACRO.finditer(text):
            head, body = attributes(found.group(1)), found.group(2)
            if head.get('class') != 'player' or not head.get('name'):
                continue
            component = re.search(r'<component\b[^>]*\bref="([^"]+)"', body)
            identification = re.search(r'<identification\b([^>]*?)/?>', body)
            models = re.findall(r'<model\b([^>]*?)/?>', body)
            if not component or not component.group(1).startswith('character_player_'):
                continue
            body_name = 'character_' + component.group(1)[len('character_player_'):]
            if body_name not in npc:
                continue
            lines = ['  <macro name="%s%s" class="npc"%s>' % (PREFIX, head['name'], ' ref="%s"' % head['ref'] if head.get('ref') else ''),
                     '    <component ref="%s" />' % body_name, '    <properties>']
            if identification:
                a = attributes(identification.group(1))
                kept = ' '.join('%s="%s"' % (k, a[k]) for k in KEPT if k in a)
                if kept:
                    lines.append('      <identification %s />' % kept)
            shown = [attributes(m) for m in models]
            shown = [m for m in shown if m.get('type') and m.get('ref')]
            if shown:
                lines.append('      <models>')
                lines += ['        <model type="%s" ref="%s" />' % (m['type'], m['ref']) for m in shown]
                lines.append('      </models>')
            elif not head.get('ref'):
                continue                      # neither models of its own nor a macro to take them from
            lines += ['    </properties>', '  </macro>']
            out[PREFIX + head['name']] = '\n'.join(lines)      # a later library (an extension) replaces an earlier one
    return out


def generate(root):
    made = clones(root)
    if not made:
        return ''
    return ('<?xml version="1.0" encoding="utf-8"?>\n<!-- Made on this PC from its own game files by tools/dev/native_gate_figure_macros.py: '
            'an NPC twin of every player character, for the walking players\' figures. -->\n<macros>\n' +
            '\n'.join(made[name] for name in sorted(made)) + '\n</macros>\n')


def write(text, extension_dir):
    """Into a composed client extension; never over a file. Returns the path written, or None for nothing."""
    if not text:
        return None
    path = Path(extension_dir) / TARGET
    if path.exists():
        raise SystemExit(f'{path} exists; the figure macros never replace a file')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode('utf-8'))
    return path


if __name__ == '__main__':
    import sys
    sys.stdout.write(generate(sys.argv[1]))
