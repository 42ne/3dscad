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

It also contains an explicit `TreeNode` hierarchy with group, primitive, and variable nodes. Add/delete/boolean-mode changes update this tree incrementally; transform and primitive parameter edits leave tree structure intact.
The service root is a non-deletable `Module` tree node used as the generated `scene_model` module root. User-visible boolean operation nodes are `Union`, `Difference`, and `Intersection`; `Module` is also reserved in the graphics palette for future nested module work.
Group `TreeNode` entries store position, rotation, and scale transforms. OpenSCAD generation wraps transformed groups with `translate`/`rotate`/`scale`, and Manifold CSG applies the same transform after evaluating the group operation.
Transform groups are edited directly in the graphics tree through compact controls; the Properties dock remains available for legacy/simple property editing. Edits use `UpdateGroupTransformCommand` and document snapshots for undo/redo.
Variable `TreeNode` entries are first-stage scaffolding for future parameterization. They currently live only as direct children of the root module, store a generated unique name plus expression text, are emitted as assignment lines, and are ignored by geometry/CSG evaluation.
`ExpressionSyntax` is a small expression-syntax validator used by the parser for variable assignment text. It accepts numbers, identifiers, `+`, `-`, `*`, `/`, parentheses, and unary `+/-`. It does not yet evaluate expressions or connect them to primitive/transform parameters.
Selected groups can also be moved from the viewport axis gizmo and rotated from the viewport rotation rings. Viewport group dragging updates the group transform live and commits old/new document snapshots to an undoable `UpdateGroupTransformCommand` on release.
The document model exposes tree operations used by the UI layer: add group, remove group by promoting children, add/remove root variables, and move a tree node to another group. Undo/redo commands wrap these operations by storing document snapshots before and after each tree edit.

`MainWindow` owns the Qt UI, undo stack, property panel, scene tree, code editor, and coordination between scene, code, and viewport.

The scene tree is a `QTreeWidget` projection of the internal boolean tree. Primitive rows select the corresponding `ShapeNode`; group rows are selectable targets for creating, deleting, and moving explicit operation groups. A context menu exposes the same core group actions near the selected node.
For clarity, children of `difference()` groups are labeled as `base` or `cut`, and children of `intersection()` groups are labeled as `mask`.
The properties panel derives the selected primitive's displayed tree role from `SceneDocument::TreeNode`; the legacy `ShapeNode::booleanMode` is synchronized after tree moves and is no longer rewritten by unrelated parameter edits.

`SceneTreeGraphicsWidget` is the experimental graphical tree editor. It is a `QGraphicsView`/`QGraphicsScene` projection of the same `SceneDocument::TreeNode` hierarchy, not a second document model. It draws an embedded palette for primitives, operation groups, transform containers, root variables, nested rectangles for the tree, dedicated base/cut regions for `difference`, object icons plus stable numbers for primitives, and a dark grid canvas. Root variable cards expose numeric literals in their assignment expressions as small wheel-adjustable controls. Palette drag/drop creates new tree nodes; dragging existing nodes moves them to a target group with an explicit insert index. Right-click selection is used for now so selection does not conflict with left-button drag/move.
The graphics widget maintains transient hit-area metadata for each drawn group so drag preview can compute source and target containers, future child order, container expansion, `difference` base/cut placement, and self-drop suppression without mutating the document during mouse move. For moved nodes, the widget renders a snapshot of the source node under the cursor, shows a reserved slot at the source location, and separately previews the target container after insertion. Palette drags preview the real node that will be inserted. Active drags are marked with a green dashed focus outline, using an ellipse for primitives and a rounded rectangle for operation groups.
Graphics-tree selection flows back through `MainWindow`, which updates the classic tree, viewport selection, Properties dock, and OpenSCAD code highlight. The widget intentionally avoids `Q_OBJECT`; callbacks are plain `std::function` hooks to keep it easy to isolate while the graphics tree is being developed.

`ViewportWidget` owns interactive viewing, picking, and viewport-local display controls:

- camera orbit/zoom
- right-button viewport panning
- projection
- software depth buffer
- viewport rasterization
- explicit render backend selection through an in-viewport `OpenGL` checkbox
- dark/light viewport theme and material color variant controls
- transform gizmo with move axes and rotation rings
- helper cut-volume drawing
- cached CSG preview reuse between repaints

## Code Generation And Parsing

`OpenScadGenerator` converts `SceneDocument` to OpenSCAD code.
It can also produce a source map from tree-node ids to generated text ranges. `MainWindow` uses that map with `QTextEdit::ExtraSelection` to highlight the code block for the currently selected tree node.

`SceneDocument::TreeNode` is the editable document tree used by OpenSCAD generation, Manifold CSG evaluation, CSG preview mode detection, and scene-tree UI projection. It is still initialized from flat per-shape boolean modes, but it is updated as document state instead of being rebuilt on every shape edit.
CSG preview routes through tree-based Manifold evaluation whenever the tree contains boolean operations or group transforms; the flat shape fallback is only for untransformed plain primitive previews.

Node kinds:

- group: `Module`, `Union`, `Difference`, `Intersection`
- primitive: shape reference by stable `shapeId`
- variable: root-module assignment scaffold with generated name and numeric value

Generated boolean structure:

- the document root is emitted as `module scene_model() { ... }` followed by `scene_model();`
- variable nodes are emitted as assignment lines inside `scene_model`, currently before/among other root children according to tree order
- plain shapes are emitted as module children or inside `union()`
- subtract shapes generate `difference()`
- intersect shapes generate `intersection()`

`OpenScadParser` parses the generated subset back into a `SceneDocument::Snapshot`, including the module wrapper, root variable assignments with simple expressions, boolean groups, transform groups, and primitive nodes. Applying generated code restores the explicit document tree instead of flattening the scene back to only `ShapeNode` data. It does not yet bind variables to primitive or transform parameters. It is not a general OpenSCAD parser.

## Undo/Redo

Undo commands live in `scenecommands.*`:

- `AddShapeCommand`
- `DeleteShapeCommand`
- `AddVariableCommand`
- `RemoveVariableCommand`
- `UpdateShapeCommand`
- `ReplaceSceneCommand`, which can restore a parsed `SceneDocument::Snapshot` for generated-code roundtrip

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
4. Render helper shapes for editing. Subtract helpers are displayed as translucent cut volumes; selected cuts show useful silhouette/feature edges, while inactive cuts remain faint.

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
5. Helper shapes are rendered as editing overlays. Subtract helpers use translucent cut volumes instead of full wireframes.

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
- Graphics-tree palette dragging creates primitives or operation groups through the same undoable commands used by the classic tree/buttons.
- Graphics-tree `VAR` palette dragging creates a variable only in the root module. Variable nodes can be selected, deleted, and have individual numeric literals adjusted with `Ctrl` + mouse wheel, but they are not yet renamed or used as parameter sources.
- Graphics-tree existing-node dragging moves explicit `TreeNode` entries into target groups through `MoveTreeNodeCommand`.
- Graphics-tree drops include an insert index so new and moved nodes can land before, between, or after siblings instead of always appending.
- Graphics-tree right-click selection keeps selection separate from drag/move and updates the viewport, Properties dock, classic tree, and generated-code highlight.
- Graphics-tree keyboard handling maps `Delete` and `Backspace` to the same delete commands used elsewhere in the UI.
- Graphics-tree background dragging pans a bounded virtual canvas; scroll bars are hidden but still used internally by `QGraphicsView`.
- Graphics-tree drag preview is visual-only: it reserves source and target slots, previews container growth, removes the moved node from the source-container preview, prevents a group from being previewed as dropped into itself, and suppresses fallback target overlays when there is no real drop target during a move.
- Moving a node between groups adjusts the moved node's local position to preserve its world position for translation-only group transforms.
- Scene-tree context menus call the same add/delete commands as the Shapes dock buttons.
- After tree moves, `SceneDocument` verifies that every existing shape still has a primitive tree node.
- During active drag, CSG evaluation is paused to avoid expensive per-frame recomputation and memory churn.
- Outside drag, the viewport caches CSG preview data by a scene fingerprint so camera motion and repaint events do not recompute Manifold CSG.
- The viewport exposes `OpenGL`, dark/light theme, and material color controls as small overlay widgets. When OpenGL is enabled, solid scene meshes, grid/axes, and contact shadows are drawn by shader paths while gizmo, helper overlays, text, CPU-side projection, and picking still reuse the software path.
- Graphics-tree transform and primitive parameter controls can be adjusted with `Ctrl + mouse wheel`. Hovering editable controls provides viewport hints for the affected axis, rotation, scale, or primitive dimension.

## Current Technical Risks

- Software rendering and CSG preview allocate enough data that 32-bit builds can hit memory pressure.
- The experimental OpenGL backend does not yet use persistent GPU buffers, GPU-side projection, or GPU picking.
- Optional Manifold integration currently depends on a local build artifact under `build/`.
- Mesh approximate fallback is still only a fallback and can diverge from exact OpenSCAD output.
- Shape boolean mode is still present as a legacy/simple editing control and can rewrite primitive placement in the explicit tree.
- Variable nodes are intentionally minimal: only root-module placement, expression syntax validation, and generated assignment output are implemented; rename, scope, expression evaluation, and parameter binding are future work.
- The graphics tree is still a preview/editor prototype; classic tree remains available until insertion, explicit reordering affordances, group deletion ergonomics, and difference base/cut editing feel complete there.
- Parser and generator are coupled to a narrow generated subset.

## Recommended Next Work

Short term:

- Refine graphics-tree editing: rename groups, add clearer reorder affordances, improve difference base/cut editing, and decide when the classic tree can be hidden.
- Formalize Manifold setup as a submodule, bootstrap script, or CMake migration.
- Add visible reason text when Manifold is unavailable and fallback preview is active.
- Add smoke tests for generator/parser roundtrip and CSG backend availability.

Medium term:

- Continue isolating scene-tree UI/model code so the graphics tree can be developed independently from the main viewport.
- Move the experimental OpenGL backend toward persistent vertex/index buffers and GPU picking.
- Split viewport projection/raster/picking helpers out of `ViewportWidget`.

Long term:

- Integrate OpenSCAD CLI for exact render/export validation.
- Replace approximate mesh filtering with real triangle clipping or a dedicated CSG library.
