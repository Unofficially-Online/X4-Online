"""Exit-time tail checks only; full evidence validation remains mandatory."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_gate_live as live

PASS = b'X4LAN LIFECYCLE_DISCARD result=pass reason=physics_frames_returned exit=223 restored=0 resumed=0\n'
FAIL = b'X4LAN LIFECYCLE_DISCARD result=fail reason=camera_anchor_context_changed exit=223 restored=0 resumed=0\n'


class DiscardFailFastTests(unittest.TestCase):
    def test_failure_first_stops_wait_and_success_first_preserves_peer(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = {slot: Path(directory) / str(slot) for slot in (1,2)}
            paths[1].write_bytes(FAIL)
            with self.assertRaisesRegex(RuntimeError, 'client 1:.*reported failure: camera_anchor_context_changed'):
                live.lifecycle_discard_wait({1:223,2:None},paths)
            paths[1].write_bytes(PASS)
            self.assertFalse(live.lifecycle_discard_wait({1:223,2:None},paths))
            paths[2].write_bytes(FAIL)
            with self.assertRaisesRegex(RuntimeError, 'client 2:.*reported failure'):
                live.lifecycle_discard_wait({1:223,2:223},paths)
            paths[2].write_bytes(PASS)
            self.assertTrue(live.lifecycle_discard_wait({1:223,2:223},paths))

    def test_never_reads_trace_until_owned_child_exits(self):
        with mock.patch.object(live,'lifecycle_discard_terminal') as inspect:
            self.assertFalse(live.lifecycle_discard_wait({1:None,2:None},{}))
            inspect.assert_not_called()
            with self.assertRaisesRegex(RuntimeError,'expected exit 223, got -9'):
                live.lifecycle_discard_wait({1:223,2:-9},{})
            inspect.assert_not_called()
        with self.assertRaisesRegex(RuntimeError,'trace path missing'):
            live.lifecycle_discard_wait({1:223},{})

    def test_missing_malformed_truncated_and_repeated_receipts_refuse(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'trace'
            with self.assertRaisesRegex(RuntimeError,'terminal evidence unavailable'):
                live.lifecycle_discard_wait({1:223},{1:path})
            for data in (b'',PASS[:-1],PASS+b'\n',PASS+FAIL,FAIL+PASS,b'not a receipt\n',
                         PASS.replace(b'exit=223',b'exit=0'),PASS.replace(b'restored=0',b'restored=1'),
                         b'prefix '+PASS,PASS.replace(b'reason=physics_frames_returned',b'reason=bad reason')):
                with self.subTest(data=data):
                    path.write_bytes(data)
                    with self.assertRaises(RuntimeError):
                        live.lifecycle_discard_wait({1:223,2:None},{1:path})
            path.unlink();path.symlink_to(Path(directory)/'absent')
            self.assertRaises(RuntimeError,live.lifecycle_discard_terminal,path)
            self.assertRaises(RuntimeError,live.lifecycle_discard_terminal,Path(directory))

    def test_exact_tail_bound_and_discarded_partial_first_line(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'trace';path.write_bytes(b'x'*(3*1024*1024)+b'\n'+PASS)
            original=live.os.fdopen;reads=[];seeks=[]
            class TrackingReader:
                def __init__(self,stream):self.stream=stream
                def __enter__(self):return self
                def __exit__(self,*args):self.stream.close()
                def fileno(self):return self.stream.fileno()
                def seek(self,*args):seeks.append(args);return self.stream.seek(*args)
                def read(self,size):reads.append(size);return self.stream.read(size)
            with mock.patch.object(live.os,'fdopen',side_effect=lambda fd,mode:TrackingReader(original(fd,mode))):
                self.assertEqual(live.lifecycle_discard_terminal(path),('pass','physics_frames_returned'))
            self.assertEqual(reads,[4096]);self.assertEqual(seeks,[(path.stat().st_size-4096,)])
            path.write_bytes(b'x'*8192+PASS)  # no preceding newline within bounded tail
            self.assertRaises(RuntimeError,live.lifecycle_discard_terminal,path)

    def test_mode_size_limits_and_oversize_refusal_before_read(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'trace'
            with path.open('wb') as stream:
                stream.seek(8*1024*1024);stream.write(b'\n'+PASS)
            self.assertRaises(RuntimeError,live.lifecycle_discard_terminal,path)
            self.assertEqual(live.lifecycle_discard_terminal(path,network=True),('pass','physics_frames_returned'))
            with path.open('wb') as stream:stream.truncate(32*1024*1024+1)
            with self.assertRaisesRegex(RuntimeError,'exceeds 32 MiB'):
                live.lifecycle_discard_terminal(path,network=True)

    def test_terminal_pass_is_not_full_evidence_acceptance(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'trace';path.write_bytes(PASS)
            self.assertTrue(live.lifecycle_discard_wait({1:223,2:223},{1:path,2:path}))
            # The wait gate admits final validation; it cannot manufacture stage evidence.
            result=live.lifecycle_discard_evidence(PASS.decode().splitlines(keepends=True),223,strict=True)
            self.assertFalse(result['pass'])


if __name__=='__main__':unittest.main()
