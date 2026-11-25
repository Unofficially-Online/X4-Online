"""Periodic saves: the play overlay's --save-every and x4ls play's collection (tools/build_play_overlay.py,
src/lockstep/launcher/saves.py). Synthetic: files and log rows made here, no X4."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(ROOT / 'tools'))
import build_play_overlay  # noqa: E402
from lockstep.launcher import saves  # noqa: E402


def row(tick, body, ms=2800):
    return (f'[=ERROR=] 12.3 [X4LAN] NATIVE_GATE PERIODIC_SAVE saved tick={tick} file=mpsave/s{tick}.xml.gz bytes={len(body)} '
            f'sha256={hashlib.sha256(body).hexdigest()} ms={ms} at={tick + 1}')


class KeepTests(unittest.TestCase):
    def test_in_sync_the_newest_ticks_stay(self):
        self.assertEqual(saves.keep_ticks([18000, 36000, 54000, 72000], 3), {36000, 54000, 72000})
        self.assertEqual(saves.keep_ticks([18000], 3), {18000})

    def test_after_a_desync_the_last_before_it_and_the_first_after_it_stay(self):
        ticks = [18000, 36000, 54000, 72000, 90000, 108000, 126000]
        self.assertEqual(saves.keep_ticks(ticks, 3, 88552), {36000, 54000, 72000, 90000})
        self.assertEqual(saves.keep_ticks(ticks, 2, 88552), {54000, 72000, 90000})
        self.assertEqual(saves.keep_ticks(ticks, 3, 90000), {36000, 54000, 72000, 90000})   # a save at the tick itself is after
        self.assertEqual(saves.keep_ticks(ticks, 3, 500), {18000})


class CollectorTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        root = Path(self.dir.name)
        self.profiles = {k: root / f'profile{k}/save' for k in (1, 2)}
        for folder in self.profiles.values():
            (folder / 'mpsave').mkdir(parents=True)
        self.log = []
        self.keeper = saves.Collector(root / 'run/saves', keep=2, tick_hz=30, log=self.log.append)

    def save(self, client, tick, body=None):
        body = body or f'world {tick} client {client}'.encode()
        (self.profiles[client] / f'mpsave/s{tick}.xml.gz').write_bytes(body)
        self.assertTrue(self.keeper.line(client, row(tick, body), self.profiles[client]))

    def names(self):
        """The kept saves; each has its sidecar beside it (x4ls continue --save), pruned with it."""
        folder = Path(self.dir.name) / 'run/saves'
        saves = sorted(p.name for p in folder.glob('c*.xml*'))
        self.assertEqual(sorted(p.name for p in folder.glob('c*.json')), sorted(n.split('.')[0] + '.json' for n in saves))
        return saves

    def test_saves_move_to_the_run_folder_and_the_oldest_go(self):
        self.assertFalse(self.keeper.line(1, '[X4LAN] NATIVE_GATE PERIODIC_SAVE armed tick=18000 at=17970', self.profiles[1]))
        for tick in (18000, 36000, 54000):
            for client in (1, 2):
                self.save(client, tick)
            self.keeper.poll()
        self.assertEqual(self.names(), ['c1-t36000.xml.gz', 'c1-t54000.xml.gz', 'c2-t36000.xml.gz', 'c2-t54000.xml.gz'])
        self.assertEqual(list((self.profiles[1] / 'mpsave').iterdir()), [])   # moved, not copied
        index = [json.loads(line) for line in (Path(self.dir.name) / 'run/saves/saves.jsonl').read_text().splitlines()]
        self.assertEqual(len(index), 6)
        self.assertEqual((index[0]['tick'], index[0]['client'], index[0]['minute'], index[0]['verified']), (18000, 1, 10.0, True))
        self.assertEqual(self.keeper.summary()['ticks'], [36000, 54000])

    def test_a_desync_freezes_the_saves_before_it(self):
        for tick in (18000, 36000, 54000):
            self.save(1, tick)
        self.keeper.poll()
        self.keeper.poll(first_desync=60000)
        for tick in (72000, 90000, 108000):
            self.save(1, tick)
            self.keeper.poll(first_desync=60000)
        self.assertEqual(self.names(), ['c1-t36000.xml.gz', 'c1-t54000.xml.gz', 'c1-t72000.xml.gz'])
        self.assertEqual(sum('first desync at tick 60000' in line for line in self.log), 1)

    def test_a_file_that_differs_from_the_gates_hash_is_said(self):
        (self.profiles[1] / 'mpsave/s18000.xml.gz').write_bytes(b'other bytes')
        self.keeper.line(1, row(18000, b'what the gate hashed'), self.profiles[1])
        self.keeper.poll()
        self.assertTrue(any('SHA-256 DIFFERS' in line for line in self.log))
        self.assertFalse(json.loads((Path(self.dir.name) / 'run/saves/saves.jsonl').read_text())['verified'])

    def test_a_missing_file_is_given_up_and_a_bad_name_ignored(self):
        self.keeper.line(1, row(18000, b'x'), self.profiles[1])   # no file
        self.keeper.poll()
        self.assertEqual((self.keeper.pending, self.names()), ([], []))
        self.assertTrue(any('not collected' in line for line in self.log))
        self.keeper.line(1, row(18000, b'x').replace('mpsave/s18000', '../../elsewhere'), self.profiles[1])
        self.assertEqual(self.keeper.pending, [])

    def test_a_save_only_some_clients_made_is_named_once(self):
        self.save(1, 18000)
        self.assertTrue(self.keeper.line(2, '[X4LAN] NATIVE_GATE ERROR PERIODIC_SAVE_FAILED tick=18000 state=failed '
                                            'why=is_saving_possible retry=18300 at=18001', self.profiles[2]))
        self.keeper.poll()
        self.assertEqual(sum('PERIODIC SAVE UNEVEN at tick 18000' in line for line in self.log), 1)
        self.assertEqual(self.keeper.summary()['uneven'], [18000])
        # refused by every client alike: no difference
        for client in (1, 2):
            self.keeper.line(client, '[X4LAN] NATIVE_GATE ERROR PERIODIC_SAVE_FAILED tick=36000 state=failed why=is_saving_possible '
                                     'retry=36300 at=36001', self.profiles[client])
        self.assertEqual(self.keeper.summary()['uneven'], [18000])


class OverlayTests(unittest.TestCase):
    def build(self, *extra):
        calls = []

        def fake_step(name, cmd):
            calls.append((name, [str(c) for c in cmd]))
            if name == 'players':
                out = Path(cmd[cmd.index('--out') + 1])
                (out / 'native').mkdir(parents=True)
                (out / 'native/x4lan_gate_64.dll').write_bytes(b'gate')
                (out / 'ui').mkdir()
                (out / 'ui/config.lua.append').write_text('X4LAN_CONFIG.enabled = true\n')
            return 'ok'
        with tempfile.TemporaryDirectory() as d, mock.patch.object(build_play_overlay, 'step', fake_step):
            snap = Path(d) / 'save_009.xml.gz'
            snap.write_bytes(b'save')
            build_play_overlay.main(['--snapshot', str(snap), '--gate-dll', str(snap), '--out', str(Path(d) / 'ov'), *extra])
            players = dict(calls)['players']
            return [players[i + 1] for i, a in enumerate(players) if a == '--set'], build_play_overlay.read_recipe(Path(d) / 'ov')

    def test_a_play_overlay_saves_every_ten_minutes_by_default(self):
        sets, recipe = self.build()
        self.assertIn('native_gate_save_every=18000', sets)
        self.assertIn('native_gate_save_keep=3', sets)
        self.assertEqual(recipe['save_every'], 10)

    def test_the_interval_is_set_in_minutes_and_zero_is_none(self):
        sets, recipe = self.build('--save-every', '2.5', '--save-keep', '5')
        self.assertIn('native_gate_save_every=4500', sets)
        self.assertIn('native_gate_save_keep=5', sets)
        self.assertEqual(recipe['save_every'], 2.5)
        sets, recipe = self.build('--save-every', '0')
        self.assertFalse(any(s.startswith('native_gate_save_') for s in sets))
        self.assertEqual(recipe['save_every'], 0)

    def test_no_saves_by_default_with_open_slots_or_linux_clients(self):
        sets, recipe = self.build('--players', '3', '--open', '3')
        self.assertFalse(any(s.startswith('native_gate_save_') for s in sets))
        sets, recipe = self.build('--mixed-platform')
        self.assertFalse(any(s.startswith('native_gate_save_') for s in sets))
        self.assertEqual(recipe['save_every'], 0)
        sets, recipe = self.build('--players', '3', '--open', '3', '--save-every', '10')   # asked for: given
        self.assertIn('native_gate_save_every=18000', sets)
        with self.assertRaisesRegex(SystemExit, 'the Linux gate has no periodic save'):
            self.build('--mixed-platform', '--save-every', '10')
        with self.assertRaisesRegex(SystemExit, 'at least 10 seconds'):
            self.build('--save-every', '0.1')

    def test_a_joiner_overlay_takes_the_sessions_interval(self):
        recipe = {'players': 3, 'mode': 'coop', 'minutes': 40.0, 'lua_block': 'Pause', 'open': [3], 'join_mode': 'report',
                  'full_logs': False, 'set': [], 'save_every': 10.0}
        cmd = build_play_overlay.joiner_command(recipe, 'j.xml.gz', 'gate.dll', 'out', 'j.x4join', 'ab' * 32)
        self.assertEqual(cmd[cmd.index('--save-every') + 1], '10.0')
        del recipe['save_every']   # an overlay from before this option: no saves on either side
        cmd = build_play_overlay.joiner_command(recipe, 'j.xml.gz', 'gate.dll', 'out', 'j.x4join', 'ab' * 32)
        self.assertEqual(cmd[cmd.index('--save-every') + 1], '0')


if __name__ == '__main__':
    unittest.main()
