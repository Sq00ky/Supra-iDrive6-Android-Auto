@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-6wb.ps1" %*
exit /b %errorlevel%
