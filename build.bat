@echo off
setlocal

set "ROOT=%~dp0"
if not defined BASS_SDK set "BASS_SDK=%ROOT%..\bass24"

if not exist "%BASS_SDK%\bass.h" (
    echo BASS SDK not found: "%BASS_SDK%"
    echo Set BASS_SDK to the directory containing bass.h and Additions.
    exit /b 1
)

set "VS_VCVARS=C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VS_VCVARS%" (
    set "VS_VCVARS=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
)
if not exist "%VS_VCVARS%" (
    echo Visual Studio 2026 x64 build tools were not found.
    exit /b 1
)

call "%VS_VCVARS%" >nul
if errorlevel 1 exit /b 1

if not exist "%ROOT%bin" mkdir "%ROOT%bin"
if not exist "%ROOT%obj" mkdir "%ROOT%obj"

rc /nologo /fo "%ROOT%obj\app.res" "%ROOT%app.rc"
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /EHsc /W4 /permissive- /O2 /GL /guard:cf ^
    /DUNICODE /D_UNICODE ^
    /I"%BASS_SDK%" ^
    /I"%BASS_SDK%\Additions\bassmix24\c" ^
    /I"%BASS_SDK%\Additions\basswasapi24\c" ^
    "%ROOT%main.cpp" "%ROOT%obj\app.res" ^
    /Fo:"%ROOT%obj\main.obj" /Fd:"%ROOT%obj\vc145.pdb" ^
    /Fe:"%ROOT%bin\Mp3cutter.exe" ^
    /link /LTCG /SUBSYSTEM:WINDOWS /GUARD:CF /DYNAMICBASE /NXCOMPAT ^
    "%BASS_SDK%\bass.lib" ^
    "%BASS_SDK%\Additions\bassmix24\c\x64\bassmix.lib" ^
    "%BASS_SDK%\Additions\basswasapi24\c\x64\basswasapi.lib" ^
    comctl32.lib comdlg32.lib dwmapi.lib uxtheme.lib user32.lib ^
    gdi32.lib shell32.lib
if errorlevel 1 exit /b 1

copy /Y "%BASS_SDK%\bin\x64\Release\bass.dll" "%ROOT%bin\bass.dll" >nul
copy /Y "%BASS_SDK%\Additions\bassmix24\x64\bassmix.dll" "%ROOT%bin\bassmix.dll" >nul
copy /Y "%BASS_SDK%\Additions\basswasapi24\x64\basswasapi.dll" "%ROOT%bin\basswasapi.dll" >nul
if exist "%ROOT%bin\Mp3cutter.exe.manifest" del /Q "%ROOT%bin\Mp3cutter.exe.manifest"

echo.
echo Build complete: "%ROOT%bin\Mp3cutter.exe"
exit /b 0
