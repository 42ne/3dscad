# Tree Widget UX Improvement Roadmap

Status as of: 2026-05-25.

Planned usability enhancements for `SceneTreeGraphicsWidget` (the visual scene tree),
with cross-references to test scenarios in `tools/testrunner/`.

---

## 🔴 Critical

### 1. Keyboard navigation
- **Status:** ❌ Not started
- **File:** `scenetreegraphicswidget.cpp` — `keyPressEvent`
- **Description:** Only `Delete`/`Backspace` handled. Add `Up/Down` to traverse siblings,
  `Left/Right` to collapse/expand groups, `Enter` to rename, `Escape` to cancel.
- **Test:** New testrunner scenario needed — select nodes with keyboard, verify selection changes.

### 2. Collapse / Expand groups
- **Status:** ❌ Not started
- **File:** `scenetreenoderenderer.cpp` (chevron is decorative), `scenetreegraphicswidget.cpp`
- **Description:** Chevron drawn but not clickable. Need `QSet<int> m_collapsedGroupIds`,
  click-to-toggle, skip children in `drawGroup()`, animated size transition.
- **Test:** New testrunner scenario — collapse a group, add children, expand, verify structure.

---

## 🟡 High Priority

### 3. Right-click context menu
- **Status:** ❌ Not started
- **File:** `scenetreegraphicswidget.cpp` — `contextMenuEvent` / `mousePressEvent`
- **Description:** Delete, Rename, Copy, Cut, Paste, Duplicate, Wrap in group, Select Parent.
- **Test:** Would need synthetic right-click in testrunner → `QTest::mouseClick(..., Qt::RightButton)`.

### 4. Multi-select
- **Status:** ❌ Not started
- **File:** `scenetreegraphicswidget.h` — `m_selectedTreeNodeId` is scalar `int`
- **Description:** Replace with `QSet<int> m_selectedNodeIds`. Ctrl+click toggle, Shift+click range.
- **Test:** New testrunner scenario — select multiple, verify `treeNodeSelected` signals.

### 5. Search / filter bar
- **Status:** ❌ Not started
- **File:** New floating overlay or embedded QLineEdit
- **Description:** Filter nodes by name, type, id. Highlight matches, dim non-matches.
- **Test:** Manual only (text input simulation complex in testrunner).

### 6. Copy / Cut / Paste / Duplicate
- **Status:** ❌ Not started
- **File:** New signals + clipboard serialization
- **Description:** Ctrl+C/X/V/D. Serialize subtree to JSON/MIME for clipboard.
- **Test:** New testrunner scenario — duplicate a subtree, verify tree structure.

---

## 🟠 Medium Priority

### 7. Parent-ancestor highlight
- **Status:** ❌ Not started
- **File:** `scenetreenoderenderer.cpp` — `GroupCardItem::paint`
- **Description:** When child selected, draw ancestor group borders in secondary color.
- **Test:** Visual inspection.

### 8. Zoom reset (Ctrl+0) + Fit to node + Smooth zoom with inertia
- **Status:** ⏳ Partial (smooth zoom ✅ done, reset/fit ❌ not started)
- **File:** `scenetreegraphicswidget.cpp` — `wheelEvent`, `m_zoomAnimTimer`
- **Description:**
  - Wheel event pushes `m_zoomTarget` (×1.12/÷1.12) and boosts `m_zoomVelocity`.
  - 16ms timer: ease factor = 0.25 + `|velocity|/800` (capped 0.9).
    - Slow scroll → ease 0.25 → smooth 150ms animation.
    - Fast scroll (v=600) → ease 0.9 → near-instant catch-up in ~7 frames.
    - Coasting: velocity decays ×0.92 → ease drops → gentle finish.
  - Step clamped to max 12%/frame. Anchor point under cursor stays stable via
    `mapFromScene()` before/after `scale()` with scrollbar compensation.
  - Ctrl+0 reset: ❌ not yet.
  - Fit to node: ❌ not yet.
- **Details:**
  - `scenetreegraphicswidget.h:274-279` — `m_zoomLevel`, `m_zoomTarget`, `m_zoomVelocity`, `m_zoomAnimTimer`, `m_zoomAnchorScene`
  - `scenetreegraphicswidget.cpp:388-421` — timer tick: ease scaling, anchored scale, velocity decay
  - `scenetreegraphicswidget.cpp:1144-1158` — wheelEvent: push target + boost velocity
- **Test:** Manual — scroll slowly → smooth; scroll fast → responsive; release → coasts to stop.

### 9. Scroll to selected on programmatic selection
- **Status:** ❌ Not started
- **File:** `scenetreegraphicswidget.cpp` — `setSelectedTreeNodeId()`
- **Description:** After setting m_selectedTreeNodeId and refresh, call `ensureVisible()`.
- **Test:** `setSelectedTreeNodeId(id)` via testrunner, verify viewport moved.

### 10. Undo/redo visual feedback
- **Status:** ❌ Not started
- **File:** `scenetreegraphicswidget.cpp` — `refresh()` or post-refresh
- **Description:** After undo, briefly pulse affected node border (300ms opacity).
- **Test:** New testrunner undo/redo step with visual check (hard to automate).

### 11. Invalid drop zone visual feedback
- **Status:** ❌ Not started
- **File:** `scenetreegraphicswidget.cpp` — `renderDropPreviewFrame`
- **Description:** Red-tinted overlay + "no entry" icon when drop rejected.
- **Test:** New testrunner scenario — attempt invalid drop, verify visual feedback.

---

### 14. Pan inertia (momentum scrolling)
- **Status:** ✅ Done (2026-05-25)
- **File:** `scenetreegraphicswidget.cpp` — `mouseMoveEvent` / `mouseReleaseEvent`
- **Description:** Track pan velocity via `m_panVelocity`; on release start `m_panInertiaTimer`
  (16ms interval) that decelerates with 0.92 damping factor. Stops when velocity < 2px.
  New press or scroll resets inertia immediately.
- **Details:**
  - `scenetreegraphicswidget.h:270-272` — added `m_panVelocity` (QPointF), `m_panInertiaTimer` (QTimer*)
  - `scenetreegraphicswidget.cpp:369-383` — timer init in constructor
  - `scenetreegraphicswidget.cpp:674-680` — stop inertia on any left-button press
  - `scenetreegraphicswidget.cpp:914` — track `m_panVelocity = delta` during pan
  - `scenetreegraphicswidget.cpp:996-1001` — start inertia on release if velocity > 8px
- **Test:** Manual — pan viewport, release while moving, observe deceleration.

---

## 🟢 Low Priority

### 12. Empty group affordance ("+" button)
- **Status:** ❌ Not started
- **File:** `scenetreenoderenderer.cpp`
- **Description:** Dashed border + "+" icon in empty groups for quick child insertion.
- **Test:** Visual inspection + manual click.

### 13. Keyboard shortcut cheat-sheet overlay
- **Status:** ❌ Not started
- **File:** `scenetreegraphicswidget.cpp` — new `?` handler
- **Description:** Popup overlay listing all keyboard shortcuts.
- **Test:** Manual.

### 15. VAR / PAR icon differentiation
- **Status:** ❌ Not started
- **File:** `scenetreenoderenderer.cpp` — `VariableCardItem::paint`
- **Description:** Visual icon difference (e.g., `@` for param, `$` for local) beyond just text.
- **Test:** Visual inspection.

---

## Implementation Order (Suggested)

1. **Pan inertia (#14)** — small, self-contained, high impact ✓ DONE
2. **Collapse/expand (#2)** — unlocks large-scene usability
3. **Keyboard navigation (#1)** — needed before most other features
4. **Context menu (#3)** — low code risk, high user value
5. **Scroll to selected (#9)** — trivial fix, big impact
6. **Zoom reset (#8)** — also trivial
7. **Parent-ancestor highlight (#7)** — improves orientation in deep trees
8. **Multi-select (#4)** — needed for bulk operations
9. **Copy/cut/paste (#6)** — clipboard infrastructure
10. **Empty group affordance (#12)** — quick-add
11. **Invalid drop feedback (#11)** — polish
12. **Undo/redo feedback (#10)** — polish
13. **Search/filter (#5)** — advanced feature
14. **Shortcut overlay (#13)** — nice-to-have
15. **VAR/PAR icons (#15)** — nice-to-have
