"""Requested executable verification must finish or fail before building anything."""
import contextlib
import importlib.util
import io
from pathlib import Path
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'build_portable_math_preload', ROOT / 'tools/dev/build_portable_math_preload.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class RequestedVerification(unittest.TestCase):
    def test_executable_argument_cannot_silently_skip_testing(self):
        error = io.StringIO()
        with mock.patch('sys.argv', ['builder', '--elf', '/read-only/X4']), \
                mock.patch.object(builder.subprocess, 'run') as compile_, \
                contextlib.redirect_stderr(error), self.assertRaises(SystemExit) as stopped:
            builder.main()
        self.assertEqual(stopped.exception.code, 2)
        self.assertIn('--elf requires --test', error.getvalue())
        compile_.assert_not_called()

    def test_missing_decoder_fails_before_building(self):
        error = io.StringIO()
        with mock.patch('sys.argv', ['builder', '--test', '--elf', '/read-only/X4']), \
                mock.patch.object(builder.importlib.util, 'find_spec', return_value=None), \
                mock.patch.object(builder.subprocess, 'run') as compile_, \
                contextlib.redirect_stderr(error), self.assertRaises(SystemExit) as stopped:
            builder.main()
        self.assertEqual(stopped.exception.code, 2)
        self.assertIn('--elf requires capstone', error.getvalue())
        compile_.assert_not_called()


if __name__ == '__main__':
    unittest.main()
