# Debug Log Format — Scene Tree Debugger

Opened via **Debug → Log Snapshot Now** or triggered automatically whenever a tree action occurs (scroll-adjust, drag, rename, delete).  
The log is displayed in the right panel; **copy and paste the full text** to share with Claude.

---

## Event Lines

Every interactive action appends a one-line event record:

```
[NNNN] <type>  <details>  cursor=(<sx>,<sy>)
```

| Field | Meaning |
|---|---|
| `[NNNN]` | 4-digit sequential event number (resets on "Clear Log") |
| `<type>` | Event category (see table below) |
| `cursor=(sx,sy)` | Last known mouse position **in scene coordinates** at the moment of the event |

### Event types

| Type | Trigger | Key extra fields |
|---|---|---|
| `transform` | Mouse-wheel on a transform (translate / rotate / scale) parameter pill | `node=#N axis=X/Y/Z "old"→"new" Δ=±d  start=S len=L  pill=(x,y w×h)` |
| `for-loop`  | Mouse-wheel on a for-loop range number | `node=#N "old"→"new" Δ=±d  start=S len=L  pill=(x,y w×h)` |
| `variable`  | Mouse-wheel on a variable expression number | `node=#N "varName" "old"→"new" Δ=±d  start=S len=L  pill=(x,y w×h)` |
| `shape`     | Mouse-wheel on a primitive shape parameter | `node=#N param=P "old"→"new" Δ=±d  start=S len=L  pill=(x,y w×h)` |
| `modcall`   | Mouse-wheel on a module-call argument | `start=S len=L Δ=±d` |
| `toolDrop`  | Tool dragged from toolbar onto tree | `"toolName"  parent=#N  idx=I` |
| `nodeDrop`  | Tree node dragged to new position | `node=#N  parent=#N  idx=I` |
| `select`    | Node clicked (selected) | `node=#N  cursor=(sx,sy)` |
| `delete`    | Node deleted (Delete key) | `node=#N` |
| `modRename` | Module header double-clicked and renamed | `#N → "newName"` |
| `varRename` | Variable name double-clicked and renamed | `#N → "newName"` |

### Pill / hit rect fields

```
pill=(x,y w×h)
```

- `x,y` — top-left of the interactive number pill **in scene coordinates** (integer pixels)
- `w×h` — width × height of the pill (integer pixels)

This is the rect that the hit-test code uses. If cursor position does not overlap it, there is a mismatch between rendering and hit-testing.

### start / len fields

```
start=S len=L
```

- `start` — character offset within the expression string where the matched number begins
- `len` — character count of the matched number

Example: expression `[0 : 10 : 100]`, number `10` → `start=5 len=2`.

---

## Snapshot Blocks

A snapshot is taken automatically after every action and on "Log Snapshot Now".  
It shows the **entire tree** with computed rects at the moment of capture.

```
=== Snapshot #N (context) ===
shapes=S
[TypeLabel#id  description]  card=(x,y w×h)
  pill "text"  start=S len=L  hit=(x,y w×h)
  ...
---
```

### Node types in snapshots

| Label | Meaning |
|---|---|
| `Module#N "name"` | Module group |
| `Scene#N` | Root scene group |
| `For#N  var=[range]` | For-loop group |
| `Trans#N` / `Rot#N` / `Scale#N` | Transform groups |
| `Union#N` / `Diff#N` / `Inter#N` | Boolean groups |
| `Var#N  name = expr` | Variable node |
| `Prim#N  cube/sphere/cylinder` | Primitive shape node |
| `Call#N` | Module-call node |

### card rect

```
card=(x,y w×h)
```

The bounding rectangle of the entire node card in scene coordinates.  
For group nodes this is the full card including header + children area.  
For leaf nodes (Variable, Primitive, ModuleCall) it is the single row card.

---

## Reading a mismatch report

If the cursor is to the **right** of the visual pill when scrolling activates:

1. Find the event line — `pill=(x,y w×h)` shows the hit rect.
2. Find `cursor=(sx,sy)` — compare with pill left/right edges.
3. In the snapshot find the same node — `hit=(x,y w×h)` per pill.
4. If `hit.x > visual_x` the hit rect is shifted right relative to rendering → font metrics inconsistency.
5. If `hit.x < visual_x` the hit rect is shifted left.

### Example

```
[0003] for-loop  node=#5  "10"→"11"  Δ=+1.0  start=5 len=2  pill=(130,173 22×14)  cursor=(136,180)
=== Snapshot #3 (after for-loop) ===
shapes=5
[For#5  i=[0:11:100]]  card=(24,168 396×54)
  pill "0"    start=1 len=1  hit=(108,173 18×14)
  pill "11"   start=5 len=2  hit=(130,173 22×14)
  pill "100"  start=9 len=3  hit=(158,173 26×14)
---
```

Reading: cursor was at scene (136,180); hit rect for "11" is x=130…152 → cursor is inside. ✓

---

## Coordinate system

- Origin (0,0) is at the **top-left of the QGraphicsScene**.
- The scene coordinate system does **not** change when the widget is scrolled or resized — positions are stable across redraws.
- Widget coordinates (only logged as `cursor=` in the raw event, from `event->pos()`) can differ by the scroll offset; the scene coordinates are preferred for comparison.

---

## How to use

1. Launch the app — initial snapshot is logged automatically.
2. Hover over a pill, use the mouse wheel to adjust a value.
3. An event line and a snapshot appear in the log (newest at top).
4. Select all log text (`Ctrl+A`), copy (`Ctrl+C`), paste to Claude.
5. Describe what you saw visually ("cursor was X pixels to the right of the pill").  
   Claude will cross-reference the `pill=` and `cursor=` fields to locate the mismatch.
