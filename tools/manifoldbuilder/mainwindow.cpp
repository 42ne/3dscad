#include "mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {

QLineEdit *makeReadOnlyField()
{
    auto *field = new QLineEdit;
    field->setReadOnly(true);
    return field;
}

QLineEdit *makeEditableField()
{
    auto *field = new QLineEdit;
    field->setMinimumWidth(460);
    return field;
}

QPushButton *makeBrowseButton()
{
    auto *button = new QPushButton(QStringLiteral("..."));
    button->setFixedWidth(34);
    return button;
}

QString nativePath(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

QString cleanPath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_process(new QProcess(this))
{
    buildUi();
    autoDetect();

    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &MainWindow::onReadyRead);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &MainWindow::onProcessFinished);
}

MainWindow::~MainWindow()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1500);
    }
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Manifold Builder"));
    setMinimumSize(700, 480);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setSpacing(8);
    setCentralWidget(central);

    auto *configGroup = new QGroupBox(QStringLiteral("Configuration"), this);
    auto *form = new QFormLayout(configGroup);
    form->setLabelAlignment(Qt::AlignRight);

    m_repoRoot = makeEditableField();
    auto *repoBrowse = makeBrowseButton();
    auto *repoRow = new QWidget;
    auto *repoLayout = new QHBoxLayout(repoRow);
    repoLayout->setContentsMargins(0, 0, 0, 0);
    repoLayout->addWidget(m_repoRoot, 1);
    repoLayout->addWidget(repoBrowse);
    form->addRow(QStringLiteral("Repo root:"), repoRow);

    m_qtRoot = makeEditableField();
    auto *qtBrowse = makeBrowseButton();
    auto *qtRow = new QWidget;
    auto *qtLayout = new QHBoxLayout(qtRow);
    qtLayout->setContentsMargins(0, 0, 0, 0);
    qtLayout->addWidget(m_qtRoot, 1);
    qtLayout->addWidget(qtBrowse);
    form->addRow(QStringLiteral("Qt root:"), qtRow);

    m_arch = new QComboBox;
    m_arch->addItem(QStringLiteral("32-bit MinGW"), QStringLiteral("32"));
    m_arch->addItem(QStringLiteral("64-bit MinGW"), QStringLiteral("64"));
    form->addRow(QStringLiteral("Architecture:"), m_arch);

    m_generator = new QComboBox;
    m_generator->addItem(QStringLiteral("MinGW Makefiles"));
    form->addRow(QStringLiteral("CMake generator:"), m_generator);

    m_cleanBuildDir = new QCheckBox(QStringLiteral("Clean build directory before build"));
    form->addRow(QString(), m_cleanBuildDir);

    m_scriptPath = makeReadOnlyField();
    form->addRow(QStringLiteral("Script:"), m_scriptPath);

    m_buildDir = makeReadOnlyField();
    form->addRow(QStringLiteral("Build dir:"), m_buildDir);

    m_outputLibrary = makeReadOnlyField();
    form->addRow(QStringLiteral("Output lib:"), m_outputLibrary);

    root->addWidget(configGroup);

    auto *buttonRow = new QHBoxLayout;
    m_buildButton = new QPushButton(QStringLiteral("Build Manifold"));
    m_stopButton = new QPushButton(QStringLiteral("Stop"));
    m_stopButton->setEnabled(false);
    buttonRow->addWidget(m_buildButton);
    buttonRow->addWidget(m_stopButton);
    buttonRow->addStretch(1);
    root->addLayout(buttonRow);

    m_status = new QLabel(QStringLiteral("Ready"));
    root->addWidget(m_status);

    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setFontFamily(QStringLiteral("Consolas"));
    root->addWidget(m_log, 1);

    connect(repoBrowse, &QPushButton::clicked, this, &MainWindow::browseRepoRoot);
    connect(qtBrowse, &QPushButton::clicked, this, &MainWindow::browseQtRoot);
    connect(m_buildButton, &QPushButton::clicked, this, &MainWindow::onBuildClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(m_repoRoot, &QLineEdit::textChanged, this, &MainWindow::refreshDerivedPaths);
    connect(m_arch, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::refreshDerivedPaths);
}

void MainWindow::autoDetect()
{
    m_repoRoot->setText(nativePath(repoRootFromToolPath()));
    m_qtRoot->setText(nativePath(detectQtRoot()));

    const QString qtRoot = cleanPath(m_qtRoot->text());
    if (QDir(qtRoot + QStringLiteral("/Tools/mingw810_32/bin")).exists())
        m_arch->setCurrentIndex(m_arch->findData(QStringLiteral("32")));
    else if (QDir(qtRoot + QStringLiteral("/Tools/mingw810_64/bin")).exists())
        m_arch->setCurrentIndex(m_arch->findData(QStringLiteral("64")));

    refreshDerivedPaths();
}

QString MainWindow::repoRootFromToolPath() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (QFile::exists(dir.filePath(QStringLiteral("3DScad.pro")))
            && QFile::exists(dir.filePath(QStringLiteral("scripts/build-manifold.ps1"))))
            return dir.absolutePath();
        dir.cdUp();
    }

    QDir sourceDir(QStringLiteral(__FILE__));
    sourceDir.cdUp(); // tools/manifoldbuilder
    sourceDir.cdUp(); // tools
    sourceDir.cdUp(); // repo
    return sourceDir.absolutePath();
}

QString MainWindow::detectQtRoot() const
{
    const QString prefix = QLibraryInfo::location(QLibraryInfo::PrefixPath);
    QDir dir(prefix);
    for (int i = 0; i < 4; ++i) {
        if (dir.exists(QStringLiteral("Tools/CMake_64/bin/cmake.exe"))
            && (dir.exists(QStringLiteral("Tools/mingw810_32/bin/g++.exe"))
                || dir.exists(QStringLiteral("Tools/mingw810_64/bin/g++.exe"))))
            return dir.absolutePath();
        dir.cdUp();
    }

    for (const QString &candidate : {QStringLiteral("E:/Qt"), QStringLiteral("C:/Qt")}) {
        if (QDir(candidate).exists())
            return candidate;
    }
    return QString();
}

void MainWindow::refreshDerivedPaths()
{
    const QString repo = cleanPath(m_repoRoot->text());
    const QString arch = selectedArch();
    m_scriptPath->setText(nativePath(repo + QStringLiteral("/scripts/build-manifold.ps1")));
    m_buildDir->setText(nativePath(buildDir()));
    m_outputLibrary->setText(nativePath(outputLibraryPath()));
}

QString MainWindow::selectedArch() const
{
    return m_arch->currentData().toString();
}

QString MainWindow::buildDir() const
{
    return cleanPath(m_repoRoot->text()) + QStringLiteral("/build/manifold-build-") + selectedArch();
}

QString MainWindow::outputLibraryPath() const
{
    return buildDir() + QStringLiteral("/src/libmanifold.a");
}

void MainWindow::browseRepoRoot()
{
    const QString dir = QFileDialog::getExistingDirectory(this,
                                                          QStringLiteral("Select repository root"),
                                                          cleanPath(m_repoRoot->text()));
    if (!dir.isEmpty())
        m_repoRoot->setText(nativePath(dir));
}

void MainWindow::browseQtRoot()
{
    const QString dir = QFileDialog::getExistingDirectory(this,
                                                          QStringLiteral("Select Qt root"),
                                                          cleanPath(m_qtRoot->text()));
    if (!dir.isEmpty())
        m_qtRoot->setText(nativePath(dir));
}

bool MainWindow::validateInputs()
{
    const QString repo = cleanPath(m_repoRoot->text());
    const QString qtRoot = cleanPath(m_qtRoot->text());
    const QString arch = selectedArch();

    if (!QFile::exists(repo + QStringLiteral("/scripts/build-manifold.ps1"))) {
        QMessageBox::warning(this,
                             QStringLiteral("Missing script"),
                             QStringLiteral("build-manifold.ps1 was not found in:\n%1").arg(nativePath(repo + QStringLiteral("/scripts"))));
        return false;
    }

    if (!QFile::exists(qtRoot + QStringLiteral("/Tools/CMake_64/bin/cmake.exe"))) {
        QMessageBox::warning(this,
                             QStringLiteral("Missing CMake"),
                             QStringLiteral("CMake was not found in:\n%1").arg(nativePath(qtRoot + QStringLiteral("/Tools/CMake_64/bin"))));
        return false;
    }

    if (!QFile::exists(qtRoot + QStringLiteral("/Tools/mingw810_%1/bin/g++.exe").arg(arch))) {
        QMessageBox::warning(this,
                             QStringLiteral("Missing MinGW"),
                             QStringLiteral("g++.exe was not found in:\n%1").arg(nativePath(qtRoot + QStringLiteral("/Tools/mingw810_%1/bin").arg(arch))));
        return false;
    }

    return true;
}

QString MainWindow::powerShellProgram() const
{
    return QStringLiteral("powershell.exe");
}

void MainWindow::onBuildClicked()
{
    refreshDerivedPaths();
    if (!validateInputs())
        return;

    const QString repo = cleanPath(m_repoRoot->text());
    const QString qtRoot = cleanPath(m_qtRoot->text());
    const QString script = repo + QStringLiteral("/scripts/build-manifold.ps1");
    const QString arch = selectedArch();

    if (m_cleanBuildDir->isChecked()) {
        QDir dir(buildDir());
        if (dir.exists()) {
            appendLog(QStringLiteral("Cleaning build directory: %1\n").arg(nativePath(dir.absolutePath())));
            if (!dir.removeRecursively()) {
                QMessageBox::warning(this,
                                     QStringLiteral("Clean failed"),
                                     QStringLiteral("Could not remove:\n%1").arg(nativePath(dir.absolutePath())));
                return;
            }
        }
    }

    m_log->clear();
    appendLog(QStringLiteral("Repo root: %1\n").arg(nativePath(repo)));
    appendLog(QStringLiteral("Qt root:   %1\n").arg(nativePath(qtRoot)));
    appendLog(QStringLiteral("Arch:      %1-bit\n\n").arg(arch));

    QStringList args;
    args << QStringLiteral("-NoProfile")
         << QStringLiteral("-ExecutionPolicy")
         << QStringLiteral("Bypass")
         << QStringLiteral("-File")
         << nativePath(script)
         << QStringLiteral("-QtRoot")
         << nativePath(qtRoot)
         << QStringLiteral("-Arch")
         << arch
         << QStringLiteral("-Generator")
         << m_generator->currentText();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString pathPrefix = qtRoot + QStringLiteral("/Tools/mingw810_%1/bin;%2/Tools/CMake_64/bin")
                                   .arg(arch, qtRoot);
    env.insert(QStringLiteral("PATH"), nativePath(pathPrefix) + QStringLiteral(";") + env.value(QStringLiteral("PATH")));
    m_process->setProcessEnvironment(env);
    m_process->setWorkingDirectory(repo);

    setBuildRunning(true);
    m_status->setText(QStringLiteral("Building Manifold..."));
    m_process->start(powerShellProgram(), args);
    if (!m_process->waitForStarted(3000)) {
        appendLog(QStringLiteral("ERROR: Could not start PowerShell.\n"));
        setBuildRunning(false);
        m_status->setText(QStringLiteral("Failed to start PowerShell"));
    }
}

void MainWindow::onStopClicked()
{
    if (m_process->state() != QProcess::NotRunning) {
        appendLog(QStringLiteral("\nStopping build...\n"));
        m_process->kill();
    }
}

void MainWindow::onReadyRead()
{
    const QByteArray data = m_process->readAllStandardOutput();
    if (!data.isEmpty())
        appendLog(QString::fromLocal8Bit(data));
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    onReadyRead();
    setBuildRunning(false);

    const bool ok = exitStatus == QProcess::NormalExit
                    && exitCode == 0
                    && QFile::exists(outputLibraryPath());
    if (ok) {
        appendLog(QStringLiteral("\nDone: %1\n").arg(nativePath(outputLibraryPath())));
        m_status->setText(QStringLiteral("Build finished"));
    } else {
        appendLog(QStringLiteral("\nBuild failed. Exit code: %1\n").arg(exitCode));
        m_status->setText(QStringLiteral("Build failed"));
    }
}

void MainWindow::setBuildRunning(bool running)
{
    m_buildButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
    m_repoRoot->setEnabled(!running);
    m_qtRoot->setEnabled(!running);
    m_arch->setEnabled(!running);
    m_generator->setEnabled(!running);
    m_cleanBuildDir->setEnabled(!running);
}

void MainWindow::appendLog(const QString &text)
{
    m_log->moveCursor(QTextCursor::End);
    m_log->insertPlainText(text);
    m_log->moveCursor(QTextCursor::End);
    if (QScrollBar *bar = m_log->verticalScrollBar())
        bar->setValue(bar->maximum());
}
