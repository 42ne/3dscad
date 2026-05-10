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

The scene tree is a `QTreeWidget` projection of the internal boolean tree. Primitive rows select the corresponding `ShapeNode`; dragging a primitive row onto a boolean group changes its flat `ShapeNode::booleanMode`.

`ViewportWidget` owns interactive viewing and picking:

- camera orbit/zoom
- projection
- software depth buffer
- viewport rasterization
- move gizmo
- helper wireframe picking
- cached CSG preview reuse between repaints

## Code Generation And Parsing

`OpenScadGenerator` converts `SceneDocument` to OpenSCAD code.

`scenebooleantree.*` converts the current flat shape list into an internal boolean tree:

- `Union`
- `Difference`
- `Intersection`
- `Primitive`

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
- `ManifoldComputed`: exact mesh boolean result from the optional Manifold backend.
- `BoxComputed`: exact-ish box CSG for unrotated cubes.
- `MeshApproximate`: centroid-based triangle filtering for non-box shapes.
- `Fallback`: cannot compute because there is no add/base shape.

### Manifold CSG

`manifoldcsg.*` is an optional adapter around the open-source Manifold library.

Build activation:

- qmake checks for `build/manifold-build/src/libmanifold.a`.
- If the library exists, `HAVE_MANIFOLD_CSG` is defined.
- If not, the adapter compiles as a no-op and `csgevaluator` falls back to internal modes.

Runtime flow:

1. Convert each `ShapeNode` to a Manifold primitive.
2. Evaluate the `SceneBooleanNode` tree with union, difference, and intersection operators.
3. Convert Manifold `MeshGL` output back to `SceneMesh`.
4. Render helper shapes as wireframes for editing.

The current local Manifold build lives under `build/` and is not part of the repository. With Qt's MinGW GCC 8, Manifold needed local sequential fallbacks for unavailable standard parallel numeric functions.

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
- Outside drag, the viewport caches CSG preview data by a scene fingerprint so camera motion and repaint events do not recompute Manifold CSG.

## Current Technical Risks

- Software rendering and CSG preview allocate enough data that 32-bit builds can hit memory pressure.
- Optional Manifold integration currently depends on a local build artifact under `build/`.
- Mesh approximate fallback is still only a fallback and can diverge from exact OpenSCAD output.
- The flat boolean mode per shape is simple, but a real OpenSCAD model needs an operation tree.
- Parser and generator are coupled to a narrow generated subset.

## Recommended Next Work

Short term:

- Formalize Manifold setup as a submodule, bootstrap script, or CMake migration.
- Add visible reason text when Manifold is unavailable and fallback preview is active.
- Add smoke tests for generator/parser roundtrip and CSG backend availability.

Medium term:

- Store explicit operation groups in `SceneDocument` instead of deriving the tree only from flat per-shape boolean modes.
- Move rasterization to OpenGL buffers.
- Split viewport projection/raster/picking helpers out of `ViewportWidget`.

Long term:

- Integrate OpenSCAD CLI for exact render/export validation.
- Replace approximate mesh filtering with real triangle clipping or a dedicated CSG library.
