<#
.SYNOPSIS
  Runs the loader on the prepared image and logs the run to user\last_run.log.
.PARAMETER CheckImports
  Lists the PS4 imports the runtime does not implement, without running the game's code.
.PARAMETER TimeoutSeconds
  Kill the loader after this many seconds (0: never).
.PARAMETER LoaderArgs
  Extra arguments passed to dcp-loader.exe.
#>
[CmdletBinding()]
param(
    [ValidateSet('1.03')][string]$Version = '1.03',
    [switch]$CheckImports,
    [int]$TimeoutSeconds = 0,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$LoaderArgs = @()
)

. (Join-Path $PSScriptRoot '_common.ps1')
$builds = @(Get-ChildItem -LiteralPath (Join-Path $script:RepoRoot 'build\cmake') -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\dcp-loader.exe') } |
    Sort-Object { (Get-Item -LiteralPath (Join-Path $_.FullName 'bin\dcp-loader.exe')).LastWriteTime } -Descending)
if ($builds.Count -eq 0) { Stop-Tool 'No dcp-loader.exe: run tools\build.ps1' }
$binFile = Join-Path $builds[0].FullName 'toolchain-bin.txt'
if (Test-Path -LiteralPath $binFile) { $env:PATH = "$((Get-Content -LiteralPath $binFile -Raw).Trim());$env:PATH" }

$arguments = @('--version', $Version, '--loader', (Join-Path $builds[0].FullName 'bin\dcp-loader.exe'))
if ($CheckImports) { $arguments += '--check-imports' }
if ($TimeoutSeconds -gt 0) { $arguments += @('--timeout', $TimeoutSeconds) }
if ($LoaderArgs.Count -gt 0) { $arguments += '--'; $arguments += $LoaderArgs }
exit (Invoke-Script 'run_game.py' $arguments)
