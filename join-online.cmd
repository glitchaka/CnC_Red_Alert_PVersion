@echo off
setlocal
cd /d "%~dp0"

echo.
echo CLICK COMMAND - JOIN ONLINE
echo ===========================
set /p CC_HOST=IP o hostname del host: 
if "%CC_HOST%"=="" exit /b 1

echo.
echo Conectando a %CC_HOST%:46000
call build.cmd
