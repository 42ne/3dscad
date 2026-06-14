#ifndef SUBSETTESTWINDOW_H
#define SUBSETTESTWINDOW_H

#include "../../scenedocument.h"

#include <QMainWindow>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

class QListWidget;
class QTextEdit;
class QTabWidget;
class QLabel;
class QPushButton;

struct FileResult
{
    QString filePath;
    QString fileName;

    bool parseOk = false;
    QString parseError;
    int parseErrorLine = 0;

    // ── node counts ──────────────────────────────────────────────────
    int varCount           = 0;
    int topPrimitiveCount  = 0;  // primitives NOT inside a module definition
    int allPrimitiveCount  = 0;  // all primitives including inside modules
    int transformCount     = 0;
    int booleanCount       = 0;
    int moduleDefCount     = 0;
    int moduleCallCount    = 0;
    int forLoopCount       = 0;
    int colorCount         = 0;
    int extrudeCount       = 0;
    int otherGroupCount    = 0;

    QMap<QString, int> primitiveTypes;  // "Cube"→N …
    QMap<QString, int> transformTypes;  // "Translate"→N …
    QMap<QString, int> booleanTypes;
    QStringList        moduleNames;

    // ── render analysis ──────────────────────────────────────────────
    bool    willRender   = false;
    QString renderReason;

    // ── roundtrip ────────────────────────────────────────────────────
    QString generatedCode;
    bool    roundtripOk       = false;
    int     roundtripNodeDiff = 0;   // difference in primitive counts after re-parse

    // ── snapshot (kept for generated-code tab) ───────────────────────
    SceneDocument::Snapshot snapshot;
};

class SubsetTestWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit SubsetTestWindow(QWidget *parent = nullptr);

private slots:
    void analyzeAll();
    void onFileSelected(int row);

private:
    void buildUi();
    void loadFileList();

    FileResult analyzeFile(const QString &filePath) const;
    QString    buildReportHtml(const FileResult &r) const;
    QString    summaryHtml() const;

    // tree helpers
    static void collectStats(const SceneDocument::TreeNode &node,
                              bool insideModule,
                              FileResult *r);
    static void determineRender(FileResult *r);
    static QString generateCode(const SceneDocument::Snapshot &snap);
    static QString nodeToCode(const SceneDocument::TreeNode &node,
                               const QVector<ShapeNode> &shapes,
                               int depth);
    static QString shapeToCode(const ShapeNode &s);
    static QString opName(SceneDocument::TreeNode::Operation op);
    static QString typeName(ShapeNode::Type t);

private:
    QListWidget *m_fileList  = nullptr;
    QTextEdit   *m_report    = nullptr;
    QTextEdit   *m_codeView  = nullptr;
    QTabWidget  *m_tabs      = nullptr;
    QLabel      *m_status    = nullptr;

    QVector<FileResult> m_results;
};

#endif // SUBSETTESTWINDOW_H
