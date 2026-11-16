<p align="center">
  <img src="assets/Mp3cutter-icon.png" width="128" height="128" alt="Mp3cutter icon">
</p>

<h1 align="center">Mp3cutter</h1>

<p align="center">
  A native Win32 MP3 player with A-B range export without re-encoding.
</p>

<p align="center">
  <img src="assets/Mp3cutter-screenshot.png" alt="Mp3cutter main window" width="900">
</p>

## What it does

Mp3cutter is a small Windows desktop program for selecting an A-B range in an
MP3 file and exporting that range quickly. Export copies the original MP3
frames without decoding or re-encoding the audio.

> **Important:** `Mp3cutter.exe` does not include the BASS libraries. It cannot
> start until `bass.dll`, `bassmix.dll`, and `basswasapi.dll` are placed next to
> the executable.

## Download and run

The repository provides a 64-bit Windows executable:

- [Download Mp3cutter.exe](dist/Mp3cutter.exe)

The BASS DLLs are not included. Download the current 64-bit SDK packages
directly from Un4seen:

| Package | Download | DLL needed |
| --- | --- | --- |
| BASS 2.4 | [bass24.zip](https://www.un4seen.com/files/bass24.zip) | `x64\bass.dll` |
| BASSmix 2.4 | [bassmix24.zip](https://www.un4seen.com/files/bassmix24.zip) | `x64\bassmix.dll` |
| BASSWASAPI 2.4 | [basswasapi24.zip](https://www.un4seen.com/files/basswasapi24.zip) | `x64\basswasapi.dll` |

Create one folder and place `Mp3cutter.exe` and all three DLLs directly inside
it:

```text
Mp3cutter\
|-- Mp3cutter.exe
|-- bass.dll
|-- bassmix.dll
`-- basswasapi.dll
```

All DLLs must be the x64 versions. Do not copy them into `C:\Windows\System32`
and do not rename them. Run `Mp3cutter.exe` by double-clicking it, or from
PowerShell:

```powershell
.\Mp3cutter.exe
```

Settings are created in `settings.txt` next to the executable. The development
build is not code-signed, so Windows SmartScreen may display a warning. Verify
that the executable was downloaded from this repository.

BASS has its own license and is not covered by this project's MIT license. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the official
[Un4seen licensing terms](https://www.un4seen.com/bass.html#license).

### If the executable does not start

- Confirm that Windows 10 or 11 x64 is being used.
- Confirm that all three DLLs are in the same folder as `Mp3cutter.exe`.
- Confirm that the files are named exactly `bass.dll`, `bassmix.dll`, and
  `basswasapi.dll`.
- Confirm that the x64 DLLs were extracted, not the x86 versions.

## Using the program

1. Click `Open MP3`, or pass a file path when starting the program.
2. Click the waveform to move the playback cursor.
3. Use the mouse wheel over the waveform to zoom.
4. Hold `Shift` and use the mouse wheel to pan a zoomed waveform.
5. Right-click the waveform and choose `Set A here` or `Set B here`.
6. Use `Play A-B` to audition the selected range.
7. Click `Export A-B` to create an MP3 containing the selected range.

Markers can also be adjusted by dragging them horizontally.

## Export behavior

The exported file contains the original MP3 data. A-B boundaries are aligned to
complete MP3 frames, so they can differ slightly from the selected time. No
additional compression, volume adjustment, or audio effect is applied.

## Build from source

### Get the source

Clone the repository:

```powershell
git clone https://github.com/ds860813-sys/mp3cutter.git
cd mp3cutter
```

You can also download and extract the repository ZIP from GitHub, then open a
terminal in the extracted project directory.

### Requirements

- Windows 10 or 11, x64.
- Visual Studio 2022 or newer with the x64 C++ build tools and Windows SDK.
- 64-bit BASS 2.4, BASSmix 2.4, and BASSWASAPI 2.4 SDKs.
- CMake 3.24 or newer only when using the CMake build.
- Git is needed to clone the source; it is not needed for a downloaded ZIP.

Download `bass24.zip`, `bassmix24.zip`, and `basswasapi24.zip` from
[un4seen.com](https://www.un4seen.com/). Extract them inside the project using
the following layout:

```text
mp3cutter/
`-- Bass/
    |-- bass24/
    |   |-- c/
    |   |   |-- bass.h
    |   |   `-- x64/bass.lib
    |   `-- x64/bass.dll
    |-- bassmix24/
    |   |-- c/
    |   |   |-- bassmix.h
    |   |   `-- x64/bassmix.lib
    |   `-- x64/bassmix.dll
    `-- basswasapi24/
        |-- c/
        |   |-- basswasapi.h
        |   `-- x64/basswasapi.lib
        `-- x64/basswasapi.dll
```

The `Bass\` directory is excluded from Git because it contains third-party SDK
files.

### Build with build.bat

Open a Command Prompt in the project directory and run:

```bat
build.bat
```

To use a different SDK location:

```bat
set BASS_DIR=C:\path\to\Bass
build.bat
```

From PowerShell:

```powershell
$env:BASS_DIR = "C:\path\to\Bass"
cmd /c build.bat Release
```

The executable and the three runtime DLLs are written to `bin\`.

### Build with CMake

```bat
cmake -S . -B build -DBASS_DIR=C:\path\to\Bass
cmake --build build --config Release
```

Open a file at startup by passing its path:

```powershell
.\bin\Mp3cutter.exe "C:\Music\track.mp3"
```

### Testing

The smoke test builds a waveform, exports a range, and reopens the result
through BASS. A test MP3 is not included; provide a file you are allowed to use:

```powershell
tests\build_smoke.bat
tests\core_smoke.exe "C:\Music\track.mp3" "$env:TEMP\range.mp3" 0.15 0.35
```

Build output, the local `Bass\` SDK, test media, exported files, and editor
settings are ignored by Git. The ready-to-use executable in `dist\` is the only
tracked build artifact.

## Third-party software

BASS, BASSmix, and BASSWASAPI are products of Un4seen Developments. The local
`Bass\` directory is not distributed with this repository and is not covered by
this project's MIT license. Review the current
[Un4seen BASS licensing terms](https://www.un4seen.com/bass.html#license)
before distributing a built application. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.

## Responsibility and legal notice

Mp3cutter's original source code and executable are provided under the MIT
license on an "as is" basis, without warranties, to the extent permitted by
applicable law. The complete warranty and liability terms are in
[LICENSE](LICENSE).

This repository does not grant any rights to BASS, BASSmix, BASSWASAPI, or
music files. Users obtain the BASS libraries directly from Un4seen and decide
whether their use or redistribution complies with the applicable Un4seen
license. Users are also responsible for ensuring that they have the right to
play, edit, and export any audio file.

Mp3cutter is not affiliated with or endorsed by Un4seen Developments. This
notice is a project description, not legal advice, and it does not override
mandatory law or any license that applies to third-party software.

## License

Copyright (c) 2026 Дмитрий (ds860813-sys).

Mp3cutter is available under the [MIT License](LICENSE).
