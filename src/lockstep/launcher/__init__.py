"""x4ls: start N windowed X4 clients and the lockstep relay on one Windows PC.

Each client runs from its own game view (hard links and junctions into the real
installation, plus a private extensions folder) with its own disposable profile,
so clients can carry different slots and configs. Nothing in the installation or
in a normal profile is changed. See docs/LAUNCHER.md.
"""
