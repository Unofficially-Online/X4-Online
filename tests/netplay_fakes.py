"""Fakes for the netplay launcher tests: package 2's modules (masterclient, nat, netifs, landisco) and the master's
HTTP API v1, in memory and following docs/NETPLAY.md's interfaces ("Python interfaces between the packages"). Addresses
are documentation ranges (RFC 5737) and private LAN ranges; nothing here leaves this PC."""
import hashlib
import json
import random
import secrets
import shutil
import socket
import sys
import tempfile
import threading
import time
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import netkit, netplay  # noqa: E402

CODE_ALPHABET = '23456789ABCDEFGHJKLMNPQRSTUVWXYZ'
RELAY_HOST = '203.0.113.50'     # the master's relay (relay mode)
HOST_PUBLIC = '198.51.100.20'   # the host's public address (direct mode)


def normalize_code(text):
    """The contract's normalize_code: 8 characters of the code alphabet, case and dash insensitive."""
    letters = str(text or '').strip().upper().replace('-', '')
    if len(letters) != 8 or any(ch not in CODE_ALPHABET for ch in letters):
        return None
    return letters[:4] + '-' + letters[4:]


class MasterError(Exception):
    def __init__(self, code, why='', status=0):
        super().__init__(why or code)
        self.code = code
        self.why = why or code
        self.status = status


class FakeMaster:
    """The master's state: games, files, players, the start. One per test; MasterClient objects talk to it."""

    def __init__(self, reachable=True):
        self.reachable = reachable
        self.games = {}
        self.calls = []
        self.lock = threading.Lock()

    def module(self):
        master = self

        class Client:
            def __init__(self, base_url, timeout=10):
                self.base_url = base_url

            def __getattr__(self, name):
                method = getattr(master, 'api_' + name, None)
                if method is None:
                    raise AttributeError(name)

                def call(*args, **kwargs):
                    with master.lock:
                        master.calls.append((name, args, kwargs))
                    if not master.reachable and name != 'echo_address':
                        raise MasterError('unreachable', 'the master server does not answer')
                    return method(*args, **kwargs)
                return call

        return types.SimpleNamespace(MasterClient=Client, MasterError=MasterError,
                                     master_url=lambda kit_dir=None, env=None: 'http://master.example.org:47100')

    def called(self, name):
        with self.lock:
            return [call for call in self.calls if call[0] == name]

    # the API
    def api_health(self):
        return {'version': 1, 'games': len(self.games), 'relays': 0, 'uptime_s': 1}

    def api_echo_address(self):
        return ('master.example.org', 47101)

    def api_create_game(self, name, public, release, players_max, session_sha256, open_slots=(), want='auto', direct=None, lan=None,
                        gamestart=''):
        game_id = 'g' + secrets.token_hex(3)
        code = 'K7QX-3FNA' if not self.games else 'M8RT-5HJK'
        self.games[game_id] = {'game_id': game_id, 'name': name, 'public': public, 'release': release,
                               'players_max': players_max, 'session_sha256': session_sha256, 'want': want,
                               'direct': direct, 'lan': lan, 'code': code, 'secret': secrets.token_hex(16),
                               'host_ticket': secrets.token_hex(16), 'files': {}, 'state': 'lobby', 'stamp': 1,
                               'players': [{'slot': 1, 'ready': True, 'host': True}], 'tickets': {}, 'start': None,
                               'open_slots': tuple(open_slots), 'send_join': [], 'gamestart': gamestart}
        game = self.games[game_id]
        return {'game_id': game_id, 'code': code, 'secret': game['secret'], 'host_ticket': game['host_ticket'], 'slot': 1}

    def api_upload(self, game_id, secret, name, path):
        game = self.games[game_id]
        assert secret == game['secret'] or (name.startswith('j') and secret in game['tickets'])
        if name in game.get('refuse', ()):
            code = game.get('refuse_code', 'bad_name')
            raise MasterError(code, 'this master does not take ' + name, 409 if code == 'not_asked' else 400)
        data = Path(path).read_bytes()
        game['files'][name] = data
        return {'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}

    def info(self, game):
        return {'game_id': game['game_id'], 'name': game['name'], 'release': game['release'], 'players': len(game['players']),
                'players_max': game['players_max'], 'state': game['state'], 'mode': 'direct' if game['direct'] else 'relay',
                'open_slots_free': len(self.free_slots(game)) if game['state'] != 'lobby' else 0, 'age_s': 1,
                **({'gamestart': game['gamestart']} if game.get('gamestart') else {})}

    @staticmethod
    def free_slots(game):
        """The master's rule: before the start the common load's slots first, then the open ones; after it the open ones."""
        opened = list(game.get('open_slots') or ())
        taken = {row['slot'] for row in game['players']}
        if game['state'] == 'lobby':
            order = [slot for slot in range(1, game['players_max'] + 1) if slot not in opened] + opened
        else:
            order = opened
        return [slot for slot in order if slot not in taken]

    def api_games(self, release):
        return [dict(self.info(game), compatible=game['release'] == release) for game in self.games.values() if game['public']]

    def api_code(self, code):
        for game in self.games.values():
            if game['code'] == code:
                answer = self.info(game)
                if game['lan']:   # the test's joiners share the host's public address
                    answer['lan'] = game['lan']
                return answer
        raise MasterError('unknown', 'no game has this code', 404)

    def api_join(self, game_id, release, code=None):
        game = self.games.get(game_id)
        if game is None:
            raise MasterError('unknown', 'no such game', 404)
        if release != game['release']:
            raise MasterError('incompatible', 'another release', 409)
        free = self.free_slots(game)
        if not free:
            raise MasterError('full', 'the game is full', 409)
        slot = free[0]
        ticket = secrets.token_hex(16)
        game['tickets'][ticket] = slot
        game['players'].append({'slot': slot, 'ready': False, 'host': False})
        game['stamp'] += 1
        files = {name: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()} for name, data in game['files'].items()}
        return {'ticket': ticket, 'slot': slot, 'files': files}

    def api_download(self, game_id, ticket, name, dest, sha256, size, progress=None):
        game = self.games[game_id]
        assert ticket in game['tickets']
        data = game['files'][name]
        if hashlib.sha256(data).hexdigest() != sha256:
            raise MasterError('sha256', 'the download does not match')
        Path(dest).parent.mkdir(parents=True, exist_ok=True)
        half = len(data) // 2
        Path(dest).write_bytes(data)
        if progress:
            progress(half, len(data))
            progress(len(data), len(data))
        return Path(dest)

    def api_lobby(self, game_id, ticket, since=0, wait=25):
        time.sleep(0.02)
        game = self.games.get(game_id)
        if game is None:
            raise MasterError('unknown', 'no such game', 404)
        answer = {'stamp': game['stamp'], 'state': game['state'], 'players': [dict(row) for row in game['players']],
                  'start': game['start']}
        if ticket == game['host_ticket'] and game.get('send_join'):   # the host is the saver of these joins
            answer['send_join'] = list(game['send_join'])
        return answer

    def api_ready(self, game_id, ticket):
        game = self.games[game_id]
        slot = game['tickets'][ticket]
        for row in game['players']:
            if row['slot'] == slot:
                row['ready'] = True
        game['stamp'] += 1
        return {}

    def api_start(self, game_id, secret):
        game = self.games[game_id]
        assert secret == game['secret']
        if game['direct']:
            relay = {'host': HOST_PUBLIC, 'port': game['direct']['udp_port'], 'token': secrets.token_hex(16)}
            mode = 'direct'
        else:
            relay = {'host': RELAY_HOST, 'port': 47200, 'token': secrets.token_hex(16)}
            mode = 'relay'
        game['start'] = {'load_at': int(time.time()) + 30, 'relay': relay, 'mode': mode}
        game['state'] = 'starting'
        game['stamp'] += 1
        return {}

    def api_heartbeat(self, game_id, auth, tick=0, state='lobby'):
        return {}

    def api_leave(self, game_id, ticket):
        game = self.games.get(game_id)
        if game and ticket in game['tickets']:
            slot = game['tickets'].pop(ticket)
            game['players'] = [row for row in game['players'] if row['slot'] != slot]
        return {}

    def api_delete(self, game_id, secret):
        self.games.pop(game_id, None)
        return {}

    def api_probe(self, udp_port, tcp_port, nonce):
        return {'tcp': True, 'ip': HOST_PUBLIC}

    # what another player does at the master (a joiner this test does not run as a flow)
    def add_player(self, game_id, ready=True):
        game = self.games[game_id]
        slot = self.free_slots(game)[0]
        game['players'].append({'slot': slot, 'ready': ready, 'host': False})
        game['stamp'] += 1
        return slot


class FakeMapping:
    def __init__(self, udp_port, tcp_port, method='upnp'):
        self.method = method
        self.external_ip = HOST_PUBLIC
        self.udp_port = udp_port
        self.tcp_port = tcp_port
        self.lease = 3600
        self.released = False

    def renew(self):
        return True

    def release(self):
        self.released = True


class FakeNat:
    def __init__(self, map_ok=True, reachable=True):
        self.map_ok = map_ok
        self.is_reachable = reachable
        self.mappings = []
        self.kept = []

    def map_ports(self, udp_port, tcp_port, lease=3600, timeout=3.0, log=None):
        if not self.map_ok:
            return None
        mapping = FakeMapping(udp_port, tcp_port)
        self.mappings.append(mapping)
        return mapping

    def keep_mapped(self, mapping, stop):
        self.kept.append((mapping, stop))
        thread = threading.Thread(target=stop.wait, daemon=True)
        thread.start()
        return thread

    def public_endpoint(self, host, echo_port=47101, timeout=2.0):
        return (HOST_PUBLIC, 50000, 38.4)

    def reachable(self, client, udp_port, tcp_port, timeout=4.0):
        return {'tcp': self.is_reachable, 'udp': self.is_reachable, 'ip': HOST_PUBLIC}

    normalize_code = staticmethod(normalize_code)


class FakeNetifs:
    def __init__(self, rows):
        self.rows = rows

    def addresses(self):
        return [dict(row) for row in self.rows]

    def best_for(self, peer_ip, candidates):
        prefix = '.'.join(str(peer_ip).split('.')[:3]) + '.'
        return sorted(candidates, key=lambda ip: 0 if ip.startswith(prefix) else 1)


class FakeAnnouncer:
    made = []

    def __init__(self, release, code, name, tcp_port, players, players_max, port=47102):
        self.fields = {'release': release, 'code': code, 'name': name, 'tcp_port': tcp_port, 'players': players,
                       'players_max': players_max}
        self.started = False
        self.stopped = False
        FakeAnnouncer.made.append(self)

    def update(self, players=None, code=None):
        if players is not None:
            self.fields['players'] = players

    def start(self):
        self.started = True

    def stop(self):
        self.stopped = True


class FakeLandisco:
    def __init__(self, found=()):
        self.found = list(found)
        self.Announcer = FakeAnnouncer

    def scan(self, seconds=2.5, port=47102):
        return [dict(entry) for entry in self.found]


def network(master=None, nat=None, netifs=None, landisco=None):
    return netplay.Network(masterclient=master.module() if master else None, nat=nat or FakeNat(),
                           netifs=netifs or FakeNetifs([{'ip': '192.168.10.5', 'netmask': '255.255.255.0',
                                                         'broadcast': '192.168.10.255', 'name': 'eth0'}]),
                           landisco=landisco or FakeLandisco())


# ---- sessions on disk ----
SAFE_SETS = ["native_gate_render_motion='interpolate'", 'native_gate_pilot_window={40,640}']


def recipe_for(players, save_sha, dll_sha, session=None, open_slots=()):
    return {'kind': 'x4ls-play-recipe', 'version': 1, 'role': 'session', 'players': players, 'mode': 'coop',
            'minutes': 40.0, 'full_logs': False, 'lua_block': 'Pause,Unpause,SaveGame', 'set': list(SAFE_SETS),
            'open': list(open_slots),
            'join_mode': 'report', 'window': [40, 72040], 'gate_dll_sha': dll_sha, 'snapshot_sha': save_sha,
            'session': session, 'mixed_platform': False, 'save_every': 10.0}


def session_dict(players, save_sha, save_path):
    return {'format': 'x4ls-session-1', 'base_start': 'x4ep1_gamestart_discover', 'mode': 'coop', 'players': players,
            'snapshot': {'path': str(save_path), 'sha256': save_sha, 'bytes': 6}, 'player_ship': '0' * 16,
            'player_sector': 'cluster_14_sector001_macro', 'fixture': ['1' * 16] * 12, 'hulls': {}, 'slots': {}}


def make_session(folder, players=2, dll=b'MZ-gate', save=b'SAVE-1', open_slots=()):
    """A session as netkit.SessionFiles: an overlay folder with a valid recipe, the save, the gate DLL."""
    folder = Path(folder)
    overlay = folder / 'overlay'
    (overlay / 'native').mkdir(parents=True)
    (overlay / 'native' / 'x4lan_gate_64.dll').write_bytes(dll)
    save_path = folder / 'session-save.xml.gz'
    save_path.write_bytes(save)
    save_sha = hashlib.sha256(save).hexdigest()
    dll_sha = hashlib.sha256(dll).hexdigest()
    recipe = recipe_for(players, save_sha, dll_sha, session_dict(players, save_sha, save_path.resolve()), open_slots)
    (overlay / 'play-recipe.json').write_text(json.dumps(recipe))
    return netkit.SessionFiles(overlay=overlay, snapshot=save_path, recipe=recipe, players=players, sha256=save_sha)


def session_save(players, seed=276944566, tail=b''):
    """A session save as X4 writes one (gzip; the header names the start and the seed), small."""
    import gzip
    text = ('<?xml version="1.0" encoding="UTF-8"?>\n<savegame>\n<info>\n<save name="fixture" date="1"/>\n'
            f'<game id="X4" version="900" build="611726" start="x4ep1_gamestart_discover" seed="{seed}"/>\n'
            f'<player name="Pilot {players}" money="5000"/>\n</info>\n</savegame>\n').encode() + tail
    return gzip.compress(text)


def overlay_manifest(overlay):
    overlay = Path(overlay)
    manifest = {p.relative_to(overlay).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(overlay.rglob('*')) if p.is_file() and p.name != 'x4lane-manifest.json'}
    (overlay / 'x4lane-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def host_session(kit_root, players=2, open_slots=(), seed=276944566, md=b'<mdscript name="Setup"/>\n',
                 work_path=None, extra=None, base_start=None, story=False):
    """A host session as x4ls new-session leaves it in a kit's sessions/ (netkit.cached_session takes it): session.json
    (no fixture), the save, spec.json, the overlay (an md file, the probe, the config, the gate DLL of the kit, the
    recipe, the manifest; extra: more overlay files, {relative path: bytes}), and the capture's and the base overlay's
    folders beside. work_path: the path session.json names (the work folder new-session wrote it in). base_start: its
    start (default the default start); story: every slot 2..N with its own copy of the start's story (new-session
    --story 2..N)."""
    kit = netkit.Kit(kit_root)
    base_start = base_start or netkit.HOST_SESSION_START
    key = netkit.host_session_key(kit, players, netkit.HOST_SESSION_MODE, base_start, open_slots)
    folder = kit.sessions / key
    overlay = folder / 'overlay'
    for sub in ('md', 'ui', 'native'):
        (overlay / sub).mkdir(parents=True, exist_ok=True)
    save = session_save(players, seed)
    (folder / 'session-save.xml.gz').write_bytes(save)
    save_sha = hashlib.sha256(save).hexdigest()
    shutil.copyfile(kit.gate_dll, overlay / 'native' / 'x4lan_gate_64.dll')
    (overlay / 'md' / 'setup.xml').write_bytes(md)
    (overlay / 'ui' / 'native_gate_probe.lua').write_text('-- the probe\n')
    (overlay / 'ui' / 'config.lua.append').write_text(f'X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@\n-- {players}\n')
    (overlay / 'ui.xml.files').write_text('ui/native_gate_probe.lua\n')
    slots = {str(k): {'gamestart': base_start, 'story': None, 'plots': [{'id': 'story', 'copy': f'GS_Copy_P{k}'}]}
             for k in range(2, players + 1)} if story else {}
    hulls = {k: f'{0x100 + int(k):016x}' for k in slots}
    described = {'format': 'X4LAN_SESSION_1', 'base_start': base_start, 'mode': 'coop', 'players': players,
                 'fixture': [], 'fixture_form': 'none', 'seed': str(seed), 'hulls': hulls, 'slots': slots,
                 'snapshot': {'path': str(work_path or folder.resolve() / 'session-save.xml.gz'), 'sha256': save_sha,
                              'bytes': len(save)}}
    (folder / 'session.json').write_text(json.dumps(described, indent=1, sort_keys=True))
    (overlay / 'session.json').write_text(json.dumps(described, indent=1, sort_keys=True))
    (folder / 'spec.json').write_text(json.dumps({'format': 'X4LAN_SESSION_START_SPEC_1', 'players': players}))
    recipe = recipe_for(players, save_sha, netkit.sha256_file(kit.gate_dll), described, open_slots)
    (overlay / 'play-recipe.json').write_text(json.dumps(recipe, indent=1, sort_keys=True))
    for name, data in (extra or {}).items():
        (overlay / name).parent.mkdir(parents=True, exist_ok=True)
        (overlay / name).write_bytes(data)
    overlay_manifest(overlay)
    for left in ('capture-overlay', 'overlay.base'):   # what new-session leaves beside, not part of a shipped session
        (folder / left / 'ui').mkdir(parents=True)
        (folder / left / 'ui' / 'config.lua.append').write_text('X4LAN_CONFIG.test_profile = "45841"\n')
    return folder


def release_kit(folder, release='0123456789ab-ba9876543210', dll=b'MZ-gate'):
    folder = Path(folder)
    (folder / 'native').mkdir(parents=True)
    (folder / 'native' / 'x4lan_gate_64.dll').write_bytes(dll)
    (folder / 'release.json').write_text(json.dumps({'release': release, 'commit': '0' * 40,
                                                     'gate_dll_sha256': hashlib.sha256(dll).hexdigest(), 'built': 'now'}))
    return folder


def fake_build(calls):
    """build_overlay's runner: records the command and writes the overlay build_play_overlay.py would (its recipe with
    this kit's gate DLL)."""
    def run(command, cwd=None, capture_output=True, text=True):
        calls.append(command)
        out = Path(command[command.index('--out') + 1])
        dll = Path(command[command.index('--gate-dll') + 1])
        save = Path(command[command.index('--snapshot') + 1])
        players = int(command[command.index('--players') + 1])
        opened = [int(slot) for slot in command[command.index('--open') + 1].split(',')] if '--open' in command else []
        out.mkdir(parents=True)
        recipe = recipe_for(players, netkit.sha256_file(save), netkit.sha256_file(dll), open_slots=opened)
        (out / 'play-recipe.json').write_text(json.dumps(recipe))
        return types.SimpleNamespace(returncode=0, stdout='play overlay: ' + str(out), stderr='')
    return run


# hoststarts.load's (rows, refused) of the stock game as this PC's X4 9.00 has it (Discover, the Terran cadet start).
STARTS = ([{'id': 'x4ep1_gamestart_discover', 'label': 'The Untested Explorer', 'story': False, 'page': 1021, 'line': 400},
           {'id': 'x4ep1_gamestart_terran1', 'label': 'Terran Cadet', 'story': True, 'page': 1021, 'line': 900}],
          {'x4ep1_gamestart_trade': 'start x4ep1_gamestart_trade: the snapshot capture needs the player in a ship in open '
                                    'space (on_foot)'})


class FakeBackend(netplay.Backend):
    """The flow's backend without X4: the clipboard in memory, sessions from files, installs recorded."""

    def __init__(self, work, host_files=None, own=None, starts=None):
        self.work = Path(work)
        self.clipboard = ''
        self.host_files = host_files
        self.own = own
        self.starts = starts      # host_starts' (rows, refused): the Host form's starts; None: no table
        self.installed = []
        self.hosted = []
        self.hosted_starts = []   # (base_start, story) of each host_session

    def clipboard_get(self):
        return self.clipboard

    def clipboard_set(self, text):
        self.clipboard = text
        return True

    def host_starts(self, mode):
        return self.starts

    def host_session(self, players, progress, cancelled, open_slots=(), base_start='', story=None):
        self.hosted.append((players, tuple(open_slots)) if open_slots else players)
        self.hosted_starts.append((base_start, story))
        progress(0.5)
        progress(1.0)
        return self.host_files

    def install_session(self, files, progress):
        self.installed.append(files)
        progress(1.0)

    def kit_session(self):
        return self.own

    def work_folder(self):
        self.work.mkdir(parents=True, exist_ok=True)
        return self.work

    def save_folder(self):
        return self.work / 'profile-save'


def free_port_pair():
    """A UDP port P and the TCP ports P and P + 1, all free on this PC (a host's relay, file service and lobby)."""
    for _ in range(200):
        port = random.randrange(20000, 60000)
        try:
            with socket.socket() as tcp, socket.socket() as tcp_next, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                tcp.bind(('0.0.0.0', port))
                tcp_next.bind(('0.0.0.0', port + 1))
                udp.bind(('0.0.0.0', port))
            return port
        except OSError:
            continue
    raise RuntimeError('no free port pair')


def wait_for(predicate, seconds=10):
    return netplay.wait_until(predicate, seconds)


class Folder:
    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name)

    def cleanup(self):
        shutil.rmtree(self.path, ignore_errors=True)
        self.tmp.cleanup()
