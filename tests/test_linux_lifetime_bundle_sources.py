"""Lifetime-enabled bundles freeze every source needed for the V5 preload."""
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
spec = importlib.util.spec_from_file_location('linux_gate_live', ROOT / 'tools/dev/linux_gate_live.py')
live = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)


class LifetimeBundleSourcesTests(unittest.TestCase):
    def test_requires_exact_v5_activation(self):
        receipt = 'X4 PLAYER LIFETIME PRELOAD ACTIVE version=5'
        self.assertTrue(live.validate_lifetime_receipts([receipt]))
        for receipts in ([], [receipt, receipt], [receipt.replace('5', '3')],
                         ['X4 PLAYER LIFETIME PRELOAD REFUSED']):
            self.assertFalse(live.validate_lifetime_receipts(receipts))

    def test_wrappers_are_required_and_available(self):
        sources = live.lifetime_preload_sources(ROOT)
        wrapper = ROOT / 'src/native/engine/linux/player_lifetime_preload_wrappers.h'
        self.assertIn(wrapper, sources)
        self.assertTrue(all(source.is_file() for source in sources))
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            for source in sources:
                destination = repo / source.relative_to(ROOT)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
            frozen = live.lifetime_preload_sources(repo)
            self.assertEqual(len(frozen), len(sources))
            bundle = repo / 'bundle'
            live.freeze_source_receipts(repo, bundle, frozen)
            receipt = bundle / 'source-receipts' / wrapper.relative_to(ROOT)
            self.assertEqual(receipt.read_bytes(), wrapper.read_bytes())
            (repo / wrapper.relative_to(ROOT)).unlink()
            with self.assertRaises(SystemExit):
                live.lifetime_preload_sources(repo)
