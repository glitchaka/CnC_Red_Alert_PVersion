@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake no esta disponible en PATH.
    exit /b 1
)

if not exist "build_asset_hub\CMakeCache.txt" (
    cmake -S . -B build_asset_hub -A x64 -DRA_BUILD_SANDBOX=OFF -DRA_BUILD_ASSET_HUB=ON
    if errorlevel 1 goto :error
)

cmake --build build_asset_hub --config Release --target ra_asset_hub --parallel
if errorlevel 1 goto :error

if not exist "build_asset_hub\bin\Release\RA-AssetHub.exe" (
    echo ERROR: la compilacion termino sin generar RA-AssetHub.exe.
    exit /b 1
)

copy /y "build_asset_hub\bin\Release\RA-AssetHub.exe" "RA-AssetHub.exe" >nul

echo.
echo LISTO: %CD%\RA-AssetHub.exe
start "" /D "%CD%" "%CD%\RA-AssetHub.exe"
exit /b 0

:error
echo.
echo ERROR: la compilacion fallo.
exit /b 1
