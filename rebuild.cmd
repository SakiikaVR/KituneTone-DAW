@echo off
set VSCMD_SKIP_SENDTELEMETRY=1
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set VSCMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake
"%VSCMAKE%\CMake\bin\cmake.exe" -S D:\LMMS -B D:\LMMS\build -DWANT_SDL=ON -DWANT_JACK=OFF -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="/MD /Zi /O2 /Ob1 /DNDEBUG" -DCMAKE_C_FLAGS_RELWITHDEBINFO="/MD /Zi /O2 /Ob1 /DNDEBUG"
if errorlevel 1 exit /b 1
"%VSCMAKE%\Ninja\ninja.exe" -C D:\LMMS\build
if errorlevel 1 exit /b 1

rem Deploy the Qt runtime next to KitsuneTone.exe so it can be launched directly.
"C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe" --release --no-translations --compiler-runtime D:\LMMS\build\KitsuneTone.exe
if errorlevel 1 exit /b 1
