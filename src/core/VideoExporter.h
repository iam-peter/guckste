#pragma once

#include "Annotation.h"

#include <QAudioBuffer>
#include <QAudioBufferInput>
#include <QAudioBufferOutput>
#include <QMediaCaptureSession>
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QObject>
#include <QVideoFrameInput>
#include <QVideoSink>
#include <QtQml/qqmlregistration.h>

// Writes a recording out again with marks drawn on every frame and the
// part outside the trim left out: a player decodes the source into a sink
// and an audio buffer output, each frame gets the marks painted on, and a
// recorder encodes what comes out. Without marks or trim the file is
// simply moved.
class VideoExporter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the video editor")
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

public:
    struct Job
    {
        QString source;
        QString target;
        qint64 startMs = 0;
        qint64 endMs = -1; // -1: to the end
        QList<Annotation> annotations;
        int quality = 2;
        // The source may be moved into place when nothing is to change.
        bool moveSource = false;
    };

    explicit VideoExporter(QObject *parent = nullptr);

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
    void setRunning(bool running);
    void setProgress(qreal progress);
    void onLoaded();
    void onFrame(const QVideoFrame &frame);
    void onAudio(const QAudioBuffer &buffer);
    void finishEncoding();
    void fail(const QString &message);
    // The inputs take one frame at a time; the rest waits here.
    void flushVideo();
    void flushAudio();
    void stopWhenFlushed();

    Job m_job;
    bool m_running = false;
    bool m_cancelled = false;
    bool m_encoding = false;
    bool m_stopping = false;
    qreal m_progress = 0;
    qint64 m_startUs = 0;
    qint64 m_endUs = -1;
    qint64 m_durationUs = 0;
    int m_frames = 0;
    QList<QVideoFrame> m_pendingVideo;
    QList<QAudioBuffer> m_pendingAudio;

    QMediaPlayer m_player;
    QVideoSink m_sink;
    QAudioBufferOutput m_audioOut;
    QMediaCaptureSession m_session;
    QVideoFrameInput m_videoIn;
    QAudioBufferInput m_audioIn;
    QMediaRecorder m_recorder;
};
