#include "App.h"

#include "core/Document.h"
#include "core/Preferences.h"
#include "core/ScreenCapture.h"
#include "core/Encoding.h"
#include "core/ScreenRecorder.h"
#include "core/VideoExporter.h"
#include "core/WindowFinder.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>

namespace {
App *s_instance = nullptr;
}

App::App(QQmlEngine *engine, QObject *parent)
    : QObject(parent), m_engine(engine), m_recorder(new ScreenRecorder(this))
{
    s_instance = this;
    connect(m_recorder, &ScreenRecorder::stateChanged, this, &App::updateTrayForRecording);
    connect(m_recorder, &ScreenRecorder::finished, this, [this](const QString &path) {
        if (m_recordingControls)
            m_recordingControls->close();
        if (Preferences::instance()->editAfterRecording()) {
            openVideoEditor(path, m_recorder->firstFrame(), true);
            return;
        }
        if (Preferences::instance()->copyPathAfterSave())
            QGuiApplication::clipboard()->setText(path);
        notify(tr("Recording saved"), QFileInfo(path).fileName());
    });
    connect(m_recorder, &ScreenRecorder::errorOccurred, this,
            [this](const QString &message) { notify(tr("Recording failed"), message); });
    connect(m_recorder, &ScreenRecorder::notice, this,
            [this](const QString &message) { notify(tr("Recording"), message); });
}

App::~App()
{
    s_instance = nullptr;
}

App *App::instance()
{
    return s_instance;
}

App *App::create(QQmlEngine *, QJSEngine *)
{
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

void App::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning("No system tray: start captures with 'guckste gui'");
        return;
    }
    m_trayMenu = std::make_unique<QMenu>();
    auto *takeShot = m_trayMenu->addAction(tr("Take Screenshot"));
    connect(takeShot, &QAction::triggered, this, &App::capture);
    auto *full = m_trayMenu->addAction(tr("Capture All Screens"));
    connect(full, &QAction::triggered, this, &App::captureFull);
    m_recordAction = m_trayMenu->addAction(tr("Record Screen…"));
    connect(m_recordAction, &QAction::triggered, this, &App::toggleRecording);
    auto *open = m_trayMenu->addAction(tr("Open Image…"));
    connect(open, &QAction::triggered, this, &App::openImage);
    m_trayMenu->addSeparator();
    if (Preferences::instance()->launchAtLoginSupported()) {
        m_launchAtLogin = m_trayMenu->addAction(tr("Launch at Login"));
        m_launchAtLogin->setCheckable(true);
        m_launchAtLogin->setChecked(Preferences::instance()->launchAtLogin());
        connect(m_launchAtLogin, &QAction::toggled, Preferences::instance(), &Preferences::setLaunchAtLogin);
        connect(Preferences::instance(), &Preferences::changed, this, [this] {
            m_launchAtLogin->setChecked(Preferences::instance()->launchAtLogin());
        });
    }
    auto *settings = m_trayMenu->addAction(tr("Settings…"));
    connect(settings, &QAction::triggered, this, &App::openSettings);
    m_trayMenu->addSeparator();
    auto *quit = m_trayMenu->addAction(tr("Quit"));
    connect(quit, &QAction::triggered, this, &App::quit);

    m_tray = std::make_unique<QSystemTrayIcon>(QIcon(QStringLiteral(":/qt/qml/Guckste/logo/guckste.svg")));
    m_tray->setToolTip(QStringLiteral("Guckste"));
    m_tray->setContextMenu(m_trayMenu.get());
    connect(m_tray.get(), &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            capture();
    });
    m_tray->show();
}

void App::handleCommand(const QString &command)
{
    const QString verb = command.section(u' ', 0, 0);
    const QString argument = command.section(u' ', 1).trimmed();
    if (verb == QStringLiteral("gui")) {
        capture();
    } else if (verb == QStringLiteral("full")) {
        captureFull();
    } else if (verb == QStringLiteral("open")) {
        if (argument.isEmpty())
            openImage();
        else
            openEditor(argument);
    } else if (verb == QStringLiteral("record")) {
        toggleRecording();
    } else if (verb == QStringLiteral("settings")) {
        openSettings();
    } else if (verb == QStringLiteral("quit")) {
        quit();
    } else if (!verb.isEmpty()) {
        qWarning("Unknown command: %s", qPrintable(command));
    }
}

QQuickWindow *App::createWindow(const QString &type, const QVariantMap &properties)
{
    QQmlComponent component(m_engine);
    component.loadFromModule(QStringLiteral("Guckste"), type);
    if (component.isError()) {
        for (const QQmlError &error : component.errors())
            qWarning("%s", qPrintable(error.toString()));
        return nullptr;
    }
    QObject *object = component.createWithInitialProperties(properties);
    if (component.isError()) {
        for (const QQmlError &error : component.errors())
            qWarning("%s", qPrintable(error.toString()));
        delete object;
        return nullptr;
    }
    auto *window = qobject_cast<QQuickWindow *>(object);
    if (!window) {
        qWarning("%s is not a window", qPrintable(type));
        delete object;
        return nullptr;
    }
    // Closing a window is the end of it.
    connect(window, &QWindow::visibleChanged, window, [window](bool visible) {
        if (!visible)
            window->deleteLater();
    });
    return window;
}

void App::capture()
{
    openOverlay(false);
}

void App::captureForRecording()
{
    openOverlay(true);
}

void App::openOverlay(bool recordMode)
{
    if (m_overlay) {
        m_overlay->setProperty("recordMode", recordMode);
        m_overlay->requestActivate();
        return;
    }
    // The tray menu may still be on screen; let it go first.
    QTimer::singleShot(150, this, [this, recordMode] {
        ScreenCapture::grab(this, Preferences::instance()->includeCursor(), [this, recordMode](const Capture &capture) {
            if (!capture.ok) {
                notify(tr("Screenshot failed"), tr("The screen could not be captured."));
                return;
            }
            auto *document = new Document;
            document->setImage(capture.image);
            // The windows on screen, for picking one: as rects in capture
            // coordinates, front to back.
            QVariantList windows;
            for (const QRect &w : WindowFinder::windows())
                windows << QRectF(w.translated(-capture.geometry.topLeft()));
            QQuickWindow *overlay = createWindow(QStringLiteral("CaptureOverlay"),
                                                 {{QStringLiteral("document"), QVariant::fromValue(document)},
                                                  {QStringLiteral("screenGeometry"), capture.geometry},
                                                  {QStringLiteral("recordMode"), recordMode},
                                                  {QStringLiteral("windowRects"), windows}});
            if (!overlay) {
                delete document;
                return;
            }
            document->setParent(overlay);
            m_overlay = overlay;
            connect(overlay, &QObject::destroyed, this, &App::capturingChanged);
            emit capturingChanged();
            overlay->show();
            overlay->raise();
            overlay->requestActivate();
        });
    });
}

void App::captureFull()
{
    QTimer::singleShot(150, this, [this] {
        ScreenCapture::grab(this, Preferences::instance()->includeCursor(), [this](const Capture &capture) {
            if (!capture.ok) {
                notify(tr("Screenshot failed"), tr("The screen could not be captured."));
                return;
            }
            Document document;
            document.setImage(capture.image);
            document.copyToClipboard(1);
            const QString path = Preferences::instance()->nextSavePath();
            if (document.save(path, 1))
                notify(tr("Screenshot saved"), QFileInfo(path).fileName());
            else
                notify(tr("Screenshot copied"), tr("Could not save to %1").arg(path));
        });
    });
}

void App::openImage()
{
    const QString start = Preferences::instance()->saveDirectory();
    const QString path = QFileDialog::getOpenFileName(
        nullptr, tr("Open Image"), QFileInfo::exists(start) ? start : QString(),
        tr("Images and videos (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.mp4 *.webm *.mkv *.avi *.mov);;All files (*)"));
    if (!path.isEmpty())
        openEditor(path);
}

bool App::openEditor(const QString &path)
{
    if (isVideoFile(path)) {
        if (!QFileInfo::exists(path)) {
            notify(tr("Could not open video"), path);
            return false;
        }
        openVideoEditor(path, QImage(), false);
        return true;
    }
    auto *document = new Document;
    if (!document->load(path)) {
        delete document;
        notify(tr("Could not open image"), path);
        return false;
    }
    showEditor(document);
    return true;
}

void App::editInWindow(Document *document, const QRectF &region)
{
    if (!document)
        return;
    document->crop(region);
    // Out of the overlay, which is about to close and would delete it.
    document->setParent(nullptr);
    showEditor(document);
    closeOverlay();
}

void App::showEditor(Document *document)
{
    QQuickWindow *editor = createWindow(QStringLiteral("EditorWindow"),
                                        {{QStringLiteral("document"), QVariant::fromValue(document)}});
    if (!editor) {
        delete document;
        return;
    }
    document->setParent(editor);
    m_editors.append(editor);
    m_editors.removeAll(nullptr);
    editor->show();
    editor->raise();
    editor->requestActivate();
}

void App::startRecording(const QRectF &region)
{
    if (m_recorder->state() != ScreenRecorder::Idle) {
        notify(tr("Already recording"), tr("Stop the recording first."));
        return;
    }
    closeOverlay();
    const QRect rect = region.toAlignedRect();
    // The overlay has to be off the screen before the first frame.
    QTimer::singleShot(300, this, [this, rect] {
        Preferences *prefs = Preferences::instance();
        // To the editor first, or straight into the folder.
        QString path = prefs->nextRecordingPath(recordingSuffix());
        if (prefs->editAfterRecording()) {
            const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                                + QStringLiteral("/guckste-recordings");
            path = dir + u'/' + QFileInfo(path).fileName();
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        ScreenRecorder::Options options;
        options.frameRate = prefs->frameRate();
        options.quality = prefs->recordingQuality();
        options.microphone = prefs->recordMicrophone();
        options.includeCursor = prefs->includeCursor();
        options.showKeys = prefs->showKeys();
        if (!m_recorder->start(rect, path, options))
            return;
        m_recordingControls = createWindow(QStringLiteral("RecordingControls"),
                                           {{QStringLiteral("recorder"), QVariant::fromValue(m_recorder)}});
        if (m_recordingControls)
            m_recordingControls->show();
        if (m_recorder->wholeScreen())
            notify(tr("Recording the screen"), tr("Stop it from the tray menu or with “guckste record”."));
    });
}

void App::stopRecording()
{
    m_recorder->stop();
}

void App::toggleRecording()
{
    if (m_recorder->state() == ScreenRecorder::Idle)
        captureForRecording();
    else
        stopRecording();
}

QString App::recordingSuffix() const
{
    return Encoding::suffixFor(Encoding::bestFormat());
}

void App::exportRecording(VideoExporter *exporter, const QString &source, const QString &target, Document *document,
                          int startMs, int endMs, bool moveSource)
{
    if (!exporter || !document)
        return;
    VideoExporter::Job job;
    job.moveSource = moveSource;
    job.source = source;
    job.target = target;
    job.startMs = startMs;
    job.endMs = endMs;
    job.annotations = document->annotations();
    job.quality = Preferences::instance()->recordingQuality();
    exporter->start(job);
}

bool App::isVideoFile(const QString &path)
{
    static const QStringList suffixes{QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mkv"),
                                      QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("m4v")};
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

void App::openVideoEditor(const QString &path, const QImage &preview, bool temporary)
{
    auto *document = new Document;
    document->setImage(preview);
    auto *exporter = new VideoExporter;
    QQuickWindow *editor = createWindow(QStringLiteral("VideoEditorWindow"),
                                        {{QStringLiteral("document"), QVariant::fromValue(document)},
                                         {QStringLiteral("sourcePath"), path},
                                         {QStringLiteral("exporter"), QVariant::fromValue(exporter)},
                                         {QStringLiteral("temporary"), temporary}});
    if (!editor) {
        delete document;
        delete exporter;
        if (temporary)
            notify(tr("Recording saved"), path);
        return;
    }
    document->setParent(editor);
    exporter->setParent(editor);
    // A recording that was never saved is not kept.
    if (temporary) {
        connect(editor, &QWindow::visibleChanged, editor, [editor, path](bool visible) {
            if (!visible && !editor->property("saved").toBool())
                QFile::remove(path);
        });
    }
    m_editors.append(editor);
    m_editors.removeAll(nullptr);
    editor->show();
    editor->raise();
    editor->requestActivate();
}

void App::updateTrayForRecording()
{
    const bool recording = m_recorder->state() != ScreenRecorder::Idle;
    if (m_recordAction)
        m_recordAction->setText(recording ? tr("Stop Recording") : tr("Record Screen…"));
    if (!m_tray)
        return;
    QIcon icon(QStringLiteral(":/qt/qml/Guckste/logo/guckste.svg"));
    if (recording) {
        // The logo with a red dot in its corner.
        QPixmap pixmap = icon.pixmap(64, 64);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::white, 3));
        painter.setBrush(QColor(255, 59, 48));
        painter.drawEllipse(QRect(36, 36, 24, 24));
        painter.end();
        icon = QIcon(pixmap);
    }
    m_tray->setIcon(icon);
    m_tray->setToolTip(recording ? tr("Guckste — recording") : QStringLiteral("Guckste"));
}

void App::openSettings()
{
    if (m_settings) {
        m_settings->raise();
        m_settings->requestActivate();
        return;
    }
    m_settings = createWindow(QStringLiteral("SettingsWindow"), {});
    if (m_settings)
        m_settings->show();
}

void App::closeOverlay()
{
    if (m_overlay)
        m_overlay->close();
}

void App::notify(const QString &title, const QString &text)
{
    if (m_tray && m_tray->isVisible())
        m_tray->showMessage(title, text, QSystemTrayIcon::Information, 3000);
    else
        qInfo("%s: %s", qPrintable(title), qPrintable(text));
}

void App::quit()
{
    QApplication::quit();
}
