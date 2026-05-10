#ifndef OPENSCADGENERATOR_H
#define OPENSCADGENERATOR_H

#include "scenebooleantree.h"
#include "scenedocument.h"

#include <QString>

class OpenScadGenerator
{
public:
    static QString generate(const SceneDocument &scene);

private:
    static void appendBooleanNode(QString *code, const SceneBooleanNode &node, const SceneDocument &scene, const QString &indent);
    static void appendBooleanGroup(QString *code, const QString &name, const SceneBooleanNode &node, const SceneDocument &scene, const QString &indent);
    static void appendShape(QString *code, const ShapeNode &shape, const QString &indent);
    static QString shapeToOpenScad(const ShapeNode &shape);
};

#endif
