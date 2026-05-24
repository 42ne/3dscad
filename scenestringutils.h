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
