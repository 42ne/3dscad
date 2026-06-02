# 3D Viewport UX Improvement Roadmap

Status as of: 2026-06-02.

Planned usability enhancements for `ViewportWidget` (the 3D preview/render panel),
with cross-references to the existing render backends and interaction code.

---

## Already Implemented

| Feature | Details |
|---|---|
| **Camera orbit** | Left-button drag; yaw/pitch with 0.45×/0.35× sensitivity |
| **Camera pan** | Right-button drag; world-space delta scaled by `distance/420` |
| **Camera zoom** | Mouse wheel; nonlinear sensitivity; distance clamped to [2, 8000] |
| **Perspective / Orthographic** | Toggle via "Ortho" checkbox in viewport |
| **Dual render backend** | Software (QPainter) and OpenGL (GL shader); toggle via "OpenGL" checkbox |
| **Dark / Light theme** | Toggle via "Dark" checkbox |
| **Color variants** | 5 presets: Neutral, Mint, Clay, Steel, Amber |
| **Lighting presets** | 4 presets: Studio, Soft, Side, Contrast — each with 3 lights + ambient + specular |
| **Custom appearance themes** | Load/save via `ViewportAppearanceTheme` and theme editor dialog |
| **Selection (single)** | Shape index and group ID selection; viewport → tree sync |
| **Polyhedron element selection** | Face/point sub-element picking with move tool |
| **Selection breadcrumb bar** | Clickable tree-path chips below info panel |
| **Glass info panel** | Top-left overlay with help text, CSG status, renderer name |
| **Axis gizmo (compass)** | Top-right 76×76 panel; X/Y/Z axes depth-sorted |
| **Transform axes / rotation rings** | On-selection gizmo for Translate/Rotate groups |
| **Shape parameter preview** | Dimension arrows with +/- labels and value during parameter editing |
| **Contact shadows** | Planar Z-up shadow; both SW and GL backends |
| **Grid** | Minor lines ±120 every 20; X/Y/Z axis lines with labels |
| **Async CSG computation** | `QtConcurrent::run()` with deferred recomputation during drag |
| **Selection shimmer** | Animated glow on selected objects (OpenGL only) |
| **Thumbnail rendering** | Off-screen scene thumbnail with silhouette edge darkening |
| **Viewport control bar** | Checkboxes and combos positioned at y=70 (OpenGL, Dark, Nav UI, Ortho, Color, Lighting) |

---

## 🔴 Critical

### 1. Frame selection / Zoom-to-fit
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` — `setSelectedIndex` / `setSelectedGroupId`
- **Description:** No automatic centering on the selected object. Camera always initializes at (0,0,0) target. Add `frameSelection()` that computes AABB of selected shape/group and adjusts camera distance + target to fit it in view. Bind to a toolbar button and keyboard shortcut (F).
- **Test:** Select a shape at (100,100,100), press F, verify camera target moves to (100,100,100) and distance is appropriate.

### 2. Viewport keyboard shortcuts
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` — `keyPressEvent` (not overridden)
- **Description:** Override `keyPressEvent` for common shortcuts: F (frame selection), R (reset view), Ctrl+0 (zoom reset), 1-6 (preset views), W (wireframe toggle), G (grid toggle), B (toggle backend), V (toggle ortho).
- **Test:** Place cursor in viewport, press F, verify frame-selection occurs.

---

## 🟡 High Priority

### 3. 3D ray-cast picking in OpenGL mode
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` — `mousePressEvent` / `paintGL`, `viewportglrenderer.cpp`
- **Description:** Currently the pick buffer is produced only by the software renderer. When OpenGL is active, picking runs a full software rendering pass behind the scenes just to generate the pick buffer, and it can become stale. Implement proper `glReadPixels`-based picking in the GL backend — render shape IDs as flat colors to an offscreen FBO, read back the pixel under the cursor, and map it to a shape index.
- **Test:** Toggle OpenGL on, click a shape, verify correct selection. The software backend fallback buffer must be bypassed.

### 4. Camera preset views
- **Status:** ❌ Not started
- **File:** `viewportcamera.h` / `viewportwidget.cpp`
- **Description:** Add preset view buttons to the viewport overlay: Front, Top, Right, Back, Bottom, Left. Each sets a fixed yaw/pitch pair and optionally resets target to (0,0,0) or the selection centroid. Could be a small popup toolbar or a radial menu.
- **Test:** Click "Front" → camera shows +Z as front. Click "Top" → bird's-eye view.

### 5. Scale gizmo
- **Status:** ❌ Not started
- **File:** `viewportaxisgizmo.h` / `viewportaxisgizmo.cpp`
- **Description:** When a `Scale` group is selected, draw a scale gizmo (three axis handles + uniform center cube). The drag event should produce uniform or axis-constrained scale deltas. Detect the group operation type in `pickSelectedTransformAxis()`.
- **Test:** Select a Scale group, drag the X handle, verify the mesh stretches on X only.

### 6. Multi-select in viewport
- **Status:** ❌ Not started
- **File:** `viewportwidget.h` — `m_selectedIndex` / `m_selectedGroupId` are scalar
- **Description:** Replace scalar selection with `QSet<int>` for shape indices and group IDs. Ctrl+click toggles, Shift+click adds range. Emit multi-select signals to the tree and CSG pipeline.
- **Test:** Ctrl+click two cubes, both highlighted, code editor shows both selected.

### 7. Wireframe rendering mode
- **Status:** ❌ Not started
- **File:** `viewportglrenderer.cpp` / `viewportsoftwarerenderer.cpp`
- **Description:** Add a wireframe toggle that renders mesh edges instead of (or overlaid on) solid faces. In GL: `glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)` or an additional edge VBO. In SW: draw un-filled triangles with edge only.
- **Test:** Toggle wireframe, verify all meshes show as edge-only.

### 8. Viewport context menu
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` — `contextMenuEvent`
- **Description:** Right-click currently only pans. Add a context menu with: Frame Selection, Reset View, Preset Views submenu, Wireframe toggle, Fullscreen, Copy Screenshot.
- **Test:** Right-click on empty area → menu appears with expected options.

---

## 🟠 Medium Priority

### 9. Gizmo snapping (grid snap, angle snap)
- **Status:** ❌ Not started
- **File:** `viewportaxisgizmo.cpp` — drag delta computation
- **Description:** Add a snap toggle to the viewport control bar. When active, translate deltas snap to a grid (e.g. 5 mm), rotate deltas snap to angle increments (e.g. 15°). The preview labels should show snapped values.
- **Test:** Enable snap, drag a translate axis, verify position jumps in 5 mm increments.

### 10. Touch / gesture support
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` — `touchEvent` / `gestureEvent`
- **Description:** Currently only mouse events are handled. Add pinch-to-zoom (two-finger), two-finger orbit, and single-finger pan. Use `QTouchEvent` or `QGestureEvent`.
- **Test:** On a touch device, pinch to zoom in/out, two-finger drag to orbit.

### 11. Viewport screenshot export
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` — new `exportScreenshot()` method
- **Description:** Bind Ctrl+E or a toolbar button to save the current viewport contents as a PNG file via `QOpenGLWidget::grabFramebuffer()` or `QWidget::grab()`. Include a resolution multiplier (1×, 2×, 4×) for high-res exports.
- **Test:** Press Ctrl+E, choose filename, verify PNG matches viewport.

### 12. Selection box / lasso
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` — `mousePressEvent` / `mouseMoveEvent`
- **Description:** When Shift+drag on empty space, draw a rubber-band rectangle. On release, select all shapes whose projected AABB intersects the rectangle. For a lasso, hold Ctrl+Shift and draw a freehand polygon.
- **Test:** Shift-drag a rectangle around two cubes → both selected.

### 13. Fullscreen toggle
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` / `mainwindow.cpp`
- **Description:** Hide main window chrome (toolbar, dock widgets, status bar) and expand the viewport to fill the screen. Toggle via F11 or a button in the viewport overlay.
- **Test:** Press F11 → viewport fills entire screen. Press F11 again → restore layout.

### 14. Camera animation / smoothing
- **Status:** ❌ Not started
- **File:** `viewportcamera.cpp` / `viewportwidget.cpp` — new timer
- **Description:** Animate camera transitions (frame selection, preset views, reset) with an easing curve over ~300 ms. Use a 16 ms timer with linear interpolation or `QEasingCurve::OutCubic`.
- **Test:** Click a preset view → camera glides smoothly to the new orientation.

### 15. Viewport overlay as proper toolbar
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` — `updateViewportControls`
- **Description:** Currently controls are positioned with absolute geometry at y=70. Replace with a real toolbar/overlay that uses layout, supports overflow, and can be dragged/repositioned.
- **Test:** Resize window → controls reflow gracefully instead of overlapping.

### 16. Frustum culling
- **Status:** ❌ Not started
- **File:** `viewportglrenderer.cpp` — `renderPreview()`
- **Description:** Compute view frustum planes from the projection × view matrix. Skip rendering meshes whose bounding sphere is entirely outside the frustum. Implement as a filter in the render loop.
- **Test:** Zoom in close to a small object, verify off-screen meshes are not submitted to the GPU.

### 17. X-Ray / see-through mode
- **Status:** ❌ Not started
- **File:** `viewportglrenderer.cpp` — new shader / render pass
- **Description:** A toggle that renders all objects with transparency (e.g. alpha=0.3) so internal structure is visible. In GL this can be done by drawing back faces first, then front faces with blending. In SW, reduce triangle opacity.
- **Test:** Toggle X-Ray, verify all meshes appear semi-transparent.

### 18. Grid snapping toggle in viewport controls
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` — new checkbox in control bar
- **Description:** Add an explicit "Snap" checkbox alongside the other controls, independent of the gizmo snap feature. When active, show snap indicators on the grid (highlighted intersection points).
- **Test:** Enable snap, drag an object, verify position snaps to grid points.

---

## 🟢 Low Priority

### 19. MSAA / anti-aliasing control
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` — `initializeGL` / `QSurfaceFormat`
- **Description:** Add a setting to control multi-sample anti-aliasing (2×, 4×, 8×, off). Requires `QSurfaceFormat::setSamples()` before widget creation, with a restart or toggle mechanism.
- **Test:** Set 4× MSAA, verify jagged edges are reduced.

### 20. Measurement / distance tool
- **Status:** ❌ Not started
- **File:** New tool overlay
- **Description:** Click-to-place measurement markers that display the distance between two 3D points. Draw a dashed line and a floating label. Persist markers per scene or clear on toggle-off.
- **Test:** Place two markers, verify distance label matches actual 3D distance.

### 21. Screen-space ambient occlusion (SSAO)
- **Status:** ❌ Not started
- **File:** `viewportglrenderer.cpp` — new shader
- **Description:** Add a post-processing SSAO pass that samples the depth buffer to approximate ambient occlusion in creases and corners. Toggleable from a checkbox.
- **Test:** Toggle SSAO, verify contact shadows appear in corners of a concave shape.

### 22. LOD / mesh decimation
- **Status:** ❌ Not started
- **File:** `scenemesh.cpp` / `viewportglrenderer.cpp`
- **Description:** Generate lower-detail versions of sphere/cylinder meshes based on camera distance. Use a simple reduction in segment counts for primitives. Avoid recalculating full-resolution CSG for every viewport change.
- **Test:** Zoom out, verify far-away objects use fewer triangles.

### 23. VR / stereoscopic rendering
- **Status:** ❌ Not started
- **File:** New render path
- **Description:** Side-by-side stereoscopic rendering for VR headsets (OpenVR/SteamVR integration) or simple anaglyph (red-cyan glasses). Very low priority — pure novelty.
- **Test:** Put on VR headset, verify correct stereo separation.

### 24. Face/edge/vertex snapping
- **Status:** ❌ Not started
- **File:** `viewportaxisgizmo.cpp` — drag delta computation
- **Description:** During transform drag, snap the gizmo position to nearby face centers, edge midpoints, or vertex positions of other meshes. Show a snap indicator (glowing dot) on the snapped target.
- **Test:** Drag a cube near another cube's face → gizmo snaps to face center.

### 25. Camera zoom limits indicator
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` — `wheelEvent`
- **Description:** When the zoom hits the [2, 8000] clamp, briefly flash a visual indicator or show a subtle shake. Currently the zoom silently stops.
- **Test:** Zoom in as close as possible, verify a "max zoom" indicator appears.

### 26. Middle-button orbit option
- **Status:** ❌ Not started
- **File:** `viewportwidget.cpp` — `mousePressEvent` / `mouseMoveEvent`
- **Description:** Some users prefer middle-button for orbit and left-button for selection/pan. Add a settings toggle to swap the left/middle button roles.
- **Test:** Enable middle-button orbit, click+drag middle button → camera orbits, left button → pan.

---

## Implementation Order (Suggested)

1. **Frame selection / Zoom-to-fit (#1)** — trivial, unlocks everyday workflow
2. **Viewport keyboard shortcuts (#2)** — needed for power users
3. **Ray-cast picking in GL mode (#3)** — correctness fix for OpenGL backend
4. **Camera preset views (#4)** — low code risk
5. **Scale gizmo (#5)** — fills a gap in the existing gizmo system
6. **Wireframe mode (#7)** — simple visual toggle
7. **Camera animation (#14)** — polish for #1, #4, and camera reset
8. **Context menu (#8)** — common expectation
9. **Fullscreen (#13)** — small code change, big impact
10. **Screenshot export (#11)** — low effort, useful for sharing
11. **Multi-select (#6)** — needed for bulk operations
12. **Gizmo snapping (#9)** — precise positioning
13. **Viewport overlay toolbar (#15)** — maintainability
14. **Touch/gesture (#10)** — tablet/laptop users
15. **X-Ray mode (#17)** — visual debugging
16. **Selection box/lasso (#12)** — bulk selection
17. **Frustum culling (#16)** — performance for large scenes
18. **Grid snap toggle (#18)** — usability
19. **Measurement tool (#20)** — utility
20. **MSAA control (#19)** — visual quality
21. **Middle-button orbit (#26)** — user preference
22. **Face/edge/vertex snapping (#24)** — advanced precision
23. **SSAO (#21)** — visual polish
24. **Zoom limits indicator (#25)** — minor feedback
25. **LOD (#22)** — performance for large scenes
26. **VR (#23)** — novelty
