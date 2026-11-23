"""Join codes of the master (src/lockstep/master/codes.py): the alphabet, the display form, and what counts as one code."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.master import codes  # noqa: E402


class CodeTests(unittest.TestCase):
    def test_a_new_code_has_eight_characters_of_the_alphabet_in_two_groups(self):
        for _ in range(200):
            code = codes.new_code(set())
            self.assertRegex(code, r'^[23456789ABCDEFGHJKLMNPQRSTUVWXYZ]{4}-[23456789ABCDEFGHJKLMNPQRSTUVWXYZ]{4}$')

    def test_the_alphabet_leaves_out_the_characters_that_read_alike(self):
        self.assertEqual(len(codes.ALPHABET), 32)
        for character in '01IO':
            self.assertNotIn(character, codes.ALPHABET)

    def test_a_new_code_is_not_one_a_live_game_holds(self):
        taken = {codes.new_code(set()) for _ in range(50)}
        for _ in range(200):
            self.assertNotIn(codes.new_code(taken), taken)

    def test_case_dashes_and_spaces_do_not_matter(self):
        for text in ('K7QX-3FNA', 'k7qx-3fna', 'K7QX3FNA', 'k7qx 3fna', ' K7-QX-3F-NA ', 'k7qx\t3fna'):
            self.assertEqual(codes.normalize_code(text), 'K7QX-3FNA', text)

    def test_anything_else_is_not_a_code(self):
        for text in ('', 'K7QX-3FN', 'K7QX-3FNAA', 'K7QX-3FN0', 'K7QX-3FNI', 'K7QX_3FNA', 'K7QX-3FNÄ', 'k7qxßfna',
                     'X' * 40, None, 12345678, 'K7QX/3FNA'):
            self.assertIsNone(codes.normalize_code(text), text)


if __name__ == '__main__':
    unittest.main()


class JoinFileNames(unittest.TestCase):
    def test_the_gzipped_package_is_a_join_file_of_its_tick(self):
        # x4mods-15's fast join sends j<T>.x4join.gz (the package gzipped) beside or instead of the raw package
        from lockstep.master import files
        self.assertEqual(files.join_tick('j680.x4join.gz'), 680)
        self.assertEqual(files.join_tick('j680.x4join'), 680)
        self.assertEqual(files.join_tick('j680.xml.gz'), 680)
        self.assertIsNone(files.join_tick('j680.x4join.gz.exe'))
        self.assertIsNone(files.join_tick('../j680.x4join.gz'))
        self.assertEqual(files.cap_for('j680.x4join.gz'), files.JOIN_FILE_MAX)
