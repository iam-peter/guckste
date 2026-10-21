#pragma once

#include "KeyListener.h"
#include "KeyOverlay.h"

#include <QAudioInput>
#include <QElapsedTimer>
#include <QMediaCaptureSession>
#include <QImage>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QObject>
#include <QRect>
#include <QScreenCapture>
#include <QVideoFrame>
#include <QVideoFrameInput>
#include <QVideoSink>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QScreen;

// Records a region of a screen to a video file, all through Qt Multimedia:
// a QScreenCapture delivers the screen's frames to a sink, each is cropped
// to the region and handed to a QVideoFrameInput, which a QMediaRecorder
// encodes with Qt's own FFmpeg. So nothing has to be installed, and the
// same code runs on X11, Windows and macOS.
class ScreenRecorder : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by App")
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QRect region READ region NOTIFY regionChanged)
    Q_PROPERTY(bool wholeScreen READ wholeScreen NOTIFY regionChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY outputPathChanged)
    Q_PROPERTY(bool canPause READ canPause NOTIFY outputPathChanged)
    Q_PROPERTY(QStringList recentKeys READ recentKeys NOTIFY recentKeysChanged)

public:
    enum State { Idle, Recording, Paused, Stopping };
    Q_ENUM(State)

    explicit ScreenRecorder(QObject *parent = nullptr);
    ~ScreenRecorder() override;

    State state() const { return m_state; }
    // Milliseconds recorded, pauses left out.
    qint64 duration() const { return m_recorder.duration(); }
    // The region in logical screen coordinates, as it is recorded.
    QRect region() const { return m_region; }
    bool wholeScreen() const { return m_wholeScreen; }
    QString outputPath() const { return m_outputPath; }
    // A pause cuts the time out of the video, which the microphone's audio
    // would not follow; so there is none while it is recorded.
    bool canPause() const { return !m_audio; }

    // Starts recording `region` (logical screen coordinates, clipped to the
    // desktop) to `path`, whose suffix should match Encoding::bestFormat().
    // False, with errorOccurred(), when it cannot start.
    struct Options
    {
        int frameRate = 30;
        int quality = 2;
        bool microphone = false;
        bool includeCursor = true;
        bool showKeys = false;
    };
    bool start(const QRect &region, const QString &path, const Options &options);
    // Keys pressed while recording, for the panel.
    QStringList recentKeys() const;
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();

    // The first frame recorded, for a preview of the file.
    QImage firstFrame() const { return m_firstFrame; }

signals:
    void stateChanged();
    void durationChanged();
    void regionChanged();
    void outputPathChanged();
    // The file is complete.
    void finished(const QString &path);
    void errorOccurred(const QString &message);
    // Something to tell that does not stop the recording.
    void notice(const QString &message);
    void recentKeysChanged();

private:
    void setState(State state);
    bool startCapture(QScreen *preferred);
    void onFrame(const QVideoFrame &frame);
    void finish();

    State m_state = Idle;
    QRect m_region;
    QRect m_virtualGeometry;
    QRect m_screenGeometry;
    qreal m_devicePixelRatio = 1;
    // Worked out from the first frame: what it covers, and the region in it.
    QRect m_cropDevice;
    bool m_cropKnown = false;
    bool m_wholeScreen = false;
    QString m_outputPath;
    QImage m_firstFrame;
    Options m_options;
    bool m_failed = false;
    // Paused time so far, taken off the frames' timestamps.
    qint64 m_pausedTotal = 0;
    QElapsedTimer m_pauseTimer;

    std::unique_ptr<QScreenCapture> m_screenCapture;
    QMediaCaptureSession m_captureSession;
    QVideoSink m_sink;

    QMediaCaptureSession m_encodeSession;
    QVideoFrameInput m_input;
    QMediaRecorder m_recorder;
    std::unique_ptr<QAudioInput> m_audio;
    std::unique_ptr<KeyListener> m_keyListener;
    KeyOverlay m_keys;
};
