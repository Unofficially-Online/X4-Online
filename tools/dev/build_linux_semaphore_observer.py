#!/usr/bin/env python3
"""Build the default-off semaphore observation preload and print its pin.

The caller must freeze this exact file and pass the printed SHA and size to the
separate gate verifier. This does not launch a game or authorize the observer.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--zig", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = root / "src/native/engine/linux/player_semaphore_observer_preload.c"
    args.out.mkdir(parents=True, exist_ok=True)
    library = args.out / "libx4mp_semaphore_observer.so"
    subprocess.run([str(args.zig), "cc", "-target", "x86_64-linux-gnu.2.17", "-std=c11",
                    "-O2", "-fPIC", "-fvisibility=hidden", "-shared", "-Wall", "-Wextra",
                    "-Werror", str(source), "-o", str(library)], check=True)
    content = library.read_bytes()
    print(json.dumps({"path": str(library.resolve()), "size": len(content),
                      "sha256": hashlib.sha256(content).hexdigest()}, sort_keys=True))


if __name__ == "__main__":
    main()
