"""The 'ffi' input section codec (tools/dev/native_gate_ffi_relay_core.lua) against records laid out as
lockstep_ui_exports.h captures them (ux_encode: u16 index, then the arguments by kind)."""
from pathlib import Path
import struct
import unittest

from lupa.luajit21 import LuaRuntime

CORE = Path(__file__).resolve().parents[1] / 'tools/dev/native_gate_ffi_relay_core.lua'


def record(index, *parts):
    """A captured call as ux_encode lays it out: u16 index le, then the encoded arguments."""
    return (struct.pack('<H', index) + b''.join(parts)).hex()


class FfiRelayCore(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(CORE.read_text(encoding='utf-8'))
        self.K = self.lua.globals().X4LAN_FFI_RELAY_CORE
        self.kinds = {1: 'qdqdq', 7: 'Ub', 9: 'sis', 3: ''}
        self.lookup = self.lua.eval('function(t) return function(i) return t[i] end end')(self.lua.table_from(self.kinds))

    def decode(self, payload):
        result = self.K.decode(payload, self.lookup)
        if isinstance(result, tuple):
            return None, result[1]
        return [(r.index, list(r.args.values())) for r in result.values()], None

    def test_signature_maps_kinds_to_c_types(self):
        self.assertEqual(self.K.signature('Ub'), 'void(*)(uint64_t,bool)')
        self.assertEqual(self.K.signature(''), 'void(*)(void)')
        self.assertEqual(self.K.signature('qidfs'), 'void(*)(int64_t,int32_t,double,float,const char*)')

    def test_records_decode_as_the_gate_encodes_them(self):
        a = record(1, struct.pack('<q', 0x1122), struct.pack('<d', 1.5), struct.pack('<q', 0x3344), struct.pack('<d', -2.0),
                   struct.pack('<q', 0x5566))
        b = record(7, struct.pack('<Q', 0xabc), b'\x01')
        c = record(9, bytes([5]) + b'hello', struct.pack('<i', -7), b'\xff')
        payload, taken = self.K.pack(self.lua.table_from([a, b, c]), 480)
        self.assertEqual(taken, 3)
        out, why = self.decode(payload)
        self.assertIsNone(why)
        self.assertEqual(out[0][0], 1)
        self.assertEqual(out[0][1], ['0000000000001122', struct.pack('>d', 1.5).hex(), '0000000000003344',
                                     struct.pack('>d', -2.0).hex(), '0000000000005566'])
        self.assertEqual(out[1], (7, ['0000000000000abc', True]))
        self.assertEqual(out[2], (9, ['hello', -7, False]))

    def test_pack_keeps_what_does_not_fit_for_later(self):
        r = record(3)
        payload, taken = self.K.pack(self.lua.table_from([r, r, r]), 2 + 2 * (2 + len(r)))
        self.assertEqual(taken, 2)
        self.assertEqual(payload, '01' + ('02' + r) * 2)
        self.assertEqual(self.K.pack(self.lua.table_from([r]), 4), (None, 0))

    def test_malformed_payloads_refuse(self):
        r = record(7, struct.pack('<Q', 1), b'\x00')
        self.assertEqual(self.decode('02' + '0b' + r)[1], 'version')
        self.assertEqual(self.decode('01' + '0c' + r)[1], 'record_length')
        self.assertEqual(self.decode('01' + '0b' + record(8, struct.pack('<Q', 1), b'\x00'))[1], 'unknown_export')
        self.assertEqual(self.decode('01' + '0a' + r[:-2])[1], 'truncated')
        self.assertEqual(self.decode('01' + '0c' + r + '00')[1], 'record_size')
        self.assertEqual(self.decode('0Z')[1], 'encoding')


class LuaGlobalRecords(unittest.TestCase):
    """Lua-global calls (index 0xffff): name, argument count, tagged arguments; components by portable key."""

    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(CORE.read_text(encoding='utf-8'))
        self.K = self.lua.globals().X4LAN_FFI_RELAY_CORE
        # Components: runtime ID -> portable key; a LuaID is a userdata (newproxy) mapped to its ID.
        self.lua.execute('''
          keys={[4096]='00000000000a0001',[777]='8000000000000003'}
          ids=setmetatable({},{__mode='k'})
          function luaid(id) local u=newproxy();ids[u]=id;return u end
          function component(v,form)
           if form=='number' then return keys[v] end
           if form=='cdata' then return keys[tonumber(v)] end
           return keys[ids[v]]
          end
        ''')

    def record(self, name, args, n, kinds=''):
        return self.K.lua_record(name, self.lua.table_from(args), n, kinds, self.lua.globals().component)

    def decode(self, records):
        payload, taken = self.K.pack(self.lua.table_from(records), 480)
        self.assertEqual(taken, len(records))
        result = self.K.decode(payload, self.lua.eval('function() return nil end'))
        if isinstance(result, tuple):
            return result[1]
        out = []
        for r in result.values():
            args = []
            for i in range(1, r.n + 1):
                v = r.args[i]
                args.append((v.form, v.key) if self.lua.eval('type')(v) == 'table' else v)
            out.append((r.index, r.name, args))
        return out

    def test_money_transfer_round_trips_with_the_container_as_a_component(self):
        rec = self.record('TransferPlayerMoneyTo', [-1250.5, 4096], 2, '-c')
        self.assertEqual(self.decode([rec]), [(0xffff, 'TransferPlayerMoneyTo', [-1250.5, ('number', '00000000000a0001')])])

    def test_a_number_outside_a_component_place_stays_a_number(self):
        rec = self.record('SetMinBudget', [4096, 4096], 2, 'c')
        self.assertEqual(self.decode([rec])[0][2], [('number', '00000000000a0001'), 4096])

    def test_nil_booleans_strings_and_exact_doubles(self):
        rec = self.record('AddInventory', [None, 'inv_timewarp', 0.1, True, False], 5, 'c')
        self.assertEqual(self.decode([rec])[0][2], [None, 'inv_timewarp', 0.1, True, False])

    def test_zero_in_a_component_place_is_no_object(self):
        rec = self.record('TransferMoneyToPlayer', [5, 0], 2, '-c')
        self.assertEqual(self.decode([rec])[0][2], [5, ('number', '0000000000000000')])

    def test_unportable_arguments_refuse(self):
        self.assertEqual(self.record('TransferPlayerMoneyTo', [5, 999], 2, '-c'), (None, 'unportable_object'))
        self.assertEqual(self.record('StartDiplomacyActionOperation', [1, 2, self.lua.table()], 3), (None, 'unportable_table'))
        self.assertEqual(self.record('X', [float('inf')], 1), (None, 'unportable_number'))
        self.assertEqual(self.record('X', ['a' * 201], 1), (None, 'too_long'))

    def test_luaid_and_cdata_components_anywhere(self):
        args = self.lua.eval("function() return {luaid(777), require and nil, (require('ffi')).new('uint64_t', 4096)} end")()
        rec = self.K.lua_record('SetOrderParam', args, 3, '', self.lua.globals().component)
        self.assertEqual(self.decode([rec])[0][2], [('luaid', '8000000000000003'), None, ('cdata', '00000000000a0001')])

    def test_mixed_with_export_records(self):
        export = record(3)
        lua = self.record('SetVirtualCargoMode', [4096, True, -1], 3, 'c')
        payload, taken = self.K.pack(self.lua.table_from([export, lua]), 480)
        result = self.K.decode(payload, self.lua.eval('function(i) if i==3 then return "" end end'))
        self.assertEqual([r.index for r in result.values()], [3, 0xffff])

    def test_malformed_lua_records_refuse(self):
        rec = self.record('AddCargo', [4096, 'energycells', 5, True], 4, 'c')
        self.assertEqual(self.decode([rec[:-2]]), 'truncated')
        self.assertEqual(self.decode([rec + '02']), 'record_size')
        bad_tag = 'ffff' + '01' + '41' + '01' + '09'
        self.assertEqual(self.K.decode('01' + '%02x' % (len(bad_tag) // 2) + bad_tag, self.lua.eval('function() end'))[1], 'tag')
        short = 'ffff' + '05' + '41'
        self.assertEqual(self.K.decode('01' + '%02x' % (len(short) // 2) + short, self.lua.eval('function() end'))[1], 'truncated')


if __name__ == '__main__':
    unittest.main()
