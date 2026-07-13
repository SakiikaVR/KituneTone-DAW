@echo off
rem Rebuild portaudio.dll with ASIO support and drop it into the LMMS build.
rem
rem The vcpkg portaudio port is built without ASIO, so a vcpkg install or
rem upgrade reverts build\portaudio.dll to the non-ASIO version - rerun this
rem script afterwards. Uses the same portaudio commit as the vcpkg port (19.7)
rem plus the Steinberg ASIO SDK 2.3.3 (downloaded from steinberg.net; its
rem license restricts redistribution of ASIO-enabled binaries).

setlocal
set VSCMD_SKIP_SENDTELEMETRY=1
set WORK=%~dp0build\portaudio-asio
set PA_COMMIT=147dd722548358763a8b649b3e4b41dfffbcfbb6

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set VSCMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake

if not exist "%WORK%" mkdir "%WORK%"
cd /d "%WORK%"

if not exist portaudio (
	curl -sSLo portaudio.zip "https://codeload.github.com/PortAudio/portaudio/zip/%PA_COMMIT%"
	tar -xf portaudio.zip
	ren portaudio-%PA_COMMIT% portaudio
)
if not exist asiosdk_2.3.3_2019-06-14 (
	curl -sSLo asiosdk.zip "https://download.steinberg.net/sdk_downloads/asiosdk_2.3.3_2019-06-14.zip"
	tar -xf asiosdk.zip
)

"%VSCMAKE%\CMake\bin\cmake.exe" -S "%WORK%\portaudio" -B "%WORK%\build" -G Ninja ^
	-DCMAKE_MAKE_PROGRAM="%VSCMAKE%\Ninja\ninja.exe" -DCMAKE_BUILD_TYPE=Release ^
	-DPA_BUILD_SHARED=ON -DPA_BUILD_STATIC=OFF -DPA_USE_ASIO=ON ^
	-DPA_DLL_LINK_WITH_STATIC_RUNTIME=OFF
if errorlevel 1 exit /b 1
"%VSCMAKE%\Ninja\ninja.exe" -C "%WORK%\build"
if errorlevel 1 exit /b 1

copy /y "%WORK%\build\portaudio_x64.dll" "%~dp0build\portaudio.dll"
copy /y "%WORK%\build\portaudio_x64.dll" "%~dp0vcpkg_installed\x64-windows\bin\portaudio.dll"
if exist "%~dp0build\tests" copy /y "%WORK%\build\portaudio_x64.dll" "%~dp0build\tests\portaudio.dll"
echo Done - ASIO-enabled portaudio.dll deployed.
