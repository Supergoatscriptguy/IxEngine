@echo off
REM Build the Ixchess engine with MSVC. Run from anywhere.
setlocal

REM --- Locate the MSVC environment (vcvars64.bat) ---
set "VCVARS="
for %%E in (Enterprise Professional Community BuildTools Preview) do (
  if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
  )
  if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if "%VCVARS%"=="" (
  echo ERROR: Could not find vcvars64.bat for Visual Studio 2022.
  exit /b 1
)
call "%VCVARS%"

cd /d "%~dp0"
if not exist build mkdir build
cd build
cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release .. || exit /b 1
cmake --build . || exit /b 1
echo.
echo Build complete: %~dp0bin\ixchess-engine.exe
endlocal
