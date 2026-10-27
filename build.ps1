# SPDX-License-Identifier: GPL-3.0-or-later
param([string]$Toolchain)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (!$Toolchain) {
    $candidates = @(Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT*\*\bin\x86_64-w64-mingw32-clang++.exe" -ErrorAction SilentlyContinue)
    if ($candidates.Count) { $Toolchain = $candidates[-1].Directory.FullName }
    elseif (Get-Command x86_64-w64-mingw32-clang++.exe -ErrorAction SilentlyContinue) { $Toolchain = Split-Path (Get-Command x86_64-w64-mingw32-clang++.exe).Source }
    else { throw 'Instala LLVM-MinGW UCRT con winget (consulta README) o pasa -Toolchain con su carpeta bin.' }
}
New-Item -ItemType Directory build,out -Force | Out-Null
$cxx = Join-Path $Toolchain 'x86_64-w64-mingw32-clang++.exe'
$rc = Join-Path $Toolchain 'x86_64-w64-mingw32-windres.exe'
$flags = @('-std=c++20','-Os','-flto','-static','-fno-exceptions','-fno-rtti','-ffunction-sections','-fdata-sections','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00','-Wall','-Wextra','-Werror','-Wl,--gc-sections','-Wl,--no-insert-timestamp','-s')
& $cxx @flags -shared src/bridge.cpp src/bridge.def -o build/bridge.dll -lole32 -loleaut32 -luuid -lruntimeobject -luser32
if ($LASTEXITCODE) { throw 'Fallo al compilar el puente XAML.' }
@'
#include <windows.h>
1 RT_MANIFEST "../app.manifest"
101 RCDATA "bridge.dll"
'@ | Set-Content build/app.rc -Encoding ascii
& $rc -I build build/app.rc -o build/app.o
if ($LASTEXITCODE) { throw 'Fallo al compilar los recursos.' }
& $cxx @flags -municode -mwindows src/main.cpp build/app.o -o out/TranslucentBar.exe -luser32 -lgdi32 -lshell32 -ladvapi32
if ($LASTEXITCODE) { throw 'Fallo al compilar TranslucentBar.' }
Get-Item out/TranslucentBar.exe | Select-Object FullName,Length
