# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'A current Visual Studio C++ toolchain is required' }
$devshell = Join-Path $installation 'Common7\Tools\Launch-VsDevShell.ps1'
& $devshell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC developer environment was not activated' }
cmake --workflow --preset release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$packages = @(Get-ChildItem dist/*.tar.gz)
if ($packages.Count -ne 1) { throw 'Expected one native package' }
python tools/package_smoke.py $packages[0].FullName
exit $LASTEXITCODE
