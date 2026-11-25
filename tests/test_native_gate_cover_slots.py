"""Every player's cover (disguise) and ship stance (native_gate_cover_slots; src/native/engine/lockstep_cover_slots.h and
tools/dev/native_gate_player_cover_manager.xml).

Static checks: the session default and its MD patch, the docked menu's three calls as flight family calls (the hook table,
the call specs, the UI guard's generated table and its inventory), the keys' and readers' wiring in the probe and the
script ops, the stance of the yaki plot's debug list. With lxml and the read-only stock md/cover.xml (X4LAN_GAMEDATA's or
the x4-re copy): every selector of the diff hits one node, each replaced value is the stock one with "the player" made
the ship's own player, and the patched manager names the player of the ship for players 2 and 3 where the stock names
player 1. The C half's behaviour is the gate's own test (build_native_gate.py --test). Nothing here runs X4.
"""
import copy
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT / 'tools/dev'
ENGINE = ROOT / 'src/native/engine'
sys.path.insert(0, str(DEV))
import native_gate_defaults as defaults  # noqa: E402
import native_gate_plot_copies as plot_copies  # noqa: E402
import ui_guard_inventory as inventory  # noqa: E402

try:
    from lxml import etree
except ImportError:
    etree = None
RE = Path(os.environ.get('X4_RE', ROOT.parent / 'x4-re'))
GAMEDATA = os.environ.get('X4LAN_GAMEDATA')
STOCK = [p for p in ([Path(GAMEDATA) / 'base/md/cover.xml'] if GAMEDATA else []) + [RE / 'gamescripts/resolved/base/md/cover.xml'] if p.is_file()]
SKIP_STOCK = 'needs the stock md/cover.xml read only: X4LAN_GAMEDATA/base/md or the x4-re copy (and lxml)'
PATCH = DEV / 'native_gate_player_cover_manager.xml'
LOSE = "//cue[@name='CoverAbilityManager_LoseCover']"


def diff():
    return ET.fromstring(PATCH.read_text(encoding='utf-8'))


def patched():
    """The stock script with our diff applied as X4 applies it: an attribute replaced or added (type="@name"), elements
    added after, prepended or appended."""
    doc = etree.parse(str(STOCK[0]))
    for op in etree.parse(str(PATCH)).getroot():
        if not isinstance(op.tag, str):
            continue
        nodes = doc.xpath(op.get('sel'))
        assert len(nodes) == 1, (op.get('sel'), len(nodes))
        node = nodes[0]
        if op.tag == 'replace':
            node.getparent().set(node.attrname, op.text)
            continue
        if op.get('type', '').startswith('@'):
            node.set(op.get('type')[1:], op.text)
            continue
        kids = [copy.deepcopy(k) for k in op if isinstance(k.tag, str)]
        if op.get('pos') == 'after':
            for k in reversed(kids):
                node.addnext(k)
        elif op.get('pos') == 'prepend':
            for k in reversed(kids):
                node.insert(0, k)
        else:
            for k in kids:
                node.append(k)
    return doc


class DefaultsAndWiringTests(unittest.TestCase):
    def test_on_by_default_with_its_prerequisites_and_md_patch(self):
        entry = next(e for e in defaults.TABLE['on'] if e[0] == 'native_gate_cover_slots')
        self.assertEqual(entry[1], 'true')
        self.assertEqual(set(entry[2]), {'native_gate_pilot_slots', 'native_gate_pilot_present', 'native_gate_ui_replay'})
        on = defaults.ON_KEYS
        for need in ('native_gate_pilot_present', 'native_gate_ui_replay'):
            self.assertLess(on.index(need), on.index('native_gate_cover_slots'))   # armed after what it needs
        self.assertEqual(defaults.MD_PATCHES['md/cover.xml'], 'native_gate_player_cover_manager.xml')
        self.assertEqual(diff().tag, 'diff')
        self.assertIn("'native_gate_cover_slots': (('true',), (), None)", (DEV / 'check_gate_options.py').read_text(encoding='utf-8'))

    def test_the_docked_menus_cover_and_stance_are_flight_family_calls(self):
        for name in ('SetObjectCoverFaction', 'SetPlayerLastCoverFaction', 'SetShipStance'):
            entry = inventory.INVENTORY[('ffi', name)]
            self.assertEqual(entry[-1], inventory.FLIGHT, name)   # the guard leaves the entry to the family's own hook
        self.assertIn('fleet', inventory.INVENTORY[('ffi', 'SetShipStance')])
        table = (ENGINE / 'lockstep_ui_guard_table.h').read_text(encoding='utf-8')
        for name in ('SetObjectCoverFaction', 'SetPlayerLastCoverFaction', 'SetShipStance'):
            row = re.search(r'\{"%s",0x[0-9a-f]+u,UIGUARD_FFI,UIGUARD_G_\w+,(\d+)u,' % name, table)
            self.assertEqual(int(row.group(1)), 512, name)
        replay = (ENGINE / 'lockstep_ui_replay.h').read_text(encoding='utf-8')
        for name, rva, spec, hook in (('SetObjectCoverFaction', '0x1b88f0u', 'Is', 'UIREP_H_COVER_FACTION'),
                                      ('SetPlayerLastCoverFaction', '0x1b8a60u', 's', 'UIREP_H_COVER_LAST'),
                                      ('SetShipStance', '0x1f0ea0u', 'Is', 'UIREP_H_STANCE')):
            self.assertRegex(replay, r'\{"%s",%s,\d,UIREP_FAMILY_FLIGHT,' % (name, rva))
            self.assertIn('{%s,"%s",UIREP_RET_ONE}' % (hook, spec), replay)
        hooks = re.search(r'enum \{UIREP_H_UI=0,(.*?)UIREP_HOOKS\};', replay, re.S).group(1)
        self.assertTrue(hooks.rstrip().endswith('UIREP_H_COVER_FACTION,UIREP_H_COVER_LAST,UIREP_H_STANCE,'))   # the table's order

    def test_the_keys_ride_the_record_and_the_probe_arms_the_module_after_the_ui_replay(self):
        stock = (ENGINE / 'lockstep_pilot_stock.h').read_text(encoding='utf-8')
        self.assertIn('if(pcov_key_replayed(kind,id)) return PSTK_REPLAY;', stock)
        self.assertIn('if(pcov_key_replayed(kind,id)) {pcov_key_walk(header,kind,id,value);return;}', stock)
        cover = (ENGINE / 'lockstep_cover_slots.h').read_text(encoding='utf-8')
        self.assertIn('#define PCOV_KEY_COVER 0x179', cover)
        self.assertIn('#define PCOV_KEY_STANCE 0x17d', cover)
        self.assertIn('#define PCOV_STOP_REASON 246u', cover)
        probe = (DEV / 'native_gate_probe.lua').read_text(encoding='utf-8')
        self.assertLess(probe.index('pilot_view.arm_ui_replay(slot,window) end'), probe.index('gate.cover_slots()==1'))
        self.assertIn("{'native_gate_cover_slots','cover_slots_status','COVER_SLOTS'}", probe)
        # client 2..N: the menus' "player is covered" asks this client's own cover (the gate's reader answers for its slot)
        self.assertIn("Helper.isPlayerCovered=function() return ffi.string(ffi.C.GetPlayerCoverFaction())~='' end", probe)
        gate = (ENGINE / 'lockstep_gate.c').read_text(encoding='utf-8')
        for method in ('cover_slots', 'cover_slots_status', 'cover_slots_look'):
            self.assertIn('METHOD(%s);' % method, gate)
        self.assertIn('if(pcov_installed && !pcov_check()) {finish(PCOV_STOP_REASON);', gate)

    def test_script_ops_and_the_md_fixture(self):
        core = (DEV / 'native_gate_ui_replay_core.lua').read_text(encoding='utf-8')
        for op in ('cover_look', 'cover_ui', 'cover_button', 'cover_key', 'stance_key', 'stance_set', 'cover_blow', 'cover_trigger'):
            self.assertIn("op=='%s'" % op, core)
        scoped = re.search(r'K\.SCOPED=\{([^}]*)\}', core).group(1)
        canonical = re.search(r'K\.CANONICAL=\{([^}]*)\}', core).group(1)
        for op in ('cover_ui', 'cover_button', 'stance_set'):
            self.assertIn(op + '=true', scoped)   # as a person's click: in the presentation scope, captured
        for op in ('cover_look', 'cover_blow', 'cover_trigger'):
            self.assertIn(op + '=true', canonical)   # in every client's plan
        self.assertIn("pilot_stock_inject('action',0x179,0)", core)
        self.assertIn("pilot_stock_inject('action',0x17d,0)", core)
        md = ET.fromstring((DEV / 'native_gate_ui_replay.xml').read_text(encoding='utf-8'))
        blow = md.find("cues/cue[@name='CoverBlow']")
        signal = blow.find('actions/do_if/signal_objects')
        self.assertEqual((signal.get('object'), signal.get('param'), signal.get('param2')), ('$ship', "'LoseCover'", 'event.param3.$scanned'))
        trigger = md.find("cues/cue[@name='CoverTrigger']").find('actions/do_if/signal_objects')   # md.Cover's TriggerCover
        self.assertEqual((trigger.get('object'), trigger.get('param'), trigger.get('param2')), ('player.galaxy', "'Cover'", '[$ship, $faction]'))

    def test_the_yaki_plots_third_debug_cue_gives_the_cover_ability(self):
        self.assertEqual(plot_copies.PLOTS['yaki']['debug'], ('Activate_Story', 'DEBUG_Setup_Wingmate', 'DEBUG_New_Cover_Ability'))


class ManagerPatchTests(unittest.TestCase):
    def test_every_new_reference_to_a_players_table_is_guarded(self):
        # Without the gate the tables do not exist: every lookup through them is behind @, so $X4LAN_P is player.entity.
        for op in diff():
            text = (op.text or '') + ''.join(ET.tostring(k, encoding='unicode') for k in op)
            for at in [m.start() for m in re.finditer(r'\$X4LAN_Pilots', text)]:
                before = text[max(0, at - 70):at]
                self.assertTrue(before.endswith('@player.entity.') or (before.endswith('then player.entity.') and '.exists then' in before),
                                text[max(0, at - 40):at + 30])
        first = diff()[0][0]
        self.assertEqual(first.attrib, {'name': '$X4LAN_P', 'exact': 'if @player.entity.$X4LAN_Pilots.{event.object}.exists then '
                                                                      'player.entity.$X4LAN_Pilots.{event.object} else player.entity'})

    def test_player_1_runs_the_stock_lines_and_players_2_to_n_their_own_copies(self):
        ops = [op for op in diff() if op.get('sel', '').startswith(LOSE) or op.get('sel') == '/mdscript/cues']
        chances = [op for op in ops if op.get('type') == '@chance']
        self.assertEqual([op.get('sel').rsplit('/', 1)[1] for op in chances], ["speak[@line='1506']", "set_value[@name='$LastExposedObject']"])
        for op in chances:
            self.assertEqual(op.text, 'if $X4LAN_P == player.entity then 100 else 0')
        added = [k for op in ops if op.get('pos') == 'after' for k in op]
        say = added[0]   # "Cover exposed" for a player 2..N: said by that player's own script
        self.assertEqual((say.tag, say.get('value')), ('do_if', '$X4LAN_P != player.entity'))
        self.assertEqual([(e.tag, e.get('name') or e.get('ref')) for e in say], [('set_value', '$X4LAN_Say'), ('include_actions', 'X4LAN_CoverSay'),
                                                                               ('remove_value', '$X4LAN_Say')])
        self.assertEqual(say[0].get('exact'), '[1506, null]')
        self.assertEqual((added[1].tag, added[1].get('chance'), added[1].get('name')),
                         ('set_value', 'if $X4LAN_P == player.entity then 0 else 100', '$X4LAN_P.$X4LAN_LastExposedObject'))

    def test_the_slot_cues_speak_in_the_slots_scope_and_end_its_cooldown(self):
        cues = {c.get('name'): c for op in diff() if op.get('sel') == '/mdscript/cues' for c in op}
        self.assertEqual(set(cues), {'X4LAN_CoverDue', 'X4LAN_CoverCooldownSlots', 'X4LAN_CoverSay'})
        for name in ('X4LAN_CoverDue', 'X4LAN_CoverSay'):
            self.assertEqual((cues[name].tag, cues[name].get('purpose')), ('library', 'include_actions'))
        # every player 2..N's own script hears the line (slots 2 to 8), with the player it is for
        says = [(e.tag, e.get('cue'), e.get('param')) for e in cues['X4LAN_CoverSay'].find('actions')]
        self.assertEqual(says, [('signal_cue_instantly', 'md.X4LAN_CoverP%d.Say' % k, '[$X4LAN_P, $X4LAN_Say.{1}, $X4LAN_Say.{2}]')
                                for k in defaults.COVER_SLOTS])
        loop = cues['X4LAN_CoverCooldownSlots']
        self.assertEqual((loop.get('checkinterval'), loop.get('instantiate')), ('1s', 'true'))
        # it looks only while a player 2..N's cooldown runs: the earliest end of them, set where the manager sets one
        self.assertEqual(loop.find('conditions/check_value').get('value'), 'global.$X4LAN_CoverDueNext? and global.$X4LAN_CoverDueNext le player.age')
        each = loop.find('actions/do_for_each')
        self.assertEqual(each.get('in'), 'global.$X4LAN_CoverDue.clone')
        due = each.find('do_elseif').get('value')
        self.assertEqual(due, '$E.$x4ep1_player_cover_ability_cooldown le player.age')
        self.assertEqual([e.tag for e in loop.find('actions/do_else')], ['remove_value', 'remove_value'])
        ended = each.find('do_elseif')
        self.assertIn(('include_actions', 'X4LAN_CoverSay'), [(e.tag, e.get('ref')) for e in ended])
        self.assertIn({'name': '$X4LAN_Say', 'exact': '[1507, @$E.$X4LAN_LastExposedObject]'}, [e.attrib for e in ended.iter('set_value')])
        includes = [op for op in diff() for k in op.iter('include_actions') if k.get('ref') == 'X4LAN_CoverDue']
        self.assertEqual(len(includes), 2)   # after each of the two cooldown lines of the manager
        for op in includes:
            self.assertEqual(op[0].get('value'), '$X4LAN_P != player.entity')   # players 2..N only

    def test_each_players_own_script_says_its_lines_in_its_scope(self):
        probe = (DEV / 'native_gate_probe.lua').read_text(encoding='utf-8')
        self.assertIn("gate.mpp_first('X4LAN_CoverP'..k,k)", probe)
        self.assertEqual(defaults.COVER_SLOTS, (2, 3, 4, 5, 6, 7, 8))
        for k in (2, 3, 8):
            md = ET.fromstring(defaults.cover_slot_md(k).encode('utf-8'))
            self.assertEqual(md.get('name'), 'X4LAN_CoverP%d' % k)
            say = md.find("cues/cue[@name='Say']")
            self.assertEqual(say.get('instantiate'), 'true')
            mine = say.find('actions/do_if')
            self.assertEqual(mine.get('value'), 'event.param.{1} == player.entity')   # slot k's scope: its own player
            lines = [(e.get('line') or e.find('text').get('line'), e.get('actor')) for e in mine.iter('speak')]
            self.assertEqual(lines, [(line, 'player.computer') for line in ('1506', '1507', '1501', '1505')])
            activated = next(e for e in mine.iter('speak') if e.find('text') is not None)
            self.assertEqual(activated.find('name').get('value'), 'event.param.{3}')   # the faction, as the stock line names it
            available = mine.find('do_elseif').get('value')
            self.assertIn('player.entity.hascontext.{event.param.{3}} and event.param.{3}.canhaveplayercover', available)   # X4's own test
        with tempfile.TemporaryDirectory() as tmp:
            defaults.ship_md(Path(tmp))
            self.assertEqual(sorted(p.name for p in (Path(tmp) / 'md').glob('x4lan_cover_p*.xml')), ['x4lan_cover_p%d.xml' % k for k in range(2, 9)])
            self.assertTrue((Path(tmp) / 'md/cover.xml').is_file())

    def test_the_scripted_cover_gives_every_other_player_aboard_what_player_1_aboard_gets(self):
        # md.Cover's CoverInstance (stories, the envoy ships' VisitStations order): the stock block names player 1; the
        # addition after it walks every other player (global.$X4LAN_Players, absent without the gate: nothing happens).
        ops = {op.get('sel'): op for op in diff() if op.get('pos') == 'after'}
        put = ops["//cue[@name='SetCover']/actions/do_if/do_if[@value='$Object.isplayerowned and player.entity.object == $Object']"]
        guard = put[0]
        self.assertEqual(guard.get('value'), '$Object.isplayerowned and global.$X4LAN_Players?')
        each = guard.find('do_for_each')
        self.assertEqual((each.get('name'), each.get('in')), ('$X4LAN_P', 'global.$X4LAN_Players'))
        aboard = each.find('do_if')
        self.assertEqual(aboard.get('value'), '$X4LAN_P != player.entity and @$X4LAN_P.object == $Object')
        self.assertEqual([e.tag for e in aboard if e.tag != 'debug_text'], ['set_value', 'include_actions', 'remove_value', 'set_cover_owner'])
        self.assertEqual(aboard.find('set_value').get('exact'), '[1501, $Faction]')
        self.assertEqual(aboard.find('set_cover_owner').attrib, {'object': '$X4LAN_P', 'faction': '$Faction'})
        off = ops["//cue[@name='LoseCover']/actions/do_else/do_if[@value='player.entity.coverowner']"]
        guard = off[0]
        self.assertEqual(guard.get('value'), "global.$X4LAN_Players? and not (event.param == 'LoseCover' and event.param3)")
        aboard = guard.find('do_for_each/do_if')
        self.assertEqual(aboard.get('value'), '$X4LAN_P != player.entity and $X4LAN_P.coverowner and @$X4LAN_P.object == $Object')
        owned = aboard.find('do_if')   # the stock test of the ship being the player's, as for player 1
        self.assertEqual(owned.get('value'), "$Object.isplayerowned or (event.name == 'event_object_changed_true_owner' and event.param2 == faction.player)")
        self.assertEqual(owned.find('set_value').get('exact'), '[1505, null]')
        self.assertEqual(owned.find('set_cover_owner').attrib, {'object': '$X4LAN_P'})
        for cue, line in (('EntityEntered', '[1501, $Faction]'), ('EntityLeft', '[1505, null]')):
            sel = "//cue[@name='%s']/actions/do_if/speak" % cue
            chance = [op for op in diff() if op.get('sel') == sel and op.get('type') == '@chance']
            self.assertEqual([op.text for op in chance], ['if event.param == player.entity then 100 else 0'])
            say = ops[sel][0]
            self.assertEqual(say.get('value'), 'event.param != player.entity')
            self.assertEqual([(e.tag, e.get('exact') or e.get('ref') or e.get('name')) for e in say],
                             [('set_value', 'event.param'), ('set_value', line), ('include_actions', 'X4LAN_CoverSay'),
                              ('remove_value', '$X4LAN_Say'), ('remove_value', '$X4LAN_P')])

    @unittest.skipUnless(etree is not None and STOCK, SKIP_STOCK)
    def test_the_scripted_cover_additions_sit_after_the_stock_player_1_blocks(self):
        doc = patched()
        block, = doc.xpath("//cue[@name='SetCover']/actions/do_if/do_if[@value='$Object.isplayerowned and player.entity.object == $Object']")
        self.assertEqual(block.getnext().get('value'), '$Object.isplayerowned and global.$X4LAN_Players?')
        self.assertEqual([e.tag for e in block if isinstance(e.tag, str)], ['speak', 'set_cover_owner'])   # player 1's: stock
        block, = doc.xpath("//cue[@name='LoseCover']/actions/do_else/do_if[@value='player.entity.coverowner']")
        self.assertTrue(block.getnext().get('value').startswith('global.$X4LAN_Players?'))
        for cue in ('EntityEntered', 'EntityLeft'):
            speak, = doc.xpath("//cue[@name='%s']/actions/do_if/speak" % cue)
            self.assertEqual(speak.get('chance'), 'if event.param == player.entity then 100 else 0')
            self.assertEqual(speak.getnext().get('value'), 'event.param != player.entity')

    def test_no_cue_of_ours_resets_itself_or_runs_every_check_without_need(self):
        # A self-resetting checker re-evaluated in the same MD update: the first live run hung X4 at the load (25 Nov).
        text = PATCH.read_text(encoding='utf-8')
        self.assertNotIn('reset_cue', text)
        for op in diff():
            for cue in op.iter('cue'):
                if cue.get('checkinterval'):
                    self.assertEqual(cue.get('instantiate'), 'true', cue.get('name'))

    @unittest.skipUnless(etree is not None and STOCK, SKIP_STOCK)
    def test_every_selector_hits_one_node_and_each_value_is_the_stock_one_for_the_ships_player(self):
        stock = etree.parse(str(STOCK[0]))
        for op in etree.parse(str(PATCH)).getroot():
            if not isinstance(op.tag, str):
                continue
            nodes = stock.xpath(op.get('sel')) if op.get('type') or op.tag == 'replace' else None
            if op.tag != 'replace':
                continue
            self.assertEqual(len(nodes), 1, op.get('sel'))
            old, new = str(nodes[0]), op.text
            # player.entity of the stock line is the ship's own player; the stock tests keep their place and order
            if old.startswith('player.entity.$x4ep1_player_cover_ability_cooldown'):
                self.assertEqual(new, old.replace('player.entity', '$X4LAN_P'))
            elif old == 'player.entity':
                self.assertEqual(new, '$X4LAN_P')
            elif old.startswith('not player.entity.$x4ep1_player_cover_ability'):
                self.assertEqual(new, old.replace('player.entity', '$X4LAN_P'))
            elif old == 'event.object == $LastExposedObject':
                self.assertEqual(new, '(event.object == $LastExposedObject) or (event.object == @$X4LAN_P.$X4LAN_LastExposedObject)')
            else:
                self.assertEqual(old, 'event.object.coverowner or (event.object == $LastExposedObject)')
                self.assertEqual(new, old + ' or (event.object == @player.entity.$X4LAN_Pilots.{event.object}.$X4LAN_LastExposedObject)')
        doc = patched()
        actions, = doc.xpath(LOSE + '/actions')
        # every line that named the player names the ship's own player now; the definition is the actions' first line
        self.assertEqual(actions[0].get('name'), '$X4LAN_P')
        named = [e for e in actions.iter() if isinstance(e.tag, str) and e is not actions[0] and 'player.entity' in ' '.join(e.attrib.values())
                 and e.tag not in ('debug_text',)]
        self.assertEqual(sorted({e.tag for e in named}), ['do_if', 'set_value', 'speak'])
        for e in named:   # only the chance of player 1's own lines and of the slot's copies of them, the slots' cooldowns
            if e.tag == 'do_if':
                self.assertEqual(e.get('value'), '$X4LAN_P != player.entity')
                self.assertIn('include_actions', [k.tag for k in e])
                continue
            self.assertTrue(e.get('chance', '').startswith('if $X4LAN_P == player.entity then'), etree.tostring(e))
        self.assertEqual(len(doc.xpath("/mdscript/cues/cue[@name='X4LAN_CoverCooldownSlots']")), 1)
        self.assertEqual(doc.xpath(LOSE + '/actions')[0][-1].get('name'), '$X4LAN_P')   # removed at the end of the frame

    @unittest.skipUnless(etree is not None and STOCK, SKIP_STOCK)
    def test_players_2_and_3_get_their_own_player_where_player_1_gets_the_stock_one(self):
        # A small model of the patched actions' player lines: which entity each one acts on for the ship of slot k.
        doc = patched()
        actions, = doc.xpath(LOSE + '/actions')
        lines = [(e.tag, e.get('object') or e.get('name') or e.get('actor')) for e in actions.iter()
                 if isinstance(e.tag, str) and e.tag in ('set_cover_owner', 'set_value', 'speak', 'signal_objects')]
        self.assertIn(('set_cover_owner', '$X4LAN_P'), lines)
        self.assertIn(('set_value', '$X4LAN_P.$x4ep1_player_cover_ability_cooldown'), lines)
        pilots = {'ship1': 'P1', 'ship2': 'P2', 'ship3': 'P3'}   # $X4LAN_Pilots on every entity, as the player groups set it

        def player_of(ship, gate=True):
            return pilots[ship] if gate and ship in pilots else 'P1'
        for ship, k in (('ship1', 1), ('ship2', 2), ('ship3', 3)):
            p = player_of(ship)
            self.assertEqual(p, 'P%d' % k)
            spoken_here = p == 'P1'   # the stock speak's chance
            self.assertEqual(spoken_here, k == 1)
        self.assertEqual(player_of('ship2', gate=False), 'P1')   # without the gate: the stock lines


if __name__ == '__main__':
    unittest.main()
