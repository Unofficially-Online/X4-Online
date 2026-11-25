"""X4's move of a player on foot to its ship's bridge, for players 2..N (lockstep_pilot_teleport.h, native_gate_pilot_teleport,
stop reason 222, a session default).

Covers the probe arming it after the foot slots and the bodies on every client (two synthetic clients under LuaJIT, the real
probe, a mocked gate) and its refusals, the PILOT_TELEPORT rows it writes, the option checker, the defaults, the registry
and the C side's wiring (the pass before the walking checks, the restore before the seats' release, the stop reason's
order, the fade's third site). The native half is tested in C: src/native/engine/test_lockstep_pilot_teleport.h."""
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_pilot_foot as foot  # noqa: E402
import test_native_gate_pilot_stock as stock  # noqa: E402
import check_gate_options  # noqa: E402

ENGINE = ROOT/'src/native/engine'
TELEPORT_H = (ENGINE/'lockstep_pilot_teleport.h').read_text(encoding='utf-8')
REGISTRY = (ROOT/'docs/LOCKSTEP-REGISTRY.txt').read_text(encoding='utf-8')
DEFAULTS = (ROOT/'tools/dev/native_gate_defaults.lua').read_text(encoding='utf-8')

PROBE_TELEPORT = foot.PROBE_FOOT + r'''
  gate.pilot_teleport=function() calls[#calls+1]='teleport';if TELEPORT_REFUSAL then return 0,TELEPORT_REFUSAL end;return 1 end
  teleport_rows=0
  gate.pilot_teleport_frame=function() teleport_rows=teleport_rows+1;if teleport_rows==3 then return 'slot=2 tick=7 room=22fcc2 applied' end;return nil end
'''
ON = dict(native_gate_pilot_foot_bodies=True, native_gate_pilot_foot_slots=True, native_gate_pilot_teleport=True)


class ProbeTests(unittest.TestCase):
    def test_teleport_arms_after_the_foot_slots_and_bodies_on_every_client(self):
        clients = [foot.foot_client(k, prelude=PROBE_TELEPORT, **ON) for k in (1, 2)]
        stock.run(clients, 12)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            calls = c.values('calls')
            self.assertLess(calls.index('bodies'), calls.index('teleport'))
            self.assertLess(calls.index('foot_slots'), calls.index('teleport'))
            logs = c.values('logs')
            self.assertTrue(any(f'PILOT_TELEPORT_ARMED slot={c.slot}' in line for line in logs))
            rows = [line for line in logs if 'PILOT_TELEPORT tick=' in line]
            self.assertEqual(len(rows), 1, rows)
            self.assertIn(' slot=2 tick=7 room=22fcc2 applied', rows[0])
        self.assertNotIn('teleport', foot.foot_client(1, prelude=PROBE_TELEPORT, native_gate_pilot_foot_slots=True).values('calls'))

    def test_refusals_name_themselves(self):
        for extra, prelude, why in (
                (dict(ON, native_gate_pilot_teleport='yes'), PROBE_TELEPORT, 'pilot teleport refused: native_gate_pilot_teleport must be true'),
                (dict(native_gate_pilot_foot_slots=True, native_gate_pilot_teleport=True), PROBE_TELEPORT,
                 'pilot teleport refused: needs native_gate_pilot_foot_slots and native_gate_pilot_foot_bodies'),
                (ON, PROBE_TELEPORT + "TELEPORT_REFUSAL='pins_teleport:7052f0'", 'pilot teleport refused: pins_teleport:7052f0'),
                (ON, PROBE_TELEPORT + 'gate.pilot_teleport=nil', 'pilot teleport refused: gate without pilot_teleport')):
            with self.subTest(why):
                errors = foot.foot_client(1, prelude=prelude, **extra).errors()
                self.assertTrue(any(why in line for line in errors), errors[-2:])


class MirrorTests(unittest.TestCase):
    def test_the_default_follows_its_prerequisites(self):
        self.assertIn("{'native_gate_pilot_teleport',true,'native_gate_pilot_foot_slots native_gate_pilot_foot_bodies',", DEFAULTS)
        self.assertLess(DEFAULTS.index("{'native_gate_pilot_foot_slots',"), DEFAULTS.index("{'native_gate_pilot_teleport',"))
        self.assertLess(DEFAULTS.index("{'native_gate_pilot_foot_bodies',"), DEFAULTS.index("{'native_gate_pilot_teleport',"))
        self.assertIn("[222]='pilot teleport'", DEFAULTS)
        self.assertEqual(check_gate_options.PROBE_OPTIONS['native_gate_pilot_teleport'], (('true',), (), None))

    def test_the_stop_reason_and_its_whys_are_registered(self):
        self.assertIn('#define PTELE_STOP_REASON 222u', TELEPORT_H)
        self.assertRegex(REGISTRY, r'\n  222 PTELE_STOP_REASON +lockstep_pilot_teleport\.h \(native_gate_pilot_teleport')
        self.assertNotRegex(REGISTRY, r'free: [^\n]*\b222\b')
        section = REGISTRY.split('222 PTELE_STOP_REASON', 1)[1].split('\n  2', 1)[0]
        whys = set(re.findall(r'ptele_fail\((?:pslot_leave_named\()?"(\w+)"', TELEPORT_H)) | {'slot_camera', 'slot_connected'}
        self.assertEqual(whys, {'teleport_leave', 'teleport_fault', 'teleport_entry_lost', 'teleport_entry_restore', 'slot_camera', 'slot_connected',
                                'undock_leave', 'undock_fault'})
        self.assertEqual(sorted(w for w in whys if w not in section), [])
        inputs = (ENGINE/'lockstep_pilot_inputs.h').read_text(encoding='utf-8')
        self.assertIn('pchair_failed?226u:ptele_failed?222u:pfoot_failed?', inputs)   # named before the walking checks' 185

    def test_the_pass_and_the_restore_sit_where_the_seats_need_them(self):
        native = (ENGINE/'lockstep_pilot_native.h').read_text(encoding='utf-8')
        passes = native.split('static int pnat_pass(', 1)[1]
        self.assertLess(passes.index('ptele_pass(tick)'), passes.index('pfslot_pass(tick)'))
        self.assertLess(passes.index('pfbody_pass(tick,0)'), passes.index('ptele_pass(tick)'))
        restore = native.split('static void pnat_restore(void) {', 1)[1].split('\n}', 1)[0]
        self.assertLess(restore.index('pfbody_restore();'), restore.index('ptele_restore();'))
        self.assertLess(restore.index('ptele_restore();'), restore.index('pslot_restore();'))
        gate = (ENGINE/'lockstep_gate.c').read_text(encoding='utf-8')
        self.assertLess(gate.index('#include "lockstep_pilot_foot_doors.h"'), gate.index('#include "lockstep_pilot_teleport.h"'))
        self.assertIn('METHOD(pilot_teleport);METHOD(pilot_teleport_frame);', gate)

    def test_the_seat_never_nulls_a_parent_that_x4_holds(self):
        slots = (ENGINE/'lockstep_pilot_slots.h').read_text(encoding='utf-8')
        unseat = slots.split('static void pslot_unseat_player(pslot_slot *s) {', 1)[1].split('\n}', 1)[0]
        self.assertRegex(unseat, r'if\(pslot_listed\(s->player\)\) InterlockedIncrement64\(&pslot_unseat_kept\);\s*else \*\(unsigned char \*volatile\*\)\(s->player\+0x70\)=NULL;')
        death = (ENGINE/'lockstep_pilot_death.h').read_text(encoding='utf-8')
        self.assertIn('#define PVEHK_FADE_RET_TELEPORT 0x713c44u', death)
        self.assertIn('(ptele_fade_site && ret==PVEHK_FADE_RET_TELEPORT)', death)


if __name__ == '__main__':
    unittest.main()
