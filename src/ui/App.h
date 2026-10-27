#pragma once

#include "core/ScreenRecorder.h"
#include "core/VideoExporter.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

class Document;
#include <QQuickWindow>
class QSystemTrayIcon;
class QMenu;
class QAction;

// The application: the tray icon, the windows, and what the command line
// and the tray ask for.
class App : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)
    Q_PROPERTY(ScreenRecorder *recorder READ recorder CONSTANT)

public:
    explicit App(QQmlEngine *engine, QObject *parent = nullptr);
    ~App() override;

    static App *instance();
    static App *create(QQmlEngine *, QJSEngine *);

    void setupTray();
    // Calls `then` once the tray icon is on the panel, as far as can be
    // told.
    void whenTrayShown(std::function<void()> then);
    // Loads the overlay once while nothing else is going on.
    void warmUp();
    bool capturing() const { return m_overlay != nullptr; }
    ScreenRecorder *recorder() const { return m_recorder; }

    // "gui": pick a region; "full": all screens to clipboard and file;
    // "open <path>": the editor on a file; "open": an empty editor;
    // "record": pick a region to record, or stop the recording; "still":
    // keep the frame recorded now as a lossless still.
    void handleCommand(const QString &command);

    Q_INVOKABLE void capture();
    // Pick a region to record.
    Q_INVOKABLE void captureForRecording();
    // Counts down beside `region` (screen coordinates), then captures the
    // screens again and opens the overlay with the region selected.
    Q_INVOKABLE void captureLater(int seconds, const QRectF &region, bool recordMode);
    // Records a region in screen coordinates, once the overlay is gone.
    Q_INVOKABLE void startRecording(const QRectF &region);
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void toggleRecording();
    // The suffix of the video format this machine records.
    Q_INVOKABLE QString recordingSuffix() const;
    // Writes the document's clips, images and marks to `target` as a
    // video, from startMs to endMs of its time (-1: to the end); the
    // exporter reports progress and the result. `movable`: a temporary
    // recording, moved into place when the video is that clip as it is.
    Q_INVOKABLE void exportVideo(VideoExporter *exporter, const QString &target, Document *document, int startMs,
                                 int endMs, const QString &movable);
    // A new editor on what the document shows now, clips as images.
    Q_INVOKABLE void openFrame(Document *document);
    // A new editor on the stills from `fromMs` to `toMs`, as one image.
    Q_INVOKABLE void openStills(Document *document, int fromMs, int toMs);
    // The editor on a video, as a clip of its own, with the stills of its
    // recording; a temporary recording is deleted unless it is saved, its
    // stills with the window.
    void openVideoEditor(const QString &path, bool temporary, const QList<Still> &stills = {});
    Q_INVOKABLE bool isVideoFile(const QString &path) const;
    Q_INVOKABLE bool fileExists(const QString &path) const;
    Q_INVOKABLE void captureFull();
    // The editor with nothing in it yet, to open, paste or drop into.
    Q_INVOKABLE void openEmptyEditor();
    Q_INVOKABLE bool openEditor(const QString &path);
    // Hands the overlay's document, cropped to the region, to a new editor.
    Q_INVOKABLE void editInWindow(Document *document, const QRectF &region);
    // Captures a region to add to an editor's document: the editor gets
    // out of the way while the overlay is up.
    Q_INVOKABLE void addScreenshotTo(QQuickWindow *editor, Document *document);
    // From the overlay: its region goes into `target` as an image.
    Q_INVOKABLE void addToDocument(Document *target, Document *source, const QRectF &region);
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void closeOverlay();
    Q_INVOKABLE void notify(const QString &title, const QString &text);
    Q_INVOKABLE void quit();

signals:
    void capturingChanged();

private slots:
    void onCountdownElapsed();

private:
    // Says when a picture of stills holds only `taken` of `count`.
    void noticeSheet(int taken, int count);
    // Where recordings wait for the editor, each in a folder of its own.
    static QString temporaryRecordings();
    QQuickWindow *createWindow(const QString &type, const QVariantMap &properties);
    QQuickWindow *showEditor(Document *document, const QVariantMap &properties = {});
    void openOverlay(bool recordMode, const QRect &presetRegion = QRect(), int minimumWait = 0);
    void updateTrayForRecording();

    QQmlEngine *m_engine;
    std::unique_ptr<QSystemTrayIcon> m_tray;
    std::unique_ptr<QMenu> m_trayMenu;
    QElapsedTimer m_trayMenuClosed;
    QAction *m_launchAtLogin = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_stillAction = nullptr;
    ScreenRecorder *m_recorder = nullptr;
    QPointer<QQuickWindow> m_recordingControls;
    QPointer<QQuickWindow> m_countdown;
    bool m_countdownRecordMode = false;
    QPointer<QQuickWindow> m_overlay;
    // The editor a capture is for, and its document.
    QPointer<QQuickWindow> m_addEditor;
    QPointer<Document> m_addTarget;
    QPointer<QQuickWindow> m_settings;
    QList<QPointer<QQuickWindow>> m_editors;
};
