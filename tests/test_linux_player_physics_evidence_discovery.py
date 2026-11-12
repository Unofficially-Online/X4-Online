"""Expose the focused physics evidence suites to standard test discovery.

The source suites remain in tools/dev because the diagnostic tools use their
fixtures directly. Importing their TestCase classes runs each test once.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))

from test_linux_player_physics_evidence import PhysicsEvidenceTest
from test_linux_player_frames_evidence import RepeatedPhysicsEvidenceTest

__all__ = ['PhysicsEvidenceTest', 'RepeatedPhysicsEvidenceTest']
