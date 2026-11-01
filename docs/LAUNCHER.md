# x4ls launcher

`python tools/x4ls.py <command>` runs lockstep clients on one Windows PC. Every
client is a separate, windowed X4 process with its own game view and its own
disposable profile.

## Commands

| Command | What it does |
|---|---|
| `doctor [--overlay DIR]` | Finds X4 through Steam's library folders, verifies the pinned 9.00 `X4.exe`, and checks that directly started clients can reach Steam |
| `snapshot --out FILE [--gamestart custom_creative]` | One client starts a new game, spawns the 12-ship gate fixture and saves it; FILE is the starting snapshot |
| `session --clients N --overlay DIR [--snapshot FILE] [--ticks T] [--tick-hz 30] [--observe] [--size 960x540]` | Starts the relay and N clients, tiles their windows, waits for the relay to finish, collects logs into `.local/runs/<time>/` and cleans up |
| `clean` | Stops processes and removes views left behind by a crashed session |
| `steam-fix [--restore]` | See "Steam" below |

## How a client is isolated

- **Game view:** `<game drive>:\x4ls-lanes\cK` holds hard links to the
  installation's top-level files and junctions to its folders, so nothing is
  copied. `extensions\` is a real folder with junctions to the official DLCs and
  this client's own `x4_lan_lab`. Third-party mods in the installation are not
  linked. The probe finds the gate DLL relative to the working directory, so it
  has to run from the view. Removing a view unlinks; it never enters a junction.
- **Profile:** `-personalfolderid` uses 45850, 45851, 45853 to 45858 for clients,
  and 45841 for snapshot capture only. The profile is wiped before each run,
  then gets a windowed `config.xml` (autosave off, protected UI off) and the
  snapshot in `save_009`. Any other profile id, including your own, is refused.
- **Extension:** `src/lockstep/base` plus the overlay, with
  `@X4LANE_PORT@ / TOKEN / SLOT / CLIENTS@` filled in per client. The overlay's
  manifest is checked first.

## Steam

X4's `steam_api64.dll` finds the running Steam client through
`HKCU\Software\Valve\Steam\ActiveProcess\pid`. Current Steam clients can leave
that at 0, and then every directly started X4 stops with "Steam client is
required". Steam's own launch works but allows only one instance, and it asks
you to confirm the extra arguments.

`x4ls steam-fix` writes the running Steam's process id into that value and
records the old one. `x4ls steam-fix --restore` puts it back. The session
command never changes it by itself; `doctor` tells you when it is needed. Steam
still checks ownership as usual. Only `SteamAppId`/`SteamGameId` are added to a
client's environment. Copying a Steam-launched game's whole environment
crashed the Steam client once in testing.

Use `session --steam-fix` to register Steam's process id before each client and
restore the old value when the session ends.

## Several X4 processes

X4 creates the named mutex `EGOSOFT_X4_INSTANCE`. A second process shows "Game is
already running!" and exits with code 50 when that box is closed. The launcher
starts clients one after another. Once client k logs `Entering startmenu`, the
launcher closes client k's handle to that mutex (`DuplicateHandle` with
`DUPLICATE_CLOSE_SOURCE`, `instance.py`), and then starts client k+1. Only
processes the launcher started are touched.

Every gate gives up after 60 s without relay permits (stop reason 7). With
snapshot overlays, x4ls therefore sets `native_gate_load_hold`: client k idles
in the start menu for (N−k)×45 s + 10 s before arming and loading, so clients
that start one after another still arm together. `--stagger` changes the 45 s.

## Overlays

Build one with the existing builders, for example the two-human sandbox:

```
python tools/dev/build_native_gate_overlay.py --mode battle --gate-dll .local/native-lockstep/build/x4lan_gate_64.dll --snapshot <save> --session-clients ... --out .local/overlays/base
python tools/dev/native_gate_sandbox.py overlay --base .local/overlays/base --gate-dll ... --probe tools/dev/native_gate_probe.lua --out .local/overlays/sandbox
```

Overlays are frozen: a builder refuses to write into an existing directory, and
`x4ls` refuses an overlay whose files no longer match its manifest.

## Outputs

`.local/runs/<time>/`: `relay.log`, `native-barrier.json` (relay report),
`native-inputs.jsonl` (input journal), `desync-status.json` (observe mode),
`clientK/x4ls-cK.log` (each X4 log), and `summary.json` (passed, failed,
relay-failed, or timeout).
