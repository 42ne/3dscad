#include "scenetreecanvasgraphics.h"
#include "scenetreetoolmetadata.h"

#include <QFontMetricsF>
#include <QtGlobal>

namespace SceneTreeGraphics {

QString toolbarToolTip(const QString &tool)
{
    if (tool == QStringLiteral("module")) {
        return QStringLiteral(
            "Module declaration\n"
            "Drop at the scene level. Add variables to its parameters lane or body, "
            "then drag the module call chip into the scene, groups, loops, or module bodies.");
    }

    if (isVariableToolName(tool)) {
        return QStringLiteral(
            "Variable / parameter\n"
            "Drop at the scene level for a global variable, or into a module's parameters lane or body.");
    }

    if (tool == QStringLiteral("polyhedron")) {
        return QStringLiteral(
            "Polyhedron group\n"
            "A container that builds a polyhedron from the point and face table inside it.");
    }

    if (ShapeNode::isPrimitiveTool(tool)) {
        const QString name = tool.left(1).toUpper() + tool.mid(1);
        return QStringLiteral(
            "%1 primitive\n"
            "Drop into the scene, groups, loops, transforms, or module bodies.").arg(name);
    }

    SceneDocument::TreeNode::Operation operation = SceneDocument::TreeNode::Union;
    if (operationForToolName(tool, &operation)) {
        if (isTransformOperation(operation)) {
            return QStringLiteral(
                "%1 transform\n"
                "Drop into the scene, groups, loops, or module bodies; then drop child objects inside it.")
                .arg(tool.left(1).toUpper() + tool.mid(1));
        }
        if (operation == SceneDocument::TreeNode::For) {
            return QStringLiteral(
                "For loop\n"
                "Drop into the scene, groups, transforms, or module bodies; then drop repeated objects inside it.");
        }
        if (operation == SceneDocument::TreeNode::Color) {
            return QStringLiteral(
                "Color group\n"
                "Drop into the scene, groups, loops, transforms, or module bodies; then drop objects inside it.");
        }
        return QStringLiteral(
            "%1 group\n"
            "Drop into the scene, groups, loops, transforms, or module bodies; then drop child objects inside it.")
            .arg(tool.left(1).toUpper() + tool.mid(1));
    }

    return QStringLiteral("Tool\nDrop into a highlighted slot in the scene tree.");
}

bool isVariableToolName(const QString &tool)
{
    const QString normalized = tool.toLower();
    return normalized == QStringLiteral("var")
           || normalized == QStringLiteral("variable")
           || normalized == QStringLiteral("par")
           || normalized == QStringLiteral("parameter");
}

ShapeNode::Type primitiveTypeForTool(const QString &tool)
{
    const QString normalized = tool.toLower();
    if (normalized.contains("circle"))
        return ShapeNode::Circle;
    if (normalized.contains("square"))
        return ShapeNode::Square;
    if (normalized.contains("polygon"))
        return ShapeNode::Polygon2D;
    if (normalized.contains("sphere"))
        return ShapeNode::Sphere;
    if (normalized.contains("cone"))
        return ShapeNode::Cone;
    if (normalized.contains("cylinder"))
        return ShapeNode::Cylinder;
    if (normalized.contains("polyhedron"))
        return ShapeNode::Polyhedron;
    if (normalized == "point_3d")
        return ShapeNode::Point3D;
    if (normalized == "face_3d")
        return ShapeNode::Face3D;
    return ShapeNode::Cube;
}

QString toolNameForPrimitiveType(ShapeNode::Type type)
{
    if (type == ShapeNode::Circle)
        return QStringLiteral("circle");
    if (type == ShapeNode::Square)
        return QStringLiteral("square");
    if (type == ShapeNode::Polygon2D)
        return QStringLiteral("polygon");
    if (type == ShapeNode::Sphere)
        return QStringLiteral("sphere");
    if (type == ShapeNode::Cylinder)
        return QStringLiteral("cylinder");
    if (type == ShapeNode::Cone)
        return QStringLiteral("cone");
    if (type == ShapeNode::Polyhedron)
        return QStringLiteral("polyhedron");
    if (type == ShapeNode::Point3D)
        return QStringLiteral("point_3d");
    if (type == ShapeNode::Face3D)
        return QStringLiteral("face_3d");
    return QStringLiteral("cube");
}

bool operationForToolName(const QString &tool, SceneDocument::TreeNode::Operation *operation)
{
    if (!operation)
        return false;

    const QString normalized = tool.toLower();
    if (normalized.contains("union")) {
        *operation = SceneDocument::TreeNode::Union;
        return true;
    }
    if (normalized.contains("difference")) {
        *operation = SceneDocument::TreeNode::Difference;
        return true;
    }
    if (normalized.contains("intersection")) {
        *operation = SceneDocument::TreeNode::Intersection;
        return true;
    }
    if (normalized.contains("module")) {
        *operation = SceneDocument::TreeNode::Module;
        return true;
    }
    if (normalized.contains("translate")) {
        *operation = SceneDocument::TreeNode::Translate;
        return true;
    }
    if (normalized == QStringLiteral("rotate_extrude")) {
        *operation = SceneDocument::TreeNode::RotateExtrude;
        return true;
    }
    if (normalized.contains("rotate")) {
        *operation = SceneDocument::TreeNode::Rotate;
        return true;
    }
    if (normalized.contains("scale")) {
        *operation = SceneDocument::TreeNode::Scale;
        return true;
    }
    if (normalized.contains("mirror")) {
        *operation = SceneDocument::TreeNode::Mirror;
        return true;
    }
    if (normalized.contains("hull")) {
        *operation = SceneDocument::TreeNode::Hull;
        return true;
    }
    if (normalized.contains("minkowski")) {
        *operation = SceneDocument::TreeNode::Minkowski;
        return true;
    }
    if (normalized == QStringLiteral("polyhedron")) {
        *operation = SceneDocument::TreeNode::Polyhedron;
        return true;
    }
    if (normalized == QStringLiteral("linear_extrude") || normalized == QStringLiteral("extrude")) {
        *operation = SceneDocument::TreeNode::LinearExtrude;
        return true;
    }
    if (normalized == QStringLiteral("resize")) {
        *operation = SceneDocument::TreeNode::Resize;
        return true;
    }
    if (normalized == QStringLiteral("for")) {
        *operation = SceneDocument::TreeNode::For;
        return true;
    }
    if (normalized == QStringLiteral("color") || normalized == QStringLiteral("colour")) {
        *operation = SceneDocument::TreeNode::Color;
        return true;
    }
    return false;
}

namespace {

const OperationVisual OperationVisuals[] = {
    {SceneDocument::TreeNode::Union, "union", QColor(216, 237, 226), GroupMinWidth},
    {SceneDocument::TreeNode::Difference, "difference", QColor(247, 224, 204), GroupWideMinWidth},
    {SceneDocument::TreeNode::Intersection, "intersection", QColor(226, 220, 247), GroupWideMinWidth},
    {SceneDocument::TreeNode::Module, "module", QColor(230, 232, 236), GroupModuleMinWidth},
    {SceneDocument::TreeNode::Translate, "translate", QColor(218, 238, 246), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
    {SceneDocument::TreeNode::Rotate, "rotate", QColor(239, 229, 247), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
    {SceneDocument::TreeNode::Scale, "scale", QColor(229, 241, 218), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
    {SceneDocument::TreeNode::Mirror, "mirror", QColor(242, 218, 235), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
    {SceneDocument::TreeNode::Hull, "hull", QColor(218, 240, 218), GroupMinWidth},
    {SceneDocument::TreeNode::Minkowski, "minkowski", QColor(227, 235, 248), GroupMinWidth},
    {SceneDocument::TreeNode::Polyhedron, "polyhedron", QColor(218, 238, 228), 300.0},
    {SceneDocument::TreeNode::LinearExtrude, "linear_extrude", QColor(222, 238, 232), GroupWideMinWidth},
    {SceneDocument::TreeNode::Resize, "resize", QColor(218, 238, 218), GroupWideMinWidth},
    {SceneDocument::TreeNode::RotateExtrude, "rotate_extrude", QColor(225, 235, 245), GroupWideMinWidth},
    {SceneDocument::TreeNode::For, "for", QColor(236, 232, 205), GroupWideMinWidth},
    {SceneDocument::TreeNode::Color, "color", QColor(218, 234, 248), TransformHeaderWidth + GroupPadding * 2.0 + PrimitiveWidth},
    {SceneDocument::TreeNode::Scene, "scene", QColor(210, 215, 225), GroupMinWidth},
};

} // namespace

const OperationVisual &operationVisual(SceneDocument::TreeNode::Operation operation)
{
    for (const OperationVisual &visual : OperationVisuals) {
        if (visual.operation == operation)
            return visual;
    }
    return OperationVisuals[0];
}

qreal minimumWidthForOperation(SceneDocument::TreeNode::Operation operation)
{
    const qreal hardMin = operationVisual(operation).minWidth;
    if (isVerticalHeaderOperation(operation))
        return hardMin;

    // For horizontal-header cards ensure the label fits.
    // Header geometry: grip+gap(30) + icon(24) + gap(10) = 64 from left to label;
    // chevron+gap(28) reserved on the right.
    const QFontMetricsF fm(sceneTreeGraphicsFont());
    const qreal labelW = fm.horizontalAdvance(
        QString::fromLatin1(operationVisual(operation).toolName));
    return qMax(hardMin, 64.0 + labelW + 28.0 + 6.0);
}

QString labelForOperation(SceneDocument::TreeNode::Operation operation)
{
    return QString::fromLatin1(operationVisual(operation).toolName);
}

bool isTransformOperation(SceneDocument::TreeNode::Operation operation)
{
    return operation == SceneDocument::TreeNode::Translate
           || operation == SceneDocument::TreeNode::Rotate
           || operation == SceneDocument::TreeNode::Scale
           || operation == SceneDocument::TreeNode::Mirror;
}

bool isVerticalHeaderOperation(SceneDocument::TreeNode::Operation operation)
{
    return isTransformOperation(operation)
           || operation == SceneDocument::TreeNode::Color;
}

QColor fillForTool(const QString &tool)
{
    if (isVariableToolName(tool))
        return QColor(246, 236, 196);

    SceneDocument::TreeNode::Operation operation;
    if (operationForToolName(tool, &operation))
        return operationVisual(operation).fill;
    return QColor(219, 231, 246);
}


QColor fillForOperation(SceneDocument::TreeNode::Operation operation)
{
    return operationVisual(operation).fill;
}

} // namespace SceneTreeGraphics
