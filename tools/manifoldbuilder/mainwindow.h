#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void browseRepoRoot();
    void browseQtRoot();
    void onBuildClicked();
    void onStopClicked();
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void buildUi();
    void autoDetect();
    void refreshDerivedPaths();
    void setBuildRunning(bool running);
    bool validateInputs();
    QString repoRootFromToolPath() const;
    QString detectQtRoot() const;
    QString powerShellProgram() const;
    QString selectedArch() const;
    QString buildDir() const;
    QString outputLibraryPath() const;
    void appendLog(const QString &text);

    QLineEdit *m_repoRoot = nullptr;
    QLineEdit *m_qtRoot = nullptr;
    QLineEdit *m_scriptPath = nullptr;
    QLineEdit *m_buildDir = nullptr;
    QLineEdit *m_outputLibrary = nullptr;
    QComboBox *m_arch = nullptr;
    QComboBox *m_generator = nullptr;
    QCheckBox *m_cleanBuildDir = nullptr;
    QPushButton *m_buildButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QLabel *m_status = nullptr;
    QTextEdit *m_log = nullptr;
    QProcess *m_process = nullptr;
};

#endif
