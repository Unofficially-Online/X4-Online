"""The 'ffi' input section codec (tools/dev/native_gate_ffi_relay_core.lua) against records laid out as
lockstep_ui_exports.h captures them (ux_encode: u16 index, then the arguments by kind)."""
from pathlib import Path
import struct
import unittest

from lupa import LuaRuntime

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


if __name__ == '__main__':
    unittest.main()
