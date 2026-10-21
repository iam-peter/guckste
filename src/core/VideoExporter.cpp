#include "VideoExporter.h"

#include "AnnotationPainter.h"
#include "Encoding.h"
#include "VideoFrames.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaMetaData>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcExport, "guckste.export")

VideoExporter::VideoExporter(QObject *parent)
    : QObject(parent)
{
    m_player.setVideoSink(&m_sink);
    m_player.setAudioBufferOutput(&m_audioOut);
    connect(&m_sink, &QVideoSink::videoFrameChanged, this, &VideoExporter::onFrame);
    connect(&m_audioOut, &QAudioBufferOutput::audioBufferReceived, this, &VideoExporter::onAudio);
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        qCDebug(lcExport) << "player status" << status << "running" << m_running << "encoding" << m_encoding;
        if (!m_running)
            return;
        if (status == QMediaPlayer::LoadedMedia && !m_encoding)
            onLoaded();
        else if (status == QMediaPlayer::EndOfMedia && m_encoding)
            finishEncoding();
        else if (status == QMediaPlayer::InvalidMedia)
            fail(tr("The recording cannot be read"));
    });
    connect(&m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &message) { fail(message); });

    m_session.setVideoFrameInput(&m_videoIn);
    m_session.setRecorder(&m_recorder);
    connect(&m_videoIn, &QVideoFrameInput::readyToSendVideoFrame, this, &VideoExporter::flushVideo);
    connect(&m_audioIn, &QAudioBufferInput::readyToSendAudioBuffer, this, &VideoExporter::flushAudio);
    connect(&m_recorder, &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString &message) { fail(message); });
    connect(&m_recorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        qCDebug(lcExport) << "recorder state" << state << "stopping" << m_stopping;
        if (state != QMediaRecorder::StoppedState || !m_stopping)
            return;
        m_stopping = false;
        m_encoding = false;
        m_pendingVideo.clear();
        m_pendingAudio.clear();
        const QString path = m_recorder.actualLocation().toLocalFile();
        if (m_cancelled || QFileInfo(path).size() == 0) {
            QFile::remove(path);
            fail(m_cancelled ? tr("Cancelled") : tr("Nothing was written"));
            return;
        }
        setProgress(1);
        setRunning(false);
        emit finished(path);
    });
}

void VideoExporter::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
}

void VideoExporter::setProgress(qreal progress)
{
    progress = qBound<qreal>(0, progress, 1);
    if (qFuzzyCompare(m_progress, progress))
        return;
    m_progress = progress;
    emit progressChanged();
}

bool VideoExporter::start(const Job &job)
{
    if (m_running)
        return false;
    m_job = job;
    m_cancelled = false;
    m_frames = 0;
    m_pendingVideo.clear();
    m_pendingAudio.clear();
    setProgress(0);
    QDir().mkpath(QFileInfo(job.target).absolutePath());

    if (job.annotations.isEmpty() && job.startMs <= 0 && job.endMs < 0) {
        // Nothing to change: the file as it is.
        if (QFileInfo(job.source) == QFileInfo(job.target)) {
            setProgress(1);
            emit finished(job.target);
            return true;
        }
        QFile::remove(job.target);
        if (!(job.moveSource && QFile::rename(job.source, job.target)) && !QFile::copy(job.source, job.target)) {
            emit failed(tr("Could not write %1").arg(job.target));
            return false;
        }
        setProgress(1);
        emit finished(job.target);
        return true;
    }

    setRunning(true);
    m_startUs = qMax<qint64>(0, job.startMs) * 1000;
    m_endUs = job.endMs < 0 ? -1 : job.endMs * 1000;
    m_player.setSource(QUrl::fromLocalFile(job.source));
    return true;
}

void VideoExporter::onLoaded()
{
    // The duration may not be known yet; then the end of the file is the end.
    m_durationUs = m_player.duration() > 0 ? m_player.duration() * 1000 : -1;
    if (m_durationUs > 0 && m_endUs > m_durationUs)
        m_endUs = m_durationUs;
    if (m_endUs >= 0 && m_endUs <= m_startUs) {
        fail(tr("Nothing left after trimming"));
        return;
    }
    const QMediaMetaData meta = m_player.metaData();
    QSize size = meta.value(QMediaMetaData::Resolution).toSize();
    if (size.isEmpty())
        size = QSize(1280, 720);
    double fps = meta.value(QMediaMetaData::VideoFrameRate).toDouble();
    if (fps <= 0)
        fps = 30;

    QMediaFormat format = Encoding::bestFormat();
    // An audio input without audio would have the muxer wait for it forever.
    m_session.setAudioBufferInput(m_player.hasAudio() ? &m_audioIn : nullptr);
    if (!m_player.hasAudio())
        format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
    m_recorder.setMediaFormat(format);
    m_recorder.setVideoFrameRate(fps);
    m_recorder.setVideoResolution(QSize(size.width() & ~1, size.height() & ~1));
    Encoding::apply(m_recorder, size, fps, m_job.quality);
    m_recorder.setOutputLocation(QUrl::fromLocalFile(m_job.target));
    m_encoding = true;
    m_recorder.record();
    if (!m_running)
        return;
    // Audio at any other speed would come out stretched.
    m_player.setPlaybackRate(m_player.hasAudio() ? 1.0 : 2.0);
    if (m_startUs > 0)
        m_player.setPosition(m_startUs / 1000);
    m_player.play();
}

void VideoExporter::onFrame(const QVideoFrame &frame)
{
    if (!m_encoding || m_stopping || !frame.isValid())
        return;
    const qint64 at = frame.startTime();
    qCDebug(lcExport) << "frame at" << at << "pending" << m_pendingVideo.size();
    if (at < m_startUs)
        return;
    if (m_endUs >= 0 && at > m_endUs) {
        finishEncoding();
        return;
    }
    QImage image = VideoFrames::toImage(frame).convertToFormat(QImage::Format_RGB32);
    if (!m_job.annotations.isEmpty()) {
        // Each mark on the frames of its time, in the source's time.
        QPainter painter(&image);
        AnnotationPainter marks(painter, image, 1);
        const qint64 atMs = at / 1000;
        for (const Annotation &a : std::as_const(m_job.annotations)) {
            if (a.shownAt(atMs))
                marks.paint(a);
        }
    }
    QVideoFrame out = VideoFrames::toNV12(image);
    out.setStartTime(at - m_startUs);
    out.setEndTime(frame.endTime() >= 0 ? frame.endTime() - m_startUs : -1);
    m_pendingVideo.append(out);
    flushVideo();
    ++m_frames;
    const qint64 endUs = m_endUs >= 0 ? m_endUs : m_player.duration() * 1000;
    if (endUs > m_startUs)
        setProgress(qreal(at - m_startUs) / qreal(endUs - m_startUs));
}

void VideoExporter::onAudio(const QAudioBuffer &buffer)
{
    if (!m_encoding || m_stopping || !buffer.isValid())
        return;
    const qint64 at = buffer.startTime();
    if (at < m_startUs || (m_endUs >= 0 && at > m_endUs))
        return;
    m_pendingAudio.append(QAudioBuffer(QByteArray(buffer.constData<char>(), buffer.byteCount()), buffer.format(),
                                       at - m_startUs));
    flushAudio();
}

void VideoExporter::flushVideo()
{
    while (!m_pendingVideo.isEmpty() && m_encoding) {
        if (!m_videoIn.sendVideoFrame(m_pendingVideo.first()))
            return;
        m_pendingVideo.removeFirst();
    }
    if (m_stopping)
        stopWhenFlushed();
}

void VideoExporter::flushAudio()
{
    while (!m_pendingAudio.isEmpty() && m_encoding) {
        if (!m_audioIn.sendAudioBuffer(m_pendingAudio.first()))
            return;
        m_pendingAudio.removeFirst();
    }
    if (m_stopping)
        stopWhenFlushed();
}

void VideoExporter::stopWhenFlushed()
{
    qCDebug(lcExport) << "stopWhenFlushed: stopping" << m_stopping << "encoding" << m_encoding << "pending"
                      << m_pendingVideo.size() << m_pendingAudio.size();
    if (!m_stopping || !m_encoding || !m_pendingVideo.isEmpty() || !m_pendingAudio.isEmpty())
        return;
    // The last frame handed over still has to pass through.
    m_encoding = false;
    QTimer::singleShot(200, this, [this] { m_recorder.stop(); });
}

void VideoExporter::finishEncoding()
{
    qCDebug(lcExport) << "finish: encoding" << m_encoding << "stopping" << m_stopping << "frames" << m_frames;
    if (!m_encoding || m_stopping)
        return;
    m_stopping = true;
    m_player.stop();
    if (m_cancelled) {
        m_pendingVideo.clear();
        m_pendingAudio.clear();
    }
    stopWhenFlushed();
}

void VideoExporter::cancel()
{
    if (!m_running)
        return;
    m_cancelled = true;
    if (m_encoding)
        finishEncoding();
    else
        fail(tr("Cancelled"));
}

void VideoExporter::fail(const QString &message)
{
    if (!m_running)
        return;
    m_player.stop();
    const bool recording = m_recorder.recorderState() != QMediaRecorder::StoppedState;
    m_stopping = false;
    m_encoding = false;
    m_pendingVideo.clear();
    m_pendingAudio.clear();
    if (recording)
        m_recorder.stop();
    QFile::remove(m_job.target);
    setRunning(false);
    emit failed(message);
}
