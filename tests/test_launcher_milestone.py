"""Milestone saves in x4ls play and x4ls continue (src/lockstep/launcher/saves.py, milestone.py, netkit.py's host form).
Synthetic: log rows, saves in X4's line shape and sidecars written here; the overlay builder is a stand-in. No X4."""
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import milestone, netkit, saves  # noqa: E402

NAME = 'terran_cadet-M1-slot2'


def saved_row(client, tick, body):
    return (f'[=ERROR=] 99.1 [X4LAN] NATIVE_GATE MILESTONE_SAVE saved name={NAME} tick={tick} file=mpsave/s{tick}.xml.gz '
            f'bytes={len(body)} sha256={hashlib.sha256(body).hexdigest()} ms=2600 at={tick + 1}')


def slot_row(tick, slot, player, ship, inventory='-', walking=0, active=None):
    return (f'[X4LAN] NATIVE_GATE SAVE_SLOT name={NAME} tick={tick} slot={slot} player={player} ship={ship} walking={walking} '
            f'inventory={inventory} research=-' + (f' active={active}' if active else '') + f' at={tick - 25}')


class CollectorTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        root = Path(self.dir.name)
        self.profiles = {k: root / f'profile{k}/save' for k in (1, 2)}
        for folder in self.profiles.values():
            (folder / 'mpsave').mkdir(parents=True)
        self.log = []
        self.context = {'session': {'players': 2, 'mode': 'coop'}, 'recipe': {'kind': 'x4ls-play-recipe'}, 'spec': None, 'overlay': 'o'}
        self.keeper = saves.Collector(root / 'run/saves', keep=2, tick_hz=30, log=self.log.append, context=self.context)

    def test_a_persons_save_is_collected_with_who_asked(self):
        body = b'world after player 2 pressed quick save'
        (self.profiles[1] / 'mpsave/s361.xml.gz').write_bytes(body)
        self.keeper.line(1, slot_row(361, 2, 502, 1002, 'weapon_x:3').replace(NAME, 'quicksave-t361'), self.profiles[1])
        self.assertTrue(self.keeper.line(1, f'[X4LAN] NATIVE_GATE PLAYER_SAVE saved name=quicksave-t361 tick=361 file=mpsave/s361.xml.gz '
                                            f'bytes={len(body)} sha256={hashlib.sha256(body).hexdigest()} ms=2500 slot=2 save=quicksave at=362',
                                         self.profiles[1]))
        self.keeper.poll()
        folder = Path(self.dir.name) / 'run/saves/player'
        self.assertEqual(sorted(p.name for p in folder.iterdir()), ['quicksave-t361.json', 'quicksave-t361.xml.gz'])
        data = json.loads((folder / 'quicksave-t361.json').read_text())
        self.assertEqual((data['kind'], data['asked_by'], data['save_name'], data['save_file'], data['tick']), ('player', 2, 'quicksave', 'quicksave', 361))
        self.assertEqual(data['slots']['2']['inventory'], [{'ware': 'weapon_x', 'amount': 3}])
        self.assertEqual(self.keeper.summary()['player_saves'], ['quicksave-t361'])

    def test_one_file_per_milestone_with_every_clients_hash_and_the_slots(self):
        bodies = {1: b'world as client 1 saved it', 2: b'world as client 2 saved it'}
        for client in (2, 1):   # client 2's row first: client 1's file is kept all the same
            self.assertTrue(self.keeper.line(client, '[X4LAN] NATIVE_GATE MILESTONE reached name=terran_cadet-M1-slot2 age=10 due=481 save=541 at=301',
                                             self.profiles[client]))
            self.assertTrue(self.keeper.line(client, slot_row(541, 1, 501, 1001), self.profiles[client]))
            self.assertTrue(self.keeper.line(client, slot_row(541, 2, 502, 1002, 'software_scannerobjectmk3:1,weapon_x:3', active='3:plot'),
                                             self.profiles[client]))
            (self.profiles[client] / 'mpsave/s541.xml.gz').write_bytes(bodies[client])
            self.assertTrue(self.keeper.line(client, saved_row(client, 541, bodies[client]), self.profiles[client]))
            self.keeper.poll()
        folder = Path(self.dir.name) / 'run/saves/milestones'
        self.assertEqual(sorted(p.name for p in folder.iterdir()), [NAME + '.json', NAME + '.xml.gz'])
        self.assertEqual((folder / f'{NAME}.xml.gz').read_bytes(), bodies[1])
        self.assertEqual(list((self.profiles[2] / 'mpsave').iterdir()), [])   # client 2's file: hashed and gone
        data = json.loads((folder / f'{NAME}.json').read_text())
        self.assertEqual((data['format'], data['story'], data['milestone'], data['slot'], data['tick'], data['client']),
                         ('X4LAN_MILESTONE_SAVE_1', 'terran_cadet', 1, 2, 541, 1))
        self.assertEqual(data['sha256'], hashlib.sha256(bodies[1]).hexdigest())
        self.assertEqual(sorted(data['clients']), ['1', '2'])
        self.assertTrue(all(c['verified'] for c in data['clients'].values()))
        self.assertEqual(data['slots']['2'], {'player': f'{502:016x}', 'ship': f'{1002:016x}', 'walking': False, 'research': [],
                                              'inventory': [{'ware': 'software_scannerobjectmk3', 'amount': 1}, {'ware': 'weapon_x', 'amount': 3}],
                                              'active': [3, 'plot']})   # its active mission: the 3rd of the list, a plot
        self.assertIsNone(data['slots']['1']['inventory'])   # slot 1: X4's own player, kept in the save itself
        self.assertIsNone(data['slots']['1']['active'])      # ...and its active mission (the manager's own)
        self.assertEqual(data['session'], self.context['session'])
        self.assertEqual(data['reached']['due'], 481)
        self.assertIn('cadet part 1', data['title'])
        self.assertEqual(self.keeper.summary()['milestones'], [NAME])
        self.assertEqual(self.keeper.summary()['ticks'], [])   # never among the periodic saves (never pruned)

    def test_a_file_the_gate_did_not_announce_is_said(self):
        (self.profiles[1] / 'mpsave/s541.xml.gz').write_bytes(b'other bytes')
        self.keeper.line(1, saved_row(1, 541, b'the bytes the gate hashed'), self.profiles[1])
        self.keeper.poll()
        self.assertTrue(any('SHA-256 DIFFERS' in line for line in self.log))
        data = json.loads((Path(self.dir.name) / f'run/saves/milestones/{NAME}.json').read_text())
        self.assertFalse(data['verified'])

    def test_the_context_of_an_overlay(self):
        root = Path(self.dir.name)
        spec = {'format': 'X4LAN_SESSION_START_SPEC_1', 'players': 2}
        (root / 'sess').mkdir()
        (root / 'sess/spec.json').write_text(json.dumps(spec))
        session = {'players': 2, 'snapshot': {'path': str(root / 'sess/session-save.xml.gz')},
                   'spec_sha256': hashlib.sha256(json.dumps(spec, sort_keys=True).encode()).hexdigest()}
        (root / 'overlay').mkdir()
        (root / 'overlay/play-recipe.json').write_text(json.dumps({'kind': 'x4ls-play-recipe', 'session': session}))
        context = saves.overlay_context(root / 'overlay')
        self.assertEqual((context['session'], context['spec']), (session, spec))
        self.assertEqual(saves.overlay_context(root / 'missing'), {})


def save_text(players, p1_in_ship=True, slot2_owner='player', flying=False):
    """A session save in X4's line shape: slot 1's scout with X4's player (named), slot 2's fighter with its player. flying:
    slot 1's ship on the ship computer's autopilot at speed, slot 2's in travel mode with boost (as rk8's c3-t18000 had it)."""
    p1 = ('<component class="player" macro="character_player_discover_macro" connection="room" name="{1021,402}" owner="player" '
          'id="[0x69539]">\n<inventory>\n<ware ware="weapon_gen_spacesuit_repairlaser_01_mk1"/>\n</inventory>\n'
          + ('<autopilot nextchecktime="66.47" target="[0x5]"/>\n' if flying else '') + '</component>\n')
    flight1 = ['<velocity>', '<linear x="-226.2" z="-2190.2"/>', '<angular rz="0.57"/>', '</velocity>', '<acceleration>', '<linear z="5"/>',
               '</acceleration>', '<position>', '<read space="[0x4]">', '<offset>', '</offset>', '<velocity>', '<linear z="-2190.2"/>',
               '</velocity>', '<acceleration/>', '</read>', '</position>', '<targetpoints>',
               '<targetpoint refobject="[0x5]" behavior="playerautopilot2" travel="1">', '<offset>', '</offset>', '</targetpoint>',
               '</targetpoints>', '<transition/>'] if flying else []
    flight2 = ['<boost start="290.265"/>', '<sound travel="1"/>'] if flying else []
    engine2 = ['<connection connection="con_engine_01">', '<component class="engine" macro="engine_ter_s_allround_01_mk1_macro" id="[0x3ce20]">',
               '<offset>', '</offset>', '<travel active="1" factor="14"/>', '<connections>', '<connection connection="connectionre">',
               '<component class="effectobject" macro="effect_gen_s_traveldriveexhaust_01_macro" id="[0x3ce21]">',
               '<effect effect="gen_traveldrive_fx_s" duration="-1"/>', '</component>', '</connection>', '</connections>', '</component>',
               '</connection>', '<connection connection="connectionre">',
               '<component class="effectobject" macro="effect_gen_s_travelbubble_01_exterior_macro" id="[0x3ce22]">',
               '<movement class="scale">', '<scale>', '<value x="34.4"/>', '</scale>', '</movement>', '</component>', '</connection>',
               '<connection connection="connectionre">', '<component class="effectobject" macro="dummyemitter_macro" id="[0x3ce23]">',
               '</component>', '</connection>'] if flying else []
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<savegame>', '<info>',
             '<game start="x4ep1_gamestart_discover" seed="276944566" build="611726"/>', '</info>', '<universe>',
             '<component class="galaxy" macro="xu_ep2_universe_macro" id="[0x1]">', '<connections>', '<connection connection="clusters">',
             '<component class="cluster" macro="cluster_23_macro" id="[0x2]">', '<connections>', '<connection connection="sectors">',
             '<component class="sector" macro="cluster_23_sector001_macro" id="[0x3]">', '<connections>', '<connection connection="zones">',
             '<component class="zone" macro="zone001_cluster_23_sector001_macro" id="[0x4]">', '<connections>',
             '<connection connection="ships">',
             '<component class="ship_s" macro="ship_arg_s_scout_01_a_macro" owner="player" id="[0x69534]">',
             '<movement forceposition="0" player="1" class="player">', '<offset>', '</offset>'] + flight1 + ['</movement>', '<connections>',
             '<connection connection="con_cockpit">', '<component class="cockpit" macro="cockpit_gen_virtual_01_macro" id="[0x69538]">',
             '<connections>', '<connection connection="player">']
    if p1_in_ship:
        lines += p1.splitlines()
    lines += ['</connection>', '</connections>', '</component>', '</connection>', '</connections>', '</component>',
              f'<component class="ship_s" macro="ship_ter_s_fighter_01_a_macro" owner="{slot2_owner}" id="[0x3ce11]">',
              '<movement forceposition="0" player="1" class="player">', '<targetpoints>', '<targetpoint refobject="[0x3db66]" behavior="playerautopilot2">',
              '<offset>', '</offset>', '</targetpoint>', '</targetpoints>', '<flightcontrolmodel type="spline"/>', '</movement>',
              '<offset>', '<position x="1" z="2"/>', '</offset>'] + flight2 + ['<connections>'] + engine2 + [
              '<connection connection="con_cockpit">', '<component class="cockpit" macro="cockpit_gen_virtual_01_macro" id="[0x3ce18]">',
              '<connections>', '<connection connection="player">']
    if players >= 2:
        lines += ['<component class="player" macro="character_player_discover_macro" connection="room" code="JOR-849" owner="player" id="[0x160455]">',
                  '<inventory>', '<ware ware="software_scannerobjectmk3"/>', '<ware ware="weapon_x" amount="3"/>', '</inventory>',
                  '<blueprints>', '<blueprint ware="module_ter_pier_01"/>', '</blueprints>', '<research>',
                  '<research ware="research_radioreceiver" method="research"/>', '</research>',
                  '<connections>', '<connection connection="computer">', '<component class="computer" macro="computer" id="[0x160456]">',
                  '<inventory>', '<ware ware="not_the_players"/>', '</inventory>', '</component>', '</connection>', '</connections>',
                  '</component>']
    lines += ['</connection>', '</connections>', '</component>', '</connection>', '</connections>', '</component>']
    if not p1_in_ship:
        lines += ['<component class="station" macro="station_macro" owner="argon" id="[0x77]">'] + p1.splitlines() + ['</component>']
    lines += ['</connection>', '</connections>', '</component>'] * 4 + ['</universe>', '<md>',
              '<script id="312" name="X4LAN_PlayerIdentity">', '<cue id="7" name="State" state="complete">', '<vars>',
              '<value name="$ordinal" type="largefloat" value="38"/>', '</vars>', '</cue>', '</script>',
              '<script id="313" name="X4LAN_UiReplay">', '</script>',
              '<value name="$x" type="component" value="[0x160455]"/>', '</md>', '</savegame>']
    return '\n'.join(lines) + '\n'


FIRST = {'format': 'X4LAN_SESSION_1', 'base_start': 'x4ep1_gamestart_discover', 'base_class': 'c', 'mode': 'coop', 'players': 2,
         'snapshot': {'path': 'C:/first/session-save.xml.gz', 'sha256': 'a' * 64, 'bytes': 1}, 'seed': '276944566', 'build': '611726',
         'player_ship': '00000000000ae828', 'player_macro': 'ship_arg_s_scout_01_a_macro', 'player_sector': 'cluster_23_sector001_macro',
         'fixture': [], 'fixture_form': 'none', 'hulls': {'2': '000000000016fbe1'}, 'first_plots': [], 'spec_sha256': 'b' * 64,
         'slots': {'2': {'gamestart': 'x4ep1_gamestart_terran1', 'class': 'b', 'sector': 'cluster_101_sector001_macro',
                         'ship': 'ship_ter_s_fighter_01_a_macro', 'world_omitted': [], 'story': None,
                         'plots': [{'id': 'story', 'script': 'GS_Terran1', 'copy': 'GS_Terran1_P2', 'cue': 'Start', 'slot': 2}],
                         'kit': {'slot': 2, 'gamestart': 'x4ep1_gamestart_terran1', 'money': 15000, 'omitted': []}}}}
RECIPE = {'kind': 'x4ls-play-recipe', 'version': 1, 'role': 'session', 'players': 2, 'mode': 'coop', 'minutes': 40.0, 'set': [],
          'open': [], 'join_mode': 'report', 'lua_block': 'Pause,Unpause,SaveGame', 'full_logs': False, 'gate_dll_sha': 'c' * 64,
          'save_every': 10.0}


class ContinueTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.root = Path(self.dir.name)
        (self.root / 'milestones').mkdir()

    def milestone(self, text=None, slots=None, **change):
        path = self.root / f'milestones/{NAME}.xml.gz'
        with gzip.open(path, 'wb') as f:
            f.write((text or save_text(2)).encode())
        data = {'format': 'X4LAN_MILESTONE_SAVE_1', 'name': NAME, 'story': 'terran_cadet', 'milestone': 1, 'slot': 2, 'title': 'cadet part 1',
                'tick': 541, 'file': path.name, 'client': 1, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                'slots': slots if slots is not None else
                {'1': {'player': '0000000000069539', 'ship': '0000000000069534', 'walking': False, 'inventory': None, 'research': []},
                 '2': {'player': '0000000000160455', 'ship': '000000000003ce11', 'walking': False,
                       'inventory': [{'ware': 'weapon_x', 'amount': 4}], 'research': [], 'active': [2, 'plot']}},
                'session': json.loads(json.dumps(FIRST)), 'recipe': dict(RECIPE)}
        data.update(change)
        (self.root / f'milestones/{NAME}.json').write_text(json.dumps(data))
        return path

    def prepare(self, path, **options):
        calls = []

        def run(cmd, **_):
            calls.append(cmd)
            return mock.Mock(returncode=0, stdout='', stderr='')
        with mock.patch.object(milestone.subprocess, 'run', run):
            result = milestone.prepare(milestone.ContinueOptions(milestone=path, gate_dll=self.root / 'gate.dll', out=self.root / 'new',
                                                                 **options), log=lambda *_: None)
        return result, calls

    def test_every_player_continues_as_saved(self):
        result, calls = self.prepare(self.milestone())
        self.assertEqual(result['outcome'], 'ready')
        with gzip.open(self.root / 'new/session-save.xml.gz', 'rt') as f:
            text = f.read()
        self.assertEqual(text.count('<component class="player"'), 1)  # X4 loads one player object
        self.assertIn('id="[0x69539]"', text)                         # X4's own, as saved
        self.assertNotIn('0x160456', text)                            # slot 2's, with what it held
        self.assertIn('value="[0x160455]"', text)                     # scripts' references stay (X4 drops them at the load)
        self.assertNotIn('X4LAN_PlayerIdentity', text)                # its setup runs again (its step counter was the last session's)
        self.assertEqual(text.count('<movement forceposition="0" player="1" class="player">'), 1)   # X4's own player's ship keeps it
        self.assertNotIn('playerautopilot2', text)                    # slot 2's ship: no PlayerController for the gate to refuse
        self.assertIn('<position x="1" z="2"/>', text)                # ...its place kept
        self.assertIn('name="X4LAN_UiReplay"', text)
        session = json.loads((self.root / 'new/session.json').read_text())
        self.assertEqual((session['player_ship'], session['hulls'], session['fixture'], session['fixture_form']),
                         ('0000000000069534', {'2': '000000000003ce11'}, [], 'none'))
        self.assertIsNone(session['slots']['2']['kit'])
        self.assertEqual(session['slots']['2']['plots'], FIRST['slots']['2']['plots'])   # the story copies stay the slot's
        block = session['continue']
        self.assertEqual((block['format'], block['name'], block['tick']), ('X4LAN_CONTINUE_1', NAME, 541))
        self.assertEqual(block['slots'], {'2': {'inventory': [{'ware': 'weapon_x', 'amount': 4}],   # the probe's row wins
                                                'active': [2, 'plot']}})                           # its active mission again
        self.assertEqual(session['snapshot']['sha256'], hashlib.sha256((self.root / 'new/session-save.xml.gz').read_bytes()).hexdigest())
        cmd = calls[0]
        self.assertEqual(cmd[cmd.index('--session') + 1], str(self.root / 'new/session.json'))
        self.assertEqual(cmd[cmd.index('--snapshot') + 1], str(self.root / 'new/session-save.xml.gz'))
        state = json.loads((self.root / 'new/continued-from.json').read_text())
        self.assertEqual((state['kept_player'], state['removed_players']), ('0000000000069539', ['0000000000160455']))

    def test_every_players_ship_starts_at_rest(self):
        # rk9, 24 Nov: player 2's ship was saved in travel mode at full speed and flew on into the void at once
        saved = save_text(2, flying=True)
        result, _ = self.prepare(self.milestone(saved))
        self.assertEqual(result['outcome'], 'ready')
        with gzip.open(self.root / 'new/session-save.xml.gz', 'rt') as f:
            text = f.read()
        ET.fromstring(text)                                            # still one document
        self.assertNotIn('<travel ', text)                             # slot 2's travel drive off
        self.assertNotIn('traveldriveexhaust', text)                   # ...and its effects gone, with their connections
        self.assertNotIn('travelbubble', text)
        self.assertIn('dummyemitter_macro', text)                      # another effect object stays
        self.assertNotIn('<connections>\n</connections>', text)       # X4 writes no empty connections
        self.assertIn('<boost/>', text)
        self.assertNotIn('<boost start', text)
        self.assertNotIn('<sound', text)
        self.assertNotIn('2190', text)                                 # player 1's ship: no speed...
        self.assertNotIn('<linear z="5"/>', text)                      # ...no acceleration...
        self.assertIn('<targetpoints/>', text)                         # ...no autopilot path
        self.assertNotIn('playerautopilot2', text)
        self.assertIn('<autopilot nextchecktime="66.47"/>', text)      # X4's player: the ship computer's target gone
        self.assertIn('<transition/>', text)
        state = json.loads((self.root / 'new/continued-from.json').read_text())
        self.assertEqual({k: v for k, v in state['flight'].items() if k != 'ships'},
                         {'form': 'rest', 'still': 4, 'travel': 1, 'effects': 2, 'boost': 1, 'autopilot': 1})
        self.assertEqual(state['flight']['ships'], ['000000000003ce11', '0000000000069534'])
        # the flag keeps the flight as saved (only the second ship's PlayerController goes, as before)
        shutil.rmtree(self.root / 'new')
        self.prepare(self.milestone(saved), keep_flight=True)
        with gzip.open(self.root / 'new/session-save.xml.gz', 'rt') as f:
            kept = f.read()
        for part in ('<travel active="1" factor="14"/>', 'traveldriveexhaust', 'travelbubble', '<boost start="290.265"/>', '<sound travel="1"/>',
                     '<linear x="-226.2" z="-2190.2"/>', 'target="[0x5]"'):
            self.assertIn(part, kept)
        self.assertEqual(json.loads((self.root / 'new/continued-from.json').read_text())['flight'], 'kept')

    def test_without_rows_the_saved_player_object_tells_the_ship_and_what_it_held(self):
        session = dict(FIRST, mode='factions')
        session['slots'] = json.loads(json.dumps(FIRST['slots']))
        self.prepare(self.milestone(slots={}, session=session, recipe=dict(RECIPE, mode='factions')))
        block = json.loads((self.root / 'new/session.json').read_text())['continue']
        self.assertEqual(block['slots']['2'], {'inventory': [{'ware': 'software_scannerobjectmk3', 'amount': 1}, {'ware': 'weapon_x', 'amount': 3}],
                                               'blueprints': ['module_ter_pier_01'], 'research': ['research_radioreceiver']})
        self.assertEqual(json.loads((self.root / 'new/session.json').read_text())['hulls'], {'2': '000000000003ce11'})

    def test_a_periodic_save_with_its_sidecar_continues(self):
        """x4ls play writes a sidecar beside every periodic save (the rows the probe wrote when it armed): x4ls continue --save."""
        runs = self.root / 'run/saves'
        profile = self.root / 'profile1/save'
        (profile / 'mpsave').mkdir(parents=True)
        with gzip.open(profile / 'mpsave/s18000.xml.gz', 'wb') as f:
            f.write(save_text(2).encode())
        body = (profile / 'mpsave/s18000.xml.gz').read_bytes()
        keeper = saves.Collector(runs, keep=2, log=lambda *_: None, context={'session': json.loads(json.dumps(FIRST)), 'recipe': dict(RECIPE)})
        for row in (slot_row(18000, 1, int('69539', 16), int('69534', 16)).replace(NAME, 'periodic-t18000'),
                    slot_row(18000, 2, int('160455', 16), int('3ce11', 16), 'weapon_x:5').replace(NAME, 'periodic-t18000'),
                    f'[X4LAN] NATIVE_GATE PERIODIC_SAVE saved tick=18000 file=mpsave/s18000.xml.gz bytes={len(body)} '
                    f'sha256={hashlib.sha256(body).hexdigest()} ms=2900 at=18001'):
            self.assertTrue(keeper.line(1, row, profile))
        keeper.poll()
        data = json.loads((runs / 'c1-t18000.json').read_text())
        self.assertEqual((data['kind'], data['name'], data['tick'], data['prepared']), ('periodic', 'c1-t18000', 18000, True))
        self.prepare(runs / 'c1-t18000.xml.gz')
        block = json.loads((self.root / 'new/session.json').read_text())['continue']
        self.assertEqual((block['name'], block['kind'], block['slots']), ('c1-t18000', 'periodic', {'2': {'inventory': [{'ware': 'weapon_x', 'amount': 5}]}}))

    def test_a_save_without_a_sidecar_continues_with_its_session(self):
        """A periodic save of a build before 25 Nov: --session and --overlay stand in for the sidecar; slot 2 from its saved
        player object (the one taken out), the stories from the session (claimed again by their roots' names)."""
        path = self.root / 'c3-t18000.xml.gz'
        with gzip.open(path, 'wb') as f:
            f.write(save_text(2).encode())
        (self.root / 'session.json').write_text(json.dumps(FIRST))
        (self.root / 'first-overlay').mkdir()
        (self.root / 'first-overlay/play-recipe.json').write_text(json.dumps(RECIPE))
        with self.assertRaisesRegex(SystemExit, 'no sidecar'):
            self.prepare(path)
        self.setUp()
        path = self.root / 'c3-t18000.xml.gz'
        with gzip.open(path, 'wb') as f:
            f.write(save_text(2).encode())
        (self.root / 'session.json').write_text(json.dumps(FIRST))
        (self.root / 'first-overlay').mkdir()
        (self.root / 'first-overlay/play-recipe.json').write_text(json.dumps(RECIPE))
        self.prepare(path, session=self.root / 'session.json', overlay=self.root / 'first-overlay')
        session = json.loads((self.root / 'new/session.json').read_text())
        self.assertEqual((session['hulls'], session['continue']['name'], session['continue']['tick']), ({'2': '000000000003ce11'}, 'c3-t18000', 18000))
        self.assertEqual(session['continue']['slots']['2']['inventory'], [{'ware': 'software_scannerobjectmk3', 'amount': 1}, {'ware': 'weapon_x', 'amount': 3}])

    def test_what_cannot_continue_is_refused(self):
        with self.assertRaisesRegex(SystemExit, 'not in a ship'):
            self.prepare(self.milestone(save_text(2, p1_in_ship=False)))
        self.setUp()
        with self.assertRaisesRegex(SystemExit, 'belongs to argon'):
            self.prepare(self.milestone(save_text(2, slot2_owner='argon')))
        self.setUp()
        path = self.milestone()
        with gzip.open(path, 'ab') as f:
            f.write(b'<!-- changed -->\n')
        with self.assertRaisesRegex(SystemExit, 'SHA-256 differs'):
            self.prepare(path)
        self.setUp()
        path = self.milestone()
        (self.root / f'milestones/{NAME}.json').unlink()
        with self.assertRaisesRegex(SystemExit, 'no sidecar'):
            self.prepare(path)
        self.setUp()
        with self.assertRaisesRegex(SystemExit, 'open slots'):
            self.prepare(self.milestone(recipe=dict(RECIPE, open=[2])))
        self.setUp()
        with self.assertRaisesRegex(SystemExit, 'another slot'):
            self.prepare(self.milestone(slots={'2': {'player': None, 'ship': '0000000000069534', 'walking': False, 'inventory': [], 'research': []}}))


class HostTests(unittest.TestCase):
    def test_a_hosted_game_from_a_milestone_is_made_once_and_kept(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'native').mkdir()
            (root / 'native/x4lan_gate_64.dll').write_bytes(b'gate')
            kit = mock.Mock(gate_dll=root / 'native/x4lan_gate_64.dll', sessions=root / 'sessions')
            save = root / f'{NAME}.xml.gz'
            save.write_bytes(b'save')
            (root / f'{NAME}.json').write_text(json.dumps({'format': 'X4LAN_MILESTONE_SAVE_1', 'name': NAME, 'session': {'players': 2}}))
            made = []

            def prepare(o, log=print):
                made.append(o)
                (o.out / 'overlay').mkdir(parents=True)
                (o.out / 'session-save.xml.gz').write_bytes(b'continued')
                (o.out / 'overlay/play-recipe.json').write_text(json.dumps({'players': 2, 'gate_dll_sha': netkit.sha256_file(kit.gate_dll)}))
                return {'outcome': 'ready'}
            self.assertEqual(netkit.milestone_players(save), 2)
            files = netkit.milestone_session(kit, save, 2, prepare=prepare)
            self.assertEqual((files.players, files.snapshot.name), (2, 'session-save.xml.gz'))
            self.assertTrue(files.overlay.parent.name.startswith('continue-r-'))   # the ships at rest
            self.assertFalse(made[0].keep_flight)
            again = netkit.milestone_session(kit, save, 2, prepare=prepare)
            self.assertEqual((len(made), again.overlay), (1, files.overlay))
            flying = netkit.milestone_session(kit, save, 2, prepare=prepare, keep_flight=True)   # x4ls host --keep-flight: its own
            self.assertTrue(flying.overlay.parent.name.startswith('continue-f-') and made[-1].keep_flight and len(made) == 2)
            with self.assertRaisesRegex(SystemExit, 'of 2 players, not 3'):
                netkit.milestone_session(kit, save, 3, prepare=prepare)
            with self.assertRaisesRegex(SystemExit, 'drop-in'):
                netkit.milestone_session(kit, save, 2, open_slots=(3,), prepare=prepare)


if __name__ == '__main__':
    unittest.main()
