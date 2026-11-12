"""Explicit immutable startup epochs for disposable Linux diagnostics.

The common tuple is opt-in. It does not reinterpret legacy evidence or imply
that pre-apply frames contain controller Updates.
"""
LEGACY = {
    'version': 0, 'factory': 2, 'seat_request': 2, 'seat_preflight': 4,
    'take': 5, 'capture': 3, 'apply': 5, 'presentation_probe': 7,
    'presentation_arm': 8, 'presentation_first': 9,
}
COMMON = {
    'version': 1, 'factory': 35, 'seat_request': 35, 'seat_preflight': 36,
    'take': 37, 'capture': 38, 'apply': 40, 'presentation_probe': 42,
    'presentation_arm': 43, 'presentation_first': 44,
}


def startup_phases(value=None):
    if value is None:
        return dict(LEGACY)
    if type(value) is not str or value != 'v1-t40':
        raise ValueError('startup phases must be absent or exactly v1-t40')
    return dict(COMMON)


def phases_from_options(options):
    phase = startup_phases(options.get('player_startup_phases'))
    if phase['version'] and not options.get('player_ui_session_frames'):
        raise ValueError('common startup phases require an explicit finite session')
    return phase
