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
    # Normalise line endings to LF for consistent matching; we will restore CRLF at save.
    $text = $text.Replace("`r`n", "`n")

    # ── GCC 8 numeric fallbacks ────────────────────────────────────────────────
    if ($text -match "std::reduce|std::inclusive_scan|std::exclusive_scan") {
        $text = $text.Replace(
            "return std::reduce(first, last, init, f);",
            "for (auto it = first; it != last; ++it) {`n    init = f(init, *it);`n  }`n  return init;"
        )
        $text = $text.Replace(
            "return std::reduce(range.begin(), range.end(), value, f);",
            "for (auto it = range.begin(); it != range.end(); ++it) {`n              value = f(value, *it);`n            }`n            return value;"
        )
        $text = $text.Replace(
            "std::inclusive_scan(first, last, d_first);",
            "if (first == last) return;`n  T sum = *first;`n  *d_first = sum;`n  ++first;`n  ++d_first;`n  for (; first != last; ++first, ++d_first) {`n    sum = sum + *first;`n    *d_first = sum;`n  }"
        )
        # Replace stdlib call with sequential loop (still has alias bug — fixed below).
        $text = $text.Replace(
            "std::exclusive_scan(first, last, d_first, init, f);",
            "for (; first != last; ++first, ++d_first) {`n    *d_first = init;`n    init = f(init, *first);`n  }"
        )
        Write-Host "Patched Manifold parallel.h for Qt MinGW GCC 8 numeric fallbacks."
    }

    # ── Hull fix: exclusive_scan alias bug (manifold-hull-fix.patch) ──────────
    # When input and output iterators alias the same buffer, writing *d_first
    # before reading *first corrupts the scan.  Save *first first.
    $buggyLoop  = "for (; first != last; ++first, ++d_first) {`n    *d_first = init;`n    init = f(init, *first);`n  }"
    $fixedLoop  = "for (; first != last; ++first, ++d_first) {`n    auto cur = *first;  // read BEFORE write -- fixes aliased/in-place usage`n    *d_first = init;`n    init = f(init, cur);`n  }"
    if ($text.Contains("auto cur = *first")) {
        Write-Host "Hull fix already applied to Manifold parallel.h - skipping."
    } elseif ($text.Contains("*d_first = init;")) {
        $text = $text.Replace($buggyLoop, $fixedLoop)
        Write-Host "Applied hull fix to Manifold parallel.h (exclusive_scan alias bug)."
    } else {
        Write-Warning "Hull fix: expected loop pattern not found in parallel.h - manual check required."
    }

    Set-Content -Path $parallelHeader -Value $text -NoNewline
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
    -DMANIFOLD_CROSS_SECTION=ON `
    -DMANIFOLD_USE_BUILTIN_CLIPPER2=ON `
    -DMANIFOLD_PAR=OFF `
    -DMANIFOLD_DOWNLOADS=ON `
    "-DCMAKE_CXX_COMPILER=$compiler" `
    "-DCMAKE_MAKE_PROGRAM=$make"

Invoke-Native $cmake --build $manifoldBuild --config Release

Write-Host "Manifold build finished: $(Join-Path $manifoldBuild 'src\libmanifold.a')"
