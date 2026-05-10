#include "scenebooleantree.h"

static SceneBooleanNode primitiveNode(int shapeIndex)
{
    SceneBooleanNode node;
    node.type = SceneBooleanNode::Primitive;
    node.shapeIndex = shapeIndex;
    return node;
}

static SceneBooleanNode groupNode(SceneBooleanNode::Type type, const QVector<int> &shapeIndices)
{
    SceneBooleanNode node;
    node.type = type;

    for (int shapeIndex : shapeIndices)
        node.children.append(primitiveNode(shapeIndex));

    return node;
}

SceneBooleanNode buildSceneBooleanTree(const QVector<ShapeNode> &shapes)
{
    QVector<int> addShapes;
    QVector<int> subtractShapes;
    QVector<int> intersectShapes;

    for (int i = 0; i < shapes.size(); ++i) {
        if (shapes[i].booleanMode == ShapeNode::Subtract)
            subtractShapes.append(i);
        else if (shapes[i].booleanMode == ShapeNode::Intersect)
            intersectShapes.append(i);
        else
            addShapes.append(i);
    }

    if (shapes.isEmpty())
        return {};

    if (addShapes.isEmpty()) {
        QVector<int> allShapes;
        for (int i = 0; i < shapes.size(); ++i)
            allShapes.append(i);
        return groupNode(SceneBooleanNode::Union, allShapes);
    }

    SceneBooleanNode solid = groupNode(SceneBooleanNode::Union, addShapes);

    if (!subtractShapes.isEmpty()) {
        SceneBooleanNode difference;
        difference.type = SceneBooleanNode::Difference;
        difference.children.append(solid);
        for (int shapeIndex : subtractShapes)
            difference.children.append(primitiveNode(shapeIndex));
        solid = difference;
    }

    if (!intersectShapes.isEmpty()) {
        SceneBooleanNode intersection;
        intersection.type = SceneBooleanNode::Intersection;
        intersection.children.append(solid);
        intersection.children.append(groupNode(SceneBooleanNode::Union, intersectShapes));
        return intersection;
    }

    return solid;
}
