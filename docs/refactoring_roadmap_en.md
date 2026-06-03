# Codebase Refactoring Roadmap

Status as of: 2026-06-03.

Ongoing decomposition of the `ViewportWidget` and `SceneTreeGraphicsWidget` god-classes into
focused subsystem controllers, plus utility consolidation and correctness fixes.

---

## Phase 1 — Viewport Extractions

| Subsystem | New class / file | Lines extracted | Status |
|---|---|---|---|
| Software renderer | `ViewportSoftwareRenderer` (`viewportsoftwarerenderer.h/.cpp`) | ~297 | ✅ Done |
| OpenGL renderer | `ViewportGLRenderer` (`viewportglrenderer.h/.cpp`) | ~606 | ✅ Done |
| Axis gizmo | `ViewportAxisGizmo` (`viewportaxisgizmo.h/.cpp`) | ~468 | ✅ Done |
| Transform / shape overlay preview | `ViewportOverlayPreview` (`viewportoverlaypreview.h/.cpp`) | ~202 | ✅ Done |
| Camera model | `ViewportCamera` (`viewportcamera.h/.cpp`) | ~44 | ✅ Done |
| Named constants | `ViewportConstants` (`viewportconstants.h`) | ~26 | ✅ Done |

**Result:** `ViewportWidget` reduced from ~3 700 lines to ~1 042 lines (−72 %).

---

## Phase 2 — Utility Deduplication

| Item | Before | After | Status |
|---|---|---|---|
| `isValidIdentifier` | 3 copies scattered across files | Inline in `scenestringutils.h` | ✅ Done |
| `splitAtTopLevelCommas` | Duplicated in `openscadparser.cpp` | Removed; canonical copy remains | ✅ Done |
| Number-adjustment preamble | 4 identical preambles in `SceneController` | `adjustedNumberReplacement` helper + `setCtrlHighlight` method | ✅ Done |

---

## Phase 3 — Scene Tree God-Class Decomposition

`SceneTreeGraphicsWidget` started at ~6 195 lines. Each subsystem below was extracted as a
friend class following the same pattern established in Phase 1.

### 3.1 Already-extracted subsystems

| Subsystem | File | Lines | Status |
|---|---|---|---|
| Color-edit mode | `scenetreecoloreditmode.h/.cpp` | ~1 135 | ✅ Done |
| Wheel handler (Ctrl+scroll 8 cases) | `scenetreewheelhandler.h/.cpp` | ~183 | ✅ Done |
| Hover manager (tooltips, highlights) | `scenetreehovermanager.h/.cpp` | ~1 192 | ✅ Done |
| Hit-test manager (all `*ControlAt` fns) | `scenetreehittestmanager.h/.cpp` | ~278 | ✅ Done |
| Inline editor (expression inline edit) | `scenetreeinlineeditor.h/.cpp` | ~157 | ✅ Done |
| Canvas controller (pan / zoom) | `scenetreecanvascontroller.h/.cpp` | ~174 | ✅ Done |
| Drop preview controller | `scenetreedroppreviewcontroller.h/.cpp` | ~267 | ✅ Done |
| Overlay controller (glass panels) | `scenetreeoverlaycontroller.h/.cpp` | ~381 | ✅ Done |
| Toolbar renderer | `scenetreetoolbarrenderer.h/.cpp` | ~192 | ✅ Done |
| Expression layout | `scenetreeexpressionlayout.h/.cpp` | ~440 | ✅ Done |
| Node renderer | `scenetreenoderenderer.h/.cpp` | ~1 605 | ✅ Done |
| Icon painter | `scenetreetoolbarrenderer.h/.cpp` | ~392 | ✅ Done |
| Preview geometry | `scenetreepreviewgeometry.h/.cpp` | ~227 | ✅ Done |
| Preview renderer | `scenetreepreviewrenderer.h/.cpp` | ~395 | ✅ Done |
| Canvas drag handler | `scenetreegraphicswidget_canvasdrag.cpp` | ~299 | ✅ Done |

### 3.2 Constant and type consolidation

| Item | Details | Status |
|---|---|---|
| `CanvasBackgroundTheme` struct + helpers | Unified from 2 anonymous-namespace duplicates into `SceneTreeGraphics` namespace in `scenetreegraphicsconstants.h` / `scenetreegraphicshelpers` | ✅ Done |
| Overlay Z / margin constants | `OverlayBaseZ`, `HintOverlayZ`, `OverlayMargin`, `OverlayBottomGap` centralized; replaced per-file literals in 3 files | ✅ Done |
| `ExpressionEditTarget` struct | Moved from `SceneTreeGraphicsWidget` inner class to global scope in `scenetreeinlineeditor.h`; widget uses `using` alias | ✅ Done |
| Dead code removal from `widget.cpp` | ~15 unused functions + 3 constants removed (drop preview math, color lerp, interpolated rects, etc.) | ✅ Done |

**Result:** `SceneTreeGraphicsWidget` reduced from ~6 195 lines to ~1 812 lines (−71 %).

### 3.3 Remaining

| Task | Target file | Approx. lines | Status |
|---|---|---|---|
| `SceneTreeNodeRenderer` split | `scenetreenoderenderer.cpp` (~1 605 lines) → painting utils, layout math, expression render, preview sizing | ~1 605 | ❌ Not started |
| `SceneTreeHoverManager` growth | `scenetreehovermanager.cpp` grew from estimated ~450 to ~1 192 lines after hint overlay additions — candidate for further split | ~1 192 | ❌ Not started |

---

## Phase 4 — Correctness Fixes (tied to refactoring work)

These bugs were exposed during the god-class decomposition work and fixed in the same session.

| Bug | Root cause | Fix | Status |
|---|---|---|---|
| `$fn=` disappears on Apply Code | Parser regex `[A-Za-z_]` rejected `$` prefix | Changed to `\\$?[A-Za-z_]` in `openscadparser.cpp` | ✅ Fixed |
| `$fn` not affecting viewport geometry | `buildCsgPreview(scene)` fell through to `buildCsgPreview(shapes)` which had no `fn` parameter | Threaded `fn` through entire flat-shapes path in `csgevaluator.cpp` | ✅ Fixed |
| `center` checkbox ignored in Manifold CSG | `Manifold::Cube/Cylinder` hardcoded `center=true` | Changed to `shape.center` in `manifoldcsg.cpp` | ✅ Fixed |
| `center` checkbox ignored in box intersection tests | `containsPoint` for Cube/Square/Cylinder/Cone did not offset by half-size when `center=true` | Fixed boundary tests in `csgevaluator.cpp` | ✅ Fixed |
| Viewport not updating after center toggle | `refreshSceneViews()` did not call `invalidateCsgPreview()` | Added call in `mainwindow.cpp` | ✅ Fixed |
| GL VBO not refreshing after center toggle | `shapeFingerprint` missing `shape.center`, `shape.radius2`, polyhedron data → same `meshKey` → VBO upload skipped | Extended `shapeFingerprint` in `viewporthelpers.cpp` | ✅ Fixed |

---

## File Size Overview (current)

### Scene tree subsystem

| File | Lines |
|---|---|
| `scenetreegraphicswidget.cpp` | 1 812 |
| `scenetreenoderenderer.cpp` | 1 605 |
| `scenetreecoloreditmode.cpp` | 1 135 |
| `scenetreehovermanager.cpp` | 1 192 |
| `scenetreepreviewrenderer.cpp` | 395 |
| `scenetreeexpressionlayout.cpp` | 440 |
| `scenetreeoverlaycontroller.cpp` | 381 |
| `scenetreeiconpainter.cpp` | 392 |
| `scenetreepreviewgeometry.cpp` | 227 |
| `scenetreepalette.cpp` | 290 |
| `scenetreedroppreviewcontroller.cpp` | 267 |
| `scenetreehittestmanager.cpp` | 278 |
| `scenetreeoverlaygraphicsitems.h` | 105 |
| `scenetreeinlinetextinput.cpp` | 281 |
| `scenetreelayout.cpp` | 503 |
| `scenetreecanvascontroller.cpp` | 174 |
| `scenetreewheelhandler.cpp` | 183 |
| `scenetreeinlineeditor.cpp` | 157 |
| `scenetreegraphicswidget_canvasdrag.cpp` | 299 |
| `scenetreeglasspanelhelpers.cpp` | 68 |
| `scenetreecanvasgraphics.cpp` | 105 |
| `scenetreetoolbarrenderer.cpp` | 192 |
| `scenetree.cpp` | 615 |
| `scenetreetoolmetadata.cpp` | 245 |

### Viewport subsystem

| File | Lines |
|---|---|
| `viewporthelpers.cpp` | 1 082 |
| `viewportwidget.cpp` | 1 042 |
| `viewportglrenderer.cpp` | 606 |
| `viewportaxisgizmo.cpp` | 468 |
| `viewportsoftwarerenderer.cpp` | 297 |
| `viewportoverlaypreview.cpp` | 202 |
| `viewportcamera.cpp` | 23 |

---

## Suggested Next Steps

1. **Split `SceneTreeNodeRenderer`** — at 1 605 lines it is the largest remaining single-purpose file. Candidate split: primitive drawing helpers, group frame drawing, expression widget rendering, preview sizing calculations.
2. **Split `SceneTreeHoverManager`** — grew from ~450 to ~1 192 lines after hint overlay additions. Could extract hint overlay rendering into a separate `SceneTreeHintOverlay` class.
3. **Split `viewporthelpers.cpp`** — 1 082 lines; contains mesh building, shape fingerprinting, interaction mesh logic, and string utilities — could become 2–3 files.
