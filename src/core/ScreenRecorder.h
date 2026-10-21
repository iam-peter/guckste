#pragma once

#include <QAudioInput>
#include <QElapsedTimer>
#include <QMediaCaptureSession>
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
    // desktop) to `path`, whose suffix should match bestFormat(). False,
    // with errorOccurred(), when it cannot start.
    bool start(const QRect &region, const QString &path, int frameRate, bool microphone);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();

    // The first of MP4/H.264, WebM/VP9, WebM/VP8, MP4/MPEG-4, AVI/MJPEG that
    // this Qt encodes here: tried out once by encoding a few frames, since
    // the codec table also lists hardware encoders that may not work.
    static QMediaFormat bestFormat();
    // Encodes a few frames with the format; false if that fails.
    static bool canEncode(const QMediaFormat &format);
    static QString suffixFor(const QMediaFormat &format);
    // The frame cut down to `rect` (device pixels) as NV12, times kept. NV12
    // because of how Qt picks the encoder's format: from RGB frames it can
    // land on a 12-bit format NVENC refuses for H.264, from NV12 it takes
    // NV12, which every encoder has.
    static QVideoFrame cropped(const QVideoFrame &frame, const QRect &rect);
    // An image as an NV12 frame, BT.601 video range.
    static QVideoFrame toNV12(const QImage &image);

signals:
    void stateChanged();
    void durationChanged();
    void regionChanged();
    void outputPathChanged();
    // The file is complete.
    void finished(const QString &path);
    void errorOccurred(const QString &message);

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
};
