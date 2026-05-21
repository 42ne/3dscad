# Manifold Builder

`tools/manifoldbuilder` is a small Qt Widgets utility for building the optional
Manifold CSG backend used by the main editor.

It is intentionally a GUI wrapper around `scripts/build-manifold.ps1`, so the
actual build logic stays in one script and can still be run from PowerShell.

## Building the Tool

Open `tools/manifoldbuilder/manifoldbuilder.pro` in Qt Creator and build it
with a Qt MinGW kit. A Debug build is fine.

From a Qt-enabled shell:

```powershell
qmake tools\manifoldbuilder\manifoldbuilder.pro -o tools\manifoldbuilder\Makefile
mingw32-make -C tools\manifoldbuilder
```

## Using the Tool

Launch `ManifoldBuilder.exe`.

Fields:

| Field | Meaning |
| --- | --- |
| Repo root | Repository folder containing `3DScad.pro` and `scripts/build-manifold.ps1`. |
| Qt root | Qt installation root, e.g. `E:\Qt` or `C:\Qt`. |
| Architecture | Selects `build/manifold-build-32` or `build/manifold-build-64`. Match this to the Qt kit used for the main app. |
| CMake generator | Currently defaults to `MinGW Makefiles`, matching the script. |
| Clean build directory | Deletes only `build/manifold-build-<arch>` before running the script. The downloaded `build/manifold-src` checkout is kept. |

Click **Build Manifold**. The log panel streams the PowerShell output.

When successful, the output library is:

```text
build/manifold-build-32/src/libmanifold.a
```

or:

```text
build/manifold-build-64/src/libmanifold.a
```

After that, rerun `qmake` for `3DScad.pro` so the main project sees the local
library and defines `HAVE_MANIFOLD_CSG`.

## Notes

- The first build may clone Manifold from GitHub, so it needs network access.
- Qt MinGW GCC 8 may need the fallback patch in `src/parallel.h`; the existing
  PowerShell script applies that patch automatically.
- Build artifacts stay under `build/` and are intentionally not committed.
