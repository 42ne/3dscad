#include "mainwindow.h"

#include <QApplication>
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
#include <QThread>
#include <QVBoxLayout>

// ── BuildWorker ───────────────────────────────────────────────────────────────

bool BuildWorker::runProcess(const QString &program, const QStringList &args,
                             const QString &extraPath)
{
    QProcess proc;

    if (!extraPath.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("PATH", extraPath + ";" + env.value("PATH"));
        proc.setProcessEnvironment(env);
    }

    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(program, args);

    if (!proc.waitForStarted(5000)) {
        emit log("  [error] Failed to start: " + program);
        return false;
    }

    while (!proc.waitForFinished(200)) {
        const QByteArray data = proc.readAll();
        if (!data.isEmpty())
            emit log(QString::fromLocal8Bit(data));
    }

    const QByteArray remaining = proc.readAll();
    if (!remaining.isEmpty())
        emit log(QString::fromLocal8Bit(remaining));

    return proc.exitCode() == 0;
}

bool BuildWorker::copyDirRecursive(const QDir &src, const QDir &dst)
{
    bool ok = true;
    const auto entries = src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        if (fi.isDir()) {
            QDir subDst(dst.absoluteFilePath(fi.fileName()));
            QDir().mkpath(subDst.absolutePath());
            ok &= copyDirRecursive(QDir(fi.absoluteFilePath()), subDst);
        } else {
            QString dest = dst.absoluteFilePath(fi.fileName());
            QFile::remove(dest);
            ok &= QFile::copy(fi.absoluteFilePath(), dest);
        }
    }
    return ok;
}

void BuildWorker::run()
{
    const QString exeName  = QFileInfo(sourceExe).fileName();
    const QString appBase  = QFileInfo(sourceExe).completeBaseName();
    const QString stageDir = outputDir + "/_stage";
    const QString archFile = outputDir + "/_tmp.7z";
    const QString outExe   = outputDir + "/" + appBase + "_portable.exe";
    const QString sfxPath  = sevenZipDir + "/7z.sfx";
    const QString sevenExe = sevenZipDir + "/7z.exe";

    auto fail = [&](const QString &msg) {
        emit log("\nERROR: " + msg);
        emit finished(false, {});
    };

    // ── Staging dir ──────────────────────────────────────────────────────────
    emit log("==> Preparing staging directory");
    QDir sd(stageDir);
    if (sd.exists()) sd.removeRecursively();
    QDir().mkpath(stageDir);

    if (!QFile::copy(sourceExe, stageDir + "/" + exeName)) {
        fail("Cannot copy source exe to staging dir");
        return;
    }
    emit log("  Copied " + exeName);

    // ── windeployqt ──────────────────────────────────────────────────────────
    emit log("\n==> Running windeployqt");
    const QString wdqPath = qtBinDir + "/windeployqt.exe";
    const QStringList wdqArgs = {
        "--no-translations",
        "--no-system-d3d-compiler",
        "--no-opengl-sw",
        stageDir + "/" + exeName
    };
    if (!runProcess(wdqPath, wdqArgs, qtBinDir)) {
        fail("windeployqt failed");
        return;
    }

    // ── Extra files (optional) ───────────────────────────────────────────────
    if (!extraSrcFolder.isEmpty() && QDir(extraSrcFolder).exists()) {
        emit log("\n==> Copying extra files from: " + extraSrcFolder);
        QString relPath = extraBundlePath.trimmed();
        if (relPath.isEmpty())
            relPath = QFileInfo(extraSrcFolder).fileName();
        const QString destPath = stageDir + "/" + relPath;
        QDir().mkpath(destPath);
        if (!copyDirRecursive(QDir(extraSrcFolder), QDir(destPath)))
            emit log("  Warning: some extra files could not be copied");
        else
            emit log("  Copied to bundle path: " + relPath);
    }

    // ── 7z archive ───────────────────────────────────────────────────────────
    emit log("\n==> Creating 7z archive");
    QFile::remove(archFile);
    const QStringList archArgs = {"a", "-mx=9", "-mmt=on", archFile, stageDir + "/*"};
    if (!runProcess(sevenExe, archArgs)) {
        fail("7-Zip archive creation failed");
        return;
    }

    // ── Assemble SFX ─────────────────────────────────────────────────────────
    emit log("\n==> Assembling self-extracting exe");

    if (!QFile::exists(sfxPath)) { fail("7z.sfx not found: " + sfxPath); return; }
    if (!QFile::exists(archFile)) { fail("Archive not found: " + archFile); return; }

    const QString sfxConfig =
        ";!@Install@!UTF-8!\n"
        "Title=\"" + appBase + " Portable\"\n"
        "RunProgram=\"" + exeName + "\"\n"
        ";!@InstallEnd@!";

    QFile::remove(outExe);
    QFile out(outExe);
    if (!out.open(QIODevice::WriteOnly)) { fail("Cannot create: " + outExe); return; }

    { QFile f(sfxPath);  f.open(QIODevice::ReadOnly); out.write(f.readAll()); }
    out.write(sfxConfig.toUtf8());
    { QFile f(archFile); f.open(QIODevice::ReadOnly); out.write(f.readAll()); }
    out.close();

    // ── Cleanup ───────────────────────────────────────────────────────────────
    QDir(stageDir).removeRecursively();
    QFile::remove(archFile);

    const double sizeMB = QFileInfo(outExe).size() / 1048576.0;
    emit log(QString("\nDone!  %1  (%2 MB)").arg(outExe).arg(sizeMB, 0, 'f', 1));
    emit finished(true, outExe);
}

// ── MainWindow ────────────────────────────────────────────────────────────────

static QLineEdit *makeField(const QString &placeholder = {})
{
    auto *e = new QLineEdit;
    if (!placeholder.isEmpty()) e->setPlaceholderText(placeholder);
    return e;
}

static QPushButton *makeBrowse() { return new QPushButton("..."); }

static QString findDir(const QStringList &candidates)
{
    for (const QString &c : candidates)
        if (QDir(c).exists()) return c;
    return {};
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("SFX Builder");
    setMinimumWidth(620);

    auto *central = new QWidget;
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setSpacing(8);

    // ── Form ─────────────────────────────────────────────────────────────────
    auto *form = new QGroupBox("Configuration");
    auto *fl = new QFormLayout(form);
    fl->setLabelAlignment(Qt::AlignRight);

    auto addRow = [&](const QString &label, QLineEdit *&field,
                      QPushButton *&btn, const QString &placeholder = {}) {
        field = makeField(placeholder);
        btn   = makeBrowse();
        btn->setFixedWidth(32);
        auto *row = new QHBoxLayout;
        row->addWidget(field);
        row->addWidget(btn);
        fl->addRow(label, row);
    };

    QPushButton *bSrc, *bOut, *bQt, *bSfx, *bExtra;
    addRow("Source EXE:",   m_sourceExe,      bSrc);
    addRow("Output folder:", m_outputDir,     bOut,  "same as source exe");
    addRow("Qt bin dir:",   m_qtBinDir,       bQt);
    addRow("7-Zip dir:",    m_sevenZipDir,    bSfx);

    fl->addRow(new QLabel);  // spacer

    m_extraSrcFolder  = makeField("optional — folder to bundle with the app");
    m_extraBundlePath = makeField("relative path inside bundle, e.g. docs/sample_codes");
    bExtra = makeBrowse();
    bExtra->setFixedWidth(32);

    auto *extraRow = new QHBoxLayout;
    extraRow->addWidget(m_extraSrcFolder);
    extraRow->addWidget(bExtra);
    fl->addRow("Extra folder:", extraRow);
    fl->addRow("Bundle path:",  m_extraBundlePath);

    root->addWidget(form);

    // ── Build button ──────────────────────────────────────────────────────────
    m_buildBtn = new QPushButton("Build SFX");
    m_buildBtn->setFixedHeight(36);
    QFont f = m_buildBtn->font();
    f.setBold(true);
    m_buildBtn->setFont(f);
    root->addWidget(m_buildBtn);

    // ── Log ───────────────────────────────────────────────────────────────────
    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setFontFamily("Consolas");
    m_log->setMinimumHeight(200);
    root->addWidget(m_log, 1);

    // ── Auto-detect & connect ─────────────────────────────────────────────────
    autoDetect();

    connect(bSrc,   &QPushButton::clicked, this, &MainWindow::browseSourceExe);
    connect(bOut,   &QPushButton::clicked, this, &MainWindow::browseOutputDir);
    connect(bQt,    &QPushButton::clicked, this, &MainWindow::browseQtBin);
    connect(bSfx,   &QPushButton::clicked, this, &MainWindow::browseSevenZip);
    connect(bExtra, &QPushButton::clicked, this, &MainWindow::browseExtraFolder);
    connect(m_buildBtn, &QPushButton::clicked, this, &MainWindow::onBuildClicked);
}

MainWindow::~MainWindow()
{
    if (m_thread) { m_thread->quit(); m_thread->wait(); }
}

void MainWindow::autoDetect()
{
    // Qt bin — use the installation this app was compiled with
    const QString qtBin = QLibraryInfo::location(QLibraryInfo::BinariesPath);
    if (QFile::exists(qtBin + "/windeployqt.exe"))
        m_qtBinDir->setText(QDir::toNativeSeparators(qtBin));

    // 7-Zip
    const QString sfxDir = findDir({
        "C:/Program Files/7-Zip",
        "C:/Program Files (x86)/7-Zip"
    });
    if (!sfxDir.isEmpty())
        m_sevenZipDir->setText(QDir::toNativeSeparators(sfxDir));
}

// ── Browse slots ──────────────────────────────────────────────────────────────

void MainWindow::browseSourceExe()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Select compiled EXE", m_sourceExe->text(), "Executables (*.exe)");
    if (path.isEmpty()) return;
    m_sourceExe->setText(QDir::toNativeSeparators(path));

    // Auto-fill output dir
    if (m_outputDir->text().isEmpty())
        m_outputDir->setText(QDir::toNativeSeparators(QFileInfo(path).absolutePath() + "/dist"));

    // Auto-fill extra folder if docs/sample_codes found near the exe
    if (m_extraSrcFolder->text().isEmpty()) {
        QDir dir(QFileInfo(path).absoluteDir());
        for (int i = 0; i < 6; ++i) {
            const QString candidate = dir.absoluteFilePath("docs/sample_codes");
            if (QDir(candidate).exists()) {
                m_extraSrcFolder->setText(QDir::toNativeSeparators(candidate));
                m_extraBundlePath->setText("docs/sample_codes");
                break;
            }
            if (!dir.cdUp()) break;
        }
    }
}

void MainWindow::browseOutputDir()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, "Select output folder", m_outputDir->text());
    if (!path.isEmpty())
        m_outputDir->setText(QDir::toNativeSeparators(path));
}

void MainWindow::browseQtBin()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, "Select Qt bin directory", m_qtBinDir->text());
    if (!path.isEmpty())
        m_qtBinDir->setText(QDir::toNativeSeparators(path));
}

void MainWindow::browseSevenZip()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, "Select 7-Zip directory", m_sevenZipDir->text());
    if (!path.isEmpty())
        m_sevenZipDir->setText(QDir::toNativeSeparators(path));
}

void MainWindow::browseExtraFolder()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, "Select extra folder to bundle", m_extraSrcFolder->text());
    if (!path.isEmpty())
        m_extraSrcFolder->setText(QDir::toNativeSeparators(path));
}

// ── Build ─────────────────────────────────────────────────────────────────────

void MainWindow::onBuildClicked()
{
    const QString srcExe   = QDir::fromNativeSeparators(m_sourceExe->text().trimmed());
    const QString outDir   = QDir::fromNativeSeparators(m_outputDir->text().trimmed());
    const QString qtBin    = QDir::fromNativeSeparators(m_qtBinDir->text().trimmed());
    const QString sfxDir   = QDir::fromNativeSeparators(m_sevenZipDir->text().trimmed());

    if (!QFile::exists(srcExe)) {
        QMessageBox::warning(this, "Missing input", "Source EXE not found:\n" + srcExe);
        return;
    }
    if (!QFile::exists(qtBin + "/windeployqt.exe")) {
        QMessageBox::warning(this, "Missing tool", "windeployqt.exe not found in:\n" + qtBin);
        return;
    }
    if (!QFile::exists(sfxDir + "/7z.exe")) {
        QMessageBox::warning(this, "Missing tool", "7z.exe not found in:\n" + sfxDir);
        return;
    }

    QDir().mkpath(outDir);

    m_log->clear();
    setUiEnabled(false);

    m_worker = new BuildWorker;
    m_worker->sourceExe      = srcExe;
    m_worker->outputDir      = outDir;
    m_worker->qtBinDir       = qtBin;
    m_worker->sevenZipDir    = sfxDir;
    m_worker->extraSrcFolder = QDir::fromNativeSeparators(m_extraSrcFolder->text().trimmed());
    m_worker->extraBundlePath = m_extraBundlePath->text().trimmed();

    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started,          m_worker, &BuildWorker::run);
    connect(m_worker, &BuildWorker::log,           this,     &MainWindow::appendLog);
    connect(m_worker, &BuildWorker::finished,      this,     &MainWindow::onBuildFinished);
    connect(m_worker, &BuildWorker::finished,      m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished,          m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished,          m_thread, &QObject::deleteLater);

    m_thread->start();
}

void MainWindow::appendLog(const QString &msg)
{
    m_log->moveCursor(QTextCursor::End);
    m_log->insertPlainText(msg);
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

void MainWindow::onBuildFinished(bool success, const QString &path)
{
    setUiEnabled(true);
    m_thread = nullptr;
    m_worker = nullptr;

    if (success) {
        QMessageBox::information(this, "Done",
            "Portable exe created:\n" + QDir::toNativeSeparators(path));
    } else {
        QMessageBox::critical(this, "Build failed",
            "Build failed. See the log for details.");
    }
}

void MainWindow::setUiEnabled(bool enabled)
{
    m_sourceExe->setEnabled(enabled);
    m_outputDir->setEnabled(enabled);
    m_qtBinDir->setEnabled(enabled);
    m_sevenZipDir->setEnabled(enabled);
    m_extraSrcFolder->setEnabled(enabled);
    m_extraBundlePath->setEnabled(enabled);
    m_buildBtn->setEnabled(enabled);
    m_buildBtn->setText(enabled ? "Build SFX" : "Building...");
}
