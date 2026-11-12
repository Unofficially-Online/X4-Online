"""Discover the focused disposable network physics evidence suite."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
from test_linux_player_network_evidence import NetworkEvidenceTest, ThrottleEvidenceTest

__all__ = ['NetworkEvidenceTest', 'ThrottleEvidenceTest']
