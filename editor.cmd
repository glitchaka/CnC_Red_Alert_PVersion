@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake no esta disponible en PATH.
    exit /b 1
)

if not exist "assets\mystic\tilesets\grass.png" goto :restore_assets
if not exist "assets\mystic\plains.png" goto :restore_assets
if not exist "assets\mystic\tilesets\decor_16x16.png" goto :restore_assets
if not exist "assets\mystic\tilesets\fences.png" goto :restore_assets
if not exist "assets\mystic\tilesets\walls.png" goto :restore_assets
if not exist "assets\mystic\tilesets\flooring.png" goto :restore_assets
if not exist "assets\mystic\tilesets\wooden.png" goto :restore_assets
if not exist "assets\mystic\objects\chest_01.png" goto :restore_assets
if not exist "assets\mystic\characters\player.png" goto :restore_assets
if not exist "assets\mystic\characters\slime.png" goto :restore_assets
goto :assets_ready

:restore_assets
git restore --source=HEAD -- assets/mystic >nul 2>nul
if errorlevel 1 git checkout HEAD -- assets/mystic >nul 2>nul

:assets_ready
if not exist "build\CMakeCache.txt" (
    cmake -S . -B build -A x64
    if errorlevel 1 goto :error
)

cmake --build build --config Release --target ra_map_editor --parallel
if errorlevel 1 goto :error

if not exist "build\bin\Release\ra_map_editor.exe" (
    echo ERROR: no se genero ra_map_editor.exe.
    exit /b 1
)

copy /y "build\bin\Release\ra_map_editor.exe" "RA-MapEditor.exe" >nul

echo.
echo LISTO: %CD%\RA-MapEditor.exe
start "" /D "%CD%" "%CD%\RA-MapEditor.exe"
exit /b 0

:error
echo.
echo ERROR: fallo la compilacion del editor.
exit /b 1
