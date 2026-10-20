@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake no esta disponible en PATH.
    exit /b 1
)

rem Importa automaticamente el ZIP completo de Mystic Woods si esta disponible.
set "MWZIP="
if exist "mystic_woods_free_2.2.zip" set "MWZIP=%CD%\mystic_woods_free_2.2.zip"
if not defined MWZIP if exist "%USERPROFILE%\Downloads\mystic_woods_free_2.2.zip" set "MWZIP=%USERPROFILE%\Downloads\mystic_woods_free_2.2.zip"
if not defined MWZIP if exist "%USERPROFILE%\Desktop\mystic_woods_free_2.2.zip" set "MWZIP=%USERPROFILE%\Desktop\mystic_woods_free_2.2.zip"
if defined MWZIP call :import_mystic

rem El editor integrado exige el pack completo, no vuelve a la paleta reducida.
if not exist "assets\mystic\tilesets\water-sheet.png" goto :full_pack_error
if not exist "assets\mystic\tilesets\decor_8x8.png" goto :full_pack_error
if not exist "assets\mystic\objects\objects.png" goto :full_pack_error
if not exist "assets\mystic\objects\chest_02.png" goto :full_pack_error
if not exist "assets\mystic\characters\skeleton.png" goto :full_pack_error
if not exist "assets\mystic\characters\skeleton_swordless.png" goto :full_pack_error

rem Restaura los assets base versionados si alguien los borro localmente.
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

rem Genera la fuente de juego/editor con todas las hojas Mystic Woods detectadas.
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

:import_mystic
set "MWTMP=%TEMP%\click_command_mystic_%RANDOM%_%RANDOM%"
if exist "%MWTMP%" rmdir /s /q "%MWTMP%"
mkdir "%MWTMP%" >nul 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath '%MWZIP%' -DestinationPath '%MWTMP%' -Force"
if errorlevel 1 (
    echo ERROR: no se pudo extraer %MWZIP%
    rmdir /s /q "%MWTMP%" >nul 2>nul
    exit /b 1
)
if not exist "%MWTMP%\sprites" (
    echo ERROR: el ZIP Mystic Woods no contiene la carpeta sprites esperada.
    rmdir /s /q "%MWTMP%" >nul 2>nul
    exit /b 1
)
if not exist "assets\mystic" mkdir "assets\mystic"
xcopy "%MWTMP%\sprites\*" "assets\mystic\" /E /I /Y /Q >nul

rem Alias usados por gameplay legado; el editor usa tambien la estructura completa original.
if exist "assets\mystic\tilesets\plains.png" copy /y "assets\mystic\tilesets\plains.png" "assets\mystic\plains.png" >nul
if exist "assets\mystic\tilesets\floors\flooring.png" copy /y "assets\mystic\tilesets\floors\flooring.png" "assets\mystic\tilesets\flooring.png" >nul
if exist "assets\mystic\tilesets\floors\wooden.png" copy /y "assets\mystic\tilesets\floors\wooden.png" "assets\mystic\tilesets\wooden.png" >nul
if exist "assets\mystic\tilesets\walls\walls.png" copy /y "assets\mystic\tilesets\walls\walls.png" "assets\mystic\tilesets\walls.png" >nul
rmdir /s /q "%MWTMP%" >nul 2>nul
exit /b 0

:full_pack_error
echo ERROR: falta el pack completo Mystic Woods 2.2.
echo Deja mystic_woods_free_2.2.zip en la carpeta del repo, Downloads o Desktop y ejecuta build.cmd otra vez.
exit /b 1

:asset_error
echo ERROR: faltan assets Mystic Woods base. Ejecuta git pull nuevamente.
exit /b 1

:error
echo.
echo ERROR: la compilacion fallo.
exit /b 1
