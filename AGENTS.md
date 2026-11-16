# AGENTS.md

**Project language:** English. User-facing text, documentation, and source
comments are written in English.

This is a native Win32 C++17 application: an MP3 player with `A-B` export
without re-encoding. Most of the code is in `main.cpp` (about 3,500 lines).

## Working with the code

Use `rg` to locate a symbol, then inspect its surrounding implementation:

```powershell
rg -n "FunctionName|g_variable" main.cpp
rg -n -C 25 "FunctionName" main.cpp
```

Consult `README.md`, `CMakeLists.txt`, and scripts when changing the build or
tests.

Search map (line numbers may change):

- geometry — `WaveAreaRect`, `WaveScrollRect`, `MenuButtonRect`;
- settings — `LoadSettings`, `SaveSettings`;
- MP3 — `ParseMp3Frame`, `IndexMp3File`, `ExportMp3Range`;
- audio — `InitAudioOutput`, `StartPlaybackAt`, `UpdatePosition`;
- waveform — `CreateDecodeWaveform`, `StartWaveLoad`, `FinishWaveLoad`;
- rendering — `DrawMain`, `DrawWave`, `DrawScrollBar`, `DrawSettingsMenu`;
- interaction — `HandleWaveMouse*`, `HandleWheel`, `RequestFrame`;
- window — `MainProc`, `RunApplication`.

## Invariants

- Export copies complete MP3 frames without decoding, re-encoding, or changing
  the volume.
- A-B positions, the cursor, `g_viewStart`, and the visible view span use the
  `0.0..1.0` range. `g_zoom` is `1.0..100.0`, and `ViewSpan()` is `1 / g_zoom`.
- Waveform generation and export run on background threads. UI updates are
  posted to the window thread through `PostMessageW`, and worker threads are
  cancelled and joined on shutdown.
- The UI is rendered with GDI and scaled through `S()`/`g_uiScale`.
- Buttons are displayed without green focus outlines.
- During waveform dragging, `RequestFrame` and `g_pendingDirty` identify changed
  areas for partial repainting during `WM_MOUSEMOVE`.

## Build and testing

```powershell
cmd /c build.bat Release
```

The output is `bin\Mp3cutter.exe`, and the BASS DLLs are copied there as well.
The SDK is located in `Bass\` by default and must not be deleted. Build outputs
are generated in `bin/` and `obj/`. The icon is included through `app.rc` and
`resource.h`; the source asset `assets\Mp3cutter.ico` is rebuilt by
`tools\generate-app-icon.ps1`.

For the parser, waveform, and export:

```powershell
tests\build_smoke.bat
tests\core_smoke.exe "C:\path\input.mp3" "$env:TEMP\range.mp3" 0.15 0.35
```

The repository does not include a test MP3. If no file is available, build the
project and explicitly state that the audio smoke test was not run. Verify the
UI in `bin\Mp3cutter.exe`.

## Launching the window

This is a native Win32 window. Launch it from the `bin` directory and wait for
the waveform to load:

```powershell
$p = Start-Process -FilePath (Resolve-Path .\bin\Mp3cutter.exe) `
    -WorkingDirectory (Resolve-Path .\bin) -PassThru
Start-Sleep -Seconds 3
$p.Refresh()
$p.MainWindowHandle
```

`MainWindowHandle` may remain zero for several seconds. Check it for up to 10
seconds; if needed, find the `Mp3cutterMainWindow` window class through
`FindWindow`. If the process exits, show its `ExitCode`. When the environment
does not allow GUI use, verify the build and logic, and state in the response
that the window was not checked manually.

Preserve unrelated existing changes. After editing the sources, rebuild
Release, run an appropriate verification, and briefly report the result.
