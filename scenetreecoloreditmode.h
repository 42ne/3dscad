#ifndef SCENETREECOLOREDITMODE_H
#define SCENETREECOLOREDITMODE_H

#include "scenedocument.h"

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QColor;
class QGraphicsItem;
class QTimer;
class SceneTreeGraphicsWidget;

class SceneTreeColorEditMode : public QObject
{
    Q_OBJECT

public:
    struct ColorZoneHit {
        QString fieldName;      // "canvas"|"card"|"header"|"input"|"hint"|"toolbar"|"toolbar_controls"
        QString label;          // human-readable zone name for the hover hint
        QRectF  rect;           // zone rect in scene coords (or local item rect for hint/toolbar)
        bool    valid = false;
        SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
        bool    hasOperation = false;
        int     nodeId = 0;       // scene node id; 0 = none
        QString spanText     = {}; // for direct numText/mutedText hits: the span's text content
        QPointF itemScenePos = {}; // for hint/toolbar hits: scene pos of the ItemIgnoresTransformations panel
    };
    struct ColorPropDef {
        QString id;       // field name in TreeAppearanceTheme / OperationCardPalette
        QString label;    // shown in hint + dialog title
        bool isText   = false; // true → blink the highlight
        bool isGlobal = false; // true = global theme; false = per-op palette
    };

    explicit SceneTreeColorEditMode(SceneTreeGraphicsWidget *widget);

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    void handleClick(const QPointF &scenePos);
    void handleWheel(const QPointF &scenePos, int angleDelta);
    void updateHighlight(const QPointF &scenePos);
    void clearHighlight();
    ColorZoneHit zoneAt(const QPointF &scenePos) const;

signals:
    void enabledChanged(bool enabled);
    void inlineThemeEdited();

private:
    QVector<ColorPropDef> propsForHit(const ColorZoneHit &hit) const;
    QColor getColorForProp(const ColorPropDef &prop, const ColorZoneHit &hit) const;
    void setColorForProp(const ColorPropDef &prop, const ColorZoneHit &hit, const QColor &color);

    SceneTreeGraphicsWidget *m_widget;
    bool m_enabled = false;
    QGraphicsItem *m_highlight = nullptr;
    QVector<QGraphicsItem*> m_blinkTargets;
    QString m_zoneField;
    int m_propIndex = 0;
    ColorZoneHit m_currentZone;
    bool m_blinkOn = true;
    QTimer *m_blinkTimer = nullptr;
};

#endif // SCENETREECOLOREDITMODE_H
