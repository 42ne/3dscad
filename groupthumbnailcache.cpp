#include "groupthumbnailcache.h"
#include "viewportwidget.h"

#include <QDataStream>
#include <QtConcurrent>
#include <algorithm>

// Background colour for group thumbnails — slightly cooler than the primitive card
// bg (219, 231, 246) to keep them visually distinct while blending with the group card.
static const QColor GroupThumbnailBg(210, 225, 240);
static constexpr int MaxRenderItemsPerBatch = 3;

// ── Helpers ──────────────────────────────────────────────────────────────────

bool GroupThumbnailCache::isEligibleOperation(SceneDocument::TreeNode::Operation op)
{
    using Op = SceneDocument::TreeNode;
    return op == Op::Union
        || op == Op::Difference
        || op == Op::Intersection
        || op == Op::Hull
        || op == Op::Minkowski
        || op == Op::Polyhedron
        || op == Op::Module
        || op == Op::For;
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

void GroupThumbnailCache::collectModuleCalls(const SceneDocument::TreeNode &node,
                                             QHash<int, SceneDocument::TreeNode> &calls)
{
    if (node.type == SceneDocument::TreeNode::ModuleCall)
        calls[node.id] = node;
    // Do NOT recurse into module declaration bodies.  Calls inside a module
    // body are implementation details; rendering them in isolation (with the
    // loop variable undefined, etc.) produces misleading results and can crash
    // when the geometry references undefined variables or is too complex.
    if (node.type == SceneDocument::TreeNode::Group
        && node.operation == SceneDocument::TreeNode::Module)
        return;
    for (const SceneDocument::TreeNode &child : node.children)
        collectModuleCalls(child, calls);
}

// ── Node lookup ──────────────────────────────────────────────────────────────
// Placed before fingerprinting so both fingerprint and rendering code can use it.

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

// Collects all module declarations transitively referenced by ModuleCall nodes
// within `node`'s subtree.  `treeRoot` is used to look up declarations by ID.
// `visited` prevents infinite recursion in case of pathological circular refs.
static void collectReferencedModules(const SceneDocument::TreeNode &node,
                                     const SceneDocument::TreeNode &treeRoot,
                                     QHash<int, SceneDocument::TreeNode> &modules,
                                     QSet<int> &visited)
{
    if (node.type == SceneDocument::TreeNode::ModuleCall) {
        if (!visited.contains(node.shapeId)) {
            visited.insert(node.shapeId);
            const SceneDocument::TreeNode *modDecl = findNodeInTree(treeRoot, node.shapeId);
            if (modDecl) {
                modules[modDecl->id] = *modDecl;
                // Recurse to capture modules that *this* module calls.
                collectReferencedModules(*modDecl, treeRoot, modules, visited);
            }
        }
    }
    for (const SceneDocument::TreeNode &child : node.children)
        collectReferencedModules(child, treeRoot, modules, visited);
}

static bool moduleReferencesModule(const SceneDocument::TreeNode &node,
                                   const SceneDocument::TreeNode &treeRoot,
                                   int targetModuleId,
                                   QSet<int> &visited)
{
    if (node.type == SceneDocument::TreeNode::ModuleCall) {
        if (node.shapeId == targetModuleId)
            return true;
        if (visited.contains(node.shapeId))
            return false;

        visited.insert(node.shapeId);
        const SceneDocument::TreeNode *modDecl = findNodeInTree(treeRoot, node.shapeId);
        return modDecl && moduleReferencesModule(*modDecl, treeRoot, targetModuleId, visited);
    }

    for (const SceneDocument::TreeNode &child : node.children) {
        if (moduleReferencesModule(child, treeRoot, targetModuleId, visited))
            return true;
    }
    return false;
}

static bool moduleHasRecursiveCall(const SceneDocument::TreeNode &moduleNode,
                                   const SceneDocument::TreeNode &treeRoot)
{
    QSet<int> visited;
    visited.insert(moduleNode.id);
    for (const SceneDocument::TreeNode &child : moduleNode.children) {
        if (moduleReferencesModule(child, treeRoot, moduleNode.id, visited))
            return true;
    }
    return false;
}

static void collectShapeIdsForThumbnail(const SceneDocument::TreeNode &node, QSet<int> &shapeIds)
{
    if (node.type == SceneDocument::TreeNode::Primitive)
        shapeIds.insert(node.shapeId);
    for (const SceneDocument::TreeNode &child : node.children)
        collectShapeIdsForThumbnail(child, shapeIds);
}

static QVector<ShapeNode> filteredShapesForThumbnail(
    const QVector<ShapeNode> &allShapes,
    const SceneDocument::TreeNode &primaryNode,
    const QHash<int, SceneDocument::TreeNode> &referencedModules)
{
    QSet<int> shapeIds;
    collectShapeIdsForThumbnail(primaryNode, shapeIds);
    for (const SceneDocument::TreeNode &modDecl : referencedModules)
        collectShapeIdsForThumbnail(modDecl, shapeIds);

    QVector<ShapeNode> filtered;
    filtered.reserve(shapeIds.size());
    for (const ShapeNode &shape : allShapes) {
        if (shapeIds.contains(shape.id))
            filtered.append(shape);
    }
    return filtered;
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
       << node.conditionExpression
       << static_cast<quint8>(node.isElseBranch)
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

QByteArray GroupThumbnailCache::computeFingerprint(
    const SceneDocument::TreeNode &groupNode,
    const QHash<int, ShapeNode> &allShapes,
    const QVector<SceneDocument::TreeNode> &globalVars,
    const QHash<int, SceneDocument::TreeNode> &referencedModules)
{
    QByteArray data;
    QDataStream ds(&data, QIODevice::WriteOnly);

    // Global variables influence expressions inside the group.
    ds << static_cast<int>(globalVars.size());
    for (const SceneDocument::TreeNode &var : globalVars)
        serializeTreeNode(ds, var);

    // The group's full tree structure.
    serializeTreeNode(ds, groupNode);

    // Shapes referenced within the group subtree AND within any referenced modules
    // (so that changing a module's primitive dimensions triggers a re-render here).
    QSet<int> shapeIds;
    collectShapeIds(groupNode, shapeIds);
    for (const SceneDocument::TreeNode &modDecl : referencedModules)
        collectShapeIds(modDecl, shapeIds);

    QList<int> sortedIds = shapeIds.values();
    std::sort(sortedIds.begin(), sortedIds.end());
    ds << static_cast<int>(sortedIds.size());
    for (int id : sortedIds) {
        auto it = allShapes.find(id);
        if (it != allShapes.end())
            serializeShapeNode(ds, *it);
    }

    // Serialize referenced module declarations (sorted by id for determinism).
    // This ensures changes to a called module propagate to this group's fingerprint.
    QList<int> sortedModIds = referencedModules.keys();
    std::sort(sortedModIds.begin(), sortedModIds.end());
    ds << static_cast<int>(sortedModIds.size());
    for (int id : sortedModIds)
        serializeTreeNode(ds, referencedModules[id]);

    return data;
}

QByteArray GroupThumbnailCache::computeModuleCallFingerprint(
    const SceneDocument::TreeNode &callNode,
    const SceneDocument::TreeNode &modDecl,
    const QHash<int, ShapeNode> &allShapes,
    const QVector<SceneDocument::TreeNode> &globalVars,
    const QHash<int, SceneDocument::TreeNode> &referencedModules)
{
    QByteArray data;
    QDataStream ds(&data, QIODevice::WriteOnly);

    // Global variables influence expressions inside the module.
    ds << static_cast<int>(globalVars.size());
    for (const SceneDocument::TreeNode &var : globalVars)
        serializeTreeNode(ds, var);

    // The call node (holds argument overrides).
    serializeTreeNode(ds, callNode);

    // The full module declaration subtree.
    serializeTreeNode(ds, modDecl);

    // Shapes referenced in the module body AND in any modules it calls.
    QSet<int> shapeIds;
    collectShapeIds(modDecl, shapeIds);
    for (const SceneDocument::TreeNode &mod : referencedModules)
        collectShapeIds(mod, shapeIds);

    QList<int> sortedIds = shapeIds.values();
    std::sort(sortedIds.begin(), sortedIds.end());
    ds << static_cast<int>(sortedIds.size());
    for (int id : sortedIds) {
        auto it = allShapes.find(id);
        if (it != allShapes.end())
            serializeShapeNode(ds, *it);
    }

    // Serialize transitively referenced module declarations.
    QList<int> sortedModIds = referencedModules.keys();
    std::sort(sortedModIds.begin(), sortedModIds.end());
    ds << static_cast<int>(sortedModIds.size());
    for (int id : sortedModIds)
        serializeTreeNode(ds, referencedModules[id]);

    return data;
}

// ── GroupThumbnailCache implementation ───────────────────────────────────────

GroupThumbnailCache::GroupThumbnailCache(QSize size, QObject *parent)
    : QObject(parent)
    , m_size(size)
    , m_renderTimer(new QTimer(this))
    , m_watcher(new QFutureWatcher<QHash<int, QImage>>(this))
{
    m_renderTimer->setSingleShot(true);
    connect(m_renderTimer, &QTimer::timeout,
            this, &GroupThumbnailCache::onRenderTimerTimeout);
    connect(m_watcher, &QFutureWatcher<QHash<int, QImage>>::finished,
            this, &GroupThumbnailCache::onRenderFinished);
}

GroupThumbnailCache::~GroupThumbnailCache()
{
    m_renderTimer->stop();
    if (m_watcher->isRunning()) {
        m_watcher->cancel();
        m_watcher->waitForFinished();
    }
}

void GroupThumbnailCache::syncGroups(const SceneDocument &scene)
{
    // Collect eligible groups and module calls.
    //
    // IMPORTANT: iterate treeRoot().children rather than passing treeRoot()
    // itself.  The synthetic scene root is Group+Module, so passing it
    // directly to collectEligibleGroups would add the root node as an eligible
    // group (Module is an eligible operation).  The resulting sub-document
    // would try to render the entire scene multiple times inside a union,
    // causing excessive Manifold CSG work and a crash on complex scenes.
    QHash<int, SceneDocument::TreeNode> currentGroups;
    for (const SceneDocument::TreeNode &child : scene.treeRoot().children)
        collectEligibleGroups(child, currentGroups);

    QHash<int, SceneDocument::TreeNode> currentCalls;
    for (const SceneDocument::TreeNode &child : scene.treeRoot().children)
        collectModuleCalls(child, currentCalls);

    // Purge cache entries for nodes that no longer exist.
    const QList<int> cachedIds = m_cache.keys();
    for (int id : cachedIds) {
        if (!currentGroups.contains(id) && !currentCalls.contains(id)) {
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

    bool anyDirty = false;

    // Mark groups whose fingerprint changed since the last render.
    // Include module declarations transitively called by the group so that
    // changes inside a module (e.g. tooth dimensions) invalidate any for-loop
    // or other group that calls it.
    for (auto it = currentGroups.constBegin(); it != currentGroups.constEnd(); ++it) {
        const int groupId = it.key();

        QHash<int, SceneDocument::TreeNode> referencedModules;
        QSet<int> visited;
        collectReferencedModules(it.value(), scene.treeRoot(), referencedModules, visited);

        const QByteArray fp = computeFingerprint(it.value(), shapesById, globalVars, referencedModules);
        if (!m_lastFingerprint.contains(groupId) || m_lastFingerprint[groupId] != fp) {
            m_pending.insert(groupId);
            anyDirty = true;
        }
    }

    // Mark module calls whose fingerprint changed since the last render.
    for (auto it = currentCalls.constBegin(); it != currentCalls.constEnd(); ++it) {
        const int callId = it.key();
        const SceneDocument::TreeNode *modDecl = scene.treeNodeById(it.value().shapeId);
        if (!modDecl)
            continue; // orphaned call — skip, will be purged next cycle

        // Collect modules called (transitively) by the module body so that
        // changes inside a called module propagate to this call's fingerprint.
        QHash<int, SceneDocument::TreeNode> referencedModules;
        QSet<int> visited;
        visited.insert(modDecl->id); // skip the module itself if it recurses
        collectReferencedModules(*modDecl, scene.treeRoot(), referencedModules, visited);

        const QByteArray fp = computeModuleCallFingerprint(
            it.value(), *modDecl, shapesById, globalVars, referencedModules);
        if (!m_lastFingerprint.contains(callId) || m_lastFingerprint[callId] != fp) {
            m_pending.insert(callId);
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

void GroupThumbnailCache::onRenderTimerTimeout()
{
    // If a render is already running we leave m_pending intact; onRenderFinished
    // will restart the timer once the current batch completes.
    if (m_suspended || m_pending.isEmpty() || m_renderInFlight)
        return;

    QSet<int> toRender;
    const QList<int> pendingIds = m_pending.values();
    for (int i = 0; i < pendingIds.size() && toRender.size() < MaxRenderItemsPerBatch; ++i) {
        toRender.insert(pendingIds[i]);
        m_pending.remove(pendingIds[i]);
    }

    // Shape lookup from the captured snapshot (used for fingerprinting).
    QHash<int, ShapeNode> shapesById;
    for (const ShapeNode &shape : m_pendingSnapshot.shapes)
        shapesById[shape.id] = shape;

    // Build per-node sub-snapshots on the main thread; rendering happens off it.
    using SubItem = QPair<int, SceneDocument::Snapshot>; // {nodeId, subSnap}
    QVector<SubItem> items;
    m_inflightFingerprints.clear();

    for (int nodeId : toRender) {
        const SceneDocument::TreeNode *nodePtr =
            findNodeInTree(m_pendingSnapshot.treeRoot, nodeId);

        if (!nodePtr) {
            // Node was removed between syncGroups() and the timer firing.
            m_cache.remove(nodeId);
            m_lastFingerprint.remove(nodeId);
            continue;
        }

        // Minimal sub-document: global variables + node-specific content.
        SceneDocument::Snapshot subSnap = m_pendingSnapshot;
        subSnap.treeRoot.children.clear();
        for (const SceneDocument::TreeNode &var : m_globalVars)
            subSnap.treeRoot.children.append(var);

        QByteArray fp;
        if (nodePtr->type == SceneDocument::TreeNode::ModuleCall) {
            // Sub-document: module declaration (+ all modules it calls) + call node.
            const SceneDocument::TreeNode *modDecl =
                findNodeInTree(m_pendingSnapshot.treeRoot, nodePtr->shapeId);
            if (!modDecl) {
                m_cache.remove(nodeId);
                m_lastFingerprint.remove(nodeId);
                continue;
            }

            // Collect modules transitively called by the module body.
            QHash<int, SceneDocument::TreeNode> referencedModules;
            QSet<int> visited;
            visited.insert(modDecl->id);
            collectReferencedModules(*modDecl, m_pendingSnapshot.treeRoot,
                                     referencedModules, visited);

            // Append transitively called module declarations first.
            QList<int> sortedModIds = referencedModules.keys();
            std::sort(sortedModIds.begin(), sortedModIds.end());
            for (int modId : sortedModIds)
                subSnap.treeRoot.children.append(referencedModules[modId]);

            subSnap.treeRoot.children.append(*modDecl);
            subSnap.treeRoot.children.append(*nodePtr);
            subSnap.shapes = filteredShapesForThumbnail(
                m_pendingSnapshot.shapes, *modDecl, referencedModules);
            fp = computeModuleCallFingerprint(
                *nodePtr, *modDecl, shapesById, m_globalVars, referencedModules);
        } else {
            // Collect module declarations called (transitively) within this group
            // so they are present in the sub-document for the renderer.
            QHash<int, SceneDocument::TreeNode> referencedModules;
            QSet<int> visited;
            collectReferencedModules(*nodePtr, m_pendingSnapshot.treeRoot,
                                     referencedModules, visited);

            // Append module declarations before the group node so the code
            // generator encounters the definitions before any calls.
            QList<int> sortedModIds = referencedModules.keys();
            std::sort(sortedModIds.begin(), sortedModIds.end());
            for (int modId : sortedModIds)
                subSnap.treeRoot.children.append(referencedModules[modId]);

            subSnap.treeRoot.children.append(*nodePtr);

            // Module declarations are definitions, so they do not render any
            // geometry by themselves.  Add an ephemeral call with no arguments
            // to preview the module using its declared default parameter values.
            if (nodePtr->type == SceneDocument::TreeNode::Group
                && nodePtr->operation == SceneDocument::TreeNode::Module
                && !moduleHasRecursiveCall(*nodePtr, m_pendingSnapshot.treeRoot)) {
                SceneDocument::TreeNode syntheticCall;
                syntheticCall.id = subSnap.nextTreeNodeId++;
                syntheticCall.type = SceneDocument::TreeNode::ModuleCall;
                syntheticCall.shapeId = nodePtr->id;
                syntheticCall.moduleName = nodePtr->moduleName;
                subSnap.treeRoot.children.append(syntheticCall);
            }

            subSnap.shapes = filteredShapesForThumbnail(
                m_pendingSnapshot.shapes, *nodePtr, referencedModules);
            fp = computeFingerprint(*nodePtr, shapesById, m_globalVars, referencedModules);
        }

        // CRITICAL: treeSnapshot.treeRoot must match treeRoot so that
        // SceneDocument::restoreSnapshot takes the treeSnapshot path (id > 0).
        subSnap.treeSnapshot.treeRoot = subSnap.treeRoot;

        items.append({nodeId, subSnap});
        m_inflightFingerprints[nodeId] = fp;
    }

    if (items.isEmpty()) {
        if (!m_pending.isEmpty() && !m_suspended)
            m_renderTimer->start(RenderDelayMs);
        return;
    }

    // Launch the render on a pool thread so the GUI stays responsive.
    // buildManifoldCsgMesh() is guarded by s_manifoldMutex in manifoldcsg.cpp,
    // so concurrent calls from the viewport thread and this thread are serialised.
    m_renderInFlight = true;
    const QSize size    = m_size * 2;
    const QColor bgColor = GroupThumbnailBg;

    auto future = QtConcurrent::run([items, size, bgColor]() -> QHash<int, QImage> {
        QHash<int, QImage> results;
        for (const SubItem &item : items) {
            SceneDocument subDoc;
            subDoc.restoreSnapshot(item.second);
            results[item.first] = ViewportWidget::renderThumbnail(subDoc, size, bgColor);
        }
        return results;
    });
    m_watcher->setFuture(future);
}

void GroupThumbnailCache::onRenderFinished()
{
    m_renderInFlight = false;

    const QHash<int, QImage> results = m_watcher->result();
    bool anyRendered = false;

    for (auto it = results.constBegin(); it != results.constEnd(); ++it) {
        if (!it.value().isNull()) {
            m_cache[it.key()] = it.value();
            anyRendered = true;
        }
    }

    // Commit fingerprints only for nodes whose render succeeded.
    for (auto it = m_inflightFingerprints.constBegin();
         it != m_inflightFingerprints.constEnd(); ++it) {
        if (m_cache.contains(it.key()))
            m_lastFingerprint[it.key()] = it.value();
    }
    m_inflightFingerprints.clear();

    if (anyRendered)
        emit thumbnailsUpdated();

    // Nodes that were dirtied while the render was in flight can go now.
    if (!m_pending.isEmpty() && !m_suspended)
        m_renderTimer->start(RenderDelayMs);
}
