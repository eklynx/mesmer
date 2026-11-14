# Tests install.ps1 against a fake X-Plane folder and a locally built fake
# release zip. No network, no real X-Plane, no yoke.
#   pwsh -NoProfile -File tests/test-install.ps1
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3
$installer = Join-Path $PSScriptRoot '..' 'install.ps1'
$failures = 0
function Check([bool]$Condition, [string]$What) {
    if ($Condition) { Write-Host "ok   $What" } else { Write-Host "FAIL $What"; $script:failures++ }
}
function New-Sandbox {
    $dir = Join-Path ([IO.Path]::GetTempPath()) ('mesmer-test-' + [guid]::NewGuid().ToString('N'))
    $xp = Join-Path $dir 'X-Plane 12'
    New-Item -ItemType Directory -Path (Join-Path $xp 'Resources/plugins/XPlane10Connector/64') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $xp 'X-Plane.exe') -Value 'fake'
    Set-Content -LiteralPath (Join-Path $xp 'Resources/plugins/XPlane10Connector/64/win.xpl') -Value 'connector'
    # The release: BrunnerDirect/ with the plugin, its defaults and profiles.
    $stage = Join-Path $dir 'stage/BrunnerDirect'
    New-Item -ItemType Directory -Path (Join-Path $stage 'win_x64') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $stage 'win_x64/BrunnerDirect.xpl') -Value 'new plugin'
    Set-Content -LiteralPath (Join-Path $stage 'BrunnerDirect.ini') -Value 'schema_version=6'
    Set-Content -LiteralPath (Join-Path $stage 'BrunnerDirect.profiles.json') -Value '{"schema":2}'
    New-Item -ItemType Directory -Path (Join-Path $stage 'yokes') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $stage 'yokes/cls-e.json') -Value '{"schema":1}'
    Set-Content -LiteralPath (Join-Path $stage 'yokes/cls-e-ng.json') -Value '{"schema":1}'
    $zip = Join-Path $dir 'mesmer-0.1.0-alpha.1-win_x64.zip'
    Compress-Archive -Path $stage -DestinationPath $zip
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    $sums = Join-Path $dir 'SHA256SUMS'
    Set-Content -LiteralPath $sums -Value "$hash  mesmer-0.1.0-alpha.1-win_x64.zip"
    return @{ Dir = $dir; XPlane = $xp; Zip = $zip; Sums = $sums; Plugins = (Join-Path $xp 'Resources/plugins') }
}
function Tree([string]$Path) { @(Get-ChildItem -LiteralPath $Path -Recurse -File | ForEach-Object { $_.FullName.Substring($Path.Length) } | Sort-Object) -join '|' }

# 1. A fresh install, connector moved with the pilot's yes.
$s = New-Sandbox
& $installer -XPlaneRoot $s.XPlane -Zip $s.Zip -Sums $s.Sums -MoveConnector Yes | Out-Null
Check (Test-Path (Join-Path $s.Plugins 'BrunnerDirect/win_x64/BrunnerDirect.xpl')) 'fresh install: plugin in Resources/plugins/BrunnerDirect/win_x64'
Check (Test-Path (Join-Path $s.Plugins 'BrunnerDirect/BrunnerDirect.profiles.json')) 'fresh install: profiles beside it'
Check ((Test-Path (Join-Path $s.Plugins 'BrunnerDirect/yokes/cls-e.json')) -and (Test-Path (Join-Path $s.Plugins 'BrunnerDirect/yokes/cls-e-ng.json'))) 'fresh install: the yoke models beside it'
Check (-not (Test-Path (Join-Path $s.Plugins 'XPlane10Connector'))) 'fresh install: connector out of the plugins folder'
Check (@(Get-ChildItem (Join-Path $s.XPlane 'Mesmer-backups') -Directory -Filter 'XPlane10Connector-*').Count -eq 1) 'fresh install: connector kept in Mesmer-backups'

# 2. An upgrade keeps the pilot's settings and backs up the old plugin outside plugins/.
Set-Content -LiteralPath (Join-Path $s.Plugins 'BrunnerDirect/BrunnerDirect.ini') -Value 'schema_version=6 # mine'
Set-Content -LiteralPath (Join-Path $s.Plugins 'BrunnerDirect/BrunnerDirect.feel.ini') -Value 'feel_schema=4 # mine'
Set-Content -LiteralPath (Join-Path $s.Plugins 'BrunnerDirect/BrunnerDirect.yokes.json') -Value '{"schema":1,"yokes":[]} # mine'
& $installer -XPlaneRoot $s.XPlane -Zip $s.Zip -Sums $s.Sums -MoveConnector No | Out-Null
Check ((Get-Content (Join-Path $s.Plugins 'BrunnerDirect/BrunnerDirect.yokes.json')) -eq '{"schema":1,"yokes":[]} # mine') 'upgrade: BrunnerDirect.yokes.json kept'
Check ((Get-Content (Join-Path $s.Plugins 'BrunnerDirect/BrunnerDirect.ini')) -eq 'schema_version=6 # mine') 'upgrade: BrunnerDirect.ini kept'
Check ((Get-Content (Join-Path $s.Plugins 'BrunnerDirect/BrunnerDirect.feel.ini')) -eq 'feel_schema=4 # mine') 'upgrade: BrunnerDirect.feel.ini kept'
Check (@(Get-ChildItem (Join-Path $s.XPlane 'Mesmer-backups') -Directory -Filter 'BrunnerDirect-*').Count -eq 1) 'upgrade: old plugin in Mesmer-backups'
Check (@(Get-ChildItem $s.Plugins -Directory).Count -eq 1) 'upgrade: nothing extra left in plugins/'
Remove-Item -LiteralPath $s.Dir -Recurse -Force

# 3. -WhatIf changes nothing.
$s = New-Sandbox
$before = Tree $s.XPlane
& $installer -XPlaneRoot $s.XPlane -Zip $s.Zip -Sums $s.Sums -WhatIf | Out-Null
Check ((Tree $s.XPlane) -eq $before) '-WhatIf: the X-Plane folder is unchanged'

# 4. -MoveConnector No leaves CLS2Sim's plugin alone.
& $installer -XPlaneRoot $s.XPlane -Zip $s.Zip -Sums $s.Sums -MoveConnector No | Out-Null
Check (Test-Path (Join-Path $s.Plugins 'XPlane10Connector/64/win.xpl')) 'connector declined: left in place'
Remove-Item -LiteralPath $s.Dir -Recurse -Force

# 5. A zip that does not match SHA256SUMS installs nothing.
$s = New-Sandbox
Set-Content -LiteralPath $s.Sums -Value ('0' * 64 + '  mesmer-0.1.0-alpha.1-win_x64.zip')
$before = Tree $s.XPlane
$refused = $false
try { & $installer -XPlaneRoot $s.XPlane -Zip $s.Zip -Sums $s.Sums -MoveConnector Yes | Out-Null } catch { $refused = $_.Exception.Message -match 'does not match SHA256SUMS' }
Check $refused 'bad hash: refused with a clear message'
Check ((Tree $s.XPlane) -eq $before) 'bad hash: the X-Plane folder is unchanged'

# 6. A folder without X-Plane.exe is refused.
$refused = $false
try { & $installer -XPlaneRoot $s.Dir -Zip $s.Zip -Sums $s.Sums | Out-Null } catch { $refused = $_.Exception.Message -match 'no X-Plane.exe' }
Check $refused 'no X-Plane.exe: refused'

# 7. X-Plane found from %LOCALAPPDATA%\x-plane_install_12.txt.
$local = Join-Path $s.Dir 'LocalAppData'
New-Item -ItemType Directory -Path $local | Out-Null
Set-Content -LiteralPath (Join-Path $local 'x-plane_install_12.txt') -Value @((Join-Path $s.Dir 'missing'), $s.XPlane)
Set-Content -LiteralPath $s.Sums -Value ((Get-FileHash -LiteralPath $s.Zip -Algorithm SHA256).Hash.ToLowerInvariant() + '  mesmer-0.1.0-alpha.1-win_x64.zip')
$saved = $env:LOCALAPPDATA
try { $env:LOCALAPPDATA = $local; & $installer -Zip $s.Zip -Sums $s.Sums -MoveConnector No | Out-Null } finally { $env:LOCALAPPDATA = $saved }
Check (Test-Path (Join-Path $s.Plugins 'BrunnerDirect/win_x64/BrunnerDirect.xpl')) 'found X-Plane from x-plane_install_12.txt'
Remove-Item -LiteralPath $s.Dir -Recurse -Force

# The release it downloads: the highest version, whatever GitHub's order.
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $installer), [ref]$null, [ref]$null)
$fn = $ast.Find({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Select-NewestRelease' }, $true)
. ([scriptblock]::Create($fn.Extent.Text))
function Rel([string]$Tag, [bool]$Draft = $false) { [pscustomobject]@{ tag_name = $Tag; draft = $Draft } }
$github = @((Rel 'v0.1.0-alpha.3'), (Rel 'v0.1.0'), (Rel 'v0.1.0-alpha.2'), (Rel 'v0.1.0-alpha.1'))
Check ((Select-NewestRelease $github).tag_name -eq 'v0.1.0') 'picks v0.1.0 over the alphas GitHub lists first'
Check ((Select-NewestRelease @((Rel 'v0.1.0'), (Rel 'v0.10.0'), (Rel 'v0.2.0'), (Rel 'v0.1.1'))).tag_name -eq 'v0.10.0') 'compares version numbers as numbers'
Check ((Select-NewestRelease @((Rel 'v0.1.0-alpha.2'), (Rel 'v0.1.0-alpha.10'), (Rel 'v0.1.0-beta.1'))).tag_name -eq 'v0.1.0-beta.1') 'alpha before beta, alpha.10 after alpha.2'
Check ((Select-NewestRelease @((Rel 'v0.2.0' $true), (Rel 'v0.1.1'))).tag_name -eq 'v0.1.1') 'skips drafts'
Check ((Select-NewestRelease @((Rel 'nightly'), (Rel 'v0.1.0'))).tag_name -eq 'v0.1.0') 'skips tags that are not versions'
Check ($null -eq (Select-NewestRelease @())) 'no release, nothing picked'

if ($failures) { Write-Host "$failures failed"; exit 1 }
Write-Host 'all passed'
