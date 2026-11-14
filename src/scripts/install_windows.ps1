# Install the plugin built by build_windows.ps1 (build/stage/BrunnerDirect) into X-Plane on
# this PC, the PowerShell counterpart of src/scripts/install_macos.sh. Runs in Windows
# PowerShell 5.1 and PowerShell 7.
#
#   src/scripts/install_windows.ps1 [-Version N] [-XPlaneDir PATH] [-Force] [-DryRun] [-Help]
#
# X-Plane's install folder is the first line of %LOCALAPPDATA%\x-plane_install_<N>.txt
# (N = X-Plane's major version; the newest one found unless -Version says otherwise).
[CmdletBinding()]
param(
    [string]$Version,
    [string]$XPlaneDir,
    [switch]$Force,
    [switch]$DryRun,
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path # the repository root
$Stage = Join-Path $Root 'build\stage\BrunnerDirect'

function Die([string]$message) {
    [Console]::Error.WriteLine("install_windows.ps1: $message")
    exit 1
}
function Say([string]$message) { Write-Host $message }

if ($Help) {
    @"
Install the plugin built by build_windows.ps1 (build\stage\BrunnerDirect) into X-Plane.

  src/scripts/install_windows.ps1 [-Version N] [-XPlaneDir PATH] [-Force] [-DryRun] [-Help]

  -Version N         X-Plane major version whose x-plane_install_N.txt names the folder
                     (default: the newest version that has such a file)
  -XPlaneDir PATH    the X-Plane folder itself (the one holding Resources\), instead of the file
  -Force             install even if X-Plane appears to be running (it will not see the plugin until restart)
  -DryRun            say what would happen and change nothing
  -Help              this text

X-Plane's install folder is the first line of %LOCALAPPDATA%\x-plane_install_<N>.txt.

An existing Resources\plugins\BrunnerDirect moves to <X-Plane>\Mesmer-backups\BrunnerDirect-<UTC time>
(X-Plane loads every folder in plugins\, so a backup cannot stay there). Your BrunnerDirect.ini,
BrunnerDirect.feel.ini and BrunnerDirect.yokes.json, and any other file the new plugin does not ship,
are carried into the new folder.
"@
    exit 0
}

if ($env:OS -ne 'Windows_NT') { Die 'this script installs on Windows and must run on Windows' }
if ($Version -and $Version -notmatch '^[0-9]+$') { Die '-Version must be a number such as 12' }

# ---- the staged plugin: what build_windows.ps1 produced ----
if (-not (Test-Path -LiteralPath $Stage -PathType Container)) { Die "no staged plugin at $Stage; run src/scripts/build_windows.ps1 first" }
foreach ($file in 'win_x64\BrunnerDirect.xpl', 'BrunnerDirect.ini', 'BrunnerDirect.profiles.json', 'yokes\cls-e.json', 'yokes\cls-e-ng.json') {
    if (-not (Test-Path -LiteralPath (Join-Path $Stage $file) -PathType Leaf)) { Die "the staged plugin has no $file; run src/scripts/build_windows.ps1 again" }
}

# ---- X-Plane's folder ----
# Either the folder the pilot named (-XPlaneDir), or the one X-Plane recorded; if neither is
# available the install stops and says how to name it. Never a guess.
if ($XPlaneDir) {
    $xplane = $XPlaneDir
    $sourceOf = '-XPlaneDir'
} else {
    $prefsDir = $env:LOCALAPPDATA
    if (-not $prefsDir) { Die '%LOCALAPPDATA% is not set; pass the X-Plane folder with -XPlaneDir PATH' }
    if (-not $Version) {
        # The newest X-Plane: the highest N among x-plane_install_N.txt (numeric, so 12 beats 9).
        $newest = -1
        foreach ($f in Get-ChildItem -LiteralPath $prefsDir -Filter 'x-plane_install_*.txt' -File -ErrorAction SilentlyContinue) {
            if ($f.Name -match '^x-plane_install_([0-9]+)\.txt$') {
                $n = [int]$Matches[1]
                if ($n -gt $newest) { $newest = $n }
            }
        }
        if ($newest -lt 0) { Die "no X-Plane install folder was found (no x-plane_install_N.txt in $prefsDir`: is X-Plane installed, and has it been run once?); pass the folder with -XPlaneDir PATH" }
        $Version = [string]$newest
    }
    $prefs = Join-Path $prefsDir "x-plane_install_$Version.txt"
    if (-not (Test-Path -LiteralPath $prefs -PathType Leaf)) { Die "X-Plane $Version's install folder was not found ($prefs does not exist: is it installed, and has it been run once?); pass the folder with -XPlaneDir PATH" }
    $xplane = Get-Content -LiteralPath $prefs | Where-Object { $_ -match '\S' } | Select-Object -First 1
    if (-not $xplane) { Die "$prefs is empty; pass the X-Plane folder with -XPlaneDir PATH" }
    $xplane = $xplane.Trim()
    $sourceOf = $prefs
}
$xplane = $xplane.TrimEnd('\', '/')
# Checked whichever way the folder was named, before anything is changed.
if (-not (Test-Path -LiteralPath $xplane -PathType Container)) { Die "the X-Plane folder does not exist: $xplane (from $sourceOf); pass the right one with -XPlaneDir PATH" }
if (-not (Test-Path -LiteralPath (Join-Path $xplane 'Resources\plugins') -PathType Container)) { Die "not an X-Plane folder, it has no Resources\plugins: $xplane (from $sourceOf); pass the right one with -XPlaneDir PATH" }
$xplane = (Resolve-Path -LiteralPath $xplane).Path

# ---- X-Plane must be closed: it loads plugins at start only ----
if (Get-Process -Name 'X-Plane' -ErrorAction SilentlyContinue) {
    if (-not $Force) { Die 'X-Plane is running; close it first (or use -Force)' }
    Say 'X-Plane is running; the plugin loads at its next start'
}

$target = Join-Path $xplane 'Resources\plugins\BrunnerDirect'
$backups = Join-Path $xplane 'Mesmer-backups'
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
Say "X-Plane: $xplane"

$kept = New-Object System.Collections.Generic.List[string]
if (Test-Path -LiteralPath $target -PathType Container) {
    # What is the pilot's, or the plugin's own state, goes with the new install.
    foreach ($own in 'BrunnerDirect.ini', 'BrunnerDirect.feel.ini', 'BrunnerDirect.yokes.json') {
        if (Test-Path -LiteralPath (Join-Path $target $own) -PathType Leaf) { $kept.Add($own) }
    }
    foreach ($f in Get-ChildItem -LiteralPath $target -File -Force) {
        if (Test-Path -LiteralPath (Join-Path $Stage $f.Name)) { continue }
        if (-not $kept.Contains($f.Name)) { $kept.Add($f.Name) }
    }
}

if ($DryRun) {
    if (Test-Path -LiteralPath $target) { Say "Would move the existing plugin to $(Join-Path $backups "BrunnerDirect-$stamp")" }
    Say "Would install $Stage to $target"
    if ($kept.Count -gt 0) { Say "Would keep your $($kept -join ' ')" }
    exit 0
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("mesmer-install-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    $fresh = Join-Path $work 'BrunnerDirect'
    Copy-Item -LiteralPath $Stage -Destination $fresh -Recurse
    if (Test-Path -LiteralPath $target) {
        foreach ($name in $kept) { Copy-Item -LiteralPath (Join-Path $target $name) -Destination (Join-Path $fresh $name) -Force }
        New-Item -ItemType Directory -Force -Path $backups | Out-Null
        $backup = Join-Path $backups "BrunnerDirect-$stamp"
        $n = 1
        while (Test-Path -LiteralPath $backup) { $n++; $backup = Join-Path $backups "BrunnerDirect-$stamp-$n" } # two installs in a second
        try { Move-Item -LiteralPath $target -Destination $backup }
        catch { Die "could not move the existing plugin to $backup ($($_.Exception.Message)); is a file in it open?" }
        Say "Moved the existing plugin to $backup"
    }
    Copy-Item -LiteralPath $fresh -Destination $target -Recurse
    if (-not (Test-Path -LiteralPath (Join-Path $target 'win_x64\BrunnerDirect.xpl') -PathType Leaf)) { Die "the copy to $target failed" }
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
Say "Installed the plugin to $target"
if ($kept.Count -gt 0) { Say "Kept your $($kept -join ' ')" }
Say 'Start X-Plane; Log.txt will say "Yoke models loaded from ..." when it finds the yoke.'
exit 0
