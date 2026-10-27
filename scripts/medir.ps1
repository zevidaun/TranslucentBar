# SPDX-License-Identifier: GPL-3.0-or-later
param(
    [ValidateRange(1,60)][int]$Segundos = 30,
    [string]$Salida
)
$ErrorActionPreference = 'Stop'
$processes = @(Get-Process TranslucentBar -ErrorAction Stop)
if ($processes.Count -ne 1) { throw 'Espera a que termine el auxiliar de conexión XAML; debe quedar un solo proceso.' }
$before = $processes[0]
$processId = $before.Id
$cpuBefore = $before.CPU
$workingBefore = $before.WorkingSet64
$privateBefore = $before.PrivateMemorySize64
$clock = [Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Seconds $Segundos
$after = Get-Process -Id $processId -ErrorAction Stop
$elapsed = $clock.Elapsed.TotalSeconds
$delta = $after.CPU - $cpuBefore
$os = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$measurement = [ordered]@{
    fecha = (Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz')
    windows = "$($os.DisplayVersion) $($os.CurrentBuild).$($os.UBR)"
    intervalo_segundos = [math]::Round($elapsed,3)
    procesadores_logicos = [Environment]::ProcessorCount
    cpu_segundos = $delta
    cpu_porcentaje_un_nucleo = [math]::Round(100*$delta/$elapsed,6)
    cpu_porcentaje_equipo = [math]::Round(100*$delta/$elapsed/[Environment]::ProcessorCount,6)
    working_set_inicial_bytes = $workingBefore
    working_set_final_bytes = $after.WorkingSet64
    memoria_privada_inicial_bytes = $privateBefore
    memoria_privada_final_bytes = $after.PrivateMemorySize64
    objetivo_cpu_cero_cumplido_en_muestra = ($delta -eq 0)
    objetivo_working_set_5_MB_cumplido = ($after.WorkingSet64 -lt 5000000)
    alcance = 'Proceso residente. Incluye DLL compartidas en WorkingSet64. No atribuye el coste del puente y del compositor dentro de Explorer/DWM.'
}
$json = $measurement | ConvertTo-Json
if ($Salida) { $json | Set-Content -LiteralPath $Salida -Encoding utf8 }
$json
