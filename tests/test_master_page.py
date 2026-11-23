"""GET /, the master's lobby view: public games only, every string escaped, refreshed every 10 s, nothing loaded from
elsewhere, and the links under a proxy's prefix."""
import re
import unittest

from master_support import MasterCase
from lockstep.master import page

HOSTILE = '<script>alert("x")</script> & \'quotes\''


class PageTests(MasterCase):
    def test_the_view_lists_public_games_escaped(self):
        self.host_game(name=HOSTILE)
        self.host_game(name='Private <b>game</b>', public=False, ip='203.0.113.50')
        answer = self.call('GET', '/')
        self.assertEqual(answer.status, 200)
        self.assertEqual(answer.headers['Content-Type'], 'text/html; charset=utf-8')
        self.assertIn("default-src 'none'", answer.headers['Content-Security-Policy'])
        self.assertEqual(answer.headers['X-Content-Type-Options'], 'nosniff')
        text = answer.body.decode('utf-8')
        self.assertNotIn('<script', text)
        self.assertIn('&lt;script&gt;alert(&quot;x&quot;)&lt;/script&gt; &amp; &#x27;quotes&#x27;', text)
        self.assertNotIn('Private', text)
        self.assertIn('<meta http-equiv="refresh" content="10">', text)
        self.assertIn('1/2', text)
        self.assertIsNone(re.search(r'(?:src|href)="(?:https?:)?//', text))   # nothing from elsewhere
        self.assertNotIn('<link', text)

    def test_an_empty_lobby(self):
        text = self.call('GET', '/').body.decode('utf-8')
        self.assertIn('No public game is open right now.', text)


class PrefixTests(MasterCase):
    options = {'public_prefix': '/x4mp'}

    def test_links_carry_the_proxy_prefix(self):
        text = self.call('GET', '/').body.decode('utf-8')
        self.assertIn('href="/x4mp/api/v1/games"', text)


class RenderTests(unittest.TestCase):
    def test_every_field_is_escaped(self):
        game = {'name': '<i>n</i>', 'players': 1, 'players_max': 2, 'state': '<s>', 'mode': '<m>', 'release': '<r>',
                'age_s': 4000}
        text = page.render([game], {'games': 1, 'relays': 0})
        for raw in ('<i>', '<s>', '<m>', '<r>'):
            self.assertNotIn(raw, text)
        self.assertIn('1 h 6 min', text)


if __name__ == '__main__':
    unittest.main()
