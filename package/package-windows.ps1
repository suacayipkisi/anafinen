# Builds a Release binary in build/package-release and packs it into a ZIP with CPack.
# Independent of the CMake Tools variant selected in VS Code: a Debug build would link the
# debug CRT (ucrtbased.dll, VCRUNTIME140D.dll), which exists only where Visual Studio is installed.
#
# The vcpkg toolchain, triplet and Gmsh SDK come from the parameters, else from
# .vscode/settings.json (cmake.configureSettings). Whatever is still unset is located by CMake
# (cmake/LocateWindowsSdks.cmake: VCPKG_ROOT / GMSH_SDK_DIR, PATH, then a scan of every drive).
param(
    [string]$Toolchain,
    [string]$Triplet,
    [string]$GmshSdkDir,
    [string]$BuildDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $root 'build/package-release' }

$settingsPath = Join-Path $root '.vscode/settings.json'
if (Test-Path $settingsPath) {
    $configure = (Get-Content $settingsPath -Raw | ConvertFrom-Json).'cmake.configureSettings'
    if ($configure) {
        if (-not $Toolchain)  { $Toolchain  = $configure.CMAKE_TOOLCHAIN_FILE }
        if (-not $Triplet)    { $Triplet    = $configure.VCPKG_TARGET_TRIPLET }
        if (-not $GmshSdkDir) { $GmshSdkDir = $configure.GMSH_SDK_DIR }
    }
}
if (-not $Triplet) { $Triplet = 'x64-windows' }
if ($Toolchain -and -not (Test-Path $Toolchain)) {
    throw "vcpkg toolchain '$Toolchain' does not exist. Fix -Toolchain / settings.json, or drop it to auto-detect."
}

# Load the MSVC x64 environment unless this shell already has it (Developer PowerShell).
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found; install Visual Studio or the Build Tools.' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'No Visual Studio installation with the C++ x64 tools was found.' }
    $vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}

$configureArgs = @(
    '-S', $root, '-B', $BuildDir, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DVCPKG_TARGET_TRIPLET=$Triplet"
)
if ($Toolchain)  { $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" }
if ($GmshSdkDir) { $configureArgs += "-DGMSH_SDK_DIR=$GmshSdkDir" }

& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& cmake --build $BuildDir --target anafinen anafinen_material_library
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
& cpack --config (Join-Path $BuildDir 'CPackConfig.cmake') -G ZIP -B $BuildDir
if ($LASTEXITCODE -ne 0) { throw 'CPack failed.' }

Get-ChildItem $BuildDir -Filter 'anafinen-*.zip' | ForEach-Object { "Package: $($_.FullName)" }
