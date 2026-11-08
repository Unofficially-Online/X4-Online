"""Validate the default-off Linux navmesh post observation window."""


def validate_window(window, ticks=None):
    if window is None:
        return
    if (not isinstance(window, (list, tuple)) or len(window) != 2 or
            any(type(value) is not int for value in window)):
        raise ValueError('navmesh post observer requires an inclusive FIRST LAST window')
    first, last = window
    if not 1 <= first <= last <= 9007199254740991:
        raise ValueError('navmesh post observer window must satisfy 1 <= FIRST <= LAST <= 2^53-1')
    if ticks is not None and last > ticks:
        raise ValueError('navmesh post observer window must end within the run')
