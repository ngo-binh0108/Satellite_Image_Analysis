@echo off
REM ==========================================================================
REM  EarthView - make a folder that runs on a PC without Python or Qt installed
REM    make_dist.bat          -> ..\dist\EarthView  (EarthView.exe + Qt DLLs + engine\)
REM    make_dist.bat engine   -> rebuild only the packaged analysis engine
REM  The analysis engine (..\satimg) is frozen with PyInstaller into engine\satimg-engine.exe;
REM  EarthView.exe starts it automatically when it sits in that subfolder.
REM ==========================================================================
setlocal
cd /d "%~dp0"
set ROOT=%~dp0..
set PY="%ROOT%\.conda-env\python.exe"
set DIST=%ROOT%\dist\EarthView

if not exist %PY% (
  echo Python environment not found: %PY%  ^(see README.md, first-time setup^)
  exit /b 1
)

if /I not "%1"=="engine" (
  call "%~dp0build.bat" || goto :err
  cd /d "%~dp0"
)

echo [engine] freezing the analysis engine with PyInstaller
%PY% -m pip install --quiet pyinstaller || goto :err
%PY% -m PyInstaller --noconfirm --clean --onedir --console --name satimg-engine ^
  --paths "%ROOT%" --distpath build\engine_dist --workpath build\engine_work --specpath build ^
  --collect-all rasterio --collect-all pyproj --collect-data certifi ^
  --collect-submodules sklearn.ensemble --add-data "%ROOT%\satimg\models;satimg\models" ^
  --exclude-module matplotlib --exclude-module tkinter --exclude-module pytest ^
  --log-level WARN engine_entry.py || goto :err

echo [dist] assembling %DIST%
robocopy build "%DIST%" /MIR /XF *.pdb *.ilk *.exp *.lib *.obj *.spec Makefile CMakeCache.txt cmake_install.cmake ^
  /XD CMakeFiles EarthView_autogen engine_dist engine_work engine .cmake >nul
if %ERRORLEVEL% GEQ 8 goto :err
robocopy build\engine_dist\satimg-engine "%DIST%\engine" /MIR >nul
if %ERRORLEVEL% GEQ 8 goto :err
if exist "D:\KJS_02\work\Stable Diffusion\ALPR\packages\redist\vc_redist.x64.exe" copy /y "D:\KJS_02\work\Stable Diffusion\ALPR\packages\redist\vc_redist.x64.exe" "%DIST%\" >nul

echo.
echo Distribution folder: %DIST%
echo On the target PC run vc_redist.x64.exe once, then EarthView.exe
exit /b 0

:err
echo *** make_dist failed ***
exit /b 1
