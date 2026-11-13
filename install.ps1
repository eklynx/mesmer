<#
.SYNOPSIS
    Installs Mesmer, the X-Plane 12 plugin for the Brunner CLS-E yoke.

.DESCRIPTION
    Finds X-Plane 12, checks that X-Plane and CLS2Sim are closed, downloads the
    latest pre-release from GitHub (or uses -Zip), verifies it against the
    release's SHA256SUMS, and installs it as
    X-Plane 12\Resources\plugins\BrunnerDirect. An existing install is moved to
    X-Plane 12\Mesmer-backups first; your BrunnerDirect.ini and
    BrunnerDirect.feel.ini are kept. CLS2Sim's own X-Plane plugin
    (XPlane10Connector) is moved to Mesmer-backups only if you agree.

    Nothing outside the X-Plane folder and a temporary folder is changed.
    Run with -WhatIf to see what it would do.

.PARAMETER XPlaneRoot
    The X-Plane 12 folder (the one with X-Plane.exe). Found from
    %LOCALAPPDATA%\x-plane_install_12.txt when not given.

.PARAMETER Zip
    Install this release zip instead of downloading. Needs -Sums.

.PARAMETER Sums
    The SHA256SUMS file that lists -Zip.

.PARAMETER Tag
    Install this release tag (for example v0.1.0-alpha.1) instead of the newest.

.PARAMETER MoveConnector
    Yes or No: move CLS2Sim's XPlane10Connector out of the plugins folder
    without asking. Ask when not given.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\install.ps1 -WhatIf

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\install.ps1 -XPlaneRoot 'D:\X-Plane 12'
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$XPlaneRoot,
    [string]$Zip,
    [string]$Sums,
    [string]$Tag,
    [ValidateSet('Ask', 'Yes', 'No')][string]$MoveConnector = 'Ask',
    [string]$Repository = 'antialias/mesmer'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3

function Say([string]$Text) { Write-Host $Text }
function Stop-Install([string]$Text) { throw "Mesmer install stopped: $Text" }

# ---- 1. Find X-Plane 12 ----
function Find-XPlane {
    if ($XPlaneRoot) {
        if (-not (Test-Path -LiteralPath (Join-Path $XPlaneRoot 'X-Plane.exe'))) {
            Stop-Install "no X-Plane.exe in '$XPlaneRoot'."
        }
        return (Resolve-Path -LiteralPath $XPlaneRoot).Path
    }
    $list = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'x-plane_install_12.txt' } else { $null }
    if (-not $list -or -not (Test-Path -LiteralPath $list)) {
        Stop-Install "could not find X-Plane 12 (no %LOCALAPPDATA%\x-plane_install_12.txt). Run again with -XPlaneRoot 'C:\path\to\X-Plane 12'."
    }
    $found = @(Get-Content -LiteralPath $list | ForEach-Object { $_.Trim() } | Where-Object {
        $_ -and (Test-Path -LiteralPath (Join-Path $_ 'X-Plane.exe'))
    } | Select-Object -Unique)
    if ($found.Count -eq 0) { Stop-Install "x-plane_install_12.txt lists no folder with X-Plane.exe. Use -XPlaneRoot." }
    if ($found.Count -gt 1) {
        Stop-Install ("more than one X-Plane 12 is installed. Choose one with -XPlaneRoot:`n  " + ($found -join "`n  "))
    }
    return $found[0].TrimEnd('\', '/')
}

# ---- 2. X-Plane and CLS2Sim must be closed ----
function Assert-Closed {
    $running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ProcessName -match '^(X-Plane|CLS2Sim)'
    } | ForEach-Object { $_.ProcessName } | Select-Object -Unique)
    if ($running.Count -gt 0) {
        Stop-Install ("please close " + ($running -join ' and ') + " first. The plugin needs the yoke's USB bridge to itself, and X-Plane must not be running while its plugins change.")
    }
}

# ---- 3. Get the release ----
function Get-Release([string]$Work) {
    if ($Zip) {
        if (-not $Sums) { Stop-Install '-Zip needs -Sums, the SHA256SUMS file that lists it.' }
        return @{ Zip = (Resolve-Path -LiteralPath $Zip).Path; Sums = (Resolve-Path -LiteralPath $Sums).Path; Name = 'local file' }
    }
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $headers = @{ 'User-Agent' = 'mesmer-install'; 'Accept' = 'application/vnd.github+json' }
    if ($Tag) {
        $release = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/$Repository/releases/tags/$Tag"
    } else {
        # The newest release, pre-releases included (GitHub's "latest" skips them).
        $release = @(Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/$Repository/releases?per_page=10" |
            Where-Object { -not $_.draft }) | Select-Object -First 1
    }
    if (-not $release) { Stop-Install "no release found in $Repository." }
    $zipAsset = @($release.assets | Where-Object { $_.name -like '*.zip' })
    $sumsAsset = @($release.assets | Where-Object { $_.name -eq 'SHA256SUMS' })
    if ($zipAsset.Count -ne 1 -or $sumsAsset.Count -ne 1) {
        Stop-Install "release $($release.tag_name) does not have exactly one zip and one SHA256SUMS."
    }
    $zipPath = Join-Path $Work $zipAsset[0].name
    $sumsPath = Join-Path $Work 'SHA256SUMS'
    Say "Downloading $($release.tag_name): $($zipAsset[0].name)"
    Invoke-WebRequest -UseBasicParsing -Headers $headers -Uri $zipAsset[0].browser_download_url -OutFile $zipPath
    Invoke-WebRequest -UseBasicParsing -Headers $headers -Uri $sumsAsset[0].browser_download_url -OutFile $sumsPath
    return @{ Zip = $zipPath; Sums = $sumsPath; Name = $release.tag_name }
}

# ---- 4. Verify it ----
function Assert-Hash([string]$ZipPath, [string]$SumsPath) {
    $name = Split-Path -Leaf $ZipPath
    $expected = $null
    foreach ($line in Get-Content -LiteralPath $SumsPath) {
        if ($line -match '^\s*([0-9a-fA-F]{64})\s+\*?(.+?)\s*$' -and $Matches[2] -eq $name) { $expected = $Matches[1].ToLowerInvariant() }
    }
    if (-not $expected) { Stop-Install "SHA256SUMS has no line for $name." }
    $actual = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { Stop-Install "$name does not match SHA256SUMS (expected $expected, got $actual). Nothing was installed." }
    Say "Verified $name (SHA-256 $actual)"
}

# ---- 5. Unpack and check its layout ----
function Expand-Release([string]$ZipPath, [string]$Work) {
    $unpacked = Join-Path $Work 'unpacked'
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $unpacked -Force -WhatIf:$false
    $plugin = Join-Path $unpacked 'BrunnerDirect'
    foreach ($file in 'win_x64\BrunnerDirect.xpl', 'BrunnerDirect.ini', 'BrunnerDirect.profiles.json', 'yokes\cls-e.json', 'yokes\cls-e-ng.json') {
        if (-not (Test-Path -LiteralPath (Join-Path $plugin $file))) { Stop-Install "the zip has no BrunnerDirect\$file." }
    }
    return $plugin
}

# ---- Main ----
$root = Find-XPlane
Say "X-Plane 12: $root"
Assert-Closed

$work = Join-Path ([IO.Path]::GetTempPath()) ('mesmer-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -WhatIf:$false | Out-Null
try {
    $release = Get-Release $work
    Assert-Hash $release.Zip $release.Sums
    $new = Expand-Release $release.Zip $work

    $plugins = Join-Path $root 'Resources\plugins'
    $target = Join-Path $plugins 'BrunnerDirect'
    $stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
    $backups = Join-Path $root 'Mesmer-backups'

    # An existing install goes outside the plugins folder (X-Plane loads every
    # folder in it), keeping the pilot's own settings for the new one.
    $kept = @()
    if (Test-Path -LiteralPath $target) {
        foreach ($own in 'BrunnerDirect.ini', 'BrunnerDirect.feel.ini', 'BrunnerDirect.yokes.json') {
            $path = Join-Path $target $own
            if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $new $own) -Force -WhatIf:$false; $kept += $own }
        }
        $backup = Join-Path $backups "BrunnerDirect-$stamp"
        if ($PSCmdlet.ShouldProcess($target, "Move the existing plugin to $backup")) {
            New-Item -ItemType Directory -Path $backups -Force | Out-Null
            Move-Item -LiteralPath $target -Destination $backup
            Say "Moved the existing plugin to $backup"
        }
    }
    if ($PSCmdlet.ShouldProcess($target, 'Install the plugin')) {
        New-Item -ItemType Directory -Path $plugins -Force | Out-Null
        Copy-Item -LiteralPath $new -Destination $target -Recurse
        Say "Installed $($release.Name) to $target"
        if ($kept.Count) { Say ('Kept your ' + ($kept -join ' and ')) }
    }

    # CLS2Sim's X-Plane plugin: only with the pilot's yes.
    $connector = Join-Path $plugins 'XPlane10Connector'
    if (Test-Path -LiteralPath $connector) {
        $answer = $MoveConnector
        if ($answer -eq 'Ask') {
            if ($WhatIfPreference) { $answer = 'Yes' }
            else {
                Say ''
                Say "CLS2Sim's X-Plane plugin is installed: $connector"
                Say 'Mesmer replaces it; left in place it may hold pitch and roll. Moving it out of the plugins folder is reversible.'
                $reply = Read-Host 'Move it to Mesmer-backups now? [y/N]'
                $answer = if ($reply -match '^(y|yes)$') { 'Yes' } else { 'No' }
            }
        }
        if ($answer -eq 'Yes') {
            $aside = Join-Path $backups "XPlane10Connector-$stamp"
            if ($PSCmdlet.ShouldProcess($connector, "Move CLS2Sim's X-Plane plugin to $aside")) {
                New-Item -ItemType Directory -Path $backups -Force | Out-Null
                Move-Item -LiteralPath $connector -Destination $aside
                Say "Moved CLS2Sim's plugin to $aside (move it back to use CLS2Sim again)"
            }
        } else {
            Say "Left CLS2Sim's plugin in place. If the yoke's status reads 'Pitch or roll is owned by another plugin', move $connector out of the plugins folder."
        }
    }
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue -WhatIf:$false
}

Say ''
Say 'Next: your first flight'
Say '  1. Switch the yoke on. Keep CLS2Sim closed. Start X-Plane and load a flight.'
Say "  2. The corner line reads 'Yoke stopped - Retry to resume'."
Say '  3. Take your hands off the yoke, then click that line (or Plugins > Yoke (Brunner) > Retry).'
Say '     The yoke centres itself; then pitch and roll are yours.'
Say '  4. Force feel is off. Turn it on, parked, with Plugins > Yoke (Brunner) > Force feel.'
Say '     Stop yoke motors, in the same menu, turns it off at once.'
Say '  Guide: https://github.com/antialias/mesmer/blob/main/docs/first-flight.md'
Say '  Safety: https://github.com/antialias/mesmer/blob/main/SAFETY.md'
