#include "manifoldcsg.h"

#ifdef HAVE_MANIFOLD_CSG
#include <manifold/manifold.h>

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

static Manifold evaluateNode(const SceneDocument::TreeNode &node, const SceneDocument &scene)
{
    if (node.type == SceneDocument::TreeNode::Primitive) {
        const ShapeNode *shape = scene.shapeById(node.shapeId);
        if (!shape)
            return {};

        return manifoldFromShape(*shape);
    }

    if (node.type == SceneDocument::TreeNode::Variable)
        return {};

    QVector<const SceneDocument::TreeNode *> geometryChildren;
    for (const SceneDocument::TreeNode &child : node.children) {
        if (child.type != SceneDocument::TreeNode::Variable)
            geometryChildren.append(&child);
    }

    if (geometryChildren.isEmpty())
        return {};

    Manifold result = evaluateNode(*geometryChildren.first(), scene);

    for (int i = 1; i < geometryChildren.size(); ++i) {
        const Manifold child = evaluateNode(*geometryChildren[i], scene);

        if (node.operation == SceneDocument::TreeNode::Union
            || node.operation == SceneDocument::TreeNode::Module
            || node.operation == SceneDocument::TreeNode::Translate
            || node.operation == SceneDocument::TreeNode::Rotate
            || node.operation == SceneDocument::TreeNode::Scale)
            result += child;
        else if (node.operation == SceneDocument::TreeNode::Difference)
            result -= child;
        else if (node.operation == SceneDocument::TreeNode::Intersection)
            result ^= child;
    }

    return applyNodeTransform(result, node);
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

    const Manifold result = evaluateNode(scene.treeRoot(), scene);

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
