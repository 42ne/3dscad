#include "scenetreecoloreditmode.h"
#include "scenetreegraphicshelpers.h"
#include "scenetreegraphicswidget.h"
#include "scenetreehovermanager.h"
#include "scenetreepalette.h"
#include "scenestringutils.h"

#include <QApplication>
#include <QColorDialog>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>
#include <QPen>
#include <QTimer>
#include <QWidget>

using namespace SceneTreeGraphics;

// ── Grid blink overlay (for canvas grid color-edit preview) ─────────────────────

class GridBlinkItem : public QGraphicsItem
{
public:
    GridBlinkItem(const QRectF &sceneRect, qreal gridSize, const QColor &color)
        : m_sceneRect(sceneRect), m_gridSize(gridSize), m_color(color) {}
    QRectF boundingRect() const override { return m_sceneRect; }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        drawCanvasGrid(p, m_sceneRect, m_gridSize, m_color, 3);
    }
private:
    QRectF  m_sceneRect;
    qreal   m_gridSize;
    QColor  m_color;
};

// ── Color-edit helpers ────────────────────────────────────────────────────────

static QString colorEditOpName(SceneDocument::TreeNode::Operation op)
{
    switch (op) {
    case SceneDocument::TreeNode::Union:        return QStringLiteral("Union");
    case SceneDocument::TreeNode::Difference:   return QStringLiteral("Difference");
    case SceneDocument::TreeNode::Intersection: return QStringLiteral("Intersection");
    case SceneDocument::TreeNode::Module:       return QStringLiteral("Module");
    case SceneDocument::TreeNode::Translate:    return QStringLiteral("Translate");
    case SceneDocument::TreeNode::Rotate:       return QStringLiteral("Rotate");
    case SceneDocument::TreeNode::Scale:        return QStringLiteral("Scale");
    case SceneDocument::TreeNode::Mirror:       return QStringLiteral("Mirror");
    case SceneDocument::TreeNode::Hull:         return QStringLiteral("Hull");
    case SceneDocument::TreeNode::For:          return QStringLiteral("For");
    case SceneDocument::TreeNode::Scene:        return QStringLiteral("Scene");
    case SceneDocument::TreeNode::Minkowski:    return QStringLiteral("Minkowski");
    case SceneDocument::TreeNode::Polyhedron:   return QStringLiteral("Polyhedron");
    case SceneDocument::TreeNode::Color:        return QStringLiteral("Color");
    case SceneDocument::TreeNode::LinearExtrude:return QStringLiteral("Linear extrude");
    case SceneDocument::TreeNode::Resize:       return QStringLiteral("Resize");
    }
    return QStringLiteral("Group");
}

// ── SceneTreeColorEditMode ────────────────────────────────────────────────────

SceneTreeColorEditMode::SceneTreeColorEditMode(SceneTreeGraphicsWidget *widget)
    : QObject(widget)
    , m_widget(widget)
{
}

void SceneTreeColorEditMode::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    clearHighlight();
    m_propIndex = 0;
    m_currentZone = {};
    m_blinkOn = true;

    if (enabled) {
        if (!m_blinkTimer) {
            m_blinkTimer = new QTimer(this);
            m_blinkTimer->setInterval(250);
            connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
                m_blinkOn = !m_blinkOn;
                if (m_highlight)
                    m_highlight->setVisible(m_blinkOn);
                for (QGraphicsItem *it : m_blinkTargets)
                    if (it && it->scene())
                        it->setVisible(m_blinkOn);
            });
        }
        m_blinkTimer->start();
        m_widget->setCursor(Qt::PointingHandCursor);
        m_widget->m_hoverManager->updateHoverHint(QStringLiteral("colorEdit:mode"),
                        QStringLiteral("✏ Color edit\n"
                                       "Hover a card, then scroll ↕ to cycle properties.\n"
                                       "Click to pick a color.  Esc to exit."));
    } else {
        if (m_blinkTimer)
            m_blinkTimer->stop();
        m_zoneField.clear();
        m_widget->setCursor(Qt::OpenHandCursor);
        m_widget->m_hoverManager->updateHoverHint(QString(), QString());
    }
    m_widget->updateToolbarOverlay();
    emit enabledChanged(enabled);
}

SceneTreeColorEditMode::ColorZoneHit
SceneTreeColorEditMode::zoneAt(const QPointF &scenePos) const
{
    const QPointF vpCursor(m_widget->mapFromScene(scenePos));
    QGraphicsItem *topToolbarItem = nullptr;
    qreal topZ = -1.0;
    for (QGraphicsItem *it : m_widget->m_overlay->m_items) {
        if (!it->isVisible())
            continue;
        const QRectF vpRect = it->boundingRect().translated(m_widget->mapFromScene(it->pos()));
        if (vpRect.contains(vpCursor) && it->zValue() > topZ) {
            topToolbarItem = it;
            topZ = it->zValue();
        }
    }
    if (topToolbarItem) {
        const QString zone = topToolbarItem->data(0).toString();
        if (zone == QLatin1String("glass_hint")) {
            ColorZoneHit h;
            h.fieldName    = QStringLiteral("hint");
            h.label        = QStringLiteral("Hint panel");
            h.valid        = true;
            h.rect         = topToolbarItem->boundingRect();
            h.itemScenePos = topToolbarItem->pos();
            return h;
        }
        if (zone == QLatin1String("glass_toolbar")) {
            ColorZoneHit h;
            h.fieldName    = QStringLiteral("toolbar");
            h.label        = QStringLiteral("Toolbar panel");
            h.valid        = true;
            h.rect         = topToolbarItem->boundingRect();
            h.itemScenePos = topToolbarItem->pos();
            return h;
        }
        return {QStringLiteral("toolbar_controls"), QString(), QRectF(), false,
                SceneDocument::TreeNode::Union, false};
    }

    const SceneTreeLayout::GroupHitArea *best = nullptr;
    for (const SceneTreeLayout::GroupHitArea &area : m_widget->m_treeLayout.groupHitAreas()) {
        if (!area.rect.contains(scenePos))
            continue;
        const qreal area2 = area.rect.width() * area.rect.height();
        if (!best || area2 < best->rect.width() * best->rect.height())
            best = &area;
    }

    if (best) {
        const SceneDocument::TreeNode::Operation op = best->operation;
        for (const SceneTreeLayout::ChildLayout &child : best->children) {
            if (!child.rect.contains(scenePos))
                continue;
            if (m_widget->m_scene) {
                const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(child.nodeId);
                if (node && node->type == SceneDocument::TreeNode::Variable) {
                    const QFont font = sceneTreeGraphicsFont();
                    const QFontMetricsF metrics(font);
                    const qreal nameW = metrics.horizontalAdvance(node->variableName);
                    const QVector<ExpressionTextSpan> spans =
                        expressionTextSpans(child.rect, node->variableExpression, metrics, nameW);
                    for (const ExpressionTextSpan &span : spans) {
                        if (span.rect.contains(scenePos)) {
                            const QString fn = span.number
                                ? QStringLiteral("numText") : QStringLiteral("mutedText");
                            ColorZoneHit h{fn, span.number
                                ? QStringLiteral("Number constant") : QStringLiteral("Formula"),
                                span.rect, true, op, true};
                            h.nodeId = child.nodeId;
                            h.spanText = span.text;
                            return h;
                        }
                    }
                    const qreal eqLeft = child.rect.left() + 38.0 + nameW + 4.0;
                    const QRectF eqRect(eqLeft, child.rect.top() + (VariableHeight - 16.0) * 0.5, 12.0, 16.0);
                    if (eqRect.contains(scenePos)) {
                        ColorZoneHit h{QStringLiteral("numLabelText"), QStringLiteral("Label color"),
                                        eqRect, true, op, true};
                        h.nodeId = child.nodeId;
                        return h;
                    }
                    const QString label = node->isParameter
                        ? QStringLiteral("Parameter row")
                        : QStringLiteral("Variable row");
                    return {QStringLiteral("input"), label, child.rect, true, op, true};
                }
                if (node && node->type == SceneDocument::TreeNode::Primitive) {
                    const ShapeNode *shape = m_widget->m_scene->shapeById(node->shapeId);
                    if (shape) {
                        const QFontMetricsF metrics(sceneTreeValueFont());
                        const QVector<ShapeParameterControl> controls = shapeParameterControls(*shape);
                        const int count = controls.size();
                        for (int i = 0; i < count; ++i) {
                            const QRectF rowRect = shapeParameterControlRect(child.rect, i, count);
                            if (!rowRect.contains(scenePos)) continue;
                            const QRectF labelRect(rowRect.left(), rowRect.top(),
                                                   PrimitiveParamLabelArea - 2.0, rowRect.height());
                            if (labelRect.contains(scenePos)) {
                                ColorZoneHit h{QStringLiteral("numLabelText"), QStringLiteral("Param label"),
                                               child.rect, true, op, true};
                                h.nodeId = child.nodeId;
                                return h;
                            }
                            const QRectF textRect = shapeUsesExpressionPillLayout(*shape)
                                ? shapeParameterFieldRect(rowRect, *shape)
                                : QRectF(rowRect.left() + PrimitiveParamLabelArea, rowRect.top(),
                                         rowRect.width() - PrimitiveParamLabelArea, rowRect.height());
                            const ExpressionPillLayout pillLayout(textRect, controls[i].expression, metrics);
                            const QVector<ExpressionTextSpan> spans = shapeUsesExpressionPillLayout(*shape)
                                ? pillLayout.spans()
                                : expressionSpansInTextRect(textRect, controls[i].expression, metrics);
                            for (const ExpressionTextSpan &span : spans) {
                                const QRectF spanRect = shapeUsesExpressionPillLayout(*shape)
                                    ? pillLayout.visualRectFor(span)
                                    : span.rect;
                                if (!spanRect.contains(scenePos)) continue;
                                const QString fn = span.number
                                    ? QStringLiteral("numText") : QStringLiteral("mutedText");
                                ColorZoneHit h{fn, span.number ? QStringLiteral("Number constant")
                                                               : QStringLiteral("Formula"),
                                               spanRect, true, op, true};
                                h.nodeId = child.nodeId;
                                h.spanText = span.text;
                                return h;
                            }
                        }
                    }
                }
                if (node && node->type == SceneDocument::TreeNode::ModuleCall) {
                    const SceneDocument::TreeNode *modGroup = m_widget->m_scene->treeNodeById(node->shapeId);
                    if (modGroup && modGroup->operation == SceneDocument::TreeNode::Module) {
                        const QFontMetricsF metrics(sceneTreeGraphicsFont());
                        QVector<ModuleCallParam> params;
                        const QHash<QString, QString> overrides =
                            resolveModuleArguments(node->moduleCallArguments, *modGroup);
                        for (const SceneDocument::TreeNode &pChild : modGroup->children) {
                            if (pChild.type == SceneDocument::TreeNode::Variable && pChild.isParameter) {
                                const QString expr = overrides.value(
                                    pChild.variableName,
                                    pChild.variableExpression.trimmed().isEmpty()
                                        ? QString::number(pChild.variableValue)
                                        : pChild.variableExpression.trimmed());
                                params.append({pChild.id, pChild.variableName, expr});
                            }
                        }
                        qreal x = child.rect.left() + 46.0
                                  + metrics.horizontalAdvance(node->moduleName + QStringLiteral("("));
                        for (int i = 0; i < params.size(); ++i) {
                            x += metrics.horizontalAdvance(params[i].name + QStringLiteral(" = "));
                            const QRectF exprRect(x, child.rect.top(),
                                                   child.rect.right() - x, VariableHeight);
                            for (const ExpressionTextSpan &span :
                                     expressionSpansInTextRect(exprRect, params[i].expression, metrics)) {
                                if (!span.rect.contains(scenePos)) continue;
                                const QString fn = span.number
                                    ? QStringLiteral("numText") : QStringLiteral("mutedText");
                                ColorZoneHit h{fn, span.number ? QStringLiteral("Number constant")
                                                               : QStringLiteral("Formula"),
                                               span.rect, true, op, true};
                                h.nodeId = child.nodeId;
                                h.spanText = span.text;
                                return h;
                            }
                            x += metrics.horizontalAdvance(params[i].expression);
                            if (i < params.size() - 1)
                                x += metrics.horizontalAdvance(QStringLiteral(", "));
                        }
                    }
                }
            }
            bool isLeafCard = false;
            QString leafLabel = QStringLiteral("Card body");
            if (m_widget->m_scene) {
                const SceneDocument::TreeNode *cn = m_widget->m_scene->treeNodeById(child.nodeId);
                if (cn) {
                    if (cn->type == SceneDocument::TreeNode::Primitive) {
                        isLeafCard = true;
                        leafLabel  = QStringLiteral("Primitive card");
                    } else if (cn->type == SceneDocument::TreeNode::ModuleCall) {
                        isLeafCard = true;
                        leafLabel  = QStringLiteral("Module call card");
                    }
                }
            }
            ColorZoneHit h{QStringLiteral("card"), leafLabel, child.rect,
                           true, op, !isLeafCard};
            h.nodeId = child.nodeId;
            return h;
        }

        if (op == SceneDocument::TreeNode::Module
            && best->moduleParameterSeparatorY > 0.0 && m_widget->m_scene)
        {
            const SceneDocument::TreeNode *modNode = m_widget->m_scene->treeNodeById(best->groupId);
            if (modNode) {
                QVector<ModuleCallParam> tmplParams;
                for (const SceneDocument::TreeNode &pc : modNode->children) {
                    if (pc.type == SceneDocument::TreeNode::Variable && pc.isParameter) {
                        const QString expr = pc.variableExpression.trimmed().isEmpty()
                            ? QString::number(pc.variableValue)
                            : pc.variableExpression.trimmed();
                        tmplParams.append({pc.id, pc.variableName, expr});
                    }
                }
                const QSizeF tmplSz = moduleCallPreviewSize(modNode->moduleName, tmplParams);
                const QRectF tmplRect(best->rect.left() + GroupPadding,
                                      best->moduleParameterSeparatorY
                                          - ChildGap * 0.5 + 16.0 + ChildGap,
                                      tmplSz.width(), tmplSz.height());
                if (tmplRect.contains(scenePos)) {
                    ColorZoneHit h{QStringLiteral("card"), QStringLiteral("Call template"),
                                   tmplRect, true, op, false};
                    h.nodeId = -best->groupId;
                    return h;
                }
            }
        }

        const QRectF hdr = isVerticalHeaderOperation(op)
            ? QRectF(best->rect.left(), best->rect.top(),
                     TransformIconWidth, best->rect.height())
            : QRectF(best->rect.left(), best->rect.top(),
                     best->rect.width(), GroupHeaderHeight);
        if (hdr.contains(scenePos)) {
            if (op == SceneDocument::TreeNode::For && m_widget->m_scene) {
                const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(best->groupId);
                const QString varName     = node ? forLoopVariableName(*node)     : QStringLiteral("i");
                const QString rangeExpr   = node ? forLoopRangeExpression(*node)  : QStringLiteral("[0 : 1 : 3]");
                const QFontMetricsF metrics(sceneTreeGraphicsFont());
                const QVector<ExpressionTextSpan> spans =
                    forLoopRangeTextSpans(best->rect, varName, rangeExpr, metrics);
                for (const ExpressionTextSpan &span : spans) {
                    if (span.rect.contains(scenePos)) {
                        const QString fn = span.number
                            ? QStringLiteral("numText") : QStringLiteral("mutedText");
                        ColorZoneHit h{fn, span.number
                            ? QStringLiteral("Number constant") : QStringLiteral("Formula"),
                            span.rect, true, op, true};
                        h.nodeId = best->groupId;
                        h.spanText = span.text;
                        return h;
                    }
                }
                const QString prefix = QStringLiteral("for (%1 = ").arg(varName);
                const qreal textLeft = best->rect.left() + 30.0 + (PrimitiveIconSize - 4.0) + 10.0;
                const QRectF prefixRect(textLeft, best->rect.top() + 7.0,
                                        metrics.horizontalAdvance(prefix), 16.0);
                if (prefixRect.contains(scenePos)) {
                    ColorZoneHit h{QStringLiteral("numLabelText"), QStringLiteral("Loop label"),
                                    prefixRect, true, op, true};
                    h.nodeId = best->groupId;
                    return h;
                }
            }
            ColorZoneHit h{QStringLiteral("header"), QStringLiteral("Card header"), hdr, true, op, true};
            h.nodeId = best->groupId;
            return h;
        }

        if (isVerticalHeaderOperation(op) && m_widget->m_scene) {
            const QFontMetricsF metrics(sceneTreeGraphicsFont());
            const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(best->groupId);
            for (int axis = 0; axis < 3; ++axis) {
                const QRectF rowRect = transformParameterControlRect(best->rect, axis, TransformHeaderWidth);
                if (!rowRect.contains(scenePos))
                    continue;
                const QRectF labelRect(rowRect.left(), rowRect.top(),
                                       TransformParamLabelArea - 2.0, rowRect.height());
                if (labelRect.contains(scenePos)) {
                    ColorZoneHit h{QStringLiteral("numLabelText"), QStringLiteral("Axis label"),
                                    best->rect, true, op, true};
                    h.nodeId = best->groupId;
                    return h;
                }
                if (!node) continue;
                const QString expr = transformAxisExpression(*node, axis);
                for (const ExpressionNumberControl &nc :
                         transformParameterNumberControls(best->rect, axis, expr, metrics)) {
                    if (nc.rect.contains(scenePos)) {
                        ColorZoneHit h{QStringLiteral("numText"), QStringLiteral("Number constant"),
                                        best->rect, true, op, true};
                        h.nodeId = best->groupId;
                        return h;
                    }
                }
                const QRectF textRect(rowRect.left() + TransformParamLabelArea, rowRect.top(),
                                      rowRect.width() - TransformParamLabelArea, rowRect.height());
                for (const ExpressionTextSpan &span : expressionSpansInTextRect(textRect, expr, metrics)) {
                    if (!span.number && span.rect.contains(scenePos)) {
                        ColorZoneHit h{QStringLiteral("mutedText"), QStringLiteral("Formula"),
                                        best->rect, true, op, true};
                        h.nodeId = best->groupId;
                        return h;
                    }
                }
            }
        }

        ColorZoneHit cardHit{QStringLiteral("card"), QStringLiteral("Card body"),
                              best->rect, true, op, true};
        cardHit.nodeId = best->groupId;
        return cardHit;
    }

    return {QStringLiteral("canvas"), QStringLiteral("Canvas background"), QRectF(), false,
            SceneDocument::TreeNode::Union, false};
}

void SceneTreeColorEditMode::updateHighlight(const QPointF &scenePos)
{
    clearHighlight();
    if (!m_enabled || !m_widget->m_graphicsScene)
        return;

    const ColorZoneHit hit = zoneAt(scenePos);

    m_zoneField = hit.fieldName;

    const bool sameZone = (hit.fieldName   == m_currentZone.fieldName   &&
                            hit.hasOperation == m_currentZone.hasOperation &&
                            hit.operation    == m_currentZone.operation);
    if (!sameZone) {
        m_propIndex   = 0;
        m_currentZone = hit;
    }

    const QVector<ColorPropDef> props = propsForHit(hit);
    const int idx   = props.isEmpty() ? 0 : qBound(0, m_propIndex, props.size() - 1);
    const bool hasProp = !props.isEmpty();
    const ColorPropDef prop = hasProp ? props.at(idx) : ColorPropDef{};

    if (hasProp && hit.valid && !hit.rect.isNull()) {
        const QColor cur = getColorForProp(prop, hit);
        const QColor flash(255 - cur.red(), 255 - cur.green(), 255 - cur.blue(),
                           prop.isText ? cur.alpha() : 218);

        if (hit.fieldName == QLatin1String("hint")
            || hit.fieldName == QLatin1String("toolbar"))
        {
            QPainterPath flashPath;
            flashPath.addRoundedRect(hit.rect, 6.0, 6.0);
            QPen borderPen(flash, 2.5);
            borderPen.setCosmetic(true);
            auto *blink = m_widget->m_graphicsScene->addPath(flashPath, borderPen,
                                                   QBrush(QColor(flash.red(), flash.green(),
                                                                  flash.blue(), 55)));
            blink->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
            blink->setPos(hit.itemScenePos);
            blink->setZValue(12000.0);
            blink->setVisible(m_blinkOn);
            m_blinkTargets.append(blink);
        } else if (prop.isText
            && prop.id == QLatin1String("text")
            && (hit.fieldName == QLatin1String("header") || hit.fieldName == QLatin1String("card"))
            && !isVerticalHeaderOperation(hit.operation))
        {
            const QFont font = sceneTreeGraphicsFont();
            const QFontMetricsF metrics(font);
            const qreal iconSize = (hit.operation == SceneDocument::TreeNode::Module)
                ? PrimitiveIconSize - 6.0 : PrimitiveIconSize - 4.0;
            const qreal labelLeft = hit.rect.left() + 30.0 + iconSize + 10.0;
            const qreal headerTop = hit.rect.top();
            const qreal textTop = headerTop + 7.0 + (16.0 - metrics.height()) * 0.5;

            QString headerLabel = labelForOperation(hit.operation);
            if (hit.operation == SceneDocument::TreeNode::Module && m_widget->m_scene && hit.nodeId) {
                const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(hit.nodeId);
                if (node && !node->moduleName.trimmed().isEmpty())
                    headerLabel += QStringLiteral(" ") + node->moduleName.trimmed();
            }
            auto *overlay = new QGraphicsSimpleTextItem(headerLabel);
            overlay->setFont(font);
            overlay->setPos(QPointF(labelLeft, textTop));
            overlay->setBrush(QBrush(flash));
            overlay->setZValue(8801.0);
            m_widget->m_graphicsScene->addItem(overlay);
            overlay->setVisible(m_blinkOn);
            m_blinkTargets.append(overlay);
        } else if ((prop.id == QLatin1String("numLabelText")
                    || prop.id == QLatin1String("numText")
                    || prop.id == QLatin1String("mutedText")
                    || prop.id == QLatin1String("numFill")
                    || prop.id == QLatin1String("numBorder"))
                   && hit.hasOperation && isVerticalHeaderOperation(hit.operation)
                   && hit.rect.height() > 3.0 * VariableHeight)
        {
            const QFont font = sceneTreeGraphicsFont();
            QFont valueFont = font;
            valueFont.setPointSizeF(qMax<qreal>(7.0, font.pointSizeF() - 2.0));
            const QFontMetricsF metrics(font);
            const QFontMetricsF vfm(valueFont);
            const qreal r = 3.0 / qMax(0.001, qAbs(m_widget->transform().m11()));
            static const QString xyzLabels[3] = {
                QStringLiteral("X ="), QStringLiteral("Y ="), QStringLiteral("Z =")};
            static const QString rgbLabels[3] = {
                QStringLiteral("R ="), QStringLiteral("G ="), QStringLiteral("B =")};
            const QString *rowLabels = (hit.operation == SceneDocument::TreeNode::Color)
                                       ? rgbLabels : xyzLabels;
            const SceneDocument::TreeNode *groupNode = (hit.nodeId && m_widget->m_scene)
                ? m_widget->m_scene->treeNodeById(hit.nodeId) : nullptr;
            for (int axis = 0; axis < 3; ++axis) {
                const QRectF rowRect = transformParameterControlRect(hit.rect, axis, TransformHeaderWidth);
                const QRectF labelRect(rowRect.left(), rowRect.top(),
                                       TransformParamLabelArea - 2.0, rowRect.height());
                if (prop.id == QLatin1String("numLabelText")) {
                    auto *lbl = new QGraphicsSimpleTextItem(rowLabels[axis]);
                    lbl->setFont(valueFont);
                    const qreal ty = labelRect.top() + (labelRect.height() - vfm.height()) * 0.5;
                    lbl->setPos(labelRect.left(), ty);
                    lbl->setBrush(QBrush(flash));
                    lbl->setZValue(8801.0);
                    m_widget->m_graphicsScene->addItem(lbl);
                    lbl->setVisible(m_blinkOn);
                    m_blinkTargets.append(lbl);
                } else {
                    const QString expr = groupNode ? transformAxisExpression(*groupNode, axis) : QString();
                    const QRectF textRect(rowRect.left() + TransformParamLabelArea, rowRect.top(),
                                          rowRect.width() - TransformParamLabelArea, rowRect.height());
                    const bool wantNumber = (prop.id != QLatin1String("mutedText"));
                    for (const ExpressionTextSpan &span :
                             expressionSpansInTextRect(textRect, expr, metrics)) {
                        if (span.number != wantNumber) continue;
                        if (prop.id == QLatin1String("numFill")) {
                            QPainterPath path;
                            path.addRoundedRect(span.rect, r, r);
                            auto *it = m_widget->m_graphicsScene->addPath(path, Qt::NoPen, QBrush(flash));
                            it->setZValue(8800.0);
                            it->setVisible(m_blinkOn);
                            m_blinkTargets.append(it);
                        } else if (prop.id == QLatin1String("numBorder")) {
                            QPainterPath path;
                            path.addRoundedRect(span.rect, r, r);
                            QPen borderPen(flash, 2.0);
                            borderPen.setCosmetic(true);
                            auto *it = m_widget->m_graphicsScene->addPath(path, borderPen, Qt::NoBrush);
                            it->setZValue(8800.0);
                            it->setVisible(m_blinkOn);
                            m_blinkTargets.append(it);
                        } else {
                            auto *lbl = new QGraphicsSimpleTextItem(span.text);
                            lbl->setFont(valueFont);
                            const qreal tx = span.number
                                ? span.rect.left() + (span.rect.width() - vfm.horizontalAdvance(span.text)) * 0.5
                                : span.rect.left();
                            const qreal ty = span.rect.top() + (span.rect.height() - vfm.height()) * 0.5;
                            lbl->setPos(tx, ty);
                            lbl->setBrush(QBrush(flash));
                            lbl->setZValue(8801.0);
                            m_widget->m_graphicsScene->addItem(lbl);
                            lbl->setVisible(m_blinkOn);
                            m_blinkTargets.append(lbl);
                        }
                    }
                }
            }
        } else if ((prop.id == QLatin1String("mutedText") || prop.id == QLatin1String("numText"))
                   && !hit.spanText.isEmpty())
        {
            const QFont font = sceneTreeGraphicsFont();
            QFont valueFont = font;
            valueFont.setPointSizeF(qMax<qreal>(7.0, font.pointSizeF() - 2.0));
            const QFontMetricsF vfm(valueFont);
            auto *lbl = new QGraphicsSimpleTextItem(hit.spanText);
            lbl->setFont(valueFont);
            const qreal tx = (prop.id == QLatin1String("numText"))
                ? hit.rect.left() + (hit.rect.width() - vfm.horizontalAdvance(hit.spanText)) * 0.5
                : hit.rect.left();
            const qreal ty = hit.rect.top() + (hit.rect.height() - vfm.height()) * 0.5;
            lbl->setPos(tx, ty);
            lbl->setBrush(QBrush(flash));
            lbl->setZValue(8801.0);
            m_widget->m_graphicsScene->addItem(lbl);
            lbl->setVisible(m_blinkOn);
            m_blinkTargets.append(lbl);
        } else if (hit.fieldName == QLatin1String("card") && !hit.hasOperation
                   && hit.nodeId && m_widget->m_scene
                   && (prop.id == QLatin1String("numLabelText")
                       || prop.id == QLatin1String("numText")
                       || prop.id == QLatin1String("mutedText")
                       || prop.id == QLatin1String("numFill")
                       || prop.id == QLatin1String("numBorder")))
        {
            const QFont font = sceneTreeGraphicsFont();
            QFont valueFont = font;
            valueFont.setPointSizeF(qMax<qreal>(7.0, font.pointSizeF() - 2.0));
            const QFontMetricsF metrics(font);
            const QFontMetricsF valueMetrics(valueFont);
            const QFontMetricsF vfm(valueFont);
            const qreal r = 3.0 / qMax(0.001, qAbs(m_widget->transform().m11()));
            const bool wantNumber = (prop.id != QLatin1String("mutedText"));

            const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(hit.nodeId);

            auto addSpanOverlayAt = [&](const ExpressionTextSpan &span, const QRectF &spanRect) {
                if (span.number != wantNumber) return;
                if (prop.id == QLatin1String("numFill")) {
                    QPainterPath path;
                    path.addRoundedRect(spanRect, r, r);
                    auto *it = m_widget->m_graphicsScene->addPath(path, Qt::NoPen, QBrush(flash));
                    it->setZValue(8800.0);
                    it->setVisible(m_blinkOn);
                    m_blinkTargets.append(it);
                } else if (prop.id == QLatin1String("numBorder")) {
                    QPainterPath path;
                    path.addRoundedRect(spanRect, r, r);
                    QPen borderPen(flash, 2.0);
                    borderPen.setCosmetic(true);
                    auto *it = m_widget->m_graphicsScene->addPath(path, borderPen, Qt::NoBrush);
                    it->setZValue(8800.0);
                    it->setVisible(m_blinkOn);
                    m_blinkTargets.append(it);
                } else {
                    auto *lbl = new QGraphicsSimpleTextItem(span.text);
                    lbl->setFont(valueFont);
                    const qreal tx = span.number
                        ? spanRect.left() + (spanRect.width() - vfm.horizontalAdvance(span.text)) * 0.5
                        : spanRect.left();
                    lbl->setPos(tx, spanRect.top() + (spanRect.height() - vfm.height()) * 0.5);
                    lbl->setBrush(QBrush(flash));
                    lbl->setZValue(8801.0);
                    m_widget->m_graphicsScene->addItem(lbl);
                    lbl->setVisible(m_blinkOn);
                    m_blinkTargets.append(lbl);
                }
            };
            auto addSpanOverlay = [&](const ExpressionTextSpan &span) {
                addSpanOverlayAt(span, span.rect);
            };

            if (node && node->type == SceneDocument::TreeNode::Primitive) {
                const ShapeNode *shape = m_widget->m_scene->shapeById(node->shapeId);
                if (shape) {
                    const QVector<ShapeParameterControl> controls = shapeParameterControls(*shape);
                    const int count = controls.size();
                    for (int i = 0; i < count; ++i) {
                        const QRectF rowRect = shapeParameterControlRect(hit.rect, i, count);
                        if (prop.id == QLatin1String("numLabelText")) {
                            const QRectF labelRect(rowRect.left(), rowRect.top(),
                                                   PrimitiveParamLabelArea - 2.0, rowRect.height());
                            auto *lbl = new QGraphicsSimpleTextItem(
                                controls[i].label + QStringLiteral(" ="));
                            lbl->setFont(valueFont);
                            lbl->setPos(labelRect.left(),
                                        labelRect.top() + (labelRect.height() - vfm.height()) * 0.5);
                            lbl->setBrush(QBrush(flash));
                            lbl->setZValue(8801.0);
                            m_widget->m_graphicsScene->addItem(lbl);
                            lbl->setVisible(m_blinkOn);
                            m_blinkTargets.append(lbl);
                        } else {
                            const QRectF textRect = shapeUsesExpressionPillLayout(*shape)
                                ? shapeParameterFieldRect(rowRect, *shape)
                                : QRectF(rowRect.left() + PrimitiveParamLabelArea,
                                         rowRect.top(),
                                         rowRect.width() - PrimitiveParamLabelArea,
                                         rowRect.height());
                            const ExpressionPillLayout pillLayout(textRect, controls[i].expression, valueMetrics);
                            const QVector<ExpressionTextSpan> spans = shapeUsesExpressionPillLayout(*shape)
                                ? pillLayout.spans()
                                : expressionSpansInTextRect(textRect, controls[i].expression, valueMetrics);
                            for (const ExpressionTextSpan &span : spans) {
                                const QRectF spanRect = shapeUsesExpressionPillLayout(*shape)
                                    ? pillLayout.visualRectFor(span)
                                    : span.rect;
                                addSpanOverlayAt(span, spanRect);
                            }
                        }
                    }
                }
            } else if (node && node->type == SceneDocument::TreeNode::ModuleCall) {
                const SceneDocument::TreeNode *modGroup = m_widget->m_scene->treeNodeById(node->shapeId);
                if (modGroup && modGroup->operation == SceneDocument::TreeNode::Module) {
                    QVector<ModuleCallParam> params;
                    const QHash<QString, QString> overrides =
                        resolveModuleArguments(node->moduleCallArguments, *modGroup);
                    for (const SceneDocument::TreeNode &pChild : modGroup->children) {
                        if (pChild.type == SceneDocument::TreeNode::Variable && pChild.isParameter) {
                            const QString expr = overrides.value(
                                pChild.variableName,
                                pChild.variableExpression.trimmed().isEmpty()
                                    ? QString::number(pChild.variableValue)
                                    : pChild.variableExpression.trimmed());
                            params.append({pChild.id, pChild.variableName, expr});
                        }
                    }
                    qreal x = hit.rect.left() + 46.0
                              + metrics.horizontalAdvance(node->moduleName + QStringLiteral("("));
                    for (int i = 0; i < params.size(); ++i) {
                        const QString nameLabel = params[i].name + QStringLiteral(" =");
                        const qreal nameLabelW  = metrics.horizontalAdvance(nameLabel + QStringLiteral(" "));
                        if (prop.id == QLatin1String("numLabelText")) {
                            auto *lbl = new QGraphicsSimpleTextItem(nameLabel);
                            lbl->setFont(valueFont);
                            lbl->setPos(x, hit.rect.top() + (VariableHeight - vfm.height()) * 0.5);
                            lbl->setBrush(QBrush(flash));
                            lbl->setZValue(8801.0);
                            m_widget->m_graphicsScene->addItem(lbl);
                            lbl->setVisible(m_blinkOn);
                            m_blinkTargets.append(lbl);
                        } else {
                            const QRectF exprRect(x + nameLabelW, hit.rect.top(),
                                                  hit.rect.right() - (x + nameLabelW), VariableHeight);
                            for (const ExpressionTextSpan &span :
                                     expressionSpansInTextRect(exprRect, params[i].expression, metrics))
                                addSpanOverlay(span);
                        }
                        x += nameLabelW;
                        x += metrics.horizontalAdvance(params[i].expression);
                        if (i < params.size() - 1)
                            x += metrics.horizontalAdvance(QStringLiteral(", "));
                    }
                }
            } else if (hit.nodeId < 0) {
                const SceneDocument::TreeNode *modNode = m_widget->m_scene->treeNodeById(-hit.nodeId);
                if (modNode && modNode->operation == SceneDocument::TreeNode::Module) {
                    QVector<ModuleCallParam> tmplParams;
                    for (const SceneDocument::TreeNode &pc : modNode->children) {
                        if (pc.type == SceneDocument::TreeNode::Variable && pc.isParameter) {
                            const QString expr = pc.variableExpression.trimmed().isEmpty()
                                ? QString::number(pc.variableValue)
                                : pc.variableExpression.trimmed();
                            tmplParams.append({pc.id, pc.variableName, expr});
                        }
                    }
                    qreal x = hit.rect.left() + 46.0
                              + metrics.horizontalAdvance(modNode->moduleName + QStringLiteral("("));
                    for (int i = 0; i < tmplParams.size(); ++i) {
                        const QString nameLabel = tmplParams[i].name + QStringLiteral(" =");
                        const qreal nameLabelW  = metrics.horizontalAdvance(nameLabel + QStringLiteral(" "));
                        if (prop.id == QLatin1String("numLabelText")) {
                            auto *lbl = new QGraphicsSimpleTextItem(nameLabel);
                            lbl->setFont(valueFont);
                            lbl->setPos(x, hit.rect.top() + (VariableHeight - vfm.height()) * 0.5);
                            lbl->setBrush(QBrush(flash));
                            lbl->setZValue(8801.0);
                            m_widget->m_graphicsScene->addItem(lbl);
                            lbl->setVisible(m_blinkOn);
                            m_blinkTargets.append(lbl);
                        } else {
                            const QRectF exprRect(x + nameLabelW, hit.rect.top(),
                                                  hit.rect.right() - (x + nameLabelW), VariableHeight);
                            for (const ExpressionTextSpan &span :
                                     expressionSpansInTextRect(exprRect, tmplParams[i].expression, metrics))
                                addSpanOverlay(span);
                        }
                        x += nameLabelW;
                        x += metrics.horizontalAdvance(tmplParams[i].expression);
                        if (i < tmplParams.size() - 1)
                            x += metrics.horizontalAdvance(QStringLiteral(", "));
                    }
                }
            }
        } else if (hit.fieldName == QLatin1String("input")
                   && hit.nodeId && m_widget->m_scene
                   && (prop.id == QLatin1String("numLabelText")
                       || prop.id == QLatin1String("numText")
                       || prop.id == QLatin1String("mutedText")
                       || prop.id == QLatin1String("numFill")
                       || prop.id == QLatin1String("numBorder")))
        {
            const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(hit.nodeId);
            if (node && node->type == SceneDocument::TreeNode::Variable) {
                const QFont font = sceneTreeGraphicsFont();
                const QFontMetricsF metrics(font);
                const qreal r    = 3.0 / qMax(0.001, qAbs(m_widget->transform().m11()));
                const qreal nameW = metrics.horizontalAdvance(node->variableName);

                if (prop.id == QLatin1String("numLabelText")) {
                    const qreal eqLeft = hit.rect.left() + 38.0 + nameW + 4.0;
                    const QRectF eqRect(eqLeft,
                                        hit.rect.top() + (VariableHeight - 16.0) * 0.5,
                                        12.0, 16.0);
                    auto *lbl = new QGraphicsSimpleTextItem(QStringLiteral("="));
                    lbl->setFont(font);
                    lbl->setPos(eqRect.left(),
                                eqRect.top() + (eqRect.height() - metrics.height()) * 0.5);
                    lbl->setBrush(QBrush(flash));
                    lbl->setZValue(8801.0);
                    m_widget->m_graphicsScene->addItem(lbl);
                    lbl->setVisible(m_blinkOn);
                    m_blinkTargets.append(lbl);
                } else {
                    const bool wantNumber = (prop.id != QLatin1String("mutedText"));
                    for (const ExpressionTextSpan &span :
                             expressionTextSpans(hit.rect, node->variableExpression, metrics, nameW)) {
                        if (span.number != wantNumber) continue;
                        if (prop.id == QLatin1String("numFill")) {
                            QPainterPath path;
                            path.addRoundedRect(span.rect, r, r);
                            auto *it = m_widget->m_graphicsScene->addPath(path, Qt::NoPen, QBrush(flash));
                            it->setZValue(8800.0);
                            it->setVisible(m_blinkOn);
                            m_blinkTargets.append(it);
                        } else if (prop.id == QLatin1String("numBorder")) {
                            QPainterPath path;
                            path.addRoundedRect(span.rect, r, r);
                            QPen borderPen(flash, 2.0);
                            borderPen.setCosmetic(true);
                            auto *it = m_widget->m_graphicsScene->addPath(path, borderPen, Qt::NoBrush);
                            it->setZValue(8800.0);
                            it->setVisible(m_blinkOn);
                            m_blinkTargets.append(it);
                        } else {
                            auto *lbl = new QGraphicsSimpleTextItem(span.text);
                            lbl->setFont(font);
                            const qreal tx = span.number
                                ? span.rect.left() + (span.rect.width() - metrics.horizontalAdvance(span.text)) * 0.5
                                : span.rect.left();
                            lbl->setPos(tx,
                                        span.rect.top() + (span.rect.height() - metrics.height()) * 0.5);
                            lbl->setBrush(QBrush(flash));
                            lbl->setZValue(8801.0);
                            m_widget->m_graphicsScene->addItem(lbl);
                            lbl->setVisible(m_blinkOn);
                            m_blinkTargets.append(lbl);
                        }
                    }
                }
            }
        } else if (prop.id == QLatin1String("mutedText")
                   && hit.fieldName == QLatin1String("card")
                   && hit.hasOperation
                   && hit.operation == SceneDocument::TreeNode::Module
                   && hit.nodeId && m_widget->m_scene)
        {
            const SceneTreeLayout::GroupHitArea *area = nullptr;
            for (const SceneTreeLayout::GroupHitArea &a : m_widget->m_treeLayout.groupHitAreas())
                if (a.groupId == hit.nodeId) { area = &a; break; }

            if (area && area->moduleParameterSeparatorY > hit.rect.top()) {
                QVector<QGraphicsItem *> items;
                m_widget->drawModuleSectionLabels(hit.rect, area->moduleParameterSeparatorY,
                                        area->depth, flash, &items);
                for (QGraphicsItem *it : items) {
                    it->setVisible(m_blinkOn);
                    m_blinkTargets.append(it);
                }
            }
        } else if (prop.id == QLatin1String("mutedText")
                   && hit.fieldName == QLatin1String("card")
                   && hit.hasOperation
                   && hit.operation == SceneDocument::TreeNode::Difference
                   && hit.nodeId && m_widget->m_scene)
        {
            const SceneTreeLayout::GroupHitArea *area = nullptr;
            for (const SceneTreeLayout::GroupHitArea &a : m_widget->m_treeLayout.groupHitAreas())
                if (a.groupId == hit.nodeId) { area = &a; break; }

            if (area && area->cutSeparatorY > hit.rect.top()) {
                QPainterPath sepPath;
                sepPath.moveTo(hit.rect.left() + GroupPadding, area->cutSeparatorY);
                sepPath.lineTo(hit.rect.right() - GroupPadding, area->cutSeparatorY);
                QPen dashPen(flash, 2.0, Qt::DashLine);
                dashPen.setCosmetic(true);
                auto *sepLine = m_widget->m_graphicsScene->addPath(sepPath, dashPen, Qt::NoBrush);
                sepLine->setZValue(8801.0);
                sepLine->setVisible(m_blinkOn);
                m_blinkTargets.append(sepLine);

                const qreal labelLeft  = hit.rect.left() + GroupPadding * 0.5;
                const qreal baseTop    = hit.rect.top() + GroupHeaderHeight + GroupPadding;
                const qreal baseBottom = area->cutSeparatorY - 4.0;
                const qreal baseH      = qMin(42.0, baseBottom - baseTop);
                if (baseH >= 18.0) {
                    const qreal baseY = baseTop + (baseBottom - baseTop - baseH) * 0.5;
                    QPainterPath basePath;
                    basePath.addRoundedRect(QRectF(labelLeft, baseY, 20.0, baseH), 4.0, 4.0);
                    auto *baseItem = m_widget->m_graphicsScene->addPath(basePath, Qt::NoPen, QBrush(flash));
                    baseItem->setZValue(8801.0);
                    baseItem->setVisible(m_blinkOn);
                    m_blinkTargets.append(baseItem);
                }

                const qreal cutTop    = area->cutSeparatorY + 4.0;
                const qreal cutBottom = hit.rect.bottom() - GroupPadding;
                const qreal cutH      = qMin(42.0, cutBottom - cutTop);
                if (cutH >= 18.0) {
                    const qreal cutY = cutTop + (cutBottom - cutTop - cutH) * 0.5;
                    QPainterPath cutPath;
                    cutPath.addRoundedRect(QRectF(labelLeft, cutY, 20.0, cutH), 4.0, 4.0);
                    auto *cutItem = m_widget->m_graphicsScene->addPath(cutPath, Qt::NoPen, QBrush(flash));
                    cutItem->setZValue(8801.0);
                    cutItem->setVisible(m_blinkOn);
                    m_blinkTargets.append(cutItem);
                }
            }
        } else {
            const qreal r = 3.0 / qMax(0.001, qAbs(m_widget->transform().m11()));
            QPainterPath flashPath;
            QGraphicsPathItem *overlay = nullptr;

            if (prop.id == QLatin1String("border") || prop.id == QLatin1String("numBorder")) {
                flashPath.addRoundedRect(hit.rect, r, r);
                QPen borderPen(flash, 3.0);
                borderPen.setCosmetic(true);
                overlay = m_widget->m_graphicsScene->addPath(flashPath, borderPen, Qt::NoBrush);
            } else {
                flashPath.addRoundedRect(hit.rect, r, r);
                overlay = m_widget->m_graphicsScene->addPath(flashPath, Qt::NoPen, QBrush(flash));
            }

            overlay->setZValue(8800.0);
            overlay->setVisible(m_blinkOn);
            m_highlight = overlay;
        }
    }

    if (hasProp && hit.fieldName == QLatin1String("canvas")) {
        const QColor cur = getColorForProp(prop, hit);
        if (cur.isValid()) {
            const QColor inv(255 - cur.red(), 255 - cur.green(), 255 - cur.blue(), 200);
            if (prop.id == QLatin1String("minorGrid") || prop.id == QLatin1String("majorGrid")) {
                const qreal gridSize = (prop.id == QLatin1String("minorGrid")) ? 24.0 : 120.0;
                const QRectF vis = m_widget->mapToScene(m_widget->viewport()->rect()).boundingRect();
                auto *gi = new GridBlinkItem(vis, gridSize, inv);
                gi->setZValue(8800.0);
                m_widget->m_graphicsScene->addItem(gi);
                gi->setVisible(m_blinkOn);
                m_blinkTargets.append(gi);
            } else {
                const qreal w = m_widget->viewport() ? m_widget->viewport()->width()  : 640.0;
                const qreal h = m_widget->viewport() ? m_widget->viewport()->height() : 480.0;
                constexpr qreal kInset = 4.0;
                auto *frame = m_widget->m_graphicsScene->addRect(
                    QRectF(kInset, kInset, w - kInset * 2, h - kInset * 2),
                    QPen(inv, 6),
                    Qt::NoBrush);
                frame->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
                frame->setPos(m_widget->mapToScene(QPoint(0, 0)));
                frame->setZValue(8800.0);
                frame->setVisible(m_blinkOn);
                m_blinkTargets.append(frame);
            }
        }
    }

    if (hasProp) {
        const QColor cur = getColorForProp(prop, hit);
        const QString hexStr = cur.isValid()
            ? (cur.alpha() < 255
                ? QStringLiteral("#") + QString::number(cur.rgba(), 16).rightJustified(8, QLatin1Char('0')).toUpper()
                : cur.name().toUpper())
            : QStringLiteral("—");

        QString zoneLine;
        if (hit.hasOperation)
            zoneLine = colorEditOpName(hit.operation) + QStringLiteral("  ") + hit.label;
        else if (hit.fieldName == QLatin1String("input")
                 || hit.fieldName == QLatin1String("card")
                 || hit.fieldName == QLatin1String("hint")
                 || hit.fieldName == QLatin1String("toolbar"))
            zoneLine = hit.label;
        else
            zoneLine = QStringLiteral("Canvas");

        const QString navLine = props.size() > 1
            ? QStringLiteral("↕ ") + QString::number(idx + 1) + QStringLiteral("/")
              + QString::number(props.size()) + QStringLiteral("  ·  click to pick  ·  Esc exit")
            : QStringLiteral("click to pick  ·  Esc exit");

        m_widget->m_hoverManager->updateHoverHint(QStringLiteral("colorEdit:%1").arg(hit.fieldName),
                        QStringLiteral("✏ ") + zoneLine
                        + QStringLiteral("\n") + prop.label + QStringLiteral("   ") + hexStr
                        + QStringLiteral("\n") + navLine);
    } else {
        m_widget->m_hoverManager->updateHoverHint(QStringLiteral("colorEdit:none"),
                        QStringLiteral("✏ Color edit mode\n"
                                       "Hover a card to inspect its colors.\n"
                                       "Esc — exit color edit mode"));
    }
}

void SceneTreeColorEditMode::clearHighlight()
{
    for (QGraphicsItem *it : m_blinkTargets) {
        if (it && m_widget->m_graphicsScene) {
            m_widget->m_graphicsScene->removeItem(it);
            delete it;
        }
    }
    m_blinkTargets.clear();

    if (m_highlight && m_widget->m_graphicsScene) {
        m_widget->m_graphicsScene->removeItem(m_highlight);
        delete m_highlight;
        m_highlight = nullptr;
    }
}

void SceneTreeColorEditMode::handleClick(const QPointF &scenePos)
{
    const ColorZoneHit hit = zoneAt(scenePos);
    const QVector<ColorPropDef> props = propsForHit(hit);
    if (props.isEmpty())
        return;

    const int idx = qBound(0, m_propIndex, props.size() - 1);
    const ColorPropDef &prop = props.at(idx);
    const QColor current = getColorForProp(prop, hit);

    const QString title = hit.hasOperation
        ? colorEditOpName(hit.operation) + QStringLiteral(" — ") + prop.label
        : prop.label;

    const QColor chosen = QColorDialog::getColor(
        current.isValid() ? current : Qt::white,
        m_widget, title, QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid())
        return;

    setColorForProp(prop, hit, chosen);
    m_widget->refresh();
    m_widget->updateToolbarOverlay();
    updateHighlight(scenePos);
    emit inlineThemeEdited();
}

void SceneTreeColorEditMode::handleWheel(const QPointF &scenePos, int angleDelta)
{
    const ColorZoneHit hit = zoneAt(scenePos);
    const QVector<ColorPropDef> props = propsForHit(hit);
    if (props.isEmpty())
        return;

    const bool sameZone = (hit.fieldName   == m_currentZone.fieldName   &&
                            hit.hasOperation == m_currentZone.hasOperation &&
                            hit.operation    == m_currentZone.operation);
    if (!sameZone) {
        m_propIndex   = 0;
        m_currentZone = hit;
    }

    const int dir = angleDelta > 0 ? -1 : 1;
    m_propIndex = (m_propIndex + dir + props.size()) % props.size();
    m_currentZone = hit;

    updateHighlight(scenePos);
    m_widget->updateToolbarOverlay();
}

QVector<SceneTreeColorEditMode::ColorPropDef>
SceneTreeColorEditMode::propsForHit(const ColorZoneHit &hit) const
{
    if (hit.fieldName == QLatin1String("numLabelText"))
        return {{QStringLiteral("numLabelText"), QStringLiteral("Label text color (X=, Y=, Z=)"), true, true}};
    if (hit.fieldName == QLatin1String("numText"))
        return {
            {QStringLiteral("numText"),   QStringLiteral("Number text color"),  true,  true},
            {QStringLiteral("numFill"),   QStringLiteral("Number pill fill"),   false, true},
            {QStringLiteral("numBorder"), QStringLiteral("Number pill border"), false, true},
        };
    if (hit.fieldName == QLatin1String("mutedText"))
        return {{QStringLiteral("mutedText"), QStringLiteral("Expression color"), true, true}};

    if (hit.fieldName == QLatin1String("hint"))
        return {
            {QStringLiteral("glassTop"),    QStringLiteral("Hint glass (top)"),    false, true},
            {QStringLiteral("glassBottom"), QStringLiteral("Hint glass (bottom)"), false, true},
            {QStringLiteral("glassBorder"), QStringLiteral("Hint border"),         false, true},
            {QStringLiteral("text"),        QStringLiteral("Hint text color"),     true,  true},
        };
    if (hit.fieldName == QLatin1String("toolbar"))
        return {
            {QStringLiteral("glassBottom"), QStringLiteral("Toolbar glass fill"),   false, true},
            {QStringLiteral("glassBorder"), QStringLiteral("Toolbar glass border"), false, true},
        };

    if (hit.fieldName == QLatin1String("header")) {
        if (hit.hasOperation && isVerticalHeaderOperation(hit.operation))
            return {{QStringLiteral("header"), QStringLiteral("Header fill"), false, false}};
        return {
            {QStringLiteral("header"), QStringLiteral("Header fill"),     false, false},
            {QStringLiteral("text"),   QStringLiteral("Card text color"), true,  false},
        };
    }
    if (hit.fieldName == QLatin1String("card")) {
        if (!hit.hasOperation) {
            return {
                {QStringLiteral("leafCard"),     QStringLiteral("Card fill"),            false, true},
                {QStringLiteral("numLabelText"), QStringLiteral("Param label color"),    true,  true},
                {QStringLiteral("numText"),      QStringLiteral("Number constant color"),true,  true},
                {QStringLiteral("mutedText"),    QStringLiteral("Expression color"),     true,  true},
                {QStringLiteral("numFill"),      QStringLiteral("Number pill fill"),     false, true},
                {QStringLiteral("numBorder"),    QStringLiteral("Number pill border"),   false, true},
            };
        }
        QVector<ColorPropDef> props = {
            {QStringLiteral("card"),   QStringLiteral("Card fill"),   false, false},
            {QStringLiteral("border"), QStringLiteral("Card border"), false, false},
        };
        using Op = SceneDocument::TreeNode;
        if (isVerticalHeaderOperation(hit.operation) || hit.operation == Op::For) {
            props << ColorPropDef{QStringLiteral("numLabelText"), QStringLiteral("Label text (X=, Y=)"), true,  true};
            props << ColorPropDef{QStringLiteral("numText"),     QStringLiteral("Constant values"),      true,  true};
            props << ColorPropDef{QStringLiteral("mutedText"),   QStringLiteral("Expression color"),     true,  true};
        } else if (hit.operation != Op::Scene) {
            props << ColorPropDef{QStringLiteral("text"),      QStringLiteral("Header text color"),  true, false};
            props << ColorPropDef{QStringLiteral("mutedText"), QStringLiteral("Label / separator color"), true, true};
        }
        return props;
    }
    if (hit.fieldName == QLatin1String("input"))
        return {
            {QStringLiteral("input"),        QStringLiteral("Row fill"),           false, true},
            {QStringLiteral("numLabelText"), QStringLiteral("Label color (=)"),    true,  true},
            {QStringLiteral("numText"),      QStringLiteral("Number constant"),    true,  true},
            {QStringLiteral("mutedText"),    QStringLiteral("Expression color"),   true,  true},
            {QStringLiteral("numFill"),      QStringLiteral("Number pill fill"),   false, true},
            {QStringLiteral("numBorder"),    QStringLiteral("Number pill border"), false, true},
        };
    return {
        {QStringLiteral("canvas"),    QStringLiteral("Canvas background"), false, true},
        {QStringLiteral("minorGrid"), QStringLiteral("Minor grid"),        false, true},
        {QStringLiteral("majorGrid"), QStringLiteral("Major grid"),        false, true},
    };
}

QColor SceneTreeColorEditMode::getColorForProp(const ColorPropDef &prop,
                                                 const ColorZoneHit &hit) const
{
    const auto pt = static_cast<SceneTreePalette::Theme>(m_widget->m_treeTheme);
    if (!prop.isGlobal && hit.hasOperation) {
        const auto op = hit.operation;
        const OperationCardPalette pal = SceneTreePalette::operationCardPalette(op);
        const QColor cardFill = pal.card.isValid()
            ? pal.card : SceneTreePalette::groupFill(op, 0, pt);
        if (prop.id == QLatin1String("header"))
            return pal.header.isValid() ? pal.header : SceneTreePalette::groupHeaderColor(op, cardFill, pt);
        if (prop.id == QLatin1String("text"))
            return pal.text.isValid() ? pal.text : SceneTreePalette::cardTextPrimary(op, pt);
        if (prop.id == QLatin1String("card"))
            return cardFill;
        if (prop.id == QLatin1String("border"))
            return pal.border.isValid() ? pal.border : SceneTreePalette::cardBorder(op, cardFill, pt);
        if (prop.id == QLatin1String("numBorderActive"))
            return pal.numBorderActive.isValid() ? pal.numBorderActive : SceneTreePalette::cardPillBorderActive(op);
        if (prop.id == QLatin1String("numFillActive"))
            return pal.numFillActive.isValid() ? pal.numFillActive : SceneTreePalette::cardPillFillActive(op);
    }
    const TreeAppearanceTheme t = SceneTreePalette::customTheme();
    if (prop.id == QLatin1String("canvas"))          return t.canvas;
    if (prop.id == QLatin1String("minorGrid"))        return t.minorGrid;
    if (prop.id == QLatin1String("majorGrid"))        return t.majorGrid;
    if (prop.id == QLatin1String("text"))             return t.text;
    if (prop.id == QLatin1String("mutedText"))        return t.mutedText;
    if (prop.id == QLatin1String("accent"))           return t.accent;
    if (prop.id == QLatin1String("glassTop"))         return t.glassTop;
    if (prop.id == QLatin1String("glassBottom"))      return t.glassBottom;
    if (prop.id == QLatin1String("glassBorder"))      return t.glassBorder;
    if (prop.id == QLatin1String("card"))             return t.card;
    if (prop.id == QLatin1String("header"))           return t.header;
    if (prop.id == QLatin1String("input"))            return t.input;
    if (prop.id == QLatin1String("numFill"))          return t.numFill;
    if (prop.id == QLatin1String("numBorder"))        return t.numBorder;
    if (prop.id == QLatin1String("numText"))          return t.numText;
    if (prop.id == QLatin1String("numLabelText"))     return t.numLabelText;
    if (prop.id == QLatin1String("numBorderActive"))  return t.numBorderActive;
    if (prop.id == QLatin1String("numFillActive"))    return t.numFillActive;
    if (prop.id == QLatin1String("leafCard"))         return t.leafCard;
    return QColor();
}

void SceneTreeColorEditMode::setColorForProp(const ColorPropDef &prop,
                                               const ColorZoneHit &hit,
                                               const QColor &color)
{
    if (!prop.isGlobal && hit.hasOperation) {
        const auto op = hit.operation;
        OperationCardPalette pal = SceneTreePalette::operationCardPalette(op);
        bool applied = false;
        if (prop.id == QLatin1String("header"))          { pal.header          = color; applied = true; }
        else if (prop.id == QLatin1String("text"))       { pal.text            = color; applied = true; }
        else if (prop.id == QLatin1String("card"))       { pal.card            = color; applied = true; }
        else if (prop.id == QLatin1String("border"))     { pal.border          = color; applied = true; }
        else if (prop.id == QLatin1String("numBorderActive")) { pal.numBorderActive = color; applied = true; }
        else if (prop.id == QLatin1String("numFillActive"))   { pal.numFillActive   = color; applied = true; }
        if (applied) {
            SceneTreePalette::setOperationCardPalette(op, pal);
            return;
        }
    }
    TreeAppearanceTheme t = SceneTreePalette::customTheme();
    if (prop.id == QLatin1String("canvas"))          t.canvas          = color;
    else if (prop.id == QLatin1String("minorGrid"))  t.minorGrid       = color;
    else if (prop.id == QLatin1String("majorGrid"))  t.majorGrid       = color;
    else if (prop.id == QLatin1String("text"))       t.text            = color;
    else if (prop.id == QLatin1String("mutedText"))  t.mutedText       = color;
    else if (prop.id == QLatin1String("accent"))     t.accent          = color;
    else if (prop.id == QLatin1String("glassTop"))   t.glassTop        = color;
    else if (prop.id == QLatin1String("glassBottom"))t.glassBottom     = color;
    else if (prop.id == QLatin1String("glassBorder"))t.glassBorder     = color;
    else if (prop.id == QLatin1String("card"))       t.card            = color;
    else if (prop.id == QLatin1String("header"))     t.header          = color;
    else if (prop.id == QLatin1String("input"))      t.input           = color;
    else if (prop.id == QLatin1String("numFill"))    t.numFill         = color;
    else if (prop.id == QLatin1String("numBorder"))  t.numBorder       = color;
    else if (prop.id == QLatin1String("numText"))    t.numText         = color;
    else if (prop.id == QLatin1String("numLabelText"))t.numLabelText   = color;
    else if (prop.id == QLatin1String("numBorderActive"))t.numBorderActive = color;
    else if (prop.id == QLatin1String("numFillActive"))  t.numFillActive   = color;
    else if (prop.id == QLatin1String("leafCard"))       t.leafCard        = color;
    SceneTreePalette::setCustomTheme(t);
}
