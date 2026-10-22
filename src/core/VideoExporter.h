#pragma once

#include "Annotation.h"
#include "Document.h"

#include <QAudioBuffer>
#include <QAudioBufferInput>
#include <QAudioFormat>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QObject>
#include <QVideoFrameInput>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

class QAudioBufferOutput;
class QMediaPlayer;
class QVideoSink;

// Writes a document with video clips out as one video: every frame is
// the canvas at its time, each clip on it at the frame of that time, the
// marks and images drawn with them in their order, and the clips' sound
// mixed. A player decodes each clip as its time comes, into a sink and
// an audio buffer output; a frame is put together once every clip shown
// in it has its frame; a recorder encodes what comes out. A video that
// is one clip as it is, untrimmed, is simply moved or copied.
class VideoExporter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

public:
    struct Job
    {
        QString target;
        // The document's layers bottom to top, clips among them, and the
        // part of the canvas to write, in image pixels.
        QList<Annotation> layers;
        QRect canvas;
        // The part of the document's time to write.
        qint64 startMs = 0;
        qint64 endMs = -1; // -1: to the end, as Document::durationOf()
        int quality = 2;
        // A clip that is a temporary recording may be moved into place
        // when the video is that clip as it is.
        bool moveSource = false;
    };

    explicit VideoExporter(QObject *parent = nullptr);
    ~VideoExporter() override;

    bool running() const { return m_running; }
    qreal progress() const { return m_progress; }

    bool start(const Job &job);
    Q_INVOKABLE void cancel();

signals:
    void runningChanged();
    void progressChanged();
    void finished(const QString &path);
    void failed(const QString &message);

private:
    struct Decoder
    {
        Annotation clip;
        QMediaPlayer *player = nullptr;
        QVideoSink *sink = nullptr;
        QAudioBufferOutput *audio = nullptr;
        // Decoded and not yet passed by, oldest first.
        QList<QVideoFrame> frames;
        bool loaded = false;
        bool started = false;
        bool ended = false;
        bool hasAudio = false;
        // Paused, with frames enough waiting.
        bool held = false;
        double frameRate = 0;
        // How far its sound has come, in µs of the output.
        qint64 audioUs = 0;
    };

    // Moves or copies the one clip, when there is nothing to change.
    bool copyAsItIs();
    void setRunning(bool running);
    void setProgress(qreal progress);
    void onLoaded(Decoder *decoder);
    void beginEncoding();
    void onFrame(Decoder *decoder, const QVideoFrame &frame);
    void onAudio(Decoder *decoder, const QAudioBuffer &buffer);
    void schedulePump();
    // Puts frames together while every clip shown has its frame and the
    // encoder keeps up.
    void pump();
    // Whether the clip has the frame for `sourceUs`, which then is the
    // oldest it holds.
    bool hasFrame(Decoder *decoder, qint64 sourceUs);
    void startDecoder(Decoder *decoder, qint64 atUs);
    void stopDecoder(Decoder *decoder);
    // Hands the mixed sound on up to where every clip's is known, and no
    // further than `untilUs` of the output.
    void mixUpTo(qint64 untilUs);
    void finishEncoding();
    void fail(const QString &message);
    void stopDecoders();
    // The inputs take one frame at a time; the rest waits here.
    void flushVideo();
    void flushAudio();
    void stopWhenFlushed();

    Job m_job;
    bool m_running = false;
    bool m_cancelled = false;
    bool m_encoding = false;
    bool m_stopping = false;
    bool m_pumpScheduled = false;
    qreal m_progress = 0;
    qint64 m_startUs = 0;
    qint64 m_lengthUs = 0;
    double m_frameRate = 30;
    QSize m_size;
    int m_frames = 0;
    QList<QVideoFrame> m_pendingVideo;
    QList<QAudioBuffer> m_pendingAudio;
    std::vector<std::unique_ptr<Decoder>> m_decoders;
    // The layers, drawn for each frame at its time.
    Document m_canvas;

    bool m_hasAudio = false;
    QAudioFormat m_audioFormat;
    // Mixed sound not yet handed on, interleaved, from frame m_audioSent.
    QList<float> m_mix;
    qint64 m_audioSent = 0;

    QMediaCaptureSession m_session;
    QVideoFrameInput m_videoIn;
    QAudioBufferInput m_audioIn;
    QMediaRecorder m_recorder;
};
