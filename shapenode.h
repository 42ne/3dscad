#ifndef SHAPENODE_H
#define SHAPENODE_H

#include <QString>
#include <QStringList>
#include <QVector3D>

struct ShapeNode
{
    enum Type {
        Cube,
        Sphere,
        Cylinder,
        Cone // frustum: r1 (bottom) = radius, r2 (top) = radius2; r2=0 → true cone
    };

    enum BooleanMode {
        Add,
        Subtract,
        Intersect
    };

    int id = -1;
    Type type = Cube;
    BooleanMode booleanMode = Add;
    QString name;

    QVector3D position = QVector3D(0, 0, 0);
    QVector3D rotation = QVector3D(0, 0, 0);
    QVector3D size = QVector3D(20, 20, 20);

    float radius  = 10.0f;
    float radius2 = 0.0f;  // top radius for Cone (r2); unused for other types
    float height  = 20.0f;

    // One expression string per parameter, in shapeParameterControls() order.
    // Empty list = plain numeric mode (use size/radius/height directly).
    QStringList parameterExpressions;
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
           && left.radius2 == right.radius2
           && left.height == right.height
           && left.parameterExpressions == right.parameterExpressions;
}

inline bool operator!=(const ShapeNode &left, const ShapeNode &right)
{
    return !(left == right);
}

#endif
