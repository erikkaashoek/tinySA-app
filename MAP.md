# MAP — read this first

tinySA-App 2.0: clean C++20 / Qt 6 rewrite of the C++Builder VCL tinySA-App (old sources in
`legacy/` = functional spec only, deleted at parity). Branch `qt6`. Plan: see "Steps" below.
Units everywhere: **Hz, dBm**. Screens are Qt Designer `.ui` files (edit in Designer, not in code).

## Status
- **Step 0 DONE**: branch, legacy/ move, CMake (core lib + app + tst_core), build.bat, QCustomPlot vendored.
- Next: Step 1 — live spectrum (model, protocol, link, sim, session, minimal main window, SpectrumView).

## Build / test
`build.bat` (VS 2026 vcvars64 + bundled cmake/ninja → build/, copies runtime DLLs, runs ctest).
`build.bat dist` also stages dist/ + tinySA-App.zip. Qt auto-detected at `C:/Qt/6.*/msvc*_64`.

## Files
| File | Role |
|---|---|
| src/core/units.h | `FreqRange` (all range edits/zoom/pan + clamping), `ScanGrid` (firmware-correct grid), `niceStep`, `formatHz`, `parseHz` |
| src/ui/mainwindow.* | main window (Designer form + thin controller) |
| src/main.cpp | entry point |
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
- Commands sent while scanraw runs wait in the shell (up to 4×5 s, then "Command timeout").
- `capture`: W×H×2 bytes RGB565 then prompt. `refresh on` pushes regions:
  `"bulk\r\n"|"fill\r\n"` + x,y,w,h int16 LE + payload + `ch> `.
- USB: VID 0483 PID 5740, 576000 baud. On open: `abort`, drain.

## Rules
- core/ has no QtWidgets; UI controllers stay thin (widgets → Session setters, Session signals → render).
- .cpp ≤ ~450 lines, headers are the contract. Every interactive widget has an objectName (autotest).
- Never small-read QSerialPort on Windows: bulk readAll into a buffer, parse from the buffer.
