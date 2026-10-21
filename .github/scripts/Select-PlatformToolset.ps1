<#
.SYNOPSIS
  Chooses the MSVC platform toolset for CI builds and publishes it as step outputs.

.DESCRIPTION
  The projects target v145 (Visual Studio 2026). When the newest Visual Studio on the
  runner does not have v145, the newest installed v14x toolset (v143 on images that only
  ship Visual Studio 2022) is selected instead.

  Step outputs (GITHUB_OUTPUT):
    toolset       the selected toolset, for example v145 or v143
    msbuild_args  extra MSBuild arguments: empty for v145, /p:PlatformToolset=<toolset> otherwise
    vc_bin        the x64-hosted x64 compiler directory, which also holds the ASan runtime DLLs
#>
param(
    [string] $Preferred = 'v145'
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) {
    throw 'No Visual Studio installation with the MSVC x64 tools was found.'
}

$toolsets = Get-ChildItem -Path (Join-Path $vs 'MSBuild\Microsoft\VC') -Directory |
    ForEach-Object { Join-Path $_.FullName 'Platforms\x64\PlatformToolsets' } |
    Where-Object { Test-Path $_ } |
    ForEach-Object { Get-ChildItem -Path $_ -Directory } |
    Where-Object { $_.Name -match '^v14\d$' } |
    Select-Object -ExpandProperty Name -Unique |
    Sort-Object { [int]$_.Substring(1) } -Descending

if (-not $toolsets) {
    throw "No v14x platform toolset was found under $vs."
}

if ($toolsets -contains $Preferred) {
    $toolset = $Preferred
    $msbuildArgs = ''
} else {
    $toolset = @($toolsets)[0]
    $msbuildArgs = "/p:PlatformToolset=$toolset"
    Write-Output "::warning::Platform toolset $Preferred is not installed on this runner; building with $toolset."
}

$toolsVersionFile = Join-Path $vs 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt'
$vcBin = ''
if (Test-Path $toolsVersionFile) {
    $toolsVersion = (Get-Content -Path $toolsVersionFile -Raw).Trim()
    $vcBin = Join-Path $vs "VC\Tools\MSVC\$toolsVersion\bin\Hostx64\x64"
}

Write-Output "Visual Studio: $vs"
Write-Output "Installed toolsets: $($toolsets -join ', ')"
Write-Output "Selected toolset: $toolset"
Write-Output "MSVC bin: $vcBin"

if ($env:GITHUB_OUTPUT) {
    "toolset=$toolset" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    "msbuild_args=$msbuildArgs" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    "vc_bin=$vcBin" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
}
