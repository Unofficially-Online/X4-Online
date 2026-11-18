"""tools/dev/native_gate_figure_macros.py: the NPC twins of the player characters (synthetic libraries)."""
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import native_gate_figure_macros as figures  # noqa: E402

COMPONENTS = """<components>
  <component name="character_player_argon_male_01" class="player"><size/></component>
  <component name="character_argon_male_01" class="npc"><size/></component>
  <component name="character_player_odd_01" class="player"><size/></component>
</components>"""
BASE = """<macros>
  <macro name="character_argon_male_base_01_macro" class="npc"><component ref="character_argon_male_01"/></macro>
  <macro name="character_player_a_macro" class="player" ref="character_argon_male_base_01_macro">
    <component ref="character_player_argon_male_01" />
    <properties>
      <identification name="{1,2}" race="argon" faction="player" female="false" image="briefing_x" />
      <models>
        <model type="head" ref="assets/characters/argon/heads/h1" />
        <model type="torso" ref="assets/characters/argon/bodies/b1" />
      </models>
    </properties>
    <connections><connection ref="computer"><macro ref="entity_betty_macro" connection="player" /></connection></connections>
  </macro>
  <!-- <macro name="character_player_commented_macro" class="player"><component ref="character_player_argon_male_01"/></macro> -->
  <macro name="character_player_odd_macro" class="player"><component ref="character_player_odd_01" />
    <properties><models><model type="head" ref="assets/x" /></models></properties></macro>
  <macro name="character_player_bare_macro" class="player"><component ref="character_player_argon_male_01" /></macro>
</macros>"""
DLC = """<macros>
  <macro class="player" name="character_player_b_macro">
    <component ref="character_player_argon_male_01"/>
    <properties><identification race="argon" female="true" name="{3,4}" page="10005"/>
      <models><model ref="assets/characters/argon/heads/h2" type="head"/></models></properties>
  </macro>
</macros>"""


class FigureMacros(unittest.TestCase):
    def tree(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        for folder, name, text in (('base', 'character_components.xml', COMPONENTS), ('base', 'character_macros.xml', BASE),
                                   ('ego_dlc_x', 'character_macros.xml', DLC)):
            (root / folder / 'libraries').mkdir(parents=True, exist_ok=True)
            (root / folder / 'libraries' / name).write_text(text, encoding='utf-8')
        return root

    def test_a_twin_for_every_player_character_an_npc_can_wear(self):
        text = figures.generate(self.tree())
        macros = {m.get('name'): m for m in ET.fromstring(text.encode('utf-8'))}
        # the player macro with a body an NPC has; the one in a stock extension, its attributes in another order
        self.assertEqual(sorted(macros), ['x4lan_figure_character_player_a_macro', 'x4lan_figure_character_player_b_macro'])
        a = macros['x4lan_figure_character_player_a_macro']
        self.assertEqual((a.get('class'), a.get('ref')), ('npc', 'character_argon_male_base_01_macro'))
        self.assertEqual(a.find('component').get('ref'), 'character_argon_male_01')
        self.assertEqual(a.find('properties/identification').attrib, {'name': '{1,2}', 'race': 'argon', 'female': 'false'})
        self.assertEqual([(m.get('type'), m.get('ref')) for m in a.findall('properties/models/model')],
                         [('head', 'assets/characters/argon/heads/h1'), ('torso', 'assets/characters/argon/bodies/b1')])
        self.assertIsNone(a.find('connections'))                      # no ship computer on a figure
        b = macros['x4lan_figure_character_player_b_macro']
        self.assertEqual(b.find('properties/identification').get('page'), '10005')
        self.assertIsNone(b.get('ref'))
        # no NPC body of that name, no models and no macro to take them from, a commented macro: no twin
        self.assertNotIn('odd', text)
        self.assertNotIn('bare', text)
        self.assertNotIn('commented', text)

    def test_nothing_without_libraries_and_never_over_a_file(self):
        with tempfile.TemporaryDirectory() as empty:
            self.assertEqual(figures.generate(empty), '')
            self.assertIsNone(figures.write('', empty))
            written = figures.write('<macros/>', empty)
            self.assertEqual(written, Path(empty) / 'libraries/character_macros.xml')
            with self.assertRaises(SystemExit):
                figures.write('<macros/>', empty)


if __name__ == '__main__':
    unittest.main()
