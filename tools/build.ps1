<#
.SYNOPSIS
  Makes everything current and builds dcp-loader.exe incrementally (MSYS2 CLANG64, CMake + Ninja). -Run launches it.
.DESCRIPTION
  Reuses what is current and updates only what is stale: the video core dependencies (gpu\deps.txt), the
  unpacked and prepared game (game\prepared\v1.03, extracted once), the boot image (build\image\v1.03) and
  the CMake configuration. The one build configuration, RelWithDebInfo (-O2 -g -DNDEBUG), is what is tested
  and what a package must contain; nothing else is built.
.PARAMETER Run
  Launches the loader that was just built and stays attached until the game is closed. Logs to
  build\logs\dragons_crown_pro_NNN.log.
.PARAMETER Clean
  Deletes build\cmake first (the prepared game, the boot image and the logs are kept).
.PARAMETER CheckImports
  Diagnostic: with -Run, lists the PS4 imports the runtime lacks instead of running the game.
.PARAMETER TimeoutSeconds
  Diagnostic for automated tests only: with -Run, kills the loader after this many seconds.
.PARAMETER LoaderArgs
  Extra arguments for dcp-loader.exe (with -Run).
.EXAMPLE
  .\tools\build.ps1
  .\tools\build.ps1 -Run
#>
[CmdletBinding()]
param(
    [switch]$Run,
    [switch]$Clean,
    [switch]$CheckImports,
    [int]$TimeoutSeconds = 0,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$LoaderArgs = @()
)

. (Join-Path $PSScriptRoot '_common.ps1')
$version = '1.03'
$config = 'RelWithDebInfo'
$root = $script:RepoRoot
$buildDir = Join-Path $root 'build\cmake\clang64'
$loader = Join-Path $buildDir 'bin\dcp-loader.exe'
$view = Join-Path $root "game\prepared\v$version"
$image = Join-Path $root "build\image\v$version"
$imageFiles = @('boot.bin', 'content.bin', 'image.json')

function Test-ViewCurrent {
    $marker = Join-Path $view '.dcp_view.json'
    if (-not (Test-Path -LiteralPath $marker)) { return $false }
    $info = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
    if ($info.version -ne $version -or $info.scope -ne 'full') { return $false }
    $package = @(Get-ChildItem -LiteralPath (Join-Path $root 'game') -Filter '*.pkg' -ErrorAction SilentlyContinue)
    return -not ($package | Where-Object { $_.LastWriteTime -gt (Get-Item -LiteralPath $marker).LastWriteTime })
}

function Test-ImageCurrent {
    $newest = (Get-Item -LiteralPath (Join-Path $view '.dcp_view.json')).LastWriteTime
    foreach ($script in 'prepare_image.py', 'ps4_module.py', 'game_profiles.py') {
        $time = (Get-Item -LiteralPath (Join-Path $root "scripts\$script")).LastWriteTime
        if ($time -gt $newest) { $newest = $time }
    }
    foreach ($name in $imageFiles) {
        $file = Join-Path $image $name
        if (-not (Test-Path -LiteralPath $file) -or (Get-Item -LiteralPath $file).LastWriteTime -lt $newest) { return $false }
    }
    return $true
}

$tc = Resolve-Toolchain 'auto'
$env:PATH = "$($tc.Bin);$env:PATH"
Write-Step "Toolchain clang64 ($($tc.Bin)), $config"

Sync-GpuDependencies

if (-not (Test-ViewCurrent)) {
    Write-Step 'Preparing the game (unpacks the package once, about 5.7 GB; later builds reuse it)'
    & (Join-Path $PSScriptRoot 'prepare-game.ps1') -Version $version -Scope full
    if ($LASTEXITCODE -ne 0) { Stop-Tool 'Game preparation failed' }
}
elseif (-not (Test-ImageCurrent)) {
    Write-Step 'Updating the boot image'
    if ((Invoke-Script 'prepare_image.py' @($view, '--out', $image)) -ne 0) { Stop-Tool 'Boot image build failed' }
}

if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    Write-Step 'Cleaning build\cmake'
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}
$cache = Join-Path $buildDir 'CMakeCache.txt'
$configured = (Test-Path -LiteralPath (Join-Path $buildDir 'build.ninja')) -and (Test-Path -LiteralPath $cache) -and
    (Select-String -Path $cache -SimpleMatch "CMAKE_BUILD_TYPE:STRING=$config" -Quiet)
if (-not $configured) {
    Write-Step 'Configuring'
    & cmake -S $root -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$config" '-DCMAKE_C_COMPILER=clang'
    if ($LASTEXITCODE -ne 0) { Stop-Tool 'CMake configuration failed' }
}
Write-Step 'Building'
& ninja -C $buildDir dcp-loader
if ($LASTEXITCODE -ne 0) { Stop-Tool 'Build failed' }

$required = @($loader) + @($imageFiles | ForEach-Object { Join-Path $image $_ }) + @(Join-Path $view '.dcp_view.json')
foreach ($file in $required) { if (-not (Test-Path -LiteralPath $file)) { Stop-Tool "Missing $file" } }
Write-Host ''
Write-Host "Built $loader" -ForegroundColor Green
if (-not $Run) { return }

$running = @(Get-Process dcp-loader -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $loader })
if ($running.Count -gt 0) {
    Write-Warn "dcp-loader.exe is already running (PID $($running.Id -join ', ')); not launching another copy."
    return
}
$arguments = @('--version', $version, '--loader', $loader)
if ($CheckImports) { $arguments += '--check-imports' }
if ($TimeoutSeconds -gt 0) { $arguments += @('--timeout', $TimeoutSeconds) }
if ($LoaderArgs.Count -gt 0) { $arguments += '--'; $arguments += $LoaderArgs }
exit (Invoke-Script 'run_game.py' $arguments)
