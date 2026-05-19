# SFX Builder — Portable Executable Tool

`tools/sfxbuilder` is a small Qt GUI utility that packages a compiled Windows
executable into a single self-extracting portable `.exe` that requires no
installer. It automates the three steps needed for a portable Windows release:

1. Run `windeployqt` to collect all Qt DLLs and plugins.
2. Copy the MinGW runtime DLLs that `windeployqt` intentionally leaves out.
3. Pack everything with 7-Zip's SFX module so the result is one double-clickable
   file that extracts and launches the app from a temp folder.

---

## Prerequisites

| Requirement | Notes |
|---|---|
| Qt 5.15.x (MinGW 32 or 64-bit) | Same kit used to compile the main app. `windeployqt.exe` must be in the Qt bin dir. |
| 7-Zip installed | Default paths `C:\Program Files\7-Zip` and `C:\Program Files (x86)\7-Zip` are auto-detected. The console `7z.exe` **and** `7z.sfx` (LZMA2 GUI SFX) must both be present. |
| The compiled `.exe` | A Release build is strongly recommended; Debug builds include debug DLLs that inflate the package size significantly. |

---

## Building sfxbuilder

Open `tools/sfxbuilder/sfxbuilder.pro` in Qt Creator and build with the same
MinGW kit used for the main application. A Debug build is fine for the tool
itself.

---

## Using the Tool

Launch `sfxbuilder.exe`. All fields are auto-detected where possible.

### Fields

| Field | What to enter |
|---|---|
| **Source EXE** | The compiled application executable, e.g. `build/release/3DScad.exe`. Choosing a file here auto-fills _Output folder_ and searches parent directories for `docs/sample_codes`. |
| **Output folder** | Directory where the finished `<AppName>_portable.exe` will be written. Defaults to a `dist/` subfolder next to the source exe. |
| **Qt bin dir** | Qt MinGW bin directory, e.g. `C:\Qt\5.15.2\mingw81_64\bin`. Auto-filled from the Qt installation sfxbuilder itself was built with. |
| **7-Zip dir** | Directory containing `7z.exe` and `7z.sfx`. Auto-detected from the standard 7-Zip install locations. |
| **Extra folder** _(optional)_ | Any additional directory to bundle alongside the app, e.g. the `docs/sample_codes` folder. When Source EXE is chosen, sfxbuilder checks parent directories and fills this in automatically if `docs/sample_codes` exists. |
| **Bundle path** _(optional)_ | Relative path inside the bundle where the extra folder will appear, e.g. `docs/sample_codes`. Defaults to the folder name if left blank. |

### Steps

1. Fill in **Source EXE** first — most other fields auto-populate.
2. Verify **Qt bin dir** and **7-Zip dir** are correct.
3. Optionally set an **Extra folder** (e.g. sample SCAD files) and its **Bundle path**.
4. Click **Build SFX**.

The log panel shows each step. When the build succeeds a dialog shows the path
to the finished portable `.exe`.

---

## What Happens Internally

```
Source EXE
  └─► copy to _stage/
        └─► windeployqt          (Qt DLLs, plugins, platforms, …)
              └─► copy MinGW runtime DLLs from Qt bin dir:
                    libgcc_s_seh-1.dll   (64-bit) / libgcc_s_dw2-1.dll (32-bit)
                    libstdc++-6.dll
                    libwinpthread-1.dll
                      └─► copy Extra folder  (optional)
                            └─► 7z a -mx=9  _tmp.7z  _stage/*
                                  └─► cat 7z.sfx + sfx-config + _tmp.7z  →  AppName_portable.exe
_stage/ and _tmp.7z cleaned up
```

The SFX config embedded between the 7z.sfx stub and the archive sets:

```ini
;!@Install@!UTF-8!
Title="AppName Portable"
RunProgram="AppName.exe"
;!@InstallEnd@!
```

The 7z.sfx module extracts the archive to a temp folder and launches
`RunProgram` automatically. No registry writes, no installer.

---

## MinGW Runtime DLLs

`windeployqt` copies Qt-owned DLLs but deliberately skips the MinGW GCC
runtime. Without these files the app fails immediately with
_"libgcc_s_seh-1.dll was not found"_ on machines that do not have MinGW
installed.

sfxbuilder copies them from the **same Qt bin dir** that contains
`windeployqt.exe`, because the MinGW toolchain ships them there alongside the
Qt libraries. If a file is absent (e.g. the 32-bit variant on a 64-bit kit)
it is silently skipped.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| _"libgcc_s_seh-1.dll was not found"_ at launch | MinGW DLL missing from package | Rebuild with sfxbuilder version that copies MinGW DLLs; check log for "Copying MinGW runtime DLLs" step. |
| _"windeployqt failed"_ | Wrong Qt bin dir or Release/Debug mismatch | Point Qt bin dir at the MinGW bin that matches the exe's kit. |
| _"7z.sfx not found"_ | 7-Zip installed but `7z.sfx` absent | Install the full 7-Zip package, not just the standalone `7z.exe` download. |
| Package works on dev machine but fails elsewhere | Missing VC++ redist or System DLL | The app must be built with MinGW, not MSVC. MinGW DLLs are self-contained; MSVC DLLs require a separate redistributable. |
| Large portable exe (> 50 MB) | Debug build included | Rebuild the main app as Release before packaging. |
