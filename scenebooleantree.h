#ifndef SCENEBOOLEANTREE_H
#define SCENEBOOLEANTREE_H

#include "shapenode.h"

#include <QVector>

struct SceneBooleanNode
{
    enum Type {
        Empty,
        Primitive,
        Union,
        Difference,
        Intersection
    };

    Type type = Empty;
    int shapeIndex = -1;
    QVector<SceneBooleanNode> children;
};

SceneBooleanNode buildSceneBooleanTree(const QVector<ShapeNode> &shapes);

#endif
