# The Multiplayer page: design (26 Nov 2026)

The main-menu Multiplayer page (`tools/dev/native_gate_lobby.lua`) and the in-session "Multiplayer session" page
(`tools/dev/native_gate_probe.lua`, block `mp_menu`) as X4 9.00 draws its own menus: a titled page, one short status
line, sections, a real table of games, forms laid out as X4's options, a progress bar, X4's warning colours and icons.
The requests and the state table stay as `docs/NETPLAY.md` ("The page and the launcher", "Page notes") defines them;
both backends (the launcher's log rows and `lobby/state.lua`, the Steam helper's pipes) are untouched by the drawing.
New state fields are optional additions; a page without them, and a launcher without them, keep working.

Phase 1 (this file): what X4 does, what the page did before, the design phase by phase. Phase 2 (26 Nov, on p2-all
210fcea9: zero-restart and host-start merged; join-starts' choosing phase built against its state fields): the views,
the parts, the optional `game` field, page tests, screenshots; "Phase 2: what was built" at the end says what differs
from the design.

## What X4 9.00's own menus do (read from the game's UI Lua; names only, no code taken)

The options menu (`ui/addons/ego_gameoptions/gameoptions.lua`, menu table `OptionsMenu`):
- **Frame and title.** `menu.createOptionsFrame(extrawide)` makes the page frame: 800 px wide, or 1220 px "extra wide"
  for list pages (Extensions, Load, Save, New game). Content starts at `menu.table.x/y` (45, 45 at 1080p) and is
  `menu.table.width` (710) or `menu.table.widthExtraWide` (1130) wide. The first row is the title: a back-arrow button
  (icon `table_arrow_inv_left`, calls `menu.onCloseElement("back")`) and the page name in bold outlined Zekton 13,
  34 px high, the title colour `row_title`. An optional warning line follows (Zekton outlined 9, word wrap).
- **Ordinary pages** are option lists drawn by `menu.displayOption` into a 5-column table: a 20 px arrow column, the
  label (the rest), and a value area of 330 px (two half columns and a 16 px icon column). Value types: `button`
  (text button), `confirmation` (centred text, with an optional second row of Yes/No buttons, `confirmline`),
  `dropdown`, `slidercell`, plain value text. `id = "header"` is a centred bold sub-header (22 px); `id = "line"` a
  1 px separator in `row_separator`. Labels are Zekton outlined 10, 19 px rows, inactive ones `text_inactive`.
- **List pages** build their own tables: a title table, then a list table with their own columns, and an info table
  beside it (`optionsmenu_cell_background`, black at 160 alpha). Extensions: 7 columns (name 40 %, id, version 13 %,
  date, an On/Off button of 10 % with a centred, coloured label, a "..." button), a bold left sub-header row of column
  names, separators between groups, names in `text_error` / `text_warning` when an extension has a problem. Load:
  extra wide, list on the left half with sortable column headers and a `menu_reload` icon button, details on the
  right. New game: starts grouped under sub-headers, `menu_recommended` icons in the arrow column, a 16:9 start image
  (`module.image`, `gamestart_default`) and the description on the right, a requirement cell on a red
  (`icon_error`) background with bold text when a start cannot be played. The right side follows the selected row
  without a redraw: `menu.onRowChanged` puts the row's data into `menu.selectedOption`, and the image and the texts
  are function cells that read it.
- **Navigation.** The first table of the page is `menu.optionTable`; Enter (or a click) on a row whose row data has a
  `callback` runs it (`menu.onSelectElement`); `menu.refresh()` keeps that table's selected row and top row. Tables
  are joined for keyboard and gamepad with `nextTable` / `prevTable` and `tabOrder`.
- **Helper** (`ui/addons/ego_detailmonitorhelper/helper.lua`, global): `Helper.scaleX/scaleY/scaleFont`,
  `Helper.borderSize`, fonts `Helper.standardFontOutlined` ("Zekton outlined"), `standardFontBoldOutlined`,
  `standardFontMono` ("Zekton fixed"), `standardFontBoldMono`; cells `createText`, `createButton` (`setText`,
  `setIcon`), `createIcon`, `createEditBox`, `createDropDown`, `createCheckBox`, `createStatusBar` (`current`, `start`,
  `max` may be functions: the bar follows them every frame), `setColSpan`, `setBackgroundColSpan`; tables `addRow`,
  `addEmptyRow`, `setColWidth`, `setColWidthPercent`, `setDefaultCellProperties`, `getVisibleHeight` (known before the
  frame is shown), `setSelectedRow`, `setTopRow`. Text may carry inline icons and colours (`\27[icon]`,
  `ColorText[...]`).
- **Colours** (`libraries/colors.xml` mappings, the global `Color` / `ColorText`): `text_normal`, `text_lowlight`,
  `text_inactive`, `text_success` / `text_positive` (green), `text_warning` (orange), `text_error` (red),
  `text_inprogress` (yellow), `text_player_current` (bright green: the player), `icon_positive`, `icon_warning`,
  `icon_error`, `row_title`, `row_separator`, `row_background_container`, `statusbar_value_default` (azure),
  `statusbar_marker_hidden`, `row_background_unselectable`, `frame_background_notification`.
- **Icons** that exist in 9.00 (`libraries/icons.xml`): `widget_tick_01`, `workshop_error` (X4's warning sign in
  lists), `menu_warning`, `menu_info`, `menu_reload`, `menu_locked` / `menu_unlocked`, `menu_recommended`,
  `mm_externallink`, `order_wait`, the start images `gamestart_*`.
- **Warnings and notices.** Problems are coloured text (`text_warning`, `text_error`) with the `workshop_error` icon in
  `icon_warning`; a blocking problem is a cell on `icon_error` with bold text. Notices that stop the player (the
  "Modified game" and online-update popups) are a centred table on `frame_background_notification` with a header,
  word-wrapped text and buttons.
- **Starts at runtime.** `GetRegisteredModules()` lists the game's starts with their name, description and image in
  the player's language; `ReadText(page, line)` reads a text of the game.

The page calls these at runtime (functions, colour and icon names); every layout, size and text below is our own.

## The page today ("before")

Screenshots of every phase but `session` (26 Nov, `x4ls lobby --page-preview tools/dev/page_preview_states.json`, the
launcher's X4 with the page alone, a 1920x1080 window drawn as 1536x864 on a scaled display) are kept outside the
repository with the phase 2 hand-over. In short, as the rows of the page (the same code under LuaJIT with the page
test's fake stock menu; `** **` is a centred bold sub-header, `[ ]` a value button or dropdown, `{ }` an edit box):

```
idle, games listed                                   hosting (master, direct)
< MULTIPLAYER                                        < MULTIPLAYER
   ** 4 public game(s), 1 on the local network. **   ** Join code: K7QX-3FNA (on the clipboard). Send it ... **
   (five empty sub-header rows)                      ** Private game, 1 of 2 players here, 1 ready. **
 ---------------------------------------------       ** Direct: the other players reach this PC (UPnP). Ping ... **
   ** Host a game **                                 ** The game loads when all 2 players are ready. **
 Visibility                     [Private]            (two empty sub-header rows)
 Players                        [2 v]              ---------------------------------------------
 Drop-in slots                  [none v]             Join code: K7QX-3FNA            [Copy]
 Game name                      {<X4 game>}          Connection: direct (UPnP), ping 38 ms
 Host the game                  [Host]                  ** Players **
   ** Join a game **                                 Player 1 (host): ready
 Code or address                {<Join code, ...>}   Player 2: not ready yet
 Join with this code or address [Join]               Start the game                  [Start]  (grey)
 Join with the code on the ...  [Join]               Cancel                          [Cancel]
   ** Public games **
 Look for games again           [Refresh]            error
 Friday coop, 1/2, open, 41 ms  [Join]               < MULTIPLAYER
 Old release game, 2/4, open, 1 [Join] (grey)           ** The master server does not know this game: **
 Full house, 4/4, open, 66 ms ( [Join] (grey)           ** not_found **
 Running drop-in, 2/4, running, [Join]                  ** Cancel to go back. **
   ** LAN games **                                   (three empty sub-header rows), then the whole start form
 Living room, 1/2               [Join]               again, then Cancel [Cancel]
```

What makes it look like a debug page (the screenshots show each):
1. The state's lines are six centred bold sub-header rows, always six, each with X4's blue title line under it: a
   striped block, and the empty ones leave a gap (about 120 px above the form at 1080p when one line is given). Long
   lines are clipped, not wrapped.
2. The lines repeat what the rows show (the code, the connection, the player count) and carry technical text: ports
   ("UDP 47000, TCP 47001"), the LAN address, a 40-character invite code, "the relay", "UPnP", exception names
   ("KeyError: 'slot'"), "Cancel to go back." over a Cancel button.
3. A game is one line of comma-joined text in the label column; its reason "(another release)" runs into the value
   column, and every row has a 330 px left-aligned "Join" button, a dark bar when it cannot be pressed. The LAN
   invite code is the "join code" row, cut with "...".
4. Sections are only sub-headers; the start form, the join form and both lists are one long column; Refresh is a row.
5. No progress bar: "Making the session: 35%" is a text row. No icons, no colour: ready and not ready, errors and
   warnings all look the same.
6. An error keeps the whole start form under the error lines; the nohelper page shows a greyed form that cannot work.
7. Cancel is a row "Cancel [Cancel]" in every phase, whatever it does (stop preparing, close the hosted game, leave).

The in-session page today: five sub-header lines (player K of N; "Step 7214 (game time 2:00)"; "In the session: every
step is shared ..."; "Relay 127.0.0.1:47000"; "Desyncs are not shown in game; the host logs them (desyncs.log)"),
the connection line, the code with Copy, "Players", "Player 1 (host)", "Player 2 (you)", a separator, "Leave the
session [Leave]".

## How the page is drawn

**X4 draws the frame and the title, the page draws the rest.** The page keeps opening through the stock
"multiplayer" page definition (`stock_draw('multiplayer')`, as now): X4 clears the layer, makes the options frame, the
title row with the back arrow and that definition's name (`ReadText(1001, 7283)`, "Multiplayer" in English, in the
player's language), and the 5-column table. In the first row call the page takes over:
1. it widens the frame for the views that need it (`frame.properties.width`, as X4's New game page does) to the extra
   wide size (`menu.widthExtraWide`, `menu.table.widthExtraWide`);
2. it uses the stock table as the **head**: under the title one status line (and a notice panel when there is one);
3. it adds its own **body** tables to `menu.optionsFrame` below the head (`y` from the head's `getVisibleHeight()`),
   with the columns each view needs, joined to the head with `nextTable` / `prevTable` and `tabOrder`;
4. it keeps the selected and top rows of its own tables over redraws (their ids exist after the first display).

Rows that must act on Enter are in the head (the first table, `menu.optionTable`) with a row-data `callback`; in the
body every action is a button cell. A list row never acts on a single click: Join is its own button.

**Fallback.** The page checks first that what it needs is there (the frame, `addTable`, the cell functions, `Helper`,
`Color`). If not, or if a view raises, it logs `DRAW_FALLBACK <why>` once and draws the option rows of before
through the stock row function (`page_rows`, unchanged, so their tests stay the fallback's), so a replaced options menu
still gets a working page. kuertee's UI Extensions keep `createOptionsFrame`, `menu.table` and `onCloseElement`, so the
native drawing is expected to work there too; the tests cover both.

**Shared parts.** The styles and parts below live in one place, `X4LAN_MP_UI`, defined at the top of
`native_gate_lobby.lua` before its session branch, so that a session the lobby made has it, and the probe's
in-session page uses it when present (its current rows otherwise, as in an `x4ls play` session without a lobby).

| Part | Look |
|---|---|
| Status line | one line under the title, Zekton outlined 9, `text_lowlight`; a warning sign and `text_warning` when the state warns; the launcher's full lines are its mouse-over |
| Section header | bold outlined Zekton 10, left, 22 px, with X4's title line (`titleColor = row_title`); an optional right-aligned count or icon button (Refresh); 1 px `row_separator` rows between groups |
| Form row | label left (Zekton outlined 10, 19 px), the control in the value area (dropdown, edit box, button with centred text); inactive labels `text_inactive`; one line of help under a control in Zekton 9 `text_lowlight` |
| Table | column-name row in the section header style, rows 19 px (25 px with a button), right-aligned numbers, a status icon column on the left |
| Status icon | `widget_tick_01` in `icon_positive` (ready, same release), `workshop_error` in `icon_warning` (another release, a warning), `order_wait` in `text_inprogress` (getting ready), `menu_locked` (private) |
| Big code | the join code in Zekton bold fixed (monospace: the code alphabet has no I and O, but 8 and B, 5 and S must read apart) at about twice the label size, a Copy button beside it that says "Copied" for 2 s after a press |
| Progress | the step list (done: tick, current: bold, next: `text_inactive`) and one status bar (`statusbar_value_default` on `row_background_unselectable`, marker hidden), the percentage right of it; without a fraction the bar sweeps |
| Notice panel | an `optionsmenu_cell_background` block: sign and bold title (`text_error` for errors, `text_warning` for warnings, `text_normal` for info), word-wrapped body in `text_normal`, its buttons right-aligned |
| Action bar | the view's buttons in one row at the bottom, centred text, the main action rightmost (Start, Host, Join); the destructive one left of it and named for what it does |

## The views, phase by phase

Every view: the title row (X4's), the status line, then the body. Texts in the sketches are the page's own; `[ ]` is a
button, `[ v]` a dropdown, `{ }` an edit box, `(i)` an icon.

### Start view: idle, browsing, starting

Extra wide, two columns as X4's list pages: the games on the left (about 60 %), joining by code and hosting on the
right.

```
< MULTIPLAYER
  3 public games, 1 on your network.                                      (status line)
+------------------------------------------------------------------+  +--------------------------------------------+
| GAMES                                                  (Refresh) |  | JOIN WITH A CODE                           |
|     Name                Players        Start      Ping           |  | {K7QX-3FNA, invite code or address      }  |
| PUBLIC                                                         3 |  | [Use the code on my clipboard]     [Join]  |
| (v) Friday coop         1 / 2          Fight      41 ms  [Join]  |  |                                            |
| (!) Old release game    2 / 4          Trade     120 ms  Other   |  | HOST A GAME                                |
|                                                          version |  | Game name      {X4 game                 }  |
|     Full house          4 / 4          Fight      66 ms  Full    |  | Start          [Fight                  v]  |
| (v) Running drop-in     2 / 4 running  Discover   52 ms  [Join]  |  | [ start image, 16:9 ]                      |
|                         1 free                                   |  | Every player starts in this start's own    |
| ON YOUR NETWORK                                                1 |  | ship, at its place, and plays its story.   |
| (v) Living room         1 / 2          Fight             [Join]  |  | Visibility     [Private                v]  |
|                                                                  |  | Players [2  v]      Drop-in slots [none v] |
| (!) Windows Firewall may hide games on your network     [Allow]  |  | Up to 4 players: 2 load together, 2 can    |
|                                                                  |  | join while the game runs.                  |
|                                                                  |  |                         [Host the game]    |
+------------------------------------------------------------------+  +--------------------------------------------+
```

- **Games table.** One table, two groups (Public, On your network) under group headers with their count; columns: a
  status icon, name (40 %), players "1 / 2" (running games "2 / 4 · running", with free drop-in slots
  "· 1 free"), start (this PC's name of the game's `start`, from `host_starts` or `GetRegisteredModules()`; empty when
  the game has none), ping (right-aligned; `text_positive` under 80 ms, `text_warning` from 160 ms), and the Join
  column. A joinable game has a Join button (`JOINGAME|<id>`, `JOINLAN|<index>`, as now). A game that cannot be joined
  has no button: its reason in that column ("Other version" in `text_warning`, "Full", "No free slot", in
  `text_inactive`), its name in `text_inactive`, the reason in full as the row's mouse-over ("Made with another
  release of X4 Online. Both players need the same release."). At most 30 rows per group, then "and N more".
- **Refresh** is the `menu_reload` icon button on the GAMES header (`BROWSE`, as now; also once by itself when the page
  opens in idle without games). Before the first answer the groups say "Looking for games..." (the dots move), empty
  groups "Nobody is hosting right now." and "No games found on your network." in `text_inactive`.
- **Firewall** (`firewall = 'blocked'`): a warning row under the network group with Allow (`FIREWALL`).
- **Join with a code.** The edit box (as now: hint, 100 bytes, Enter joins, the typing hold of NETPLAY.md stays), Join
  (`JOINCODE|<text>`), and a smaller "Use the code on my clipboard" button (`JOIN`). The box's mouse-over names the
  three forms (a join code like K7QX-3FNA, an invite code, an address).
- **Host a game.** Game name (edit box, empty: "X4 game"), Start (dropdown of `host_starts`, the default chosen;
  hidden while the launcher has not listed them, as on the host-start branch), the start's image and one line of
  what it means (ship, place, money, story or not) when `GetRegisteredModules()` knows the start, Visibility (a
  dropdown Private / Public, each with its mouse-over: "Only players with the code can join" / "Listed here for
  everyone"), Players and Drop-in slots on one row, kept valid together as now (`fit_counts`), and one plain summary
  line under them. Host the game sends `HOST|<public>|<players>[+<drop-in>]|<name>[|<start>]` exactly as today.
  Without a master server (`master = 'none'` or `'down'`, below) Public and Drop-in slots are inactive with the reason
  as mouse-over.
- **Steam** (zero-restart, `state.steam_play`, the config's `lobby.single`): a STEAM section at the bottom of the right
  column. "Steam's Play starts X4 Online" as an On/Off button with a centred, coloured label, as X4's own On/Off
  options (`STEAMPLAY|1` / `STEAMPLAY|0`); with `pending` a line "Takes effect once Steam is closed." and, in a
  Steam-started X4 with Steam running, Restart Steam (`STEAMRESTART`). With `lobby.single` (X4 that Steam's Play
  started through X4 Online) "Play single-player" is a button in the title area, right-aligned, where the player looks
  for the way back to the own profile (`SINGLEPLAYER`); its mouse-over says that X4 starts again with the own profile.
- **Inactive while busy.** In `starting` every control is inactive and the status says "Connecting to the multiplayer
  helper...".

### Progress view: preparing, joining, loading, session

Standard width, one column.

```
< MULTIPLAYER
  Joining "Friday coop"
+--------------------------------------------------------------+
| GETTING READY                                                 |
|  (v) Found the game                                           |
|  >   Downloading the game files                         40 %  |
|      [=====================-------------------------------]   |
|      Preparing the game files                                 |
|      Starting X4 with the game                                |
|  About 2 minutes.                                             |
|                                                     [Cancel]  |
+--------------------------------------------------------------+
```

- The steps come from the phase and `progress.what`: a host from the launcher's X4: Make the game (`session`),
  Open the router's ports (`ports`), Register the game (`register`), Upload it for the others (`upload`); a host from
  Steam's X4: Make the game, then the restart notice; a joiner: Find the game (`lookup`, the `joining` phase), Download
  (`download`), Prepare the files (`overlay`), Start X4 with the game (`x4`); every player at the start: Load the game
  (`loading`). A step without a fraction sweeps the bar; `progress.text` (optional) replaces the step's name, for
  example "Uploading the game for the others (31 MB)".
- `loading` counts down: "The game loads in 14 s" in the status line and the bar from `progress = {what = 'load',
  done}` (the launcher has the seconds; X4's UI Lua has no wall clock).
- `session`: the status line "Starting the game..." and no buttons; the page runs the session as now.
- The button says what it does: Cancel while preparing or joining, Leave while loading (`CANCEL` both times, as now).

### Game view: hosting, waiting

Standard width, one column.

```
< MULTIPLAYER
  Waiting for players: 1 of 2 ready.
+--------------------------------------------------------------+
| JOIN CODE                                                     |
|      K7QX-3FNA                                     [Copy]     |
|  The code is on your clipboard. Send it to the other players. |
| PLAYERS                                               1 / 2   |
|  (v) Player 1    Host · You                    Ready          |
|  (.) Player 2                                  Getting ready  |
|      Player 3    Drop-in slot                  Joins later    |
| CONNECTION                                                    |
|  Direct connection · 38 ms                                    |
|  (!) Windows Firewall blocks direct connections      [Allow]  |
| GAME                                                          |
|  Friday coop · (lock) Private · Start: Fight                  |
|                                  [Close the game]  [Start]    |
+--------------------------------------------------------------+
```

- **Join code** (`code`): big, monospace, Copy (`COPY`). A LAN invite code (40 characters, `invite`) is not drawn: the
  row says "Invite code" with Copy and "The invite code is on your clipboard."; its text is the mouse-over. Hosting on
  the LAN without a master adds "Players on your network can also join with the address 192.168.1.20" (from
  `game.address`, below).
- **Players** (`players`): one row per slot. A status icon and its word: Ready (`icon_positive` tick, `text_positive`),
  Getting ready (`order_wait`, `text_inprogress`), Joins later (drop-in slots, `text_inactive`), Open (`text_inactive`,
  a free slot of the common load). The player's own row in `text_player_current` with "You"; the host marked "Host".
  With `game.players_max` and `game.open` the empty slots are drawn too; without them only `players`.
- **Connection** (`network`), one plain line, the details as mouse-over:
  - direct: "Direct connection" (mouse-over: "Your router opened the ports by itself (UPnP)", NAT-PMP, PCP, or "a
    forwarded port");
  - relay: "Through the X4 Online server" (mouse-over: "Other players cannot reach this PC directly, so the game runs
    through the server. It works everywhere and adds a little delay.");
  - relay with drop-in slots: "Through the X4 Online server (drop-in players join there)";
  - lan: "On your local network";
  - the ping as "· 38 ms"; `network.firewall = 'blocked'`: the warning row with Allow (`FIREWALL`).
- **Game** (optional `game`): its name, Private/Public, the start.
- **Buttons**: Start (`START`, the host only, active when `can_start`; inactive with the mouse-over "Starts when every
  player is ready"); the host's Cancel is "Close the game" (mouse-over: "The game is deleted; players who joined are
  told so"), a joiner's is "Leave the game". Both send `CANCEL`.
- **Status line**: host: "Waiting for players: 1 of 2 ready." or "Every player is ready: the game loads in a moment.";
  joiner: "Joined Friday coop as player 2. The game loads when every player is ready."

### Error view: error, nohelper

Standard width. The start form is not drawn under an error: the notice says what happened and what to do.

```
< MULTIPLAYER
+--------------------------------------------------------------+
| (!) Could not join the game                                   |
|     The server does not know this code. Check it with the     |
|     host, or ask for a new one.                               |
|                                    [Back]   [Try again]       |
+--------------------------------------------------------------+
```

- The title is the state's first line, the body the others, without "Cancel to go back." (the Back button is there).
- Back sends `CANCEL` (the launcher goes idle, as now). Try again sends the page's last HOST or join request again
  (the page remembers it; nothing new for the launcher), shown only when there was one.
- `nohelper`: "The multiplayer helper is not running", "Start X4 Online from the Windows Start menu, then open this
  page again." and Try again (the page reads the state again at once).

### Restart notice: restart

Standard width; a notice in X4's notification style (`frame_background_notification`), since X4 is about to close:
"X4 restarts into the game", one line on what happens ("X4 closes and comes back in its multiplayer profile, straight
into the game. Your own profile and saves are not touched."), and a bar that runs down over `quit_after` seconds
(`getElapsedTime`, as the quit timer). No buttons.

### Start choice of a drop-in join: choosing

Extra wide, as X4's New game page: the starts on the left, the chosen one on the right.

```
< MULTIPLAYER
  A running game: choose where you start. You join as player 3.
+--------------------------------------+  +-----------------------------------------+
| STARTS                               |  | [ start image, 16:9 ]                   |
| (*) With the host                    |  | Fight                                   |
|     Fight (Paranid S fighter)        |  | The start's description, in the         |
|     Kha'ak (Kha'ak S fighter)        |  | player's language. Your ship is made in |
|                                      |  | the running game when you join; the     |
|                                      |  | others play on.                         |
|                                      |  |                 [Cancel]  [Start here]  |
+--------------------------------------+  +-----------------------------------------+
```

- One row per `choices` entry; "With the host" (index 0) first with `menu_recommended`. The list is in the head table
  (`menu.optionTable`), so the arrow keys move through it as on X4's New game list, and the right side is function
  cells that read `menu.selectedOption` (no redraw, no wrapped function). A click or Enter on a row only selects it;
  Start here sends `JOINSTART|<index>` of the selected row (as on the join-starts branch), Cancel `CANCEL`.
- With the optional `choices[].gamestart` the right side shows the start's image and description from
  `GetRegisteredModules()`; without it only the label.

### The kit server and LAN pages (lobby.server, neither)

They keep their rows and requests (`JOIN`, `JOIN<k>`, `HOST`, `COPY`, `CANCEL`) and get the same parts where it costs
nothing: the status line instead of six sub-header rows, the start picker of a running game as the choosing list.

## The in-session page ("Multiplayer session")

Standard width, through `X4LAN_MP_UI` when the lobby made the session, as before otherwise.

```
< MULTIPLAYER
  In the game. You are player 2 of 3.
+--------------------------------------------------------------+
| JOIN CODE                (only while drop-in slots are free)  |
|      K7QX-3FNA                                     [Copy]     |
| PLAYERS                                                       |
|  (v) Player 1    Host                          In the game    |
|  (v) Player 2    You                           In the game    |
|      Player 3    Drop-in slot                  Free           |
| CONNECTION   Through the X4 Online server · 80 ms             |
| PLAYING FOR  12:34                                            |
|                                          [Leave the game]     |
|                 Leave? The others play on without you.        |
|                                          [Leave]   [Stay]     |
+--------------------------------------------------------------+
```

- The step number, the relay address and the desync note move to mouse-overs (the playing time row and the
  connection row); "Not in the session" becomes the status line "The session has ended." in `text_warning`, and Leave
  goes inactive.
- Leave the game asks first, as X4's confirmation options do (a second row with Leave / Stay); Leave sends `LEAVE` as
  now.

## Plain language: what the page writes, and what it hides

The page writes its own status line from the phase and the fields; the launcher's `lines` are no longer drawn as a
block. They stay in the state (older pages, the kit and LAN pages, support) and are the status line's mouse-over.
Exceptions: the error notice (title and body from the lines), a `notice` the launcher sets, and the LAN address row.

| Phase | Status line |
|---|---|
| starting | Connecting to the multiplayer helper... |
| idle | Host a game, or join one with a code or from the list. (on a Steam X4 the restart note as mouse-over) |
| browsing | Looking for games... / 3 public games, 1 on your network. / Nobody is hosting right now. |
| preparing | Getting the game ready... (the current step is in the body) |
| hosting | Waiting for players: 1 of 2 ready. / Every player is ready: the game loads in a moment. |
| joining | Joining K7QX-3FNA... |
| waiting | Joined Friday coop as player 2. The game loads when every player is ready. |
| loading | The game loads in 14 s. |
| session | Starting the game... |
| restart, error, nohelper | (the notice says it) |
| choosing | A running game: choose where you start. You join as player 3. |

Hidden from the page, kept in the log and the mouse-overs: ports and protocols (UDP, TCP, UPnP, NAT-PMP, PCP), relay
addresses, invite codes in full, tick numbers, exception names and file names. The launcher's error lines become plain
in phase 2 where they are not: "Something went wrong in the launcher: KeyError: 'slot'" turns into "Something went
wrong in X4 Online." with "The launcher's log has the details." (the exception goes to the log only); "The master
server ..." reads "The X4 Online server ..."; a bare reason code such as `not_found` from the server is mapped to a
sentence where one is known.

Names from other players (game names) are drawn as plain text: `field()` already removes control characters, which
includes X4's ESC colour and icon codes.

## State: what the page reads (all optional, both backends through `netplay.State`)

Unchanged: `stamp`, `phase`, `lines`, `code`, `invite`, `public`, `host`, `can_start`, `network` (with `firewall`),
`players`, `games` (with `open`), `lan`, `progress`, `firewall`, `quit_after`, `steam_play` (zero-restart),
`host_starts`, `games[].start` (host-start), `choices`, `slot` (join-starts); the config's `lobby.single`. Added in
phase 2, each optional:
- `progress.what` gains `lookup`, `ports`, `register`, `upload`, `x4` and `load`; `progress.done` may be missing (no
  fraction known); `progress.text` a short plain step name.
- `game = {name, start, public, players_max, open = {slots}, address}` in hosting and waiting (built: the master's
  and the LAN host's hosting, a master join's waiting; the LAN address only for a LAN host).
- `slot`: this PC's slot in waiting and loading too (the session has it already).
- `master = 'ok'|'down'|'none'`: whether the X4 Online server answers (idle and browsing).
- `notice = {level = 'info'|'warning'|'error', text = '...'}`: a note the player must read in any phase (for example
  "No X4 Online server for this release: games on your network, by invite code or by address only.").
- `choices[].gamestart`: the start's id, for its image and description.

The requests do not change (zero-restart's `SINGLEPLAYER`, `STEAMPLAY|0/1` and `STEAMRESTART` included). The page adds
two behaviours of its own: Try again (the last HOST or join request sent again) and the Copied label after COPY.

## Phase 2: what was built (26 Nov)

- `tools/dev/native_gate_lobby.lua`: `X4LAN_MP_UI` (styles, `UI.color` by mapping name with `rawget`, text, button,
  icon, dropdown, status bar, separator and section parts, the plain connection words, `UI.session_page`) before the
  session branch; the views `native.start_view`, `progress_view`, `game_view`, `error_view`, `restart_view`,
  `choosing_view` (`VIEWS` maps the phases); `draw_native` in the draw hook, `draw_rows` (the rows of before) when it
  cannot draw. Only the start view and the choosing view add tables to X4's frame (the games and the forms; the chosen
  start's picture); every other view is rows of X4's own head table, so X4's selection, Enter on a row and its refresh
  keep working there.
- Differences from the design: the Refresh button is a text button; the start view's games table sits under the head
  in the head's width and the forms beside it (the frame is X4's extra wide one); the progress steps come from the
  phase, `progress.what` and the page's own last request (no new `what` names: the launcher's line under the bar says
  the step it is in); `master`, `notice`, `progress.text` and `choices[].gamestart` from the launcher are not built
  (the views read them if a launcher sends them: `choices[].gamestart` gives the choosing view its picture).
- `tools/dev/native_gate_probe.lua`: `mp_menu_draw_native` draws the in-session page through `UI.session_page` in a
  session the lobby made; its rows otherwise.
- Launcher: `nethost.game_summary` (the `game` field), the joiner's `game` in waiting, the plain line of an unexpected
  failure (`netplay.Job.main`).
- Tests: `tests/test_native_gate_lobby_views.py` with `tests/x4_widget_fake.py` (X4's widget handles as far as the
  pages use them; a property X4 does not know, a colspan over a created cell or past the table, a cell created twice,
  a column width after the first row are errors there), every preview state drawn, every button's request, both
  backends, the fallback, the in-session page; `tests/test_launcher_page_preview.py` (the `game` summary, the plain
  failure line, every preview state through the fallback rows too).

- Live (26 Nov, `x4ls lobby --page-preview` on this PC, one X4 below normal priority): every state of
  `tools/dev/page_preview_states.json` drawn by X4 9.00 at 1280x720 and 1920x1080, 19 draws each, no widget setup,
  view validation or draw error in X4's log. The first try showed two rules of X4 the fake did not know yet, both
  fixed and now refused by the fake as well: a button, dropdown or edit box only in a selectable row (X4's view check
  said "Button defined in an unselectable row" for the games header's Refresh, and reloaded the UI at every draw), and
  the default colspan X4's options table gives column 3 (a cell in column 4 of the head first shrinks it). A preview
  X4 beside busy test clients may take minutes to its start menu: the preview waits up to 15 minutes for it.

- Follow-up (26 Nov, after the merge): a listed game's start and the game view's summary name the start as this PC's
  X4 does (the launcher's `host_starts`: X4's text of the start; else `GetRegisteredModules()`; else the id's short
  form); the games groups carry their count in the label ("Public (4)"). `--preview-pipe` serves the preview through
  named pipes of its own, as the Steam helper does, and `tools/dev/page_preview_steam.json` has the Steam path's
  states and `preview_session` (the in-session page drawn on the start menu page by `UI.session_page`, since a real
  session needs two X4s or the master). Live: Single-player, the Steam switch, the pending line and Restart Steam drawn
  by a pipe page; BROWSE spelled back through the pipes; the in-session page with the join code, the players, the
  connection, the playing time and the Leave question drawn without an error. Keyboard by window messages
  (tools/dev/ui_window_input.py): Tab moves between the page's tables, Enter presses the selected row's button
  (Refresh sent BROWSE), Esc goes back to X4's Start Menu. The arrow keys sent that way move no selection, X4's own
  Start Menu's neither, so arrows and a gamepad are still to be tried with a real keyboard and pad.

## Phase 2 plan (as written before it was built)

1. Merge base: `contrib/ferrite-p2-all` with the three branches merged; rebase this branch's design commit on it.
2. `X4LAN_MP_UI` (styles and parts), the views above, the fallback; the probe's session page through it.
3. The optional fields in `netplay.py`, `nethost.py`, `netjoin.py`, `steamplay.py` (the same `State` for both
   backends) and plain error lines; `docs/NETPLAY.md` "Page notes" updated, pointing here.
4. Tests (`tests/test_native_gate_lobby_page.py`, `test_native_gate_mp_menu.py`, the launcher tests):
   - a widget fake (frame, tables with columns, cells recording kind, text, colour, active, handlers) beside the
     current stock fake;
   - per phase: the view's sections and texts, which buttons are active, that each button sends the request of
     NETPLAY.md (Join per row, Refresh, Host with the start, Join by code and by clipboard, Copy, Start only with
     `can_start`, Allow, Back, Try again, Start here, Leave with its confirmation);
   - the status bar follows `progress.done`; the restart bar follows `quit_after`; the error notice drops "Cancel to go
     back."; technical lines are not drawn; long names are cut at whole UTF-8 characters;
   - the fallback: a menu without `addTable` and a body draw that raises both give the option rows and one
     `DRAW_FALLBACK` row; the current row tests run on the fallback;
   - pipes and log rows: the same requests from the native view, as the current tests check;
   - the full suite before hand-over.
5. Screenshots of every phase, before (the current page) and after: one X4 in the x4-a slot once the release test
   is over, `x4ls lobby` in a disposable profile, windowed, below normal priority, lanes `C:/x4ls-lanes-mpui`, on
   display 3, at 1280x720 and 1920x1080. The phases come from the page-preview test switch, since most phases cannot
   be reached without a second PC: `x4ls lobby --page-preview tools/dev/page_preview_states.json --shots DIR
   --display 3 --lanes C:/x4ls-lanes-mpui` (the launcher writes one state after the other into `lobby/state.lua`, no
   flow, no network; every phase but `session`, which the launcher's page runs instead of drawing).
