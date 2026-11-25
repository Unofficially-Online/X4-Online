"""The NPCs that fight and police the players take every player's own ship where the stock AI scripts take "the ship the
player flies" (six stock AI script patches, X4's own diff loading; tools/dev/native_gate_player_*.xml).

Static checks of our diffs: what each one replaces or adds, that each new value is the stock one with only our clause in
it, that every new clause leaves the stock line when the gate's tables are absent, and (with lxml and the read-only stock
scripts, X4LAN_GAMEDATA's aiscripts or the x4-re copy) that every selector hits one node and the patched script still
validates against X4's aiscripts.xsd. Nothing here runs X4: what the NPCs do is measured live (docs/PLAYTEST.md).
"""
import copy
import os
from pathlib import Path
import sys
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT / 'tools/dev'
sys.path.insert(0, str(DEV))
import native_gate_defaults as defaults  # noqa: E402

try:
    from lxml import etree
except ImportError:
    etree = None
RE = Path(os.environ.get('X4_RE', ROOT.parent / 'x4-re'))
GAMEDATA = os.environ.get('X4LAN_GAMEDATA')
STOCK_DIRS = ([Path(GAMEDATA) / 'base'] if GAMEDATA else []) + [RE / 'gamescripts/resolved/base']
SKIP_STOCK = 'needs the stock aiscripts read only: X4LAN_GAMEDATA/base/aiscripts or the x4-re copy (and lxml)'

PATCHES = {'aiscripts/move.seekenemies.xml': 'native_gate_player_seekenemies.xml',
           'aiscripts/lib.target.selection.xml': 'native_gate_player_targets.xml',
           'aiscripts/interrupt.disengage.xml': 'native_gate_player_cover.xml',
           'aiscripts/masstraffic.generic.xml': 'native_gate_player_masstraffic.xml',
           'aiscripts/masstraffic.watchdog.xml': 'native_gate_player_watchdog.xml',
           'aiscripts/masstraffic.police.xml': 'native_gate_player_police.xml'}
PILOT = '@player.entity.$X4LAN_Pilots.{%s}'


def diff(name):
    return ET.fromstring((DEV / PATCHES[name]).read_text(encoding='utf-8'))


def stock_path(name):
    for d in STOCK_DIRS:
        if (d / name).is_file():
            return d / name
    return None


def stock_value(name, sel):
    """The stock attribute value a selector names (lxml, the read-only stock copy)."""
    node, = etree.parse(str(stock_path(name))).xpath(sel)
    return str(node)


def patched(name):
    """The stock script with our diff applied the way X4 applies it (replace of an attribute, add after/prepend/append)."""
    doc = etree.parse(str(stock_path(name)))
    for op in etree.parse(str(DEV / PATCHES[name])).getroot():
        if not isinstance(op.tag, str):
            continue
        nodes = doc.xpath(op.get('sel'))
        assert len(nodes) == 1, (name, op.get('sel'), len(nodes))
        node = nodes[0]
        if op.tag == 'replace':
            node.getparent().set(node.attrname, op.text)
            continue
        kids = [copy.deepcopy(k) for k in op]
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


def have_stock():
    return etree is not None and all(stock_path(name) for name in PATCHES)


class CombatAiPatchTests(unittest.TestCase):
    def test_every_patch_ships_with_the_session_defaults_under_the_stock_name(self):
        for name, source in PATCHES.items():
            self.assertEqual(defaults.MD_PATCHES.get(name), source)
            self.assertEqual(diff(name).tag, 'diff')
        # they need the player groups' tables, which md/setup.xml's patch keeps (on by default)
        self.assertEqual(defaults.MD_PATCHES['md/setup.xml'], 'native_gate_player_groups.xml')
        groups = (DEV / 'native_gate_player_groups.xml').read_text(encoding='utf-8')
        self.assertIn('name="$X4LAN_E.$X4LAN_Pilots" exact="$X4LAN_T"', groups)
        self.assertIn('name="$X4LAN_E.$X4LAN_Controlled" exact="$X4LAN_L"', groups)
        self.assertIn("{'native_gate_player_groups',true,", (DEV / 'native_gate_defaults.lua').read_text(encoding='utf-8'))

    def test_without_the_gate_every_new_clause_is_false_and_every_line_the_stock_one(self):
        # A table the gate never set: a lookup through @ is null (false); the mass traffic's new way and new actions
        # open only when the controlled list exists.
        for name in PATCHES:
            for op in diff(name):
                text = (op.text or '') + ''.join(ET.tostring(k, encoding='unicode') for k in op)
                for at in range(len(text)):
                    if text.startswith('$X4LAN_Pilots', at) or text.startswith('$X4LAN_Controlled', at):
                        before = text[max(0, at - 30):at]
                        guarded = before.endswith('@player.entity.') or 'player.entity.$X4LAN_Controlled?' in text
                        self.assertTrue(guarded, '%s: %s' % (name, text[max(0, at - 40):at + 30]))
        mt = diff('aiscripts/masstraffic.generic.xml')
        way = mt[0][0]
        self.assertEqual([(e.tag, e.attrib) for e in way][:2],
                         [('event_gravidar_has_scanned', {'object': 'this.ship'}), ('check_value', {'value': 'player.entity.$X4LAN_Controlled?'})])
        self.assertTrue(mt[1][1].get('value').startswith("event.name == 'event_gravidar_has_scanned' and player.entity.$X4LAN_Controlled? and "))
        self.assertEqual(mt[1][0].attrib, {'name': '$X4LAN_Target', 'exact': 'player.occupiedship'})   # the stock target unless a player 2..N's

    def test_the_hunters_take_any_players_ship_whatever_its_purpose(self):
        seek = diff('aiscripts/move.seekenemies.xml')
        self.assertEqual([(e.tag, e.get('sel')) for e in seek],
                         [('replace', "//check_value[@value='@$detected.{$i} == player.occupiedship']/@value"),
                          ('replace', "//do_elseif[@value='$targetpurposes.count and not $targetpurposes.indexof.{$locobject.primarypurpose} and ($locobject != player.occupiedship)']/@value")])
        self.assertEqual(seek[0].text, '@$detected.{$i} == player.occupiedship or ' + PILOT % '$detected.{$i}')
        self.assertEqual(seek[1].text, '$targetpurposes.count and not $targetpurposes.indexof.{$locobject.primarypurpose} and '
                                       '($locobject != player.occupiedship) and not ' + PILOT % '$locobject')

    def test_the_target_choice_cover_and_watchdog_choice_take_any_players_ship(self):
        self.assertEqual(diff('aiscripts/lib.target.selection.xml')[0].text,
                         'player.age le 1min and ($target == player.occupiedship or %s) and $targets.count gt 1 and $priority gt 1' % (PILOT % '$target'))
        self.assertEqual(diff('aiscripts/interrupt.disengage.xml')[0].text,
                         '($target == player.occupiedship or %s) and this.assignedcontrolled.cansee.{$target}' % (PILOT % '$target'))
        watch = diff('aiscripts/masstraffic.watchdog.xml')
        self.assertEqual(watch[0].get('sel'), "//do_if[@value='$target == player.occupiedship'][@chance='20']/@value")
        self.assertEqual(watch[0].text, '$target == player.occupiedship or ' + PILOT % '$target')

    def test_the_searches_also_read_the_inventory_of_the_player_who_controls_the_ship(self):
        for name, police, n in (('aiscripts/masstraffic.watchdog.xml', '$homebase.zone.policefaction', 4),
                                ('aiscripts/masstraffic.police.xml', 'this.zone.policefaction', 1)):
            ops = [op for op in diff(name) if 'inventory' in op.text]
            self.assertEqual(len(ops), n)
            stock = '@$target.pilot.inventory.illegalto.{%s} or @$target.cargo.illegalto.{%s}' % (police, police)
            want = '@$target.pilot.inventory.illegalto.{%s} or %s.inventory.illegalto.{%s} or @$target.cargo.illegalto.{%s}' % (
                police, PILOT % '$target', police, police)
            for op in ops:
                self.assertEqual(op.text, want, name)
                self.assertTrue(op.get('sel').endswith('/@value') or op.get('sel').endswith('/@exact'))

    def test_hostile_traffic_mirrors_the_stock_test_for_each_player_2_to_n(self):
        mt = diff('aiscripts/masstraffic.generic.xml')
        self.assertEqual([(e.tag, e.get('sel'), e.get('pos')) for e in mt],
                         [('add', "/aiscript/attention[@min='nearby']/actions/wait/interrupt/conditions/check_any/check_all[event_gravidar_has_scanned]", 'after'),
                          ('add', "/aiscript/attention[@min='nearby']/actions/wait/interrupt/actions", 'prepend'),
                          ('replace', "/aiscript/attention[@min='nearby']//set_value[@name='$target'][@exact='player.occupiedship']/@exact", None)])
        self.assertEqual(mt[2].text, '$X4LAN_Target')
        each = mt[0][0].find('check_any')
        self.assertEqual(each.get('counter'), '$X4LAN_I')
        tests = each.find('check_all')
        entity = 'player.entity.$X4LAN_Pilots.{player.entity.$X4LAN_Controlled.{$X4LAN_I}}'
        values = [e.get('value') for e in tests.iter('check_value')]
        self.assertEqual(values[0], 'player.entity.$X4LAN_Controlled.{$X4LAN_I} != player.occupiedship')   # player 1: the stock way
        self.assertIn('this.ship.distanceto.{%s}' % entity, values)
        self.assertEqual([e.get('max') for e in tests.iter('check_value') if e.get('max')], ['250m'])
        self.assertIn('this.hasrelation.kill.{%s.owner}' % entity, values)
        self.assertIn('this.zone.owner.hasrelation.kill.{%s.owner}' % entity, values)
        loop = mt[1][1].find('do_all')
        self.assertEqual(loop.get('counter'), '$X4LAN_I')   # slot order, the first that passes
        self.assertEqual([e.tag for e in loop.find('do_if')], ['set_value', 'break'])

    @unittest.skipUnless(have_stock(), SKIP_STOCK)
    def test_each_new_value_is_the_stock_one_with_our_clause_only(self):
        for name in PATCHES:
            for op in diff(name):
                if op.tag != 'replace':
                    continue
                stock = ' '.join(stock_value(name, op.get('sel')).split())
                if op.text == '$X4LAN_Target':
                    self.assertEqual(stock, 'player.occupiedship')
                    continue
                ours = op.text
                for police in ('$homebase.zone.policefaction', 'this.zone.policefaction'):
                    ours = ours.replace(' or %s.inventory.illegalto.{%s}' % (PILOT % '$target', police), '', 1)
                for clause in (' or ' + PILOT % '$detected.{$i}', ' and not ' + PILOT % '$locobject', ' or ' + PILOT % '$target'):
                    ours = ours.replace(clause, '', 1)
                if ours.startswith('(') and not stock.startswith('('):
                    ours = ours.replace('($target == player.occupiedship)', '$target == player.occupiedship', 1)
                self.assertEqual(ours, stock, '%s: %s' % (name, op.get('sel')))

    @unittest.skipUnless(have_stock(), SKIP_STOCK)
    def test_the_stock_test_the_traffic_mirrors_is_the_one_patched(self):
        doc = etree.parse(str(stock_path('aiscripts/masstraffic.generic.xml')))
        stock, = doc.xpath("/aiscript/attention[@min='nearby']/actions/wait/interrupt/conditions/check_any/check_all[event_gravidar_has_scanned]")
        values = [e.get('value') for e in stock.iter('check_value')]
        self.assertEqual(values, ['player.occupiedship', 'this.ship.distanceto.{player.entity}', 'this.hasrelation.kill.{player.entity.owner}',
                                  'this.zone.owner', 'this.zone.owner.hasrelation.kill.{player.entity.owner}', 'this.hasrelation.friend.{this.zone.owner}'])
        self.assertEqual([e.get('max') for e in stock.iter('check_value') if e.get('max')], ['250m'])
        # the actions' recomputation of player 1's way is the same test as one expression
        first = diff('aiscripts/masstraffic.generic.xml')[1][1].get('value')
        self.assertIn('not (player.occupiedship and this.ship.distanceto.{player.entity} le 250m and (this.hasrelation.kill.{player.entity.owner} or '
                      '(this.zone.owner and this.zone.owner.hasrelation.kill.{player.entity.owner} and this.hasrelation.friend.{this.zone.owner})))', first)

    # Compiling the game schema takes about a minute and a half: opt in with X4LAN_MD_SCHEMA=1 (as the MD schema test).
    @unittest.skipUnless(os.environ.get('X4LAN_MD_SCHEMA') == '1' and have_stock() and (STOCK_DIRS[-1] / 'libraries/aiscripts.xsd').is_file(),
                         'set X4LAN_MD_SCHEMA=1; ' + SKIP_STOCK + ' and aiscripts.xsd')
    def test_every_patched_script_validates_against_the_game_schema(self):
        schema = etree.XMLSchema(etree.parse(str(STOCK_DIRS[-1] / 'libraries/aiscripts.xsd')))
        for name in PATCHES:
            doc = patched(name)
            self.assertTrue(schema.validate(doc), '%s: %s' % (name, schema.error_log))


if __name__ == '__main__':
    unittest.main()
