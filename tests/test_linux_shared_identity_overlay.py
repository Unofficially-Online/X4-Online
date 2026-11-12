"""The opt-in identity layer prepares an overlay; it never starts X4."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import build_shared_identity_overlay as bridge


class SharedIdentityOverlayTests(unittest.TestCase):
    def base(self, directory):
        base = Path(directory) / 'base'
        for name in ('native', 'ui', 'md'):
            (base / name).mkdir(parents=True, exist_ok=True)
        (base / 'native/x4lan_gate.so').write_bytes(b'fixture')
        (base / 'ui/native_gate_probe.lua').write_text('-- fixture\n')
        (base / 'ui.xml.files').write_text('ui/native_gate_probe.lua\n')
        (base / 'ui/config.lua.append').write_text(
            'X4LAN_CONFIG.native_gate_hash = "battle"\n'
            'X4LAN_CONFIG.native_gate_world_identities = true\n'
            'X4LAN_CONFIG.native_gate_snapshot_sha = "' + 'a' * 64 + '"\n'
            "X4LAN_CONFIG.native_gate_linux_startup_phases = 'v1-t40'\n"
            'X4LAN_CONFIG.native_gate_linux_ui_session_frames = 900\n'
            'X4LAN_CONFIG.native_gate_lookahead = 0\n'
            "X4LAN_CONFIG.native_gate_stock_keyboard_replay_probe = {last_capture=937, neutral_ship='0000000000175310', independent_players=true}\n")
        self.seal(base)
        return base

    def seal(self, directory):
        manifest = {path.relative_to(directory).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in directory.rglob('*') if path.is_file() and path.name != 'x4lane-manifest.json'}
        (directory / 'x4lane-manifest.json').write_text(json.dumps(manifest))

    def test_plan_and_script_order(self):
        with tempfile.TemporaryDirectory() as directory:
            base = self.base(directory)
            out = Path(directory) / 'out'
            digest = bridge.build(base, out, '00000000000ad43a', bridge.TARGET, 900)
            self.assertEqual(digest, 'bb588d3e9d290c2339220cbae3b122127427a85d513447a598750e979457fbcd')
            self.assertEqual((out / 'ui.xml.files').read_text().splitlines(),
                             [*bridge.ADAPTERS, 'ui/native_gate_probe.lua'])
            self.assertIn('native_gate_shared_identity_t40', (out / 'ui/config.lua.append').read_text())
            self.assertTrue((out / 'md/native_gate_player_identity.xml').is_file())
            self.assertEqual((out / 'libraries/factions.xml').read_bytes(),
                             (ROOT / 'src/lockstep/base/libraries/factions.xml').read_bytes())
            self.assertFalse((base / 'md/native_gate_player_identity.xml').exists())
            (base / 'ui.xml.files').write_text('ui/native_gate_probe.lua\nui/native_gate_probe.lua\n')
            self.seal(base)
            with self.assertRaises(ValueError):
                bridge.build(base, Path(directory) / 'duplicate', '00000000000ad43a', bridge.TARGET, 900)

    def test_rejects_unsealed_wrong_mode_and_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            base = self.base(directory)
            out = Path(directory) / 'out'
            with self.assertRaises(ValueError):
                bridge.build(base, out, '00000000000ad43a', '0000000000000001', 900)
            config = base / 'ui/config.lua.append'
            config.write_text(config.read_text().replace("'v1-t40'", "'other'"))
            self.seal(base)
            with self.assertRaises(ValueError):
                bridge.build(base, out, '00000000000ad43a', bridge.TARGET, 900)
            self.assertFalse(out.exists())
            config.write_text(config.read_text().replace("'other'", "'v1-t40'") +
                              'X4LAN_CONFIG.native_gate_linux_ui_session_frames = 180\n')
            self.seal(base)
            with self.assertRaises(ValueError):
                bridge.build(base, out, '00000000000ad43a', bridge.TARGET, 900)
            config.write_text(config.read_text() + '-- tampered\n')
            with self.assertRaises(SystemExit):
                bridge.build(base, out, '00000000000ad43a', bridge.TARGET, 900)

    def test_windows_layer_authenticates_the_actual_plan_and_timing(self):
        with tempfile.TemporaryDirectory() as directory:
            base = self.base(directory)
            (base / 'native/x4lan_gate.so').unlink()
            (base / 'native/x4lan_gate_64.dll').write_bytes(b'fixture')
            for name in bridge.ADAPTERS:
                (base / name).write_text('-- original fixture\n')
            (base / 'md/native_gate_player_identity.xml').write_text('<fixture/>')
            (base / 'ui.xml.files').write_text('\n'.join([*bridge.ADAPTERS, 'ui/native_gate_probe.lua']) + '\n')
            plan = {'version': 1, 'mode': 'coop', 'tick': 20, 'occupied': 'player', 'reputation': 'mirror',
                    'slots': [{'faction': 'player', 'ship': '00000000000ad43a'},
                              {'faction': 'player', 'ship': bridge.TARGET}]}
            (base / 'player-identity-plan.json').write_text(json.dumps(plan))
            (base / 'ui/config.lua.append').write_text(
                'X4LAN_CONFIG.native_gate_hash = "battle"\n'
                'X4LAN_CONFIG.native_gate_world_identities = true\n'
                'X4LAN_CONFIG.native_gate_snapshot_sha = "' + 'a' * 64 + '"\n'
                'X4LAN_CONFIG.native_gate_pilot_inputs = true\nX4LAN_CONFIG.native_gate_pilot_stock = true\n'
                "X4LAN_CONFIG.native_gate_pilot_source = 'wire'\n"
                "X4LAN_CONFIG.native_gate_pilot_stock_slots = 'all'\n"
                'X4LAN_CONFIG.native_gate_pilot_suppress_local = true\n'
                "X4LAN_CONFIG.native_gate_pilot_slots = {'00000000000ad43a','0000000000175310'}\n"
                "X4LAN_CONFIG.native_gate_pilot_capture_source = {'stock','stock'}\n"
                'X4LAN_CONFIG.native_gate_player_identity = true\n'
                'X4LAN_CONFIG.native_gate_player_identity_plan = ' + bridge.identity.lua_literal(plan) + '\n'
                'X4LAN_CONFIG.native_gate_lookahead = 0\nX4LAN_CONFIG.native_gate_pilot_delay = 2\n'
                'X4LAN_CONFIG.native_gate_pilot_window = {40,939}\n')
            self.seal(base)
            out = Path(directory) / 'out'
            self.assertEqual(bridge.build_windows(base, out, '00000000000ad43a', bridge.TARGET, 900),
                             'bb588d3e9d290c2339220cbae3b122127427a85d513447a598750e979457fbcd')
            (base / 'ui.xml.files').write_text('\n'.join([bridge.ADAPTERS[1], bridge.ADAPTERS[0], 'ui/native_gate_probe.lua']) + '\n')
            self.seal(base)
            with self.assertRaises(ValueError):
                bridge.build_windows(base, Path(directory) / 'wrong-order', '00000000000ad43a', bridge.TARGET, 900)
            (base / 'ui.xml.files').write_text('\n'.join([*bridge.ADAPTERS, 'ui/native_gate_probe.lua', 'ui/native_gate_probe.lua']) + '\n')
            self.seal(base)
            with self.assertRaises(ValueError):
                bridge.build_windows(base, Path(directory) / 'duplicate', '00000000000ad43a', bridge.TARGET, 900)
            (base / 'ui.xml.files').write_text('\n'.join([*bridge.ADAPTERS, 'ui/native_gate_probe.lua']) + '\n')
            config = base / 'ui/config.lua.append'
            config.write_text(config.read_text() + 'X4LAN_CONFIG.native_gate_pilot_delay = 3\n')
            self.seal(base)
            with self.assertRaises(ValueError):
                bridge.build_windows(base, Path(directory) / 'wrong', '00000000000ad43a', bridge.TARGET, 900)
            self.assertFalse((Path(directory) / 'wrong').exists())

    def test_lua_bridge_rejects_other_plans(self):
        script = '''
        dofile(arg[1])
        local K=X4LAN_PLAYER_IDENTITY_CORE
        local plan={version=1,mode='coop',tick=20,occupied='player',reputation='mirror',
          slots={{faction='player',ship='00000000000ad43a'},{faction='player',ship='0000000000175310'}}}
        local expected={'00000000000ad43a','0000000000175310',900}
        local normalized=assert(K.validate(plan))
        assert(K.shared_t40(normalized,expected))
        assert(not K.shared_t40(normalized,{expected[2],expected[1],900}))
        plan.tick=21; assert(not K.shared_t40(assert(K.validate(plan)),expected))
        plan.tick=20; plan.starts={{object=expected[2],sector='0000000000000729',x=1,y=2,z=3}}
        assert(not K.shared_t40(assert(K.validate(plan)),expected))
        '''
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / 'identity.lua'
            fixture.write_text(script)
            subprocess.run(['luajit', str(fixture), str(ROOT / 'tools/dev/native_gate_player_identity_core.lua')],
                           check=True, capture_output=True, text=True)


    def test_glue_checks_occupied_and_marks_real_setup(self):
        script = r"""
        local p1='00000000000ad43a'; local p2='0000000000175310'
        X4LAN_CONFIG={slot='p1',native_gate_player_identity=true,native_gate_world_identities=true,
          native_gate_shared_identity_t40={p1,p2,900},native_gate_player_identity_plan={version=1,mode='coop',tick=20,
          occupied='player',reputation='mirror',slots={{faction='player',ship=p1},{faction='player',ship=p2}}}}
        local events={}; local occupied=100
        DebugError=function() end; RegisterEvent=function() end; GetClusters=function() return {} end
        ConvertStringToLuaID=function(value) return value end
        AddUITriggeredEvent=function(_,_,step) events[#events+1]=step end
        local ffi={typeof=function() return true end,cdef=function() end,new=function() return {} end,
          string=function(x) return x end,C={GetPlayerOccupiedShipID=function() return occupied end,
          GetNumAllFactions=function() return 1 end,GetAllFactions=function(buf) buf[0]='player'; return 1 end,
          GetNumAllFactionShips=function() return 2 end,GetAllFactionShips=function(buf) buf[0]=100;buf[1]=200;return 2 end,
          IsComponentOperational=function() return true end}}
        require=function(name) assert(name=='ffi'); return ffi end
        X4LAN_SHA256={digest=function() return string.rep('a',64) end}
        dofile(arg[1]); dofile(arg[2])
        local gate={saved_identity=function(id) if id=='100' then return p1 elseif id=='200' then return p2 end end}
        local adapter=X4LAN_NATIVE_PLAYER_IDENTITY; adapter.bind(gate,2)
        assert(adapter.frame(1):match(':shared_t40:900$'))
        if arg[3]=='wrong' then
          occupied=200
          local ok=pcall(adapter.frame,20); assert(not ok and #events==0)
        else
          local row=adapter.frame(20)
          assert(row:match(':shared_t40:900$') and #events==4)
        end
        """
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / 'identity-glue.lua'
            fixture.write_text(script)
            for case in ('wrong', 'valid'):
                subprocess.run(['luajit', str(fixture), str(ROOT / 'tools/dev/native_gate_player_identity_core.lua'),
                                str(ROOT / 'tools/dev/native_gate_player_identity.lua'), case],
                               check=True, text=True)


if __name__ == '__main__':
    unittest.main()
