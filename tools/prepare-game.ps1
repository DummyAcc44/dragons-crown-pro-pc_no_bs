<#
.SYNOPSIS
  Developer convenience: unpacks a fake package from game\ (resumable) into the prepared /app0 tree.
.DESCRIPTION
  game\extracted\base                           package contents (the .pkg is only read)
  game\prepared\v<Version>                     the /app0 tree (hard links, no copies)
  Needs Python with the cryptography package (tools\setup.ps1 -InstallPython). tools\build.ps1 then builds the boot
  image. Players and the launcher do not use this: Install game... unpacks the .pkg natively.
.PARAMETER Version
  1.03 (the only supported version: the package's param.sfo VERSION).
.PARAMETER Scope
  core: executables, modules and metadata only (about 25 MB). full: all game data (about 5.7 GB, the CPK archives).
#>
[CmdletBinding()]
param(
    [ValidateSet('1.03')][string]$Version = '1.03',
    [ValidateSet('core', 'full')][string]$Scope = 'full',
    [string]$GameRoot
)

. (Join-Path $PSScriptRoot '_common.ps1')
if (-not $GameRoot) { $GameRoot = Join-Path $script:RepoRoot 'game' }
if (-not (Test-PythonModule 'cryptography')) { Stop-Tool 'cryptography is missing: run tools\setup.ps1 -InstallPython' }

Write-Step "Unpacking and layering ($Version, $Scope)"
$code = Invoke-Script 'prepare_game.py' @('--game-root', $GameRoot, '--version', $Version, '--scope', $Scope)
if ($code -ne 0) { exit $code }
Write-Ok 'Prepared. tools\build.ps1 builds the boot image.'
