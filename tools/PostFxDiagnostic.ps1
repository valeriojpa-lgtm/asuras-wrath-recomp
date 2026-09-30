$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root "Asura's Wrath.exe"
$diagDir = Join-Path $root "UserData\Diagnostics"
$report = Join-Path $diagDir "PostFX_Diagnostic.txt"

if (!(Test-Path $exe)) {
    Write-Host ""
    Write-Host "No encuentro Asura's Wrath.exe junto a este diagnostico." -ForegroundColor Red
    Write-Host "Coloca este paquete junto a la carpeta Data del juego y vuelve a ejecutar."
    Read-Host "Pulsa ENTER para salir"
    exit 2
}

New-Item -ItemType Directory -Force -Path $diagDir | Out-Null

@(
    "ASURA'S WRATH - THESEUS POSTFX AUTO DIAGNOSTIC"
    "================================================"
    "Fecha: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
    "Objetivo: aislar la combinacion minima de 5 parches que elimina la corrupcion verde al escalar."
    "Resolucion interna durante las pruebas: 1440p / scale 2 (forzada automaticamente)."
    ""
    "Sitios:"
    "  Bit 1  = 0x8273F610"
    "  Bit 2  = 0x8273F76C"
    "  Bit 4  = 0x8273F8D8"
    "  Bit 8  = 0x82741ED4"
    "  Bit 16 = 0x82741EF0"
    ""
) | Set-Content -Encoding UTF8 $report

$results = @{}

function Ask-Clean {
    param(
        [int]$Mask,
        [string]$Label
    )

    if ($results.ContainsKey($Mask)) {
        return [bool]$results[$Mask]
    }

    Clear-Host
    Write-Host "ASURA'S WRATH - DIAGNOSTICO POSTFX" -ForegroundColor Cyan
    Write-Host "==================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Prueba: $Label"
    Write-Host "Mascara: $Mask"
    Write-Host ""
    Write-Host "No cambies ninguna opcion del launcher."
    Write-Host "Theseus fuerza 1440p interno para esta prueba."
    Write-Host ""
    Write-Host "1) Espera a llegar a la pantalla donde aparece la plaga verde."
    Write-Host "2) Mira si la corrupcion verde/negra HA DESAPARECIDO."
    Write-Host "3) Cierra el juego normalmente."
    Write-Host ""

    $env:THESEUS_POSTFX_MASK = "$Mask"
    $p = Start-Process -FilePath $exe -ArgumentList "--no_launcher" -PassThru -Wait
    $exitCode = $p.ExitCode

    do {
        $answer = (Read-Host "¿Ha desaparecido la plaga verde? [S/N]").Trim().ToUpperInvariant()
    } while ($answer -ne "S" -and $answer -ne "N")

    $clean = $answer -eq "S"
    $results[$Mask] = $clean

    "Mask=$Mask Label=$Label Clean=$clean ExitCode=$exitCode" |
        Add-Content -Encoding UTF8 $report

    return $clean
}

function Finish-Diagnostic {
    param(
        [string]$Conclusion,
        [int]$SuggestedMask = -1
    )

    Remove-Item Env:THESEUS_POSTFX_MASK -ErrorAction SilentlyContinue

    "" | Add-Content -Encoding UTF8 $report
    "Conclusion: $Conclusion" | Add-Content -Encoding UTF8 $report
    if ($SuggestedMask -ge 0) {
        "SuggestedMask: $SuggestedMask" | Add-Content -Encoding UTF8 $report
    }

    Clear-Host
    Write-Host "DIAGNOSTICO TERMINADO" -ForegroundColor Green
    Write-Host "====================="
    Write-Host ""
    Write-Host $Conclusion
    if ($SuggestedMask -ge 0) {
        Write-Host "Mascara candidata: $SuggestedMask"
    }
    Write-Host ""
    Write-Host "Informe:"
    Write-Host $report -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Subeme solamente PostFX_Diagnostic.txt y continuo desde ahi."
    Read-Host "Pulsa ENTER para cerrar"
    exit 0
}

# Baseline mask 0 is already known to be bad from the user's previous tests,
# so we intentionally skip it to minimize human work.
$full = Ask-Clean 31 "Todas las correcciones (5/5)"
if (-not $full) {
    Finish-Diagnostic "Ni siquiera el bypass completo elimina la corrupcion. El culpable esta fuera de estas cinco instrucciones."
}

$groupA = Ask-Clean 7 "Bloque A (sitios 1, 2 y 3)"
if ($groupA) {
    foreach ($mask in @(1, 2, 4)) {
        if (Ask-Clean $mask "Sitio individual mask $mask") {
            Finish-Diagnostic "Un unico sitio es suficiente para eliminar la corrupcion." $mask
        }
    }

    foreach ($mask in @(3, 5, 6)) {
        if (Ask-Clean $mask "Pareja dentro del Bloque A mask $mask") {
            Finish-Diagnostic "Una pareja del Bloque A elimina la corrupcion." $mask
        }
    }

    Finish-Diagnostic "El Bloque A completo es necesario en esta pasada." 7
}

$groupB = Ask-Clean 24 "Bloque B (sitios 4 y 5)"
if ($groupB) {
    foreach ($mask in @(8, 16)) {
        if (Ask-Clean $mask "Sitio individual mask $mask") {
            Finish-Diagnostic "Un unico sitio del Bloque B elimina la corrupcion." $mask
        }
    }
    Finish-Diagnostic "Los dos sitios del Bloque B son necesarios juntos." 24
}

# Neither half works alone, so reduce greedily from the full known-good mask.
$candidate = 31
foreach ($bit in @(1, 2, 4, 8, 16)) {
    $trial = $candidate -band (-bnot $bit)
    if ($trial -eq 0) {
        continue
    }
    if (Ask-Clean $trial "Reduccion automatica: probar sin bit $bit") {
        $candidate = $trial
    }
}

Finish-Diagnostic "La corrupcion requiere una combinacion entre ambos bloques. Esta es la mascara minima encontrada por reduccion automatica." $candidate
