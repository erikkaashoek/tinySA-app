# MAP — read this first

tinySA-App 2.0: clean C++20 / Qt 6 rewrite of the C++Builder VCL tinySA-App (the old app stays in the
private repository erikkaashoek/nanoVNA-myApp, branch `tinyGTC`; this history starts at the rewrite).
Branch `main`. Plan: see "Steps" below.
Units everywhere: **Hz, dBm**. Screens are Qt Designer `.ui` files (edit in Designer, not in code).

## Status
- **Step 0 DONE**: CMake (core lib + app + tst_core), build.bat, QCustomPlot vendored.
- **Step 1 DONE**: live spectrum. tst_core 29 tests; `test/smoke.txt` (sim basic + ultra) PASS;
  `test/hw_smoke.txt` PASS on real tinySA Ultra ZS405 (fw tinySA4_v1.4-241) on COM20, 450/1000/30000 pts.
- **Step 2 DONE**: RBW/atten/input/LNA controls (model-driven lists, applyModel), "Use tinySA's range",
  settings persistence (AppSettings visit(), `--ini`), window geometry, unplug detection via port poll.
  Sim: smoke + step2a/step2b (restart persistence) PASS. Hardware `test/hw_step2.txt` PASS with
  CAL→RF cable: 110 MHz harmonic exactly on its grid point (neighbours −46 dB) → firmware-grid fix
  verified. Decisions: no scanraw streaming (sweep-to-sweep overhead < 1 ms); Stop sends `abort`
  (shell reads it within 5 s); pause state tracked when sent, shutdown always resumes the unit.
- **Step 3 DONE**: TraceSet (power averaging, min/max hold, sweep-cursor partial updates, memories M1-M3
  persisted as CSV next to the ini, interpolated normalisation), CSV save/load/record (log10 fixed),
  SpectrumView = all graph drawing + interaction (hover, axis-strip drags, wheel, keys, context menu,
  log/linear W, auto scale, spline/points, image with header, clipboard). Compact left panel (fits 700 px):
  File actions moved to menu bar + toolbar. Sim step3.txt + full regression and hw_step2 PASS.
  (Max/min peak markers moved to step 4: they are markers.)
  User requests 2026-10-27: "Avg n/N" in the status bar next to Sweeps (TraceSet::averaged(), shown
  while Avg > 1), ↺ button next to Avg = restartAverage() (average and min/max hold start again with
  the next sweep). step3b.txt checks 0/16 → 1/16 → 16/16 → restart → 1/16.
- **Step 4 DONE**: MarkerSet (normal/delta vs first normal marker, selection, persisted), peak markers
  (3-point parabolic interpolation, never bin-snapped), one MarkerOverlay layer draws all marks,
  graph interaction (double click add/delete, click select, drag, drop off-plot deletes, Left/Right,
  Del, context menu), marker list under the graph (vertical splitter, View > Marker list) so the left
  panel keeps its full height. Sim step4a/b + full regression PASS; hw_cal10 PASS: CAL harmonics at
  10.0010 / 110.0013 MHz (unit shows ~+1 kHz response offset; levels quantised to 0.5 dB).
  Bug fix 2026-10-26: the hover readout covered the max label with Start near 0 Hz and a large span
  (the peak at the far left is clamped top left). markLayout() is the one geometry of all marker
  labels; placeHover() (after every layout) puts the readout top left, else bottom left or top
  centre, clear of them. step4c.txt checks it (queries hover.covers, hover.place).
- **Step 5 DONE** (5a console + battery, 5b screen capture + live remote control + DiSlord RLE8
  capture, 5c settings dialog; peak interpolation now least-squares over the top 6 dB).
  2026-10-27: screen window "Invert colours" (printer friendly, ini `screen/invert`): shown, saved and
  copied inverted; ScreenView::image() stays as the unit sent it, shownImage() is what is shown.
- **Step 6 DONE**: firmware upload. src/dfu: DfuSe engine over a
  transport interface; transports for the driver Windows bound to 0483:DF11 — WinUSB, libusb-win32
  (\\.\libusb0-NNNN), libusbK (libusb0 IOCTLs, untested) and legacy STTub30. Only the pages the
  image covers are erased (the unit keeps its stored config). Unit tests against a fake bootloader;
  sim step6.txt covers the dialog. Hardware scripts: hw_dfu_enter (`reset dfu`, bootloader in
  ~1 s) → hw_dfu_read (256 KB in ~13 s) → hw_dfu_leave (restart + reconnect + version), run one
  after the other; hw_dfu_write does enter → write + verify → read back → leave in ONE run and
  `bail`s before leaving if the write failed. The app is a GUI program: cmd.exe does not wait for
  it, so never hand the user several commands to paste at once (on 2026-09-29 they ran together;
  WinUSB allows one handle, so the write got "Access is denied" and wrote nothing).
  On the Ultra over WinUSB "STM32 BOOTLOADER": enter, read, leave PASS; hw_dfu_write PASS (run by
  the user, 2026-09-29): 236 KB = 116 pages erased + written + verified in 36 s, the 256 KB
  read-back byte-identical to the backup, config included. The auto-mode permission check blocks
  the flash erase for Claude, so the user runs hw_dfu_write. NOT yet on hardware: the
  libusb-win32/libusbK/STTub30 paths.
  tinySA4 flash: firmware `flash0` 0x08000000 (240 KB), config + settings `flash7` 0x0803C000–
  0x08040000 (only the 4 KB config is used). The unit's v1.4-244 image appears to end at 0x0802F020
  (erased up to the page end after it); 0x0802F800–0x08039AB0 is identical in the v1.4-241 and
  v1.4-244 dumps, probably left over from an older image.
- **Step 7 DONE**: parity items (show-markers toggle, step in the status bar, colour alpha, graph
  text size; the old app's `threshold` does not apply to the tinySA), dist packaging (runtime set
  verified with dumpbin), README.
- Hardware 2026-09-29 (Ultra ZS405, the user's fw tinySA4_v1.4-244-g650f538-dirty with RLE8 and
  `reset dfu`): hw_smoke, hw_screen (RLE8 ~30 KB in ~250 ms vs raw 307 KB in ~345 ms; pixel-exact
  vs raw), hw_dfu_enter/read/write/leave PASS. The remote-mode push-inside-echo race was found and
  fixed there. test/out/dfu/flash.bin = backup of that unit (v1.4-244 + config).
- Branch `refresh-rle` (2026-10-23, firmware branch of the same name): remote control sends
  `refresh rle` and falls back to `refresh on` when the reply contains "usage" (older firmware,
  every tinySA basic). Parser: `rle8` + `scrl` pushes; sim:ultra pushes them (plus a scrolling
  band), sim:basic answers usage. tst_core remoteMirrorsScreen: mirrored screen pixel-exact for
  both. Hardware (Ultra+ ZS407, fw tinySA4_v1.4-252 + refresh-rle): hw_screen PASS on old
  firmware (fallback) and new; rebuilt screen = raw capture (0 pixels differ) for on and rle.
  First format (u16 length + PackBits per row): normal 177 → 31 KB/s (5.7×). Now one PackBits
  stream per region (93 % of pushed pixels are black, runs continue over rows): normal
  289 → 34 KB/s (8.4×), big waterfall 368 → 23 KB/s (16×) at 2.12 vs 1.75 sweeps/s; every
  stream byte-identical to DiSlord packbits() per 480 indices.
- Branch `csv-mhz` (merged 2026-11-10, tinySA issue #88): Settings > Files "MHz CSV for wireless
  managers" writes rows as "433.920000,-85.25" without header lines, as the tinySA's own MHz CSV
  (CsvFormat::mhz); reading takes a frequency with a decimal mark as MHz. Test test/csvmhz.txt.
- Branch `generator` (merged 2026-11-10): signal generator mode.
  - **UI:** Device controls moved to a top bar with an Analyzer | Generator switch. The
    generator page replaces the left panel and the graph, as the unit's own screen does: big
    DigitEdit frequency and level (wheel/↑↓ per digit, typed entry), RF button (off on entry,
    "RF ?" when unknown, Esc = off), Output (path, signal type the unit makes, external gain)
    and Signal (CW / Sweep / AM / FM, basic NFM / WFM / Ext; never sweep and modulation at once).
  - **Behaviour:** on connect the app follows the unit's mode (probe: `level`). A generating
    unit opens the page with its frequency/span/ext gain/sweep time; RF, level and type are
    "?" and nothing is sent until set. Exit/disconnect leaves the unit as it is. Battery
    polled only in CW (`vbat` restarts the sweep, interrupts modulation).
  - **Fix:** the Ultra now gets `mode low input` before its first sweep (a unit left
    generating with modulation hangs in `scanraw`).
  - **Tests:** tst_core TstGenerator; sim gen_ultra / gen_basic / gen_follow + full regression
    PASS. Hardware hw_gen_start → check → follow → sweepcheck → stop PASS both ways (Ultra+
    ZS407 COM21 and basic ZS304 COM22, fw v1.4-302, RF ports cabled together): Ultra -40 dBm
    measured -38.1 dBm by the basic, basic -40 dBm measured -40.0 by the Ultra; the units'
    screens show what the app set.
  - **Code review (/code-review, 14 findings), 13 fixed:**
    - A port switch makes the level known (the mode switch puts the unit at full level).
    - DigitEdit: Enter no longer reopens the entry.
    - A generator command without a reply is forgotten in AppliedGen and sent again
      (`forgetGenCommand`).
    - Take-over: a frequency outside the app's range is moved inside, with a message; the
      Ultra's mixer/direct setting is sent once the frequency is above 1.13 GHz; ext gain or
      sweep time that could not be read gets the app's value.
    - `modulation off` goes before a span (the basic sweeps at once); a scan asked for during
      the handshake is cancelled on a generating unit; queued generator commands are dropped
      on the switch to Analyzer; the Avg label stays hidden on the generator page; test
      asserts, the sim's `levelchange`, and `formatMHz` in units.h.
    - Not done: caching DigitEdit's layout (microseconds per mouse move).
  - **Old firmware (ZS405 v1.4-220):** a command right after `output`, `modulation`,
    `levelchange` or `ext_gain` lost its request and the shell hung for good. The link waits
    300 ms after those on firmware before v1.4-266 (`DeviceInfo::directCommandRace`,
    `DeviceLink::finish`). Raw check: 20 of 20 without a hang; the five hw_gen steps PASS with
    the ZS405 generating. The older bug fixed in v1.4-252 (lost wakeup in the shell, commit
    a65addb) can still hang any command, `scanraw` included: it hung the ZS405 as an analyzer.
    No app workaround; such units need new firmware. `sim:ultra:old` emulates the generator
    race.
  - **Measured across units on the basic's LOW port:** ZS405 at -40 dBm reads -43.6 dBm; the
    basic at -40 dBm reads -40.6 on the ZS405 (unit calibration, so hw_gen_check allows 5 dB).
  - **Decided (user, 2026-11-10):**
    - No LO-harmonic range above each Ultra path's maximum.
    - Firmware before v1.4-266 shows "Old firmware: may stop answering over USB · Update the
      firmware" next to the device. The link opens Firmware update; test gen_oldfw.txt.
  - **Level limits are nominal:** the unit's `leveloffset` counts on the Ultra only when its
    output is calibrated, which the shell cannot tell.
  - **Stale build again:** adding a widget to mainwindow.ui crashed an incremental build at
    start-up in setupMarkers (Ui struct layout). Delete build\ after any mainwindow.ui change.
- Branch `burst` (2026-11-11): burst modulation of the generator (tinySA Ultra firmware v1.4-305).
  - **Detection:** the handshake sends `modulation` alone; firmware with burst lists it in the
    usage (`DeviceInfo::hasBurst`, `genTypes()`).
  - **UI:** a Burst signal type with Period / On time (seconds with units: 100u, 1.5m, 2;
    `parseSeconds`/`formatSeconds`), Count (0 = continuous) and Start burst
    (`Session::triggerBurst` -> `modulation trigger`, queued after any settings not sent yet). An
    info line gives the duty cycle or "N bursts after each Start burst", and warns that above
    1.13 GHz burst needs Path Accurate (`burstPossible`).
  - **Tests:** tst_core burstCommands, secondsInAndOut, burstDetectedAndTriggered; sim
    gen_burst.txt; full regression PASS (the default sim Ultra is now v1.4-305 with burst,
    sim:ultra:old without). Hardware Ultra+ ZS407 v1.4-305: hw_gen_burst and hw_gen_burstend
    PASS, the unit shows "MOD: BURST 1s/2s" and "MOD: BURST 2ms/10ms x3".
  - **Measured:** hw_gen_burstcheck PASS with the Ultra+ cabled to the basic's LOW input (100 MHz
    -40 dBm, on 1 s of every 2 s). Max hold -37.6 dBm, min hold -86.8 dBm: about 49 dB on/off,
    as the firmware commit reports.
  - **Firmware display race (v1.4-305):** `output on` right after `modulation burst` sometimes
    leaves "LOW OUTPUT OFF" on the unit's screen (3 of 4 fast sequences, never with AM). The
    mute flag is on: the next menu redraw shows ON.
- Branch `ui-off` (2026-11-12): the tinySA's screen is off while the app scans (firmware with
  `ui`, tinySA Ultra from v1.4-314), so commands no longer wait for screen redraws.
  - **When:** the link sends `ui off` after `pause` and before the first scan, and `ui on` before
    `resume` when scanning stops. Remote control and the generator stop the scan first, so they
    get `ui on` too.
  - **Disconnect and connect:** disconnecting writes `ui on` with the goodbye commands, and the
    handshake sends `ui on` in case an earlier app died with the screen off.
  - **Which units:** only those whose `help` lists `ui`.
  - **Touch:** the unit's own "ui on" line (touch or jog while off), when it is no reply to our
    command, stops the scan with "Scan stopped on the tinySA".
  - **Setting:** Settings > Device "Turn the tinySA screen off while scanning (faster)", ini
    device/screen_off_scanning, on by default; a change applies between two sweeps.
  - **Tests:** tst_core screenOffWhileScanning, touchGivesTheScreenBack,
    closingWhileScanningGivesTheScreenBack; sim uioff.txt (the default sim Ultra is v1.4-314
    with `ui`); full regression PASS.
  - **Hardware (hw_uioff.txt, 1–300 MHz, 450 points, RBW auto, 10 s each):**
    - ZS407: 38 sweeps with the screen off against 26 on (270 / 391 ms per sweep).
    - ZS405: 31 against 23 (329 / 451 ms).
    - ZS407 with 101 points: 43 against 28 (238 / 359 ms).
    - The unit shows "Remote only mode"; a remote touch stopped the scan.
    - The basic (v1.4-311, no `ui`) never got the command, and its sweep rate was unchanged.
  - **Still in the firmware:** with the screen off, `scanraw` over 1–301 MHz at RBW auto costs
    about 210 ms whatever the points (11: 214 ms, 101: 216, 450: 278). That is the point-0 setup:
    `scanraw` ends with `dirty = true`.
- Remaining: DFU with the libusb-win32/libusbK/STTub30 drivers on hardware (needs a unit with
  that driver bound to 0483:DF11).

## Steps (approved plan)
0 setup · 1 live spectrum · 2 device controls + settings · 3 traces & graph interaction ·
4 markers · 5 settings/battery/console/screen-capture windows · 6 firmware upload · 7 parity + packaging.
Later: signal generator (branch `generator`).

## Tests
- `build.bat` runs `tst_core` (per-class logs: build/tst_<Class>.log; QTest stdout is not visible from Git Bash).
- GUI scripts: `build\tinySA-App.exe --sim --script test\smoke.txt --out test\out` → exit code = failures,
  log in test/out/log.txt, screenshots there too. DSL documented in src/ui/autotest.h.
- Hardware: `--port COM20 --ini test\out\hw.ini --script test\hw_step2.txt` (tinySA Ultra ZS405 on
  COM20, CAL output cabled to RF; scripts use `do send caloutput 10` and switch it off at the end).
  Always pass `--ini test\out\x.ini` so tests never touch the user's settings.
  Ports change: on 2026-11-10 an Ultra+ ZS407 (1781:0A44) was on COM21 and a basic ZS304
  (0483:5740) on COM22, RF ports cabled together. The generator scripts hw_gen_*.txt need two
  units like that: five separate runs in turn, generator unit and measuring unit (commands in
  hw_gen_start.txt). The generator unit must start in analyzer mode.
- Script helpers: `do mark` + `sweeps.new`, `idle` (stopped and link queue empty), `sent` (commands,
  vbat filtered), `messages`, `do send <cmd>`, `do clearsent`.
- `waitfor` takes ONE word as its value, then the timeout: quote values with spaces
  (`waitfor sent contains "output off" 5000`). Unquoted, the second word was read as the timeout
  (0): the wait failed at once, or passed on an unrelated match.
- `pollCheck` exists in both the Console and the Screen window; scripts reach the Console's
  ("all traffic", useful with `dump console.log`).

## Build / test
NOTE: after any mainwindow.ui change that adds, removes or moves widgets, do a clean rebuild
(delete build/): stale incremental builds crashed at start-up twice (objects vs. generated ui header).
`build.bat` (VS 2026 vcvars64 + bundled cmake/ninja → build/ as RelWithDebInfo with PDB, copies runtime DLLs, runs ctest).
Crash stacks: `"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -g -G -y build -c "kn 20;q" build\tinySA-App.exe --sim`
`build.bat dist` also stages dist/ + tinySA-App-<version>.zip (version 2.0.0-28[-dirty] from build/version.txt,
written by CMake; an older tinySA-App-*.zip is deleted first, so one zip remains). Qt auto-detected at `C:/Qt/6.*/msvc*_64`.
changelog.txt (in the zip too): user-facing, one entry per release, newest first, in the firmware changelog's
format. The entry's version is the commit count after the commit that adds it (2.0.0-<count>), so commit it
directly on main as the last commit before `build.bat dist` (a branch + merge adds two commits).
Version: CMake makes `2.0.0-<commits>-g<hash>[-dirty]` (git rev-list --count / describe, like the
firmware's v1.4-255-g37e46e3) for the About box, `--version` and the exe version info (2.0.0.<commits>);
build a release from a clean, committed tree.

## Files
| File | Role |
|---|---|
| src/core/units.h | `FreqRange` (all range edits/zoom/pan + clamping), `ScanGrid` (firmware-correct grid), `niceStep`, `formatHz`, `parseHz` |
| src/core/model.h | `ModelDesc` table (basic / Ultra / Ultra+), `detectModel(info, version)`, `DeviceInfo`, help/zero parsing |
| src/core/protocol.h | `RxParser`: bytes → events (echo, lines, prompt, scanraw data, capture pixels, refresh regions); RGB565 BE decoders |
| src/core/sweepconfig.h | `SweepConfig` value + `configCommands()` diff vs `AppliedConfig`, `clampConfig()` |
| src/core/genconfig.h | `GenConfig` (generator) + `genCommands()` diff vs `AppliedGen` in firmware order, `clampGen()`/`genLimits()` (Ultra limits per ZS405/406/407), `adoptUnitGenerator()`, `outputPathText()` |
| src/core/link.h | `DeviceLink` (own thread): handshake, 1 command in flight, per-sweep scanraw loop, battery poll, timeouts/resync, `SweepFrame`; linkgen.cpp = analyzer/generator activity, GenConfig diff, mode probe + follow |
| src/core/simdevice.h | `SimTransport` QIODevice: firmware-exact simulated tinySA (carriers 100/250/433.92/868 MHz, 2.4/5.8 GHz) |
| src/core/session.h | GUI-thread facade owning the link thread; the only API controllers use |
| src/core/appsettings.h | `AppSettings` (+ `ViewSettings`, `Theme`): every persistent setting listed once in `visit()` |
| src/core/traces.h | `Trace` (grid + dBm, interpolating `at()`), `TraceSet`: average/hold/memories/normalisation |
| src/core/csvio.h | trace CSV write/read (separator + decimal detection), `SweepRecorder` |
| src/core/markers.h | `MarkerSet` (normal/delta, readings at nearest point), `tracePeak`/`interpolatePeak` (parabolic) |
| src/ui/mainwindow.* | main window: Designer form + controller (wiring, device, config); mainfiles.cpp = traces/memories/files; maintest.cpp = TestHost; maingenerator.cpp = mode switch + generator page wiring |
| src/ui/generatorpanel.* | generator page (own .ui, promoted into the main window's `modeStack`): shows a GenConfig, emits edits |
| src/ui/digitedit.* | big-digit number editor (wheel/keys per digit, inline typed entry) for frequency and level |
| src/ui/spectrumview.* | `SpectrumView` (QCustomPlot subclass, promoted in the .ui): traces, levels, hover, menu, image; spectrummouse.cpp = mouse/wheel/keys; `SiTicker` |
| src/ui/spectrummarkers.cpp | `MarkerOverlay` layerable: user markers, peak markers, readout block |
| src/ui/markermodel.* / mainmarkers.cpp | marker list table model; marker controller (intents → MarkerSet → render) |
| src/ui/consolewindow.* / batterywindow.* | console (log, history, macros), battery plot |
| src/ui/screenwindow.* / screenview.* | screen capture + live remote control (touches) |
| src/ui/settingsdialog.* | device settings, colours, scheme, files |
| src/ui/firmwaredialog.* | DFU dialog; work runs in src/dfu/firmwareupdater (own thread) |
| src/ui/maintools.cpp | creates and wires the tool windows/dialogs |
| src/dfu/dfufile.h | .bin/.hex/.dfu parsing, DfuSe flash layout string |
| src/dfu/dfuse.h | DfuSe protocol over `DfuTransport` (erase covered pages, write, verify, read, leave) |
| src/dfu/dfuusb.h | find 0483:DF11 devices + open through the bound driver (WinUSB/libusb0/libusbK/STTub30) |
| src/ui/autotest.* | script runner + `TestHost` interface |
| src/main.cpp | CLI: --sim, --port, --script, --out |
| changelog.txt | what changed per release, for users (copied into the dist zip) |
| test/tst_core.cpp | Qt Test unit tests for core, one class per module |
| vendor/qcustomplot/ | QCustomPlot 2.1.1 — never read these files |
| res/ | icon + version resource |

## Device facts (verified in firmware source C:\work\VNA\tinySA)
- Shell: commands end `\r` (`\n` ignored); echo = typed chars + `\r\n`; prompt exactly `ch> `;
  unknown command → `cmd?\r\n` + prompt.
- `scanraw S E N`: `{` + N×(`x`, lo, hi) + `}` then prompt. Value = int16 (dBm + zero)·32.
  Step = (E−S)/N (NOT N−1) → request E' = S + span·N/(N−1) (`ScanGrid::scanrawStop`).
  Aborted by touch/jog → `}` early and **no prompt**. Data flushed in 60-byte chunks (20 points).
- Commands sent while scanraw runs wait in the shell until the scan ends or 5 s have passed
  (then the shell reads on); `abort` then ends the scan with an early `}` and no prompt.
- 0 Hz shows the LO feedthrough (strongest point) — start peak tests above 0.
- `capture`: W×H×2 bytes RGB565 then prompt. `refresh on` pushes regions:
  `"bulk\r\n"|"fill\r\n"` + x,y,w,h int16 LE + payload + `ch> `.
- `refresh rle` (firmware `__REFRESH_RLE__`, tinySA4 only; others: "usage: refresh off|on"):
  a bulk of ≥256 px becomes `"rle8\r\n"` + x,y,w,h + u16 palette bytes + its own palette (sorted,
  BE RGB565) + the w×h indices as one PackBits stream without lengths (decode blocks until w×h;
  the firmware packs 480 indices at a time); smaller ones, >256 colours or not smaller stay `bulk`. The waterfall scrolls on the unit and is pushed as
  `"scrl\r\n"` + x,y,w,h + int16 dy (rows of the rect move down by dy; the new top row follows).
- USB: VID 0483 PID 5740 (tinySA, older Ultra firmware) or VID 1781 PID 0A44 (newer Ultra firmware),
  `DeviceLink::isTinySAUsbId`; 576000 baud. On open: `abort`, drain.
- Screen capture: the app always sends `capture rle` = DiSlord NanoVNA-D RLE8 (any argument):
  "BM" u16 w u16 h u8 8 u8 1, u16 palette bytes + palette (BE RGB565 as read from the LCD),
  per row u16 length + PackBits (DiSlord `packbits()`) palette indices; then prompt. Firmware
  without it ignores the argument → raw W*H*2 (parser tells them apart by the header).
  tinySA4 port (user's `capture-rle8` worktree C:\work\VNA\tinySA-rle8, `__CAPTURE_RLE8__`): sorted
  palette ≤256 colours incl. waterfall and OR-ed trace colours; other content (image viewer,
  display test) → the screen is read twice; >256 colours → raw. Time is spent on the unit (~0.8 ms
  per row: LCD read at DIV4 + lookup/packbits), not on USB (raw is USB-bound at ~890 KB/s).
  (The older `rle` branch has a different 9-bit colour `capt`/`refresh rle` scheme,
  not used). NanoVNA-D region pushes end with "ch> \r\n" (tinySA: "ch> ") — both parse.
- Generator (output mode), verified on fw v1.4-302 (ZS304, ZS407):
  - **Mode:** `mode low|high output` (high: basic only) resets all settings: output OFF, level
    at maximum, no modulation, 10 MHz (HIGH 300 MHz). Same mode again: nothing happens.
  - **Commands:** `output on|off` (Ultra also `normal|mixer`), `sweep cw|center|span`
    (`center` then `span` is exact), `level` (absolute port level, clamped silently),
    `levelchange`, `sweeptime`, `ext_gain`, `modulation off|am|fm|freq|depth|deviation`
    (basic: `off|am|nfm|wfm|extern|freq`). The Ultra sweeps only after `sweep go`.
  - **Never:** `freq` (a stub that pauses the unit). Never `scanraw` in output mode (junk;
    with modulation the sweep thread hangs until a reset).
  - **Read back:** `level` without a value prints "usage: level -76..-6" (basic HIGH
    "-38..13") only in output mode. `sweep`, `ext_gain` and `sweeptime` (e.g. "    2s") read
    back. Output on/off, level and modulation cannot: `attenuate` reports the attenuator,
    5 dB below the level on the ZS407.
  - **Side effects:** `vbat`, `level`, `sweep`, `sweeptime` restart the generator's sweep;
    `output`, `modulation`, `levelchange` do not. A paused unit applies no output changes.
  - **Burst (Ultra, v1.4-305, firmware commit 80292fa):**
    - Commands: `modulation burst`, then `modulation period <s>` (2u..89), `ontime <s>` (1u..
      period-1u), `count <n>` (0 = continuous .. 65535) and `trigger`; seconds take m/u
      suffixes, not exponents. The settings do not set `dirty`.
    - Defaults after a mode switch: 1 ms, 100 us, continuous.
    - With count > 0 the output waits for `trigger`, and every settings change re-arms it. A
      wait-mutex command stops the bursts while it runs.
    - No burst on the direct path above 1.13 GHz ("MOD: BURST N/A").
  - **`ui off|on` (Ultra, v1.4-314, firmware a092e6b):**
    - While off nothing draws, and the screen shows "Remote only mode. Touch screen to enable
      UI". `ui` alone prints "ui on|off".
    - A touch or jog gives the UI back without acting on the screen, and prints an unsolicited
      "ui on"; during a `scanraw` the touch also ends it with an early `}`.
    - Unplugging USB and the commands that need the user (`selftest`, `touchcal`, `touchtest`,
      `menu`) also give it back. Not stored; it survives mode changes.
    - A remote `touch` + `release` wakes it like a finger (the firmware waits in
      `touch_wait_release()` until the release, so send the two back to back).
  - **Old firmware:** before v1.4-266, a wait-mutex command right after one of the direct
    commands `output`, `modulation`, `levelchange` or `ext_gain` can lose its request in
    output mode. That hangs until "Command timeout" on v1.4-252..265, and for good before
    v1.4-252. v1.4-220 boots back into output mode if it was off in it.
- `reset dfu`: "Performing reset to DFU mode", then the ROM bootloader (0483:DF11). tinySA basic
  always; tinySA4 only from the user's 2026-09 firmware — older Ultra firmware just restarts, the
  firmware dialog says so after 10 s. Otherwise: power on with the jog switch pressed.

## Rules
- core/ has no QtWidgets; UI controllers stay thin (widgets → Session setters, Session signals → render).
- .cpp ≤ ~450 lines, headers are the contract. Every interactive widget has an objectName (autotest).
- Never small-read QSerialPort on Windows: bulk readAll into a buffer, parse from the buffer.
- QSerialPort emits errorOccurred synchronously from inside write()/waitForBytesWritten(): handle it
  queued, and DeviceLink::shutdown() detaches + disconnects the port before its goodbye writes (a
  direct handler recursed until the stack overflowed when `reset dfu` removed the port mid-scan).
