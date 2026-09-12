param(
    [string]$VcpkgRoot = "",
    [string]$GmshSdkDir = "C:/libs/gmsh-sdk",
    [string]$CholmodRoot = "",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"

git submodule update --init --recursive

$cmakeArgs = @(
    "-S", "$PSScriptRoot/..",
    "-B", $BuildDir,
    "-G", "Ninja",
    "-A", "x64",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DGMSH_SDK_DIR=$GmshSdkDir"
)

if ($VcpkgRoot -ne "") {
    $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake"
    & "$VcpkgRoot/vcpkg.exe" install eigen3:x64-windows suitesparse:x64-windows
}

if ($CholmodRoot -ne "") {
    $cmakeArgs += "-DCHOLMOD_ROOT=$CholmodRoot"
}

cmake @cmakeArgs
cmake --build $BuildDir --config Release
cpack --config "$BuildDir/CPackConfig.cmake" -G ZIP -B $BuildDir

Write-Host "ZIP created successfully in $BuildDir/."
