# tinySA-App 2

PC software for the **tinySA** spectrum analysers (tinySA basic, tinySA Ultra, tinySA Ultra+),
rewritten in C++20 / Qt 6. It replaces the C++Builder version of tinySA-App; the device is
recognised from its `info` reply, so the controls always match
the connected model.

## Features
- Live spectrum over USB with the firmware's `scanraw`, on the exact frequency grid of the unit
- Start/stop/center/span, points (51–30000), RBW, attenuation, LNA (Ultra), LOW/HIGH input (basic),
  or take over the range set on the tinySA itself
- Max/min hold, averaging over 2–64 sweeps, normalisation, three trace memories (kept between runs),
  log (dBm) or linear (W) scale, spline and point display
- Graph interaction: hover readout, drag the frequency or level scale (ends or middle), mouse wheel
  zoom, PgUp/PgDn, Esc stops, right-click menu
- Markers: double click to add, drag, delta markers, marker list; max/min peak markers with
  sub-point (least-squares parabolic) frequency interpolation
- Save CSV, record every sweep, save or copy the graph as an image
- Screen capture of the tinySA and live remote control (click on the picture to touch the screen);
  uses DiSlord's compressed RLE8 capture when the firmware supports it
- Console with command history and four programmable macro buttons, battery voltage plot
- Firmware update over USB (DFU) with whichever Windows driver the bootloader uses
  (WinUSB, libusb-win32, libusbK or ST's DfuSe/STTub30); only the pages the firmware needs are
  erased, so the tinySA keeps its stored configuration
- Light or dark application colours, adjustable graph colours

## Running
Unzip `tinySA-App.zip` anywhere and start `tinySA-App.exe`; nothing needs installing.
Settings are kept in `tinySA-App.settings.ini` next to the exe (or in `%APPDATA%\tinySA-App`
when that folder is read-only). `tinySA-App.exe --sim` adds simulated tinySA units to the port list.

## Building
Requirements: Visual Studio 2026 (Community is fine) with *Desktop development with C++*, and
Qt 6.10 for `msvc2022_64` including *Qt Serial Port*, installed under `C:\Qt`.

```
build.bat          configure + build (build\), copy the Qt runtime, run the unit tests
build.bat dist     the same, plus a ready-to-run dist\ folder and tinySA-App.zip
```

In Visual Studio use *File > Open > Folder* on this directory (CMake project).

### Editing the screens
Every window is a Qt Designer form in `src/ui/*.ui` (main window, console, battery, screen,
settings, firmware). Open them with Qt Designer (`C:\Qt\6.10.3\msvc2022_64\bin\designer.exe`),
or from Visual Studio with the Qt VS Tools extension. The graph (`SpectrumView`) and the screen
view are *promoted* widgets: you place and size them in Designer, their drawing lives in code.
Give new widgets an objectName; after moving widgets between containers, delete `build\` once.

## Tests
- `build\tst_core.exe`: unit tests for the protocol parser, device model, sweep settings, traces,
  CSV, markers, RLE8 capture and DFU (against a simulated bootloader); run by `build.bat`.
- GUI scripts: `build\tinySA-App.exe --sim --ini test\out\t.ini --script test\smoke.txt --out test\out`
  (exit code = number of failures). `test\step*.txt` cover each feature, `test\hw_*.txt` need a real
  tinySA (`--port COMx`).

`MAP.md` describes the code layout, the tinySA protocol details and the test conventions.

## License
GPL-3.0-or-later. Uses Qt 6 (LGPL-3) and QCustomPlot 2.1.1 (GPL). Based on tinySA-App by
Erik Kaashoek, itself derived from NanoVNA-App by OneOfEleven. RLE8 screen capture format by
DiSlord (NanoVNA-D).
