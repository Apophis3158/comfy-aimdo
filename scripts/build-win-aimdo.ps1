$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $PSScriptRoot
Set-Location $RootDir

$BuildDir = Join-Path $RootDir "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$DetoursDir = Join-Path $RootDir "Detours"
$Arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture
$DetoursLibDir = Join-Path $DetoursDir "lib.$Arch"

$VsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
. "$VsPath\Common7\Tools\Launch-VsDevShell.ps1" -Arch $Arch

if (-not (Test-Path $DetoursDir)) {
    git clone --depth 1 https://github.com/microsoft/Detours.git $DetoursDir
}

if (-not (Test-Path "$DetoursLibDir\detours.lib")) {
    Push-Location "$DetoursDir\src" # do not build Detours\samples
    try {
        nmake
        if ($LASTEXITCODE -ne 0) { Write-Host -ForegroundColor Red "Failed to build Detours($Arch)"; exit $LASTEXITCODE }
    } finally {
        Pop-Location
    }
}

cl.exe /LD /O2 `
    src/*.c src-cuda/dispatch.c src-win/*.c `
    /Isrc /Isrc-win /I"$DetoursDir\include" /FIcompiler.h /Fo"$BuildDir\" /Fe:comfy_aimdo\aimdo.dll `
    /link /LIBPATH:"$DetoursLibDir" /IMPLIB:"$BuildDir\aimdo.lib" `
    dxgi.lib dxguid.lib detours.lib onecore.lib
if ($LASTEXITCODE -ne 0) { Write-Host -ForegroundColor Red "Failed to build aimdo.dll($Arch)"; exit $LASTEXITCODE }

cl.exe /LD /O2 /D__HIP_PLATFORM_AMD__ `
    src/*.c src-hip/dispatch.c src-win/*.c `
    /Isrc /Isrc-win /I"$DetoursDir\include" /FIcompiler.h /Fo"$BuildDir\" /Fe:comfy_aimdo\aimdo_rocm.dll `
    /link /LIBPATH:"$DetoursLibDir" /IMPLIB:"$BuildDir\aimdo_rocm.lib" `
    dxgi.lib dxguid.lib detours.lib onecore.lib psapi.lib
if ($LASTEXITCODE -ne 0) { Write-Host -ForegroundColor Red "Failed to build aimdo_rocm.dll($Arch)"; exit $LASTEXITCODE }

Write-Host -ForegroundColor Green "`naimdo($Arch) built successfully"
