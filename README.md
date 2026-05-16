# OpenSCAD Visual Editor Prototype

Prototype of a visual editor for OpenSCAD-style modeling. The goal is a Tinkercad-like interface where UI actions update OpenSCAD code, and supported OpenSCAD code can be applied back into the visual scene.

## Current State

The project is a Qt Widgets application using a custom `QOpenGLWidget` viewport. Rendering currently uses a software rasterizer through `QPainter`, with an explicit backend switch prepared for an optional OpenGL path.

Implemented:

- Scene tree with cube, sphere, and cylinder primitives.
- Scene tree displays the generated boolean structure as a root `module scene_model` with `union`, `difference`, and `intersection` groups.
- The document root is a non-deletable `module scene_model` group, so dropping a top-level group no longer creates an accidental implicit wrapper.
- `SceneDocument` has an explicit tree-node hierarchy that is updated incrementally for add/delete/boolean-mode changes.
- Group tree nodes store position and rotation transforms for upcoming group editing.
- `SceneDocument` exposes group operations for add/remove/move, with undo/redo commands ready for UI wiring.
- The scene tree can create explicit `union`, `difference`, and `intersection` groups and move tree nodes between them.
- Experimental graphics tree preview based on `QGraphicsScene`, shown beside the classic tree.
- The graphics tree has an in-scene palette for cube, sphere, cylinder, union, difference, intersection, and module tools.
- Graphics-tree drag/drop can add new primitives/groups and move existing tree nodes between groups with explicit insertion positions.
- Graphics-tree selection is synchronized with the classic tree, 3D viewport selection, Properties dock, and generated OpenSCAD code highlight.
- Graphics-tree primitives use shape icons plus stable object numbers instead of text-only cards.
- Graphics-tree groups use operation icons, nested panels, and dedicated `difference` base/cut regions with reserved cut-space even when empty.
- Graphics-tree live drag preview now shows future container expansion, source/target reserved slots, real toolbar-drop previews, and a snapshot of the moved node instead of a second temporary document node.
- Active graphics-tree drags use a green dashed focus outline: an ellipse for primitives and a rounded rectangle for group blocks.
- Graphics-tree move preview suppresses self-drop and removes the moved node from its source container preview while dragging.
- Graphics-tree supports `Delete`/`Backspace` for the selected tree node through the same undoable commands as the classic tree.
- The graphics tree uses a dark grid canvas with mouse panning and hidden scroll bars.
- Scene-tree context menus expose group creation plus shape/group deletion near the selected node.
- Scene-tree refreshes preserve selected groups when no primitive is selected.
- `difference()` and `intersection()` children are labeled in the tree so base/cut/mask roles are visible.
- The properties panel shows the selected primitive's effective tree role, and transform/parameter edits no longer overwrite that role.
- Selecting a group enables position/rotation editing for that group through undoable property changes.
- Selected primitives and groups can be moved from the viewport with axis gizmo arrows and rotated with the gizmo rings.
- Scene-tree drag/drop defers model updates until after Qt finishes the drop event to avoid transient disappearing rows.
- Moving nodes between groups preserves their world position for translation-only group transforms.
- OpenSCAD generation and Manifold CSG preview read the explicit document tree.
- OpenSCAD generation and Manifold CSG apply group transforms from the document tree.
- CSG preview detects boolean operations from the explicit tree, not only from legacy per-shape boolean flags.
- CSG preview also uses the explicit tree when group transforms are present, so transformed plain `union()` groups move as a unit.
- Shape properties for position, rotation, size, radius, height, and boolean mode.
- Undo/redo for add, delete, property changes, viewport drag, and code apply.
- OpenSCAD generation for the supported scene subset, wrapped as `module scene_model()` plus a call.
- OpenSCAD source mapping highlights the currently selected tree node in the code editor.
- Parser for the generated OpenSCAD subset, including the no-parameter module wrapper and call.
- Interactive viewport orbit/zoom.
- Depth-tested triangle rendering.
- Explicit viewport render backend plumbing with a `Use OpenGL` checkbox; software is the active backend by default, and the checkbox enables an experimental OpenGL mesh path.
- Simple lights and shadows.
- Shape picking in the viewport.
- Transform gizmo with X/Y/Z move axes and rotation rings.
- Boolean modes:
  - `Add solid`
  - `Subtract hole`
  - `Intersect mask`
- CSG preview status in the viewport and the left panel.
- Optional Manifold-backed exact mesh CSG preview when a local Manifold build is available.
- OpenSCAD preview bridge that writes `openscad_preview.scad` for live reload in OpenSCAD.

## CSG Preview

There are currently four preview modes:

- `plain mesh`: no boolean operation is active.
- `Manifold exact mesh`: real mesh boolean evaluation through the optional Manifold backend.
- `box mode`: real computed CSG for unrotated cubes.
- `mesh approximate`: approximate CSG for spheres, cylinders, and rotated cubes.

Manifold mode:

- Is used first when the matching local Manifold library exists at qmake time:
  `build/manifold-build-32/src/libmanifold.a` for 32-bit kits or
  `build/manifold-build-64/src/libmanifold.a` for 64-bit kits.
- Evaluates the scene boolean tree as mesh booleans.
- Falls back to the internal box/mesh preview if Manifold is not built or returns an invalid result.

Box mode:

- Works with unrotated cubes.
- Computes subtract and intersect operations as box volume operations.
- Builds a surface mesh from occupied cells.
- Removes internal faces.
- Merges coplanar face cells into larger rectangles.

Mesh approximate mode:

- Works as a first preview for non-box shapes.
- Filters triangles by centroid against subtract/intersect helper volumes.
- Adds approximate subtract cut faces for some helper shapes, but is not a robust boolean solver.

During drag, CSG evaluation is paused and the viewport shows a lightweight interaction preview. Full CSG preview recomputes after the drag is finished.

## Build

This is a Qt `.pro` project.

Known working setup:

- Qt 5.15.2
- MinGW 32-bit or 64-bit
- qmake project: `3DScad.pro`

Typical build from Qt Creator should work. From the existing debug build directory, the project has been verified with:

```powershell
qmake ..\..\3DScad.pro -spec win32-g++ "CONFIG+=debug" "CONFIG+=qml_debug"
mingw32-make -f Makefile.Debug
```

Optional Manifold CSG backend:

```powershell
.\scripts\build-manifold.ps1
qmake 3DScad.pro
mingw32-make
```

Use `.\scripts\build-manifold.ps1 -Arch 32` for a 32-bit Qt kit or
`.\scripts\build-manifold.ps1 -Arch 64` for a 64-bit Qt kit. The default is 64-bit.

With Qt's MinGW GCC 8, current Manifold may require local sequential fallbacks in `build/manifold-src/src/parallel.h` for `std::reduce`, `std::inclusive_scan`, and `std::exclusive_scan`.

The `Use OpenGL` checkbox enables the optional experimental viewport backend. In this mode solid scene meshes are drawn through an OpenGL shader path with depth testing. Grid, gizmos, helper wireframes, text overlay, CPU-side projection, and the pick buffer still use the existing software path, so large performance gains are not expected yet.

## Limitations

- The OpenGL viewport path is still experimental and mixed with QPainter overlays; it does not yet use persistent VBO/index buffers or GPU picking.
- OpenSCAD parser supports only the generated subset.
- Manifold is currently an optional local build, not a vendored/submodule dependency.
- Mesh approximate fallback is not exact.
- Box CSG only handles axis-aligned cubes.
- No export pipeline yet.
- The graphics tree is still a prototype: it coexists with the classic tree, its drag preview is still being refined, and its toolbar is intentionally embedded in the scene for experimentation.
- Shape boolean mode is still present as a legacy/simple editing control and can rewrite primitive placement in the explicit tree.

## Next Good Steps

1. Formalize Manifold dependency setup: submodule, bootstrap script, or CMake migration.
2. Refine graphics-tree editing: rename groups, add explicit reorder affordances, keep improving difference base/cut behavior, and decide when the classic tree can be hidden.
3. Implement the optional OpenGL backend with vertex/index buffers and GPU picking.
4. Add OpenSCAD CLI integration for validation/export.
5. Improve parser into an AST-based roundtrip layer.
