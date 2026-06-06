#include "scenetreeinlineeditor.h"
#include "scenetreegraphicswidget.h"
#include "scenetreehovermanager.h"
#include "scenetreeinlinetextinput.h"
#include "scenetreegraphicshelpers.h"
#include "expression.h"
#include "scenestringutils.h"

#include <QHash>

namespace {

void collectVariableValues(const SceneDocument::TreeNode &node, QHash<QString, qreal> *values, int excludedNodeId = 0)
{
    if (!values)
        return;
    if (node.type == SceneDocument::TreeNode::Variable) {
        if (node.id != excludedNodeId)
            (*values)[node.variableName] = node.variableValue;
        return;
    }
    for (const SceneDocument::TreeNode &child : node.children)
        collectVariableValues(child, values, excludedNodeId);
}

}

SceneTreeInlineEditor::SceneTreeInlineEditor(SceneTreeGraphicsWidget *widget)
    : QObject(widget)
    , m_widget(widget)
{
    m_inlineInput = new SceneTreeInlineTextInput(widget);
}

bool SceneTreeInlineEditor::validateInlineExpression(const SceneTreeGraphicsWidget::ExpressionEditTarget &target,
                                                     const QString &expression,
                                                     QString *errorMessage) const
{
    const QString trimmed = expression.trimmed();
    if (trimmed.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Expression cannot be empty.");
        return false;
    }

    if (target.kind == SceneTreeGraphicsWidget::ExpressionEditTarget::ModuleCallArgument)
        return ExpressionSyntax::validate(trimmed, errorMessage);

    // LinearExtrude full expression (secondaryId == -1): "h=20, c=false, t=0, sl=0, sc=1"
    if (target.kind == SceneTreeGraphicsWidget::ExpressionEditTarget::Transform
        && target.secondaryId == -1 && m_widget->m_scene) {
        const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(target.nodeId);
        if (node && node->operation == SceneDocument::TreeNode::LinearExtrude) {
            // Must have at least one recognizable key=value pair.
            const bool hasParam = trimmed.contains(QLatin1Char('='))
                && (trimmed.contains(QStringLiteral("h="))    || trimmed.contains(QStringLiteral("height="))
                 || trimmed.contains(QStringLiteral("t="))    || trimmed.contains(QStringLiteral("twist="))
                 || trimmed.contains(QStringLiteral("sl="))   || trimmed.contains(QStringLiteral("slices="))
                 || trimmed.contains(QStringLiteral("sc="))   || trimmed.contains(QStringLiteral("scale=")));
            if (hasParam) return true;
            if (errorMessage) *errorMessage = QStringLiteral("Use: h=<height>, c=true/false, t=<twist>, sl=<slices>, sc=<scale>");
            return false;
        }
    }

    // LinearExtrude center (boolean) — accept true/false
    if (target.kind == SceneTreeGraphicsWidget::ExpressionEditTarget::Transform
        && target.secondaryId == 1 && m_widget->m_scene) {
        const SceneDocument::TreeNode *node = m_widget->m_scene->treeNodeById(target.nodeId);
        if (node && node->operation == SceneDocument::TreeNode::LinearExtrude) {
            const QString low = trimmed.toLower();
            if (low == QStringLiteral("true") || low == QStringLiteral("false")
                || low == QStringLiteral("1") || low == QStringLiteral("0")) {
                return true;
            }
            if (errorMessage)
                *errorMessage = QStringLiteral("Enter true or false.");
            return false;
        }
    }

    if (target.kind == SceneTreeGraphicsWidget::ExpressionEditTarget::PolyhedronParticipation) {
        bool ok = false;
        trimmed.toInt(&ok);
        if (!ok && errorMessage)
            *errorMessage = QStringLiteral("Enter a vertex position (0–N) or -1 for none.");
        return ok;
    }

    QHash<QString, qreal> values;
    if (m_widget->m_scene)
        collectVariableValues(m_widget->m_scene->treeRoot(), &values,
                              target.kind == SceneTreeGraphicsWidget::ExpressionEditTarget::Variable ? target.nodeId : 0);

    qreal value = 0.0;
    return ExpressionSyntax::evaluate(trimmed, values, &value, errorMessage);
}

void SceneTreeInlineEditor::startInlineExpressionEdit(const SceneTreeGraphicsWidget::ExpressionEditTarget &target)
{
    if (!m_inlineInput || target.kind == SceneTreeGraphicsWidget::ExpressionEditTarget::None)
        return;

    m_inlineInputActive = true;
    m_inlineInputSceneRect = target.editRect;
    m_widget->m_hoverManager->m_hoveredScrollRect = QRectF();
    m_widget->m_hoverManager->updateActiveShapeParameterControl(QPointF(), false);

    m_inlineInput->startEditing(
        m_widget->mapFromScene(target.editRect).boundingRect(),
        target.expression,
        [this, target](const QString &newExpression) {
            QString error;
            if (!validateInlineExpression(target, newExpression, &error)) {
                m_widget->m_hoverManager->updateHoverHint(QStringLiteral("expression-error:%1:%2")
                                    .arg(target.nodeId).arg(target.secondaryId),
                                QStringLiteral("Expression error\n%1\nEditing stays active").arg(error));
                return false;
            }

            m_inlineInputActive = false;
            m_inlineInputSceneRect = QRectF();
            m_widget->m_hoverManager->m_hoveredExpressionRect = QRectF();
            if (newExpression == target.expression.trimmed()) {
                m_widget->m_hoverManager->updateHighlightOverlay();
                return true;
            }

            switch (target.kind) {
            case SceneTreeGraphicsWidget::ExpressionEditTarget::Transform:
                emit m_widget->transformExpressionEdited(target.nodeId, target.secondaryId, newExpression);
                break;
            case SceneTreeGraphicsWidget::ExpressionEditTarget::ShapeParameter: {
                QString exprToSave = newExpression;
                if (target.spanStart >= 0 && !target.fullExpression.isEmpty()) {
                    exprToSave = target.fullExpression.left(target.spanStart)
                               + newExpression
                               + target.fullExpression.mid(target.spanStart + target.spanLength);
                }
                emit m_widget->shapeParameterExpressionEdited(target.nodeId, target.secondaryId, exprToSave);
                break;
            }
            case SceneTreeGraphicsWidget::ExpressionEditTarget::Polygon2DPoint: {
                const int pointIndex = target.secondaryId / 2;
                const int coord = target.secondaryId % 2;
                emit m_widget->polygon2DPointExpressionEdited(target.nodeId, pointIndex, coord, newExpression);
                break;
            }
            case SceneTreeGraphicsWidget::ExpressionEditTarget::Variable:
                emit m_widget->variableExpressionEdited(target.nodeId, newExpression);
                break;
            case SceneTreeGraphicsWidget::ExpressionEditTarget::ModuleCallArgument:
                emit m_widget->moduleCallArgumentExpressionEdited(target.nodeId, target.secondaryId, newExpression);
                break;
            case SceneTreeGraphicsWidget::ExpressionEditTarget::PolyhedronParticipation: {
                bool ok = false;
                int newPos = newExpression.trimmed().toInt(&ok);
                if (ok)
                    emit m_widget->polyhedronFaceParticipationAdjusted(target.nodeId, target.secondaryId, newPos);
                break;
            }
            case SceneTreeGraphicsWidget::ExpressionEditTarget::None:
                break;
            }
            m_widget->m_hoverManager->updateHighlightOverlay();
            return true;
        },
        [this]() {
            m_inlineInputActive = false;
            m_inlineInputSceneRect = QRectF();
            m_widget->m_hoverManager->m_hoveredExpressionRect = QRectF();
            m_widget->m_hoverManager->updateHighlightOverlay();
        });
}

void SceneTreeInlineEditor::startInlineRename(int nodeId,
                                              bool isModule,
                                              const QRectF &sceneRect,
                                              const QString &currentName)
{
    if (!m_inlineInput)
        return;

    m_inlineInputActive = true;
    m_inlineInputSceneRect = sceneRect;

    m_inlineInput->startEditing(
        m_widget->mapFromScene(sceneRect).boundingRect(),
        currentName,
        [this, nodeId, isModule](const QString &newName) {
            m_inlineInputActive = false;
            m_inlineInputSceneRect = QRectF();
            if (newName.isEmpty() || newName == (isModule
                    ? (m_widget->m_scene ? m_widget->m_scene->treeNodeById(nodeId) ?
                           m_widget->m_scene->treeNodeById(nodeId)->moduleName : QString() : QString())
                    : (m_widget->m_scene ? m_widget->m_scene->treeNodeById(nodeId) ?
                           m_widget->m_scene->treeNodeById(nodeId)->variableName : QString() : QString())))
                return true;
            if (isModule) {
                emit m_widget->moduleRenameRequested(nodeId, newName);
            } else {
                emit m_widget->variableRenameRequested(nodeId, newName);
            }
            return true;
        },
        [this]() {
            m_inlineInputActive = false;
            m_inlineInputSceneRect = QRectF();
        });
}

void SceneTreeInlineEditor::updateInlineInputGeometry()
{
    if (!m_inlineInput || !m_inlineInputActive || !m_inlineInputSceneRect.isValid())
        return;

    m_inlineInput->reposition(m_widget->mapFromScene(m_inlineInputSceneRect).boundingRect());
    m_inlineInput->raise();
}
