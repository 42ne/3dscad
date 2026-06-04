#ifndef SCENETREE_H
#define SCENETREE_H

#include <QColor>
#include <QStringList>
#include <QVector>
#include <QVector3D>
#include <QString>

/**
 * @brief Scene tree model for boolean operations
 *
 * Manages explicit tree hierarchy with groups (Union, Difference, Intersection)
 * and primitive nodes. Separate from ShapeNode list to allow independent development.
 *
 * This class is owned by SceneDocument but has isolated tree logic.
 */
class SceneTree
{
public:
    struct TreeNode
    {
        enum Type {
            Primitive,
            Group,
            Variable,
            ModuleCall // calls a named module; shapeId = referenced module group id
        };

        enum Operation {
            Module,
            Union,
            Difference,
            Intersection,
            Translate,
            Rotate,
            Scale,
            Mirror,     // reflect across plane with normal [x,y,z]; axis stored in position
            Hull,       // convex hull of children; no parameters
            Minkowski,  // Minkowski sum of children; no parameters
            Polyhedron, // polyhedron group; expects Point3D and Face3D children
            For,
            Color,
            LinearExtrude,
            RotateExtrude, // rotate_extrude(angle=360); angle stored in scale.x()
            Resize,     // resize([x,y,z]) or resize([x,y,z], auto=[x,y,z]); newsize in scale
            Scene // top-level container; flattened in code generation, never emitted as a block
        };

        int id = 0;
        Type type = Group;
        Operation operation = Union;
        int shapeId = -1;
        QString moduleName = QStringLiteral("scene_model"); // for operation == Module
        QString moduleCallArguments;                        // temporary top-level call args until ModuleCall nodes exist
        bool isParameter = false;                           // for type == Variable
        QString variableName;
        QString variableExpression = QStringLiteral("0");
        qreal variableValue = 0.0;

        // LinearExtrude parameters
        float linearExtrudeTwist = 0.0f;
        int linearExtrudeSlices = 0;       // 0 = auto (use $fn/$fa/$fs)
        float linearExtrudeScaleVal = 1.0f;
        bool linearExtrudeCenter = false;
        QVector3D position = QVector3D(0, 0, 0);
        QVector3D rotation = QVector3D(0, 0, 0);
        QVector3D scale = QVector3D(1, 1, 1);
        QStringList transformExpressions; // 3 entries (x,y,z) for Translate/Rotate/Scale
        QString loopVariable = QStringLiteral("i");
        QString loopRangeExpression = QStringLiteral("[0 : 1 : 3]");

        // Resize parameters
        QVector3D resizeAuto = QVector3D(0, 0, 0); // auto=[x,y,z], 0 means not set
        QColor color = QColor(79, 163, 255);
        QVector<TreeNode> children;
    };

    SceneTree();

    const TreeNode &root() const;
    int shapeCount() const;
    bool isEmpty() const;

    const TreeNode *nodeById(int id) const;
    TreeNode *nodeById(int id);

    int sceneNodeId() const;
    int addModuleCall(int moduleGroupId, int parentGroupId = 0, int insertIndex = -1, const QString &arguments = QString());
    bool removeModuleCallById(int moduleCallId);
    bool removeModuleCallForModule(int moduleGroupId);

    int addGroup(TreeNode::Operation operation, int parentNodeId = 0, int insertIndex = -1);
    bool removeGroupById(int groupId);
    int addVariable(const QString &name, const QString &expression = QStringLiteral("0"), qreal value = 0.0,
                    int parentGroupId = 0, bool isParameter = false, int insertIndex = -1);
    bool removeVariableById(int variableId);
    bool setModuleName(int groupId, const QString &name);
    bool renameVariable(int variableId, const QString &newName);
    bool setVariableIsParameter(int variableId, bool isParameter);
    bool moveNode(int nodeId, int parentGroupId, int insertIndex = -1, bool moduleParameterZone = false);
    bool updateGroupTransform(int groupId, const QVector3D &position, const QVector3D &rotation, const QVector3D &scale, const QStringList &transformExpressions = QStringList());
    bool updateGroupLinearExtrudeParams(int groupId, const QVector3D &scale,
                                         float twist, int slices, float scaleVal,
                                         const QStringList &transformExpressions = QStringList());
    bool updateGroupColor(int groupId, const QColor &color);

    // Primitive management
    bool addPrimitive(int shapeId, TreeNode::Operation operation, int parentGroupId = 0, int insertIndex = -1);
    bool removePrimitive(int shapeId);
    bool movePrimitiveToOperation(int shapeId, TreeNode::Operation operation);
    bool containsPrimitive(int shapeId) const;
    QVector<int> primitiveShapeIdsForNode(int nodeId) const;

    // Tree reconstruction
    void clear();
    struct Snapshot
    {
        TreeNode treeRoot;
        int nextNodeId = 1;
    };

    Snapshot snapshot() const;
    void restoreSnapshot(const Snapshot &snapshot);

private:
    // Helper methods
    TreeNode *sceneNode();
    const TreeNode *sceneNode() const;
    TreeNode *nodeById(TreeNode *node, int id);
    const TreeNode *nodeById(const TreeNode *node, int id) const;
    bool containsNodeId(const TreeNode &node, int id) const;
    bool containsPrimitiveShapeId(const TreeNode &node, int shapeId) const;
    void collectPrimitiveShapeIds(const TreeNode &node, QVector<int> *shapeIds) const;
    bool parentWorldPositionForNode(const TreeNode &node, int id, const QVector3D &worldPosition, QVector3D *parentWorldPosition) const;
    void offsetMovedNode(TreeNode *node, const QVector3D &offset);
    bool detachNodeById(TreeNode *node, int id, TreeNode *detachedNode = nullptr);
    bool removePrimitiveFromTree(TreeNode *node, int shapeId, TreeNode *removedNode = nullptr);
    bool appendPrimitiveToOperation(TreeNode::Operation operation, const TreeNode &primitiveNode);
    void pruneEmptyGroups(TreeNode *node);

    TreeNode makeGroupNode(TreeNode::Operation operation);
    TreeNode makePrimitiveNode(int shapeId);
    TreeNode makeVariableNode(const QString &name, const QString &expression, qreal value);
    TreeNode makeModuleCallNode(int moduleGroupId, const QString &moduleName, const QString &arguments = QString());

private:
    TreeNode m_root;
    int m_nextNodeId = 1;
};

#endif
