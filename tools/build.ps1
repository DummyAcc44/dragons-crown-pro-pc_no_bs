<#
.SYNOPSIS
  Configures (once) and builds dcp-loader.exe incrementally with CMake + Ninja from MSYS2 CLANG64.
.PARAMETER Toolchain
  auto or clang64 (the only supported toolchain).
.PARAMETER Config
  CMake build type (default RelWithDebInfo).
.PARAMETER Reconfigure
  Re-runs CMake even when the build folder is already configured.
#>
[CmdletBinding()]
param(
    [ValidateSet('auto', 'clang64')][string]$Toolchain = 'auto',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')][string]$Config = 'RelWithDebInfo',
    [switch]$Reconfigure
)

. (Join-Path $PSScriptRoot '_common.ps1')
$tc = Resolve-Toolchain $Toolchain
$info = $script:Toolchains[$tc.Name]
Sync-GpuDependencies

$buildDir = Join-Path $script:RepoRoot "build\cmake\$($tc.Name)"
$env:PATH = "$($tc.Bin);$env:PATH"
$compiler = [IO.Path]::GetFileNameWithoutExtension($info.Compiler)

Write-Step "Toolchain $($tc.Name): $compiler from $($tc.Bin)"
if ($Reconfigure -or -not (Test-Path -LiteralPath (Join-Path $buildDir 'build.ninja'))) {
    Write-Step 'Configuring'
    & cmake -S $script:RepoRoot -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_C_COMPILER=$compiler"
    if ($LASTEXITCODE -ne 0) { Stop-Tool 'CMake configuration failed' }
}
Write-Step 'Building'
& ninja -C $buildDir dcp-loader
if ($LASTEXITCODE -ne 0) { Stop-Tool 'Build failed' }
Set-Content -LiteralPath (Join-Path $buildDir 'toolchain-bin.txt') -Value $tc.Bin -Encoding ASCII
Write-Host ''
Write-Host "Built $(Join-Path $buildDir 'bin\dcp-loader.exe')" -ForegroundColor Green
