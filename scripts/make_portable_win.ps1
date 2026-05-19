#Requires -Version 5.1
<#
.SYNOPSIS
    Creates a portable single-exe for 3DScad on Windows (7-Zip SFX).
.DESCRIPTION
    1. Finds the compiled 3DScad.exe (Release preferred, Debug as fallback)
    2. Runs windeployqt to gather Qt DLLs and plugins
    3. Copies MinGW runtime DLLs
    4. Copies docs/sample_codes examples
    5. Packs everything into a self-extracting exe via 7-Zip SFX
.PARAMETER Arch
    "64" or "32" - must match the Qt kit used to compile (default: 64)
.PARAMETER BuildType
    "Release" or "Debug" (default: Release; falls back to Debug automatically)
.PARAMETER OutDir
    Directory for the output exe (default: <repo>/dist)
.EXAMPLE
    .\make_portable_win.ps1
    .\make_portable_win.ps1 -Arch 32 -BuildType Debug
#>
param(
    [ValidateSet("64","32")]
    [string]$Arch = "64",
    [ValidateSet("Release","Debug")]
    [string]$BuildType = "Release",
    [string]$OutDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step([string]$msg) { Write-Host "" ; Write-Host "==> $msg" -ForegroundColor Cyan }
function Fail([string]$msg)       { Write-Host "ERROR: $msg" -ForegroundColor Red ; exit 1 }

# ---- paths -------------------------------------------------------------------
$repoRoot  = Split-Path -Parent $PSScriptRoot
$qtRoot    = "C:\Qt\5.15.2\mingw81_$Arch"
$mingwBin  = "C:\Qt\Tools\mingw810_$Arch\bin"
# Find 7-Zip in either Program Files location
$sevenDir = @("C:\Program Files\7-Zip", "C:\Program Files (x86)\7-Zip") |
            Where-Object { Test-Path (Join-Path $_ "7z.exe") } |
            Select-Object -First 1
$sevenExe  = if ($sevenDir) { Join-Path $sevenDir "7z.exe" } else { "7z.exe" }
$sevenSfx  = if ($sevenDir) { Join-Path $sevenDir "7z.sfx" } else { "7z.sfx" }
$distDir   = if ($OutDir -ne "") { $OutDir } else { Join-Path $repoRoot "dist" }
$stageDir  = Join-Path $distDir "_stage"
$archFile  = Join-Path $distDir "_tmp.7z"
$outputExe = Join-Path $distDir "3DScad_portable.exe"

# 64-bit MinGW uses SEH, 32-bit uses DW2
$libgccDll = if ($Arch -eq "64") { "libgcc_s_seh-1.dll" } else { "libgcc_s_dw2-1.dll" }

# ---- locate exe --------------------------------------------------------------
Write-Step "Locating compiled exe ($Arch-bit $BuildType)"

$buildRoot = Join-Path $repoRoot "build"
$pattern   = "*MinGW*${Arch}*bit*${BuildType}*"
$buildDir  = Get-ChildItem $buildRoot -Directory -ErrorAction SilentlyContinue |
             Where-Object { $_.Name -like $pattern } |
             Select-Object -First 1

if ($null -eq $buildDir -and $BuildType -eq "Release") {
    Write-Warning "No Release build found - trying Debug"
    $BuildType = "Debug"
    $pattern   = "*MinGW*${Arch}*bit*Debug*"
    $buildDir  = Get-ChildItem $buildRoot -Directory -ErrorAction SilentlyContinue |
                 Where-Object { $_.Name -like $pattern } |
                 Select-Object -First 1
}

if ($null -eq $buildDir) {
    Fail "No matching build directory found. Compile the project in Qt Creator first."
}

$exePath = Join-Path $buildDir.FullName "$($BuildType.ToLower())\3DScad.exe"
if (-not (Test-Path $exePath)) {
    Fail "3DScad.exe not found at: $exePath"
}
Write-Host "  $exePath"

# ---- check tools -------------------------------------------------------------
Write-Step "Checking required tools"
if (-not (Test-Path "$qtRoot\bin\windeployqt.exe")) { Fail "windeployqt not found: $qtRoot\bin" }
if (-not (Test-Path $mingwBin))                    { Fail "MinGW bin not found: $mingwBin" }
if (-not (Test-Path $sevenExe))                    { Fail "7z.exe not found: $sevenExe" }
if (-not (Test-Path $sevenSfx))                    { Fail "7z.sfx not found: $sevenSfx" }
Write-Host "  All tools OK"

# ---- prepare staging dir -----------------------------------------------------
Write-Step "Preparing staging directory"
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
New-Item $distDir  -ItemType Directory -Force | Out-Null
New-Item $stageDir -ItemType Directory        | Out-Null
Copy-Item $exePath $stageDir
Write-Host "  Copied exe to stage"

# ---- windeployqt -------------------------------------------------------------
Write-Step "Running windeployqt"
$env:PATH = "$qtRoot\bin;$mingwBin;" + $env:PATH

$wdArgs = "--no-translations --no-system-d3d-compiler --no-opengl-sw"
if ($BuildType -eq "Release") { $wdArgs = "--release $wdArgs" }
$stageExe = Join-Path $stageDir "3DScad.exe"
$wdCmd = """$qtRoot\bin\windeployqt.exe"" $wdArgs ""$stageExe"""
cmd /c $wdCmd
if ($LASTEXITCODE -ne 0) { Fail "windeployqt failed (exit $LASTEXITCODE)" }

# ---- MinGW runtime DLLs ------------------------------------------------------
Write-Step "Copying MinGW runtime DLLs"
$mingwDlls = @($libgccDll, "libstdc++-6.dll", "libwinpthread-1.dll")
foreach ($dll in $mingwDlls) {
    $src = Join-Path $mingwBin $dll
    if (Test-Path $src) {
        Copy-Item $src $stageDir
        Write-Host "  $dll"
    } else {
        Write-Warning "  Not found (skipping): $dll"
    }
}

# ---- copy examples -----------------------------------------------------------
Write-Step "Copying example .scad files"
$examplesSrc = Join-Path $repoRoot "docs\sample_codes"
if (Test-Path $examplesSrc) {
    $examplesDst = Join-Path $stageDir "docs\sample_codes"
    New-Item $examplesDst -ItemType Directory -Force | Out-Null
    Get-ChildItem $examplesSrc -Filter "*.scad" | ForEach-Object { Copy-Item $_.FullName $examplesDst }
    $n = @(Get-ChildItem $examplesDst -Filter "*.scad").Count
    Write-Host "  $n file(s) copied"
} else {
    Write-Warning "  docs\sample_codes not found - skipping examples"
}

# ---- 7-Zip archive -----------------------------------------------------------
Write-Step "Creating 7z archive"
if (Test-Path $archFile) { Remove-Item $archFile }
& $sevenExe a -mx=9 -mmt=on $archFile "$stageDir\*"
if ($LASTEXITCODE -ne 0) { Fail "7z archive creation failed" }

# ---- assemble SFX exe --------------------------------------------------------
Write-Step "Assembling self-extracting exe"

$sfxConfig = ";!@Install@!UTF-8!" + "`n" +
             "Title=`"3DScad Portable`"" + "`n" +
             "RunProgram=`"3DScad.exe`"" + "`n" +
             ";!@InstallEnd@!"

if (Test-Path $outputExe) { Remove-Item $outputExe }

$sfxBytes = [IO.File]::ReadAllBytes($sevenSfx)
$cfgBytes = [Text.Encoding]::UTF8.GetBytes($sfxConfig)
$arcBytes = [IO.File]::ReadAllBytes($archFile)
[IO.File]::WriteAllBytes($outputExe, $sfxBytes + $cfgBytes + $arcBytes)

# ---- clean up ----------------------------------------------------------------
Remove-Item $stageDir -Recurse -Force
Remove-Item $archFile

# ---- done --------------------------------------------------------------------
$sizeMB = [math]::Round((Get-Item $outputExe).Length / 1MB, 1)
Write-Host ""
Write-Host "Done!  $outputExe  ($sizeMB MB)" -ForegroundColor Green
Write-Host "On first launch it will extract to a temp folder and open automatically." -ForegroundColor DarkGray
