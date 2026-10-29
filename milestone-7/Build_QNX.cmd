@echo off
setlocal
cd /d "%~dp0"
where qcc >nul 2>nul
if errorlevel 1 (
  echo ERROR: qcc is not in PATH. Open the QNX SDP 6.5 configured command prompt first.
  exit /b 1
)
if not defined QNX_HOST (
  echo ERROR: QNX_HOST is not set. Use the QNX SDK command prompt.
  exit /b 1
)
if not defined QNX_TARGET (
  echo ERROR: QNX_TARGET is not set. Use the QNX SDK command prompt.
  exit /b 1
)
where make >nul 2>nul
if errorlevel 1 (
  echo ERROR: GNU make is not in PATH. Use the QNX SDK command prompt.
  exit /b 1
)
for %%F in (rhmiapp\lib\libAppFramework.A.so rhmiapp\lib\libCoreFramework.A.so) do (
  if not exist "%%F" (
    echo ERROR: Missing %%F
    exit /b 1
  )
)
echo Building the daemon and iDrive application for QNX ARMv7LE...
make -B nbt-aad rhmiapp
if errorlevel 1 exit /b 1
if not exist dist-warm-reconnect5\release mkdir dist-warm-reconnect5\release
if errorlevel 1 exit /b 1
copy /b /y nbt-aad dist-warm-reconnect5\nbt-aad >nul
if errorlevel 1 exit /b 1
for %%F in (libNbtAad.so NbtAad.manifest NbtAad.xml NbtAad_ALL_Texts.zip NbtAad_ALL_Images.zip) do (
  copy /b /y "rhmiapp\%%F" "dist-warm-reconnect5\release\%%F" >nul
  if errorlevel 1 exit /b 1
)
echo Build completed: v1.8.35-warm-reconnect5 and rhmiapp-18-warm-reconnect5
echo Output: %CD%\dist-warm-reconnect5
echo This script has NOT installed anything on the car. Read WARM_RECONNECT.md.
exit /b 0
