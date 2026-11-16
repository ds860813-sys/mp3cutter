@echo off
setlocal
set "ROOT=%~dp0.."
if not defined BASS_DIR set "BASS_DIR=%ROOT%\Bass"
set "BASS_CORE=%BASS_DIR%\bass24"
set "BASS_MIX=%BASS_DIR%\bassmix24"
set "BASS_WASAPI=%BASS_DIR%\basswasapi24"

if not exist "%BASS_CORE%\c\bass.h" (
    echo BASS SDK not found under "%BASS_DIR%".
    exit /b 1
)
if not exist "%BASS_MIX%\c\bassmix.h" (
    echo BASSmix SDK not found under "%BASS_DIR%".
    exit /b 1
)
if not exist "%BASS_WASAPI%\c\basswasapi.h" (
    echo BASSWASAPI SDK not found under "%BASS_DIR%".
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL=%%I"
    )
)
if defined VS_INSTALL set "VS_VCVARS=%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not defined VS_VCVARS (
    set "VS_VCVARS=C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
)
if not exist "%VS_VCVARS%" (
    set "VS_VCVARS=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
)
if not exist "%VS_VCVARS%" (
    echo Visual Studio x64 build tools were not found.
    exit /b 1
)

call "%VS_VCVARS%" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /O2 ^
    /I"%BASS_CORE%\c" ^
    /I"%BASS_MIX%\c" ^
    /I"%BASS_WASAPI%\c" ^
    "%~dp0core_smoke.cpp" ^
    /Fe:"%~dp0core_smoke.exe" /Fo:"%~dp0core_smoke.obj" ^
    /link "%BASS_CORE%\c\x64\bass.lib" ^
    "%BASS_MIX%\c\x64\bassmix.lib" ^
    "%BASS_WASAPI%\c\x64\basswasapi.lib" ^
    comctl32.lib comdlg32.lib dwmapi.lib uxtheme.lib user32.lib ^
    gdi32.lib shell32.lib /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
copy /Y "%BASS_CORE%\x64\bass.dll" "%~dp0bass.dll" >nul
copy /Y "%BASS_MIX%\x64\bassmix.dll" "%~dp0bassmix.dll" >nul
copy /Y "%BASS_WASAPI%\x64\basswasapi.dll" "%~dp0basswasapi.dll" >nul
if exist "%~dp0core_smoke.exe.manifest" del /Q "%~dp0core_smoke.exe.manifest"
