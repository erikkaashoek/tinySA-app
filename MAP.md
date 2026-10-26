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
- Remaining: DFU with the libusb-win32/libusbK/STTub30 drivers on hardware (needs a unit with
  that driver bound to 0483:DF11).

## Steps (approved plan)
0 setup · 1 live spectrum · 2 device controls + settings · 3 traces & graph interaction ·
4 markers · 5 settings/battery/console/screen-capture windows · 6 firmware upload · 7 parity + packaging.

## Tests
- `build.bat` runs `tst_core` (per-class logs: build/tst_<Class>.log; QTest stdout is not visible from Git Bash).
- GUI scripts: `build\tinySA-App.exe --sim --script test\smoke.txt --out test\out` → exit code = failures,
  log in test/out/log.txt, screenshots there too. DSL documented in src/ui/autotest.h.
- Hardware: `--port COM20 --ini test\out\hw.ini --script test\hw_step2.txt` (tinySA Ultra ZS405 on
  COM20, CAL output cabled to RF; scripts use `do send caloutput 10` and switch it off at the end).
  Always pass `--ini test\out\x.ini` so tests never touch the user's settings.
- Script helpers: `do mark` + `sweeps.new`, `idle` (stopped and link queue empty), `sent` (commands,
  vbat filtered), `messages`, `do send <cmd>`, `do clearsent`.

## Build / test
NOTE: after structural .ui changes (widgets moved between containers) do a clean rebuild
(delete build/): a stale incremental build once crashed at start-up (objects vs. generated ui header).
`build.bat` (VS 2026 vcvars64 + bundled cmake/ninja → build/ as RelWithDebInfo with PDB, copies runtime DLLs, runs ctest).
Crash stacks: `"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -g -G -y build -c "kn 20;q" build\tinySA-App.exe --sim`
`build.bat dist` also stages dist/ + tinySA-App.zip. Qt auto-detected at `C:/Qt/6.*/msvc*_64`.
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
| src/core/link.h | `DeviceLink` (own thread): handshake, 1 command in flight, per-sweep scanraw loop, battery poll, timeouts/resync, `SweepFrame` |
| src/core/simdevice.h | `SimTransport` QIODevice: firmware-exact simulated tinySA (carriers 100/250/433.92/868 MHz, 2.4/5.8 GHz) |
| src/core/session.h | GUI-thread facade owning the link thread; the only API controllers use |
| src/core/appsettings.h | `AppSettings` (+ `ViewSettings`, `Theme`): every persistent setting listed once in `visit()` |
| src/core/traces.h | `Trace` (grid + dBm, interpolating `at()`), `TraceSet`: average/hold/memories/normalisation |
| src/core/csvio.h | trace CSV write/read (separator + decimal detection), `SweepRecorder` |
| src/core/markers.h | `MarkerSet` (normal/delta, readings at nearest point), `tracePeak`/`interpolatePeak` (parabolic) |
| src/ui/mainwindow.* | main window: Designer form + controller (wiring, device, config); mainfiles.cpp = traces/memories/files; maintest.cpp = TestHost |
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
- USB: VID 0483 PID 5740, 576000 baud. On open: `abort`, drain.
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
