#ifndef SCENETREEGRAPHICSCONSTANTS_H
#define SCENETREEGRAPHICSCONSTANTS_H

#include <QColor>
#include <QRectF>

namespace SceneTreeGraphics {

constexpr qreal ToolbarX = 12.0;
constexpr qreal ToolbarY = 12.0;
constexpr qreal ToolSize = 36.0;
constexpr qreal ToolGap = 5.0;
constexpr qreal TreeX = 12.0;
constexpr qreal TreeY = 92.0;
constexpr qreal PrimitiveWidth = 104.0;
constexpr qreal PrimitiveHeight = 38.0;
constexpr qreal PrimitiveCardWidth = 54.0;
constexpr qreal PrimitiveParamLabelArea = 28.0;
constexpr qreal VariableHeight = 26.0;
constexpr qreal PrimitiveIconSize = 28.0;
constexpr qreal GroupMinWidth = 108.0;
constexpr qreal GroupModuleMinWidth = 150.0;
constexpr qreal GroupWideMinWidth = 140.0;
constexpr qreal GroupHeaderHeight = 28.0;
constexpr qreal TransformHeaderWidth = 94.0;
constexpr qreal TransformIconWidth = 36.0;
constexpr qreal TransformParamLabelArea = 22.0;
constexpr qreal GroupPadding = 9.0;
constexpr qreal ChildGap = 5.0;
// Width of the reserved left gutter in a difference group that holds the
// vertical "base"/"cut" labels, so they sit flush to the edge without
// overlapping the child icons.
constexpr qreal DifferenceLabelGutter = 16.0;
constexpr qreal CornerRadius = 10.0;
constexpr qreal DifferenceMinContentHeight = PrimitiveHeight * 2.0 + ChildGap;
constexpr qreal DragPreviewStartDistance = 6.0;
constexpr qreal CanvasMargin = 2000.0;

constexpr qreal OverlayBaseZ     = 10000.0;
constexpr qreal HintOverlayZ     = 10020.0;
constexpr qreal OverlayMargin    = 12.0;
constexpr qreal OverlayBottomGap = 12.0;

} // namespace SceneTreeGraphics

// Center checkbox rect for primitive cards that support center=true/false.
// Returns a 14×14 rect at the bottom-right corner of the icon card area.
inline QRectF centerCheckboxRect(const QRectF &primitiveRect)
{
    constexpr qreal cbSize = 14.0;
    const qreal cardRight = primitiveRect.left() + SceneTreeGraphics::PrimitiveCardWidth;
    return QRectF(cardRight - cbSize - 2.0,
                  primitiveRect.bottom() - cbSize - 2.0,
                  cbSize, cbSize);
}

// Vertical "base"/"cut" pill label rect inside the reserved left gutter of a
// difference group. `baseLabel` selects the upper (base) or lower (cut) section.
// Returns an empty rect when the section is too short to hold a readable label.
inline QRectF differenceLabelRect(const QRectF &groupRect, qreal cutSeparatorY, bool baseLabel)
{
    constexpr qreal labelWidth = SceneTreeGraphics::DifferenceLabelGutter - 3.0;
    constexpr qreal maxHeight  = 42.0;
    const qreal left = groupRect.left() + 2.0;

    qreal top = 0.0;
    qreal bottom = 0.0;
    if (baseLabel) {
        top    = groupRect.top() + SceneTreeGraphics::GroupHeaderHeight + SceneTreeGraphics::GroupPadding;
        bottom = cutSeparatorY - 4.0;
    } else {
        top    = cutSeparatorY + 4.0;
        bottom = groupRect.bottom() - SceneTreeGraphics::GroupPadding;
    }

    const qreal height = qMin(maxHeight, bottom - top);
    if (height < 18.0)
        return QRectF();
    const qreal y = top + (bottom - top - height) * 0.5;
    return QRectF(left, y, labelWidth, height);
}

inline bool shapeSupportsCenter(int shapeType)
{
    return shapeType == 0  // Cube
        || shapeType == 2  // Cylinder
        || shapeType == 3  // Cone
        || shapeType == 5; // Square
}

#endif
