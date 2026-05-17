#ifndef OPENSCADGENERATOR_H
#define OPENSCADGENERATOR_H

#include "scenedocument.h"

#include <QString>
#include <QVector>

class OpenScadGenerator
{
public:
    struct SourceRange
    {
        int treeNodeId = 0;
        int start = 0;
        int length = 0;
    };

    static QString generate(const SceneDocument &scene);
    static QString generateWithSourceMap(const SceneDocument &scene, QVector<SourceRange> *ranges);

private:
    static void appendSceneModule(QString *code, const SceneDocument &scene, QVector<SourceRange> *ranges);
    static void appendTreeNode(QString *code, const SceneDocument::TreeNode &node, const SceneDocument &scene, const QString &indent, QVector<SourceRange> *ranges);
    static void appendTreeGroup(QString *code, const QString &name, const SceneDocument::TreeNode &node, const SceneDocument &scene, const QString &indent, QVector<SourceRange> *ranges);
    static void appendShape(QString *code, const ShapeNode &shape, const QString &indent);
    static QString shapeToOpenScad(const ShapeNode &shape);
};

#endif
