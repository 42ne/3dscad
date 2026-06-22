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

void collectVariableValueMaps(const SceneDocument::TreeNode &node,
                              QHash<QString, qreal> *scalars,
                              QHash<QString, ExpressionSyntax::Value> *vectors,
                              int excludedNodeId = 0)
{
    if (!scalars || !vectors)
        return;
    if (node.type == SceneDocument::TreeNode::Variable) {
        if (node.id != excludedNodeId) {
            ExpressionSyntax::Value v;
            if (!node.variableExpression.trimmed().isEmpty()
                && ExpressionSyntax::evaluateValue(node.variableExpression, *scalars, &v,
                                                   nullptr, nullptr, vectors)) {
                (*vectors)[node.variableName] = v;
                (*scalars)[node.variableName] = v.toScalar();
            } else {
                (*scalars)[node.variableName] = node.variableValue;
            }
        }
        return;
    }
    for (const SceneDocument::TreeNode &child : node.children)
        collectVariableValueMaps(child, scalars, vectors, excludedNodeId);
}

// Inject the loop variables of every enclosing `for` ancestor of `nodeId` so an
// expression edited inside a loop body can reference them (e.g. Z = 0 + i). The
// representative value is the first iteration value of the range — matching what
// the geometry evaluator uses on the first pass. Outer loops are resolved before
// inner ones so a nested loop's range may reference an outer loop variable.
void injectLoopVariables(const SceneDocument *scene, int nodeId,
                         QHash<QString, qreal> *scalars,
                         QHash<QString, ExpressionSyntax::Value> *vectors)
{
    if (!scene || !scalars || !vectors)
        return;

    // Walk parents up to the root, collecting for-loop ancestors (innermost first).
    QVector<const SceneDocument::TreeNode *> loops;
    int currentId = nodeId;
    while (true) {
        int parentId = 0;
        if (!SceneDocument::findChildParent(scene->treeRoot(), currentId, &parentId, nullptr))
            break;
        const SceneDocument::TreeNode *parent = scene->treeNodeById(parentId);
        if (!parent)
            break;
        if (parent->operation == SceneDocument::TreeNode::For)
            loops.append(parent);
        currentId = parentId;
    }

    // Resolve outermost first so inner ranges can see outer loop variables.
    for (int i = loops.size() - 1; i >= 0; --i) {
        const SceneDocument::TreeNode *loop = loops[i];
        const QString name = loop->loopVariable.trimmed().isEmpty()
                                 ? QStringLiteral("i") : loop->loopVariable.trimmed();
        const QString rangeExpr = loop->loopRangeExpression.trimmed().isEmpty()
                                      ? QStringLiteral("[0 : 1 : 3]") : loop->loopRangeExpression.trimmed();
        QVector<qreal> rangeValues;
        qreal first = 0.0;
        if (evaluateRangeExpression(rangeExpr, *scalars, &rangeValues) && !rangeValues.isEmpty())
            first = rangeValues.first();
        (*scalars)[name] = first;
        (*vectors)[name] = ExpressionSyntax::Value(first);
    }
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
    QHash<QString, ExpressionSyntax::Value> vectorValues;
    if (m_widget->m_scene) {
        collectVariableValueMaps(m_widget->m_scene->treeRoot(), &values, &vectorValues,
                                 target.kind == SceneTreeGraphicsWidget::ExpressionEditTarget::Variable ? target.nodeId : 0);
        injectLoopVariables(m_widget->m_scene, target.nodeId, &values, &vectorValues);
    }

    qreal value = 0.0;
    return ExpressionSyntax::evaluate(trimmed, values, &value, errorMessage, nullptr, &vectorValues);
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
            case SceneTreeGraphicsWidget::ExpressionEditTarget::Variable: {
                QString exprToSave = newExpression;
                if (target.spanStart >= 0 && !target.fullExpression.isEmpty()) {
                    exprToSave = target.fullExpression.left(target.spanStart)
                               + newExpression
                               + target.fullExpression.mid(target.spanStart + target.spanLength);
                }
                emit m_widget->variableExpressionEdited(target.nodeId, exprToSave);
                break;
            }
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
            case SceneTreeGraphicsWidget::ExpressionEditTarget::Condition:
                emit m_widget->conditionExpressionEdited(target.nodeId, newExpression);
                break;
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

void SceneTreeInlineEditor::startInlineTextEdit(int shapeId,
                                                const QRectF &sceneRect,
                                                const QString &currentText,
                                                std::function<void(int, const QString &)> onCommit)
{
    if (!m_inlineInput)
        return;

    m_inlineInputActive = true;
    m_inlineInputSceneRect = sceneRect;

    m_inlineInput->startEditing(
        m_widget->mapFromScene(sceneRect).boundingRect(),
        currentText,
        [this, shapeId, onCommit](const QString &newText) {
            m_inlineInputActive = false;
            m_inlineInputSceneRect = QRectF();
            if (onCommit)
                onCommit(shapeId, newText);
            else
                emit m_widget->textContentEdited(shapeId, newText);
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
