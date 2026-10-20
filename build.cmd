@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake no esta disponible en PATH.
    exit /b 1
)

rem Los assets originales estan versionados en este mismo repositorio.
if not exist "assets\mystic\plains.png" goto :restore_assets
if not exist "assets\bit-bonanza\People.png" goto :restore_assets
if not exist "assets\bit-bonanza\UI.png" goto :restore_assets
if not exist "assets\bit-bonanza\Misc-Future.png" goto :restore_assets
goto :assets_ready

:restore_assets
where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: faltan assets versionados y git no esta disponible para restaurarlos.
    exit /b 1
)
git restore --source=HEAD -- "assets/mystic/plains.png" "assets/bit-bonanza/People.png" "assets/bit-bonanza/UI.png" "assets/bit-bonanza/Misc-Future.png" >nul 2>nul
if errorlevel 1 (
    git checkout HEAD -- "assets/mystic/plains.png" "assets/bit-bonanza/People.png" "assets/bit-bonanza/UI.png" "assets/bit-bonanza/Misc-Future.png" >nul 2>nul
)

:assets_ready
if not exist "assets\mystic\plains.png" (
    echo ERROR: no se pudo restaurar assets\mystic\plains.png desde el repositorio.
    exit /b 1
)
if not exist "assets\bit-bonanza\People.png" (
    echo ERROR: no se pudo restaurar People.png de Bit Bonanza desde el repositorio.
    exit /b 1
)
if not exist "assets\bit-bonanza\UI.png" (
    echo ERROR: no se pudo restaurar UI.png de Bit Bonanza desde el repositorio.
    exit /b 1
)
if not exist "assets\bit-bonanza\Misc-Future.png" (
    echo ERROR: no se pudo restaurar Misc-Future.png de Bit Bonanza desde el repositorio.
    exit /b 1
)

if exist "RA-Framework.exe" del /q "RA-Framework.exe" >nul 2>nul
if exist "RA-Framework-error.txt" del /q "RA-Framework-error.txt" >nul 2>nul

rem Solo configurar desde cero si aun no existe el cache local.
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

:error
echo.
echo ERROR: la compilacion fallo.
exit /b 1
