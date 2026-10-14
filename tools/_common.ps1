# Shared helpers, dot-sourced by the other tools. Works in Windows PowerShell 5.1 and PowerShell 7.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Write-Step([string]$Text) { Write-Host "==> $Text" -ForegroundColor Cyan }
function Write-Ok([string]$Text)   { Write-Host "  [ok]   $Text" -ForegroundColor Green }
function Write-Warn([string]$Text) { Write-Host "  [warn] $Text" -ForegroundColor Yellow }
function Write-Bad([string]$Text)  { Write-Host "  [miss] $Text" -ForegroundColor Red }

function Stop-Tool([string]$Text) {
    Write-Host "error: $Text" -ForegroundColor Red
    exit 1
}

function Get-VenvPython {
    $path = Join-Path $script:RepoRoot '.venv\Scripts\python.exe'
    if (Test-Path -LiteralPath $path) { return $path }
    return $null
}

# Interpreter for the scripts: the project venv, else the py launcher, else the first python on PATH.
function Get-Python {
    $venv = Get-VenvPython
    if ($venv) { return @{ Exe = $venv; Args = @() } }
    $launcher = Get-Command py -ErrorAction SilentlyContinue
    if ($launcher) { return @{ Exe = $launcher.Source; Args = @('-3') } }
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) { return @{ Exe = $python.Source; Args = @() } }
    return $null
}

# Runs scripts/<name> with isolated-mode Python so nothing next to the data is imported.
function Invoke-Script([string]$Name, [string[]]$Arguments = @()) {
    $python = Get-Python
    if (-not $python) { Stop-Tool 'Python 3 not found. Run tools\setup.ps1.' }
    $script = Join-Path $script:RepoRoot "scripts\$Name"
    & $python.Exe @($python.Args) '-I' $script @Arguments | Out-Host
    return $LASTEXITCODE
}

function Test-PythonModule([string]$Module) {
    $python = Get-Python
    if (-not $python) { return $false }
    & $python.Exe @($python.Args) '-I' '-c' "import $Module" 2>$null
    return ($LASTEXITCODE -eq 0)
}

function Get-Msys2Root {
    $candidates = @($env:MSYS2_ROOT, 'C:\msys64', 'D:\msys64') | Where-Object { $_ }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'usr\bin\pacman.exe')) { return $candidate }
    }
    return $null
}

# The only supported toolchain: MSYS2 CLANG64, as in the reference ports (the video core needs it).
$script:Toolchains = [ordered]@{
    clang64 = @{ Dir = 'clang64'; Compiler = 'clang.exe'; Package = 'mingw-w64-clang-x86_64' }
}
# Needed to build the loader and runtime now.
$script:BuildTools = @('clang.exe', 'cmake.exe', 'ninja.exe', 'pkgconf.exe', 'SDL3.dll')
$script:Clang64Packages = 'mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,sdl3}'
# Needed once the shadPS4-derived video core is vendored (feature/first-boot).
$script:VideoCoreTools = @('glslang.exe', 'vulkan-1.dll')
$script:VideoCorePackages = 'mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} ' +
    'mingw-w64-clang-x86_64-{fmt,boost,robin-map,xxhash,ffmpeg,zydis,spirv-tools,spirv-headers,spirv-cross}'

function Get-ToolchainStatus([string]$Name) {
    $info = $script:Toolchains[$Name]
    $root = Get-Msys2Root
    $status = @{ Name = $Name; Ready = $false; Bin = $null; Missing = @(); VideoCoreMissing = @() }
    if (-not $root) { $status.Missing = @('MSYS2'); return $status }
    $bin = Join-Path $root "$($info.Dir)\bin"
    $status.Bin = $bin
    foreach ($tool in $script:BuildTools) {
        if (-not (Test-Path -LiteralPath (Join-Path $bin $tool))) { $status.Missing += $tool }
    }
    $status.Ready = ($status.Missing.Count -eq 0)
    $status.VideoCoreMissing = @($script:VideoCoreTools | Where-Object { -not (Test-Path -LiteralPath (Join-Path $bin $_)) })
    return $status
}

function Resolve-Toolchain([string]$Requested) {
    if ($Requested -and $Requested -ne 'auto') {
        $status = Get-ToolchainStatus $Requested
        if (-not $status.Ready) { Stop-Tool "Toolchain '$Requested' is incomplete (missing: $($status.Missing -join ', ')). Run tools\setup.ps1." }
        return $status
    }
    foreach ($name in $script:Toolchains.Keys) {
        $status = Get-ToolchainStatus $name
        if ($status.Ready) { return $status }
    }
    Stop-Tool 'MSYS2 CLANG64 toolchain or libraries missing. Run tools\setup.ps1 for what to install.'
}
