@echo off
setlocal EnableExtensions
title Highlander THESEUS PC - AUTO
cd /d "%~dp0"

set "CACHE=%~dp0.HIGHLANDER_THESEUS_AUTO"
set "BASE=https://raw.githubusercontent.com/valeriojpa-lgtm/asuras-wrath-recomp/highlander-automation/highlander/automation"
if not exist "%CACHE%" mkdir "%CACHE%"
if not exist "%CACHE%\BRIDGE_BIN" mkdir "%CACHE%\BRIDGE_BIN"

echo ============================================================
echo Highlander THESEUS PC - AUTO
echo Un launcher. El experimento se actualiza desde GitHub.
echo Las builds originales NO se modifican.
echo ============================================================
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
 "$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; " ^
 "$base='%BASE%'; $cache='%CACHE%'; " ^
 "$items=@(@('runner/highlander_auto.ps1','highlander_auto.ps1'),@('experiment.json','experiment.json'),@('BRIDGE_BIN/nvtt.dll.b64','nvtt.dll.b64'),@('BRIDGE_BIN/PhysXExtensions.dll.b64','PhysXExtensions.dll.b64')); " ^
 "$downloadOK=$true; foreach($i in $items){ try{ Invoke-WebRequest -UseBasicParsing -Uri ($base+'/'+$i[0]) -OutFile (Join-Path $cache $i[1]); } catch { $downloadOK=$false; Write-Host ('WARN GitHub: '+$_.Exception.Message) -ForegroundColor Yellow } }; " ^
 "foreach($n in @('nvtt','PhysXExtensions')){ $b64=Join-Path $cache ($n+'.dll.b64'); $dll=Join-Path (Join-Path $cache 'BRIDGE_BIN') ($n+'.dll'); if(Test-Path $b64){ $s=(Get-Content -Raw $b64) -replace '\s',''; [IO.File]::WriteAllBytes($dll,[Convert]::FromBase64String($s)); } }; " ^
 "if(-not(Test-Path (Join-Path $cache 'highlander_auto.ps1')) -or -not(Test-Path (Join-Path $cache 'experiment.json'))){ throw 'No hay cache util y GitHub no pudo descargarse.' }; " ^
 "if($downloadOK){Write-Host 'AUTO actualizado desde GitHub.' -ForegroundColor Green}else{Write-Host 'Usando cache local disponible.' -ForegroundColor Yellow}"

if errorlevel 1 (
  echo.
  echo ERROR preparando AUTO.
  pause
  exit /b 10
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CACHE%\highlander_auto.ps1" -LauncherRoot "%~dp0" -ManifestPath "%CACHE%\experiment.json" -BridgeDir "%CACHE%\BRIDGE_BIN"
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (
  echo AUTO finalizado correctamente.
  echo El Explorador deberia mostrarte el unico ZIP que debes subir.
) else (
  echo AUTO termino con codigo %RC%.
  echo Revisa HIGHLANDER_AUTO_RESULTS.
)
echo.
pause
exit /b %RC%
