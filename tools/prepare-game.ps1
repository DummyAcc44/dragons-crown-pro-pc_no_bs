<#
.SYNOPSIS
  Unpacks the package (resumable), builds the prepared /app0 tree and the loader's boot image.
.DESCRIPTION
  game\extracted\base                           package contents (the .pkg is only read)
  game\prepared\v<Version>                     the /app0 tree (hard links, no copies)
  build\image\v<Version>                       boot.bin, content.bin, image.json for the loader
.PARAMETER Version
  1.03 (the only supported version: the package's param.sfo VERSION).
.PARAMETER Scope
  core: executables, modules and metadata only (about 25 MB). full: all game data (about 5.7 GB, the CPK archives).
.PARAMETER NoImage
  Skip the boot image step.
#>
[CmdletBinding()]
param(
    [ValidateSet('1.03')][string]$Version = '1.03',
    [ValidateSet('core', 'full')][string]$Scope = 'core',
    [string]$GameRoot,
    [switch]$NoImage
)

. (Join-Path $PSScriptRoot '_common.ps1')
if (-not $GameRoot) { $GameRoot = Join-Path $script:RepoRoot 'game' }
if (-not (Test-PythonModule 'cryptography')) { Stop-Tool 'cryptography is missing: run tools\setup.ps1 -InstallPython' }

Write-Step "Unpacking and layering ($Version, $Scope)"
$code = Invoke-Script 'prepare_game.py' @('--game-root', $GameRoot, '--version', $Version, '--scope', $Scope)
if ($code -ne 0) { exit $code }

if (-not $NoImage) {
    Write-Step 'Building the boot image'
    $view = Join-Path $GameRoot "prepared\v$Version"
    $out = Join-Path $script:RepoRoot "build\image\v$Version"
    $code = Invoke-Script 'prepare_image.py' @($view, '--out', $out)
    if ($code -ne 0) { exit $code }
}
