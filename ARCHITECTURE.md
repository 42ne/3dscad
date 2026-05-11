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

It also contains an explicit `TreeNode` hierarchy with group and primitive nodes. Add/delete/boolean-mode changes update this tree incrementally; transform and primitive parameter edits leave tree structure intact.
Group `TreeNode` entries store position and rotation transforms. OpenSCAD generation wraps transformed groups with `translate`/`rotate`, and Manifold CSG applies the same transform after evaluating the group operation.
Group transforms can be edited from the Properties dock when a group row is selected; edits use `UpdateGroupTransformCommand` and document snapshots for undo/redo.
Selected groups can also be moved from the viewport axis gizmo and rotated from the viewport rotation rings. Viewport group dragging updates the group transform live and commits old/new document snapshots to an undoable `UpdateGroupTransformCommand` on release.
The document model exposes group operations used by the UI layer: add group, remove group by promoting children, and move a tree node to another group. Undo/redo commands wrap these operations by storing document snapshots before and after each tree edit.

`MainWindow` owns the Qt UI, undo stack, property panel, scene tree, code editor, and coordination between scene, code, and viewport.

The scene tree is a `QTreeWidget` projection of the internal boolean tree. Primitive rows select the corresponding `ShapeNode`; group rows are selectable targets for creating, deleting, and moving explicit operation groups. A context menu exposes the same core group actions near the selected node.
For clarity, children of `difference()` groups are labeled as `base` or `cut`, and children of `intersection()` groups are labeled as `mask`.
The properties panel derives the selected primitive's displayed tree role from `SceneDocument::TreeNode`; the legacy `ShapeNode::booleanMode` is synchronized after tree moves and is no longer rewritten by unrelated parameter edits.

`ViewportWidget` owns interactive viewing and picking:

- camera orbit/zoom
- projection
- software depth buffer
- viewport rasterization
- explicit render backend selection, currently using the software backend by default
- transform gizmo with move axes and rotation rings
- helper wireframe picking
- cached CSG preview reuse between repaints

## Code Generation And Parsing

`OpenScadGenerator` converts `SceneDocument` to OpenSCAD code.

`SceneDocument::TreeNode` is the editable document tree used by OpenSCAD generation, Manifold CSG evaluation, CSG preview mode detection, and scene-tree UI projection. It is still initialized from flat per-shape boolean modes, but it is updated as document state instead of being rebuilt on every shape edit.
CSG preview routes through tree-based Manifold evaluation whenever the tree contains boolean operations or group transforms; the flat shape fallback is only for untransformed plain primitive previews.

Node kinds:

- group: `Union`, `Difference`, `Intersection`
- primitive: shape reference by stable `shapeId`

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

- qmake checks for the Manifold library matching the active Qt kit:
  `build/manifold-build-32/src/libmanifold.a` or `build/manifold-build-64/src/libmanifold.a`.
- If the library exists, `HAVE_MANIFOLD_CSG` is defined.
- If not, the adapter compiles as a no-op and `csgevaluator` falls back to internal modes.

Runtime flow:

1. Convert each `ShapeNode` to a Manifold primitive.
2. Evaluate the `SceneDocument::TreeNode` tree with union, difference, and intersection operators.
3. Convert Manifold `MeshGL` output back to `SceneMesh`.
4. Render helper shapes as wireframes for editing.

The current local Manifold builds live under `build/` and are not part of the repository. With Qt's MinGW GCC 8, Manifold needed local sequential fallbacks for unavailable standard parallel numeric functions.

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

- Axis gizmo drag emits shape drag signals and moves the selected primitive.
- Rotation ring drag emits shape rotation signals and rotates the selected primitive.
- If a group row is selected, the same gizmo emits group drag/rotation signals and updates the group transform.
- `Shift + drag` supports plane dragging.
- Scene-tree row dragging moves explicit `TreeNode` entries into target groups through `MoveTreeNodeCommand`.
- Scene-tree drops use copy-action event handling and defer model updates until after the Qt drop event, so Qt's internal item move cleanup cannot remove freshly rebuilt rows.
- Moving a node between groups adjusts the moved node's local position to preserve its world position for translation-only group transforms.
- Scene-tree context menus call the same add/delete commands as the Shapes dock buttons.
- After tree moves, `SceneDocument` verifies that every existing shape still has a primitive tree node.
- During active drag, CSG evaluation is paused to avoid expensive per-frame recomputation and memory churn.
- Outside drag, the viewport caches CSG preview data by a scene fingerprint so camera motion and repaint events do not recompute Manifold CSG.
- The Shapes dock exposes a `Use OpenGL` checkbox. When enabled, solid scene meshes are drawn by the OpenGL shader path while grid, helper wireframes, gizmo, text, CPU-side projection, and picking still reuse the software path.

## Current Technical Risks

- Software rendering and CSG preview allocate enough data that 32-bit builds can hit memory pressure.
- The experimental OpenGL mesh backend does not yet use persistent GPU buffers, GPU-side projection, or GPU picking.
- Optional Manifold integration currently depends on a local build artifact under `build/`.
- Mesh approximate fallback is still only a fallback and can diverge from exact OpenSCAD output.
- Shape boolean mode is still present as a legacy/simple editing control and can rewrite primitive placement in the explicit tree.
- Group selection is UI-only, but tree refreshes preserve selected group ids when no primitive is selected.
- Parser and generator are coupled to a narrow generated subset.

## Recommended Next Work

Short term:

- Refine scene-tree editing: rename groups, reorder nodes, and improve visual distinction between service root and user groups.
- Formalize Manifold setup as a submodule, bootstrap script, or CMake migration.
- Add visible reason text when Manifold is unavailable and fallback preview is active.
- Add smoke tests for generator/parser roundtrip and CSG backend availability.

Medium term:

- Store explicit operation groups in `SceneDocument` instead of deriving the tree only from flat per-shape boolean modes.
- Move fallback CSG paths from flat shape modes to `SceneDocument::TreeNode`.
- Implement the optional OpenGL backend with vertex/index buffers and GPU picking.
- Split viewport projection/raster/picking helpers out of `ViewportWidget`.

Long term:

- Integrate OpenSCAD CLI for exact render/export validation.
- Replace approximate mesh filtering with real triangle clipping or a dedicated CSG library.
