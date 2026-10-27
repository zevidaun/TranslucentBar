# SPDX-License-Identifier: GPL-3.0-or-later
param([switch]$ReiniciarExplorer,[switch]$ProbarTemas,[switch]$DejarAbierta)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot)
if (!(Test-Path build/inspect.exe)) { throw 'Ejecuta antes .\test.ps1.' }
if (Get-Process TranslucentBar -ErrorAction SilentlyContinue) { throw 'Cierra TranslucentBar antes de esta prueba.' }
Add-Type -AssemblyName System.Drawing
function App([string]$Arguments) {
    $p = Start-Process .\out\TranslucentBar.exe -ArgumentList $Arguments -PassThru -WindowStyle Hidden
    if ($Arguments -notlike '--modo *' -or (Get-Process TranslucentBar).Count -gt 1) {
        if (!$p.WaitForExit(5000)) { throw "No termina el comando $Arguments" }
        if ($p.ExitCode) { throw "Falla el comando $Arguments ($($p.ExitCode))" }
    }
}
function State {
    $map = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting('Local\TranslucentBar.Config.v1')
    $view = $map.CreateViewAccessor()
    try { @{pid=$view.ReadInt32(0);modo=$view.ReadInt32(4);listo=$view.ReadInt32(16);elementos=$view.ReadInt32(20);aplicados=$view.ReadInt32(24);error=$view.ReadInt32(28);cambios=$view.ReadInt32(32);restaurados=$view.ReadInt32(1064);errores_restauracion=$view.ReadInt32(1068)} }
    finally {$view.Dispose();$map.Dispose()}
}
function WaitEffect {
    # Sondeo acotado SOLO en la prueba externa, nunca en la aplicación.
    for ($attempt=0;$attempt -lt 100;$attempt++) {
        try {$s=State;if ($s.listo -and $s.aplicados -gt 0 -and !$s.error) {return $s}} catch {}
        Start-Sleep -Milliseconds 100
    }
    throw 'El puente no aplica ningún fondo en 10 segundos.'
}
function Capture([string]$Name) {
    Start-Process .\build\inspect.exe -ArgumentList '--ordenar' -Wait -WindowStyle Hidden
    Start-Sleep -Milliseconds 800
    $p=Start-Process .\build\inspect.exe -ArgumentList "build\$Name" -PassThru -Wait -WindowStyle Hidden
    if ($p.ExitCode) {throw "Fallo al capturar $Name"}
}
function Difference([string]$First,[string]$Second,[string]$Screen) {
    $a=[Drawing.Bitmap]::new((Join-Path $PWD "build\$First-$Screen.bmp"))
    $b=[Drawing.Bitmap]::new((Join-Path $PWD "build\$Second-$Screen.bmp"))
    try {
        if ($a.Size -ne $b.Size) { throw 'Ha cambiado la geometría durante la comparación.' }
        [double]$sum=0;[int]$count=0
        for($x=10;$x -lt 110;$x+=2) {for($y=5;$y -lt ($a.Height-5);$y+=2) {
            $aa=$a.GetPixel($x,$y);$bb=$b.GetPixel($x,$y)
            $sum += [Math]::Abs([int]$aa.R-[int]$bb.R)+[Math]::Abs([int]$aa.G-[int]$bb.G)+[Math]::Abs([int]$aa.B-[int]$bb.B);$count+=3
        }}
        [Math]::Round($sum/$count,4)
    } finally {$a.Dispose();$b.Dispose()}
}
$themePath='HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize'
$originalTheme=(Get-ItemProperty $themePath).SystemUsesLightTheme
$results=[ordered]@{fecha=(Get-Date).ToString('yyyy-MM-dd');pantallas=@();reinicio_explorer=$null;temas=@();cierre_forzado=$null}
try {
    Start-Process .\build\inspect.exe -ArgumentList '--fondo' -WindowStyle Hidden | Out-Null
    Capture 'antes'
    Start-Process .\out\TranslucentBar.exe -ArgumentList '--modo translucido' -WindowStyle Hidden | Out-Null
    $initial=WaitEffect
    Capture 'translucido'
    App '--modo glass'; $null=WaitEffect;Capture 'glass'
    App '--modo normal';Capture 'normal'
    $restored=State
    if(!$restored.restaurados -or $restored.errores_restauracion) {throw 'No se recupera la identidad de los pinceles originales.'}
    $results.identidad_pinceles_restaurados=$restored.restaurados
    foreach($screen in @('principal','secundaria')) {
        if(!(Test-Path "build\antes-$screen.bmp")) {continue}
        $distinct=Difference 'translucido' 'glass' $screen
        $restore=Difference 'antes' 'normal' $screen
        if($distinct -lt 3) {throw "No se distingue el efecto en $screen (diferencia $distinct)."}
        $results.pantallas+=@{barra=$screen;diferencia_translucido_glass=$distinct;variacion_primer_refresco_backdrop=$restore;identidad_original_restaurada=$true}
    }
    App '--modo translucido';$null=WaitEffect
    if($ProbarTemas) {
        foreach($theme in @(1,0)) {
            Set-ItemProperty $themePath SystemUsesLightTheme $theme
            Start-Process .\build\inspect.exe -ArgumentList '--avisar-tema' -Wait -WindowStyle Hidden
            $s=WaitEffect;Capture "tema-$theme"
            $results.temas+=@{system_uses_light_theme=$theme;fondos_aplicados=$s.aplicados;error=$s.error}
        }
    }
    if($ReiniciarExplorer) {
        Stop-Process -Name explorer
        Start-Sleep -Milliseconds 500
        Start-Process explorer.exe -WindowStyle Hidden
        Start-Sleep -Seconds 2
        $s=WaitEffect;Capture 'reinicio'
        if($s.pid -ne $initial.pid) {throw 'El controlador no ha sobrevivido al reinicio.'}
        $results.reinicio_explorer=@{mismo_controlador=$true;fondos_aplicados=$s.aplicados;error=$s.error}
    }
    App '--modo normal';Capture 'antes-salida'
    App '--modo translucido';$null=WaitEffect
    App '--salir';Capture 'salida'
    foreach($screen in @('principal','secundaria')) {
        if(Test-Path "build\salida-$screen.bmp") {
            $diff=Difference 'antes-salida' 'salida' $screen
            if($diff -gt 1) {throw "La salida no restaura la barra $screen (error $diff)."}
        }
    }
    Start-Process .\out\TranslucentBar.exe -ArgumentList '--modo glass' -WindowStyle Hidden | Out-Null
    $s=WaitEffect;Stop-Process -Id $s.pid;Capture 'forzada'
    foreach($screen in @('principal','secundaria')) {
        if(Test-Path "build\forzada-$screen.bmp") {
            $diff=Difference 'antes-salida' 'forzada' $screen
            if($diff -gt 1) {throw "El cierre forzado no restaura $screen (error $diff)."}
        }
    }
    $results.cierre_forzado='Restauración comprobada por píxeles en ambas barras.'
    $results|ConvertTo-Json -Depth 5|Set-Content build/integracion.json -Encoding utf8
    $results|ConvertTo-Json -Depth 5
} finally {
    if($ProbarTemas) {
        if($null -ne $originalTheme) {Set-ItemProperty $themePath SystemUsesLightTheme $originalTheme}
        else {Remove-ItemProperty $themePath SystemUsesLightTheme -ErrorAction SilentlyContinue}
        Start-Process .\build\inspect.exe -ArgumentList '--avisar-tema' -Wait -WindowStyle Hidden
    }
    if(Get-Process TranslucentBar -ErrorAction SilentlyContinue) {App '--salir'}
    Start-Process .\build\inspect.exe -ArgumentList '--cerrar' -Wait -WindowStyle Hidden
}
if($DejarAbierta) {Start-Process .\out\TranslucentBar.exe -ArgumentList '--modo translucido' -WindowStyle Hidden}
