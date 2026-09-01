@echo off
REM Profile-guided build: instrument, train on bench + NNUE search, re-link.
REM Use for release/benchmark binaries; plain build.bat is fine for dev.
setlocal

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

echo === PGO phase 1: instrumented build ===
cd build
cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DPGO=GEN .. || exit /b 1
del ..\bin\ixchess-engine.exe 2>nul
cmake --build . || exit /b 1
cd ..

echo === PGO phase 2: training runs ===
bin\ixchess-engine.exe bench || exit /b 1
bin\ixchess-engine.exe datagen "%TEMP%\pgo_train.txt" 2 15000 7 || exit /b 1
del "%TEMP%\pgo_train.txt" 2>nul

echo === PGO phase 3: optimized re-link ===
cd build
cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DPGO=USE .. || exit /b 1
del ..\bin\ixchess-engine.exe 2>nul
cmake --build . || exit /b 1
cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DPGO= .. >nul
cd ..

echo.
echo PGO build complete: %~dp0bin\ixchess-engine.exe
endlocal
