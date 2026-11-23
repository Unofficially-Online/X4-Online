"""GET /: the lobby view, a read-only HTML page of the public games (docs/NETPLAY.md).

Every string that reaches the page (a game's name is typed by its host) goes through html.escape; the page has no
scripts, no fonts and no styles from anywhere else, and the server sends it with a Content-Security-Policy that allows
none (server.PAGE_HEADERS). It refreshes itself every 10 seconds. Behind a reverse proxy under a path prefix
(--public-prefix /x4mp) the links carry the prefix."""
import html

REFRESH_S = 10
STYLE = """
:root { color-scheme: light dark; --text: #1d2125; --muted: #5d6670; --line: #d5dae0; --head: #eef1f4; --page: #ffffff; }
@media (prefers-color-scheme: dark) {
  :root { --text: #e3e7eb; --muted: #9aa4ae; --line: #39424b; --head: #222a31; --page: #14191e; }
}
body { margin: 0 auto; max-width: 960px; padding: 16px; font: 15px/1.45 system-ui, sans-serif; color: var(--text);
       background: var(--page); }
h1 { font-size: 20px; margin: 0 0 4px; }
p { color: var(--muted); margin: 0 0 16px; }
.wrap { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; }
th, td { text-align: left; padding: 6px 10px; border-bottom: 1px solid var(--line); white-space: nowrap; }
th { background: var(--head); font-weight: 600; }
td.name { white-space: normal; word-break: break-word; }
code { font-size: 13px; }
"""


def age_text(seconds):
    if seconds < 60:
        return f'{seconds} s'
    if seconds < 3600:
        return f'{seconds // 60} min'
    return f'{seconds // 3600} h {seconds % 3600 // 60} min'


def row(game):
    escape = html.escape
    mode = game.get('mode') or '-'
    cells = [f'<td class="name">{escape(game["name"])}</td>',
             f'<td>{game["players"]}/{game["players_max"]}</td>',
             f'<td>{escape(game["state"])}</td>',
             f'<td>{escape(mode)}</td>',
             f'<td><code>{escape(game["release"])}</code></td>',
             f'<td>{escape(age_text(game["age_s"]))}</td>']
    return '<tr>' + ''.join(cells) + '</tr>'


def render(games, health, prefix=''):
    """The page for `games` (summaries as GET /api/v1/games answers them) and `health` (its answer)."""
    escape = html.escape
    base = escape(prefix.rstrip('/'), quote=True)
    if games:
        body = ''.join(row(game) for game in games)
    else:
        body = '<tr><td colspan="6">No public game is open right now.</td></tr>'
    count = len(games)
    return (
        '<!doctype html>\n<html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width, initial-scale=1">'
        f'<meta http-equiv="refresh" content="{REFRESH_S}">'
        '<title>X4 Multiplayer lobby</title>'
        f'<style>{STYLE}</style></head><body>'
        '<h1>X4 Multiplayer: public games</h1>'
        f'<p>{count} public game{"" if count == 1 else "s"} listed, {health["games"]} game(s) and {health["relays"]} '
        f'relay(s) on this server. Join from X4\'s main menu: Multiplayer, Public games. '
        f'Machine-readable: <a href="{base}/api/v1/games">{base}/api/v1/games</a>.</p>'
        '<div class="wrap"><table><thead><tr><th>Game</th><th>Players</th><th>State</th><th>Connection</th>'
        '<th>Release</th><th>Open for</th></tr></thead>'
        f'<tbody>{body}</tbody></table></div></body></html>\n')
