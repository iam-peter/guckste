#include "App.h"

#include "core/Document.h"
#include "core/Preferences.h"
#include "core/ScreenCapture.h"
#include "core/Encoding.h"
#include "core/FileNaming.h"
#include "core/ScreenRecorder.h"
#include "core/VideoExporter.h"
#include "core/VideoProbe.h"
#include "core/WindowFinder.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDeadlineTimer>
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

#ifdef GUCKSTE_HAVE_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#endif

namespace {
App *s_instance = nullptr;

#ifdef GUCKSTE_HAVE_DBUS
// Whether the StatusNotifierWatcher lists a tray icon of this process.
// Without a watcher to ask there is nothing to wait for either.
bool trayListed()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage get = QDBusMessage::createMethodCall(QStringLiteral("org.kde.StatusNotifierWatcher"),
                                                      QStringLiteral("/StatusNotifierWatcher"),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    get << QStringLiteral("org.kde.StatusNotifierWatcher") << QStringLiteral("RegisteredStatusNotifierItems");
    const QDBusReply<QDBusVariant> items = bus.call(get, QDBus::Block, 200);
    if (!items.isValid() || !bus.interface())
        return true;
    // Listed as "<bus name>/<path>"; Qt registers the icon from a
    // connection of its own, so its process tells it apart.
    for (const QString &item : qdbus_cast<QStringList>(items.value().variant())) {
        const QDBusReply<uint> pid = bus.interface()->servicePid(item.section(u'/', 0, 0));
        if (pid.isValid() && pid.value() == QCoreApplication::applicationPid())
            return true;
    }
    return false;
}
#endif
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
            openVideoEditor(path, true, m_recorder->stills());
            return;
        }
        if (Preferences::instance()->copyPathAfterSave())
            QGuiApplication::clipboard()->setText(path);
        notify(tr("Recording saved"), QFileInfo(path).fileName());
    });
    connect(m_recorder, &ScreenRecorder::stillsFinished, this, [this](const QString &recorded) {
        if (m_recordingControls)
            m_recordingControls->close();
        QList<Still> stills = m_recorder->stills();
        // A video for the editor that failed leaves its stills among the
        // temporary recordings: they go where recordings are kept.
        QString directory = recorded;
        if (directory.startsWith(temporaryRecordings() + u'/')) {
            directory = FileNaming::uniqueFolder(Preferences::instance()->saveDirectory(),
                                                 QFileInfo(recorded).fileName());
            QDir().mkpath(directory);
            for (Still &still : stills) {
                const QString moved = QDir(directory).filePath(QFileInfo(still.path).fileName());
                if (QFile::copy(still.path, moved))
                    still.path = moved;
            }
            QDir(recorded).removeRecursively();
        }
        if (Preferences::instance()->copyPathAfterSave())
            QGuiApplication::clipboard()->setText(directory);
        notify(tr("Stills saved"), tr("%n still(s) in %1", "", int(stills.size())).arg(QFileInfo(directory).fileName()));
        // In the editor as one picture, to go through the flow.
        if (Preferences::instance()->editAfterRecording()) {
            QList<std::pair<int, Still>> timed;
            for (const Still &still : std::as_const(stills))
                timed.append({still.ms, still});
            int taken = 0;
            showEditor(Document::sheetOf(timed, &taken));
            noticeSheet(taken, int(timed.size()));
        }
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
    // The menu may be drawn by the panel, over D-Bus, and close on its own
    // time with no aboutToHide here: picked from it, the capture waits.
    // One way in for screenshots and recordings: the overlay copies,
    // saves or records the region picked.
    auto *captureAction = m_trayMenu->addAction(tr("Capture Screen…"));
    connect(captureAction, &QAction::triggered, this, [this] {
        m_trayMenuClosed.start();
        capture();
    });
    // While recording, what a recording needs.
    m_stopAction = m_trayMenu->addAction(tr("Stop Recording"));
    m_stopAction->setVisible(false);
    connect(m_stopAction, &QAction::triggered, this, &App::stopRecording);
    m_stillAction = m_trayMenu->addAction(tr("Keep a Still"));
    m_stillAction->setVisible(false);
    connect(m_stillAction, &QAction::triggered, m_recorder, &ScreenRecorder::keepStill);
    auto *open = m_trayMenu->addAction(tr("Open Editor"));
    connect(open, &QAction::triggered, this, &App::openEmptyEditor);
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
    connect(m_trayMenu.get(), &QMenu::aboutToHide, this, [this] { m_trayMenuClosed.start(); });
    connect(m_tray.get(), &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            capture();
    });
    m_tray->show();
}

void App::whenTrayShown(std::function<void()> then)
{
#ifdef GUCKSTE_HAVE_DBUS
    // A capture right at start would freeze the screen before the panel
    // has drawn the icon. Once the watcher lists it, the panel asks this
    // process for it, so that takes the event loop running a little.
    if (m_tray && m_tray->isVisible()) {
        auto *poll = new QTimer(this);
        QDeadlineTimer deadline(2000);
        connect(poll, &QTimer::timeout, this, [this, poll, deadline, then = std::move(then)] {
            poll->setInterval(50);
            if (!deadline.hasExpired() && !trayListed())
                return;
            poll->deleteLater();
            QTimer::singleShot(150, this, then);
        });
        poll->start(0);
        return;
    }
#endif
    then();
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
            openEmptyEditor();
        else
            openEditor(argument);
    } else if (verb == QStringLiteral("record")) {
        toggleRecording();
    } else if (verb == QStringLiteral("still")) {
        m_recorder->keepStill();
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

// The overlay bypasses the window manager, so nothing keeps the keyboard
// on it: any window that asks for focus takes it, and Escape and the
// shortcuts go dead. Take focus back whenever it goes, and grab the
// keyboard as flameshot does. The grab fails while the hotkey that
// launched us is still held by the desktop, so it is retried for a while.
static void holdKeyboard(QQuickWindow *window)
{
    QObject::connect(window, &QWindow::activeChanged, window, [window] {
        if (window->isActive() || !window->isVisible())
            return;
        QTimer::singleShot(50, window, [window] {
            if (window->isVisible() && !window->isActive())
                WindowFinder::takeFocus(window);
        });
    });
    QObject::connect(window, &QWindow::visibleChanged, window, [window](bool visible) {
        if (!visible)
            window->setKeyboardGrabEnabled(false);
    });
    auto *retry = new QTimer(window);
    retry->setInterval(50);
    QObject::connect(retry, &QTimer::timeout, window, [window, retry, attempts = 0]() mutable {
        if (!window->isVisible() || window->setKeyboardGrabEnabled(true) || ++attempts >= 40)
            retry->stop();
    });
    retry->start();
}

void App::openOverlay(bool recordMode, const QRect &presetRegion, int minimumWait)
{
    if (m_overlay) {
        m_overlay->setProperty("recordMode", recordMode);
        m_overlay->requestActivate();
        return;
    }
    // Right after picking from the tray menu, the menu may still be on
    // screen; let it go first. From a shortcut there is nothing to wait for.
    constexpr int menuFadeMs = 300;
    int wait = 0;
    if (m_trayMenuClosed.isValid() && m_trayMenuClosed.elapsed() < menuFadeMs)
        wait = menuFadeMs - int(m_trayMenuClosed.elapsed());
    wait = qMax(wait, minimumWait);
    QTimer::singleShot(wait, this, [this, recordMode, presetRegion] {
        ScreenCapture::grab(this, Preferences::instance()->includeCursor(), [this, recordMode, presetRegion](const Capture &capture) {
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
            QRect pointerScreen;
            if (QScreen *screen = QGuiApplication::screenAt(QCursor::pos()))
                pointerScreen = screen->geometry().translated(-capture.geometry.topLeft());
            QQuickWindow *overlay = createWindow(QStringLiteral("CaptureOverlay"),
                                                 {{QStringLiteral("document"), QVariant::fromValue(document)},
                                                  {QStringLiteral("screenGeometry"), capture.geometry},
                                                  {QStringLiteral("recordMode"), recordMode},
                                                  {QStringLiteral("windowRects"), windows},
                                                  {QStringLiteral("pointerScreen"), pointerScreen},
                                                  {QStringLiteral("presetRegion"), QRectF(presetRegion)},
                                                  {QStringLiteral("addTarget"), QVariant::fromValue(m_addTarget.data())}});
            if (!overlay) {
                delete document;
                return;
            }
            document->setParent(overlay);
            m_overlay = overlay;
            connect(overlay, &QObject::destroyed, this, &App::capturingChanged);
            // The editor the capture was for comes back, added to or not.
            connect(overlay, &QObject::destroyed, this, [this] {
                if (QQuickWindow *editor = m_addEditor) {
                    editor->showNormal();
                    editor->raise();
                    editor->requestActivate();
                }
                m_addEditor = nullptr;
                m_addTarget = nullptr;
            });
            emit capturingChanged();
            overlay->show();
            overlay->raise();
            overlay->requestActivate();
            holdKeyboard(overlay);
        });
    });
}

void App::warmUp()
{
    // The first overlay spends most of its time loading QML; do that once
    // at startup, so the first capture opens as fast as the ones after it.
    auto *document = new Document;
    QImage image(16, 16, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    document->setImage(image);
    QQuickWindow *overlay = createWindow(QStringLiteral("CaptureOverlay"),
                                         {{QStringLiteral("document"), QVariant::fromValue(document)},
                                          {QStringLiteral("screenGeometry"), QRect(0, 0, 16, 16)}});
    delete overlay;
    delete document;
    m_recorder->prepare();
}

void App::captureLater(int seconds, const QRectF &region, bool recordMode)
{
    closeOverlay();
    if (m_countdown)
        m_countdown->close();
    const QRect rect = region.toAlignedRect();
    m_countdownRecordMode = recordMode;
    QQuickWindow *countdown = createWindow(QStringLiteral("Countdown"),
                                           {{QStringLiteral("region"), rect},
                                            {QStringLiteral("seconds"), qMax(1, seconds)}});
    if (!countdown)
        return;
    m_countdown = countdown;
    connect(countdown, SIGNAL(cancelled()), countdown, SLOT(close()));
    connect(countdown, SIGNAL(elapsed()), this, SLOT(onCountdownElapsed()));
    countdown->show();
}

void App::onCountdownElapsed()
{
    QRect region;
    bool recordMode = false;
    if (m_countdown) {
        region = m_countdown->property("region").toRect();
        m_countdown->close();
    }
    recordMode = m_countdownRecordMode;
    // The countdown has to be off the screen before the capture.
    QTimer::singleShot(100, this, [this, region, recordMode] { openOverlay(recordMode, region); });
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

void App::openEmptyEditor()
{
    showEditor(new Document);
}

bool App::openEditor(const QString &path)
{
    if (isVideoFile(path)) {
        if (!QFileInfo::exists(path)) {
            notify(tr("Could not open video"), path);
            return false;
        }
        openVideoEditor(path, false);
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

void App::addScreenshotTo(QQuickWindow *editor, Document *document)
{
    if (!editor || !document || m_overlay)
        return;
    m_addEditor = editor;
    m_addTarget = document;
    editor->showMinimized();
    // Long enough for the window manager to take the editor off screen.
    openOverlay(false, QRect(), 300);
}

void App::addToDocument(Document *target, Document *source, const QRectF &region)
{
    if (target && source) {
        const QImage image = source->render(1, region.toAlignedRect());
        if (target->addImage(image) >= 0 && m_addEditor) {
            m_addEditor->setProperty("tool", int(Document::Select));
            QMetaObject::invokeMethod(m_addEditor, "fitIfNeeded");
        }
    }
    closeOverlay();
}

QQuickWindow *App::showEditor(Document *document, const QVariantMap &properties)
{
    // The screenshot as a layer like any other, locked to begin with.
    document->liftBase(document->filePath().isEmpty() ? tr("Screenshot") : QFileInfo(document->filePath()).fileName());
    QVariantMap all = properties;
    all.insert(QStringLiteral("document"), QVariant::fromValue(document));
    QQuickWindow *editor = createWindow(QStringLiteral("EditorWindow"), all);
    if (!editor) {
        delete document;
        return nullptr;
    }
    document->setParent(editor);
    m_editors.append(editor);
    m_editors.removeAll(nullptr);
    editor->show();
    editor->raise();
    editor->requestActivate();
    return editor;
}

void App::startRecording(const QRectF &region)
{
    if (m_recorder->state() != ScreenRecorder::Idle) {
        notify(tr("Already recording"), tr("Stop the recording first."));
        return;
    }
    closeOverlay();
    const QRect rect = region.toAlignedRect();
    // The overlay goes first; the recorder then leaves its first moment
    // out, while what the overlay covered draws again.
    QTimer::singleShot(100, this, [this, rect] {
        Preferences *prefs = Preferences::instance();
        // To the editor first, or straight into the folder; either way in
        // a folder of its own, with its stills.
        QString path = prefs->nextRecordingPath(recordingSuffix());
        if (prefs->editAfterRecording() && !prefs->stillsOnly()) {
            const QFileInfo named(path);
            const QString folder = FileNaming::uniqueFolder(temporaryRecordings(), named.dir().dirName());
            path = folder + u'/' + named.fileName();
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        ScreenRecorder::Options options;
        options.frameRate = prefs->frameRate();
        options.quality = prefs->recordingQuality();
        options.microphone = prefs->recordMicrophone();
        options.includeCursor = prefs->includeCursor();
        options.showKeys = prefs->showKeys();
        options.stillsOnClicks = prefs->stillsOnClicks();
        options.stillsOnChanges = prefs->stillsOnChanges();
        options.video = !prefs->stillsOnly();
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

QString App::temporaryRecordings()
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/guckste-recordings");
}

QString App::recordingSuffix() const
{
    return Encoding::suffixFor(Encoding::bestFormat());
}

void App::exportVideo(VideoExporter *exporter, const QString &target, Document *document, int startMs, int endMs,
                      const QString &movable)
{
    if (!exporter || !document)
        return;
    VideoExporter::Job job;
    const QList<Annotation> &layers = document->annotations();
    job.moveSource = !movable.isEmpty() && layers.size() == 1 && layers.first().source == movable;
    job.target = target;
    job.startMs = startMs;
    job.endMs = endMs;
    job.layers = document->annotations();
    job.canvas = document->canvas();
    job.quality = Preferences::instance()->recordingQuality();
    exporter->start(job);
}

void App::openFrame(Document *document)
{
    if (document)
        showEditor(document->frameDocument());
}

void App::openStills(Document *document, int fromMs, int toMs)
{
    if (!document)
        return;
    const int count = document->stillCount(fromMs, toMs);
    if (count == 0)
        return;
    int taken = 0;
    showEditor(document->stillsDocument(fromMs, toMs, &taken));
    noticeSheet(taken, count);
}

void App::noticeSheet(int taken, int count)
{
    if (taken < count)
        notify(tr("Stills"), tr("The first %1 of %2 stills, as many as one picture holds; trim the timeline to "
                                "pick others.").arg(taken).arg(count));
}

bool App::fileExists(const QString &path) const
{
    return QFileInfo::exists(path);
}

bool App::isVideoFile(const QString &path) const
{
    static const QStringList suffixes{QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mkv"),
                                      QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("m4v")};
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

void App::openVideoEditor(const QString &path, bool temporary, const QList<Still> &stills)
{
    VideoProbe::probe(path, this, [this, path, temporary, stills](const VideoInfo &video) {
        auto *document = new Document;
        if (document->addVideo(video, Document::Placement::Base) < 0) {
            delete document;
            notify(temporary ? tr("Recording saved") : tr("Could not open video"), path);
            return;
        }
        document->addStills(path, stills);
        // The folder the recording is saved into, named as it is.
        const QString folder = QFileInfo(path).absolutePath();
        QQuickWindow *editor = showEditor(document, {{QStringLiteral("temporaryVideo"), temporary ? path : QString()},
                                                     {QStringLiteral("recordingName"),
                                                      temporary ? QFileInfo(folder).fileName() : QString()}});
        // A recording is not kept unless it was saved, which moves the
        // video and copies the stills into its folder; what is left of it
        // goes with the window.
        if (editor && temporary && folder.startsWith(temporaryRecordings() + u'/')) {
            connect(editor, &QWindow::visibleChanged, editor, [folder](bool visible) {
                if (!visible)
                    QDir(folder).removeRecursively();
            });
        }
    });
}

void App::updateTrayForRecording()
{
    const bool recording = m_recorder->state() != ScreenRecorder::Idle;
    if (m_stopAction)
        m_stopAction->setVisible(recording);
    if (m_stillAction)
        m_stillAction->setVisible(recording);
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
