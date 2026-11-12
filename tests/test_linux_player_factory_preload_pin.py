"""Verify factory original-byte view accepts only its exact preload trampoline."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/native/engine/linux/test_player_factory_preload_pin.c"


class FactoryPreloadPinTests(unittest.TestCase):
    def test_pinned_window_and_inventory(self):
        default_executable = (
            Path.home() / ".local/share/Steam/steamapps/common/X4 Foundations/X4"
        )
        executable = Path(os.environ.get("X4_LINUX_EXE", default_executable))
        if not executable.is_file():
            self.skipTest("pinned Linux executable unavailable")
        with executable.open("rb") as stream:
            self.assertEqual(
                hashlib.file_digest(stream, "sha256").hexdigest(),
                "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20",
            )
            stream.seek(0x119BD70 - 0x400000)
            factory = stream.read(24552)
        self.assertEqual(
            hashlib.sha256(factory).hexdigest(),
            "4d9b93a1b51872447a36c0f43e05caa2099d2b7c5a85bb13798ea8dae45fb49b",
        )
        self.assertEqual(factory[0x119D112 - 0x119BD70:][:8], bytes.fromhex("f30f52c0f30f53c0"))
        table = (ROOT / "src/native/portable_math/preload/exact_reciprocal_linux_sites.h").read_text()
        windows = re.findall(r"\{(0x[0-9a-fA-F]+)u?,\s*(\d+),", table)
        self.assertEqual(len(windows), 765)
        overlaps = [(index, int(address, 16), int(length))
                    for index, (address, length) in enumerate(windows)
                    if int(address, 16) < 0x119BD70 + 24552
                    and int(address, 16) + int(length) > 0x119BD70]
        self.assertEqual(overlaps, [(133, 0x119D112, 8)])

    def test_preload_integrity(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "factory-pin-test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(SOURCE), "-ldl", "-o", str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
