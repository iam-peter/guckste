#pragma once

#include "KeyListener.h"
#include "KeyOverlay.h"
#include "StillKeeper.h"

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
    // The first moment of a recording, left out of it: see warmUpMs.
    Q_PROPERTY(bool starting READ starting NOTIFY startingChanged)
    Q_PROPERTY(int stillCount READ stillCount NOTIFY stillsChanged)

public:
    enum State { Idle, Recording, Paused, Stopping };
    Q_ENUM(State)

    explicit ScreenRecorder(QObject *parent = nullptr);
    ~ScreenRecorder() override;

    // Starts Qt Multimedia ahead of the first recording.
    void prepare() { media(); }

    State state() const { return m_state; }
    // Milliseconds recorded, pauses left out.
    qint64 duration() const { return m_options.video && m_media ? m_media->recorder.duration() : m_stillsMs; }
    // The region in logical screen coordinates, as it is recorded.
    QRect region() const { return m_region; }
    bool wholeScreen() const { return m_wholeScreen; }
    QString outputPath() const { return m_outputPath; }
    // A pause cuts the time out of the video, which the microphone's audio
    // would not follow; so there is none while it is recorded.
    bool canPause() const { return !m_audio && !m_starting; }
    bool starting() const { return m_starting; }

    // How long the screen is captured before the recording begins, the
    // frames dropped: windows that the capture overlay covered, like a
    // browser playing a video, stop drawing meanwhile and take a moment to
    // draw again, which would open the video with a stale, frozen picture.
    // The encoder, and the microphone, start only after it, so the sound
    // stays with the picture.
    static constexpr int warmUpMs = 1500;

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
        // A still before and after every click in the region.
        bool stillsOnClicks = false;
        // A still of what the screen settles on after it changes.
        bool stillsOnChanges = false;
        // Without it only the stills are kept, and nothing is encoded.
        bool video = true;
    };
    bool start(const QRect &region, const QString &path, const Options &options);
    // Keys pressed while recording, for the panel.
    QStringList recentKeys() const;
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    // Keeps the frame shown now as a still, lossless.
    Q_INVOKABLE void keepStill();

    // The first frame recorded, for a preview of the file.
    QImage firstFrame() const { return m_firstFrame; }
    // The stills of the recording, as PNGs beside it in its folder: see
    // stillsDirectory().
    QList<Still> stills() const { return m_stills.stills(); }
    int stillCount() const { return int(m_stills.stills().size()); }
    static QString stillsDirectory(const QString &recording);

signals:
    void stateChanged();
    void durationChanged();
    void regionChanged();
    void outputPathChanged();
    // The file is complete.
    void finished(const QString &path);
    // A recording of stills only is over, or one whose video failed:
    // its stills are in `directory`.
    void stillsFinished(const QString &directory);
    void errorOccurred(const QString &message);
    // Something to tell that does not stop the recording.
    void notice(const QString &message);
    void recentKeysChanged();
    void startingChanged();
    void stillsChanged();

private:
    void setState(State state);
    bool startCapture(QScreen *preferred);
    void onFrame(const QVideoFrame &frame);
    void onButtonPressed();
    void beginEncoding();
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
    // The time of the last frame, for a recording without a video.
    qint64 m_stillsMs = 0;
    Options m_options;
    bool m_failed = false;
    // Paused time so far, taken off the frames' timestamps.
    qint64 m_pausedTotal = 0;
    bool m_starting = false;
    // The capture time of the first frame recorded, which becomes 0.
    qint64 m_startTime = -1;
    QElapsedTimer m_pauseTimer;

    std::unique_ptr<QScreenCapture> m_screenCapture;
    // Made for the first recording: starting Qt Multimedia takes a fifth
    // of a second, which a screenshot at start should not wait for.
    struct Media
    {
        QMediaCaptureSession captureSession;
        QVideoSink sink;

        QMediaCaptureSession encodeSession;
        QVideoFrameInput input;
        QMediaRecorder recorder;
    };
    Media &media();
    std::unique_ptr<Media> m_media;
    std::unique_ptr<QAudioInput> m_audio;
    std::unique_ptr<KeyListener> m_keyListener;
    KeyOverlay m_keys;
    StillKeeper m_stills;
};
