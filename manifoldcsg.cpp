#include "manifoldcsg.h"
#include "scenestringutils.h"

#ifdef HAVE_MANIFOLD_CSG
#include "expression.h"

#include <manifold/manifold.h>

#include <QHash>
#include <QMutex>
#include <QStringList>
#include <QtMath>

// Manifold is not re-entrant: serialise all calls with this process-wide lock.
// Both the viewport background render and the group thumbnail background render
// acquire this before entering evaluateDocumentGeometry().
static QMutex s_manifoldMutex;

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

    if (shape.type == ShapeNode::Polyhedron)
        return {}; // polyhedron skipped in Manifold CSG; handled via preview only

    if (shape.type == ShapeNode::Cube) {
        result = Manifold::Cube(vec3(shape.size.x(), shape.size.y(), shape.size.z()), true);
    } else if (shape.type == ShapeNode::Sphere) {
        result = Manifold::Sphere(shape.radius, 32);
    } else if (shape.type == ShapeNode::Cone) {
        result = Manifold::Cylinder(shape.height, shape.radius, shape.radius2, 32, true);
    } else if (shape.type == ShapeNode::Circle) {
        result = Manifold::Cylinder(0.1f, shape.radius, shape.radius, 64, true);
    } else {
        result = Manifold::Cylinder(shape.height, shape.radius, shape.radius, 32, true);
    }

    return result.Rotate(shape.rotation.x(), shape.rotation.y(), shape.rotation.z())
        .Translate(vec3(shape.position.x(), shape.position.y(), shape.position.z()));
}

static bool isUnionLikeOperation(SceneDocument::TreeNode::Operation operation)
{
    return operation == SceneDocument::TreeNode::Union
           || operation == SceneDocument::TreeNode::Module
           || operation == SceneDocument::TreeNode::Scene
           || operation == SceneDocument::TreeNode::Translate
           || operation == SceneDocument::TreeNode::Rotate
           || operation == SceneDocument::TreeNode::Scale
           || operation == SceneDocument::TreeNode::Mirror
           || operation == SceneDocument::TreeNode::Color
           || operation == SceneDocument::TreeNode::LinearExtrude;
}

static Manifold applyNodeTransform(const Manifold &source, const SceneDocument::TreeNode &node)
{
    if (node.operation == SceneDocument::TreeNode::Translate)
        return source.Translate(vec3(node.position.x(), node.position.y(), node.position.z()));
    if (node.operation == SceneDocument::TreeNode::Rotate)
        return source.Rotate(node.rotation.x(), node.rotation.y(), node.rotation.z());
    if (node.operation == SceneDocument::TreeNode::Scale)
        return source.Scale(vec3(node.scale.x(), node.scale.y(), node.scale.z()));
    if (node.operation == SceneDocument::TreeNode::LinearExtrude) {
        constexpr float baseThickness = 0.1f;
        const float height = qMax(0.1f, node.scale.x());
        return source.Scale(vec3(1.0f, 1.0f, height / baseThickness));
    }
    if (node.operation == SceneDocument::TreeNode::Mirror) {
        const QVector3D &n = node.position;
        if (qFuzzyIsNull(n.x()) && qFuzzyIsNull(n.y()) && qFuzzyIsNull(n.z()))
            return source; // zero normal = no-op, matches OpenSCAD behaviour
        return source.Mirror(vec3(n.x(), n.y(), n.z()));
    }

    return source;
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

    if (evaluatedNode.operation == SceneDocument::TreeNode::Hull) {
        std::vector<Manifold> parts;
        for (const SceneDocument::TreeNode *child : geometryChildren) {
            const Manifold part = evaluateNode(*child, scene, localVariables);
            if (!part.IsEmpty())
                parts.push_back(part);
        }
        if (parts.empty())
            return {};
        return applyNodeTransform(Manifold::Hull(parts), evaluatedNode);
    }

    if (evaluatedNode.operation == SceneDocument::TreeNode::Minkowski) {
        if (geometryChildren.size() < 2)
            return geometryChildren.isEmpty() ? Manifold{} : evaluateNode(*geometryChildren.first(), scene, localVariables);
        Manifold result = evaluateNode(*geometryChildren.first(), scene, localVariables);
        for (int i = 1; i < geometryChildren.size(); ++i)
            result = result.MinkowskiSum(evaluateNode(*geometryChildren[i], scene, localVariables));
        return applyNodeTransform(result, evaluatedNode);
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

    QMutexLocker locker(&s_manifoldMutex); // serialise concurrent calls from any thread

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
