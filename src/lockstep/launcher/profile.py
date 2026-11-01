"""Disposable X4 profiles (-personalfolderid) for launcher clients.

Only the ids in PROFILES are ever written; anything else, including the player's
own Steam-id profile, is refused. A client profile is wiped before each session
so no world, autosave or setting carries over.
"""
import os
from pathlib import Path
import shutil

PROFILES = (45850, 45851, 45853, 45854, 45855, 45856, 45857, 45858)
CAPTURE_PROFILE = 45841  # tools/dev/native_gate_snapshot.lua only saves inside .../45841/save
SNAPSHOT_SLOT = 'save_009.xml.gz'


def documents():
    return Path(os.environ['USERPROFILE']) / 'Documents/Egosoft/X4'


def profile_dir(profile, root=None):
    if profile not in (*PROFILES, CAPTURE_PROFILE):
        raise SystemExit(f'profile {profile} is not a disposable launcher profile')
    path = (Path(root) if root else documents()) / str(profile)
    if path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction()):
        raise SystemExit(f'{path} is a link; refusing')
    return path


# Render-only settings turned down for unattended soaks (--fast): more frames per second means more
# admitted ticks per second. Detail distances (lodfactor, effectdistance) stay at 1.0 because they can
# change what the simulation does; every client gets identical settings either way.
FAST = {'gfxquality': 0, 'shaderquality': 0, 'shadows': 0, 'softshadows': 'false', 'ssao': 0, 'glow': 0, 'uiglow': 0,
        'distortion': 'false', 'ssr': 'false', 'texturequality': 'low', 'chromaticaberration': 'false', 'volumetric': 0,
        'pom': 'off', 'envmapprobes': 'low', 'frameratelimit': 'false', 'presentmode': 'immediate', 'nosound': 'true'}


def config_xml(width, height, fast=False):
    extra = ''.join(f'<{k}>{v}</{k}>' for k, v in FAST.items()) if fast else ''
    return ('<?xml version="1.0"?><root><autosave>false</autosave><uisafemode>false</uisafemode>'
            f'<fullscreen>false</fullscreen><borderless>false</borderless><res_width>{width}</res_width>'
            f'<res_height>{height}</res_height><antialiasing>0</antialiasing><vsync>false</vsync>{extra}</root>\n')


def content_xml(extension_ids):
    rows = ''.join(f'<extension id="{name}" enabled="true" />' for name in extension_ids)
    return f'<?xml version="1.0"?><content>{rows}</content>\n'


def prepare(profile, extension_ids, width=1280, height=720, snapshot=None, root=None, fast=False):
    """Fresh profile: windowed config, autosave and protected UI off, optional snapshot."""
    path = profile_dir(profile, root)
    if path.exists():
        shutil.rmtree(path)
    (path / 'save').mkdir(parents=True)
    (path / 'config.xml').write_text(config_xml(width, height, fast))
    (path / 'content.xml').write_text(content_xml(extension_ids))
    if snapshot:
        shutil.copyfile(snapshot, path / 'save' / SNAPSHOT_SLOT)
    return path


def collect(profile, logfile, out, root=None):
    """Copy the client's log (and any saves it wrote) into the run directory."""
    path = profile_dir(profile, root)
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    if (path / logfile).is_file():
        shutil.copy2(path / logfile, out / logfile)
    saves = [p for p in (path / 'save').glob('*') if p.name != SNAPSHOT_SLOT] if (path / 'save').is_dir() else []
    for save in saves:
        shutil.copy2(save, out / save.name)
