# SPDX-License-Identifier: GPL-3.0-or-later
param([string]$Toolchain)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (!$Toolchain) {
    $compiler = @(Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT*\*\bin\x86_64-w64-mingw32-clang++.exe" -ErrorAction SilentlyContinue)
    if (!$compiler.Count) { throw 'No se encuentra LLVM-MinGW; usa -Toolchain con su carpeta bin.' }
    $Toolchain = $compiler[-1].Directory.FullName
}
New-Item -ItemType Directory build -Force | Out-Null
$cxx = Join-Path $Toolchain 'x86_64-w64-mingw32-clang++.exe'
& $cxx -std=c++20 -Os -static -DUNICODE -D_UNICODE -Wall -Wextra -Werror tests/config.cpp -o build/test-config.exe -luser32 -lgdi32 -lshell32 -ladvapi32
if ($LASTEXITCODE) { throw 'Fallo al compilar las pruebas de configuración.' }
& .\build\test-config.exe
if ($LASTEXITCODE) { throw 'Las pruebas han fallado.' }
& $cxx -std=c++20 -Os -static -municode -mwindows -Wall -Wextra -Werror tests/inspect.cpp -o build/inspect.exe -luser32 -lgdi32 -lshell32
if ($LASTEXITCODE) { throw 'Fallo al compilar la sonda visual.' }
