#ifndef OPENSCADGENERATOR_H
#define OPENSCADGENERATOR_H

#include "scenedocument.h"

#include <QString>

class OpenScadGenerator
{
public:
    static QString generate(const SceneDocument &scene);

private:
    static void appendTreeNode(QString *code, const SceneDocument::TreeNode &node, const SceneDocument &scene, const QString &indent);
    static void appendTreeGroup(QString *code, const QString &name, const SceneDocument::TreeNode &node, const SceneDocument &scene, const QString &indent);
    static void appendTransformPrefix(QString *code, const QVector3D &position, const QVector3D &rotation, const QString &indent);
    static void appendShape(QString *code, const ShapeNode &shape, const QString &indent);
    static QString shapeToOpenScad(const ShapeNode &shape);
};

#endif
