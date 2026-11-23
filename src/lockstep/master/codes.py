"""Join codes: what a host sends to friends instead of an address (docs/NETPLAY.md, "Join codes").

Eight characters of an alphabet without 0, 1, I and O (they read alike), shown as XXXX-XXXX. People type and paste
codes, so case, dashes and spaces do not matter. There are 32^8 = 2^40 codes against at most a few hundred live games,
and the lookup is rate limited per IP, so guessing the code of a private game is not practical."""
import secrets

ALPHABET = '23456789ABCDEFGHJKLMNPQRSTUVWXYZ'
LENGTH = 8
GROUP = 4                 # characters before the dash of the display form
TEXT_MAX = 32             # longer input is not a code (it is not even looked at)
SEPARATORS = ' -\t\r\n'


def display(letters):
    """'K7QX3FNA' -> 'K7QX-3FNA'."""
    return letters[:GROUP] + '-' + letters[GROUP:]


def new_code(taken):
    """A fresh code in display form that no live game holds (`taken`: a container of display forms)."""
    while True:
        letters = ''.join(secrets.choice(ALPHABET) for _ in range(LENGTH))
        code = display(letters)
        if code not in taken:
            return code


def normalize_code(text):
    """The display form of a typed or pasted code ('k7qx 3fna', 'K7QX-3FNA', 'k7qx3fna'), or None for anything else.
    Only ASCII is accepted: str.upper() maps some other letters to ASCII ones ('ß' to 'SS'), which would make two
    different texts one code."""
    if not isinstance(text, str) or len(text) > TEXT_MAX or not text.isascii():
        return None
    letters = ''.join(character for character in text.upper() if character not in SEPARATORS)
    if len(letters) != LENGTH:
        return None
    if not all(character in ALPHABET for character in letters):
        return None
    return display(letters)
