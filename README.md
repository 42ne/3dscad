# OpenSCAD Visual Editor Prototype

Prototype of a visual editor for OpenSCAD-style modeling. The goal is a Tinkercad-like interface where UI actions update OpenSCAD code, and supported OpenSCAD code can be applied back into the visual scene.

## Current State

The project is a Qt Widgets application using a custom `QOpenGLWidget` viewport. Rendering currently uses a software rasterizer through `QPainter`, with an explicit backend switch prepared for an optional OpenGL path.

Implemented:

- Scene tree with cube, sphere, and cylinder primitives.
- Scene tree displays the generated boolean structure as `union`, `difference`, and `intersection` groups.
- `SceneDocument` has an explicit tree-node hierarchy that is updated incrementally for add/delete/boolean-mode changes.
- Group tree nodes store position and rotation transforms for upcoming group editing.
- `SceneDocument` exposes group operations for add/remove/move, with undo/redo commands ready for UI wiring.
- The scene tree can create explicit `union`, `difference`, and `intersection` groups and move tree nodes between them.
- Scene-tree context menus expose group creation plus shape/group deletion near the selected node.
- Scene-tree refreshes preserve selected groups when no primitive is selected.
- `difference()` and `intersection()` children are labeled in the tree so base/cut/mask roles are visible.
- The properties panel shows the selected primitive's effective tree role, and transform/parameter edits no longer overwrite that role.
- Selecting a group enables position/rotation editing for that group through undoable property changes.
- Selected groups can be moved from the viewport with the same axis gizmo used for primitives.
- Scene-tree drag/drop defers model updates until after Qt finishes the drop event to avoid transient disappearing rows.
- Moving nodes between groups preserves their world position for translation-only group transforms.
- OpenSCAD generation and Manifold CSG preview read the explicit document tree.
- OpenSCAD generation and Manifold CSG apply group transforms from the document tree.
- CSG preview detects boolean operations from the explicit tree, not only from legacy per-shape boolean flags.
- CSG preview also uses the explicit tree when group transforms are present, so transformed plain `union()` groups move as a unit.
- Shape properties for position, rotation, size, radius, height, and boolean mode.
- Undo/redo for add, delete, property changes, viewport drag, and code apply.
- OpenSCAD generation for the supported scene subset.
- Parser for the generated OpenSCAD subset.
- Interactive viewport orbit/zoom.
- Depth-tested triangle rendering.
- Explicit viewport render backend plumbing; software is the active backend, while `CONFIG+=opengl_renderer` enables the placeholder OpenGL backend path for future work.
- Simple lights and shadows.
- Shape picking in the viewport.
- Move gizmo with X/Y/Z axes.
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

- Is used first when `build/manifold-build/src/libmanifold.a` exists at qmake time.
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
- MinGW 32-bit
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

With Qt's MinGW GCC 8, current Manifold may require local sequential fallbacks in `build/manifold-src/src/parallel.h` for `std::reduce`, `std::inclusive_scan`, and `std::exclusive_scan`.

## Limitations

- The viewport renderer is still a software rasterizer inside `QOpenGLWidget`, not a full OpenGL mesh pipeline.
- OpenSCAD parser supports only the generated subset.
- Manifold is currently an optional local build, not a vendored/submodule dependency.
- Mesh approximate fallback is not exact.
- Box CSG only handles axis-aligned cubes.
- No export pipeline yet.
- No node graph or operation tree UI yet.
- Boolean tree UI supports moving shapes between the generated `union`, `difference`, and `intersection` groups by drag/drop. Shape movement still edits the flat per-shape boolean mode, and the document tree is updated from that operation.

## Next Good Steps

1. Formalize Manifold dependency setup: submodule, bootstrap script, or CMake migration.
2. Refine scene-tree editing: rename groups, reorder nodes, and improve visual distinction between service root and user groups.
3. Implement the optional OpenGL backend with vertex/index buffers and GPU picking.
4. Add OpenSCAD CLI integration for validation/export.
5. Improve parser into an AST-based roundtrip layer.
