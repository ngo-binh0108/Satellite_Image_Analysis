@echo off
REM ==========================================================================
REM  EarthView - build the Qt desktop app
REM    build.bat          -> Release build with the portable MSVC v142 toolset (NMake)
REM    build.bat vs       -> generate a Visual Studio solution in build_vs\ and open it
REM                          (needs Visual Studio with the "Desktop development with C++" workload)
REM  Output: build\EarthView.exe (+ Qt DLLs and plugins)
REM ==========================================================================
setlocal
cd /d "%~dp0"
set PKG=D:\KJS_02\work\Stable Diffusion\ALPR\packages
set CMAKE="%PKG%\cmake\cmake-4.4.3-windows-x86_64\bin\cmake.exe"

if not exist "%PKG%\qt\5.15.2\msvc2019_64\bin\qmake.exe" (
  echo Qt not found in %PKG%\qt
  exit /b 1
)

if /I "%1"=="vs" (
  %CMAKE% -S . -B build_vs -A x64 || goto :err
  start "" build_vs\EarthView.sln
  exit /b 0
)

call "%PKG%\msvc\msvc_env.bat"
%CMAKE% -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release || goto :err
%CMAKE% --build build || goto :err
echo.
echo Built: %~dp0build\EarthView.exe
exit /b 0

:err
echo *** build failed ***
exit /b 1
