# MAP — read this first

tinySA-App 2.0: clean C++20 / Qt 6 rewrite of the C++Builder VCL tinySA-App (old sources in
`legacy/` = functional spec only, deleted at parity). Branch `qt6`. Plan: see "Steps" below.
Units everywhere: **Hz, dBm**. Screens are Qt Designer `.ui` files (edit in Designer, not in code).

## Status
- **Step 0 DONE**: branch, legacy/ move, CMake (core lib + app + tst_core), build.bat, QCustomPlot vendored.
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
- Next: Step 5 — Settings dialog, battery window, comms console, screen capture + remote control.

## Steps (approved plan)
0 setup · 1 live spectrum · 2 device controls + settings · 3 traces & graph interaction ·
4 markers · 5 settings/battery/console/screen-capture windows · 6 firmware upload · 7 parity + remove legacy/.

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
Crash stacks: `"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -g -G -y build -c "kn 20;q" C:\work\VNA\NanoVNA-MyApp\build\tinySA-App.exe --sim`
`build.bat dist` also stages dist/ + tinySA-App.zip. Qt auto-detected at `C:/Qt/6.*/msvc*_64`.

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
| src/ui/autotest.* | script runner + `TestHost` interface |
| src/main.cpp | CLI: --sim, --port, --script, --out |
| test/tst_core.cpp | Qt Test unit tests for core, one class per module |
| vendor/qcustomplot/ | QCustomPlot 2.1.1 — never read these files |
| res/ | icon + version resource |
| legacy/ | old VCL app, reference only (Unit1.cpp protocol, Graphs.cpp look, *.dfm layouts) |

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
- USB: VID 0483 PID 5740, 576000 baud. On open: `abort`, drain.
- Screen capture: the app always sends `capture rle` = DiSlord NanoVNA-D RLE8 (any argument):
  "BM" u16 w u16 h u8 8 u8 1, u16 palette bytes + palette (BE RGB565 as read from the LCD),
  per row u16 length + PackBits (DiSlord `packbits()`) palette indices; then prompt. Firmware
  without it ignores the argument → raw W*H*2 (parser tells them apart by the header).
  Non-palette colours become index 0 (tinySA waterfall!). The user is porting RLE8 to the tinySA
  firmware (their own `rle` branch has a different 9-bit colour `capt`/`refresh rle` scheme,
  not used). NanoVNA-D region pushes end with "ch> \r\n" (tinySA: "ch> ") — both parse.

## Rules
- core/ has no QtWidgets; UI controllers stay thin (widgets → Session setters, Session signals → render).
- .cpp ≤ ~450 lines, headers are the contract. Every interactive widget has an objectName (autotest).
- Never small-read QSerialPort on Windows: bulk readAll into a buffer, parse from the buffer.
