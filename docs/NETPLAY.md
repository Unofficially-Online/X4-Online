# Netplay: hosting, join codes, a public lobby and a release installer (design, 23 Nov 2026)

What a player does, once this is built:

1. Installs the release (an MSI made by GitHub Actions) and presses Play in Steam.
2. X4's main menu has **Multiplayer**. Its page offers **Host a game** (public or private, how many players, a name),
   **Join with a code** (typed into the page, or pasted from the clipboard), and **Public games** (a list from the
   central server, each with Join), plus LAN games found on the local network.
3. Hosting shows a join code (`K7QX-3FNA`) to send to friends. The game loads when the host presses Start (or when
   every slot is taken).
4. In the game, Escape, **Multiplayer session** shows the players, the join code (Copy), how the game is connected
   (direct, UPnP, or through the server's relay) and Leave.

This file is the contract between the parts. Parts are built separately (see "Work packages") and must agree on
what is written here; a change to a contract is a change to this file first.

## What exists and stays

- The lockstep relay `tools/dev/native_gate_relay.py` (UDP, one per game, `--token`, `--allow-from`/`--allow-file`,
  `--observe --keep-going` for play, the join arguments for drop-in games, opt-in `--wire-crypto`, opt-in
  `--rendezvous-port` = NGNAT/1, `tools/dev/native_gate_rendezvous.py`). Every client sends to the relay; the relay
  answers the source address. It must stay the session's hub: nothing here changes the lockstep wire.
- `x4ls lobby` (`src/lockstep/launcher/lobby.py`): the page in the launcher's X4 (disposable profile), host and join
  over TCP with the long invite code `X4MP-<ip>-<port>-<token>`; it stays as the offline/LAN path.
- `x4ls serve` (`src/lockstep/launcher/serve.py`): one kit's server (kit key, one game at a time, drop-in joins with
  join files uploaded by the saver). It stays for the playtest kits (service `x4mp-lobby` on UDP 47000/TCP 47001).
- The Steam-start path (`src/lockstep/launcher/steamplay.py`): the menu extension `x4mp_playtest_menu` in X4's own
  extensions folder, the helper behind named pipes, Join = restart X4 in the disposable profile.
- The page: `tools/dev/native_gate_lobby.lua`; the in-session page: the `mp_menu` block of
  `tools/dev/native_gate_probe.lua` ("Multiplayer session").
- The NAT work of 1 Nov (report `reports/LOCKSTEP-NAT-TRAVERSAL-20261101.txt`): a C core with PCP, NAT-PMP and UPnP
  IGD codecs (`src/native/engine/lockstep_nat*.h`, not wired), the relay-side NGNAT/1 rendezvous, and loopback fakes
  `tools/dev/nat_fake_gateway.py` for tests.
- Sessions: one session save every client loads (`x4ls new-session` makes it on one PC with a capture client; X4's
  new game is not reproducible across PCs, the save is). The play overlay is built from the save
  (`tools/build_play_overlay.py --session`).

## Trust and safety rules (every part)

- **No player ever runs code from another player or from the server.** A joiner gets the session's *data* (the
  session save and `session.json`, sha-256 checked) and builds its overlay locally from its *own installed release*
  (its own gate DLL and Lua). Games of another release are shown as incompatible and cannot be joined.
- The central server never executes uploaded content. Uploads are size-capped, sha-256 checked and deleted with
  their game.
- The server must not become an amplifier, a reflector or a port scanner: replies go only to the request's source,
  are never larger than the request (UDP), reachability probes connect only to the requesting IP, rate limits per IP
  and globally, caps on games, relays, uploads and bytes.
- Nothing personal is published: no player names other than what a host types as a game name; no Steam IDs; no
  paths. The repository is public (X4-Online): no server addresses, keys or tokens in the source. The master's
  address reaches a release through the build (a GitHub Actions repository variable) or `master.json` beside the kit.
- UPnP/NAT-PMP/PCP mappings are made only while hosting, with a lease, renewed while the game runs, and deleted when
  it ends (and on the next start, any stale mapping of ours is deleted first).

## Releases and compatibility

`release.json` in a release kit (written by the build): `{"release": "<12 hex of the commit>-<12 hex of the gate
DLL sha-256>", "commit": "<40 hex>", "gate_dll_sha256": "<64 hex>", "built": "<ISO time>"}`. Two players are
compatible when their `release` strings are equal and their session sha-256 are equal (the joiner downloads the
host's session first). The master lists every game with its `release`; the page marks other releases as
incompatible.

## The central server (`x4ls master`)

Module `src/lockstep/master/` (stdlib only; Python 3.10+; Linux and Windows), started by
`python3 tools/x4ls.py master --bind 0.0.0.0 --http-port 47100 --echo-port 47101 --relay-ports 47200-47263
--data DIR`. Deployed to a VPS as systemd unit `x4mp-master` by `tools/deploy_master_server.py --ssh <host>` (it
opens the ufw rules for exactly these ports; behind an HTTPS reverse proxy, `--proxy-prefix`, TCP 47100 stays closed).
It runs beside `x4mp-lobby` and shares nothing with it.

### HTTP API v1 (JSON, UTF-8; requests at most 64 KiB except file uploads)

The base URL is `http://<host>:47100` or, behind a reverse proxy with TLS, `https://<host>/<prefix>` (`--http-bind
127.0.0.1 --trust-proxy --public-prefix /<prefix>`: the client IP is taken from `X-Forwarded-For` only for requests
from loopback). The UDP echo and the relays never go through the proxy. `master.json` and `X4MP_MASTER` hold the
base URL.

Every answer is `{"ok": true, ...}` or `{"ok": false, "error": "<code>", "why": "<text for the page>"}` with an
HTTP status (400 bad request, 403 wrong secret/ticket, 404 unknown game/code, 409 wrong state/full/incompatible,
413 too large, 429 rate limited, 503 server full). A game's `secret` and a player's `ticket` are 32 hex, sent in the
JSON body or in the header `X-X4MP-Auth`.

| Method, path | Body | Answer |
|---|---|---|
| `GET /api/v1/health` | | `{version, games, relays, uptime_s, echo_port}` (the echo's host is the base URL's hostname) |
| `POST /api/v1/games` | `{name, public, release, players_max (2..8), session_sha256, open_slots: [k...], want: "auto"\|"direct"\|"relay", direct: {udp_port, tcp_port} \| null, lan: {candidates: [ip...], udp_port, tcp_port} \| null}` | `{game_id, code, secret, host_ticket, slot: 1}` |
| `PUT /api/v1/games/{id}/files/{name}` | raw bytes; headers `X-X4MP-Auth: <secret>`, `X-X4MP-Sha256` | `{name, size, sha256}`; names and caps: `session-save.xml.gz` 64 MiB, `session.json` 1 MiB, `play-recipe.json` 1 MiB |
| `GET /api/v1/games?release=R` | | `{games: [{game_id, name, release, compatible, players, players_max, state, mode, open_slots_free, age_s}]}`: public games only, newest first, at most 100 |
| `GET /api/v1/codes/{CODE}` | | the same fields for one game (public or private), plus `lan` (candidates and ports) only when the asker's public IP is the host's |
| `POST /api/v1/games/{id}/join` | `{release, code?}` | `{ticket, slot, files: {name: {size, sha256}}}`; 409 `incompatible`/`full`/`started` |
| `GET /api/v1/games/{id}/files/{name}` | header `X-X4MP-Auth: <ticket>` | raw bytes (Range supported) |
| `GET /api/v1/games/{id}/lobby?ticket=T&since=N` | | long poll (at most 25 s): `{stamp, state, players: [{slot, ready, host}], start: {load_at, relay: {host, port, token}, mode} \| null}` |
| `POST /api/v1/games/{id}/ready` | `{ticket, ready: true}` | `{}`: the player has the session files and its overlay is built |
| `POST /api/v1/games/{id}/start` | `{secret}` | `{}`: the host starts now with the ready players (the open slots stay joinable if the session has them) |
| `POST /api/v1/games/{id}/heartbeat` | `{secret or ticket, tick, state}` | `{}`; a game without a heartbeat for 90 s is gone |
| `POST /api/v1/games/{id}/leave` | `{ticket}` | `{}` |
| `DELETE /api/v1/games/{id}` | `{secret}` | `{}` |
| `POST /api/v1/probe` | `{udp_port, tcp_port, nonce}` | the server tries TCP connect and sends one UDP datagram `X4MPPROBE\|1\|<nonce>` to the **request's source IP** only; answers `{tcp: bool, ip}`; the client reports whether the UDP probe arrived; at most 6 per minute per IP |
| `GET /` | | the lobby view as an HTML page (public games, players, state, release), read-only, no scripts from elsewhere |

Join codes: 8 characters of `23456789ABCDEFGHJKLMNPQRSTUVWXYZ`, shown `XXXX-XXXX`, case and dash insensitive,
unique among live games, gone with their game.

### Starting a game: where the relay runs

On start the master decides `mode`:
- `direct`: the host registered `direct` ports and the master's probe reached both (UPnP, NAT-PMP/PCP, or a forwarded
  port). The host's launcher runs the relay on its PC (as `x4ls lobby` does today, `--allow-file` filled with the
  players' public addresses from the lobby answers); the others get `relay: {host: <host's public IP>, port, token}`.
- `relay`: the master starts `native_gate_relay.py` itself on a port of its pool, with the arguments
  `session.relay_command` makes from the uploaded `play-recipe.json` (data only: tick count, join arguments; values
  checked against ranges), a fresh token, and `--allow-from` the players' public IPs (`--allow-file` for late
  joiners). The master runs the newest release; a game of another release cannot use `relay` (the page says why).
- `want: "auto"`: `direct` if the probe passed, else `relay`.
Players behind one NAT share a public IP; the relay tells them apart by port as it does today.

The load second is `now + load_wait` (30 s after the last ready player, at least 20 s after start), as `serve` does.
Drop-in joins of a running game (open slots) follow `serve`'s join flow: the saver's launcher uploads the join files
to the master (`PUT .../files/j<T>.x4join`, `j<T>.xml.gz`, caps 512 MiB), the joiner fetches them. In `direct` mode
the host's launcher plays the server's part.

### UDP echo (`--echo-port`, NAT discovery and ping)

`X4MPECHO|1|<nonce 16 hex>|<pad to 64 bytes>` -> `X4MPSEEN|1|<nonce>|<ip>:<port>|<dots>` (only to the source, the
`|<dots>` field padding it to at most the request's size; per-IP token bucket). The client learns its public endpoint
and the round trip time.

### Master notes (what package 1 chose where the contract left it open)

- **Errors** (`error`): 400 `bad_request`, `bad_json`, `bad_code`, `bad_name`, `bad_sha256`, `bad_gzip`, `bad_method`, `short_body`,
  `bad_proxy`, `ipv4_only`; 403 `forbidden`; 404 `not_found`; 409 `incompatible`, `full`, `started`, `state`, `not_ready`,
  `not_asked`, `busy`, `unreachable`, `no_recipe`, `bad_recipe`; 413 `too_large`; 416 `bad_range`; 429 `rate_limited`,
  `too_many_games`; 503 `server_full`, `relays_full`, `relay_failed`, `no_public_host`, `storage`; 500 `internal`.
- **File caps:** the upload row used to list three names and three caps in different orders; it now names each cap's
  file (the save is the large one). Join files 512 MiB each, raw bytes as the saver sends them. All stored files together count against `--data-max-gib` (8); the relays' own folders (logs, report) are
  not counted but go with their game. The master is in memory: a restart forgets every game and empties `DATA/games`.
- **Auth:** every authenticated call takes the secret or ticket in `X-X4MP-Auth` (the header wins when the body or
  the query has one too); the body's `secret`/`ticket` and the lobby's `?ticket=` work as well. The game's secret
  stands for the host's ticket. Join files are uploaded with the saver's ticket (or the secret), only for a tick the relay asked for.
- **Additions to answers:** the lobby answer has `slot` (the asker's), for the host of a `direct` game each player's
  `ip` (the master-learned address for its relay's `--allow-file`), and for a saver of a relay-mode drop-in game
  `send_join: [{tick, slot}]` (upload `j<T>.x4join` and `j<T>.xml.gz`; serve sends `send_join` over its lobby socket). The
  start answer has `{mode, load_at}`; the code lookup has `public`; `compatible` is null without `?release=`. Game
  `state` is `lobby`, `started` or `ended` (an ended game answers its players' polls for 60 s).
- **Slots and start:** joins before the start take the slots of the common load first, then open slots; after the start
  only open slots nobody had. Start needs a ready player in every slot that is not open (409 `not_ready`); the host's
  Start counts as its ready. The game starts by itself once every slot is taken and every common-load player is ready.
- **Liveness:** every authenticated request counts as a heartbeat. Before the start a player without a request for 90 s
  leaves, and a host without one for 90 s (or a host's leave) ends the game.
- **direct or relay:** the master uses its own TCP probe result for the host's IP and the registered `direct` ports, for
  an hour (one lease); the host registers `direct` only when its UDP probe arrived. `want: "direct"` without it is 409
  `unreachable`. `--relay-release R` (repeatable) limits the relay pool to the master's own releases.
- **The relay from the recipe:** `session.relay_command` needs no overlay once it has the tick rate, the lookahead and the
  tick count; serve gets those from the overlay (`session.play_settings`). The master computes them from the recipe as
  data (`relays.recipe_settings`): `window[1] + PLAY_TAIL_TICKS`, and the recipe's `--set` lines for
  `native_gate_tick_hz` and `native_gate_lookahead`, else the defaults table. A recipe with an adaptive window, wire
  crypto or peer links is refused (409 `bad_recipe`): the master's relay runs none of them.
- **Echo and probe:** the echo's reply is padded with a trailing `|....` field of dots to the request's size (no
  padding field when fewer than 2 bytes are left; a request too short for the reply gets none). The probe takes `nonce` of 16 to 64 hex digits and ports 1024..65535, refuses a body naming a target (`ip`,
  `host`, `address`, `target`), probes IPv4 only, and sends its datagram from a fresh socket (not the echo's).
- **Behind a reverse proxy** (nginx, HTTPS): `--http-bind 127.0.0.1 --trust-proxy --public-prefix /x4mp --public-host
  <IPv4>`. Only connections from 127.0.0.1/::1 have their `X-Forwarded-For` read (its last entry), and that address is
  used for everything that depends on the client's IP. The echo and the relays are UDP and never pass the proxy. The
  proxy's host name should resolve to IPv4 only: a player whose HTTP went over IPv6 has no address to allow on a relay.
  `tools/deploy_master_server.py --proxy-prefix /x4mp` writes a location block to include (`--print-nginx` shows it).

### LAN discovery (no server needed)

A host announces itself on UDP 47102 every 2 s (broadcast and multicast on every network, see "VLANs" below):
`X4MPLAN|1|<release>|<code or ->|<name, at most 40 bytes, url-encoded>|<tcp lobby port>|<players>/<max>|<candidate
ip,ip,...>`; a browsing launcher listens on 47102 and shows LAN games beside the public ones (joined through the
existing LAN lobby on the best candidate address).

## VLANs, several networks and virtual LANs

It must work when the players are on different VLANs of one site (routed subnets behind one router: broadcast does
not cross, the public IP is the same, the router may not hairpin), when a PC has several networks (VLAN adapters,
Hyper-V/WSL vEthernet, Wi-Fi and cable), and over the virtual LANs players use for games (ZeroTier, Hamachi, Radmin
VPN: broadcast works on their adapter; Tailscale and WireGuard: no broadcast at all, routed 100.64.0.0/10 or own
ranges).

- **No "primary interface".** Listeners (relay, TCP lobby, discovery) bind `0.0.0.0`. A host advertises every usable
  IPv4 address of every up interface (`netifs.addresses()`: address, netmask, interface name; loopback and
  169.254/16 left out) as its LAN candidates. The old `--bind` stays an override, never a filter.
- **Discovery reaches every network a PC is on:** each announce goes to every interface's directed broadcast address
  (for a /24: `.255`), to `255.255.255.255` sent from each interface address, and to the multicast group
  `239.255.71.2:47102` on each interface (`IP_MULTICAST_IF`, TTL 8, so a site whose router forwards multicast between
  VLANs sees it). The listener binds `0.0.0.0:47102` and joins the group on every interface. The announcement carries
  all candidates; a joiner tries the one on its own subnet first, then the others, then the public path.
- **Same site, other VLAN:** the master gives a joiner the host's LAN candidates only when the joiner's public IP is
  the host's (as NGNAT/1 does: no LAN address leaves its site). The joiner tries each candidate (TCP connect to the
  lobby port, 1.5 s each) before the public address or the relay; hairpin NAT is never needed.
- **Join by address:** the page's text box takes a join code, a long LAN invite code, or an address (`10.20.0.5`,
  `10.20.0.5:47001`, a host name, a Tailscale/ZeroTier address). That covers routed VLANs without multicast
  forwarding and VPNs without broadcast.
- **Who may send to a relay is learned, never claimed:** in direct and LAN modes every joiner connects once to the
  host's TCP lobby port before the relay starts; the source address the host sees there (public, LAN, VLAN or VPN
  address) goes into the relay's allow list (`--allow-file`). An address a joiner merely reports is never allowed.
- **Datagram size:** the lockstep relay's and the rendezvous's UDP payloads must stay at or under 1200 bytes
  (VLAN tags, VPN and tunnel overheads shrink the path MTU). Package 2 measures the relay's largest datagram in a
  test session's capture and reports it; a change to the lockstep wire is not part of this work.
- `src/lockstep/launcher/netifs.py` (package 2): `addresses() -> list[dict(ip, netmask, broadcast, name)]` on Windows
  (`GetAdaptersAddresses` through ctypes) and Linux (`SIOCGIFCONF`/`SIOCGIFNETMASK` or `/proc/net`/`ip -o -4 addr`),
  with `best_for(peer_ip, candidates)` (same subnet first). Tests inject interface lists.

## Port mapping and reachability (`src/lockstep/launcher/nat.py`)

`nat.map_ports(udp_port, tcp_port, lease=3600)` tries, in order: PCP (RFC 6887), NAT-PMP (RFC 6886), UPnP IGD
(SSDP `M-SEARCH` for `urn:schemas-upnp-org:device:InternetGatewayDevice:1`/`:2`, the description's
WANIPConnection/WANPPPConnection control URL, SOAP `AddPortMapping`, `GetExternalIPAddress`), and returns a
`Mapping` (method, external ip, ports, lease, `renew()`, `release()`), or `None` with the reasons. A background
renewer keeps the lease while the game runs. `nat.public_endpoint(master)` uses the echo; `nat.probe(master, ...)`
asks the master's probe and listens for the UDP datagram. Everything has fakes in `tools/dev/nat_fake_gateway.py`.

### Datagram sizes (package 2, 23 Nov)

Measured with the relay's own `Barrier` code and maximal blobs (`tests/test_netplay_nat.py`, `DatagramSizes`; no
X4, no network), and read from `lockstep_wire.h` and `docs/LOCKSTEP-NSLOT-WIRE.txt`. UDP payload bytes:

| Datagram | What bounds it | Largest |
|---|---|---|
| `NGSTATE\|3/4` client to relay | one blob of at most 480 hex; the relay drops reports above 1024 (`STATE_MAX`) | 635 with a 6-digit tick (4 h at 60 Hz), 649 with a 20-digit one; `--wire-crypto` adds 97 (746) |
| `NGGO\|3/4` relay to client (permit) | about 45 bytes of head plus, per player, 1 + its blob (at most 480 hex); the client reads up to `GATE_PERMIT_MAX` 8192 (stop 8 at that size) | at tick 2, full blobs: N=2 1007, N=3 1488, N=8 3893; +1 byte per further tick digit; `NGGO\|5` about 30 more; `--wire-crypto` adds 64*(N+1)+33 (N=2 1245, N=8 4521) |
| NGNAT/1 rendezvous | requests padded to 256, refused above 512, replies never larger than the request | 512 |
| `NGPADDR`, `NGJOINSTATE`, `NGPING`, `NGSTOP`, `NGDROP` | fixed fields (`NGPADDR` 22 bytes per player) | under 300 |

Nothing but the blobs limits a permit: it carries every player's input blob of one tick. Reports always fit in
1200 bytes. A permit exceeds 1200 bytes when one tick's blobs together pass about 1150 hex characters: three or more
players whose blobs are near the 240-byte input budget (orders, hit claims and octree sections filling it), or two
near-full blobs with `--wire-crypto`. Typical play (a 28-byte pilot record per player) stays under 600 bytes with eight
players. Above the path MTU IPv4 fragments the permit (Windows sends UDP without DF; Linux fragments once an ICMP
"fragmentation needed" told it the path MTU, so the first oversized permit on such a path is lost). The relay answers
every resent report (every 100 ms) with the permit again, so a lost fragment costs a resend; a path that drops all
fragments (some firewalls and carrier NATs) would stall the session until the relay timeout. Not measured on a real
VLAN or VPN path, and no native capture with three or more players and full blobs exists yet. Without a wire change a
smaller per-slot input budget for N >= 3 (the multiplexer's budget, not the wire) would keep every permit under 1200.

### NAT notes (package 2: decisions where the contract left room)

- `Mapping.udp_port`/`tcp_port` are the external ports (a router may grant others than asked); it also has
  `internal_udp_port`, `internal_tcp_port`, `local_ip`, `gateway` and `released`. `lease` 0 = a permanent UPnP mapping
  (routers that take only those, error 725); `keep_mapped` adds it again every 30 min and never releases: the host calls
  `release()`.
- `map_ports` takes keyword-only test/diagnostic targets after the contract's arguments: `gateway`, `ssdp`, `interfaces`,
  `methods`, `state_file`, `reasons` (a list it fills; the reasons also go to `log` as one line). `timeout` bounds each
  step. NAT-PMP is tried only when the gateway answered PCP with a NAT-PMP "unsupported version" (as the C core); a
  gateway silent on 5351 goes straight to UPnP. A private or carrier-grade external address is refused (`double_nat`).
- Stale mappings of ours: PCP needs the old nonce, so the nonces of live PCP mappings are kept in
  `.local/netplay/nat-state.json`; NAT-PMP deletes by internal port (lifetime 0) before mapping; UPnP asks
  `GetSpecificPortMappingEntry` and deletes an entry naming this PC's address with our internal port or our description
  (`X4 Multiplayer host`). Another device's entry is never touched: the next external port is tried (up to 4).
- There is no `nat.probe`: the prose's probe is `reachable(client, udp_port, tcp_port, timeout)` of the interface list
  (keyword `bind`). It holds a temporary TCP listener only if nothing listens on the port yet, never takes over a busy
  port, and passes `MasterError` (for example 429 `rate_limited`) to the caller. The probe nonce is 16 hex.
- `public_endpoint(host, ...)` takes a host name, a base URL or the `(host, port)` of `MasterClient.echo_address()`
  (keywords `local_port`, `bind`). The request is padded with `.` to 64 bytes; the reply's endpoint field may carry pad.
- Base URLs (coordinator, 23 Nov): `'host'` or `'host:port'` become `http://host:47100` / `http://host:port`; a URL with
  a scheme is kept with its path prefix (`https://master.example.org/x4mp`); HTTPS is verified with the default SSL
  context and cannot be switched off. `GET /api/v1/health` also answers `echo_port`; `MasterClient.echo_address() ->
  (host, port) | None` is the base URL's host name and that port, cached. The probe stays plain HTTP API.
- `MasterClient` sends a secret or ticket in `X-X4MP-Auth` on every call that has one, and also in the body field the
  table names; `heartbeat` sends only `{tick, state}` and the header (the body would have to say which of the two it is).
  `lobby(wait)` is not a query field: it sets the read timeout to `wait + timeout`. `code()` refuses a text that is not
  a join code before sending (`MasterError('bad_code', status=0)`) and sends `XXXX-XXXX`. Methods return the whole JSON
  object (with `ok`), `games()` the list. `upload` checks the stored `size`/`sha256`; `download` calls
  `progress(done_bytes, total_bytes)` and returns a `dest` that already holds the announced bytes without a request.
- `master.json` has no format in this file: `master_url` reads `{"url": "..."}`, `{"master": "host[:port]"}` or
  `{"host": ..., "port": ...}` (package 5: please write `url`). Without `kit_dir` the kit is the folder holding the
  checkout, and only when `release.json` is there.
- LAN: the name is cut to 40 bytes of UTF-8 before url-encoding; at most 16 candidates; datagrams above 512 bytes, a
  release outside `[A-Za-z0-9._+-]{1,64}`, a code not in `XXXX-XXXX` form and a loopback candidate from another PC are
  dropped. `scan()` gives `code` None for `-` and keeps exactly one entry per (sender, code), so a host seen over two
  shared networks is listed twice (package 3 may merge equal codes). `netifs` rows also carry `prefix`; `broadcast` is
  None on /31, /32 and links without broadcast; `best_for(peer_ip, candidates, interfaces=None)` drops strings that are
  not dotted IPv4 and repeats, and takes `peer_ip` None for "this PC's subnets only".

## The page and the launcher: requests and state

Both backends (the launcher's X4: log rows `[X4LAN] LOBBY REQUEST <text>` and `lobby/state.lua`; the Steam-start
X4: named pipes) carry the same requests and the same state table.

Requests (text, at most 200 bytes of UTF-8):
`HOST|<public 0/1>|<players 2..8>[+<drop-in slots>]|<name>` (with drop-in slots: players 1..7, together at most 8;
"Drop-in games") · `JOINCODE|<code>` · `JOIN` (code from the clipboard: a join code or a
long LAN invite code) · `BROWSE` · `JOINGAME|<game_id>` · `JOINLAN|<index>` · `START` · `READY` · `CANCEL` ·
`COPY` · `LEAVE`. The existing `JOIN`, `JOIN<k>` (start picker) and `HOST`/`COPY`/`CANCEL` keep working. `READY` is
not sent by the page: the launcher marks a player ready itself once its files and overlay are done.

Requests through pipes (Steam-started X4). A named-pipe server can only answer names it created beforehand, so typed
text cannot be a pipe name. The old requests keep their old pipe names (`<prefix>-state`, `-join`, `-join<k>`, `-stop`;
the server and LAN pages also `-host`, `-copy`, `-cancel`). Every netplay request (`HOST|...`, `JOINCODE|...`,
`BROWSE`, `JOINGAME|id`, `JOINLAN|i`, `START`, `CANCEL`, `COPY`, `LEAVE`; also the in-session `COPY` and `LEAVE`) is
spelled through fixed names, in order within one frame (loadfile waits for each answer):
1. `<prefix>-tbegin`: the helper clears its buffer;
2. for each character of the request's lowercase hex encoding (UTF-8 bytes to hex), one open of `<prefix>-t0` ..
   `<prefix>-tf`: the helper appends that digit;
3. `<prefix>-tend`: the helper decodes the buffer and runs the request.

Every open is answered with the state text, as for every other pipe; the page uses only the answer to `-tend` (shown at
the next frame). The marks are `-tbegin` and `-tend`, not `-tb` and `-te`: those two names are the digits `b` and `e`
(`K` is `4b`, `N` is `4e`), so a helper could not tell them apart. An open that does not reach the helper (loadfile's
"cannot open": no helper, or every instance of that pipe busy) is tried again at once, up to 100 times; a helper should
keep a listening instance of each digit pipe ready, since one digit can repeat many times in a row.

State (a Lua table; every field optional except `stamp` and `phase`):

```lua
{ stamp = 12, phase = 'idle'|'browsing'|'preparing'|'hosting'|'joining'|'waiting'|'loading'|'session'|'restart'|'error'|'nohelper',
  lines = {'...', '...'},                -- at most 6 lines of text for the page header
  code = 'K7QX-3FNA', public = true, host = true, can_start = true,
  network = {mode = 'direct'|'relay'|'lan', mapping = 'upnp'|'natpmp'|'pcp'|'manual'|'none', public_ip_known = true, ping_ms = 38},
  players = { {slot = 1, ready = true, host = true}, ... },
  games = { {id = 'g1', name = '...', players = 2, max = 4, state = 'lobby', compatible = true, ping_ms = 40}, ... },
  lan = { {index = 1, name = '...', players = 1, max = 2, compatible = true}, ... },
  progress = {what = 'download'|'overlay'|'session', done = 0.4} }
```

The page draws rows from this table only; it never talks to the network itself (X4's UI Lua cannot).

In the session, the probe's "Multiplayer session" page reads the same state (the launcher keeps writing it; in the
Steam path the helper answers the pipe) every few seconds for the players, the code and the network line, and sends
`COPY` and `LEAVE`.

### Page notes (package 4)

Which page a config gets (`X4LAN_CONFIG.lobby`): `netplay = true` (a release's menu extension
`{pipe = 'x4online', netplay = true}`, or `x4ls lobby` running netplay, without a pipe) draws the netplay page below;
`server = true` (a playtest kit, `{pipe = 'x4mp-playtest', server = true}`) keeps Join the game and the start picker;
neither keeps the LAN lobby of before (Host a game, Join a game (invite code from the clipboard), Copy, Cancel, sending
`HOST`, `JOIN`, `COPY`, `CANCEL`). The menu entry is "Multiplayer" in all three.

The netplay page, by phase (the state's `lines` always first, at most 6):
- `idle`, `browsing`, `error` (and, greyed, `starting` before the first state and `nohelper`): Host a game
  (Visibility Public/Private, default Private; Players 2..8, default 2; Game name box; Host), Join a game (one box for a
  join code, invite code or address; Join; Join with the code on the clipboard), Public games (Refresh, the list) and
  LAN games. `error` adds Cancel. Lists not yet asked for say "Press Refresh..." (`browsing`: "Looking for games...").
- `preparing`, `hosting`, `joining`, `waiting`, `loading`: the progress line (`progress`), the code with Copy (`code`, or
  the old `invite`), the connection line (`network`), the players (`players`: "Player 1 (host): ready"), Start for the
  host (`host`; selectable when `can_start` is true), Cancel.
- `session`, `restart`: the lines only.

What the page sends, exactly:
- `HOST|<0/1>|<2..8>|<name>`, or `HOST|<0/1>|<1..7>+<1..7>|<name>` with drop-in slots (the page's "Drop-in slots"
  dropdown, 0 = none; Players offers 1 for a host who starts alone, and the two stay valid together: at most 8, and
  without drop-in slots at least 2): an empty name is sent as `X4 game`. `JOINCODE|<text>`: the box's text as typed, trimmed
  (the launcher normalizes codes); sent by Join and by Enter in the box (a click elsewhere, which X4 also reports as a
  confirmed box, sends nothing). Typed fields lose `|` and control characters (they become spaces) and are cut at whole
  UTF-8 characters: a name at 40 bytes, the code box at 100.
- `BROWSE` once by itself when the netplay page is open in `idle` and the state has no `games`; again with Refresh.
- `JOINGAME|<id>` and `JOINLAN|<index>` from the lists, only for rows that can be joined: `compatible = false` is greyed
  "(another release)", `players >= max` "(full)". At most 30 rows per list, then "and N more".
- `START`, `CANCEL`, `COPY`; in the session `COPY` and `LEAVE` (after the gate has stopped). Never `READY`.
- Through pipes: spelled as above; a request over 200 bytes is not sent (`REQUEST_UNSENT` row; the caps on typed fields
  keep every request the page makes under it), and a spelled request whose begin no helper answers stops there.
- Numbers are read with `tonumber`, so `lobby.state_text` writing floats as strings (`done = '0.4'`) works; `public` and
  `network.public_ip_known` are not shown.
- In the session (a config with `lobby`): the state is read when the page opens and every 150 frames while it is open
  (`lobby/state.lua` of the installed extension, or `<pipe>-state` when the session's config names a pipe); the page is
  drawn again when `stamp` changed. It shows the connection line, the code with Copy and the players (host and "you"
  marked from `players[].slot` and the client's own slot). A session without `lobby` shows the page as before.

## Release kit and installer (GitHub Actions)

`tools/make_release_kit.py --gate-dll DLL --out DIR [--master URL] [--python-embed ZIP] [--crypto-dll DLL]` makes a
kit that holds no session and no snapshot: `repo/` (git archive), `native/x4lan_gate_64.dll`, `python/` (embedded),
`steam-menu/`, `helper.pyw`, `second.pyw`, `release.json`, `master.json` (only with `--master`), `KIT.txt`,
`VERIFY.cmd` and `kit-manifest.json`. `--master` takes a URL (`https://master.example.org/x4mp`,
`http://203.0.113.10:47100`) or a bare `HOST[:PORT]`, which becomes `http://HOST:PORT` (port 47100);
`master.json` is `{"url": "<that URL>"}`, and the repository variable `X4MP_MASTER` holds such a URL. Sessions are
made on the host's PC (`x4ls new-session`, cached in the kit's `sessions/`) or downloaded from the game's host.
`tools/make_playtest_msi.py --kit DIR` makes the installer from it as from a playtest kit: product "X4 Online".

`.github/workflows/release.yml` (workflow_dispatch and tags `v*`): build the gate DLL (Windows, Zig 0.15.2, as
`ci.yml`), the release kit with the master address from the repository variable `X4MP_MASTER` (absent: no
`master.json`, LAN and codes through `--master` only), the MSI (Python 3.12 on Windows: `msilib`), upload both as
artifacts, and on a tag attach them to a GitHub release. No secrets are needed to build.

### Release notes (package 5, 23 Nov)

What the release build does, and what the other packages need to know about it.

- **The kit** (`tools/make_release_kit.py`): `repo/` is `git archive HEAD` of the checkout; `steam-menu/` is written
  by the exported checkout's own `steamplay.write_menu` (a child Python on `repo/src`), so the whole kit is the
  commit's. `release.json` is the contract's; `kit-manifest.json` (schema `x4-online-release-kit-v1`) adds the
  file hashes, `master` and `python_embed_sha256`. `VERIFY.cmd` (`make_release_kit.py --verify`) ignores what a PC
  makes while playing: `repo/.local/`, `sessions/`, `__pycache__/`, and a `master.json` put beside a kit built
  without one.
- **Nothing private:** the builder refuses (and removes) a kit with any file under `.local`, `.git`, `.venv`,
  `.claude`, `__pycache__`, `overlay(s)`, `snapshot(s)`, `save(s)`, `sessions`, `runs` or `logs`; named
  `server.json`, `session.json`, `.env`, credentials, SSH keys or `X4.exe`; ending `.key`, `.pem`, `.pfx`, `.p12`,
  `.ppk`, `.log`, `.xml.gz`, `.x4join`, `.sav`, `.dmp`; an X4 catalog (`01.cat`, `ext_01.dat`); a PEM private key;
  or a JSON file whose `key`, `secret`, `token`, `ticket`, `password`, `relay_seed` or `master` is long hex (and the
  wire crypto key format). The installer checks a release kit against its manifest and these rules again.
- **The installer:** a release kit becomes "X4 Online" (`%LOCALAPPDATA%\X4 Online`, Start menu "X4 Online" and
  "X4 Online (second window)", Run value `X4Online`, upgrade code `{8D3B6E2A-41C7-4F95-A0E8-6B19C2D74F31}`): a new
  release replaces an older one, never a playtest kit. Its version is `release.json`'s `built` (year, month, minute
  of the month, as playtest installers). It needs the bundled Python (`--python-embed`). Playtest kits keep "X4
  Multiplayer Playtest" and their installers' tables unchanged.
- **CI pins:** Zig as `ci.yml`; python.org's embeddable package 3.12.10 (`python-3.12.10-embed-amd64.zip`, SHA-256
  `4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3`, as python.org's SBOM for the file gives it).
  3.12.10 is the last 3.12 release with Windows binaries; a later embeddable Python means a newer minor version
  (and `python313._pth`), which `add_python` handles, while the MSI tool itself stays on 3.12 (`msilib`).
- **The wire crypto DLL** is not in a CI kit: the launcher never starts the relay with `--wire-crypto` (and the
  drop-in path `--join` excludes it). `--crypto-dll` puts it at `native/x4mp_wire_crypto.dll`; a relay started from
  a kit would then need `--wire-crypto-lib` or `X4MP_WIRE_CRYPTO_LIB`, since its default is the build folder.
- **For package 3 (`steamplay.py`, `lobby.py`):**
  - `helper.pyw` calls `steamplay.main(KIT, None)` and `second.pyw` `steamplay.second(KIT, None)`: `None` for the
    snapshot means a release kit (it has `release.json`, `native/x4lan_gate_64.dll`, `master.json` or none, and no
    `overlay/`, `snapshot/` or `server.json`). Today's `Helper` reads `overlay/play-recipe.json` and `server.json`,
    so it cannot run a release kit yet; that is package 3's to add.
  - `steamplay.TITLE` names the Start menu entry the helper restarts itself through under the redirection guard
    (`X4 Multiplayer Playtest.lnk`); a release's entry is `X4 Online.lnk`.
- **For packages 3 and 4, the menu config:** `write_menu` writes `MENU_CONFIG`, `lobby = {pipe = 'x4mp-playtest',
  server = true}`, into every kit. `server = true` makes the page the playtest server's Join-only page; a release
  needs the netplay rows (Host, Join with a code, Public games, LAN). Asked for: a release variant of the menu, for
  example `write_menu(folder, lobby_lua, release=True)` writing `lobby = {pipe = 'x4online', netplay = true}` (no
  `server`) with its own menu ID (`x4online_menu`) and pipe. Until then a playtest install and an X4 Online install
  on one PC share one pipe (the second helper ends at once) and one extension folder (removing either installer
  takes the menu out of X4). The change in the release build is one call: `MENU_SCRIPT` in `make_release_kit.py`.
- **On GitHub:** only the repository variable `X4MP_MASTER` (a URL, optional). No secrets; the tag job uses the
  run's own token with `contents: write`.

## Ports

| Port | Who | What |
|---|---|---|
| UDP 47000, TCP 47001 | host PC (LAN lobby, direct mode) | relay, lobby (as today) |
| TCP 47100 | master | HTTP API and lobby view |
| UDP 47101 | master | echo |
| UDP 47102 | LAN | discovery broadcast |
| UDP/TCP 47200-47263 | master | relays of `relay`-mode games (and their join files, TCP) |

## Python interfaces between the packages

`src/lockstep/launcher/masterclient.py` (package 2; used by 3):

```python
class MasterError(Exception): code: str; why: str; status: int     # the server's error, or 'unreachable'
class MasterClient:
    def __init__(self, base_url: str, timeout: float = 10): ...   # base_url 'http://host:47100'
    def health(self) -> dict
    def create_game(self, name, public, release, players_max, session_sha256, open_slots=(), want='auto', direct=None, lan=None) -> dict   # game_id, code, secret, host_ticket, slot
    def upload(self, game_id, secret, name, path) -> dict
    def games(self, release) -> list[dict]
    def code(self, code) -> dict
    def join(self, game_id, release, code=None) -> dict                 # ticket, slot, files
    def download(self, game_id, ticket, name, dest, sha256, size, progress=None) -> Path   # resumes with Range, checks sha-256
    def lobby(self, game_id, ticket, since=0, wait=25) -> dict
    def ready(self, game_id, ticket) -> dict
    def start(self, game_id, secret) -> dict
    def heartbeat(self, game_id, auth, tick=0, state='lobby') -> dict
    def leave(self, game_id, ticket) -> dict
    def delete(self, game_id, secret) -> dict
    def probe(self, udp_port, tcp_port, nonce) -> dict
def master_url(kit_dir=None, env=os.environ) -> str | None   # X4MP_MASTER, else master.json beside the kit, else None
```

`src/lockstep/launcher/nat.py` (package 2):

```python
class Mapping: method: str; external_ip: str | None; udp_port: int; tcp_port: int; lease: int
    def renew(self) -> bool; def release(self) -> None
def map_ports(udp_port, tcp_port, lease=3600, timeout=3.0, log=None) -> Mapping | None   # PCP, NAT-PMP, UPnP IGD in turn
def keep_mapped(mapping, stop: threading.Event) -> threading.Thread   # renews at half the lease until stop
def public_endpoint(host, echo_port=47101, timeout=2.0) -> tuple[str, int, float] | None   # ip, port, rtt_ms
def reachable(client, udp_port, tcp_port, timeout=4.0) -> dict      # {tcp: bool, udp: bool, ip: str}
def normalize_code(text) -> str | None                              # 'K7QX-3FNA' or None
```

`src/lockstep/launcher/landisco.py` and `src/lockstep/launcher/netifs.py` (package 2):

```python
class Announcer:   # threading; broadcast every 2 s until stop()
    def __init__(self, release, code, name, tcp_port, players, players_max, port=47102): ...
    def update(self, players=None, code=None): ...; def start(self): ...; def stop(self): ...
def scan(seconds=2.5, port=47102) -> list[dict]   # {ip (the sender), candidates, release, code, name, tcp_port, players, players_max}
# netifs.py
def addresses() -> list[dict]                       # {ip, netmask, broadcast, name}, up IPv4 interfaces, no loopback/169.254
def best_for(peer_ip, candidates) -> list[str]      # candidates ordered: the peer's own subnet first
```

`src/lockstep/master/` (package 1): `server.run(options, log=print)` and `server.MasterOptions` (bind, http_port,
echo_port, relay_ports, data, limits); `x4ls master` in `src/lockstep/launcher/__main__.py` calls it (package 1 adds
that subcommand; package 3 adds its own subcommands and must not remove it).

## Work packages

1. **Master** (`src/lockstep/master/`, `x4ls master`, `tools/deploy_master_server.py`, tests): the API, codes,
   registry, files, lobby/long poll, start, relay pool, echo, probe, the HTML view, limits. Reuses `serve.py`'s
   pieces (relay start, join files) where it can.
2. **NAT and networking client** (`src/lockstep/launcher/nat.py`, `src/lockstep/launcher/masterclient.py`,
   LAN discovery in `src/lockstep/launcher/landisco.py`, tests with `tools/dev/nat_fake_gateway.py`).
3. **Launcher flow** (`lobby.py`, `steamplay.py`, `__main__.py`): host (session cache, upload, port mapping, direct
   relay), join (code, browse, LAN), download, local overlay build, ready/start, the state table and the requests
   above, heartbeats, the in-session state.
4. **Pages** (`tools/dev/native_gate_lobby.lua`, the probe's `mp_menu`): the rows for the state above, a text box for
   the code (X4's options menu has edit boxes, `createEditBox`), the public list, Copy, Start, Leave; working with X4's
   stock options menu and with kuertee's UI extensions (which replace `ego_gameoptions/gameoptions`), protected UI
   mode on or off.
5. **Release build** (`tools/make_release_kit.py`, `.github/workflows/release.yml`, tests): built 23 Nov, see
   "Release notes".

## Launcher notes (package 3)

Where the flow lives: `netplay.py` (requests, the state table, the `Flow` and its one job at a time, the browse job),
`nethost.py` (HOST), `netjoin.py` (the joins), `netlan.py` (the host's TCP lobby: LAN admissions, check-ins, ready,
start; the learned allow list), `netfiles.py` (a LAN host's session files), `netkit.py` (kits, releases, sessions,
the recipe checks, the local overlay build), `netlobby.py` (x4ls lobby's backend: its X4, the pump between page and
flow, the session start), `netcli.py` (`x4ls host`, `x4ls games`, the netplay options of `x4ls join` and `lobby`),
`steamplay.py` (`NetHelper` for a release kit). Package 2's modules are imported when first used
(`netplay.Network`); without them there is no master and the flow keeps to the LAN.

Backends:
- `x4ls lobby` for a kit with a server (`--server-file`) is `lobby.lobby_run` exactly as before. Every other kit
  (release kit, playtest kit without a server, this checkout) runs netplay (`netlobby.run`), and its lobby config
  carries `lobby.netplay = true` (package 4's page keys its rows on it).
- The Steam helper of a playtest kit is unchanged (pipes `state`, `join`, `join<k>`, `stop`). A release kit
  (`release.json`) gets `NetHelper` under its own identity (`steamplay.RELEASE`: menu id `x4online_menu`, pipe prefix
  `x4online`, title "X4 Online", Start-menu entry `X4 Online.lnk` for the redirection-guard restart; the menu's config
  `X4LAN_CONFIG = {enabled = false, lobby = {pipe = 'x4online', netplay = true}}`). `tools/make_release_kit.py` writes
  the menu with `steamplay.write_menu(folder, lobby_lua, release=True)` (or `identity=steamplay.RELEASE`; its
  `MENU_SCRIPT` today passes neither and so writes the playtest menu); its helper.pyw calls
  `steamplay.main(KIT, None)` and second.pyw `steamplay.second(KIT, None)`. `remove_menu(install, identity)` removes
  only that identity's folder.
- The release helper lists games itself (BROWSE in the Steam-started X4). HOST and the joins are handed over: the
  helper says `restart`, the page quits X4, and the helper runs `x4ls lobby --kit KIT --request <text> --show
  --close-when-over --state-mirror repo/.local/helper-state.lua`. A join is resolved first in the helper (JOIN reads the
  clipboard there, JOINLAN becomes the row's code or its addresses as `JOINCODE|ip:port,ip:port`), since the lobby
  process has neither. While that game runs the helper answers `state` with the mirrored state (restamped in its own
  count) and ignores requests.

Steam-path transport (coordinator's decision, replacing `req-<hex>`): `<prefix>-tbegin` clears the buffer, one
`<prefix>-t0` .. `-tf` per hex digit of the request's UTF-8 bytes, `<prefix>-tend` decodes and runs it; every open is
answered with the state. Begin and end are `tbegin`/`tend`, not `tb`/`te` (the digits b and e; pipe names do not tell
case apart). At most 400 hex digits (200 bytes); a `tend` without `tbegin`, an odd or non-UTF-8 buffer and an overlong one
are ignored with one log line each. Every name has two serving threads, and each creates its next instance before it
answers, so a run of one digit always finds a listening instance. `join` and `join<k>` keep their names.

Phases the launcher writes: `idle`, `browsing`, `preparing` (the host's session made or found, the joiner's download and
overlay build, X4 starting again with the session), `hosting`, `joining`, `waiting` (the joiner is ready), `loading`
(the start is known; the session's config is not written yet), `session` (the config is written: the page runs it on
this phase, as before), `restart` (the Steam helper), `error` (lines say why; Cancel goes back). A kit with a server
keeps its own `starting`, `joined` and `playing`. Fields: `lines`, `code` (the join code; a LAN game's long invite code,
also as `invite` for the older page), `public`, `host`, `can_start`, `network` {mode, mapping, public_ip_known,
ping_ms}, `players` [{slot, ready, host}], `games` (at most 20: id, name, players, max, state, compatible, mode), `lan`
(at most 10: index, name, players, max, compatible), `progress` {what = session|download|overlay, done (a number)},
`slot`, `arm_at`. The state keeps being written during the session (players, code, network), and the pump reads the
page's COPY and LEAVE from the log then.

Decisions where the contract left room:
- **X4 starts again with the session.** What X4 reads only at its start must be the session's: the `-seed:` on the
  command line (the save's seed; JobDB seeds job generators from it at the load), and the extension's libraries and
  macros. So the launcher's X4 starts with the kit's own session (a playtest kit), else with the page alone (a release
  kit, seed 276944566), and starts again when a hosted or joined game needs another overlay. A host whose kit has no
  session for that many players closes X4 while `x4ls new-session` makes one (its capture client), then starts it
  with the session; `--request HOST|...` makes the session before the first start. A joiner marks itself ready at the
  master (or the LAN host) only after its X4 runs the session (download, build, restart: a ready player may be started
  at once, and the load is 20 to 30 s later). The page never sends READY; the request is accepted and resent.
- **START waits for every slot of the common load.** A session of N players needs N clients at the common load (the
  relay's barrier and the overlay are made for N), so the host starts the game at the master once every slot is taken
  and ready (START before that only says so). Fewer players: host a game for fewer, or with drop-in slots (the next
  section): those are not part of the common load, and a host with drop-in slots alone starts at once.
- **Host sessions:** `x4ls new-session --base-start x4ep1_gamestart_discover --players N --mode coop --gate-dll
  <kit's native DLL>` (every slot starts with the session's start; `x4ls lobby --base-start/--mode` change it), kept in
  the kit's `sessions/host-<start>-<N>p-<mode>-<8 hex of the DLL's sha-256>`; a playtest kit and a checkout keep
  sessions in `repo/.local/netplay/sessions` (a playtest kit's folder is checked against its manifest).
- **Uploads carry no paths:** `session.json` and the recipe's `session` name the save by its file name only
  (`snapshot.path` is the host's absolute path in a registered session).
- **The joiner's checks:** each file's sha-256 against the join answer, the save against the recipe's
  `snapshot_sha` and `session.json`, the recipe's `gate_dll_sha` against this kit's DLL (another release is refused
  with a clear line), and the recipe as data only: every `--set` item must be `native_gate_<key>=` a number,
  true/false, a quoted word or a table of those (two levels); `lua_block` names only. The overlay is built with this
  checkout's `tools/build_play_overlay.py`, this kit's DLL and probe, once per session and recipe
  (`sessions/joined-<16 hex save>-<12 hex recipe>`). Values from `session.json` reach the config through the builders'
  own literal checks (`native_gate_player_identity.lua_literal` allows identifiers only); the integrator should review
  that path once more.
- **Releases of kits without release.json:** a playtest kit is `kit-<24 hex of its overlay and snapshot>` (two copies
  of one kit match), a checkout `dev-<12 hex of its gate DLL's sha-256>`.
- **Direct mode check-in:** after the start the joiner connects to the host's lobby and sends `{'checkin': game_id,
  'slot': K}`; the host checks the slot against the master's lobby and adds the connection's source address to the
  relay's `--allow-file`. The host's TCP port is the `lan` answer's `tcp_port`, else the relay's port + 1 (the host
  registers `tcp_port = udp_port + 1`). Order: the `lan` candidates (`netifs.best_for(<this PC's primary address>,
  candidates)`: this PC's subnet first), the rest, then the host's public address (`start.relay.host`). The address
  that took the check-in is the relay host in the config. If none answers the join fails with a line; it does not
  fall back to a relay (the master chose direct for the game).
- **The host's relay** binds 0.0.0.0 (`--bind 0.0.0.0 --allow-from 127.0.0.1,<sources seen> --allow-file`); its own X4
  sends to 127.0.0.1. `--bind` of x4ls lobby only names the address put into invite codes.
- **LAN games (no master):** the host's lobby takes `{'join': 1, 'release', 'kit', 'token'}` and answers `{ok, slot,
  clients, token, kit, files, file_port}`; the joiner says `{'ready': true}` after its build; the start is `{'start':
  {host, port, arm_at, slot, token}}`. A private game needs the invite code's token; a public one takes anyone of the
  same release and is announced on the LAN with code `-` (private games are not announced). An older launcher's
  hello `{'token', 'kit'}` is a same-kit join, ready at once. A master game's host refuses LAN joins with its `code`, and
  the joiner then joins by that code. The session files come from `netfiles.py` (TCP at the relay's port number, one
  HMAC'd request per connection, resumable, only the three names, only to admitted addresses); it is kept apart from
  `joinfiles.py`, which another session is changing.
- **Join texts:** a join code (`nat.normalize_code`), a long invite code, or addresses separated by `,` or `;`
  (`host`, `host:port`; the port is the host's TCP lobby, default 47001; host names are resolved to IPv4 for the
  gate). Code-shaped text the alphabet refuses is reported as a mistyped code.
- **Master client use:** URLs go to `MasterClient` as given (`X4MP_MASTER`, `master.json`, or `--master URL`); the
  ping and public endpoint come from `nat.public_endpoint(*MasterClient.echo_address())`. `download`'s progress
  callback accepts `(done, total)`, `(name, done, total)` or a fraction. A join answer that lists fewer than the three
  files (a joiner right after the host registered) is left and joined again, up to five times.

### Drop-in games

A game hosted with drop-in slots (`HOST|p|N+M|name`, `x4ls host --players N --drop-in M`) is a session of N + M players
whose last M slots are open (`x4ls new-session --open N+1,...`; the session is kept under its own key,
`host-<start>-<N+M>p-open<slots>-<mode>-<dll>`). Every open slot starts with the session's own start, at the host's
place (no far starts). The common load is slots 1..N; the master starts the game once they are ready (a host alone:
at once), and the open slots are joined while it runs.
- **Relay mode only.** The host registers `want: "relay"` and opens no ports (no mapping, no probe): the master's relay
  runs the session with the drop-in arguments, keeps the join files and serves them to the joiner on its port (TCP, the
  pool's ports are open for it). A direct or LAN game with drop-in slots is refused with a line: the host's own relay
  would have to serve the join files on a further mapped TCP port (open).
- **The saver** (netdropin.JoinSender, in the host's job and in every common-load joiner's): the relay asks a player of
  the common load to save; the master lists the join in that player's lobby answers (`send_join`); the launcher waits
  for `save/mpjoin/j<T>.x4join` and `j<T>.xml.gz` in its X4's profile, gzips the package (`joinfiles.gzip_file`, level
  1: a 192 MB package is about 30 MB) and uploads `j<T>.x4join.gz`, then the save. A master that refuses the compressed
  copy gets the raw package. The join saves are deleted from the profile when the session ends.
- **The master** unpacks `j<T>.x4join.gz` beside it (at most 512 MiB, counted against the data budget like an upload),
  checks the unpacked bytes against the package hash the relay announced (400 `bad_gzip`, `bad_sha256`, 409
  `not_asked`), offers the compressed copy at once (the joiner's `joinfiles.fetch_package` takes it first and checks the
  unpacked bytes itself), and once the save is there too, the raw package and the save (serve's DropIn checks).
- **Slots:** after the start only open slots are given. A drop-in player who leaves (or sends nothing for 90 s) before
  the relay took its slot (no `NATIVE_GATE JOIN slot=K` row other than `failed`) frees the slot for the next one; a slot
  the relay took is gone with its player.
- **The joiner:** the master's join gives an open slot; the session files are downloaded and the overlay built as for
  any join (the recipe keeps `--open`). Its X4 is not started with the session: the job hands the backend a start with
  `late` set, the lane's X4 closes, and `hotjoin.hot_join` plays the slot against the master's relay (its early start at
  its defaults since fast-join 113dae38: X4 boots first, the save is asked once it waits in its start menu, and files
  that come late only keep it waiting there, no restart; lane and profile `c<slot>`, the join files from the relay's
  port, waiting up to 780 s for them as `x4ls join --server-file` does). A joiner of an open slot before the start
  waits for the start, then joins the same way.
- **The lists:** the public list's games carry `open` (free drop-in slots of a running game); the page shows a running
  game as `running, 1 drop-in` and joinable, or `(no drop-in slot free)`.

Command lines (all `x4ls lobby` with the first request made): `x4ls host --players N [--drop-in M] [--name NAME] [--public]`, `x4ls
join --code CODE | --address HOST[:PORT][,...] | --game-id ID`, `x4ls games [--lan] [--json]` (no X4), and `x4ls lobby
[--kit DIR] [--master URL | --no-master] [--request TEXT] [--state-mirror FILE] [--base-start S] [--mode M]`.

Open: drop-in slots in direct and LAN games (the host's relay and a mapped TCP port for the join files); the
in-session page for a drop-in joiner (its X4 is hot_join's, without the netplay page's state); a Ready button
(`can_ready`); the in-session page's requests from a Steam-started X4 while the game runs (ignored); the host's LEAVE
keeps its relay running for the others until the session ends.

## Live runs

**23 Nov, first netplay game (release kit rk1 = c48f0e43e6f1-c44872287221, the master on a VPS behind nginx/TLS):**
the desktop PC ran `x4ls host --players 2 --public` from the release kit: no session yet, so X4 closed, `x4ls new-session`
made one on this PC (discover start, 2 players), X4 came back hosting; the ports were mapped by NAT-PMP on the router,
the game registered with its code and was listed publicly; the session files went up to the master (25 MB). The
Windows laptop (same site, same public IP) ran `x4ls join --code <code>`: it downloaded the session, built the overlay
with its own copy of the release, restarted X4 with it and was ready; the master started the game in relay mode
(its relay on the VPS) and both loaded at the common second. In sync through the relay (0 mismatches, 0 clock
splits) past tick 1,720; the lobby view showed the game as 2/2 started.

- **Windows Firewall decides direct hosting.** The master's probe reached the router's mapping but not the PC (TCP not
  reached): a release kit brings its own `python.exe`, which Windows Defender Firewall has never been told about, so
  inbound connections to it are blocked (Windows asks once, "Windows Security Alert", when the program first listens;
  without an answer, or without administrator rights, it stays blocked). The host then goes through the master's relay
  by itself, which is what happened here. To host directly, allow "python.exe" of the X4 Online folder for private (and
  public, if wanted) networks. A per-user installer cannot add a firewall rule.

**23 Nov, direct mode on one site (same kit; the host run with a Python Windows Firewall already allows):** the
master's probe reached the PC through the NAT-PMP mapping, so the game registered direct; the laptop's join by code got
the host's LAN candidates from the master (both behind one router, one public IP), reached the host's lobby on its LAN
address first try, checked in, and the host's relay (UDP on every interface) allowed exactly the address the laptop
came from. Both loaded at the common second and played 17,565 ticks (9.8 game minutes, 0.88x real time) in sync.
Found and fixed: the host's heartbeat sent the local relay's tick -1 before the first step, which the master refused.

**23 Nov, without the master (LAN discovery, a VPN address):** the desktop PC hosted with `--no-master --public`;
the laptop's `x4ls games --lan` listed the game once with all five of the host's candidates (Ethernet, Tailscale,
Hamachi, ZeroTier, WSL; a scan on the host itself had listed it once per network until the scan keyed on the
candidates). The laptop then joined with `--address <the host's Tailscale address>` (Tailscale carries no broadcast):
reached the lobby, got the session from the host, checked in from its Tailscale address, which the host's relay
allowed, and both played 12,740 ticks in sync. A scan needs the same Windows Firewall permission as hosting: the
laptop's scan with the release's own python.exe heard nothing (inbound broadcast is blocked for a program Windows
has never been told about), with a Python the firewall knows it heard the game. The page's "Allow direct connections"
(FIREWALL) covers both.

**23 Nov night, the product path end to end (the CI-built "X4 Online" MSI, b2aba19d5c60):** installed on the Windows
laptop (per user; its helper started in the desktop session and put `x4online_menu` into X4's own extensions folder).
X4 started from the game folder as Steam starts it (a disposable profile standing in for the player's own): the start
menu's Multiplayer opened the netplay page through the helper's pipes (BROWSE answered by the master); a click on Host
went to the helper as a spelled request (`-tbegin`, `-t0`..`-tf`, `-tend`: `HOST|0|2|X4 game`); X4 quit by itself, the
helper made the session (`x4ls new-session`), started X4 again in its multiplayer profile hosting, mapped the ports
(NAT-PMP), and the master's probe found the laptop's firewall closed to the new python.exe, so the game went through
the server's relay (the page said so). The desktop PC, with the same MSI installed, joined with the code; both loaded
at the common second and were in sync through the relay past tick 2,154. Playtest kit and X4 Online side by side: the
playtest kit was uninstalled first on the desktop PC (its menu entry is now named apart).

**23 Nov night, a drop-in game (release kit 004517aa4d49-7dd8b65a956b, gate from 007c9991, the master on the VPS):**
host and drop-in joiner on the Windows laptop (two X4s at about half speed). `x4ls host --players 1 --drop-in 1 --public`:
the session for 2 players with slot 2 open was made (about 2.5 min), X4 started with it, the files went up (25 MB), and
the master started the game at once in relay mode (the host is the whole common load). `x4ls join --game-id <id>` from a
second lane: the master gave the open slot of the running game, the session was downloaded and the overlay built here,
then the hot join (early start): the relay queued the join at save tick 313 and the host saved it 14 s after the ask;
the host's launcher gzipped the 188 MB package to 27 MB and uploaded it in 9 s, the master unpacked and checked it and
offered it compressed; the joiner fetched it in 11.5 s. The save's upload was answered only after the master had read
the package (about 20 s, past the client's timeout: fixed, the check now runs on its own thread), so the files were
complete 41 s after the ask; X4 had waited 24 s in its menu and started again, loaded, caught up from 313, and the relay
handed over at 3,324 (the joiner at 16.8 ticks/s could not close the gap of 2,833 ticks: why=not_gaining, a pause of
about 2 min for the host). Joined at tick 3,324 with 0 digest mismatches; the relay's status past tick 4,957: no desync,
0 mismatches. Also fixed in that run: the host's session capture went to the default lanes (another session's capture
view there blocked it; now new-session gets the lobby's --lanes/--game), and a saver's retry after the timed-out answer
met not_asked and began to send the package raw (now: not_asked ends that join's sending; raw only when the master
refuses the compressed copy).
