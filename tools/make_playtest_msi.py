"""A kit as a Windows installer: python tools/make_playtest_msi.py --kit KIT --out FILE.msi

The .msi installs a kit folder for the current user, without administrator rights:

  %LOCALAPPDATA%\\<name>\\        the kit: repo, overlay, snapshot, PLAY.cmd, and python\\ when the kit bundles one
  Desktop and Start menu          a shortcut "<name>" that runs PLAY.cmd

A kit with a server and a bundled Python is installed for "Play in Steam" instead (lockstep/launcher/steamplay.py): the
installer starts the kit's helper, registers it to start with Windows (HKCU ...\\Run) and puts one Start menu entry
there that starts it again. The helper puts the Multiplayer entry into X4. A removal stops the helper and takes the
entry out of X4.

Two kinds of kit, two products (a newer installer replaces an older one of the same product only):

- A playtest kit (tools/make_playtest_kit.py), "X4 Multiplayer Playtest". Built for a kit with a server and a bundled
  Python (make_playtest_kit.py --server ADDRESS --python-embed ZIP): the player installs, starts the shortcut and
  chooses Multiplayer, Join in X4's main menu. Nothing else is needed but X4.
- A release kit (tools/make_release_kit.py, it has release.json), "X4 Online": always for "Play in Steam", so it needs
  its bundled Python (--python-embed). Its version comes from release.json's build time. The kit must match its
  manifest and hold nothing private (make_release_kit.verify and private_files), or no installer is made. Its
  ready-made host sessions (make_release_kit.py --sessions, --prebuild-sessions: the manifest's files under sessions/)
  are installed; sessions the kit folder made or downloaded while playing are not. Start menu: "X4 Online" (the
  helper), "X4 Online (play)" (X4 in its multiplayer profile on the Multiplayer page: no restart to host or join) and
  "X4 Online (second window)"; on the desktop "X4 Online" (as "(play)"). When the install ends, X4 opens on the
  Multiplayer page in its multiplayer profile (through Explorer and the installed shortcut "X4 Online (first start)",
  helper.pyw --play --first: nothing opens when an X4 runs already). An uninstall (not an upgrade) also takes out what
  X4 Online put into Steam (its library entry, X4's launch options put back) and the game views (helper.pyw --remove
  --uninstall).

- Windows shows the installer's progress bar only (no pages to click through).
  Removing it (Settings, Apps) leaves the run folders the games made in repo\\.local.
- The file is not signed, so Windows may warn about an unknown publisher when it was downloaded.
- This tool runs on Windows with Python 3.12 or older: it uses msilib, which left the standard library in 3.13.
- The kit's files are installed as they are: nothing of X4 is in a kit, and a playtest kit's server.json (the
  server's address and key) is part of the installer, so it goes to the players only."""
import argparse
import json
import os
import sys
import time
import uuid
import warnings
from datetime import datetime, timezone
from pathlib import Path
from typing import NamedTuple

UPGRADE_CODE = '{5E0C2F1B-6A4D-4C8E-9B7F-3D2A1C0E9F84}'   # the same for every playtest kit: a new installer replaces the old one
NAME = 'X4 Multiplayer Playtest'
RELEASE_UPGRADE_CODE = '{8D3B6E2A-41C7-4F95-A0E8-6B19C2D74F31}'   # the same for every release; never a playtest kit's
RELEASE_NAME = 'X4 Online'
BS = chr(92)
RUN_KEY = 'Software' + BS + 'Microsoft' + BS + 'Windows' + BS + 'CurrentVersion' + BS + 'Run'
NOT_INSTALLED = ('__pycache__', '.local')            # folders a PC makes while playing: caches, run folders
NOT_INSTALLED_RELEASE = NOT_INSTALLED + ('sessions',)   # ... and a release kit's session cache
MISSING_DIALOGS = ('ExitDialog', 'FatalError', 'UserExit')   # named by msilib's sequences but not defined


class Product(NamedTuple):
    """What an installer is called and where it puts things."""
    name: str               # the install folder, the Start menu entry, Apps
    upgrade_code: str
    manufacturer: str
    feature_title: str
    short_name: str         # the 8.3 name of the install folder and of the Start menu entry
    second_short_name: str  # ... of the "(second window)" entry
    run_value: str          # the helper's value under HKCU ...\Run
    comments: str           # Apps: which kit or release this is
    not_installed: tuple    # folder names whose files are left out
    listed: frozenset = frozenset()   # a release's ready-made sessions: the manifest's files under sessions/, installed
    play_short_name: str = 'X4ONPLAY'  # ... of a release's "(play)" entry


def version_now(now=None):
    """Installer versions are major.minor.build (255.255.65535): year, month, minute of the month. Later is higher."""
    now = now or time.gmtime()
    return f'{now.tm_year % 100}.{now.tm_mon}.{(now.tm_mday - 1) * 1440 + now.tm_hour * 60 + now.tm_min}'


def version_of_release(built):
    """A release's installer version: version_now of release.json's build time (ISO 8601), so it is the same for every
    installer made from one kit."""
    moment = datetime.fromisoformat(built.replace('Z', '+00:00'))
    if moment.tzinfo is None:
        moment = moment.replace(tzinfo=timezone.utc)
    return version_now(moment.astimezone(timezone.utc).timetuple())


def load_msilib():
    try:
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', DeprecationWarning)
            import msilib
            from msilib import schema, sequence
    except ImportError:
        raise SystemExit('this tool needs Windows and Python 3.12 or older (msilib left the standard library in 3.13)')
    return msilib, schema, sequence


def release_tools():
    """tools/make_release_kit.py (beside this file): the release kit's manifest check and private-file rules."""
    tools = str(Path(__file__).resolve().parent)
    if tools not in sys.path:
        sys.path.insert(0, tools)
    import make_release_kit
    return make_release_kit


def playtest_product(kit, name):
    if not (kit / 'kit-manifest.json').is_file() or not (kit / 'PLAY.cmd').is_file():
        raise SystemExit(f'{kit} is not a kit with PLAY.cmd (tools/make_playtest_kit.py, an overlay without open slots)')
    manifest = json.loads((kit / 'kit-manifest.json').read_text(encoding='utf-8'))
    product = Product(name=name or NAME, upgrade_code=UPGRADE_CODE, manufacturer='X4 lockstep playtest',
                      feature_title='The playtest kit', short_name='X4MPPLAY', second_short_name='X4MPSEC',
                      run_value='X4MultiplayerPlaytest', comments=f'kit {manifest["commit"][:12]}',
                      not_installed=NOT_INSTALLED)
    return product, None


def release_product(kit, name):
    """A release kit's product; refused unless the kit is whole, unchanged, private-free and has its own Python."""
    tools = release_tools()
    if not (kit / 'kit-manifest.json').is_file():
        raise SystemExit(f'{kit} has release.json but no kit-manifest.json (tools/make_release_kit.py)')
    release = json.loads((kit / 'release.json').read_text(encoding='utf-8'))
    if not tools.RELEASE_PATTERN.fullmatch(str(release.get('release', ''))):
        raise SystemExit(f'{kit}: release.json has no valid release ({release.get("release")!r})')
    try:
        version_of_release(str(release.get('built')))
    except ValueError:
        raise SystemExit(f'{kit}: release.json has no valid build time ({release.get("built")!r})')
    if not (kit / 'helper.pyw').is_file() or not (kit / 'python' / 'pythonw.exe').is_file():
        raise SystemExit(f'{kit}: an installer of a release needs its helper and its own Python '
                         '(make_release_kit.py --python-embed)')
    _, problems = tools.verify(kit)
    if problems:
        raise SystemExit(f'{kit} does not match its manifest ({len(problems)} differences): ' + '; '.join(problems[:10]))
    found = tools.private_files(kit, skip=NOT_INSTALLED_RELEASE)
    manifest = json.loads((kit / 'kit-manifest.json').read_text(encoding='utf-8'))
    listed = frozenset(path for path in manifest.get('files', {}) if path.startswith(tools.SESSIONS + '/'))
    found += [(path, why) for path in sorted(listed) if (why := tools.shipped_session_reason(kit, path))]
    if found:
        raise SystemExit(f'{kit} holds private files: ' + '; '.join(f'{path} ({why})' for path, why in found[:10]))
    product = Product(name=name or RELEASE_NAME, upgrade_code=RELEASE_UPGRADE_CODE, manufacturer='X4 Online',
                      feature_title='The X4 Online release', short_name='X4ONLINE', second_short_name='X4ONSEC',
                      run_value='X4Online', comments=f'release {release["release"]}',
                      not_installed=NOT_INSTALLED_RELEASE, listed=listed)
    return product, release


def add_sequences(msilib, sequence, db, helper, release_kit=False):
    """The standard actions, without the dialogs msilib names but does not define."""
    for table in sequence.tables:
        rows = [row for row in getattr(sequence, table) if row[0] not in MISSING_DIALOGS]
        if table == 'InstallExecuteSequence':
            # an older kit goes before the new one comes (its removal stops its helper and takes its menu extension out)
            rows = [(action, condition, 1450 if action == 'RemoveExistingProducts' else number)
                    for action, condition, number in rows]
            if helper and release_kit:
                # an upgrade's removal only stops the old helper; a real uninstall also takes X4 Online out of Steam and
                # removes the game views. The last step of an install opens X4 on the Multiplayer page.
                rows += [('StopHelper', 'REMOVE="ALL" AND UPGRADINGPRODUCTCODE', 1600),
                         ('UninstallHelper', 'REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE', 1601),
                         ('StartHelper', 'NOT REMOVE', 6650), ('StartPlay', 'NOT REMOVE', 6660)]
            elif helper:
                rows += [('StopHelper', 'REMOVE="ALL"', 1600), ('StartHelper', 'NOT REMOVE', 6650)]
        msilib.add_data(db, table, rows)


FIRST_START = 'X4 Online (first start)'   # the installed shortcut the last step of an install opens (helper.pyw --play --first)


def add_helper_actions(msilib, db, product, run_helper, release_kit=False):
    msilib.add_data(db, 'CustomAction', [
        ('StopHelper', 34 | 0x40, 'INSTALLDIR', run_helper + ' --remove'),    # waits for it; its result does not matter
        # Started and left running, through Explorer and the Start menu entry: what the installer starts itself
        # inherits its redirection guard and cannot follow the folder links of a game view (Win32 error 448 in X4).
        ('StartHelper', 34 | 0xC0, 'INSTALLDIR',
         '"[WindowsFolder]explorer.exe" "[ProgramMenuFolder]' + product.name + '.lnk"'),
    ])
    if release_kit:
        msilib.add_data(db, 'CustomAction', [
            ('UninstallHelper', 34 | 0x40, 'INSTALLDIR', run_helper + ' --remove --uninstall'),
            # X4 on the Multiplayer page, as the installer's last step: through Explorer and an installed shortcut, so
            # that neither X4 nor its launcher is a child of the installer (the redirection guard, as StartHelper)
            ('StartPlay', 34 | 0xC0, 'INSTALLDIR', '"[WindowsFolder]explorer.exe" "[INSTALLDIR]' + FIRST_START + '.lnk"'),
        ])


def add_properties(msilib, db, product):
    msilib.add_data(db, 'Property', [
        ('UpgradeCode', product.upgrade_code), ('ALLUSERS', '2'), ('MSIINSTALLPERUSER', '1'),   # for this user, no elevation
        ('LIMITUI', '1'),                                                                       # the progress bar, no pages
        ('ARPNOMODIFY', '1'), ('ARPNOREPAIR', '1'), ('SecureCustomProperties', 'OLDPRODUCTS'),
        ('ARPCOMMENTS', product.comments),
    ])
    # any other installer of this product goes
    msilib.add_data(db, 'Upgrade', [(product.upgrade_code, '0.0.0', None, None, 256, None, 'OLDPRODUCTS')])
    summary = db.GetSummaryInformation(20)
    summary.SetProperty(msilib.PID_WORDCOUNT, 2 | 8)   # compressed; 8: no administrator rights needed
    summary.SetProperty(msilib.PID_SUBJECT, product.name)
    summary.Persist()


def add_shortcuts(msilib, db, kit, product, install, helper, run_helper, release_kit=False):
    msilib.add_data(db, 'Directory', [('DesktopFolder', 'TARGETDIR', 'Desktop'), ('ProgramMenuFolder', 'TARGETDIR', 'Programs')])
    short = product.short_name + '|' + product.name
    pythonw = '[INSTALLDIR]python' + BS + 'pythonw.exe'
    if helper:   # the player presses Play in Steam; the helper starts with Windows, and from the Start menu when it was ended
        msilib.add_data(db, 'Registry', [('HelperRun', 1, RUN_KEY, product.run_value, run_helper + ' --quiet',
                                          install.component)])
        msilib.add_data(db, 'Shortcut', [
            ('HelperMenu', 'ProgramMenuFolder', short, install.component, pythonw,
             '"[INSTALLDIR]helper.pyw"', 'Start the multiplayer helper (then press Play in Steam)', None, None, None, 1, 'INSTALLDIR')])
        if release_kit:   # X4 in its multiplayer profile on the Multiplayer page: host or join there without a restart
            play = '"[INSTALLDIR]helper.pyw" --play'
            msilib.add_data(db, 'Shortcut', [
                ('PlayProfileMenu', 'ProgramMenuFolder', product.play_short_name + '|' + product.name + ' (play)',
                 install.component, pythonw, play, 'X4 in its multiplayer profile, on the Multiplayer page', None, None,
                 None, 1, 'INSTALLDIR'),
                ('PlayOnlineDesktop', 'DesktopFolder', short, install.component, pythonw, play,
                 'X4 in its multiplayer profile, on the Multiplayer page', None, None, None, 1, 'INSTALLDIR'),
                ('FirstStart', 'INSTALLDIR', 'X4FIRST|' + FIRST_START, install.component, pythonw, play + ' --first',
                 'X4 on the Multiplayer page when the installer finishes', None, None, None, 1, 'INSTALLDIR')])
        if (kit / 'second.pyw').is_file():   # a second player on the same PC (Steam starts only one X4)
            msilib.add_data(db, 'Shortcut', [
                ('SecondMenu', 'ProgramMenuFolder', product.second_short_name + '|' + product.name + ' (second window)',
                 install.component, pythonw, '"[INSTALLDIR]second.pyw"',
                 'One more X4 on this PC for the multiplayer session', None, None, None, 1, 'INSTALLDIR')])
    else:
        msilib.add_data(db, 'Shortcut', [
            (key, folder, short, install.component, '[INSTALLDIR]PLAY.cmd', None, "Start X4 with the Multiplayer entry in its main menu",
             None, None, None, 1, 'INSTALLDIR')
            for key, folder in (('PlayDesktop', 'DesktopFolder'), ('PlayMenu', 'ProgramMenuFolder'))])


def build(kit, out, name=None, version=None):
    msilib, schema, sequence = load_msilib()
    kit = Path(kit).resolve()
    out = Path(out).resolve()
    if (kit / 'release.json').is_file():
        product, release = release_product(kit, name)
        version = version or version_of_release(release['built'])
    else:
        product, release = playtest_product(kit, name)
        version = version or version_now()
    product_code = '{' + str(uuid.uuid4()).upper() + '}'
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    msilib._directories.clear()   # msilib remembers directory names per process
    db = msilib.init_database(str(out), schema, product.name, product_code, version, product.manufacturer)
    helper = (kit / 'helper.pyw').is_file() and (kit / 'python' / 'pythonw.exe').is_file()   # Play in Steam (steamplay.py)
    run_helper = '"[INSTALLDIR]python' + BS + 'pythonw.exe" "[INSTALLDIR]helper.pyw"'
    add_sequences(msilib, sequence, db, helper, release_kit=release is not None)
    if helper:
        add_helper_actions(msilib, db, product, run_helper, release_kit=release is not None)
    add_properties(msilib, db, product)

    cab = msilib.CAB('kit')
    feature = msilib.Feature(db, 'Kit', product.name, product.feature_title, 1, directory='INSTALLDIR')
    feature.set_current()
    root = msilib.Directory(db, cab, None, str(kit), 'TARGETDIR', 'SourceDir')
    local = msilib.Directory(db, cab, root, '', 'LocalAppDataFolder', 'LocalAppDataFolder')
    install = msilib.Directory(db, cab, local, '', 'INSTALLDIR', f'{product.short_name}|{product.name}')
    files = 0

    shipped_dirs = {'/'.join(path.split('/')[:depth]) for path in product.listed for depth in range(1, path.count('/') + 1)}

    def add(directory, folder, relative=''):
        nonlocal files
        entries = sorted(os.scandir(folder), key=lambda e: e.name.lower())
        for entry in entries:
            path = relative + entry.name
            if entry.is_file() and (not path.startswith('sessions/') or path in product.listed):
                directory.add_file(entry.name)
                files += 1
        for entry in entries:
            path = relative + entry.name
            shipped = path in shipped_dirs   # a release's ready-made sessions (only the manifest's files)
            if entry.is_dir() and (shipped or (entry.name not in product.not_installed and not path.startswith('sessions/'))):
                child = msilib.Directory(db, cab, directory, entry.name, entry.name, f'{directory.make_short(entry.name)}|{entry.name}')
                add(child, entry.path, path + '/')

    add(install, kit)
    add_shortcuts(msilib, db, kit, product, install, helper, run_helper, release_kit=release is not None)
    cab.commit(db)
    db.Commit()
    del db
    return {'msi': str(out), 'product': product_code, 'version': version, 'files': files, 'name': product.name,
            'size': out.stat().st_size, 'helper': bool(helper), 'release': release['release'] if release else None}


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--kit', required=True,
                   help='a kit folder: make_playtest_kit.py (best with --server and --python-embed) or make_release_kit.py')
    p.add_argument('--out', required=True, metavar='FILE.msi')
    p.add_argument('--name', help=f'the folder and shortcut name (default "{NAME}", a release kit "{RELEASE_NAME}")')
    args = p.parse_args(argv)
    made = build(args.kit, args.out, args.name)
    print(f'{made["msi"]}: {made["files"]} files, {made["size"] // (1024 * 1024)} MiB, version {made["version"]}, product {made["product"]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
