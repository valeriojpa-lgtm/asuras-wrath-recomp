@echo off
setlocal
cd /d "%~dp0\.."
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=Data\DLC"
python tools\dlc_inspect_stfs.py "%TARGET%" --json DLC_REPORT.json --csv DLC_REPORT.csv
echo.
echo Reportes: DLC_REPORT.json y DLC_REPORT.csv
pause
