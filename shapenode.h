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
        Face3D,
        Text,
        ImportedMesh
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

    // Text parameters (type == Text). textSize is the font size (OpenSCAD size=).
    QString textValue = QStringLiteral("Text");
    QString fontName;                               // empty = default font
    QString textHalign = QStringLiteral("left");    // left | center | right
    QString textValign = QStringLiteral("baseline");// baseline | bottom | center | top
    float   textSize = 10.0f;
    float   textSpacing = 1.0f;
    // Cached glyph contours (model space, z=0), computed on the main thread by
    // SceneDocument::snapshot(). The geometry backends read this instead of
    // calling QFont/QPainterPath off the GUI thread. Derived — not in operator==.
    QVector<QVector<QVector3D>> textContours;

    // Imported mesh (type == ImportedMesh). File path and parsed geometry.
    QString importFilePath;

    // One expression string per parameter, in shapeParameterControls() order.
    // Empty list = plain numeric mode (use size/radius/height directly).
    QStringList parameterExpressions;

    // center=true/false for Cube, Cylinder, Cone, Square.
    // Default false matches OpenSCAD behaviour (origin at corner);
    // when parsing existing code the value is set from the actual parameter.
    bool center = false;

    // Radius parameter notation: false = r= (default), true = d= (diameter).
    // Set from code when parsing; toolbar-created shapes always use r= notation.
    bool usesDiameter = false;  // sphere / circle / cylinder: r vs d
    bool usesD1 = false;        // cone: r1 vs d1
    bool usesD2 = false;        // cone: r2 vs d2

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
            if (paramIndex == 0) {
                const qreal v = usesDiameter ? rawValue / 2.0 : rawValue;
                radius = static_cast<float>(qMax<qreal>(0.1, v));
            }
            break;
        case Polyhedron:
        case Polygon2D:
        case ImportedMesh:
            break;
        case Text:
            if      (paramIndex == 0) textSize = static_cast<float>(qMax<qreal>(0.1, rawValue));
            else if (paramIndex == 1) textSpacing = static_cast<float>(qMax<qreal>(0.0, rawValue));
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
            if (paramIndex == 0) {
                const qreal v = usesDiameter ? rawValue / 2.0 : rawValue;
                radius = static_cast<float>(qMax<qreal>(0.1, v));
            } else if (paramIndex == 1) height = static_cast<float>(qMax<qreal>(0.1, rawValue));
            break;
        case Cone:
            if (paramIndex == 0) {
                const qreal v = usesD1 ? rawValue / 2.0 : rawValue;
                radius = static_cast<float>(qMax<qreal>(0.1, v));
            } else if (paramIndex == 1) {
                const qreal v = usesD2 ? rawValue / 2.0 : rawValue;
                radius2 = static_cast<float>(qMax<qreal>(0.0, v));
            } else if (paramIndex == 2) height = static_cast<float>(qMax<qreal>(0.1, rawValue));
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
            || toolName == QLatin1String("polygon")
            || toolName == QLatin1String("text")
            || toolName == QLatin1String("import");
    }

    // Returns the Type corresponding to toolName; falls back to Cube for unknown names.
    static Type typeFromToolName(const QString &toolName)
    {
        if (toolName == QLatin1String("sphere"))     return Sphere;
        if (toolName == QLatin1String("cylinder"))   return Cylinder;
        if (toolName == QLatin1String("cone"))       return Cone;
        if (toolName == QLatin1String("circle"))     return Circle;
        if (toolName == QLatin1String("square"))     return Square;
        if (toolName == QLatin1String("polygon"))    return Polygon2D;
        if (toolName == QLatin1String("text"))       return Text;
        if (toolName == QLatin1String("import"))     return ImportedMesh;
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
           && left.usesDiameter == right.usesDiameter
           && left.usesD1 == right.usesD1
           && left.usesD2 == right.usesD2
           && left.polyhedronPoints == right.polyhedronPoints
           && left.polyhedronFaces == right.polyhedronFaces
           && left.textValue == right.textValue
           && left.fontName == right.fontName
           && left.textHalign == right.textHalign
           && left.textValign == right.textValign
           && left.textSize == right.textSize
           && left.textSpacing == right.textSpacing
           && left.importFilePath == right.importFilePath;
}

inline bool operator!=(const ShapeNode &left, const ShapeNode &right)
{
    return !(left == right);
}

#endif
