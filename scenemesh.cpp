#include "scenemesh.h"

#include <QtMath>

static QVector3D rotatePoint(const QVector3D &point, const QVector3D &degrees)
{
    const float rx = qDegreesToRadians(degrees.x());
    const float ry = qDegreesToRadians(degrees.y());
    const float rz = qDegreesToRadians(degrees.z());

    QVector3D p = point;

    p = QVector3D(
        p.x(),
        p.y() * qCos(rx) - p.z() * qSin(rx),
        p.y() * qSin(rx) + p.z() * qCos(rx));

    p = QVector3D(
        p.x() * qCos(ry) + p.z() * qSin(ry),
        p.y(),
        -p.x() * qSin(ry) + p.z() * qCos(ry));

    p = QVector3D(
        p.x() * qCos(rz) - p.y() * qSin(rz),
        p.x() * qSin(rz) + p.y() * qCos(rz),
        p.z());

    return p;
}

static QVector3D faceNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    QVector3D normal = -QVector3D::crossProduct(b - a, c - a);

    if (normal.lengthSquared() <= 0.0001f)
        return QVector3D(0.0f, 0.0f, 1.0f);

    return normal.normalized();
}

static MeshFace makeFace(const QVector<QVector3D> &vertices, int shade = 100)
{
    MeshFace face;
    face.vertices = vertices;
    face.shade = shade;

    if (vertices.size() >= 3)
        face.normal = faceNormal(vertices[0], vertices[1], vertices[2]);

    return face;
}

static SceneMesh buildCubeMesh(const ShapeNode &shape)
{
    SceneMesh mesh;
    const QVector3D half = shape.size * 0.5f;
    QVector<QVector3D> vertices = {
        {-half.x(), -half.y(), -half.z()}, {half.x(), -half.y(), -half.z()},
        {half.x(), half.y(), -half.z()}, {-half.x(), half.y(), -half.z()},
        {-half.x(), -half.y(), half.z()}, {half.x(), -half.y(), half.z()},
        {half.x(), half.y(), half.z()}, {-half.x(), half.y(), half.z()}
    };

    for (QVector3D &vertex : vertices)
        vertex = rotatePoint(vertex, shape.rotation) + shape.position;

    mesh.shadowPoints = vertices;

    const QVector<QVector<int>> faceIndices = {
        {0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
        {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}
    };

    const QVector<int> shades = {82, 116, 92, 105, 122, 96};
    for (int i = 0; i < faceIndices.size(); ++i) {
        QVector<QVector3D> faceVertices;
        for (int index : faceIndices[i])
            faceVertices.append(vertices[index]);

        mesh.faces.append(makeFace(faceVertices, shades[i]));
    }

    return mesh;
}

static SceneMesh buildSphereMesh(const ShapeNode &shape)
{
    SceneMesh mesh;
    const int stacks = 12;
    const int sectors = 24;

    for (int i = 0; i < sectors; ++i) {
        const float angle = 2.0f * M_PI * i / sectors;
        mesh.shadowPoints.append(shape.position + QVector3D(shape.radius * qCos(angle),
                                                            shape.radius * qSin(angle),
                                                            shape.radius));
    }

    auto spherePoint = [&](int stack, int sector) {
        const float theta = M_PI * stack / stacks;
        const float phi = 2.0f * M_PI * sector / sectors;
        const float ringRadius = shape.radius * qSin(theta);

        return shape.position + QVector3D(
                   ringRadius * qCos(phi),
                   ringRadius * qSin(phi),
                   shape.radius * qCos(theta));
    };

    for (int stack = 0; stack < stacks; ++stack) {
        for (int sector = 0; sector < sectors; ++sector) {
            const int nextSector = (sector + 1) % sectors;
            const QVector3D topLeft = spherePoint(stack, sector);
            const QVector3D bottomLeft = spherePoint(stack + 1, sector);
            const QVector3D bottomRight = spherePoint(stack + 1, nextSector);
            const QVector3D topRight = spherePoint(stack, nextSector);

            if (stack == 0)
                mesh.faces.append(makeFace({topLeft, bottomLeft, bottomRight}));
            else if (stack == stacks - 1)
                mesh.faces.append(makeFace({topLeft, bottomLeft, topRight}));
            else
                mesh.faces.append(makeFace({topLeft, bottomLeft, bottomRight, topRight}));
        }
    }

    return mesh;
}

static SceneMesh buildCylinderMesh(const ShapeNode &shape)
{
    SceneMesh mesh;
    QVector<QVector3D> top;
    QVector<QVector3D> bottom;
    const int sectors = 24;

    for (int i = 0; i < sectors; ++i) {
        const float angle = 2.0f * M_PI * i / sectors;
        const QVector3D ringPoint(shape.radius * qCos(angle), shape.radius * qSin(angle), 0);
        top.append(rotatePoint(ringPoint + QVector3D(0, 0, shape.height * 0.5f), shape.rotation) + shape.position);
        bottom.append(rotatePoint(ringPoint - QVector3D(0, 0, shape.height * 0.5f), shape.rotation) + shape.position);
    }

    mesh.shadowPoints = bottom + top;

    for (int i = 0; i < top.size(); ++i) {
        const int next = (i + 1) % top.size();
        mesh.faces.append(makeFace({bottom[i], bottom[next], top[next], top[i]}));
    }

    mesh.faces.append(makeFace(bottom, 82));
    mesh.faces.append(makeFace(top, 120));

    return mesh;
}

SceneMesh buildShapeMesh(const ShapeNode &shape)
{
    if (shape.type == ShapeNode::Sphere)
        return buildSphereMesh(shape);

    if (shape.type == ShapeNode::Cylinder)
        return buildCylinderMesh(shape);

    return buildCubeMesh(shape);
}
