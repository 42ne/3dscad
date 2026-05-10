# OpenSCAD Visual Editor Prototype

Prototype of a visual editor for OpenSCAD-style modeling. The goal is a Tinkercad-like interface where UI actions update OpenSCAD code, and supported OpenSCAD code can be applied back into the visual scene.

## Current State

The project is a Qt Widgets application using a custom `QOpenGLWidget` viewport with software rasterization through `QPainter`.

Implemented:

- Scene tree with cube, sphere, and cylinder primitives.
- Shape properties for position, rotation, size, radius, height, and boolean mode.
- Undo/redo for add, delete, property changes, viewport drag, and code apply.
- OpenSCAD generation for the supported scene subset.
- Parser for the generated OpenSCAD subset.
- Interactive viewport orbit/zoom.
- Depth-tested triangle rendering.
- Simple lights and shadows.
- Shape picking in the viewport.
- Move gizmo with X/Y/Z axes.
- Boolean modes:
  - `Add solid`
  - `Subtract hole`
  - `Intersect mask`
- CSG preview status in the viewport and the left panel.

## CSG Preview

There are currently three preview modes:

- `plain mesh`: no boolean operation is active.
- `box mode`: real computed CSG for unrotated cubes.
- `mesh approximate`: approximate CSG for spheres, cylinders, and rotated cubes.

Box mode:

- Works with unrotated cubes.
- Computes subtract and intersect operations as box volume operations.
- Builds a surface mesh from occupied cells.
- Removes internal faces.
- Merges coplanar face cells into larger rectangles.

Mesh approximate mode:

- Works as a first preview for non-box shapes.
- Filters triangles by centroid against subtract/intersect helper volumes.
- Does not create new cut faces yet.

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

## Limitations

- The viewport renderer is still a software rasterizer inside `QOpenGLWidget`, not a full OpenGL mesh pipeline.
- OpenSCAD parser supports only the generated subset.
- Mesh approximate CSG is not exact and does not generate cut faces.
- Box CSG only handles axis-aligned cubes.
- No export pipeline yet.
- No node graph or operation tree UI yet.

## Next Good Steps

1. Add exact mesh clipping for approximate CSG so cut faces are generated.
2. Add a real operation tree for `union`, `difference`, and `intersection` instead of flat per-shape boolean modes.
3. Move viewport rendering toward real OpenGL vertex/index buffers.
4. Add OpenSCAD CLI integration for validation/export.
5. Improve parser into an AST-based roundtrip layer.

