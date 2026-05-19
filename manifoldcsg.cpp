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
        }
    }

    return evaluated;
}

static QHash<QString, qreal> variablesWithModuleVariables(const SceneDocument::TreeNode &node,
                                                          QHash<QString, qreal> variables)
{
    if (node.type != SceneDocument::TreeNode::Group
        || node.operation != SceneDocument::TreeNode::Module)
        return variables;

    for (const SceneDocument::TreeNode &child : node.children) {
        if (child.type != SceneDocument::TreeNode::Variable)
            continue;

        qreal value = child.variableValue;
        const QString expression = child.variableExpression.trimmed();
        if (!expression.isEmpty())
            ExpressionSyntax::evaluate(expression, variables, &value);
        variables[child.variableName] = value;
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
    if (parts.size() != 2 && parts.size() != 3)
        return false;

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
                             QHash<QString, qreal> variables)
{
    if (node.type == SceneDocument::TreeNode::Primitive) {
        const ShapeNode *shape = scene.shapeById(node.shapeId);
        if (!shape)
            return {};

        return manifoldFromShape(shapeWithEvaluatedParameters(*shape, variables));
    }

    if (node.type == SceneDocument::TreeNode::Variable)
        return {};

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

    const QHash<QString, qreal> localVariables = variablesWithModuleVariables(node, variables);
    const SceneDocument::TreeNode evaluatedNode = nodeWithEvaluatedTransform(node, localVariables);

    QVector<const SceneDocument::TreeNode *> geometryChildren;
    for (const SceneDocument::TreeNode &child : evaluatedNode.children) {
        if (child.type != SceneDocument::TreeNode::Variable)
            geometryChildren.append(&child);
    }

    if (geometryChildren.isEmpty())
        return {};

    Manifold result = evaluateNode(*geometryChildren.first(), scene, localVariables);

    for (int i = 1; i < geometryChildren.size(); ++i) {
        const Manifold child = evaluateNode(*geometryChildren[i], scene, localVariables);

        if (evaluatedNode.operation == SceneDocument::TreeNode::Union
            || evaluatedNode.operation == SceneDocument::TreeNode::Module
            || evaluatedNode.operation == SceneDocument::TreeNode::Translate
            || evaluatedNode.operation == SceneDocument::TreeNode::Rotate
            || evaluatedNode.operation == SceneDocument::TreeNode::Scale)
            result += child;
        else if (evaluatedNode.operation == SceneDocument::TreeNode::Difference)
            result -= child;
        else if (evaluatedNode.operation == SceneDocument::TreeNode::Intersection)
            result ^= child;
    }

    return applyNodeTransform(result, evaluatedNode);
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

    QHash<QString, qreal> variables;
    for (const SceneDocument::TreeNode &child : scene.treeRoot().children) {
        if (child.type == SceneDocument::TreeNode::Variable)
            variables[child.variableName] = child.variableValue;
    }

    const Manifold result = evaluateNode(scene.treeRoot(), scene, variables);

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
