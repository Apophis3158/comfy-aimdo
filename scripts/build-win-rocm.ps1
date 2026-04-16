# comfy-aimdo Windows ROCm local build script
# Requires: Visual Studio, Git, and ROCm SDK
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "git not found. Please install Git and ensure it is on PATH." }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found. Is Visual Studio installed?" }

# ── ROCm ───────────────────────────────────────────────────────────────────────
$ROCmCore = if ($env:VIRTUAL_ENV -and (Test-Path "$env:VIRTUAL_ENV\Lib\site-packages\_rocm_sdk_core")) {
    "$env:VIRTUAL_ENV\Lib\site-packages\_rocm_sdk_core"
} elseif ($env:HIP_PATH) { $env:HIP_PATH } elseif ($env:ROCM_PATH) { $env:ROCM_PATH }
if (-not $ROCmCore -or -not (Test-Path "$ROCmCore\include\hip")) {
    throw "ROCm not found. Set HIP_PATH/ROCM_PATH or activate a venv with _rocm_sdk_core."
}
$clang = "$ROCmCore\lib\llvm\bin\clang.exe"
if (-not (Test-Path $clang)) { throw "clang.exe not found at: $clang" }

Write-Host "Using ROCm SDK: $ROCmCore`n"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -Path $ProjectRoot

# ── Detours ────────────────────────────────────────────────────────────────────
$DetoursDir = "$ProjectRoot\Detours"
if (Test-Path $DetoursDir) {
    git -C $DetoursDir fetch -q --depth 1 origin main
    git -C $DetoursDir reset -q --hard FETCH_HEAD
} else {
    git clone -q --depth 1 https://github.com/microsoft/Detours.git $DetoursDir
}

$vcvars = "$(& $vswhere -latest -property installationPath)\VC\Auxiliary\Build\vcvars64.bat"
cmd.exe /c "call `"$vcvars`" && cd /d `"$DetoursDir\src`" && nmake"
if ($LASTEXITCODE -ne 0) { throw "Detours build failed (vcvars or nmake error)" }

Write-Host "Build successful: Detours`n"

# ── Compile ────────────────────────────────────────────────────────────────────
# use pure command to build: pwsh will handle *.c expanding and \ slash
& $clang src/*.c src-win/*.c --target=x86_64-pc-windows-msvc `
    -shared -O3 -D__HIP_PLATFORM_AMD__ `
    -I"$ROCmCore/include" -I"$DetoursDir/include" -Isrc `
    -L"$ROCmCore/lib" -L"$DetoursDir/lib.X64" `
    -lamdhip64 -ldxgi -ldxguid -ldetours -lonecore `
    -o"comfy_aimdo/aimdo_rocm.dll"
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit code $LASTEXITCODE)" }

Write-Host "Build successful: comfy_aimdo\aimdo_rocm.dll"
