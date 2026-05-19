#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QObject>
#include <QProcess>
#include <QDir>

class QLineEdit;
class QPushButton;
class QTextEdit;
class QThread;

// ── Worker (runs in a separate thread) ────────────────────────────────────────

class BuildWorker : public QObject
{
    Q_OBJECT
public:
    QString sourceExe;
    QString outputDir;
    QString qtBinDir;
    QString sevenZipDir;
    QString extraSrcFolder;   // optional folder to bundle
    QString extraBundlePath;  // relative path inside the bundle (e.g. "docs/sample_codes")

signals:
    void log(const QString &msg);
    void finished(bool success, const QString &outputPath);

public slots:
    void run();

private:
    bool runProcess(const QString &program, const QStringList &args,
                    const QString &extraPath = QString());
    bool copyDirRecursive(const QDir &src, const QDir &dst);
};

// ── Main window ───────────────────────────────────────────────────────────────

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void browseSourceExe();
    void browseOutputDir();
    void browseQtBin();
    void browseSevenZip();
    void browseExtraFolder();
    void onBuildClicked();
    void appendLog(const QString &msg);
    void onBuildFinished(bool success, const QString &path);

private:
    void autoDetect();
    void setUiEnabled(bool enabled);

    QLineEdit   *m_sourceExe;
    QLineEdit   *m_outputDir;
    QLineEdit   *m_qtBinDir;
    QLineEdit   *m_sevenZipDir;
    QLineEdit   *m_extraSrcFolder;
    QLineEdit   *m_extraBundlePath;
    QPushButton *m_buildBtn;
    QTextEdit   *m_log;

    QThread     *m_thread = nullptr;
    BuildWorker *m_worker = nullptr;
};

#endif
