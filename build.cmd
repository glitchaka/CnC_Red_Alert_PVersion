@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake no esta disponible en PATH.
    exit /b 1
)

rem Los assets Mystic Woods usados por el juego estan versionados en el repositorio.
if not exist "assets\mystic\plains.png" goto :restore_assets
if not exist "assets\mystic\characters\player.png" goto :restore_assets
if not exist "assets\mystic\characters\slime.png" goto :restore_assets
if not exist "assets\mystic\tilesets\grass.png" goto :restore_assets
if not exist "assets\mystic\tilesets\fences.png" goto :restore_assets
if not exist "assets\mystic\tilesets\walls.png" goto :restore_assets
goto :assets_ready

:restore_assets
where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: faltan assets versionados y git no esta disponible para restaurarlos.
    exit /b 1
)
git restore --source=HEAD -- "assets/mystic/plains.png" "assets/mystic/characters/player.png" "assets/mystic/characters/slime.png" "assets/mystic/tilesets/grass.png" "assets/mystic/tilesets/fences.png" "assets/mystic/tilesets/walls.png" >nul 2>nul
if errorlevel 1 (
    git checkout HEAD -- "assets/mystic/plains.png" "assets/mystic/characters/player.png" "assets/mystic/characters/slime.png" "assets/mystic/tilesets/grass.png" "assets/mystic/tilesets/fences.png" "assets/mystic/tilesets/walls.png" >nul 2>nul
)

:assets_ready
if not exist "assets\mystic\plains.png" goto :asset_error
if not exist "assets\mystic\characters\player.png" goto :asset_error
if not exist "assets\mystic\characters\slime.png" goto :asset_error
if not exist "assets\mystic\tilesets\grass.png" goto :asset_error
if not exist "assets\mystic\tilesets\fences.png" goto :asset_error
if not exist "assets\mystic\tilesets\walls.png" goto :asset_error

if exist "RA-Framework.exe" del /q "RA-Framework.exe" >nul 2>nul
if exist "RA-Framework-error.txt" del /q "RA-Framework-error.txt" >nul 2>nul

if not exist "build\CMakeCache.txt" (
    cmake -S . -B build -A x64
    if errorlevel 1 goto :error
)

cmake --build build --config Release --target ra_sandbox --parallel
if errorlevel 1 goto :error

if not exist "build\bin\Release\ra_sandbox.exe" (
    echo ERROR: la compilacion termino sin generar ra_sandbox.exe.
    exit /b 1
)

copy /y "build\bin\Release\ra_sandbox.exe" "RA-Framework.exe" >nul

echo.
echo LISTO: %CD%\RA-Framework.exe
start "" /D "%CD%" "%CD%\RA-Framework.exe"
exit /b 0

:asset_error
echo ERROR: faltan assets Mystic Woods versionados. Ejecuta git pull nuevamente.
exit /b 1

:error
echo.
echo ERROR: la compilacion fallo.
exit /b 1
