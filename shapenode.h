#ifndef SHAPENODE_H
#define SHAPENODE_H

#include <QString>
#include <QStringList>
#include <QVector3D>
#include <QtGlobal>

struct ShapeNode
{
    enum Type {
        Cube,
        Sphere,
        Cylinder,
        Cone,
        Circle,
        Square,
        Polygon2D,
        Polyhedron,
        Point3D,
        Face3D
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

    // Polyhedron geometry — empty for all other types.
    QVector<QVector3D>    polyhedronPoints;
    QVector<QVector<int>> polyhedronFaces;

    // One expression string per parameter, in shapeParameterControls() order.
    // Empty list = plain numeric mode (use size/radius/height directly).
    QStringList parameterExpressions;

    // center=true/false for Cube, Cylinder, Cone, Square.
    // Default true matches the toolbar/drag-drop creation behaviour;
    // when parsing existing code the value is set from the actual parameter.
    bool center = true;

    // Apply a parameter value by index, clamping to the type-appropriate minimum.
    // Pass the raw (pre-clamp) evaluated value; this method owns all clamping logic.
    // Cone R2 (index 1) may be 0.0 for a true pointed apex; every other param >= 0.1.
    void applyParameterValue(int paramIndex, qreal rawValue)
    {
        switch (type) {
        case Cube:
        case Square:
            if      (paramIndex == 0) size.setX(static_cast<float>(qMax<qreal>(0.1, rawValue)));
            else if (paramIndex == 1) size.setY(static_cast<float>(qMax<qreal>(0.1, rawValue)));
            else if (type == Cube && paramIndex == 2) size.setZ(static_cast<float>(qMax<qreal>(0.1, rawValue)));
            break;
        case Sphere:
        case Circle:
            if (paramIndex == 0) radius = static_cast<float>(qMax<qreal>(0.1, rawValue));
            break;
        case Polyhedron:
        case Polygon2D:
            break;
        case Point3D:
            if      (paramIndex == 0) position.setX(static_cast<float>(rawValue));
            else if (paramIndex == 1) position.setY(static_cast<float>(rawValue));
            else if (paramIndex == 2) position.setZ(static_cast<float>(rawValue));
            break;
        case Face3D: {
            if (polyhedronFaces.isEmpty())
                polyhedronFaces.append(QVector<int>());
            auto &face = polyhedronFaces.first();
            if (paramIndex == 0) {
                // N: vertex count (clamp to 2..32)
                const int newCount = qBound(2, static_cast<int>(qRound(rawValue)), 32);
                face.resize(newCount);
            } else {
                // V[n]: vertex index (non-negative)
                const int vi = paramIndex - 1;
                if (vi >= 0 && vi < face.size())
                    face[vi] = qMax(0, static_cast<int>(qRound(rawValue)));
            }
            break;
        }
        case Cylinder:
            if      (paramIndex == 0) radius = static_cast<float>(qMax<qreal>(0.1, rawValue));
            else if (paramIndex == 1) height = static_cast<float>(qMax<qreal>(0.1, rawValue));
            break;
        case Cone:
            if      (paramIndex == 0) radius  = static_cast<float>(qMax<qreal>(0.1, rawValue));
            else if (paramIndex == 1) radius2 = static_cast<float>(qMax<qreal>(0.0, rawValue));
            else if (paramIndex == 2) height  = static_cast<float>(qMax<qreal>(0.1, rawValue));
            break;
        }
    }

    // Returns true if toolName names a primitive shape.
    static bool isPrimitiveTool(const QString &toolName)
    {
        return toolName == QLatin1String("cube")
            || toolName == QLatin1String("sphere")
            || toolName == QLatin1String("cylinder")
            || toolName == QLatin1String("cone")
            || toolName == QLatin1String("circle")
            || toolName == QLatin1String("square")
            || toolName == QLatin1String("polygon");
    }

    // Returns the Type corresponding to toolName; falls back to Cube for unknown names.
    static Type typeFromToolName(const QString &toolName)
    {
        if (toolName == QLatin1String("sphere"))   return Sphere;
        if (toolName == QLatin1String("cylinder")) return Cylinder;
        if (toolName == QLatin1String("cone"))     return Cone;
        if (toolName == QLatin1String("circle"))   return Circle;
        if (toolName == QLatin1String("square"))   return Square;
        if (toolName == QLatin1String("polygon"))  return Polygon2D;
        return Cube;
    }
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
           && left.parameterExpressions == right.parameterExpressions
           && left.center == right.center
           && left.polyhedronPoints == right.polyhedronPoints
           && left.polyhedronFaces == right.polyhedronFaces;
}

inline bool operator!=(const ShapeNode &left, const ShapeNode &right)
{
    return !(left == right);
}

#endif
