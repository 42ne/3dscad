# OpenSCAD Syntax Analysis — Tree Implementation Effort

## Without Tree Changes (parse-time / pure evaluation / transpile)

Implemented in `expression.h` (evaluator) and/or `openscadparser.cpp` (parser).
No new `Operation`, `ShapeNode`, or `TreeNode::Type` required.

| Syntax | Where | Notes |
|---|---|---|
| **`assign()` inline** | expression.h | Same as `let`, in `()` not `{}`. Handle at expression parse level |
| **`function f(x)=expr;`** | expression.h + parser | Store in `QHash<QString, FunctionDef>`, call on eval. Produces no geometry |
| **Block comments `/* */`** | lexer / parser | Purely tokenization — no tree impact |
| **`$fn`, `$fa`, `$fs`** | expression.h | Add as special variables with defaults; for preview return high `$fn` |
| **`echo()`** | parser | Print or ignore; add to `startsWithKnownKeyword` |
| **`assert()`** | parser | Same as `echo`, error if condition false |
| **`undef`** | expression.h | Treat as `NaN` or `0` in arithmetic |
| **`rands()`** | expression.h | Single random value for preview |
| **`norm()`, `cross()`** | expression.h | Vector functions — add to function mapping |
| **`len()`** | expression.h | `len([1,2,3]) = 3`, `len("abc") = 3` |
| **`concat()`, `lookup()`** | expression.h | Limited without list comprehensions |
| **`str()`** | expression.h | Number/string concatenation |

## Require Tree Changes (new `Operation` / `ShapeNode` / architectural changes)

### New Operations (TreeNode::Operation)

| Syntax | Required | Reason |
|---|---|---|
| **`resize([x,y,z])`** | `Operation::Resize` | Stretches AABB to target size. Simulatable with computed Scale, but inexact |
| **`offset()`** | `Operation::Offset` | 2D contour transform; needs geometry processing |
| **`projection()`** | `Operation::Projection` | 3D→2D, needs rendering pipeline |
| **`rotate_extrude()`** | `Operation::RotateExtrude` | Different extrusion type from `linear_extrude` |
| **`render()`** (convexity) | `Operation::Render` | Currently flattened; to preserve `convexity` needs a node |

### New Primitives (ShapeNode::Type)

| Syntax | Required | Reason |
|---|---|---|
| **`text()`** | `ShapeNode::Text` | Font library + mesh generation |
| **`import()`** | `ShapeNode::ImportedMesh` | File I/O, external mesh storage |
| **`surface()`** | `ShapeNode::Surface` | Image/heightmap → mesh |

### Architectural Changes

| Syntax | Problem |
|---|---|
| **`children()`** | Module needs runtime access to its children. Changes the entire render model |
| **List comprehensions** | Can generate nodes (like `for`). Either iterate at parse time or add `Operation::ListComprehension` |
| **`use <file>`** | Multi-file: separate parse + tree merge |
| **`include <file>`** | Same + direct variable import |
| **Nested modules** | Module-in-module hierarchy; changes `ModuleCall` resolution |
| **`square()` with `center`** | Already partially supported (Z=0.1), but `center` ignored; can be added without tree change |

### Parameter Extensions to Existing Operations

| Syntax | Change |
|---|---|
| **`linear_extrude`** `twist`, `slices`, `scale`, `center` | Parse parameters, pass to `LinearExtrudeOperation` render |
| **`color()`** with alpha / named strings | Extend parser; no tree node change |
| **`polyhedron()`** with expressions in `points`/`faces` | Parser currently reads literals only |
| **`cube(size, center=true)`** | `center` ignored; can add as parameter or keep ignoring |

## Priority Matrix (no-tree)

| Feature | Frequency | Difficulty | Value |
|---|---|---|---|
| `function` | Very high | Medium | Unlocks parametric designs |
| Block comments | High | Low | Removal of common annoyance |
| `$fn` | High | Low | Better preview quality |
| `echo` | Medium | Low | Debugging |
| `len` | Medium | Low | Basic vector introspection |
| `undef` | Medium | Low | Closer to real OpenSCAD |
| `norm` / `cross` | Low | Low | Vector math |
| `rands` | Low | Low | Randomization |
| `str` / `concat` | Low | Medium | String ops |

## Priority Matrix (tree-change)

| Feature | Frequency | Difficulty | Value |
|---|---|---|---|
| `rotate_extrude` | High | Medium | Lathe-like shapes |
| `resize` | Medium | Low | Practical transform |
| `text` | Medium | High | Labeling/engraving |
| `linear_extrude` params | Medium | Medium | Twisted extrusions |
| `children()` | Medium | Very high | Reusable modules |
| `use` / `include` | High | High | Multi-file projects |
| `import` | Medium | Medium | External STL |
