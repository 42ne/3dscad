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

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

function Patch-ManifoldParallelHeader {
    $parallelHeader = Join-Path $manifoldSource "src\parallel.h"
    if (!(Test-Path $parallelHeader)) {
        throw "Manifold parallel.h was not found at $parallelHeader"
    }

    $text = Get-Content $parallelHeader -Raw
    if ($text -notmatch "std::reduce|std::inclusive_scan|std::exclusive_scan") {
        return
    }

    $text = $text.Replace(
        "return std::reduce(first, last, init, f);",
        "for (auto it = first; it != last; ++it) {`r`n    init = f(init, *it);`r`n  }`r`n  return init;"
    )
    $text = $text.Replace(
        "return std::reduce(range.begin(), range.end(), value, f);",
        "for (auto it = range.begin(); it != range.end(); ++it) {`r`n              value = f(value, *it);`r`n            }`r`n            return value;"
    )
    $text = $text.Replace(
        "std::inclusive_scan(first, last, d_first);",
        "if (first == last) return;`r`n  T sum = *first;`r`n  *d_first = sum;`r`n  ++first;`r`n  ++d_first;`r`n  for (; first != last; ++first, ++d_first) {`r`n    sum = sum + *first;`r`n    *d_first = sum;`r`n  }"
    )
    $text = $text.Replace(
        "std::exclusive_scan(first, last, d_first, init, f);",
        "for (; first != last; ++first, ++d_first) {`r`n    *d_first = init;`r`n    init = f(init, *first);`r`n  }"
    )

    Set-Content -Path $parallelHeader -Value $text -NoNewline
    Write-Host "Patched Manifold parallel.h for Qt MinGW GCC 8 numeric fallbacks."
}

if (!(Test-Path $cmake)) {
    throw "CMake was not found at $cmake"
}

if (!(Test-Path $compiler)) {
    throw "MinGW g++ was not found at $compiler"
}

if (!(Test-Path $manifoldSource)) {
    Invoke-Native git clone --depth 1 https://github.com/elalish/manifold.git $manifoldSource
}

Patch-ManifoldParallelHeader

$env:Path = "$mingwBin;$(Split-Path $cmake);$env:Path"

Invoke-Native $cmake `
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

Invoke-Native $cmake --build $manifoldBuild --config Release

Write-Host "Manifold build finished: $(Join-Path $manifoldBuild 'src\libmanifold.a')"
