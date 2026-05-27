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

It also contains an explicit `TreeNode` hierarchy with scene, module, module-call, group, primitive, for-loop, and variable nodes. Add/delete/boolean-mode changes update this tree incrementally; viewport interaction may edit an explicitly selected transform container but does not create or reorder containers.
The service root is a non-deletable internal container. Its user-visible children are the permanent `scene` container and top-level OpenSCAD `module` declarations. User geometry lives in `scene`; module declarations live at root level and become geometry only through explicit `ModuleCall` nodes.
Group `TreeNode` entries store position, rotation, and scale transforms. OpenSCAD generation wraps transformed groups with `translate`/`rotate`/`scale`, and Manifold CSG applies the same transform after evaluating the group operation.
Transform groups are edited directly in the graphics tree through compact controls. Edits use `UpdateGroupTransformCommand` and document snapshots for undo/redo.
Variable `TreeNode` entries live in the scene container, in module parameter sections, or directly inside module bodies. They store a generated unique name plus expression text and are emitted as assignment lines or module parameters.
`ExpressionSyntax` is a small expression-syntax validator/evaluator used by parser, generator-facing controls, and preview. It accepts numbers, identifiers, `+`, `-`, `*`, `/`, parentheses, and unary `+/-`.
Selected `Translate` and `Rotate` groups can be edited from the viewport axis gizmo or rotation rings. Viewport group dragging updates that explicit transform live and commits old/new document snapshots to an undoable `UpdateGroupTransformCommand` on release. If components are expression-backed, the gesture converts only components changed by its delta to numeric values and preserves expressions on untouched axes.
The document model exposes tree operations used by the UI layer: add group, remove group by promoting children, add/remove variables in valid zones, add/remove module calls, and move a tree node to another valid container. Undo/redo commands wrap these operations by storing document snapshots before and after each tree edit.

`MainWindow` owns the Qt UI, undo stack, scene tree, code editor, and coordination between scene, code, graphics tree, and viewport.

The scene tree is a `QTreeWidget` projection of the internal boolean tree. Primitive rows select the corresponding `ShapeNode`; group rows are selectable targets for creating, deleting, and moving explicit operation groups. A context menu exposes the same core group actions near the selected node.
For clarity, children of `difference()` groups are labeled as `base` or `cut`, and children of `intersection()` groups are labeled as `mask`.
The legacy `ShapeNode::booleanMode` is synchronized after tree moves and is no longer rewritten by unrelated parameter edits.

`SceneTreeGraphicsWidget` is the experimental graphical tree editor. It is a `QGraphicsView`/`QGraphicsScene` projection of the same `SceneDocument::TreeNode` hierarchy, not a second document model. It draws an embedded palette for primitives, operation groups, transform containers, variables, modules, nested rectangles for the tree, dedicated base/cut regions for `difference`, object icons plus stable numbers for primitives, and a dark grid canvas. Variable cards expose numeric literals in their assignment expressions as small wheel-adjustable controls. Module declaration cards expose a non-code call handle; dragging that handle creates a real `ModuleCall` node while leaving the handle in place. Palette drag/drop creates new tree nodes; dragging existing nodes moves them to a target group with an explicit insert index. Right-click selection is used for now so selection does not conflict with left-button drag/move.
The graphics widget maintains transient hit-area metadata for each drawn group so drag preview can compute source and target containers, future child order, container expansion, `difference` base/cut placement, and self-drop suppression without mutating the document during mouse move. For moved nodes, the widget renders a snapshot of the source node under the cursor, shows a reserved slot at the source location, and separately previews the target container after insertion. Palette drags preview the real node that will be inserted. Active drags are marked with a green dashed focus outline, using an ellipse for primitives and a rounded rectangle for operation groups.
Graphics-tree selection flows back through `MainWindow`, which updates the classic tree, viewport selection, and OpenSCAD code highlight. The widget intentionally avoids `Q_OBJECT`; callbacks are plain `std::function` hooks to keep it easy to isolate while the graphics tree is being developed.

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
- asynchronous CSG preview: `invalidateCsgPreview()` snapshots the scene and launches `buildCsgPreview` on a `QtConcurrent::run` thread; `QFutureWatcher<CsgPreview>` delivers the result back to the main thread via `onCsgPreviewReady()`. If the scene changed while that worker was active, its superseded result is discarded and the newest state is evaluated next; paint paths never block on CSG.

## Code Generation And Parsing

`OpenScadGenerator` converts `SceneDocument` to OpenSCAD code.
It can also produce a source map from tree-node ids to generated text ranges. `MainWindow` uses that map with `QTextEdit::ExtraSelection` to highlight the code block for the currently selected tree node.

`SceneDocument::TreeNode` is the editable document tree used by OpenSCAD generation, Manifold CSG evaluation, CSG preview mode detection, and scene-tree UI projection. It is still initialized from flat per-shape boolean modes, but it is updated as document state instead of being rebuilt on every shape edit.
CSG preview routes through tree-based Manifold evaluation whenever the tree contains boolean operations or group transforms; the flat shape fallback is only for untransformed plain primitive previews.

Node kinds:

- container/declaration: `Scene`, `Module`
- call: `ModuleCall`
- group: `Union`, `Difference`, `Intersection`, `Translate`, `Rotate`, `Scale`, `For`
- primitive: shape reference by stable `shapeId`
- variable: scene assignment, module-local assignment, or module parameter with generated name and expression text

Generated boolean structure:

- scene variables and scene children are emitted at top level.
- module declarations are emitted at top level and are not implicitly called.
- module calls are emitted only where real `ModuleCall` nodes appear.
- variable nodes are emitted as assignment lines or module parameters according to their parent zone.
- plain shapes are emitted directly or inside supported groups.
- subtract shapes generate `difference()` structure; the first child is the base and later children are cuts.
- intersect shapes generate `intersection()` structure.

`OpenScadParser` parses the generated subset back into a `SceneDocument::Snapshot`, including top-level scene variables, top-level module declarations with parameters, explicit module calls, boolean groups, transform groups, for loops, and primitive nodes. Applying generated code restores the explicit document tree instead of flattening the scene back to only `ShapeNode` data. It is not a general OpenSCAD parser; the current round-trippable syntax contract is documented in `docs/openscad_subset.md`.

## Undo/Redo

Undo commands live in `scenecommands.*`:

- `AddShapeCommand`
- `DeleteShapeCommand`
- `AddVariableCommand`
- `RemoveVariableCommand`
- `AddModuleCommand`
- `RemoveModuleCommand`
- `AddModuleCallCommand`
- `RemoveModuleCallCommand`
- `UpdateModuleCallArgumentCommand`
- `UpdateForLoopCommand`
- `UpdateGroupTransformCommand`
- `UpdateShapeCommand`
- `ReplaceSceneCommand`, which can restore a parsed `SceneDocument::Snapshot` for generated-code roundtrip

Viewport dragging applies only to an explicitly selected `Translate` or `Rotate` group and commits one `UpdateGroupTransformCommand` on release. Primitive selection does not imply a transform order, so viewport gestures do not wrap primitives in new containers.

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

- Selecting a `Translate` group exposes axis handles; dragging one updates that existing group's translation.
- Selecting a `Rotate` group exposes rotation rings; dragging one updates that existing group's rotation.
- Selecting a primitive highlights it for identification, but does not expose an implicit move/rotate gesture.
- The viewport breadcrumb exposes the selected node's existing container path; selecting a transform chip delegates to the same tree-node selection flow and never creates hierarchy.
- Scene-tree row dragging moves explicit `TreeNode` entries into target groups through `MoveTreeNodeCommand`.
- Scene-tree drops use copy-action event handling and defer model updates until after the Qt drop event, so Qt's internal item move cleanup cannot remove freshly rebuilt rows.
- Graphics-tree palette dragging creates primitives or operation groups through undoable commands.
- Graphics-tree `VAR` palette dragging creates variables in valid scene/module zones. Variable nodes can be selected, deleted, and have individual numeric literals adjusted with `Ctrl` + mouse wheel, but they are not yet renamed.
- Dragging a module declaration's call handle creates a real `ModuleCall` node in a valid scene, group, for-loop, or module-body target. The handle itself is UI-only and is not emitted as OpenSCAD.
- `ModuleCall` nodes can be selected, deleted, moved between valid containers, adjusted through module argument controls, and highlighted in the viewport as their own call instances.
- Graphics-tree existing-node dragging moves explicit `TreeNode` entries into target groups through `MoveTreeNodeCommand`.
- Graphics-tree drops include an insert index so new and moved nodes can land before, between, or after siblings instead of always appending.
- Graphics-tree right-click selection keeps selection separate from drag/move and updates the viewport, classic tree, and generated-code highlight.
- Graphics-tree keyboard handling maps `Delete` and `Backspace` to the same delete commands used elsewhere in the UI.
- Graphics-tree background dragging pans a bounded virtual canvas; scroll bars are hidden but still used internally by `QGraphicsView`.
- Graphics-tree drag preview is visual-only: it reserves source and target slots, previews container growth, removes the moved node from the source-container preview, prevents a group from being previewed as dropped into itself, and suppresses fallback target overlays when there is no real drop target during a move.
- Moving a node between groups adjusts the moved node's local position to preserve its world position for translation-only group transforms.
- Scene-tree context menus call the same add/delete commands as the Shapes dock buttons.
- After tree moves, `SceneDocument` verifies that every existing shape still has a primitive tree node.
- During transform-group drag, selection glow is suppressed and the viewport renders a lightweight primitive interaction preview transformed through the current tree. The preview follows the gizmo immediately; boolean CSG detail is restored after release.
- CSG preview is computed asynchronously: `invalidateCsgPreview()` snapshots the scene and dispatches a `QtConcurrent::run` task. Paint paths read the last accepted `m_cachedCsgPreview` without blocking. A `m_csgComputing` guard ensures at most one task runs at a time; while a viewport drag is active new CSG work is paused, then evaluation resumes with the final transform on release.
- The viewport exposes `OpenGL`, dark/light theme, material color controls, and a `Nav UI` toggle for its glass-style help/status panel and selectable tree-path breadcrumb. When OpenGL is enabled, solid scene meshes, grid/axes, contact shadows, and selection edges use shader paths and cached VBOs. Gizmo/helper overlays and text remain painter overlays; GPU-side or on-demand picking remains future work rather than a per-frame software raster pass.
- Graphics-tree transform and primitive parameter controls can be adjusted with `Ctrl + mouse wheel`. Hovering editable controls provides viewport hints for the affected axis, rotation, scale, or primitive dimension.

## Tools

### sfxbuilder (`tools/sfxbuilder/`)

A standalone Qt Widgets application that automates portable Windows packaging.
It is built and run separately from the main app; it is not linked into the
main binary.

Key components:

- `BuildWorker` (QObject, runs on a `QThread`) — executes the build pipeline:
  1. Copies the source exe into a temporary staging directory.
  2. Runs `windeployqt` to deploy Qt DLLs, plugins, and platform files.
  3. Copies MinGW runtime DLLs (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
     `libwinpthread-1.dll`) from the Qt bin dir. `windeployqt` intentionally
     omits these; without this step the resulting exe fails with
     _"libgcc_s_seh-1.dll was not found"_ on machines without a MinGW
     installation.
  4. Optionally copies an extra file tree (e.g. `docs/sample_codes`) into the
     staging directory at a configurable bundle-relative path.
  5. Compresses the staging directory with `7z.exe` at maximum compression.
  6. Concatenates the `7z.sfx` stub, a small UTF-8 config block, and the
     archive into the final `<AppName>_portable.exe`.
  7. Cleans up staging and temporary archive.

- `MainWindow` — form with auto-detect logic for Qt bin dir and 7-Zip install
  path, browse buttons for all inputs, a build button, and a log panel that
  streams `BuildWorker` output.

See [docs/sfxbuilder.md](docs/sfxbuilder.md) for end-user usage instructions.

### manifoldbuilder (`tools/manifoldbuilder/`)

A standalone Qt Widgets application that runs the optional Manifold backend
build through `scripts/build-manifold.ps1`.

Key components:

- `MainWindow` - configuration form with auto-detected repository root and Qt
  root, architecture selector, optional clean-build checkbox, derived script and
  output paths, build/stop buttons, and a streaming log panel.
- `QProcess` - starts `powershell.exe -NoProfile -ExecutionPolicy Bypass -File
  scripts/build-manifold.ps1` with `-QtRoot`, `-Arch`, and `-Generator`
  arguments. The working directory is the repository root.
- The tool deletes only `build/manifold-build-<arch>` when clean build is
  enabled. It does not delete `build/manifold-src`, so the downloaded Manifold
  checkout is reused.

See [docs/manifoldbuilder.md](docs/manifoldbuilder.md) for usage instructions.

## Current Technical Risks

- Software rendering and CSG preview allocate enough data that 32-bit builds can hit memory pressure.
- The experimental OpenGL backend uses cached VBOs, but does not yet use indexed geometry buffers, GPU-side projection, or GPU picking.
- Optional Manifold integration currently depends on a local build artifact under `build/`.
- Mesh approximate fallback is still only a fallback and can diverge from exact OpenSCAD output.
- Shape boolean mode is still present as a legacy/simple editing control and can rewrite primitive placement in the explicit tree.
- Variable and module-parameter support intentionally follows a smaller scope model than full OpenSCAD; rename UI and broader language semantics are future work.
- The graphics tree is still a preview/editor prototype; classic tree remains available until insertion, explicit reordering affordances, group deletion ergonomics, and difference base/cut editing feel complete there.
- Parser and generator are coupled to the narrow subset documented in `docs/openscad_subset.md`.
- Background CSG tasks cannot be cancelled mid-flight (QtConcurrent::run futures are not cancellable); if the scene changes rapidly, one extra compute for the superseded state may complete before the final one starts.

## Recommended Next Work

Short term:

- Refine graphics-tree editing: rename groups, add clearer reorder affordances, improve difference base/cut editing, and decide when the classic tree can be hidden.
- Formalize Manifold setup as a submodule, bootstrap script, or CMake migration.
- Add visible reason text when Manifold is unavailable and fallback preview is active.
- Add smoke tests for generator/parser roundtrip and CSG backend availability.

Medium term:

- Continue isolating scene-tree UI/model code so the graphics tree can be developed independently from the main viewport.
- Move the experimental OpenGL backend toward indexed geometry buffers and GPU picking.
- Split viewport projection/raster/picking helpers out of `ViewportWidget`.

Long term:

- Integrate OpenSCAD CLI for exact render/export validation.
- Replace approximate mesh filtering with real triangle clipping or a dedicated CSG library.
