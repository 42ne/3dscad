# Architecture Notes

This document captures the current design so work can continue without relying on chat history.

## Main Concepts

`ShapeNode` is the scene-level primitive model.

It stores:

- stable `id`
- primitive type: cube, sphere, cylinder
- boolean mode: add, subtract, intersect
- name
- transform
- primitive parameters

`SceneDocument` owns the ordered list of `ShapeNode` objects, selection state, stable ids, and snapshot/restore support.

`MainWindow` owns the Qt UI, undo stack, property panel, scene tree, code editor, and coordination between scene, code, and viewport.

`ViewportWidget` owns interactive viewing and picking:

- camera orbit/zoom
- projection
- software depth buffer
- viewport rasterization
- move gizmo
- helper wireframe picking

## Code Generation And Parsing

`OpenScadGenerator` converts `SceneDocument` to OpenSCAD code.

Generated boolean structure:

- plain shapes are emitted inside `union()`
- subtract shapes generate `difference()`
- intersect shapes generate `intersection()`

`OpenScadParser` parses the generated subset back into `ShapeNode` objects. It is not a general OpenSCAD parser.

## Undo/Redo

Undo commands live in `scenecommands.*`:

- `AddShapeCommand`
- `DeleteShapeCommand`
- `UpdateShapeCommand`
- `ReplaceSceneCommand`

Viewport dragging updates the live scene during drag, then creates one `UpdateShapeCommand` on release.

## Mesh Layer

`scenemesh.*` converts a single `ShapeNode` into triangle mesh data.

Important structures:

- `MeshTriangle`
- `SceneMesh`

`buildShapeMesh()` supports cube, sphere, and cylinder.

`buildBoxMesh()` creates an axis-aligned box mesh and is used by CSG code.

## CSG Layer

`csgevaluator.*` builds viewport CSG preview items.

Returned data:

- `CsgPreview`
- `CsgRenderItem`

Modes:

- `Plain`: no active boolean operations.
- `BoxComputed`: exact-ish box CSG for unrotated cubes.
- `MeshApproximate`: centroid-based triangle filtering for non-box shapes.
- `Fallback`: cannot compute because there is no add/base shape.

### Box CSG

Box CSG operates on axis-aligned boxes:

1. Add cubes become source boxes.
2. Subtract cubes split boxes into remaining pieces.
3. Intersect cubes clip remaining boxes.
4. Result boxes are converted to an occupied coordinate grid.
5. Only exterior faces are emitted.
6. Coplanar face cells are merged into larger rectangles.

This avoids internal faces and reduces triangle count.

### Mesh Approximate CSG

For spheres, cylinders, and rotated cubes:

1. Add shapes are meshed normally.
2. Triangle centroids are tested against subtract helper volumes.
3. Triangles inside subtract helpers are removed.
4. If intersect helpers exist, triangles outside all intersect helpers are removed.
5. Helper shapes are rendered as wireframes.

This is only a preview. It does not create cut surfaces.

## Viewport Interaction

Selection:

- Solid/computed geometry is picked through the software pick buffer.
- CSG helper wireframes are picked by distance-to-segment hit testing before normal mesh picking.

Dragging:

- Axis gizmo drag emits shape drag signals.
- `Shift + drag` supports plane dragging.
- During active drag, CSG evaluation is paused to avoid expensive per-frame recomputation and memory churn.

## Current Technical Risks

- Software rendering and CSG preview allocate enough data that 32-bit builds can hit memory pressure.
- Mesh approximate CSG can visually leave holes without cap faces.
- The flat boolean mode per shape is simple, but a real OpenSCAD model needs an operation tree.
- Parser and generator are coupled to a narrow generated subset.

## Recommended Next Work

Short term:

- Add simple cap-face generation for mesh approximate subtract cuts.
- Cache CSG previews and invalidate only when shape data changes.
- Add visible reason text for why mesh approximate or fallback is active.

Medium term:

- Introduce an explicit scene operation tree.
- Move rasterization to OpenGL buffers.
- Split viewport projection/raster/picking helpers out of `ViewportWidget`.

Long term:

- Integrate OpenSCAD CLI for exact render/export validation.
- Replace approximate mesh filtering with real triangle clipping or a dedicated CSG library.

