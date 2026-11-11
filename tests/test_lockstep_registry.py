"""The shared number registry (docs/LOCKSTEP-REGISTRY.txt) against the sources: one stop reason per owner, one bit per
UI replay family, one wire op per meaning. Offline, no game."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT/'src/native/engine'
REGISTRY = (ROOT/'docs/LOCKSTEP-REGISTRY.txt').read_text(encoding='utf-8')
NOT_REASONS = {'PSLOT_STOP_WAIT', 'PFIRE_STOP', 'PNAT_STOP_IN_MENU', 'PNAT_FULL_STOP'}   # a count and flags, not stop reasons
# Another define for an owner's code: the Linux gate's ports, the lookahead's drop (the protocol 5 number).
ALIASES = {'LMT_STOP_REASON': 'MT_STOP_REASON', 'LXMC_STOP_REASON': 'RC_MINING_STOP', 'LOOKAHEAD_STOP_DROPPED': 'ADAPT_STOP_DROPPED',
           'LINUX_EVENT_RNG_STOP_REASON': 'EVENT_RNG_STOP_REASON', 'LINUX_BUILD_RNG_STOP_REASON': 'BRNG_STOP_REASON',
           'LXSH_STOP_KEYS': 'RC_STOP_REASON'}   # the Linux shape refresh's region-camera keys
# Module defines that carry a core code: 96 world created, 58 spline, the Linux gate's nav (41/42) and shape (45/46) ports.
CORE_PORTS = {'CREATED_STOP_REASON': 96, 'LSJ_STOP_REASON': 58, 'LXN_INSTALL_STOP': 41, 'LXN_STOP_REASON': 42,
              'LXSH_STOP_INSTALL': 45, 'LXSH_STOP_FAILURE': 46}


def sources():
    for path in sorted(ENGINE.rglob('*')):
        if path.suffix in ('.h', '.c') and not path.name.startswith('test_'):
            yield path, path.read_text(encoding='utf-8', errors='replace')


def stop_defines():
    found = {}
    for path, text in sources():
        for name, value in re.findall(r'#define (\w*STOP\w*) (\d+)u?\b', text):
            if name not in NOT_REASONS:
                found.setdefault(name, set()).add((int(value), path.name))
    return found


class StopReasonTests(unittest.TestCase):
    def test_every_module_stop_reason_is_unique(self):
        by_value = {}
        for name, places in stop_defines().items():
            self.assertEqual(len({v for v, _ in places}), 1, name)   # one value wherever it is defined
            value = next(iter(places))[0]
            by_value.setdefault(value, set()).add(ALIASES.get(name, name))
        shared = {value: names for value, names in by_value.items() if len(names) > 1}
        self.assertEqual(shared, {})

    def test_every_module_stop_reason_is_registered(self):
        for name, places in stop_defines().items():
            value = next(iter(places))[0]
            self.assertRegex(REGISTRY, rf'\b{value} [^\n]*\b{name}\b', name)   # its line names the value and the define

    def test_linux_observers_use_registered_stop_reasons(self):
        gate = (ENGINE/'linux/gate_linux.c').read_text(encoding='utf-8')
        for flag, reason in (('economy_rng_installed', 'ECONOMY_RNG_STOP_REASON'),
                             ('path_transform_observer_installed', 'PATH_TRANSFORM_OBSERVER_STOP_REASON')):
            self.assertRegex(gate, rf'if\({flag}[^\n]+finish\({reason}\)')

    def test_no_module_reason_reuses_a_core_literal(self):
        core = set()
        for path in (ENGINE/'lockstep_gate.c', ENGINE/'linux/gate_linux.c'):
            core |= {int(v) for v in re.findall(r'finish\((\d+)\)', path.read_text(encoding='utf-8', errors='replace'))}
        modules = {next(iter(p))[0]: n for n, p in stop_defines().items()}
        # The Linux gate writes some module reasons as literals; each must be that module's own value.
        for value in core:
            if value >= 100:
                self.assertIn(value, modules, value)
        for value, name in modules.items():
            if value < 100:
                self.assertEqual(CORE_PORTS.get(name), value, name)   # 96 the world created module, 58 the Linux spline join


class FamilyTests(unittest.TestCase):
    def test_family_bits_are_unique_and_registered(self):
        inputs = (ENGINE/'lockstep_pilot_inputs.h').read_text(encoding='utf-8')
        bits = {name.lower(): int(value) for name, value in re.findall(r'UIREP_FAMILY_(\w+)=(\d+)', inputs)}
        self.assertEqual(len(set(bits.values())), len(bits))
        self.assertTrue(all(b & (b - 1) == 0 for b in bits.values()))
        families = sum(int(v) for v in re.search(r'UIREP_FAMILIES=([0-9|]+)', inputs).group(1).split('|'))
        self.assertEqual(families, sum(bits.values()))
        for name, bit in bits.items():
            self.assertRegex(REGISTRY, rf'\n  {bit} +{name} ', name)
        core = (ROOT/'tools/dev/native_gate_ui_replay_core.lua').read_text(encoding='utf-8')
        table = re.search(r'K\.FAMILY=\{(.*?)\}', core, re.S).group(1)
        self.assertEqual({k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', table)}, bits)

    def test_ops_are_unique(self):
        replay = (ROOT/'src/native/engine/lockstep_ui_replay.h').read_text(encoding='utf-8')
        ops = {name: int(value, 16) for name, value in re.findall(r'(UIREP_OP_\w+)=(0x[0-9a-f]+)', replay)}
        ops.pop('UIREP_OP_LUA_FIRST')
        ops.update({name: int(value, 16) for name, value in re.findall(r'#define (UI\w+_OP_\w+) (0x[0-9a-f]+)u', replay)})
        values = list(ops.values())
        self.assertEqual(len(set(values)), len(values), ops)
        for value in values:
            self.assertIn(f'0x{value:02x}', REGISTRY)


if __name__ == '__main__':
    unittest.main()


class InputSectionTests(unittest.TestCase):
    def test_input_kinds_are_unique_registered_and_mirrored(self):
        claims = (ENGINE/'lockstep_hit_claims.h').read_text(encoding='utf-8')
        kinds = {name.lower(): int(value, 16) for name, value in re.findall(r'#define INPUT_KIND_(\w+) (0x[0-9a-f]+)u', claims)}
        self.assertEqual(len(set(kinds.values())), len(kinds), kinds)
        section = REGISTRY[REGISTRY.index('5. INPUT SECTIONS'):]
        for name, kind in kinds.items():
            self.assertRegex(section, rf'\n  0x{kind:02x}  {name} ', name)
        lua = (ROOT/'tools/dev/native_gate_inputs_core.lua').read_text(encoding='utf-8')
        table = re.search(r'K\.KIND=\{(.*?)\}', lua).group(1)
        self.assertEqual({k: int(v, 16) for k, v in re.findall(r'(\w+)=(0x[0-9a-f]+)', table)}, kinds)
