<#
.SYNOPSIS
  Reads the package metadata (read-only) and, when already extracted, the executables and modules.
.PARAMETER GameRoot
  Folder holding the .pkg (default: <repo>\game).
.PARAMETER List
  Also prints the package's top-level file summary (needs cryptography; reads only the PFS index).
#>
[CmdletBinding()]
param(
    [string]$GameRoot,
    [switch]$List
)

. (Join-Path $PSScriptRoot '_common.ps1')
if (-not $GameRoot) { $GameRoot = Join-Path $script:RepoRoot 'game' }

$packages = @()
if (Test-Path -LiteralPath $GameRoot) { $packages = @(Get-ChildItem -LiteralPath $GameRoot -Filter '*.pkg') }
if ($packages.Count -eq 0) { Stop-Tool "No .pkg files in '$GameRoot'." }

$python = Get-Python
if (-not $python) { Stop-Tool 'Python 3 not found. Run tools\setup.ps1.' }

$verdict = 0
$infos = @()
foreach ($pkg in $packages) {
    Write-Step $pkg.FullName
    $json = & $python.Exe @($python.Args) '-I' (Join-Path $script:RepoRoot 'scripts\pkg_info.py') $pkg.FullName '--json'
    if ($LASTEXITCODE -ne 0) { Stop-Tool "$($pkg.Name) is not a readable PS4 PKG" }
    $info = ($json -join "`n") | ConvertFrom-Json
    $infos += $info
    $sfo = $info.param_sfo
    Write-Host ("  title id     : {0}" -f $info.title_id)
    Write-Host ("  title        : {0}" -f $sfo.TITLE)
    Write-Host ("  kind         : {0} (CATEGORY={1})" -f $info.kind, $sfo.CATEGORY)
    Write-Host ("  APP_VER      : {0}   VERSION: {1}   SDK (pubtoolinfo): {2}" -f $sfo.APP_VER, $sfo.VERSION, (($sfo.PUBTOOLINFO -split ',' | Where-Object { $_ -like 'sdk_ver=*' }) -replace 'sdk_ver=', ''))
    Write-Host ("  content id   : {0}" -f $info.content_id)
    Write-Host ("  size         : {0:N0} bytes, header agrees: {1}" -f $info.file_size, $info.size_matches_header)
    Write-Host ("  structure    : {0} entries, PFS image inside file: {1}, entries inside file: {2}" -f $info.entry_count, $info.pfs_within_file, $info.entries_within_file)
    Write-Host ("  fake PKG     : {0} (image key index {1})" -f $info.fake_pkg, $info.image_key_index)
    Write-Host ("  delta info   : {0}" -f $info.has_delta_info)
    if ($List) {
        if (-not (Test-PythonModule 'cryptography')) { Stop-Tool 'Listing needs cryptography: run tools\setup.ps1 -InstallPython' }
        & $python.Exe @($python.Args) '-I' (Join-Path $script:RepoRoot 'scripts\pkg_extract.py') $pkg.FullName '--list' |
            Where-Object { $_ -match '^(files|pfs image)|^[d-]\s+\d+\s+[^/]+(/[^/]+)?$' }
    }
}

Write-Step 'Profile'
$base = @($infos | Where-Object { $_.kind -eq 'base' -and $_.title_id -eq 'CUSA10454' })
$other = @($infos | Where-Object { $_.kind -ne 'base' -or $_.title_id -ne 'CUSA10454' })
if ($base.Count -eq 0) { Write-Bad 'no Dragon''s Crown Pro (CUSA10454) base package'; $verdict = 1 }
elseif ($base.Count -gt 1) { Write-Bad 'more than one base package for CUSA10454'; $verdict = 1 }
else {
    $b = $base[0]
    if ($b.content_id -eq 'UP2611-CUSA10454_00-DRAGONSCROWNPRO1') { Write-Ok 'content id matches the profile' } else { Write-Bad "unexpected content id $($b.content_id)"; $verdict = 1 }
    if ($b.param_sfo.APP_VER -eq '01.00' -and $b.param_sfo.VERSION -eq '01.03') { Write-Ok 'APP_VER 01.00 / VERSION 01.03: supported (profile version 1.03)' }
    else { Write-Bad "APP_VER $($b.param_sfo.APP_VER) / VERSION $($b.param_sfo.VERSION) is not a supported version"; $verdict = 1 }
    if ($b.fake_pkg) { Write-Ok 'extractable with the public fake-PKG keys' } else { Write-Bad 'retail PKG (needs console keys)'; $verdict = 1 }
}
foreach ($o in $other) { Write-Warn "$($o.file): not the supported base package (kind $($o.kind), $($o.title_id)); ignored" }

$extracted = Join-Path $GameRoot 'extracted\base'
if (Test-Path -LiteralPath (Join-Path $extracted 'eboot.bin')) {
    Write-Step "Executables ($extracted)"
    $code = Invoke-Script 'game_report.py' @($extracted)
    if ($code -ne 0) { $verdict = 1 }
} else {
    Write-Warn 'not extracted yet: run tools\prepare-game.ps1 to read the executables'
}
exit $verdict
