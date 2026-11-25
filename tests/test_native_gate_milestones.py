"""Story milestones: the catalogue, a session's watches, the listener MD and the config lines
(tools/dev/native_gate_milestones.py). The catalogue's check against the game reads this PC's unpacked game data
(X4LAN_GAMEDATA); everything else is synthetic."""
import copy
import os
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import native_gate_milestones as milestones  # noqa: E402

GAMEDATA = os.environ.get('X4LAN_GAMEDATA')
STORY = {'cue': 'Start', 'extension': 'ego_dlc_terran', 'file': 'gs_terran1.xml', 'script': 'GS_Terran1'}


def copies(k):
    """A slot's plot copies of the cadet story and the Terran story (native_gate_plot_copies.py entries, the fields read here)."""
    return [{'id': 'story', 'script': 'GS_Terran1', 'copy': f'GS_Terran1_P{k}', 'cue': 'Start', 'slot': k},
            {'id': 'yaki', 'script': 'Story_Yaki', 'copy': f'Story_Yaki_P{k}', 'cue': 'Start', 'slot': k}]


def session(slots, players=None):
    return {'players': players or (1 + len(slots)), 'mode': 'coop', 'slots': slots}


class CatalogueTests(unittest.TestCase):
    def test_the_terran_story_has_its_chapters_in_order(self):
        story = milestones.check_catalogue()['terran_cadet']
        cues = [(story['plots'][m['plot']]['script'], m['cue']) for m in story['milestones']]
        self.assertEqual(cues[:3], [('GS_Terran1', 'Ch1_Maintenance'), ('GS_Terran1', 'Ch2_Distress'), ('Story_Yaki', 'Ch1_Escort')])
        self.assertEqual(cues[-1], ('Story_Yaki', 'Ch23_Yaki_Trade'))
        self.assertEqual(len(cues), 25)
        self.assertEqual(milestones.milestone('terran_cadet', 2)['end'], 'Ch2_Clean_Up_Terran_Cadet_Gamestart_Mission')
        self.assertEqual([n for n, m in enumerate(story['milestones'], 1) if m['mission']], [2, 6, 7, 10, 11, 16, 20, 21, 23, 24, 25])

    def test_names(self):
        self.assertEqual(milestones.save_name('terran_cadet', 1, 2), 'terran_cadet-M1-slot2')
        self.assertEqual(milestones.parse_name('terran_cadet-M12-slot3'), ('terran_cadet', 12, 3))
        for bad in ('terran_cadet-M1-slot9', '../x-M1-slot2', 'Terran-M1-slot2', 'terran_cadet-M1-slot2.xml'):
            self.assertIsNone(milestones.parse_name(bad), bad)

    def test_a_bad_catalogue_is_refused(self):
        good = copy.deepcopy(milestones.STORIES)
        for change in (lambda s: s.update({'Bad': s.pop('terran_cadet')}),
                       lambda s: s['terran_cadet']['milestones'][0].update(plot='other'),
                       lambda s: s['terran_cadet']['milestones'].append(dict(s['terran_cadet']['milestones'][0])),
                       lambda s: s['terran_cadet']['milestones'][1].update(cue='Ch2 Distress')):
            stories = copy.deepcopy(good)
            change(stories)
            with self.assertRaises(milestones.MilestoneError):
                milestones.check_catalogue(stories)

    @unittest.skipUnless(GAMEDATA and Path(GAMEDATA, 'ego_dlc_terran/md/story_yaki.xml').is_file(), 'needs X4LAN_GAMEDATA (unpacked game data)')
    def test_every_milestone_is_in_the_game_data(self):
        self.assertEqual(milestones.check(GAMEDATA), [])
        # a wrong line is named: Ch1_Clean_Up does not cancel Ch2_Mines, completes no mission and is signalled outside it
        stories = copy.deepcopy(milestones.STORIES)
        stories['terran_cadet']['milestones'][3].update(end='Ch1_Clean_Up', mission=True)
        wrong = milestones.check(GAMEDATA, stories)
        self.assertEqual([n for _, n, _ in wrong], [4, 4, 4], wrong)
        self.assertIn('signalled from outside Ch2_Mines', wrong[2][2])
        # Ch22's clean-up has no signal (the chapter's cancel is its marker); Ch1's has one (its cancel is not)
        self.assertEqual([m['on'] for m in milestones.STORIES['terran_cadet']['milestones']].count('cancel'), 1)
        stories = copy.deepcopy(milestones.STORIES)
        stories['terran_cadet']['milestones'][2]['on'] = 'cancel'
        self.assertEqual([n for _, n, _ in milestones.check(GAMEDATA, stories)], [3])

    def test_the_success_path_is_checked(self):
        """Synthetic script: a clean-up signalled from outside its chapter, through a failure parameter, one that fails
        its mission on its signal, one that is right (its abort a branch on that event), and a cancel marker of a
        signalled clean-up."""
        script = """<mdscript name="Plot"><cues><cue name="Start"><cues>
 <cue name="Outside"><actions><signal_cue cue="A_Clean_Up"/></actions></cue>
 <cue name="A"><cues><cue name="A_Step"><actions><signal_cue cue="A_Clean_Up"/></actions></cue>
  <cue name="A_Clean_Up"><conditions><event_cue_signalled/></conditions><actions><cancel_cue cue="A"/></actions></cue></cues></cue>
 <cue name="B"><cues><cue name="B_Speak" ref="md.LIB_Dialog.Speak_Actor"><param name="FailureSignalCue" value="B_Clean_Up"/></cue>
  <cue name="B_Clean_Up"><conditions><event_cue_signalled/></conditions><actions><cancel_cue cue="B"/></actions></cue></cues></cue>
 <cue name="C"><cues><cue name="C_Step"><actions><signal_cue cue="C_Clean_Up"/></actions></cue>
  <cue name="C_Clean_Up"><conditions><event_cue_signalled/></conditions><actions><remove_mission cue="$M" type="completed"/>
   <remove_mission cue="$M" type="failed"/><cancel_cue cue="C"/></actions></cue></cues></cue>
 <cue name="D"><cues><cue name="D_Step" ref="md.LIB_Generic.ApproachObject_Handler"><param name="SuccessSignalCue" value="D_Clean_Up"/></cue>
  <cue name="D_Clean_Up"><conditions><check_any><event_cue_signalled/><event_mission_aborted cue="$M"/></check_any></conditions>
   <actions><do_if value="event.name == 'event_mission_aborted'"><remove_mission cue="$M" type="aborted"/></do_if>
   <do_else><remove_mission cue="$M" type="completed"/></do_else><cancel_cue cue="D"/></actions></cue></cues></cue>
 <cue name="E"><cues><cue name="E_Step"><actions><signal_cue cue="E_Clean_Up"/></actions></cue>
  <cue name="E_Clean_Up"><conditions><event_cue_signalled/></conditions><actions><cancel_cue cue="E"/></actions></cue></cues></cue>
</cues></cue></cues></mdscript>"""
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / 'ego_x/md').mkdir(parents=True)
            (Path(tmp) / 'ego_x/md/plot.xml').write_text(script, encoding='utf-8')
            rows = [{'plot': 'p', 'cue': c, 'end': f'{c}_Clean_Up', 'mission': c in 'CD', 'title': c, 'on': 'cancel' if c == 'E' else 'signal'}
                    for c in 'ABCDE']
            stories = {'synthetic': {'what': '-', 'gamestart': 'x', 'milestones': rows,
                                     'plots': {'p': {'extension': 'ego_x', 'file': 'plot.xml', 'script': 'Plot', 'cue': 'Start'}}}}
            wrong = milestones.check(tmp, stories)
        self.assertEqual([(n, text.split(' is ')[-1].split(' ends ')[-1]) for _, n, text in wrong],
                         [(1, 'signalled from outside A (signal_cue)'), (2, 'signalled on a failure (param FailureSignalCue)'),
                          (3, 'its mission otherwise than completed on its signal'),
                          (5, 'signalled (1x): use its signal, not the chapter\'s cancel')])


class WatchTests(unittest.TestCase):
    def test_a_slot_with_its_own_copies_watches_them_and_slot_1_the_stock_scripts(self):
        rows = milestones.watches(session({'2': {'story': None, 'plots': copies(2)}}))
        self.assertEqual(len(rows), 50)
        first = [r for r in rows if r['slot'] == 1]
        self.assertEqual({r['script'] for r in first}, {'GS_Terran1', 'Story_Yaki'})
        second = [r for r in rows if r['slot'] == 2]
        self.assertEqual((second[0]['script'], second[0]['cue'], second[0]['name']), ('GS_Terran1_P2', 'Ch1_Maintenance', 'terran_cadet-M1-slot2'))
        self.assertEqual((second[2]['script'], second[2]['cue']), ('Story_Yaki_P2', 'Ch1_Escort'))
        self.assertEqual([r['index'] for r in rows], list(range(1, 51)))

    def test_a_slot_that_claims_the_stock_script_watches_it_instead_of_slot_1(self):
        rows = milestones.watches(session({'2': {'story': STORY, 'plots': []}}))
        self.assertEqual([(r['slot'], r['script']) for r in rows if r['script'] == 'GS_Terran1'], [(2, 'GS_Terran1'), (2, 'GS_Terran1')])
        self.assertTrue(all(r['slot'] == 1 for r in rows if r['script'] == 'Story_Yaki'))
        self.assertEqual(milestones.story_roots(session({'2': {'story': STORY}})), [(2, 'GS_Terran1.Start')])

    def test_slot_1_watches_only_a_story_the_session_has(self):
        """A session without the Terran story (no DLC needed) names none of its scripts; with the cadet start as its base start
        slot 1 watches the stock scripts."""
        self.assertEqual(milestones.watches(dict(session({'2': {'story': None, 'plots': []}}), base_start='x4ep1_gamestart_discover')), [])
        rows = milestones.watches(dict(session({}, players=2), base_start='x4ep1_gamestart_terran1'))
        self.assertEqual({(r['slot'], r['script']) for r in rows}, {(1, 'GS_Terran1'), (1, 'Story_Yaki')})

    def test_the_story_roots_of_copies(self):
        self.assertEqual(milestones.story_roots(session({'2': {'plots': copies(2)}, '3': {'plots': copies(3)}})),
                         [(2, 'GS_Terran1_P2.Start'), (2, 'Story_Yaki_P2.Start'), (3, 'GS_Terran1_P3.Start'), (3, 'Story_Yaki_P3.Start')])

    def test_the_listener_md(self):
        s = session({'2': {'plots': copies(2)}})
        rows = milestones.watches(s)
        root = ET.fromstring(milestones.milestones_md(rows, milestones.story_roots(s), 2).split('\n', 2)[2])
        self.assertEqual(root.get('name'), 'X4LAN_Milestones')
        cues = {c.get('name'): c for c in root.find('cues')}
        cue = cues['terran_cadet_M1_S2']
        self.assertIsNone(cue.get('instantiate'))   # once per game: complete in every later save
        # its success: the chapter's clean-up signalled (not the chapter's cancel alone)
        self.assertEqual(cue.find('conditions/event_cue_signalled').get('cue'), 'md.GS_Terran1_P2.Ch1_Clean_Up')
        self.assertIsNone(cue.find('conditions/event_cue_cancelled'))
        self.assertIn('on=signal:Ch1_Clean_Up', cue.find('actions/debug_text').get('text'))
        ch22 = cues['terran_cadet_M24_S2']   # the one chapter whose clean-up is never signalled
        self.assertEqual(ch22.find('conditions/event_cue_cancelled').get('cue'), 'md.Story_Yaki_P2.Ch22_Decide_Outcome')
        index = next(r['index'] for r in rows if r['name'] == 'terran_cadet-M1-slot2')
        self.assertEqual(cue.find('actions/raise_lua_event').get('name'), f"'X4LAN_Milestones.Reached{index}'")
        self.assertEqual(cue.find('actions/raise_lua_event').get('param'), 'player.age')
        self.assertEqual(set(cues) - {f"{r['story']}_M{r['n']}_S{r['slot']}" for r in rows},
                         {'OwnersReset', 'Owner1', 'Owner2', 'OwnersDump', 'SaveRequest'})
        ask = cues['SaveRequest']
        self.assertEqual((ask.find('conditions/event_ui_triggered').get('screen'), ask.find('conditions/event_ui_triggered').get('control')),
                         ("'X4LAN_Saves'", "'Request'"))
        self.assertEqual([e.get('name') for e in ask.findall('actions/raise_lua_event')],
                         ["'X4LAN_Saves.Request'", "'X4LAN_Saves.RequestTime'"])   # the request, then its game time
        self.assertEqual(cues['Owner2'].find('conditions/event_object_signalled').get('param'), "'x4lan_owner_2'")
        dump = cues['OwnersDump'].find('actions')
        # the story roots only say their state (their scripts are their slot's by name); the saved table is sent, a cue
        # as its text (X4 gives a cue to Lua as nil), then the count
        self.assertEqual([e.get('text').split(' cue=')[0] for e in dump.findall('debug_text')][:2], ["'[X4LAN] SAVE_OWNERS_MD root slot=2"] * 2)
        self.assertEqual([e.get('name') for e in dump.findall('raise_lua_event')], ["'X4LAN_Milestones.OwnersEnd'"])
        alive = dump.find('do_if/do_all/do_if')
        self.assertEqual(alive.find('do_if').get('value'), 'typeof md.$X4LAN_Owners.{$i} == datatype.cue')
        self.assertEqual(alive.find('do_if/raise_lua_event').get('param'), "'%s'.[md.$X4LAN_Owners.{$i}]")
        self.assertEqual(alive.find('do_else/raise_lua_event').get('param'), 'md.$X4LAN_Owners.{$i}')

    def test_config_lines(self):
        rows = milestones.watches(session({'2': {'story': STORY}}))
        line, = milestones.config_lines(rows, save=False)
        self.assertTrue(line.startswith("X4LAN_CONFIG.native_gate_milestones = {save=false,watch={{'terran_cadet',3,1},{'terran_cadet',4,1},"), line)
        self.assertTrue(line.endswith("{'terran_cadet',25,1},{'terran_cadet',1,2},{'terran_cadet',2,2}}}"), line)   # slot by slot
        # a session without a story still saves (a person's Save game, the periodic saves): an empty watch list
        self.assertEqual(milestones.config_lines([]), ['X4LAN_CONFIG.native_gate_milestones = {save=true,watch={}}'])


class OwnedScriptTests(unittest.TestCase):
    def test_a_continued_session_names_each_slots_scripts(self):
        s = dict(session({'2': {'plots': copies(2)}, '3': {'story': STORY}}, players=3), **{'continue': {'name': 'c1-t3600'}})
        self.assertEqual(milestones.owned_lines(s), ["X4LAN_CONFIG.native_gate_plot_owned = {{'GS_Terran1_P2',2},{'Story_Yaki_P2',2},"
                                                     "{'GS_Terran1',3}}"])

    def test_a_new_session_claims_at_the_begin_instead(self):
        self.assertEqual(milestones.owned_lines(session({'2': {'plots': copies(2)}})), [])
        self.assertEqual(milestones.owned_lines(dict(session({}), **{'continue': {'name': 'x'}})), [])


class ContinueBlockTests(unittest.TestCase):
    def block(self, **slot):
        return {'format': milestones.CONTINUE_FORMAT, 'name': 'terran_cadet-M1-slot2',
                'slots': {'2': dict({'inventory': [{'ware': 'software_scannerobjectmk3', 'amount': 1}]}, **slot)}}

    def test_the_config_line(self):
        line, = milestones.continue_lines({'continue': self.block(research=['research_radioreceiver'])})
        self.assertEqual(line, "X4LAN_CONFIG.native_gate_continue = {name='terran_cadet-M1-slot2',slots={[2]={inventory="
                               "{{'software_scannerobjectmk3',1}},blueprints={},research={'research_radioreceiver'}}}}")
        line, = milestones.continue_lines({'continue': self.block(active=[3, 'plot'])})
        self.assertIn("research={},active={3,'plot'}}", line)
        self.assertEqual(milestones.continue_lines({'slots': {}}), [])

    def test_what_a_host_could_put_into_a_joiners_config_is_refused(self):
        for slot in ({'inventory': [{'ware': "x'}os.exit()--", 'amount': 1}]}, {'inventory': [{'ware': 'a', 'amount': 0}]},
                     {'inventory': [{'ware': 'a', 'amount': 1.5}]}, {'research': ['a b']}, {'blueprints': [1]},
                     {'inventory': [{'ware': 'a', 'amount': 1}, {'ware': 'a', 'amount': 2}]}, {'active': [0, 'plot']},
                     {'active': [1, "x'}--"]}, {'active': [1.5, 'plot']}, {'active': 'plot'}):
            with self.assertRaises(milestones.MilestoneError, msg=slot):
                milestones.continue_lines({'continue': self.block(**slot)})
        with self.assertRaises(milestones.MilestoneError):
            milestones.continue_lines({'continue': dict(self.block(), name="x'}--")})
        with self.assertRaises(milestones.MilestoneError):
            milestones.continue_lines({'continue': dict(self.block(), slots={'1': {}})})


if __name__ == '__main__':
    unittest.main()
