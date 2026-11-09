"""A playtest kit as a Windows installer: python tools/make_playtest_msi.py --kit KIT --out FILE.msi

The .msi installs a kit folder (tools/make_playtest_kit.py) for the current user, without administrator rights:

  %LOCALAPPDATA%\\<name>\\        the kit: repo, overlay, snapshot, PLAY.cmd, and python\\ when the kit bundles one
  Desktop and Start menu          a shortcut "<name>" that runs PLAY.cmd

A kit with a server and a bundled Python is installed for "Play in Steam" instead (lockstep/launcher/steamplay.py): the
installer starts the kit's helper, registers it to start with Windows (HKCU ...\\Run) and puts one Start menu entry
there that starts it again. The helper puts the Multiplayer entry into X4. A removal stops the helper and takes the
entry out of X4.

Built for a kit with a server and a bundled Python (make_playtest_kit.py --server ADDRESS --python-embed ZIP): the
player installs, starts the shortcut and chooses Multiplayer, Join in X4's main menu. Nothing else is needed but X4.

- Windows shows the installer's progress bar only (no pages to click through). A newer .msi replaces an older one.
  Removing it (Settings, Apps) leaves the run folders the games made in repo\\.local.
- The file is not signed, so Windows may warn about an unknown publisher when it was downloaded.
- This tool runs on Windows with Python 3.12 or older: it uses msilib, which left the standard library in 3.13.
- The kit's files are installed as they are: nothing of X4 is in a kit, and the kit's server.json (the server's
  address and key) is part of the installer, so it goes to the players only."""
import argparse
import json
import os
import sys
import time
import uuid
import warnings
from pathlib import Path

UPGRADE_CODE = '{5E0C2F1B-6A4D-4C8E-9B7F-3D2A1C0E9F84}'   # the same for every kit: a new installer replaces the old one
NAME = 'X4 Multiplayer Playtest'


def version_now(now=None):
    """Installer versions are major.minor.build (255.255.65535): year, month, minute of the month. Later is higher."""
    now = now or time.gmtime()
    return f'{now.tm_year % 100}.{now.tm_mon}.{(now.tm_mday - 1) * 1440 + now.tm_hour * 60 + now.tm_min}'


def build(kit, out, name=NAME, version=None):
    try:
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', DeprecationWarning)
            import msilib
            from msilib import schema, sequence
    except ImportError:
        raise SystemExit('this tool needs Windows and Python 3.12 or older (msilib left the standard library in 3.13)')
    kit, out = Path(kit).resolve(), Path(out).resolve()
    if not (kit / 'kit-manifest.json').is_file() or not (kit / 'PLAY.cmd').is_file():
        raise SystemExit(f'{kit} is not a kit with PLAY.cmd (tools/make_playtest_kit.py, an overlay without open slots)')
    manifest = json.loads((kit / 'kit-manifest.json').read_text(encoding='utf-8'))
    version = version or version_now()
    product = '{' + str(uuid.uuid4()).upper() + '}'
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    msilib._directories.clear()   # msilib remembers directory names per process
    db = msilib.init_database(str(out), schema, name, product, version, 'X4 lockstep playtest')
    helper = (kit / 'helper.pyw').is_file() and (kit / 'python' / 'pythonw.exe').is_file()   # Play in Steam (steamplay.py)
    run_helper = '"[INSTALLDIR]python' + chr(92) + 'pythonw.exe" "[INSTALLDIR]helper.pyw"'
    for table in sequence.tables:   # the standard actions, without the three dialogs msilib names but does not define
        rows = [row for row in getattr(sequence, table) if row[0] not in ('ExitDialog', 'FatalError', 'UserExit')]
        if table == 'InstallExecuteSequence':
            # an older kit goes before the new one comes (its removal stops its helper and takes its menu extension out)
            rows = [(action, condition, 1450 if action == 'RemoveExistingProducts' else number) for action, condition, number in rows]
            if helper:
                rows += [('StopHelper', 'REMOVE="ALL"', 1600), ('StartHelper', 'NOT REMOVE', 6650)]
        msilib.add_data(db, table, rows)
    if helper:
        msilib.add_data(db, 'CustomAction', [
            ('StopHelper', 34 | 0x40, 'INSTALLDIR', run_helper + ' --remove'),    # waits for it; its result does not matter
            # Started and left running, through Explorer and the Start menu entry: what the installer starts itself
            # inherits its redirection guard and cannot follow the folder links of a game view (Win32 error 448 in X4).
            ('StartHelper', 34 | 0xC0, 'INSTALLDIR', '"[WindowsFolder]explorer.exe" "[ProgramMenuFolder]' + name + '.lnk"'),
        ])
    msilib.add_data(db, 'Property', [
        ('UpgradeCode', UPGRADE_CODE), ('ALLUSERS', '2'), ('MSIINSTALLPERUSER', '1'),   # for this user, no elevation
        ('LIMITUI', '1'),                                                               # the progress bar, no pages
        ('ARPNOMODIFY', '1'), ('ARPNOREPAIR', '1'), ('SecureCustomProperties', 'OLDPRODUCTS'),
        ('ARPCOMMENTS', f'kit {manifest["commit"][:12]}'),
    ])
    msilib.add_data(db, 'Upgrade', [(UPGRADE_CODE, '0.0.0', None, None, 256, None, 'OLDPRODUCTS')])   # any older kit goes
    summary = db.GetSummaryInformation(20)
    summary.SetProperty(msilib.PID_WORDCOUNT, 2 | 8)   # compressed; 8: no administrator rights needed
    summary.SetProperty(msilib.PID_SUBJECT, name)
    summary.Persist()

    cab = msilib.CAB('kit')
    feature = msilib.Feature(db, 'Kit', name, 'The playtest kit', 1, directory='INSTALLDIR')
    feature.set_current()
    root = msilib.Directory(db, cab, None, str(kit), 'TARGETDIR', 'SourceDir')
    local = msilib.Directory(db, cab, root, '', 'LocalAppDataFolder', 'LocalAppDataFolder')
    install = msilib.Directory(db, cab, local, '', 'INSTALLDIR', f'X4MPPLAY|{name}')
    files = 0

    def add(directory, folder):
        nonlocal files
        entries = sorted(os.scandir(folder), key=lambda e: e.name.lower())
        for entry in entries:
            if entry.is_file():
                directory.add_file(entry.name)
                files += 1
        for entry in entries:
            if entry.is_dir() and entry.name not in ('__pycache__', '.local'):
                child = msilib.Directory(db, cab, directory, entry.name, entry.name, f'{directory.make_short(entry.name)}|{entry.name}')
                add(child, entry.path)

    add(install, kit)
    msilib.add_data(db, 'Directory', [('DesktopFolder', 'TARGETDIR', 'Desktop'), ('ProgramMenuFolder', 'TARGETDIR', 'Programs')])
    short = 'X4MPPLAY|' + name
    if helper:   # the player presses Play in Steam; the helper starts with Windows, and from the Start menu when it was ended
        msilib.add_data(db, 'Registry', [('HelperRun', 1, 'Software' + chr(92) + 'Microsoft' + chr(92) + 'Windows' + chr(92)
                                          + 'CurrentVersion' + chr(92) + 'Run', 'X4MultiplayerPlaytest', run_helper + ' --quiet',
                                          install.component)])
        msilib.add_data(db, 'Shortcut', [
            ('HelperMenu', 'ProgramMenuFolder', short, install.component, '[INSTALLDIR]python' + chr(92) + 'pythonw.exe',
             '"[INSTALLDIR]helper.pyw"', 'Start the multiplayer helper (then press Play in Steam)', None, None, None, 1, 'INSTALLDIR')])
    else:
        msilib.add_data(db, 'Shortcut', [
            (key, folder, short, install.component, '[INSTALLDIR]PLAY.cmd', None, "Start X4 with the Multiplayer entry in its main menu",
             None, None, None, 1, 'INSTALLDIR')
            for key, folder in (('PlayDesktop', 'DesktopFolder'), ('PlayMenu', 'ProgramMenuFolder'))])
    cab.commit(db)
    db.Commit()
    del db
    return {'msi': str(out), 'product': product, 'version': version, 'files': files, 'name': name, 'size': out.stat().st_size,
            'helper': bool(helper)}


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--kit', required=True, help='a kit folder (make_playtest_kit.py); best with --server and --python-embed')
    p.add_argument('--out', required=True, metavar='FILE.msi')
    p.add_argument('--name', default=NAME, help=f'the folder and shortcut name (default "{NAME}")')
    args = p.parse_args(argv)
    made = build(args.kit, args.out, args.name)
    print(f'{made["msi"]}: {made["files"]} files, {made["size"] // (1024 * 1024)} MiB, version {made["version"]}, product {made["product"]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
