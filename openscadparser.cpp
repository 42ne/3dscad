#include "openscadparser.h"

#include <QRegularExpression>
#include <QStringList>

static bool parseFloat(const QString &text, float *value)
{
    bool ok = false;
    const float parsedValue = text.trimmed().toFloat(&ok);

    if (!ok)
        return false;

    *value = parsedValue;
    return true;
}

bool OpenScadParser::parse(const QString &code, QVector<ShapeNode> *shapes, QString *errorMessage)
{
    if (!shapes)
        return false;

    QVector<ShapeNode> parsedShapes;
    QVector3D pendingPosition;
    QVector3D pendingRotation;
    bool hasTranslate = false;
    bool hasRotate = false;
    bool inDifference = false;
    bool inDifferenceBaseUnion = false;
    bool inIntersection = false;
    bool intersectionBaseClosed = false;
    bool inIntersectionCutterUnion = false;
    ShapeNode::BooleanMode currentBooleanMode = ShapeNode::Add;
    int cubeCount = 0;
    int sphereCount = 0;
    int cylinderCount = 0;

    const QRegularExpression translateRegex("^translate\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*$");
    const QRegularExpression rotateRegex("^rotate\\s*\\(\\s*\\[([^\\]]+)\\]\\s*\\)\\s*$");
    const QRegularExpression cubeRegex("^cube\\s*\\(\\s*\\[([^\\]]+)\\]\\s*,\\s*center\\s*=\\s*true\\s*\\)\\s*;\\s*$");
    const QRegularExpression sphereRegex("^sphere\\s*\\(\\s*r\\s*=\\s*([^\\)]+)\\)\\s*;\\s*$");
    const QRegularExpression cylinderRegex("^cylinder\\s*\\(\\s*h\\s*=\\s*([^,]+)\\s*,\\s*r\\s*=\\s*([^,\\)]+)\\s*,\\s*center\\s*=\\s*true\\s*\\)\\s*;\\s*$");

    const QStringList lines = code.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();

        if (line.isEmpty() || line.startsWith("//"))
            continue;

        if (line == "difference() {") {
            inDifference = true;
            currentBooleanMode = ShapeNode::Add;
            continue;
        }

        if (line == "intersection() {") {
            inIntersection = true;
            intersectionBaseClosed = false;
            currentBooleanMode = ShapeNode::Add;
            continue;
        }

        if (line == "union() {") {
            if (inDifference)
                inDifferenceBaseUnion = true;
            else if (inIntersection && intersectionBaseClosed) {
                inIntersectionCutterUnion = true;
                currentBooleanMode = ShapeNode::Intersect;
            }
            continue;
        }

        if (line == "}") {
            if (inIntersectionCutterUnion) {
                inIntersectionCutterUnion = false;
                currentBooleanMode = ShapeNode::Add;
            } else if (inDifferenceBaseUnion) {
                inDifferenceBaseUnion = false;
                currentBooleanMode = ShapeNode::Subtract;
            } else if (inDifference) {
                inDifference = false;
                if (inIntersection) {
                    intersectionBaseClosed = true;
                    currentBooleanMode = ShapeNode::Intersect;
                } else {
                    currentBooleanMode = ShapeNode::Add;
                }
            } else if (inIntersection && !intersectionBaseClosed) {
                intersectionBaseClosed = true;
                currentBooleanMode = ShapeNode::Intersect;
            } else if (inIntersection) {
                inIntersection = false;
                currentBooleanMode = ShapeNode::Add;
            }

            continue;
        }

        QRegularExpressionMatch match = translateRegex.match(line);
        if (match.hasMatch()) {
            if (!parseVector3(match.captured(1), &pendingPosition)) {
                if (errorMessage)
                    *errorMessage = QString("Invalid translate vector on line %1.").arg(i + 1);
                return false;
            }

            hasTranslate = true;
            continue;
        }

        match = rotateRegex.match(line);
        if (match.hasMatch()) {
            if (!hasTranslate) {
                if (errorMessage)
                    *errorMessage = QString("rotate() before translate() on line %1.").arg(i + 1);
                return false;
            }

            if (!parseVector3(match.captured(1), &pendingRotation)) {
                if (errorMessage)
                    *errorMessage = QString("Invalid rotate vector on line %1.").arg(i + 1);
                return false;
            }

            hasRotate = true;
            continue;
        }

        ShapeNode shape;
        shape.position = pendingPosition;
        shape.rotation = pendingRotation;
        shape.booleanMode = currentBooleanMode;

        match = cubeRegex.match(line);
        if (match.hasMatch()) {
            if (!hasTranslate || !hasRotate) {
                if (errorMessage)
                    *errorMessage = QString("cube() without translate()/rotate() on line %1.").arg(i + 1);
                return false;
            }

            shape.type = ShapeNode::Cube;
            shape.name = QString("Cube %1").arg(++cubeCount);

            if (!parseVector3(match.captured(1), &shape.size)) {
                if (errorMessage)
                    *errorMessage = QString("Invalid cube size on line %1.").arg(i + 1);
                return false;
            }

            parsedShapes.append(shape);
            hasTranslate = false;
            hasRotate = false;
            continue;
        }

        match = sphereRegex.match(line);
        if (match.hasMatch()) {
            if (!hasTranslate || !hasRotate) {
                if (errorMessage)
                    *errorMessage = QString("sphere() without translate()/rotate() on line %1.").arg(i + 1);
                return false;
            }

            shape.type = ShapeNode::Sphere;
            shape.name = QString("Sphere %1").arg(++sphereCount);

            if (!parseFloat(match.captured(1), &shape.radius)) {
                if (errorMessage)
                    *errorMessage = QString("Invalid sphere radius on line %1.").arg(i + 1);
                return false;
            }

            parsedShapes.append(shape);
            hasTranslate = false;
            hasRotate = false;
            continue;
        }

        match = cylinderRegex.match(line);
        if (match.hasMatch()) {
            if (!hasTranslate || !hasRotate) {
                if (errorMessage)
                    *errorMessage = QString("cylinder() without translate()/rotate() on line %1.").arg(i + 1);
                return false;
            }

            shape.type = ShapeNode::Cylinder;
            shape.name = QString("Cylinder %1").arg(++cylinderCount);

            if (!parseFloat(match.captured(1), &shape.height) || !parseFloat(match.captured(2), &shape.radius)) {
                if (errorMessage)
                    *errorMessage = QString("Invalid cylinder parameters on line %1.").arg(i + 1);
                return false;
            }

            parsedShapes.append(shape);
            hasTranslate = false;
            hasRotate = false;
            continue;
        }

        if (errorMessage)
            *errorMessage = QString("Unsupported OpenSCAD syntax on line %1: %2").arg(i + 1).arg(line);
        return false;
    }

    if (hasTranslate || hasRotate) {
        if (errorMessage)
            *errorMessage = "Incomplete transform without a primitive.";
        return false;
    }

    *shapes = parsedShapes;
    return true;
}

bool OpenScadParser::parseVector3(const QString &text, QVector3D *vector)
{
    const QStringList parts = text.split(',');
    if (parts.size() != 3 || !vector)
        return false;

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    if (!parseFloat(parts[0], &x) || !parseFloat(parts[1], &y) || !parseFloat(parts[2], &z))
        return false;

    *vector = QVector3D(x, y, z);
    return true;
}
