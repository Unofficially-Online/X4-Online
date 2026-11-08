"""Validation for the default-off Linux station economy observer."""


def validate_configuration(saved_id, window, ticks=None):
    if saved_id is None and window is None:
        return
    if (not isinstance(saved_id, str) or len(saved_id) != 16 or
            any(character not in '0123456789abcdef' for character in saved_id) or
            int(saved_id, 16) == 0 or int(saved_id, 16) >= 1 << 63):
        raise ValueError('economy watch requires one nonzero 16-digit lowercase saved ID')
    if (not isinstance(window, (list, tuple)) or len(window) != 2 or
            any(type(value) is not int for value in window)):
        raise ValueError('economy watch requires an inclusive FIRST LAST window')
    first, last = window
    if first < 1 or last < first or last - first >= 36000 or last > 9007199254740991:
        raise ValueError('economy watch window must cover 1..36000 ticks')
    if ticks is not None and last > ticks:
        raise ValueError('economy watch window must end within the run')
