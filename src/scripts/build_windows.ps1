# Build the Mesmer X-Plane 12 plugin for Windows (x64), the PowerShell
# counterpart of src/scripts/build_macos.sh. Runs in Windows PowerShell 5.1 and
# PowerShell 7.
#
#   src/scripts/build_windows.ps1 [-Sdk PATH] [-Clean] [-NoTest] [-Help]
#
# Output: build/stage/BrunnerDirect/win_x64/BrunnerDirect.xpl (plus the files
# the release ships), laid out as .github/workflows/release.yml packages it.
[CmdletBinding()]
param(
    [string]$Sdk = $env:XPLANE_SDK,
    [switch]$Clean,
    [switch]$NoTest,
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue' # Invoke-WebRequest is many times slower with the progress bar

# The X-Plane SDK pin. Keep it equal to XPLANE_SDK_URL / XPLANE_SDK_SHA256 in
# .github/workflows/release.yml and ci.yml.
$SdkUrl = if ($env:MESMER_SDK_URL) { $env:MESMER_SDK_URL } else { 'https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK430.zip' }
$SdkSha256 = if ($env:MESMER_SDK_SHA256) { $env:MESMER_SDK_SHA256 } else { 'b9875ab27b593927b4f9b3e0ddfffe7401ee5dce6d86b50aea0da65f70ff7816' }

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path # this script is src/scripts/build_windows.ps1
$Start = (Get-Location).Path
$Build = Join-Path $Root 'build'

function Die([string]$message) {
    [Console]::Error.WriteLine("build_windows.ps1: $message")
    exit 1
}

function Invoke-Checked([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { Die "$([IO.Path]::GetFileName($exe)) failed (exit code $LASTEXITCODE)" }
}

if ($Help) {
    @"
Build the Mesmer X-Plane 12 plugin for Windows (x64).

  src/scripts/build_windows.ps1 [-Sdk PATH] [-Clean] [-NoTest] [-Help]

  -Sdk PATH   unpacked X-Plane SDK directory (the one holding CHeaders\); overrides `$env:XPLANE_SDK
  -Clean      remove build\ first (a downloaded SDK in build\sdk is re-downloaded)
  -NoTest     skip ctest
  -Help       this text

With no -Sdk and no `$env:XPLANE_SDK, the pinned SDK is downloaded into build\sdk
and checked against its SHA-256 before it is unpacked.

Needs Visual Studio 2022 (or later) with the "Desktop development with C++"
workload. CMake 3.20+ is taken from PATH, or else from the copy Visual Studio
installs.

Output: build\stage\BrunnerDirect\win_x64\BrunnerDirect.xpl
"@
    exit 0
}

if ($env:OS -ne 'Windows_NT') { Die 'this script builds for Windows and must run on Windows' }

# A relative -Sdk or $env:XPLANE_SDK means relative to where it was run from.
if ($Sdk -and -not [IO.Path]::IsPathRooted($Sdk)) { $Sdk = Join-Path $Start $Sdk }
if ($Start -eq $Root) { }
elseif ($Start -eq (Join-Path $Root 'src')) { Write-Host "Run from src\; building from the repository root: $Root" }
elseif ($Start -eq (Join-Path $Root 'src\scripts')) { Write-Host "Run from src\scripts\; building from the repository root: $Root" }
else { Write-Host "Building from the repository root: $Root" }

# --- Tools: Visual Studio's C++ compiler, and CMake (PATH first, then Visual Studio's copy).
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { Die 'Visual Studio not found (install Visual Studio 2022 with "Desktop development with C++")' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Die 'Visual Studio has no x64 C++ tools (add the "Desktop development with C++" workload)' }

$cmake = $null
$onPath = Get-Command cmake -ErrorAction SilentlyContinue
if ($onPath) { $cmake = $onPath.Source }
else {
    $bundled = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $bundled) { $cmake = $bundled }
}
if (-not $cmake) { Die 'cmake not found (winget install Kitware.CMake, or add "C++ CMake tools for Windows" in the Visual Studio Installer)' }
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest)) { Die "ctest not found next to $cmake" }
Write-Host "Using CMake: $cmake"

if ($Clean -and (Test-Path -LiteralPath $Build)) { Remove-Item -LiteralPath $Build -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Build | Out-Null

# --- The X-Plane SDK.
function Get-PinnedSdk {
    $dir = Join-Path $Build 'sdk'
    $zip = Join-Path $dir 'xplane-sdk.zip'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    if (-not (Test-Path -LiteralPath $zip)) {
        Write-Host "Downloading the X-Plane SDK: $SdkUrl"
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        try { Invoke-WebRequest -Uri $SdkUrl -OutFile $zip -UseBasicParsing }
        catch {
            Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
            Die "could not download the X-Plane SDK ($($_.Exception.Message)); pass -Sdk PATH or set XPLANE_SDK"
        }
    }
    $got = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($got -ne $SdkSha256) {
        Remove-Item -LiteralPath $zip -Force
        Die "X-Plane SDK checksum $got does not match the pin $SdkSha256"
    }
    $unpacked = Join-Path $dir 'unpacked'
    if (Test-Path -LiteralPath $unpacked) { Remove-Item -LiteralPath $unpacked -Recurse -Force }
    try { Expand-Archive -LiteralPath $zip -DestinationPath $unpacked }
    catch { Die "could not unpack the X-Plane SDK ($($_.Exception.Message))" }
    return (Join-Path $unpacked 'SDK')
}

if (-not $Sdk) { $Sdk = Get-PinnedSdk }
if (-not (Test-Path -LiteralPath (Join-Path $Sdk 'CHeaders\XPLM\XPLMPlugin.h'))) { Die "not an X-Plane SDK directory (no CHeaders\XPLM\XPLMPlugin.h): $Sdk" }
if (-not (Test-Path -LiteralPath (Join-Path $Sdk 'Libraries\Win\XPLM_64.lib'))) { Die "the SDK has no Libraries\Win\XPLM_64.lib: $Sdk" }
$Sdk = (Resolve-Path -LiteralPath $Sdk).Path
$sdkForCmake = $Sdk -replace '\\', '/'

# --- Configure, build, test.
Write-Host "Configuring (x64, SDK $Sdk)"
Invoke-Checked $cmake @('-S', (Join-Path $Root 'src'), '-B', $Build, '-A', 'x64', "-DXPLANE_SDK=$sdkForCmake")
# --clean-first: MSBuild has been seen to skip rebuilding after source changes (CONTRIBUTING.md).
Invoke-Checked $cmake @('--build', $Build, '--config', 'Release', '--clean-first', '--parallel')

$xpl = Join-Path $Build 'Release\BrunnerDirect.xpl'
if (-not (Test-Path -LiteralPath $xpl)) { Die "the build did not produce $xpl" }

# X-Plane 12 loads only 64-bit plugins: check the PE header's machine field is x64 (0x8664).
$bytes = [IO.File]::ReadAllBytes($xpl)
$pe = [BitConverter]::ToInt32($bytes, 0x3C)
$machine = [BitConverter]::ToUInt16($bytes, $pe + 4)
if ($machine -ne 0x8664) { Die ('BrunnerDirect.xpl is not an x64 binary (machine 0x{0:X4})' -f $machine) }

if (-not $NoTest) {
    Invoke-Checked $ctest @('--test-dir', $Build, '-C', 'Release', '--output-on-failure')
}

# --- Stage the plugin folder the way the release zip lays it out.
$stageRoot = Join-Path $Build 'stage'
$stage = Join-Path $stageRoot 'BrunnerDirect'
if (Test-Path -LiteralPath $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'win_x64') | Out-Null
Copy-Item -LiteralPath $xpl -Destination (Join-Path $stage 'win_x64')
foreach ($file in 'config\BrunnerDirect.ini', 'config\BrunnerDirect.profiles.json', 'config\skunkcrafts_updater.cfg', 'README.md', 'LICENSE', 'NOTICE', 'SAFETY.md') {
    Copy-Item -LiteralPath (Join-Path $Root $file) -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $Root 'config\yokes') -Destination (Join-Path $stage 'yokes') -Recurse

Write-Host "Built the x64 plugin: $(Join-Path $stage 'win_x64\BrunnerDirect.xpl')"
