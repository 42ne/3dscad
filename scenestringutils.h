#ifndef SCENESTRINGUTILS_H
#define SCENESTRINGUTILS_H

// Shared inline utilities used by csgevaluator, manifoldcsg, scenecontroller,
// scenedocument, scenetreegraphicswidget, and openscadgenerator.
// Including this header eliminates per-file copies of these helpers.

#include "expression.h"
#include "scenedocument.h"
#include "shapenode.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

// ── Identifier validation ─────────────────────────────────────────────────────
inline bool isValidIdentifier(const QString &name)
{
    if (name.isEmpty())
        return false;

    const QChar first = name.front();
    if (!(first == QLatin1Char('_') || first == QLatin1Char('$') || first.isLetter()))
        return false;

    for (const QChar ch : name) {
        if (!(ch == QLatin1Char('_') || ch.isLetterOrNumber()))
            return false;
    }

    return true;
}

// ── Comma-split ───────────────────────────────────────────────────────────────
// Splits text on commas that are not nested inside () or [].
// Parts are trimmed; empty parts are skipped.
inline QStringList splitAtTopLevelCommas(const QString &text)
{
    QStringList result;
    int depth = 0, start = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if      (ch == QLatin1Char('(') || ch == QLatin1Char('[')) ++depth;
        else if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) --depth;
        else if (ch == QLatin1Char(',') && depth == 0) {
            const QString part = text.mid(start, i - start).trimmed();
            if (!part.isEmpty()) result.append(part);
            start = i + 1;
        }
    }
    const QString tail = text.mid(start).trimmed();
    if (!tail.isEmpty()) result.append(tail);
    return result;
}

// ── Named-argument parsing ────────────────────────────────────────────────────
// Parses "name = expr, ..." → {name → expr}.
// Named arguments only — positional arguments are ignored.
inline QHash<QString, QString> parseNamedArgumentExpressions(const QString &arguments)
{
    QHash<QString, QString> result;
    for (const QString &part : splitAtTopLevelCommas(arguments)) {
        const int equal = part.indexOf(QLatin1Char('='));
        if (equal <= 0) continue;
        const QString name = part.left(equal).trimmed();
        const QString expr = part.mid(equal + 1).trimmed();
        if (!name.isEmpty() && !expr.isEmpty())
            result[name] = expr;
    }
    return result;
}

// ── Module argument resolution ────────────────────────────────────────────────
// Maps named and positional call arguments to parameter names declared by moduleNode.
inline QHash<QString, QString> resolveModuleArguments(
    const QString &callArguments,
    const SceneDocument::TreeNode &moduleNode)
{
    QStringList paramOrder;
    for (const SceneDocument::TreeNode &child : moduleNode.children)
        if (child.type == SceneDocument::TreeNode::Variable && child.isParameter)
            paramOrder.append(child.variableName);

    QHash<QString, QString> result;
    int positionalIndex = 0;
    for (const QString &part : splitAtTopLevelCommas(callArguments)) {
        const int equal = part.indexOf(QLatin1Char('='));
        if (equal > 0) {
            const QString name = part.left(equal).trimmed();
            const QString expr = part.mid(equal + 1).trimmed();
            if (!name.isEmpty() && !expr.isEmpty())
                result[name] = expr;
        } else {
            const QString expr = part.trimmed();
            if (!expr.isEmpty() && positionalIndex < paramOrder.size())
                result[paramOrder[positionalIndex]] = expr;
            ++positionalIndex;
        }
    }
    return result;
}

// ── Transform expression evaluation ──────────────────────────────────────────
// Returns a copy of node with transformExpressions evaluated using variables
// and applied to position / rotation / scale (with axis-specific clamping for Scale).
inline SceneDocument::TreeNode nodeWithEvaluatedTransform(const SceneDocument::TreeNode &node,
                                                          const QHash<QString, qreal> &variables)
{
    SceneDocument::TreeNode evaluated = node;
    if (evaluated.transformExpressions.isEmpty())
        return evaluated;

    // LinearExtrude has 4 expressions: [height, twist, slices, scale] — allow up to 4.
    const int maxAxis = (evaluated.operation == SceneDocument::TreeNode::LinearExtrude) ? 4 : 3;
    for (int axis = 0; axis < evaluated.transformExpressions.size() && axis < maxAxis; ++axis) {
        const QString expression = evaluated.transformExpressions[axis].trimmed();
        if (expression.isEmpty())
            continue;

        qreal value = 0.0;
        if (!ExpressionSyntax::evaluate(expression, variables, &value))
            continue;

        if (evaluated.operation == SceneDocument::TreeNode::Translate) {
            if (axis == 0)
                evaluated.position.setX(static_cast<float>(value));
            else if (axis == 1)
                evaluated.position.setY(static_cast<float>(value));
            else
                evaluated.position.setZ(static_cast<float>(value));
        } else if (evaluated.operation == SceneDocument::TreeNode::Rotate) {
            if (axis == 0)
                evaluated.rotation.setX(static_cast<float>(value));
            else if (axis == 1)
                evaluated.rotation.setY(static_cast<float>(value));
            else
                evaluated.rotation.setZ(static_cast<float>(value));
        } else if (evaluated.operation == SceneDocument::TreeNode::Scale) {
            value = qMax<qreal>(0.01, value);
            if (axis == 0)
                evaluated.scale.setX(static_cast<float>(value));
            else if (axis == 1)
                evaluated.scale.setY(static_cast<float>(value));
            else
                evaluated.scale.setZ(static_cast<float>(value));
        } else if (evaluated.operation == SceneDocument::TreeNode::Mirror) {
            if (axis == 0)
                evaluated.position.setX(static_cast<float>(value));
            else if (axis == 1)
                evaluated.position.setY(static_cast<float>(value));
            else
                evaluated.position.setZ(static_cast<float>(value));
        } else if (evaluated.operation == SceneDocument::TreeNode::LinearExtrude) {
            // expressions: [0]=height, [1]=twist, [2]=slices, [3]=scale
            if (axis == 0)
                evaluated.scale.setX(static_cast<float>(qMax<qreal>(0.1, value)));
            else if (axis == 1)
                evaluated.linearExtrudeTwist = static_cast<float>(value);
            // axis 2 = slices: already stored in linearExtrudeSlices at parse time
            else if (axis == 3)
                evaluated.linearExtrudeScaleVal = static_cast<float>(qMax<qreal>(0.001, value));
        } else if (evaluated.operation == SceneDocument::TreeNode::RotateExtrude) {
            evaluated.scale.setX(static_cast<float>(qBound<qreal>(0.1, value, 360.0)));
        } else if (evaluated.operation == SceneDocument::TreeNode::Resize) {
            value = qMax<qreal>(0.01, value);
            if (axis == 0)
                evaluated.scale.setX(static_cast<float>(value));
            else if (axis == 1)
                evaluated.scale.setY(static_cast<float>(value));
            else
                evaluated.scale.setZ(static_cast<float>(value));
        }
    }

    return evaluated;
}

// ── Scoped variable merging ───────────────────────────────────────────────────
// Returns a copy of variables extended with any Variable children of node
// (Module or Scene group), substituting argumentOverrides for parameter values.
inline QHash<QString, qreal> variablesWithScopedVariables(
    const SceneDocument::TreeNode &node,
    QHash<QString, qreal> variables,
    const QHash<QString, QString> &argumentOverrides = {})
{
    if (node.type != SceneDocument::TreeNode::Group)
        return variables;

    if (node.operation != SceneDocument::TreeNode::Module
        && node.operation != SceneDocument::TreeNode::Scene)
        return variables;

    for (const SceneDocument::TreeNode &child : node.children) {
        if (child.type != SceneDocument::TreeNode::Variable)
            continue;

        qreal value = child.variableValue;
        const QString expression = child.isParameter && argumentOverrides.contains(child.variableName)
                                       ? argumentOverrides.value(child.variableName).trimmed()
                                       : child.variableExpression.trimmed();
        if (!expression.isEmpty())
            ExpressionSyntax::evaluate(expression, variables, &value);
        variables[child.variableName] = value;
    }

    return variables;
}

// ── Top-level variable collection ─────────────────────────────────────────────
// Collects all Variable nodes directly under root (or under a Scene child),
// returning their numeric values as a variable map.
inline QHash<QString, qreal> topLevelVariables(const SceneDocument::TreeNode &root)
{
    QHash<QString, qreal> variables;
    for (const SceneDocument::TreeNode &child : root.children) {
        if (child.type == SceneDocument::TreeNode::Variable) {
            variables[child.variableName] = child.variableValue;
        } else if (child.type == SceneDocument::TreeNode::Group
                   && child.operation == SceneDocument::TreeNode::Scene) {
            for (const SceneDocument::TreeNode &sceneChild : child.children) {
                if (sceneChild.type == SceneDocument::TreeNode::Variable)
                    variables[sceneChild.variableName] = sceneChild.variableValue;
            }
        }
    }
    return variables;
}

// ── Module declaration check ───────────────────────────────────────────────────
// Returns true when node is a top-level module definition (Group + Module).
inline bool isTopLevelModuleDeclaration(const SceneDocument::TreeNode &node)
{
    return node.type == SceneDocument::TreeNode::Group
           && node.operation == SceneDocument::TreeNode::Module;
}

// ── For-loop range evaluation ─────────────────────────────────────────────────
// Parses rangeExpression ("[start:end]", "[start:step:end]", or "[v1, v2, …]")
// into a list of qreal values using variables.  Returns false on parse error.
inline bool evaluateRangeExpression(const QString &rangeExpression,
                                    const QHash<QString, qreal> &variables,
                                    QVector<qreal> *values)
{
    if (!values)
        return false;

    values->clear();

    const QString range = rangeExpression.trimmed();
    if (!range.startsWith(QLatin1Char('[')) || !range.endsWith(QLatin1Char(']')))
        return false;

    const QStringList parts = range.mid(1, range.size() - 2).split(QLatin1Char(':'));
    if (parts.size() != 2 && parts.size() != 3) {
        // Might be a comma-separated list: [45, 135, 225, 315]
        const QStringList listParts = splitAtTopLevelCommas(range.mid(1, range.size() - 2));
        if (listParts.size() < 1)
            return false;
        for (const QString &p : listParts) {
            qreal v = 0.0;
            if (!ExpressionSyntax::evaluate(p.trimmed(), variables, &v))
                return false;
            values->append(v);
        }
        return !values->isEmpty();
    }

    qreal start = 0.0;
    qreal step  = 1.0;
    qreal end   = 0.0;
    if (!ExpressionSyntax::evaluate(parts[0].trimmed(), variables, &start))
        return false;
    if (parts.size() == 2) {
        if (!ExpressionSyntax::evaluate(parts[1].trimmed(), variables, &end))
            return false;
    } else {
        if (!ExpressionSyntax::evaluate(parts[1].trimmed(), variables, &step))
            return false;
        if (!ExpressionSyntax::evaluate(parts[2].trimmed(), variables, &end))
            return false;
    }

    if (qFuzzyIsNull(step))
        return false;

    constexpr int MaxForIterations = 200;
    const qreal epsilon = qAbs(step) * 0.0001 + 0.0001;
    for (qreal value = start;
         step > 0.0 ? value <= end + epsilon : value >= end - epsilon;
         value += step) {
        values->append(value);
        if (values->size() >= MaxForIterations)
            break;
    }

    return true;
}

// ── Shape parameter evaluation ────────────────────────────────────────────────
// Returns a copy of shape with parameterExpressions evaluated using variables
// and results applied to physical fields (size / radius / radius2 / height).
// Clamping is delegated to ShapeNode::applyParameterValue.
inline ShapeNode shapeWithEvaluatedParameters(const ShapeNode &shape,
                                               const QHash<QString, qreal> &variables)
{
    ShapeNode evaluated = shape;
    if (evaluated.parameterExpressions.isEmpty())
        return evaluated;

    for (int i = 0; i < evaluated.parameterExpressions.size(); ++i) {
        const QString expression = evaluated.parameterExpressions[i].trimmed();
        if (expression.isEmpty())
            continue;
        qreal value = 0.0;
        if (!ExpressionSyntax::evaluate(expression, variables, &value))
            continue;
        evaluated.applyParameterValue(i, value); // raw value; clamping is type-aware
    }

    return evaluated;
}

#endif // SCENESTRINGUTILS_H
