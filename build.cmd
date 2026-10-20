@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake no esta disponible en PATH.
    exit /b 1
)

rem Mystic Woods se importa desde una sola ubicacion determinista dentro del proyecto.
rem No se escanea Downloads, Desktop ni otras carpetas del usuario.
if not exist "assets\mystic\tilesets\water-sheet.png" (
    if exist "assets\vendor\mystic_woods_free_2.2.zip" (
        powershell -NoProfile -ExecutionPolicy Bypass -File "tools\import_mystic.ps1"
        if errorlevel 1 goto :error
    )
)

rem El editor integrado requiere el pack completo.
if not exist "assets\mystic\tilesets\water-sheet.png" goto :full_pack_error
if not exist "assets\mystic\tilesets\decor_8x8.png" goto :full_pack_error
if not exist "assets\mystic\objects\objects.png" goto :full_pack_error
if not exist "assets\mystic\objects\chest_02.png" goto :full_pack_error
if not exist "assets\mystic\characters\skeleton.png" goto :full_pack_error
if not exist "assets\mystic\characters\skeleton_swordless.png" goto :full_pack_error

rem Restaura assets base versionados si alguien los borro.
if not exist "assets\mystic\plains.png" goto :restore_assets
if not exist "assets\mystic\characters\player.png" goto :restore_assets
if not exist "assets\mystic\characters\slime.png" goto :restore_assets
if not exist "assets\mystic\tilesets\grass.png" goto :restore_assets
if not exist "assets\mystic\tilesets\decor_16x16.png" goto :restore_assets
if not exist "assets\mystic\tilesets\fences.png" goto :restore_assets
if not exist "assets\mystic\tilesets\walls.png" goto :restore_assets
if not exist "assets\mystic\tilesets\flooring.png" goto :restore_assets
if not exist "assets\mystic\tilesets\wooden.png" goto :restore_assets
if not exist "assets\mystic\objects\chest_01.png" goto :restore_assets
goto :assets_ready

:restore_assets
where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: faltan assets versionados y git no esta disponible para restaurarlos.
    exit /b 1
)
git restore --source=HEAD -- assets/mystic >nul 2>nul
if errorlevel 1 git checkout HEAD -- assets/mystic >nul 2>nul

:assets_ready
if not exist "assets\mystic\plains.png" goto :asset_error
if not exist "assets\mystic\characters\player.png" goto :asset_error
if not exist "assets\mystic\characters\slime.png" goto :asset_error
if not exist "assets\mystic\tilesets\grass.png" goto :asset_error
if not exist "assets\mystic\tilesets\decor_16x16.png" goto :asset_error
if not exist "assets\mystic\tilesets\fences.png" goto :asset_error
if not exist "assets\mystic\tilesets\walls.png" goto :asset_error
if not exist "assets\mystic\tilesets\flooring.png" goto :asset_error
if not exist "assets\mystic\tilesets\wooden.png" goto :asset_error
if not exist "assets\mystic\objects\chest_01.png" goto :asset_error

powershell -NoProfile -ExecutionPolicy Bypass -File "tools\prepare_game.ps1"
if errorlevel 1 goto :error
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\fit_editor_sheets.ps1"
if errorlevel 1 goto :error

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

:full_pack_error
echo ERROR: falta Mystic Woods 2.2 completo.
echo Coloca mystic_woods_free_2.2.zip exactamente en:
echo   assets\vendor\mystic_woods_free_2.2.zip
echo build.cmd lo importa completo automaticamente; no busca archivos fuera del proyecto.
exit /b 1

:asset_error
echo ERROR: faltan assets Mystic Woods base. Ejecuta git pull nuevamente.
exit /b 1

:error
echo.
echo ERROR: la compilacion fallo.
exit /b 1
