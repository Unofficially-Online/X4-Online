"""lockstep.launcher.netkit: kits and releases, the host's session (the kit's own, a cached one, one x4ls new-session
makes), the joiner's checks of a host's files (SHA-256, release, the recipe as data only) and the overlay built here
from the host's recipe with this kit's own gate DLL. The builder and new-session are fakes; no X4 runs here."""
import hashlib
import io
import json
import sys
import types
import unittest
from unittest import mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from lockstep.launcher import lobby, netkit  # noqa: E402
import netplay_fakes as fakes  # noqa: E402
from test_launcher_lobby import fake_overlay  # noqa: E402


class Kits(unittest.TestCase):
    def setUp(self):
        self.folder = fakes.Folder()
        self.root = self.folder.path

    def tearDown(self):
        self.folder.cleanup()

    def test_a_release_kit(self):
        kit = netkit.Kit(fakes.release_kit(self.root / 'kit', release='0123456789ab-ba9876543210'))
        self.assertEqual((kit.kind, kit.release), ('release', '0123456789ab-ba9876543210'))
        self.assertEqual(kit.gate_dll, self.root / 'kit' / 'native' / 'x4lan_gate_64.dll')
        self.assertEqual(kit.sessions, self.root / 'kit' / 'sessions')
        self.assertIsNone(kit.own_session())
        self.assertIsNone(kit.kit_id)

    def test_a_playtest_kit_and_one_with_a_server(self):
        kit_dir = self.root / 'kit'
        fake_overlay(kit_dir / 'overlay', players=3)
        (kit_dir / 'snapshot').mkdir()
        (kit_dir / 'snapshot' / 'save_009.xml.gz').write_bytes(b'SAVE')
        kit = netkit.Kit(kit_dir)
        self.assertEqual(kit.kind, 'playtest')
        self.assertTrue(kit.release.startswith('kit-'))
        self.assertEqual(kit.release, 'kit-' + lobby.kit_id(kit_dir / 'overlay', 'ab' * 32)[:24])
        self.assertEqual(kit.own_session().players, 3)
        self.assertEqual(kit.gate_dll, kit_dir / 'overlay' / 'native' / 'x4lan_gate_64.dll')
        self.assertNotEqual(kit.sessions, kit_dir / 'sessions')        # a playtest kit's folder stays as it was copied
        (kit_dir / 'server.json').write_text('{}')
        self.assertEqual(kit.kind, 'server')

    def test_this_checkout(self):
        dll = self.root / 'gate.dll'
        dll.write_bytes(b'MZ-dev')
        kit = netkit.Kit(None, gate_dll=dll)
        self.assertEqual(kit.kind, 'checkout')
        self.assertEqual(kit.release, 'dev-' + hashlib.sha256(b'MZ-dev').hexdigest()[:12])
        self.assertIsNone(netkit.kit_root_of_checkout(self.root / 'repo'))
        (self.root / 'release.json').write_text('{"release": "x"}')
        self.assertEqual(netkit.kit_root_of_checkout(self.root / 'repo'), self.root)

    def test_a_broken_release_file(self):
        (self.root / 'kit').mkdir()
        (self.root / 'kit' / 'release.json').write_text('{"release": "Has Spaces"}')
        with self.assertRaises(netkit.KitError):
            netkit.Kit(self.root / 'kit')


class Recipes(unittest.TestCase):
    def recipe(self, **changes):
        recipe = fakes.recipe_for(2, 'a' * 64, 'b' * 64)
        recipe.update(changes)
        return recipe

    def test_the_builders_values_pass(self):
        for item in ("native_gate_render_motion='interpolate'", 'native_gate_pilot_window={40,640}',
                     "native_gate_ui_lua_names={'Pause','SaveGame'}", 'native_gate_worker_layout=30',
                     'native_gate_lookahead = 2', 'native_gate_ik_wrap=true', "native_gate_fp_redirect='redirect'",
                     'native_gate_world_nav_stride=-1', 'native_gate_x={{1,2},{3}}'):
            self.assertTrue(netkit.safe_set_item(item), item)
        recipe = self.recipe()
        self.assertIs(netkit.validate_recipe(recipe), recipe)

    def test_code_in_a_recipe_is_refused(self):
        for item in ('native_gate_x=(function() end)()', "native_gate_x='a'..os.exit()", 'native_gate_x=os.exit',
                     "native_gate_x='a' or nil", 'other_key=1', 'native_gate_x={1,{2,{3}}}', 'native_gate_x=1\nX=2',
                     "native_gate_x='a\\'b'", 'native_gate_x=', 'native_gate_x=[[long]]', 'native_gate_x=0x10'):
            self.assertFalse(netkit.safe_set_item(item), item)
            with self.assertRaisesRegex(netkit.KitError, 'refused: set'):
                netkit.validate_recipe(self.recipe(set=[item]))
        for field, value in (('players', 9), ('mode', 'battle'), ('lua_block', 'Pause;os.exit()'), ('role', 'joiner'),
                             ('open', [1]), ('gate_dll_sha', 'x'), ('minutes', 10 ** 6), ('session', 'text')):
            with self.assertRaisesRegex(netkit.KitError, f'refused: {field}'):
                netkit.validate_recipe(self.recipe(**{field: value}))


class JoinerFiles(unittest.TestCase):
    """A joiner's folder of downloaded files, as the master or a LAN host would have sent them."""

    def setUp(self):
        self.folder = fakes.Folder()
        self.host = fakes.make_session(self.folder.path / 'host', players=2)
        self.paths = netkit.upload_files(self.host, self.folder.path / 'upload')
        self.expected = netkit.described_files(self.paths)
        self.joined = self.folder.path / 'joined'
        self.joined.mkdir()
        for name, path in self.paths.items():
            (self.joined / name).write_bytes(Path(path).read_bytes())
        self.dll_sha = hashlib.sha256(b'MZ-gate').hexdigest()

    def tearDown(self):
        self.folder.cleanup()

    def test_uploads_carry_no_paths(self):
        for name in ('session.json', 'play-recipe.json'):
            text = Path(self.paths[name]).read_text()
            self.assertNotIn(str(self.folder.path), text)
            self.assertNotIn(str(self.folder.path).replace('\\', '\\\\'), text)
        described = json.loads(Path(self.paths['session.json']).read_text())
        self.assertEqual(described['snapshot']['path'], 'session-save.xml.gz')
        self.assertEqual(self.paths['session-save.xml.gz'], self.host.snapshot)

    def test_checked_files(self):
        recipe = netkit.check_session_files(self.joined, self.dll_sha, self.expected)
        self.assertEqual(recipe['snapshot_sha'], self.host.sha256)

    def test_another_release_a_changed_save_and_mismatching_files_are_refused(self):
        with self.assertRaisesRegex(netkit.KitError, 'another release'):
            netkit.check_session_files(self.joined, 'c' * 64, self.expected)
        (self.joined / 'session-save.xml.gz').write_bytes(b'OTHER')
        with self.assertRaisesRegex(netkit.KitError, 'does not match its SHA-256'):
            netkit.check_session_files(self.joined, self.dll_sha, self.expected)
        with self.assertRaisesRegex(netkit.KitError, 'not the one its recipe'):
            netkit.check_session_files(self.joined, self.dll_sha)
        (self.joined / 'session-save.xml.gz').write_bytes(self.host.snapshot.read_bytes())
        described = json.loads((self.joined / 'session.json').read_text())
        described['players'] = 4
        (self.joined / 'session.json').write_text(json.dumps(described))
        with self.assertRaisesRegex(netkit.KitError, 'different sessions'):
            netkit.check_session_files(self.joined, self.dll_sha)

    def test_the_overlay_is_built_here_with_this_kits_gate(self):
        kit = netkit.Kit(fakes.release_kit(self.folder.path / 'kit'))
        recipe = netkit.check_session_files(self.joined, self.dll_sha, self.expected)
        calls = []
        seen = []
        files = netkit.joined_session(kit, self.joined, recipe, progress=seen.append, log=lambda text: None,
                                      run=fakes.fake_build(calls))
        command = calls[0]
        self.assertTrue(command[1].endswith('build_play_overlay.py'))
        self.assertEqual(command[command.index('--gate-dll') + 1], str(kit.gate_dll.resolve()))
        self.assertEqual(command[command.index('--snapshot') + 1], str((self.joined / 'session-save.xml.gz').resolve()))
        self.assertEqual([command[i + 1] for i, part in enumerate(command) if part == '--set'], fakes.SAFE_SETS)
        self.assertEqual(command[command.index('--players') + 1], '2')
        self.assertEqual(command[command.index('--save-every') + 1], '10.0')
        self.assertNotIn('--open', command)
        session_json = Path(command[command.index('--session') + 1])
        self.assertEqual(json.loads(session_json.read_text())['fixture'], ['1' * 16] * 12)
        self.assertEqual((seen[0], seen[-1]), (0.1, 1.0))
        self.assertEqual(files.overlay, self.joined / 'overlay')
        again = netkit.joined_session(kit, self.joined, recipe, log=lambda text: None, run=fakes.fake_build(calls))
        self.assertEqual((len(calls), again.overlay), (1, files.overlay))   # built once: the next game reuses it

    def test_a_failed_build_says_so(self):
        kit = netkit.Kit(fakes.release_kit(self.folder.path / 'kit'))
        recipe = netkit.check_session_files(self.joined, self.dll_sha)

        def fails(command, **kwargs):
            return types.SimpleNamespace(returncode=1, stdout='players failed', stderr='')
        with self.assertRaisesRegex(netkit.KitError, 'could not be built here'):
            netkit.joined_session(kit, self.joined, recipe, log=lambda text: None, run=fails)


class HostSessions(unittest.TestCase):
    def setUp(self):
        self.folder = fakes.Folder()
        self.kit = netkit.Kit(fakes.release_kit(self.folder.path / 'kit'))

    def tearDown(self):
        self.folder.cleanup()

    def popen(self, calls, code=0, form='none'):
        kit = self.kit

        class Proc:
            def __init__(self, command, **kwargs):
                calls.append(command)
                out = Path(command[command.index('--out') + 1])
                players = int(command[command.index('--players') + 1])
                opened = [int(k) for k in command[command.index('--open') + 1].split(',')] if '--open' in command else []
                if code == 0:
                    made = fakes.make_session(out, players, dll=kit.gate_dll.read_bytes(), open_slots=opened)
                    # x4ls new-session's own session.json names the fixture's form (none since 24 Nov), the start, and
                    # the slots that play the start's story as their own copy (--story)
                    start = command[command.index('--base-start') + 1]
                    story = [int(k) for k in command[command.index('--story') + 1].split(',')] if '--story' in command else []
                    slots = {str(k): {'gamestart': start, 'story': None,
                                      'plots': [{'id': 'story', 'copy': f'GS_X_P{k}'}] if k in story else []}
                             for k in range(2, players + 1)}
                    (out / 'session.json').write_text(json.dumps(dict(made.recipe['session'], fixture_form=form,
                                                                      base_start=start, slots=slots)))
                self.stdout = io.StringIO('slot 2: start x\ncapture client pid 4: starting x\nsaved: out\n'
                                          'session out: base start x, 2 players (coop); play it with:\n')
                self.returncode = code

            def wait(self):
                return self.returncode

            def poll(self):
                return self.returncode

            def kill(self):
                pass
        return Proc

    def test_made_once_then_cached(self):
        self.assertIsNone(netkit.cached_session(self.kit, 3))
        calls = []
        seen = []
        files = netkit.make_session(self.kit, 3, progress=seen.append, log=lambda text: None, popen=self.popen(calls))
        command = calls[0]
        self.assertEqual(command[2:4], ['new-session', '--base-start'])
        self.assertEqual(command[command.index('--base-start') + 1], netkit.HOST_SESSION_START)
        self.assertEqual(command[command.index('--players') + 1], '3')
        self.assertEqual(command[command.index('--gate-dll') + 1], str(self.kit.gate_dll))
        self.assertEqual(files.players, 3)
        self.assertEqual(seen[-1], 1.0)
        self.assertTrue(all(earlier <= later for earlier, later in zip(seen, seen[1:])), seen)
        cached = netkit.cached_session(self.kit, 3)
        self.assertEqual(cached.overlay, files.overlay)
        self.assertIsNone(netkit.cached_session(self.kit, 2))
        self.kit.gate_dll.write_bytes(b'MZ-newer')                     # another gate: the old session is not this release's
        self.assertIsNone(netkit.cached_session(self.kit, 3))

    def test_a_session_of_the_old_form_is_made_again(self):
        """A hosted session made before 24 Nov (players 2..N in fixture fighters, the battle fixture): not reused."""
        netkit.make_session(self.kit, 2, log=lambda text: None, popen=self.popen([], form=None))
        self.assertTrue(netkit.host_session_key(self.kit, 2, 'coop', netkit.HOST_SESSION_START).endswith('-n'))
        self.assertIsNone(netkit.cached_session(self.kit, 2))
        netkit.make_session(self.kit, 3, log=lambda text: None, popen=self.popen([]))
        self.assertIsNotNone(netkit.cached_session(self.kit, 3))

    def test_a_drop_in_session_has_its_own_key_and_open_slots(self):
        calls = []
        files = netkit.make_session(self.kit, 3, log=lambda text: None, popen=self.popen(calls), open_slots=(3,))
        command = calls[0]
        self.assertEqual(command[command.index('--open') + 1], '3')
        self.assertEqual(files.recipe['open'], [3])
        self.assertIn('-3p-open3-', files.overlay.parent.name)
        self.assertEqual(netkit.cached_session(self.kit, 3, open_slots=(3,)).overlay, files.overlay)
        self.assertIsNone(netkit.cached_session(self.kit, 3))              # the same players without drop-in: another session
        self.assertNotIn('--open', netkit.new_session_command(self.kit, 2, 'coop', 'x', Path('out')))

    def test_a_story_start_gives_every_slot_its_story_and_is_kept_in_that_form(self):
        cadet = 'x4ep1_gamestart_terran1'
        command = netkit.new_session_command(self.kit, 3, 'coop', cadet, Path('out'), story=True)
        self.assertEqual(command[command.index('--story') + 1], '2,3')
        command = netkit.new_session_command(self.kit, 4, 'coop', cadet, Path('out'), open_slots=(4,), story=True)
        self.assertEqual((command[command.index('--open') + 1], command[command.index('--story') + 1]), ('4', '2,3,4'))
        self.assertNotIn('--story', netkit.new_session_command(self.kit, 3, 'coop', cadet, Path('out')))
        calls, logs = [], []
        files = netkit.make_session(self.kit, 3, base_start=cadet, log=logs.append, popen=self.popen(calls), story=True)
        self.assertEqual(calls[0][calls[0].index('--story') + 1], '2,3')
        self.assertIn(f'start {cadet}, a story per player', logs[0])
        self.assertEqual(files.overlay.parent.name, netkit.host_session_key(self.kit, 3, 'coop', cadet))
        self.assertEqual(netkit.cached_session(self.kit, 3, 'coop', cadet, (), True).overlay, files.overlay)
        self.assertIsNotNone(netkit.cached_session(self.kit, 3, 'coop', cadet))        # story unknown: either form
        self.assertIsNone(netkit.cached_session(self.kit, 3, 'coop', cadet, (), False))
        self.assertIsNone(netkit.cached_session(self.kit, 3))                          # another start: another session
        # a session of that start made before (no story per player) is made again when the stories are wanted
        netkit.make_session(self.kit, 2, base_start=cadet, log=logs.append, popen=self.popen(calls), story=False)
        self.assertIsNone(netkit.cached_session(self.kit, 2, 'coop', cadet, (), True))
        self.assertIsNone(netkit.session_stories({'slots': {'2': {'plots': [{'id': 'story'}]}, '3': {}}}, 3))   # a mix
        # story unknown: this PC's game data says (netkit.start_story); the default start has no story per player
        asked = []
        with mock.patch.object(netkit, 'start_story', lambda start, game=None, log=print: asked.append(start) or True):
            netkit.make_session(self.kit, 2, base_start=cadet, log=logs.append, popen=self.popen(calls))
            self.assertIn('--story', calls[-1])
            netkit.make_session(self.kit, 2, log=logs.append, popen=self.popen(calls))
            self.assertNotIn('--story', calls[-1])
        self.assertEqual(asked, [cadet])

    def test_the_capture_goes_into_the_lobbys_own_lanes(self):
        # with the default lanes another session's capture view blocked hosting ("c:\x4ls-lanes\capture already
        # exists"; the laptop, 23 Nov)
        command = netkit.new_session_command(self.kit, 2, 'coop', 'x', Path('out'), game=Path('G:/X4'), lanes=Path('D:/lanes'))
        self.assertEqual(command[command.index('--lanes') + 1], str(Path('D:/lanes')))
        self.assertEqual(command[command.index('--game') + 1], str(Path('G:/X4')))
        self.assertNotIn('--lanes', netkit.new_session_command(self.kit, 2, 'coop', 'x', Path('out')))

    def test_a_failed_capture_leaves_nothing(self):
        with self.assertRaisesRegex(netkit.KitError, 'could not be made'):
            netkit.make_session(self.kit, 2, log=lambda text: None, popen=self.popen([], code=1))
        self.assertEqual(list(self.kit.sessions.iterdir()), [])

    def test_beside_an_open_x4_the_capture_is_told_so_and_takes_no_focus(self):
        calls, logs, options = [], [], []
        proc = self.popen(calls)

        def popen(command, **kwargs):
            options.append(kwargs)
            return proc(command, **kwargs)
        netkit.make_session(self.kit, 3, log=logs.append, popen=popen, beside=True)
        self.assertIn('--no-focus', calls[0])
        self.assertEqual(options[0]['env']['X4LS_BESIDE'], '1')
        self.assertTrue(logs[0].startswith(netkit.SESSION_BESIDE_MARK))
        self.assertEqual(netkit.count_restarts('\n'.join(logs)), 0)      # the player's X4 stayed: no restart
        netkit.make_session(self.kit, 2, log=logs.append, popen=popen)     # alone: no X4 of the player's was open
        self.assertNotIn('--no-focus', calls[1])
        self.assertNotIn('env', options[1])
        self.assertEqual(netkit.count_restarts('\n'.join(logs)), 1)

    def test_a_kept_session_names_no_path_of_this_pc(self):
        files = netkit.make_session(self.kit, 2, log=lambda text: None, popen=self.popen([]))
        folder = files.overlay.parent
        self.assertEqual(json.loads((folder / 'session.json').read_text())['snapshot']['path'], netkit.SESSION_SAVE)
        self.assertEqual(files.recipe['session']['snapshot']['path'], netkit.SESSION_SAVE)
        self.assertEqual(netkit.portable_session(folder), [])              # once is enough
        self.assertEqual(list(netkit.host_sessions(self.kit)), [folder.name])

    def test_two_x4s_need_free_memory(self):
        self.assertTrue(netkit.beside_fits(6, status=lambda: 7 << 30))
        self.assertFalse(netkit.beside_fits(6, status=lambda: 5 << 30))
        self.assertTrue(netkit.beside_fits(6, status=lambda: None))       # unknown: tried (a failure falls back)
        command = netkit.new_session_command(self.kit, 2, 'coop', 'x', Path('out'), focus=False, display=3)
        self.assertEqual(command[-3:], ['--no-focus', '--display', '3'])


class ShippedSessions(unittest.TestCase):
    def test_a_session_as_a_release_ships_it(self):
        folder = fakes.Folder()
        try:
            kit = netkit.Kit(fakes.release_kit(folder.path / 'kit'))
            made = fakes.host_session(kit.root, 3, work_path='C:\\Users\\someone\\x\\session-save.xml.gz')
            shipped = netkit.ship_session(made, folder.path / 'out' / made.name)
            self.assertEqual(sorted(p.name for p in shipped.iterdir()), ['overlay', 'session-save.xml.gz', 'session.json',
                                                                         'spec.json'])
            for name in ('session.json', 'overlay/session.json', 'overlay/play-recipe.json'):
                self.assertNotIn('someone', (shipped / name).read_text())
            from lockstep.launcher import extension
            extension.verify_overlay(shipped / 'overlay')                  # its manifest follows the rewritten files
            with self.assertRaises(netkit.KitError):
                netkit.ship_session(made, shipped)                         # never over another
            with self.assertRaises(netkit.KitError):
                netkit.ship_session(folder.path / 'kit', folder.path / 'out' / 'x')   # not a session folder
        finally:
            folder.cleanup()


if __name__ == '__main__':
    unittest.main()
