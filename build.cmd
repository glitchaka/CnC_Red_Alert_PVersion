@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake no esta disponible en PATH.
    exit /b 1
)

if exist "RA-Framework.exe" del /q "RA-Framework.exe" >nul 2>nul
if exist "build\bin\Release\assets" rmdir /s /q "build\bin\Release\assets" >nul 2>nul

cmake -S . -B build -A x64
if errorlevel 1 goto :error

cmake --build build --config Release --target ra_sandbox --parallel
if errorlevel 1 goto :error

if not exist "build\bin\Release\ra_sandbox.exe" (
    echo ERROR: la compilacion termino sin generar ra_sandbox.exe.
    exit /b 1
)

copy /y "build\bin\Release\ra_sandbox.exe" "RA-Framework.exe" >nul

if not exist "assets\mystic\plains.png" (
    echo ERROR: falta assets\mystic\plains.png. Ejecuta git pull nuevamente.
    exit /b 1
)
if not exist "assets\bit-bonanza\People.png" (
    echo ERROR: faltan los assets originales de Bit Bonanza.
    exit /b 1
)

echo.
echo LISTO: %CD%\RA-Framework.exe
start "" "%CD%\RA-Framework.exe"
exit /b 0

:error
echo.
echo ERROR: la compilacion fallo.
exit /b 1
