# Current Scene Tree Behavior

Status as of: 2026-05-25.

This document records the expected behavior of the graphical scene tree in the
current implementation. It is intended as a reference specification for
regression checks after changes to drag/drop, modules, variables, the code
editor, or canvas layout.

The syntax accepted by `Apply code` is documented separately in
[openscad_subset.md](openscad_subset.md).

## 1. Tree Model

The model has an invisible internal root container. It is not emitted as an
OpenSCAD block and stores the top-level cards on the canvas:

- the permanent `scene` block;
- separate `module` declarations.

`scene` contains the actual top-level scene. It cannot be deleted, even when
empty. It may contain global variables, geometry, groups, and module calls.

`module` is a reusable declaration. It does not add geometry to the viewport
by itself; a module call node (`CALL`) is required.

Tree node types:

- primitive: a geometry shape;
- group: a container or operation;
- variable: a variable or a module parameter;
- module call: a call to a specific module declaration.

## 2. Palette Blocks

The toolbar can create the following blocks:

| Category | Blocks |
| --- | --- |
| Primitives | `cube`, `sphere`, `cylinder`, `cone` |
| Boolean | `union`, `difference`, `intersection` |
| Transform / display | `translate`, `rotate`, `scale`, `mirror`, `color` |
| Geometry operations | `hull`, `minkowski` |
| Structure | `module`, `var`, `for` |

Containers can hold primitives, other containers, and module calls when the
drop lands in a valid inner zone.

Boolean block behavior:

- in `difference`, the first child is the base body and all later children are
  cuts;
- in `intersection`, all children participate in the intersection;
- `union` groups objects without special child zones.

## 3. Levels And Nesting Rules

Top-level rules:

- a `module` declaration may exist only alongside `scene` on the root canvas;
- ordinary groups and primitives added at scene level are placed in `scene`;
- ordinary nodes cannot be moved directly into the hidden root.

Move rules:

- a node can be reordered inside its container;
- a node can be nested in a valid group container;
- a node cannot be dropped into itself or one of its descendants;
- reparenting a transform/group preserves its world offset;
- empty non-permanent groups are pruned after removal or movement.

Deletion rules:

- `Delete` or `Backspace` deletes the selected node;
- deleting a primitive removes the corresponding shape;
- deleting an ordinary group promotes its children into the group's position;
- deleting a `module` removes the declaration and all of its `CALL` nodes;
- the `scene` block and internal root cannot be deleted;
- dragging an existing non-module node into truly empty canvas space is also
  treated as deletion; dropping over a root block without a valid slot is
  cancelled without deleting the node.

## 4. Variables And Modules

`VAR` has only two valid locations:

- inside `scene`: a global variable emitted at top level;
- directly inside `module`: either a parameter or a local variable.

Variables must not be accepted inside `union`, `difference`, `translate`,
`for`, or other ordinary containers.

A `module` card has two drop zones for `VAR`:

- parameter lane: the node is displayed as `PAR` and included in the signature
  `module name(par = value)`;
- body lane: the node is displayed as `VAR` and emitted as an assignment inside
  the module body.

Dragging a variable between those zones changes its role. For automatically
generated names, the editor attempts to preserve the numeric suffix:

- `varN` becomes `parN` when moved into parameters;
- `parN` becomes `varN` when moved into the body or scene;
- manually chosen names are not automatically renamed.

Variable names must be valid identifiers and unique across the whole tree.
Module names must also be valid and unique among modules.

## 5. Module Calls

Each `module` card contains a template `CALL` card. It is a handle used to
create a call and is not emitted as code itself.

Dragging `CALL` into a valid container creates a real module call node. It can
be placed:

- in `scene`;
- inside boolean, transform, geometry, and `for` containers;
- inside another module body.

`CALL` must not be placed in a module parameter lane. Call parameters are
displayed from the declaration parameters and emitted as named arguments.
Renaming a module synchronizes its associated calls.

## 6. Canvas Layout And Root Block Dragging

The positions of `scene` and `module` cards on the canvas are UI layout, not
OpenSCAD structure. Dragging a root card by its grip strip does not change its
nesting.

Current root-drag behavior:

- drag begins after at least `6 px` of movement;
- root blocks touching edge-to-edge (within about `1.5 px`) form a connected
  cluster;
- during a slow drag, that cluster moves together;
- when movement speed exceeds `16 px/event`, the grabbed block detaches and the
  rest of the cluster freezes at its current position.

Magnetic snap for root blocks:

- snap radius is `80 px`;
- a block snaps to a valid non-overlapping position beside a stationary
  neighbor;
- while a connected cluster is moving slowly, all members of that cluster are
  excluded as snap targets;
- therefore, a moving cluster can snap only to an external root block;
- if the moving cluster contains every root block, it has no valid snap target;
- after a fast detach, the frozen blocks are external neighbors for the
  remaining dragged block.

Adding a new root block from the toolbar uses normal magnetic snap to existing
root blocks.

## 7. Editing Parameters In The Tree

Selecting a node in the tree synchronizes the viewport and highlights the
corresponding part of generated code.

Supported inline actions:

- double-clicking a module or variable name opens inline rename;
- `Ctrl` + mouse wheel over a number edits that numeric token in a primitive
  parameter, transform, variable, `for` range, or `CALL` argument;
- for `color`, `Ctrl` + mouse wheel edits RGB channels;
- the active numeric token is also highlighted in the code editor.

Numeric values may be part of an expression. During an adjustment the
expression text is preserved, and the viewport reevaluates supported dependent
parameters and transform values.

## 8. Code Editor Roundtrip

Tree changes regenerate code into the editor. The generator emits content in
this order:

1. global variables from `scene`;
2. `module` declarations;
3. top-level geometry and groups from `scene`;
4. top-level `CALL` nodes from `scene`.

Module-local variables and nested `CALL` nodes are emitted at their actual
location in the relevant container body.

`Apply code` rebuilds the tree from the supported OpenSCAD subset. Important
expectations:

- a valid variable inside a module body appears as `VAR`, not as an error;
- a module signature parameter appears as `PAR` in the parameter lane;
- a variable in `scene` appears as a global `VAR`;
- valid `for` + transform + module call structures are rebuilt in the tree;
- on parse error, the message includes a line number, and the text editor
  shows line numbers and moves to the affected line.

Exact restrictions on hand-written code and expressions are documented in
[openscad_subset.md](openscad_subset.md).

## 9. Undo, Redo, And Viewport Updates

Structural tree changes, parameter edits, renames, `Apply code`, deletion, and
block creation are performed through undo/redo commands.

During live geometry dragging or rotation, the viewport updates immediately;
the final change is committed to the undo stack when the drag finishes.

Root card canvas positions are visual state of the graphics widget and are not
part of OpenSCAD code.

## 10. Minimum Regression Checklist

After changes to the tree, manually verify:

1. Add a `module`, put a `VAR` in its parameter lane, another `VAR` in its body,
   and add a primitive. Code must contain a signature parameter and a local
   variable in the body.
2. In code, add a local `VAR` directly in a module body and reference it in an
   expression of a nested transform. After `Apply code`, the tree must show the
   correct structure without a parse error.
3. Drag a module `CALL` into `scene` and into a nested transform. The viewport
   and code must represent real calls.
4. Touch all root cards edge-to-edge on the canvas and slowly drag one of them.
   The cluster must move together without snapping to itself.
5. Add a separate root block and slowly bring the cluster near it. Snap must
   occur only against that external block.
6. Quickly drag a block out of a connected cluster. After the detach threshold,
   only the selected block must keep moving.
7. Reorder a child inside `difference`. The first slot must remain the base and
   later slots must remain cuts.
8. Attempt to drag `VAR` into an ordinary group or transform zone. Such a drop
   must not create invalid nesting.
9. Select a node and press `Delete`, then `Undo`. Structure and code must be
   restored correctly.
