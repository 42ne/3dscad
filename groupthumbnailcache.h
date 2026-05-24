#ifndef GROUPTHUMBNAILCACHE_H
#define GROUPTHUMBNAILCACHE_H

#include "scenedocument.h"

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QTimer>
#include <QVector>

// Manages thumbnail renders for scene tree group nodes (Union, Difference,
// Intersection, Hull, Minkowski, Module).
// Works identically to NodeThumbnailCache: syncGroups() detects changed groups
// and schedules a deferred render; results arrive via thumbnailsUpdated().
//
// The thumbnail replaces the abstract operation icon in the group card header,
// giving the user a small live preview of what the group produces.
class GroupThumbnailCache : public QObject
{
    Q_OBJECT

public:
    explicit GroupThumbnailCache(QSize size = QSize(64, 64), QObject *parent = nullptr);
    ~GroupThumbnailCache() override;

    // Returns true for operations whose group cards show a thumbnail icon.
    static bool isEligibleOperation(SceneDocument::TreeNode::Operation op);

    // Compare the current scene state with the last-rendered set.
    // Groups whose subtree or referenced shapes changed are queued for re-render.
    void syncGroups(const SceneDocument &scene);

    // Return cached thumbnail for groupNodeId, or a null QImage if not yet ready.
    QImage thumbnail(int groupNodeId) const;

    void setSuspended(bool suspended);

signals:
    // Emitted on the main thread after one or more thumbnails become available.
    void thumbnailsUpdated();

private slots:
    void onRenderTimerTimeout();
    void onRenderFinished();

private:
    static constexpr int RenderDelayMs = 2000; // debounce: render 2 s after last change

    static void collectEligibleGroups(const SceneDocument::TreeNode &node,
                                      QHash<int, SceneDocument::TreeNode> &groups);
    static void collectGlobalVariables(const SceneDocument::TreeNode &treeRoot,
                                       QVector<SceneDocument::TreeNode> &vars);
    static void collectShapeIds(const SceneDocument::TreeNode &node, QSet<int> &shapeIds);
    static QByteArray computeFingerprint(const SceneDocument::TreeNode &groupNode,
                                         const QHash<int, ShapeNode> &allShapes,
                                         const QVector<SceneDocument::TreeNode> &globalVars);

    QSize m_size;
    QHash<int, QImage>     m_cache;           // groupId → ready thumbnail
    QHash<int, QByteArray> m_lastFingerprint; // groupId → fingerprint at last render
    QSet<int>              m_pending;         // group IDs awaiting render

    // Scene state at the time the latest dirty batch was detected; used for rendering.
    SceneDocument::Snapshot              m_pendingSnapshot;
    QVector<SceneDocument::TreeNode>     m_globalVars;

    QTimer *m_renderTimer = nullptr;
    bool    m_suspended   = false;

    // Background render state.
    QFutureWatcher<QHash<int, QImage>> *m_watcher = nullptr;
    QHash<int, QByteArray>              m_inflightFingerprints; // saved for onRenderFinished
    bool                                m_renderInFlight = false;
};

#endif // GROUPTHUMBNAILCACHE_H
