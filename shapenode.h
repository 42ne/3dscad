#ifndef SHAPENODE_H
#define SHAPENODE_H

#include <QString>
#include <QVector3D>

struct ShapeNode
{
    enum Type {
        Cube,
        Sphere,
        Cylinder
    };

    Type type = Cube;
    QString name;

    QVector3D position = QVector3D(0, 0, 0);
    QVector3D rotation = QVector3D(0, 0, 0);
    QVector3D size = QVector3D(20, 20, 20);

    float radius = 10.0f;
    float height = 20.0f;
};

#endif
