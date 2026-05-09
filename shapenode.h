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

    enum BooleanMode {
        Add,
        Subtract
    };

    int id = -1;
    Type type = Cube;
    BooleanMode booleanMode = Add;
    QString name;

    QVector3D position = QVector3D(0, 0, 0);
    QVector3D rotation = QVector3D(0, 0, 0);
    QVector3D size = QVector3D(20, 20, 20);

    float radius = 10.0f;
    float height = 20.0f;
};

inline bool operator==(const ShapeNode &left, const ShapeNode &right)
{
    return left.id == right.id
           && left.type == right.type
           && left.booleanMode == right.booleanMode
           && left.name == right.name
           && left.position == right.position
           && left.rotation == right.rotation
           && left.size == right.size
           && left.radius == right.radius
           && left.height == right.height;
}

inline bool operator!=(const ShapeNode &left, const ShapeNode &right)
{
    return !(left == right);
}

#endif
