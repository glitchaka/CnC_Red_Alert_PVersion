@echo off
setlocal

cd /d "%~dp0"

echo [1/2] Configuring Red Alert...
cmake -S . -B build -A x64
if errorlevel 1 goto :error

echo [2/2] Building Red Alert...
cmake --build build --config Release
if errorlevel 1 goto :error

echo.
echo Build finished.
echo Expected executable: build\Release\RedAlert.exe
exit /b 0

:error
echo.
echo Build failed. Copy the first compiler error above and send it back.
exit /b 1
