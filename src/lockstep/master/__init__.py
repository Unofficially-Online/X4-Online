"""x4ls master: the central lobby server of netplay (docs/NETPLAY.md, "The central server").

    python3 tools/x4ls.py master --bind 0.0.0.0 --http-port 47100 --echo-port 47101 --relay-ports 47200-47263 --data DIR

A public internet service on a VPS, beside x4ls serve and sharing nothing with it. It holds no game state of its own: a
host registers a game and gets a join code, players find the game by code or in the public list, fetch the session's
data files (never code), say when they are ready, and the master decides whether the game runs its relay on the host's
PC (direct) or in the master's relay pool (relay). Standard library only (the release ships an embedded Python without
packages); Python 3.10 to 3.14, Linux and Windows.

Modules: codes (join codes), limits (token buckets and the caps), games (a game's state and the API's answers about it),
files (uploads and downloads), relays (the relay pool: tools/dev/native_gate_relay.py per game), udp (the echo and the
reachability probe), page (the HTML lobby view), server (the HTTP API, MasterOptions and run)."""
