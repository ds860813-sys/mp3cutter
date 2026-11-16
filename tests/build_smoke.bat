@echo off
setlocal
set "ROOT=%~dp0.."
set "BASS_SDK=%ROOT%\..\bass24"
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /O2 ^
    /I"%BASS_SDK%" ^
    /I"%BASS_SDK%\Additions\bassmix24\c" ^
    /I"%BASS_SDK%\Additions\basswasapi24\c" ^
    "%~dp0core_smoke.cpp" ^
    /Fe:"%~dp0core_smoke.exe" /Fo:"%~dp0core_smoke.obj" ^
    /link "%BASS_SDK%\bass.lib" ^
    "%BASS_SDK%\Additions\bassmix24\c\x64\bassmix.lib" ^
    "%BASS_SDK%\Additions\basswasapi24\c\x64\basswasapi.lib" ^
    comctl32.lib comdlg32.lib dwmapi.lib uxtheme.lib user32.lib ^
    gdi32.lib shell32.lib /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
copy /Y "%BASS_SDK%\bin\x64\Release\bass.dll" "%~dp0bass.dll" >nul
copy /Y "%BASS_SDK%\Additions\bassmix24\x64\bassmix.dll" "%~dp0bassmix.dll" >nul
copy /Y "%BASS_SDK%\Additions\basswasapi24\x64\basswasapi.dll" "%~dp0basswasapi.dll" >nul
if exist "%~dp0core_smoke.exe.manifest" del /Q "%~dp0core_smoke.exe.manifest"
