# Refactoring Progress Summary

## Goal
- Extract heavy sub-systems out of `ViewportWidget` into dedicated classes; consolidate duplicated utility code; decompose the `SceneTreeGraphicsWidget` god-class.

## Progress

### Done
#### Viewport extractions (previous session)
- **Software renderer** → `ViewportSoftwareRenderer` (functional `Context`-struct, no friend)
- **OpenGL renderer** → `ViewportGLRenderer` (friend class, owns all VBOs/shaders/cache)
- **Axis gizmo** → `ViewportAxisGizmo` (friend class; +7 polyhedron selection helpers)
- **Transform/shape overlay preview** → `ViewportOverlayPreview` (friend class)
- **ViewportCamera** → new class: owns yaw/pitch/distance/target/orthographic
- **ViewportConstants** → 18 named constants in standalone header

#### Utility consolidation
- `isValidIdentifier` deduplicated (3 copies → inline in `scenestringutils.h`)
- `splitAtTopLevelCommas` deduplicated (found and removed from `openscadparser.cpp`)
- Number-adjustment preamble deduplicated (4 methods → `adjustedNumberReplacement` helper + `setCtrlHighlight` method in `SceneController`)

#### Scene tree god-class decomposition (this session)
- **Color-edit mode** → `SceneTreeColorEditMode` (~1210 lines extracted from `scenetreegraphicswidget.cpp`)
  - New files: `scenetreecoloreditmode.h/.cpp`
  - Structs `ColorZoneHit`/`ColorPropDef` moved from widget class
  - Friend class access to widget internals
  - `GridBlinkItem` moved with the class
  - `ColorEditToggleItem` stays in widget (used by toolbar rendering)
  - Widget .cpp: **6195 → ~4990 lines** (−19%)

### In Progress
- *(none)*

### Blocked
- *(none)*

## Key Decisions
- New classes use `friend class` pattern (consistent with viewport renderers)
- Signal `inlineThemeEdited` bridged from `SceneTreeColorEditMode` → `SceneTreeGraphicsWidget`

## Next Steps
- *(user decides)* — remaining extractions from `SceneTreeGraphicsWidget`:
  1. Hover manager (~450 lines: tooltips, hover highlights)
  2. Hit tester (~550 lines: all `controlAt` functions)
  3. Wheel dispatcher (~200 lines: `handle*Wheel` functions)
  4. Active control tracker (~300 lines)
  5. Inline edit controller (~380 lines)
  6. Or another target entirely
