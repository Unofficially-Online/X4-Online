"""x4ls cleanup never follows a link, and the DLC sets of a save and an installation (src/lockstep/launcher/game.py,
profile.py, hotjoin.py). The junction cases make real NTFS junctions on Windows (symlinks off Windows); they emulate a
Python before 3.12, where Path.is_junction is missing and is_symlink is False for a junction."""
import gzip
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import game, hotjoin, profile  # noqa: E402


def directory_link(target, link):
    if os.name == 'nt':
        game._junction(target, link)       # a real junction: symlinks need a privilege on Windows
    else:
        os.symlink(target, link, target_is_directory=True)


def without_is_junction():
    """Path as on Python 3.10/3.11: no is_junction at all."""
    return mock.patch.object(Path, 'is_junction', new=property(lambda self: (_ for _ in ()).throw(AttributeError())),
                             create=True)


class LinksAreNeverEntered(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.install = self.root / 'game'
        (self.install / 'extensions/ego_dlc_split').mkdir(parents=True)
        (self.install / 'extensions/ego_dlc_split/content.xml').write_text('<content id="ego_dlc_split" version="900"/>')
        (self.install / 'aiscripts').mkdir()
        (self.install / 'aiscripts/keep.xml').write_text('k')
        (self.install / 'X4.exe').write_text('exe')

    def tearDown(self):
        self.tmp.cleanup()

    def assert_install_whole(self):
        self.assertEqual((self.install / 'aiscripts/keep.xml').read_text(), 'k')
        self.assertTrue((self.install / 'X4.exe').is_file())
        self.assertTrue((self.install / 'extensions/ego_dlc_split/content.xml').is_file())

    def test_a_view_is_removed_without_entering_its_links_on_any_python(self):
        view = game.create_view(self.install, self.root / 'lanes/c1', junction=directory_link)
        self.assertTrue(game.is_link_or_reparse(view / 'aiscripts'))
        self.assertTrue(game.is_link_or_reparse(view / 'extensions/ego_dlc_split'))
        self.assertFalse(game.is_link_or_reparse(view / 'extensions'))   # a real folder of the view
        with without_is_junction(), mock.patch.object(Path, 'is_symlink', lambda self: False):
            game.remove_view(view)
        self.assertFalse(view.exists())
        self.assert_install_whole()

    def test_a_link_to_a_view_is_not_removed(self):
        view = game.create_view(self.install, self.root / 'lanes/c1', junction=directory_link)
        link = self.root / 'lanes/c1-link'
        directory_link(view, link)
        with self.assertRaisesRegex(SystemExit, 'is a link'):
            game.remove_view(link)
        self.assertTrue((view / game.MARKER).is_file())
        game.remove_view(view)
        self.assert_install_whole()

    def test_a_missing_path_is_no_link(self):
        self.assertFalse(game.is_link_or_reparse(self.root / 'nothing'))
        self.assertFalse(game.is_link_or_reparse(self.install / 'X4.exe'))

    def test_a_profile_slot_that_is_a_link_is_refused(self):
        documents = self.root / 'Documents'
        documents.mkdir()
        directory_link(self.install, documents / str(profile.PROFILES[0]))
        with self.assertRaisesRegex(SystemExit, 'is a link'):
            profile.profile_dir(profile.PROFILES[0], documents)
        self.assertEqual(profile.profile_dir(profile.PROFILES[1], documents), documents / str(profile.PROFILES[1]))

    def test_join_files_are_not_deleted_through_a_link(self):
        save = self.root / 'save'
        save.mkdir()
        outside = self.root / 'elsewhere'
        outside.mkdir()
        (outside / 'j600.xml.gz').write_bytes(b'keep')
        directory_link(outside, save / 'mpjoin')
        hotjoin.remove_join_files({2: save})
        self.assertEqual((outside / 'j600.xml.gz').read_bytes(), b'keep')
        own = self.root / 'save2'
        (own / 'mpjoin').mkdir(parents=True)
        (own / 'mpjoin/j600.xml.gz').write_bytes(b'gone')
        (own / 'mpjoin/notes.txt').write_bytes(b'not a join file')
        hotjoin.remove_join_files({3: own})
        self.assertFalse((own / 'mpjoin/j600.xml.gz').exists())
        self.assertTrue((own / 'mpjoin/notes.txt').exists())


SAVE_HEAD = ('<?xml version="1.0" encoding="UTF-8"?>\n<savegame>\n<info>\n<save name="x" date="1"/>\n'
             '<game id="X4" version="900" build="611726" start="x4ep1_gamestart_discover"/>\n<patches>\n'
             '<patch extension="ego_dlc_split" version="900" name="Split Vendetta"/>\n'
             '<patch extension="ego_dlc_boron" version="900" name="Kingdom End"/>\n'
             '<patch extension="some_mod" version="3" name="A mod"/>\n<history>\n'
             '<patch extension="ego_dlc_terran" version="900" name="Cradle of Humanity"/>\n</history>\n</patches>\n'
             '</info>\n<universe>\n')


def save_text(patches_rows=None):
    return SAVE_HEAD if patches_rows is None else SAVE_HEAD.replace(SAVE_HEAD[SAVE_HEAD.index('<patch '):SAVE_HEAD.index('<history>')], patches_rows)


class DlcSets(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_a_saves_dlcs_are_its_patches_without_the_history_and_without_mods(self):
        compressed = self.root / 'session-save.xml.gz'
        with gzip.open(compressed, 'wt', encoding='utf-8') as out:
            out.write(SAVE_HEAD + '<component/>' * 1000 + '\n</universe>\n</savegame>\n')
        self.assertEqual(game.save_dlcs(compressed), {'ego_dlc_split': '900', 'ego_dlc_boron': '900'})
        plain = self.root / 'plain.xml'
        plain.write_text(SAVE_HEAD, encoding='utf-8')
        self.assertEqual(game.save_dlcs(plain), {'ego_dlc_split': '900', 'ego_dlc_boron': '900'})
        no_dlc = self.root / 'no-dlc.xml.gz'
        with gzip.open(no_dlc, 'wt', encoding='utf-8') as out:
            out.write(save_text('<patch extension="some_mod" version="3" name="A mod"/>\n'))
        self.assertEqual(game.save_dlcs(no_dlc), {})          # a world of the base game only

    def test_what_is_not_an_x4_save_names_no_dlcs(self):
        for name, data in (('fake.xml.gz', b'SAVE-1'), ('cut.xml.gz', gzip.compress(SAVE_HEAD.encode())[:40]),
                           ('other.xml', b'<?xml version="1.0"?><root><patches/></root>'),
                           ('nopatches.xml.gz', gzip.compress(b'<?xml version="1.0"?>\n<savegame>\n<info>\n'))):
            (self.root / name).write_bytes(data)
            self.assertIsNone(game.save_dlcs(self.root / name), name)
        self.assertIsNone(game.save_dlcs(self.root / 'missing.xml.gz'))

    def test_installed_dlcs_are_the_dlc_folders_with_a_content_xml(self):
        extensions = self.root / 'game/extensions'
        for name, content in (('ego_dlc_split', '<?xml version="1.0"?>\n<content id="ego_dlc_split" version="900" '
                                                'name="Split Vendetta" save="1">\n<text/>\n</content>'),
                              ('ego_dlc_boron', '<content id="ego_dlc_boron" name="Kingdom End">'),
                              ('ego_dlc_pirate', None),         # a folder Steam left behind: no extension to X4
                              ('some_mod', '<content id="some_mod" version="3">')):
            (extensions / name).mkdir(parents=True)
            if content is not None:
                (extensions / name / 'content.xml').write_text(content, encoding='utf-8')
        self.assertEqual(game.installed_dlcs(self.root / 'game'), {'ego_dlc_boron': '', 'ego_dlc_split': '900'})
        self.assertEqual(game.installed_dlcs(self.root / 'no-game'), {})

    def test_mismatch_names_missing_extra_and_other_versions(self):
        used = {'ego_dlc_split': '900', 'ego_dlc_boron': '900', 'ego_dlc_terran': '900'}
        self.assertEqual(game.dlc_mismatch(used, dict(used)), ([], [], []))
        installed = {'ego_dlc_split': '901', 'ego_dlc_boron': '', 'ego_dlc_ventures': '900'}
        self.assertEqual(game.dlc_mismatch(used, installed), (['ego_dlc_terran'], ['ego_dlc_ventures'], ['ego_dlc_split']))
        self.assertEqual(game.dlc_names(['ego_dlc_boron', 'ego_dlc_mini_02', 'ego_dlc_new']),
                         'Kingdom End, Envoy Pack, ego_dlc_new')


if __name__ == '__main__':
    unittest.main()
