param(
    [string]$VcpkgRoot = "",
    [string]$GmshSdkDir = "C:/libs/gmsh-sdk",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"

git submodule update --init --recursive

$cmakeArgs = @(
    "-S", ".",
    "-B", $BuildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DGMSH_SDK_DIR=$GmshSdkDir"
)

if ($VcpkgRoot -ne "") {
    $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake"
}

cmake @cmakeArgs
cmake --build $BuildDir --config Release
cpack --config "$BuildDir/CPackConfig.cmake" -G ZIP

Write-Host "ZIP created successfully."