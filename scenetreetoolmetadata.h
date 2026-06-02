#ifndef SCENETREETOOLMETADATA_H
#define SCENETREETOOLMETADATA_H

#include "scenedocument.h"
#include "scenetreegraphicsconstants.h"
#include "shapenode.h"

#include <QColor>
#include <QString>

namespace SceneTreeGraphics {

struct OperationVisual {
    SceneDocument::TreeNode::Operation operation;
    const char *toolName;
    QColor fill;
    qreal minWidth;
};

QString toolbarToolTip(const QString &tool);
bool isVariableToolName(const QString &tool);
ShapeNode::Type primitiveTypeForTool(const QString &tool);
QString toolNameForPrimitiveType(ShapeNode::Type type);
bool operationForToolName(const QString &tool, SceneDocument::TreeNode::Operation *operation);
bool isTransformOperation(SceneDocument::TreeNode::Operation operation);
bool isVerticalHeaderOperation(SceneDocument::TreeNode::Operation operation);
const OperationVisual &operationVisual(SceneDocument::TreeNode::Operation operation);
qreal minimumWidthForOperation(SceneDocument::TreeNode::Operation operation);
QString labelForOperation(SceneDocument::TreeNode::Operation operation);
QColor fillForTool(const QString &tool);
QColor fillForOperation(SceneDocument::TreeNode::Operation operation);

} // namespace SceneTreeGraphics

#endif
