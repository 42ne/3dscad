#include "manifoldcsg.h"

#ifdef HAVE_MANIFOLD_CSG
#include "expression.h"

#include <manifold/manifold.h>

#include <QHash>
#include <QStringList>
#include <QtMath>

using manifold::Manifold;
using manifold::MeshGL;
using manifold::vec3;

static QVector3D faceNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    QVector3D normal = -QVector3D::crossProduct(b - a, c - a);

    if (normal.lengthSquared() <= 0.0001f)
        return QVector3D(0.0f, 0.0f, 1.0f);

    return normal.normalized();
}

static MeshTriangle makeTriangle(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    MeshTriangle triangle;
    triangle.a = a;
    triangle.b = b;
    triangle.c = c;
    triangle.normal = faceNormal(a, b, c);
    triangle.shade = 108;
    return triangle;
}

static Manifold manifoldFromShape(const ShapeNode &shape)
{
    Manifold result;

    if (shape.type == ShapeNode::Cube) {
        result = Manifold::Cube(vec3(shape.size.x(), shape.size.y(), shape.size.z()), true);
    } else if (shape.type == ShapeNode::Sphere) {
        result = Manifold::Sphere(shape.radius, 32);
    } else {
        result = Manifold::Cylinder(shape.height, shape.radius, shape.radius, 32, true);
    }

    return result.Rotate(shape.rotation.x(), shape.rotation.y(), shape.rotation.z())
        .Translate(vec3(shape.position.x(), shape.position.y(), shape.position.z()));
}

static ShapeNode shapeWithEvaluatedParameters(const ShapeNode &shape, const QHash<QString, qreal> &variables)
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

        value = qMax<qreal>(0.1, value);
        if (evaluated.type == ShapeNode::Cube) {
            if (i == 0)
                evaluated.size.setX(static_cast<float>(value));
            else if (i == 1)
                evaluated.size.setY(static_cast<float>(value));
            else if (i == 2)
                evaluated.size.setZ(static_cast<float>(value));
        } else if (evaluated.type == ShapeNode::Sphere) {
            if (i == 0)
                evaluated.radius = static_cast<float>(value);
        } else if (evaluated.type == ShapeNode::Cylinder) {
            if (i == 0)
                evaluated.radius = static_cast<float>(value);
            else if (i == 1)
                evaluated.height = static_cast<float>(value);
        }
    }

    return evaluated;
}

static SceneDocument::TreeNode nodeWithEvaluatedTransform(const SceneDocument::TreeNode &node,
                                                          const QHash<QString, qreal> &variables)
{
    SceneDocument::TreeNode evaluated = node;
    if (evaluated.transformExpressions.isEmpty())
        return evaluated;

    for (int axis = 0; axis < evaluated.transformExpressions.size() && axis < 3; ++axis) {
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
        }
    }

    return evaluated;
}

static bool isUnionLikeOperation(SceneDocument::TreeNode::Operation operation)
{
    return operation == SceneDocument::TreeNode::Union
           || operation == SceneDocument::TreeNode::Module
           || operation == SceneDocument::TreeNode::Scene
           || operation == SceneDocument::TreeNode::Translate
           || operation == SceneDocument::TreeNode::Rotate
           || operation == SceneDocument::TreeNode::Scale
           || operation == SceneDocument::TreeNode::Mirror;
}

static bool isTopLevelModuleDeclaration(const SceneDocument::TreeNode &node)
{
    return node.type == SceneDocument::TreeNode::Group
           && node.operation == SceneDocument::TreeNode::Module;
}

static QHash<QString, qreal> variablesWithScopedVariables(const SceneDocument::TreeNode &node,
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

static QStringList splitAtTopLevelCommas(const QString &text)
{
    QStringList result;
    int depth = 0;
    int start = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (ch == QLatin1Char('(') || ch == QLatin1Char('['))
            ++depth;
        else if (ch == QLatin1Char(')') || ch == QLatin1Char(']'))
            --depth;
        else if (ch == QLatin1Char(',') && depth == 0) {
            const QString part = text.mid(start, i - start).trimmed();
            if (!part.isEmpty())
                result.append(part);
            start = i + 1;
        }
    }
    const QString tail = text.mid(start).trimmed();
    if (!tail.isEmpty())
        result.append(tail);
    return result;
}

// Resolves both named and positional call arguments against a module's parameter list.
static QHash<QString, QString> resolveModuleArguments(
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
            const QString expr  = part.mid(equal + 1).trimmed();
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

static QHash<QString, qreal> topLevelVariables(const SceneDocument::TreeNode &root)
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

static Manifold applyNodeTransform(const Manifold &source, const SceneDocument::TreeNode &node)
{
    if (node.operation == SceneDocument::TreeNode::Translate)
        return source.Translate(vec3(node.position.x(), node.position.y(), node.position.z()));
    if (node.operation == SceneDocument::TreeNode::Rotate)
        return source.Rotate(node.rotation.x(), node.rotation.y(), node.rotation.z());
    if (node.operation == SceneDocument::TreeNode::Scale)
        return source.Scale(vec3(node.scale.x(), node.scale.y(), node.scale.z()));
    if (node.operation == SceneDocument::TreeNode::Mirror)
        return source.Mirror(vec3(node.position.x(), node.position.y(), node.position.z()));

    return source;
}

static bool evaluateRangeExpression(const QString &rangeExpression,
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
    qreal step = 1.0;
    qreal end = 0.0;
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

static Manifold evaluateNode(const SceneDocument::TreeNode &node,
                             const SceneDocument &scene,
                             QHash<QString, qreal> variables,
                             const QHash<QString, QString> &moduleArgumentOverrides = {})
{
    if (node.type == SceneDocument::TreeNode::Primitive) {
        const ShapeNode *shape = scene.shapeById(node.shapeId);
        if (!shape)
            return {};

        return manifoldFromShape(shapeWithEvaluatedParameters(*shape, variables));
    }

    if (node.type == SceneDocument::TreeNode::Variable)
        return {};

    if (node.type == SceneDocument::TreeNode::ModuleCall) {
        const SceneDocument::TreeNode *module = scene.treeNodeById(node.shapeId);
        if (!module || module->type != SceneDocument::TreeNode::Group
            || module->operation != SceneDocument::TreeNode::Module)
            return {};

        return evaluateNode(*module, scene, variables, resolveModuleArguments(node.moduleCallArguments, *module));
    }

    if (node.operation == SceneDocument::TreeNode::For) {
        QVector<qreal> values;
        const QString variableName = node.loopVariable.trimmed().isEmpty()
                                         ? QStringLiteral("i")
                                         : node.loopVariable.trimmed();
        const QString rangeExpression = node.loopRangeExpression.trimmed().isEmpty()
                                            ? QStringLiteral("[0 : 1 : 3]")
                                            : node.loopRangeExpression.trimmed();
        if (!evaluateRangeExpression(rangeExpression, variables, &values) || values.isEmpty())
            return {};

        bool hasResult = false;
        Manifold result;
        for (qreal value : values) {
            QHash<QString, qreal> iterationVariables = variables;
            iterationVariables[variableName] = value;
            for (const SceneDocument::TreeNode &child : node.children) {
                if (child.type == SceneDocument::TreeNode::Variable)
                    continue;

                const Manifold childResult = evaluateNode(child, scene, iterationVariables);
                if (!hasResult) {
                    result = childResult;
                    hasResult = true;
                } else {
                    result += childResult;
                }
            }
        }

        return result;
    }

    const QHash<QString, qreal> localVariables = variablesWithScopedVariables(node, variables, moduleArgumentOverrides);
    const SceneDocument::TreeNode evaluatedNode = nodeWithEvaluatedTransform(node, localVariables);

    QVector<const SceneDocument::TreeNode *> geometryChildren;
    for (const SceneDocument::TreeNode &child : evaluatedNode.children) {
        if (child.type != SceneDocument::TreeNode::Variable)
            geometryChildren.append(&child);
    }

    if (geometryChildren.isEmpty())
        return {};

    // Hull: union all children first, then compute convex hull via instance method.
    // Using the instance Hull() is more reliable than the static overload across versions.
    if (evaluatedNode.operation == SceneDocument::TreeNode::Hull) {
        Manifold combined;
        bool hasAny = false;
        for (const SceneDocument::TreeNode *child : geometryChildren) {
            Manifold part = evaluateNode(*child, scene, localVariables);
            if (!part.IsEmpty()) {
                combined = hasAny ? combined + part : part;
                hasAny = true;
            }
        }
        return hasAny ? combined.Hull() : Manifold{};
    }

    Manifold result = evaluateNode(*geometryChildren.first(), scene, localVariables);

    for (int i = 1; i < geometryChildren.size(); ++i) {
        const Manifold child = evaluateNode(*geometryChildren[i], scene, localVariables);

        if (isUnionLikeOperation(evaluatedNode.operation))
            result += child;
        else if (evaluatedNode.operation == SceneDocument::TreeNode::Difference)
            result -= child;
        else if (evaluatedNode.operation == SceneDocument::TreeNode::Intersection)
            result ^= child;
    }

    return applyNodeTransform(result, evaluatedNode);
}

static Manifold evaluateDocumentGeometry(const SceneDocument &scene)
{
    const QHash<QString, qreal> variables = topLevelVariables(scene.treeRoot());
    bool hasResult = false;
    Manifold result;

    for (const SceneDocument::TreeNode &child : scene.treeRoot().children) {
        if (isTopLevelModuleDeclaration(child))
            continue;

        const Manifold childResult = evaluateNode(child, scene, variables);
        if (!hasResult) {
            result = childResult;
            hasResult = true;
        } else {
            result += childResult;
        }
    }

    return result;
}

static SceneMesh sceneMeshFromManifold(const Manifold &manifold)
{
    SceneMesh mesh;
    const MeshGL meshGl = manifold.GetMeshGL();

    mesh.triangles.reserve(static_cast<int>(meshGl.triVerts.size() / 3));
    mesh.shadowPoints.reserve(static_cast<int>(meshGl.vertProperties.size() / meshGl.numProp));

    for (size_t i = 0; i < meshGl.vertProperties.size(); i += meshGl.numProp) {
        mesh.shadowPoints.append(QVector3D(meshGl.vertProperties[i],
                                           meshGl.vertProperties[i + 1],
                                           meshGl.vertProperties[i + 2]));
    }

    auto vertex = [&](uint32_t index) {
        const size_t offset = static_cast<size_t>(index) * meshGl.numProp;
        return QVector3D(meshGl.vertProperties[offset],
                         meshGl.vertProperties[offset + 1],
                         meshGl.vertProperties[offset + 2]);
    };

    for (size_t i = 0; i + 2 < meshGl.triVerts.size(); i += 3) {
        mesh.triangles.append(makeTriangle(vertex(meshGl.triVerts[i]),
                                           vertex(meshGl.triVerts[i + 1]),
                                           vertex(meshGl.triVerts[i + 2])));
    }

    return mesh;
}
#endif

bool buildManifoldCsgMesh(const QVector<ShapeNode> &shapes, SceneMesh *mesh, QString *errorMessage)
{
    SceneDocument scene;
    scene.replaceShapes(shapes);
    return buildManifoldCsgMesh(scene, mesh, errorMessage);
}

bool buildManifoldCsgMesh(const SceneDocument &scene, SceneMesh *mesh, QString *errorMessage)
{
#ifdef HAVE_MANIFOLD_CSG
    if (!mesh)
        return false;

    const Manifold result = evaluateDocumentGeometry(scene);

    if (result.Status() != Manifold::Error::NoError) {
        if (errorMessage)
            *errorMessage = "Manifold returned an invalid CSG result.";
        return false;
    }

    if (result.IsEmpty()) {
        if (errorMessage)
            *errorMessage = "Manifold returned an empty CSG result.";
        return false;
    }

    *mesh = sceneMeshFromManifold(result);
    return !mesh->triangles.isEmpty();
#else
    Q_UNUSED(scene);
    Q_UNUSED(mesh);
    if (errorMessage)
        *errorMessage = "Manifold CSG is not built.";
    return false;
#endif
}
