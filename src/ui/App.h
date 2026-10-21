#pragma once

#include "core/ScreenRecorder.h"
#include "core/VideoExporter.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

#include <memory>

class Document;
class QQuickWindow;
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
    // Loads the overlay once while nothing else is going on.
    void warmUp();
    bool capturing() const { return m_overlay != nullptr; }
    ScreenRecorder *recorder() const { return m_recorder; }

    // "gui": pick a region; "full": all screens to clipboard and file;
    // "open <path>": the editor; "open": the editor with a file dialog;
    // "record": pick a region to record, or stop the recording.
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
    // Writes `source` to `target` with the document's marks on every frame
    // and the trim applied; the exporter reports progress and the result.
    // `moveSource`: the source is a temporary recording that may be moved
    // into place when nothing changes.
    Q_INVOKABLE void exportRecording(VideoExporter *exporter, const QString &source, const QString &target,
                                     Document *document, int startMs, int endMs, bool moveSource);
    // The video editor on a recording, with `preview` as the frame to draw
    // over until the player shows the video; a temporary recording is
    // deleted unless it is saved.
    void openVideoEditor(const QString &path, const QImage &preview, bool temporary);
    static bool isVideoFile(const QString &path);
    Q_INVOKABLE void captureFull();
    Q_INVOKABLE void openImage();
    Q_INVOKABLE bool openEditor(const QString &path);
    // Hands the overlay's document, cropped to the region, to a new editor.
    Q_INVOKABLE void editInWindow(Document *document, const QRectF &region);
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void closeOverlay();
    Q_INVOKABLE void notify(const QString &title, const QString &text);
    Q_INVOKABLE void quit();

signals:
    void capturingChanged();

private slots:
    void onCountdownElapsed();

private:
    QQuickWindow *createWindow(const QString &type, const QVariantMap &properties);
    void showEditor(Document *document);
    void openOverlay(bool recordMode, const QRect &presetRegion = QRect());
    void updateTrayForRecording();

    QQmlEngine *m_engine;
    std::unique_ptr<QSystemTrayIcon> m_tray;
    std::unique_ptr<QMenu> m_trayMenu;
    QElapsedTimer m_trayMenuClosed;
    QAction *m_launchAtLogin = nullptr;
    QAction *m_recordAction = nullptr;
    ScreenRecorder *m_recorder = nullptr;
    QPointer<QQuickWindow> m_recordingControls;
    QPointer<QQuickWindow> m_countdown;
    bool m_countdownRecordMode = false;
    QPointer<QQuickWindow> m_overlay;
    QPointer<QQuickWindow> m_settings;
    QList<QPointer<QQuickWindow>> m_editors;
};
