import gzip
import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('snapshot_fixture_builder',ROOT/'tools/dev/build_native_gate_overlay.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
spec=importlib.util.spec_from_file_location('snapshot_fixture_validator',ROOT/'tools/dev/validate_native_gate.py')
validator=importlib.util.module_from_spec(spec);spec.loader.exec_module(validator)


class FixtureNavRngOverlayTests(unittest.TestCase):
    """Build real overlay configs without compiling or loading native code."""
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.snapshot = self.root / 'save.xml.gz'
        self.snapshot.write_bytes(gzip.compress(SnapshotFixtureTests().fixture().encode()))
        self.gate = self.root / 'gate.bin'
        self.gate.write_bytes(b'fake native gate')
        self.builds = 0

    def build(self, *extra, linux=True, fixture=True):
        self.builds += 1
        self.output = self.root / f'overlay{self.builds}'
        args = ['overlay', '--gate-so' if linux else '--gate-dll', str(self.gate),
                '--snapshot', str(self.snapshot), '--out', str(self.output)]
        if fixture:
            args += ['--mode', 'battle', '--snapshot-prebind', '--serial-pathing', '--world-identities']
        with mock.patch.object(sys, 'argv', [*args, *extra]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            builder.main()
        return (self.output / 'ui/config.lua.append').read_text()

    def test_linux_fixture_rng_is_opt_in_without_implicit_nav_diag_or_cadence(self):
        baseline = self.build()
        self.assertNotIn('native_gate_nav_rng', baseline)
        config = self.build('--nav-rng')
        self.assertIn('native_gate_nav_rng = true', config)
        self.assertIn('native_gate_snapshot_fixture = {', config)
        self.assertNotIn('native_gate_nav_diag', config)
        self.assertNotIn('native_gate_nav_initial_refresh', config)
        self.assertNotIn('native_gate_nav_cadence', config)
        config = self.build('--nav-rng', '--nav-cadence', 'far', '--nav-params-init')
        self.assertIn("native_gate_nav_cadence = 'far'", config)
        self.assertIn('native_gate_nav_params_init = true', config)

    def test_windows_nav_rng_keeps_legacy_diagnostic_companion(self):
        config = self.build('--nav-rng', linux=False)
        self.assertIn('native_gate_nav_rng = true', config)
        self.assertIn('native_gate_nav_diag = true', config)

    def test_linux_nav_rng_refuses_missing_prerequisites_before_output(self):
        cases = (
            ('--mode', 'clock', '--world-identities', '--serial-pathing'),
            ('--mode', 'battle', '--snapshot-prebind', '--world-identities'),
            ('--mode', 'battle', '--snapshot-prebind', '--serial-pathing'),
        )
        for options in cases:
            with self.subTest(options=options), self.assertRaises(SystemExit):
                self.build('--nav-rng', *options, fixture=False)
            self.assertFalse(self.output.exists())

    def test_linux_nav_rng_refuses_key_conflict_and_unported_companions(self):
        for flag in ('--nav-key', '--nav-diag', '--nav-initial-refresh'):
            with self.subTest(flag=flag), self.assertRaises(SystemExit):
                self.build('--nav-rng', flag)
            self.assertFalse(self.output.exists())

    def test_linux_nav_rng_requires_twelve_valid_snapshot_actors(self):
        xml = SnapshotFixtureTests().fixture()
        for before, after in (('<value type="component" value="[0xc]"/>', ''),
                              ('value="[0xc]"', 'value="[0xb]"'),
                              ('class="navigation"', 'class="docking"')):
            self.snapshot.write_bytes(gzip.compress(xml.replace(before, after, 1).encode()))
            with self.subTest(change=before), self.assertRaises(SystemExit):
                self.build('--nav-rng')
            self.assertFalse(self.output.exists())


class SnapshotFixtureTests(unittest.TestCase):
    def fixture(self):
        components=''.join(f'<component id="[0x{i:x}]" class="ship_s" owner="{("argon" if i<=6 else "xenon")}" macro="{("ship_arg_s_fighter_01_a_macro" if i<=6 else "ship_xen_s_fighter_01_a_macro")}"><movement class="navigation"/></component>' for i in range(1,13))
        values=''.join(f'<value type="component" value="[0x{i:x}]"/>' for i in range(1,13))
        return f'<savegame><universe>{components}</universe><script><refs type="list"><ref id="77">{values}</ref></refs></script><md><script name="X4LAN_NativeGate"><cue name="Spawn" state="complete"><vars><value name="$ships" type="list" value="77"/></vars></cue></script></md></savegame>'

    def parse(self,text):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'save.xml.gz'
            with gzip.open(path,'wt') as stream:stream.write(text)
            return builder.snapshot_fixture(path)

    def test_follows_exact_named_cue_and_list_order(self):
        xml=self.fixture().replace('<universe>','<universe><component id="[0xff]"/>')
        self.assertEqual(self.parse(xml),[f'{i:016x}' for i in range(1,13)])
        # A plausible whole-file ID list in another script does not replace the fixture.
        with self.assertRaises(ValueError):self.parse(xml.replace('name="X4LAN_NativeGate"','name="OtherScript"'))

    def test_rejects_incomplete_duplicate_wrong_type_and_wrong_macro(self):
        xml=self.fixture()
        changes=[('state="complete"','state="waiting"'),('type="list" value="77"','type="list" value="78"'),
                 ('value="[0xc]"','value="[0xb]"'),('<value type="component" value="[0xc]"/>',''),
                 ('macro="ship_arg_s_fighter_01_a_macro"','macro="other_macro"'),
                 ('class="ship_s"','class="station"'),('owner="argon"','owner="player"'),
                 ('class="navigation"','class="docking"'),('id="[0xc]"','id="[0xff]"'),
                 ('<ref id="77">','<ref id="77"><value type="integer" value="12"/>')]
        for before,after in changes:
            with self.subTest(change=before),self.assertRaises(ValueError):self.parse(xml.replace(before,after,1))

    def test_rejects_duplicate_named_cue_and_component_even_if_valid(self):
        xml=self.fixture()
        cue='<cue name="Spawn" state="complete"><vars><value name="$ships" type="list" value="77"/></vars></cue>'
        with self.assertRaises(ValueError):self.parse(xml.replace(cue,cue+cue))
        duplicate='<component id="[0x1]" class="ship_s" owner="argon" macro="ship_arg_s_fighter_01_a_macro"><movement class="navigation"/></component>'
        with self.assertRaises(ValueError):self.parse(xml.replace('<universe>','<universe>'+duplicate))

    def test_native_evidence_requires_pre_frame_binding_and_later_exact_enrollment(self):
        ids=[f'{i:016x}' for i in range(1,13)]
        config='native_gate_snapshot_fixture = {'+','.join(repr(s) for s in ids)+'}\nnative_gate_nav_diag = true\nnative_gate_nav_initial_refresh = true\n'
        rows=[f'NATIVE_GATE SNAPSHOT_ACTOR id={i} saved={i:016x} runtime={100+i:016x} flight=spline bound_tick=0 prepared_tick=1 nav_tick=1 nav_calls=1 initialized=1 time=3ff0000000000000 deadline_before=bff0000000000000 deadline_after=3ff0000000000000 rng_before=0000000000000001 rng_after=0000000000000002' for i in range(1,13)]
        rows += [f'NATIVE_GATE SNAPSHOT_ENROLL id={i} saved={i:016x}' for i in range(1,13)]
        log='\n'.join(rows)+'\nNATIVE_GATE SNAPSHOT_BIND_STOP bound=12 prepared=12 enrolled=12 failures=0\n'
        errors=[];evidence=validator.snapshot_fixture_evidence(log,config,1,120,errors)
        self.assertEqual(errors,[]);self.assertEqual(len(evidence),12)
        errors=[];other=validator.snapshot_fixture_evidence(log.replace('runtime=0000000000000065','runtime=000000000000ffff'),config,2,120,errors)
        self.assertEqual(errors,[]);self.assertEqual(evidence,other)
        for before,after in [('bound_tick=0','bound_tick=1'),('nav_tick=1','nav_tick=2'),('initialized=1','initialized=0'),
                             ('enrolled=12','enrolled=11'),('failures=0','failures=1'),('SNAPSHOT_ENROLL id=12','SNAPSHOT_ENROLL id=11')]:
            with self.subTest(change=before):
                errors=[];validator.snapshot_fixture_evidence(log.replace(before,after,1),config,1,120,errors);self.assertTrue(errors)
        errors=[];validator.snapshot_fixture_evidence(log,config+'native_gate_nav_rng = true\n',1,120,errors)
        self.assertTrue(errors) # Unseeded first callback cannot claim RNG coverage.

    def test_loaded_deadline_difference_is_retained_separately_from_native_result(self):
        left=[{'id':'6','deadline_before':'old_a','deadline_after':'new','rng_before':'seed','rng_after':'advanced'}]
        right=[dict(left[0],deadline_before='old_b')]
        self.assertNotEqual(left,right)
        self.assertEqual(validator.snapshot_navigation_results(left),validator.snapshot_navigation_results(right))
        right[0]['deadline_after']='different'
        self.assertNotEqual(validator.snapshot_navigation_results(left),validator.snapshot_navigation_results(right))
        right[0]['deadline_after']='new';right[0]['rng_after']='different'
        self.assertNotEqual(validator.snapshot_navigation_results(left),validator.snapshot_navigation_results(right))


if __name__=='__main__':unittest.main()
