$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $PSScriptRoot
$DetoursDir = Join-Path $RootDir "Detours"
$DetoursLibDir = Join-Path $DetoursDir "lib.X64"
$BuildDir = Join-Path $RootDir "build"

$VsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
. "$VsPath\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64

if (-not (Test-Path $DetoursDir)) {
    git clone --depth 1 https://github.com/microsoft/Detours.git $DetoursDir
}

if (-not (Test-Path "$DetoursLibDir\detours.lib")) {
    Set-Location "$DetoursDir\src"
    nmake
    if ($LASTEXITCODE -ne 0) { Write-Host -ForegroundColor Red "Failed to build Detours"; exit $LASTEXITCODE }
}

Set-Location $RootDir
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

cl.exe /LD /O2 `
    src/*.c src-cuda/dispatch.c src-win/*.c `
    /Isrc /Isrc-win /I"$DetoursDir\include" /FIcompiler.h /Fo"$BuildDir\" /Fe:comfy_aimdo\aimdo.dll `
    /link /LIBPATH:"$DetoursLibDir" /IMPLIB:"$BuildDir\aimdo.lib" `
    dxgi.lib dxguid.lib detours.lib onecore.lib
if ($LASTEXITCODE -ne 0) { Write-Host -ForegroundColor Red "Failed to build aimdo.dll"; exit $LASTEXITCODE }

cl.exe /LD /O2 /D__HIP_PLATFORM_AMD__ `
    src/*.c src-hip/dispatch.c src-win/*.c `
    /Isrc /Isrc-win /I"$DetoursDir\include" /FIcompiler.h /Fo"$BuildDir\" /Fe:comfy_aimdo\aimdo_rocm.dll `
    /link /LIBPATH:"$DetoursLibDir" /IMPLIB:"$BuildDir\aimdo_rocm.lib" `
    dxgi.lib dxguid.lib detours.lib onecore.lib
if ($LASTEXITCODE -ne 0) { Write-Host -ForegroundColor Red "Failed to build aimdo_rocm.dll"; exit $LASTEXITCODE }

Write-Host -ForegroundColor Green "aimdo.dll and aimdo_rocm.dll built successfully"
