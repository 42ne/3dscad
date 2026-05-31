#include "scenedocument.h"
#include "expression.h"
#include "scenestringutils.h"

#include <QHash>
#include <QSet>
#include <QStringList>

namespace {

void collectVariableNames(const SceneDocument::TreeNode &node, QSet<QString> *names)
{
    if (!names)
        return;

    if (node.type == SceneDocument::TreeNode::Variable) {
        names->insert(node.variableName);
        return;
    }

    for (const SceneDocument::TreeNode &child : node.children)
        collectVariableNames(child, names);
}

// Recursively collects all Variable node values from the entire tree (global + module params).
void collectAllVariableValues(const SceneDocument::TreeNode &node, QHash<QString, qreal> *values)
{
    if (!values)
        return;
    if (node.type == SceneDocument::TreeNode::Variable) {
        (*values)[node.variableName] = node.variableValue;
        return;
    }
    for (const SceneDocument::TreeNode &child : node.children)
        collectAllVariableValues(child, values);
}

bool isValidIdentifier(const QString &name)
{
    if (name.isEmpty())
        return false;

    const QChar first = name.front();
    if (!(first == QLatin1Char('_') || first.isLetter()))
        return false;

    for (const QChar ch : name) {
        if (!(ch == QLatin1Char('_') || ch.isLetterOrNumber()))
            return false;
    }

    return true;
}

QString buildNamedArgumentExpressions(const QHash<QString, QString> &arguments, const QStringList &order)
{
    QStringList parts;
    QSet<QString> emitted;
    for (const QString &name : order) {
        const QString expression = arguments.value(name).trimmed();
        if (!expression.isEmpty()) {
            parts.append(QStringLiteral("%1 = %2").arg(name, expression));
            emitted.insert(name);
        }
    }
    for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it) {
        if (emitted.contains(it.key()))
            continue;

        const QString expression = it.value().trimmed();
        if (!expression.isEmpty())
            parts.append(QStringLiteral("%1 = %2").arg(it.key(), expression));
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

const QVector<ShapeNode> &SceneDocument::shapes() const
{
    return m_shapes;
}

const SceneDocument::TreeNode &SceneDocument::treeRoot() const
{
    return m_tree.root();
}

int SceneDocument::shapeCount() const
{
    return m_shapes.size();
}

bool SceneDocument::isEmpty() const
{
    return m_tree.isEmpty();
}

int SceneDocument::sceneNodeId() const
{
    return m_tree.sceneNodeId();
}

int SceneDocument::selectedIndex() const
{
    return indexOfShapeId(m_selectedShapeId);
}

int SceneDocument::selectedShapeId() const
{
    return m_selectedShapeId;
}

void SceneDocument::setSelectedIndex(int index)
{
    const ShapeNode *shape = shapeAt(index);
    setSelectedShapeId(shape ? shape->id : -1);
}

void SceneDocument::setSelectedShapeId(int id)
{
    m_selectedShapeId = shapeById(id) ? id : -1;
}

bool SceneDocument::hasSelection() const
{
    return selectedShape() != nullptr;
}

const ShapeNode *SceneDocument::selectedShape() const
{
    return shapeById(m_selectedShapeId);
}

ShapeNode *SceneDocument::selectedShape()
{
    return shapeById(m_selectedShapeId);
}

const ShapeNode *SceneDocument::shapeAt(int index) const
{
    if (!isValidIndex(index))
        return nullptr;

    return &m_shapes[index];
}

ShapeNode *SceneDocument::shapeAt(int index)
{
    if (!isValidIndex(index))
        return nullptr;

    return &m_shapes[index];
}

const ShapeNode *SceneDocument::shapeById(int id) const
{
    const int index = indexOfShapeId(id);
    return shapeAt(index);
}

ShapeNode *SceneDocument::shapeById(int id)
{
    const int index = indexOfShapeId(id);
    return shapeAt(index);
}

int SceneDocument::indexOfShapeId(int id) const
{
    for (int i = 0; i < m_shapes.size(); ++i) {
        if (m_shapes[i].id == id)
            return i;
    }

    return -1;
}

const SceneDocument::TreeNode *SceneDocument::treeNodeById(int id) const
{
    return m_tree.nodeById(id);
}

SceneDocument::TreeNode *SceneDocument::treeNodeById(int id)
{
    return m_tree.nodeById(id);
}

int SceneDocument::addShape(const ShapeNode &shape, int parentGroupId, int treeInsertIndex)
{
    return insertShape(m_shapes.size(), shape, parentGroupId, treeInsertIndex);
}

int SceneDocument::insertShape(int index, const ShapeNode &shape, int parentGroupId, int treeInsertIndex)
{
    ShapeNode shapeWithId = shape;

    if (shapeWithId.id < 0)
        shapeWithId.id = m_nextShapeId++;
    else
        m_nextShapeId = qMax(m_nextShapeId, shapeWithId.id + 1);

    const int insertIndex = qBound(0, index, m_shapes.size());
    m_shapes.insert(insertIndex, shapeWithId);
    m_selectedShapeId = shapeWithId.id;

    if (!m_tree.addPrimitive(shapeWithId.id, operationForBooleanMode(shapeWithId.booleanMode), parentGroupId, treeInsertIndex))
        rebuildTreeFromShapes();
    else if (parentGroupId > 0)
        synchronizeBooleanModesFromTree();

    return shapeWithId.id;
}

void SceneDocument::replaceShapes(const QVector<ShapeNode> &shapes)
{
    m_shapes.clear();
    m_selectedShapeId = -1;
    m_nextShapeId = 1;

    for (ShapeNode shape : shapes) {
        if (shape.id < 0)
            shape.id = m_nextShapeId++;
        else
            m_nextShapeId = qMax(m_nextShapeId, shape.id + 1);

        m_shapes.append(shape);
    }

    rebuildTreeFromShapes();

    if (!m_shapes.isEmpty())
        m_selectedShapeId = m_shapes.first().id;
}

SceneDocument::Snapshot SceneDocument::snapshot() const
{
    Snapshot snapshot;
    snapshot.shapes = m_shapes;
    snapshot.treeRoot = m_tree.root();
    snapshot.treeSnapshot = m_tree.snapshot();
    snapshot.selectedShapeId = m_selectedShapeId;
    snapshot.nextShapeId = m_nextShapeId;
    snapshot.nextTreeNodeId = snapshot.treeSnapshot.nextNodeId;
    return snapshot;
}

void SceneDocument::restoreSnapshot(const Snapshot &snapshot)
{
    m_shapes = snapshot.shapes;
    m_tree.restoreSnapshot(snapshot.treeSnapshot.treeRoot.id > 0
                               ? snapshot.treeSnapshot
                               : SceneTree::Snapshot{snapshot.treeRoot, snapshot.nextTreeNodeId});
    m_nextShapeId = snapshot.nextShapeId;
    setSelectedShapeId(snapshot.selectedShapeId);
}

bool SceneDocument::updateShape(const ShapeNode &shape)
{
    ShapeNode *existingShape = shapeById(shape.id);
    if (!existingShape)
        return false;

    const ShapeNode::BooleanMode oldBooleanMode = existingShape->booleanMode;
    *existingShape = shape;
    m_selectedShapeId = shape.id;

    if (oldBooleanMode != shape.booleanMode)
        m_tree.movePrimitiveToOperation(shape.id, operationForBooleanMode(shape.booleanMode));

    return true;
}

bool SceneDocument::takeShapeById(int id, ShapeNode *removedShape, int *removedIndex)
{
    const int index = indexOfShapeId(id);
    if (!isValidIndex(index))
        return false;

    if (removedShape)
        *removedShape = m_shapes[index];

    if (removedIndex)
        *removedIndex = index;

    const bool wasSelected = m_shapes[index].id == m_selectedShapeId;
    m_shapes.removeAt(index);
    m_tree.removePrimitive(id);

    if (wasSelected) {
        if (m_shapes.isEmpty()) {
            m_selectedShapeId = -1;
        } else {
            const int nextIndex = qMin(index, m_shapes.size() - 1);
            m_selectedShapeId = m_shapes[nextIndex].id;
        }
    }

    return true;
}

bool SceneDocument::removeShapeById(int id)
{
    return takeShapeById(id, nullptr, nullptr);
}

bool SceneDocument::removeSelectedShape()
{
    return removeShapeById(m_selectedShapeId);
}

int SceneDocument::addGroup(TreeNode::Operation operation, int parentGroupId, int insertIndex)
{
    return m_tree.addGroup(operation, parentGroupId, insertIndex);
}

bool SceneDocument::removeGroupById(int groupId)
{
    const bool removed = m_tree.removeGroupById(groupId);
    if (removed) {
        ensureTreeContainsAllShapes();
        synchronizeBooleanModesFromTree();
    }

    return removed;
}

int SceneDocument::addModuleCall(int moduleGroupId, int parentGroupId, int insertIndex, const QString &arguments)
{
    return m_tree.addModuleCall(moduleGroupId, parentGroupId, insertIndex, arguments);
}

bool SceneDocument::removeModuleCallById(int moduleCallId)
{
    return m_tree.removeModuleCallById(moduleCallId);
}

int SceneDocument::addVariable(int insertIndex)
{
    return m_tree.addVariable(uniqueVariableName(), QStringLiteral("0"), 0.0, 0, false, insertIndex);
}

int SceneDocument::addVariableToModule(int moduleGroupId, bool isParameter, int insertIndex)
{
    const QString name = isParameter ? uniqueParameterName() : uniqueVariableName();
    return m_tree.addVariable(name, QStringLiteral("0"), 0.0,
                              moduleGroupId, isParameter, insertIndex);
}

bool SceneDocument::setModuleName(int groupId, const QString &name)
{
    return m_tree.setModuleName(groupId, name);
}

bool SceneDocument::renameVariable(int variableId, const QString &newName)
{
    const TreeNode *existing = m_tree.nodeById(variableId);
    if (!existing || existing->type != TreeNode::Variable)
        return false;

    const QString trimmed = newName.trimmed();
    if (!isValidIdentifier(trimmed))
        return false;

    // Enforce uniqueness across the whole tree.
    QSet<QString> names;
    collectVariableNames(m_tree.root(), &names);
    names.remove(existing->variableName);
    if (names.contains(trimmed))
        return false;

    return m_tree.renameVariable(variableId, trimmed);
}

bool SceneDocument::removeVariableById(int variableId)
{
    return m_tree.removeVariableById(variableId);
}

bool SceneDocument::updateVariableExpression(int variableId, const QString &expression)
{
    TreeNode *node = m_tree.nodeById(variableId);
    if (!node || node->type != TreeNode::Variable)
        return false;

    const QString trimmed = expression.trimmed();
    if (trimmed.isEmpty())
        return false;

    // Build variable context excluding self to prevent trivial self-reference.
    QHash<QString, qreal> varValues;
    for (const TreeNode &child : m_tree.root().children) {
        if (child.type == TreeNode::Variable && child.id != variableId)
            varValues[child.variableName] = child.variableValue;
    }

    qreal value = 0.0;
    if (!ExpressionSyntax::evaluate(trimmed, varValues, &value))
        return false;

    if (node->variableExpression == trimmed && node->variableValue == value)
        return false;

    node->variableExpression = trimmed;
    node->variableValue = value;

    reEvaluateDependentVariables(variableId);
    reEvaluateDependentExpressions();

    return true;
}

bool SceneDocument::updateModuleCallArgument(int moduleCallId, const QString &parameterName, const QString &expression)
{
    TreeNode *callNode = m_tree.nodeById(moduleCallId);
    if (!callNode || callNode->type != TreeNode::ModuleCall)
        return false;

    const TreeNode *moduleNode = m_tree.nodeById(callNode->shapeId);
    if (!moduleNode || moduleNode->type != TreeNode::Group || moduleNode->operation != TreeNode::Module)
        return false;

    const QString name = parameterName.trimmed();
    const QString trimmed = expression.trimmed();
    if (!isValidIdentifier(name) || trimmed.isEmpty())
        return false;

    QString expressionError;
    if (!ExpressionSyntax::validate(trimmed, &expressionError))
        return false;

    QStringList parameterOrder;
    bool knownParameter = false;
    for (const TreeNode &child : moduleNode->children) {
        if (child.type == TreeNode::Variable && child.isParameter) {
            parameterOrder.append(child.variableName);
            if (child.variableName == name)
                knownParameter = true;
        }
    }
    if (!knownParameter)
        return false;

    QHash<QString, QString> arguments = parseNamedArgumentExpressions(callNode->moduleCallArguments);
    if (arguments.value(name) == trimmed)
        return false;

    arguments[name] = trimmed;
    callNode->moduleCallArguments = buildNamedArgumentExpressions(arguments, parameterOrder);
    return true;
}

bool SceneDocument::updateForLoop(int groupId, const QString &loopVariable, const QString &rangeExpression)
{
    TreeNode *node = m_tree.nodeById(groupId);
    if (!node || node->type != TreeNode::Group || node->operation != TreeNode::For)
        return false;

    const QString variableName = loopVariable.trimmed().isEmpty()
                                     ? QStringLiteral("i")
                                     : loopVariable.trimmed();
    const QString range = rangeExpression.trimmed().isEmpty()
                              ? QStringLiteral("[0 : 1 : 3]")
                              : rangeExpression.trimmed();
    if (!isValidIdentifier(variableName))
        return false;
    if (!range.startsWith(QLatin1Char('[')) || !range.endsWith(QLatin1Char(']')))
        return false;

    if (node->loopVariable == variableName && node->loopRangeExpression == range)
        return false;

    node->loopVariable = variableName;
    node->loopRangeExpression = range;
    return true;
}

void SceneDocument::reEvaluateDependentVariables(int changedId)
{
    // Collect variable ids in tree order so upstream definitions propagate forward.
    QVector<int> varIds;
    for (const TreeNode &child : m_tree.root().children) {
        if (child.type == TreeNode::Variable)
            varIds.append(child.id);
    }

    for (int varId : varIds) {
        if (varId == changedId)
            continue;

        TreeNode *node = m_tree.nodeById(varId);
        if (!node)
            continue;

        // Build context from the current (progressively updated) values, excluding self.
        QHash<QString, qreal> varValues;
        for (const TreeNode &child : m_tree.root().children) {
            if (child.type == TreeNode::Variable && child.id != varId)
                varValues[child.variableName] = child.variableValue;
        }

        qreal value = 0.0;
        if (ExpressionSyntax::evaluate(node->variableExpression, varValues, &value))
            node->variableValue = value;
        // If evaluation fails (e.g. unresolved reference), keep the previous value.
    }
}

void SceneDocument::reEvaluateDependentExpressions()
{
    // Collect variable values from the entire tree (global scope + module parameters).
    QHash<QString, qreal> varValues;
    collectAllVariableValues(m_tree.root(), &varValues);

    for (ShapeNode &shape : m_shapes) {
        if (shape.parameterExpressions.isEmpty())
            continue;
        for (int i = 0; i < shape.parameterExpressions.size(); ++i) {
            const QString &expr = shape.parameterExpressions[i];
            if (expr.isEmpty())
                continue;
            qreal val = 0.0;
            if (!ExpressionSyntax::evaluate(expr, varValues, &val))
                continue;
            shape.applyParameterValue(i, val);
        }
    }

    TreeNode *root = m_tree.nodeById(m_tree.root().id);
    if (root)
        reEvaluateTransformExpressionsInNode(root, varValues);
}

void SceneDocument::reEvaluateTransformExpressionsInNode(TreeNode *node, const QHash<QString, qreal> &varValues)
{
    if (!node)
        return;
    if (node->type == TreeNode::Group
        && (node->operation == TreeNode::Translate
            || node->operation == TreeNode::Rotate
            || node->operation == TreeNode::Scale)) {
        for (int i = 0; i < node->transformExpressions.size() && i < 3; ++i) {
            const QString &expr = node->transformExpressions[i];
            if (expr.isEmpty())
                continue;
            qreal val = 0.0;
            if (!ExpressionSyntax::evaluate(expr, varValues, &val))
                continue;
            if (node->operation == TreeNode::Translate) {
                if (i == 0)      node->position.setX(static_cast<float>(val));
                else if (i == 1) node->position.setY(static_cast<float>(val));
                else if (i == 2) node->position.setZ(static_cast<float>(val));
            } else if (node->operation == TreeNode::Rotate) {
                if (i == 0)      node->rotation.setX(static_cast<float>(val));
                else if (i == 1) node->rotation.setY(static_cast<float>(val));
                else if (i == 2) node->rotation.setZ(static_cast<float>(val));
            } else {
                if (i == 0)      node->scale.setX(static_cast<float>(val));
                else if (i == 1) node->scale.setY(static_cast<float>(val));
                else if (i == 2) node->scale.setZ(static_cast<float>(val));
            }
        }
    }
    for (TreeNode &child : node->children)
        reEvaluateTransformExpressionsInNode(&child, varValues);
}

bool SceneDocument::moveTreeNode(int nodeId, int parentGroupId, int insertIndex, bool moduleParameterZone)
{
    // ── Snapshot for Point3D reordering ──────────────────────────────────
    int oldParentId = 0;
    int oldChildIndex = -1;
    bool isPoint3DMove = false;

    const TreeNode *before = m_tree.nodeById(nodeId);
    if (before && before->type == TreeNode::Primitive) {
        const ShapeNode *shape = shapeById(before->shapeId);
        if (shape && shape->type == ShapeNode::Point3D
            && findChildParent(m_tree.root(), nodeId, &oldParentId, &oldChildIndex)) {
            const TreeNode *oldParent = m_tree.nodeById(oldParentId);
            isPoint3DMove = oldParent && oldParent->operation == TreeNode::Polyhedron;
        }
    }

    // Snapshot the variable's current state before moving so we can detect
    // whether its isParameter flag changed and auto-rename accordingly.
    const bool wasParameter = before && before->type == TreeNode::Variable
                              ? before->isParameter : false;
    const QString oldName = before ? before->variableName : QString();

    const bool moved = m_tree.moveNode(nodeId, parentGroupId, insertIndex, moduleParameterZone);
    if (!moved)
        return false;

    // ── Remap Face3D indices after Point3D reorder ───────────────────────
    if (isPoint3DMove && oldParentId == parentGroupId) {
        int newParentId = 0;
        int newChildIndex = -1;
        if (findChildParent(m_tree.root(), nodeId, &newParentId, &newChildIndex)
            && newParentId == parentGroupId && newChildIndex >= 0) {
            remapPolyhedronFacesAfterChildMove(parentGroupId, oldChildIndex, newChildIndex);
        }
    }

    // Auto-rename when a variable is promoted to parameter or demoted back.
    // Only touch names that match the auto-generated "varN" / "parN" pattern;
    // user-chosen names (e.g. "radius", "height") are left untouched.
    const TreeNode *after = m_tree.nodeById(nodeId);
    if (after && after->type == TreeNode::Variable && after->isParameter != wasParameter) {
        const bool nowIsParam = after->isParameter;
        auto autoNumberSuffix = [](const QString &name, const QString &prefix, int *num) -> bool {
            if (!name.startsWith(prefix))
                return false;
            const QString suffix = name.mid(prefix.size());
            bool ok = false;
            const int n = suffix.toInt(&ok);
            if (ok && n > 0) {
                if (num) *num = n;
                return true;
            }
            return false;
        };

        QString newName;
        if (nowIsParam) {
            // varN → try to become parN (same number), fall back to next free parN
            int num = 0;
            if (autoNumberSuffix(oldName, QStringLiteral("var"), &num)) {
                QSet<QString> taken;
                collectVariableNames(m_tree.root(), &taken);
                taken.remove(oldName);
                const QString candidate = QStringLiteral("par%1").arg(num);
                newName = taken.contains(candidate) ? uniqueParameterName() : candidate;
            }
        } else {
            // parN → try to become varN (same number), fall back to next free varN
            int num = 0;
            if (autoNumberSuffix(oldName, QStringLiteral("par"), &num)) {
                QSet<QString> taken;
                collectVariableNames(m_tree.root(), &taken);
                taken.remove(oldName);
                const QString candidate = QStringLiteral("var%1").arg(num);
                newName = taken.contains(candidate) ? uniqueVariableName() : candidate;
            }
        }

        if (!newName.isEmpty())
            m_tree.renameVariable(nodeId, newName);
    }

    ensureTreeContainsAllShapes();
    synchronizeBooleanModesFromTree();
    return true;
}

// ── Polyhedron face index helpers ────────────────────────────────────────

/* static */
bool SceneDocument::findChildParent(const TreeNode &node, int targetId,
                                    int *outParentId, int *outChildIndex)
{
    if (node.id == targetId)
        return false; // root has no parent

    for (int i = 0; i < node.children.size(); ++i) {
        const TreeNode &child = node.children[i];
        if (child.id == targetId) {
            if (outParentId)    *outParentId    = node.id;
            if (outChildIndex)  *outChildIndex  = i;
            return true;
        }
        if (findChildParent(child, targetId, outParentId, outChildIndex))
            return true;
    }
    return false;
}

void SceneDocument::remapPolyhedronFacesAfterChildMove(int parentGroupId,
                                                       int fromIndex, int toIndex)
{
    TreeNode *parent = m_tree.nodeById(parentGroupId);
    if (!parent || parent->operation != TreeNode::Polyhedron)
        return;

    for (const TreeNode &child : parent->children) {
        if (child.type != TreeNode::Primitive) continue;
        ShapeNode *shape = shapeById(child.shapeId);
        if (!shape || shape->type != ShapeNode::Face3D) continue;
        if (shape->polyhedronFaces.isEmpty()) continue;

        bool changed = false;
        QVector<int> &face = shape->polyhedronFaces.first();
        for (int &idx : face) {
            int newIdx = idx;
            if (fromIndex < toIndex) {
                if      (idx == fromIndex)               newIdx = toIndex;
                else if (idx > fromIndex && idx <= toIndex) newIdx = idx - 1;
            } else if (fromIndex > toIndex) {
                if      (idx == fromIndex)               newIdx = toIndex;
                else if (idx >= toIndex && idx < fromIndex) newIdx = idx + 1;
            }
            if (newIdx != idx) {
                idx = newIdx;
                changed = true;
            }
        }
        if (changed)
            updateShape(*shape);
    }
}

void SceneDocument::remapPolyhedronFacesAfterChildDelete(int parentGroupId,
                                                         int deletedIndex)
{
    TreeNode *parent = m_tree.nodeById(parentGroupId);
    if (!parent || parent->operation != TreeNode::Polyhedron)
        return;

    for (const TreeNode &child : parent->children) {
        if (child.type != TreeNode::Primitive) continue;
        ShapeNode *shape = shapeById(child.shapeId);
        if (!shape || shape->type != ShapeNode::Face3D) continue;
        if (shape->polyhedronFaces.isEmpty()) continue;

        bool changed = false;
        QVector<int> &face = shape->polyhedronFaces.first();
        for (int &idx : face) {
            int newIdx = idx;
            if (idx == deletedIndex)
                newIdx = 0; // referenced point removed → fallback to 0
            else if (idx > deletedIndex)
                newIdx = idx - 1;
            if (newIdx != idx) {
                idx = newIdx;
                changed = true;
            }
        }
        if (changed)
            updateShape(*shape);
    }
}

bool SceneDocument::updateGroupTransform(int groupId, const QVector3D &position, const QVector3D &rotation, const QVector3D &scale, const QStringList &transformExpressions)
{
    return m_tree.updateGroupTransform(groupId, position, rotation, scale, transformExpressions);
}

bool SceneDocument::updateGroupColor(int groupId, const QColor &color)
{
    return m_tree.updateGroupColor(groupId, color);
}

bool SceneDocument::isValidIndex(int index) const
{
    return index >= 0 && index < m_shapes.size();
}

void SceneDocument::ensureTreeContainsAllShapes()
{
    for (const ShapeNode &shape : m_shapes) {
        if (!m_tree.containsPrimitive(shape.id))
            m_tree.addPrimitive(shape.id, operationForBooleanMode(shape.booleanMode));
    }
}

void SceneDocument::synchronizeBooleanModesFromTree()
{
    applyTreeBooleanModes(m_tree.root(), ShapeNode::Add);
}

void SceneDocument::applyTreeBooleanModes(const TreeNode &node, ShapeNode::BooleanMode inheritedMode)
{
    if (node.type == TreeNode::Primitive) {
        if (ShapeNode *shape = shapeById(node.shapeId))
            shape->booleanMode = inheritedMode;

        return;
    }

    if (node.type == TreeNode::Variable)
        return;

    for (int i = 0; i < node.children.size(); ++i) {
        ShapeNode::BooleanMode childMode = inheritedMode;
        if (node.operation == TreeNode::Difference && i > 0)
            childMode = ShapeNode::Subtract;
        else if (node.operation == TreeNode::Intersection)
            childMode = ShapeNode::Intersect;

        applyTreeBooleanModes(node.children[i], childMode);
    }
}

void SceneDocument::rebuildTreeFromShapes()
{
    m_tree.clear();

    for (const ShapeNode &shape : m_shapes)
        m_tree.addPrimitive(shape.id, operationForBooleanMode(shape.booleanMode));
}

SceneDocument::TreeNode::Operation SceneDocument::operationForBooleanMode(ShapeNode::BooleanMode booleanMode) const
{
    if (booleanMode == ShapeNode::Subtract)
        return TreeNode::Difference;
    if (booleanMode == ShapeNode::Intersect)
        return TreeNode::Intersection;
    return TreeNode::Union;
}

QString SceneDocument::uniqueVariableName() const
{
    QSet<QString> names;
    collectVariableNames(m_tree.root(), &names);

    int index = 1;
    QString candidate;
    do {
        candidate = QStringLiteral("var%1").arg(index++);
    } while (names.contains(candidate));

    return candidate;
}

QString SceneDocument::uniqueParameterName() const
{
    QSet<QString> names;
    collectVariableNames(m_tree.root(), &names);

    int index = 1;
    QString candidate;
    do {
        candidate = QStringLiteral("par%1").arg(index++);
    } while (names.contains(candidate));

    return candidate;
}
