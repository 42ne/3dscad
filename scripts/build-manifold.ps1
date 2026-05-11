param(
    [string]$QtRoot = "C:\Qt",
    [ValidateSet("32", "64")]
    [string]$Arch = "64",
    [string]$Generator = "MinGW Makefiles"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$manifoldSource = Join-Path $repoRoot "build\manifold-src"
$manifoldBuild = Join-Path $repoRoot "build\manifold-build-$Arch"
$cmake = Join-Path $QtRoot "Tools\CMake_64\bin\cmake.exe"
$mingwBin = Join-Path $QtRoot "Tools\mingw810_$Arch\bin"
$compiler = Join-Path $mingwBin "g++.exe"
$make = Join-Path $mingwBin "mingw32-make.exe"

if (!(Test-Path $cmake)) {
    throw "CMake was not found at $cmake"
}

if (!(Test-Path $compiler)) {
    throw "MinGW g++ was not found at $compiler"
}

if (!(Test-Path $manifoldSource)) {
    git clone --depth 1 https://github.com/elalish/manifold.git $manifoldSource
}

$env:Path = "$mingwBin;$(Split-Path $cmake);$env:Path"

& $cmake `
    -S $manifoldSource `
    -B $manifoldBuild `
    -G $Generator `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_SHARED_LIBS=OFF `
    -DMANIFOLD_TEST=OFF `
    -DMANIFOLD_PYBIND=OFF `
    -DMANIFOLD_CBIND=OFF `
    -DMANIFOLD_JSBIND=OFF `
    -DMANIFOLD_CROSS_SECTION=OFF `
    -DMANIFOLD_PAR=OFF `
    -DMANIFOLD_DOWNLOADS=OFF `
    "-DCMAKE_CXX_COMPILER=$compiler" `
    "-DCMAKE_MAKE_PROGRAM=$make"

& $cmake --build $manifoldBuild --config Release

Write-Host "Manifold build finished: $(Join-Path $manifoldBuild 'src\libmanifold.a')"
