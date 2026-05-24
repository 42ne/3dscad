#include "groupthumbnailcache.h"
#include "viewportwidget.h"

#include <QDataStream>
#include <algorithm>

// Background colour for group thumbnails — slightly cooler than the primitive card
// bg (219, 231, 246) to keep them visually distinct while blending with the group card.
static const QColor GroupThumbnailBg(210, 225, 240);

// ── Helpers ──────────────────────────────────────────────────────────────────

bool GroupThumbnailCache::isEligibleOperation(SceneDocument::TreeNode::Operation op)
{
    using Op = SceneDocument::TreeNode;
    return op == Op::Union
        || op == Op::Difference
        || op == Op::Intersection
        || op == Op::Hull
        || op == Op::Minkowski
        || op == Op::Module;
}

void GroupThumbnailCache::collectEligibleGroups(const SceneDocument::TreeNode &node,
                                                QHash<int, SceneDocument::TreeNode> &groups)
{
    if (node.type == SceneDocument::TreeNode::Group && isEligibleOperation(node.operation))
        groups[node.id] = node;

    for (const SceneDocument::TreeNode &child : node.children)
        collectEligibleGroups(child, groups);
}

void GroupThumbnailCache::collectGlobalVariables(const SceneDocument::TreeNode &treeRoot,
                                                  QVector<SceneDocument::TreeNode> &vars)
{
    for (const SceneDocument::TreeNode &child : treeRoot.children) {
        if (child.type == SceneDocument::TreeNode::Variable) {
            vars.append(child);
        } else if (child.type == SceneDocument::TreeNode::Group
                   && child.operation == SceneDocument::TreeNode::Scene) {
            // Flatten variables inside the implicit Scene container.
            for (const SceneDocument::TreeNode &sceneChild : child.children) {
                if (sceneChild.type == SceneDocument::TreeNode::Variable)
                    vars.append(sceneChild);
            }
        }
    }
}

void GroupThumbnailCache::collectShapeIds(const SceneDocument::TreeNode &node, QSet<int> &shapeIds)
{
    if (node.type == SceneDocument::TreeNode::Primitive)
        shapeIds.insert(node.shapeId);
    for (const SceneDocument::TreeNode &child : node.children)
        collectShapeIds(child, shapeIds);
}

// ── Fingerprinting ───────────────────────────────────────────────────────────

static void serializeTreeNode(QDataStream &ds, const SceneDocument::TreeNode &node)
{
    ds << node.id
       << static_cast<int>(node.type)
       << static_cast<int>(node.operation)
       << node.shapeId
       << node.variableName
       << node.variableExpression
       << node.variableValue
       << static_cast<quint8>(node.isParameter)
       << node.position.x() << node.position.y() << node.position.z()
       << node.rotation.x() << node.rotation.y() << node.rotation.z()
       << node.scale.x()    << node.scale.y()    << node.scale.z()
       << node.transformExpressions
       << node.loopVariable
       << node.loopRangeExpression
       << node.color.rgba()
       << node.moduleName
       << node.moduleCallArguments;

    ds << static_cast<int>(node.children.size());
    for (const SceneDocument::TreeNode &child : node.children)
        serializeTreeNode(ds, child);
}

static void serializeShapeNode(QDataStream &ds, const ShapeNode &shape)
{
    ds << shape.id
       << static_cast<int>(shape.type)
       << static_cast<int>(shape.booleanMode)
       << shape.size.x() << shape.size.y() << shape.size.z()
       << shape.radius
       << shape.radius2
       << shape.height
       << shape.parameterExpressions;
}

QByteArray GroupThumbnailCache::computeFingerprint(const SceneDocument::TreeNode &groupNode,
                                                   const QHash<int, ShapeNode> &allShapes,
                                                   const QVector<SceneDocument::TreeNode> &globalVars)
{
    QByteArray data;
    QDataStream ds(&data, QIODevice::WriteOnly);

    // Global variables influence expressions inside the group.
    ds << static_cast<int>(globalVars.size());
    for (const SceneDocument::TreeNode &var : globalVars)
        serializeTreeNode(ds, var);

    // The group's full tree structure.
    serializeTreeNode(ds, groupNode);

    // Shapes referenced anywhere within the group subtree.
    QSet<int> shapeIds;
    collectShapeIds(groupNode, shapeIds);
    QList<int> sortedIds = shapeIds.values();
    std::sort(sortedIds.begin(), sortedIds.end());
    ds << static_cast<int>(sortedIds.size());
    for (int id : sortedIds) {
        auto it = allShapes.find(id);
        if (it != allShapes.end())
            serializeShapeNode(ds, *it);
    }

    return data;
}

// ── GroupThumbnailCache implementation ───────────────────────────────────────

GroupThumbnailCache::GroupThumbnailCache(QSize size, QObject *parent)
    : QObject(parent)
    , m_size(size)
    , m_renderTimer(new QTimer(this))
{
    m_renderTimer->setSingleShot(true);
    connect(m_renderTimer, &QTimer::timeout,
            this, &GroupThumbnailCache::onRenderTimerTimeout);
}

GroupThumbnailCache::~GroupThumbnailCache()
{
    m_renderTimer->stop();
}

void GroupThumbnailCache::syncGroups(const SceneDocument &scene)
{
    // Collect all eligible group nodes.
    QHash<int, SceneDocument::TreeNode> currentGroups;
    collectEligibleGroups(scene.treeRoot(), currentGroups);

    // Purge cache entries for groups that no longer exist.
    const QList<int> cachedIds = m_cache.keys();
    for (int id : cachedIds) {
        if (!currentGroups.contains(id)) {
            m_cache.remove(id);
            m_lastFingerprint.remove(id);
            m_pending.remove(id);
        }
    }

    // Build a shape lookup for fingerprinting.
    QHash<int, ShapeNode> shapesById;
    for (const ShapeNode &shape : scene.shapes())
        shapesById[shape.id] = shape;

    // Collect global variables.
    QVector<SceneDocument::TreeNode> globalVars;
    collectGlobalVariables(scene.treeRoot(), globalVars);

    // Mark groups whose fingerprint changed since the last render.
    bool anyDirty = false;
    for (auto it = currentGroups.constBegin(); it != currentGroups.constEnd(); ++it) {
        const int groupId = it.key();
        const QByteArray fp = computeFingerprint(it.value(), shapesById, globalVars);
        if (!m_lastFingerprint.contains(groupId) || m_lastFingerprint[groupId] != fp) {
            m_pending.insert(groupId);
            anyDirty = true;
        }
    }

    if (anyDirty && !m_suspended) {
        // Capture the scene state needed to render the pending thumbnails.
        m_pendingSnapshot = scene.snapshot();
        m_globalVars      = globalVars;
        m_renderTimer->start(RenderDelayMs);
    }
}

QImage GroupThumbnailCache::thumbnail(int groupNodeId) const
{
    return m_cache.value(groupNodeId);
}

void GroupThumbnailCache::setSuspended(bool suspended)
{
    if (m_suspended == suspended)
        return;

    m_suspended = suspended;
    if (m_suspended) {
        m_renderTimer->stop();
    } else if (!m_pending.isEmpty()) {
        m_renderTimer->start(RenderDelayMs);
    }
}

// ── Rendering ────────────────────────────────────────────────────────────────

static const SceneDocument::TreeNode *findNodeInTree(const SceneDocument::TreeNode &root, int id)
{
    if (root.id == id)
        return &root;
    for (const SceneDocument::TreeNode &child : root.children) {
        const SceneDocument::TreeNode *found = findNodeInTree(child, id);
        if (found)
            return found;
    }
    return nullptr;
}

void GroupThumbnailCache::onRenderTimerTimeout()
{
    if (m_suspended || m_pending.isEmpty())
        return;

    const QSet<int> toRender = m_pending;
    m_pending.clear();

    // Shape lookup from the captured snapshot.
    QHash<int, ShapeNode> shapesById;
    for (const ShapeNode &shape : m_pendingSnapshot.shapes)
        shapesById[shape.id] = shape;

    bool anyRendered = false;

    for (int groupId : toRender) {
        const SceneDocument::TreeNode *groupNode =
            findNodeInTree(m_pendingSnapshot.treeRoot, groupId);

        if (!groupNode) {
            // Group was removed between syncGroups() and the timer firing.
            m_cache.remove(groupId);
            m_lastFingerprint.remove(groupId);
            continue;
        }

        // Build a minimal sub-document: global variables + the target group.
        // Keep all shapes so the tree's primitive references resolve correctly.
        SceneDocument::Snapshot subSnap = m_pendingSnapshot;
        subSnap.treeRoot.children.clear();
        for (const SceneDocument::TreeNode &var : m_globalVars)
            subSnap.treeRoot.children.append(var);
        subSnap.treeRoot.children.append(*groupNode);

        // CRITICAL: treeSnapshot.treeRoot must match treeRoot so that
        // SceneDocument::restoreSnapshot takes the treeSnapshot path (id > 0).
        subSnap.treeSnapshot.treeRoot = subSnap.treeRoot;

        SceneDocument subDoc;
        subDoc.restoreSnapshot(subSnap);

        m_cache[groupId] = ViewportWidget::renderThumbnail(subDoc, m_size, GroupThumbnailBg);

        // Update the stored fingerprint so subsequent unchanged syncs are cheap.
        m_lastFingerprint[groupId] =
            computeFingerprint(*groupNode, shapesById, m_globalVars);

        anyRendered = true;
    }

    if (anyRendered)
        emit thumbnailsUpdated();
}
