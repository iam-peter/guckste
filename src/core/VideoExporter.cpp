#include "VideoExporter.h"

#include "Encoding.h"
#include "VideoFrames.h"

#include <QAudioBufferOutput>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QVideoSink>

Q_LOGGING_CATEGORY(lcExport, "guckste.export")

namespace {

// How long before its time a clip starts decoding, so its first frame is
// there when it is needed.
constexpr qint64 leadUs = 500'000;
// Frames a clip may decode ahead before its player is paused.
constexpr int maxAhead = 30;
constexpr int sampleRate = 48000;
constexpr int channels = 2;

qint64 framesAt(qint64 us)
{
    return us * sampleRate / 1'000'000;
}

// A sample of any format as a float, for the mix.
float sampleAt(const QAudioBuffer &buffer, qsizetype frame, int channel)
{
    const QAudioFormat format = buffer.format();
    const int count = format.channelCount();
    const qsizetype at = frame * count + qMin(channel, count - 1);
    switch (format.sampleFormat()) {
    case QAudioFormat::Float: return buffer.constData<float>()[at];
    case QAudioFormat::Int16: return buffer.constData<qint16>()[at] / 32768.0f;
    case QAudioFormat::Int32: return buffer.constData<qint32>()[at] / 2147483648.0f;
    case QAudioFormat::UInt8: return (buffer.constData<quint8>()[at] - 128) / 128.0f;
    default: return 0;
    }
}

} // namespace

VideoExporter::VideoExporter(QObject *parent)
    : QObject(parent)
{
    m_audioFormat.setSampleFormat(QAudioFormat::Float);
    m_audioFormat.setSampleRate(sampleRate);
    m_audioFormat.setChannelCount(channels);
    m_audioFormat.setChannelConfig(QAudioFormat::ChannelConfigStereo);

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
        QTimer::singleShot(0, this, [this] { stopDecoders(); });
        emit finished(path);
    });
}

VideoExporter::~VideoExporter()
{
    stopDecoders();
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

bool VideoExporter::copyAsItIs()
{
    if (m_job.layers.size() != 1)
        return false;
    const Annotation &clip = m_job.layers.first();
    // Untrimmed, at its own size, at the start.
    if (clip.type != Annotation::Type::Video || clip.clipIn > 0 || clip.startMs > 0 || clip.endMs < clip.sourceMs
        || clip.image.isNull() || clip.rect() != QRectF(QPointF(0, 0), QSizeF(clip.image.size()))
        || m_job.canvas != clip.rect().toAlignedRect() || m_job.startMs > 0
        || (m_job.endMs >= 0 && m_job.endMs < clip.endMs))
        return false;
    const QString &source = clip.source;
    if (QFileInfo(source) == QFileInfo(m_job.target)) {
        setProgress(1);
        emit finished(m_job.target);
        return true;
    }
    QFile::remove(m_job.target);
    if (!(m_job.moveSource && QFile::rename(source, m_job.target)) && !QFile::copy(source, m_job.target)) {
        emit failed(tr("Could not write %1").arg(m_job.target));
        return true;
    }
    setProgress(1);
    emit finished(m_job.target);
    return true;
}

bool VideoExporter::start(const Job &job)
{
    if (m_running)
        return false;
    m_job = job;
    m_cancelled = false;
    m_stopping = false;
    m_frames = 0;
    m_pendingVideo.clear();
    m_pendingAudio.clear();
    m_mix.clear();
    m_audioSent = 0;
    stopDecoders();
    setProgress(0);
    QDir().mkpath(QFileInfo(job.target).absolutePath());

    if (copyAsItIs())
        return true;

    const qint64 durationMs = Document::durationOf(job.layers);
    const qint64 endMs = job.endMs < 0 ? durationMs : qMin(job.endMs, durationMs);
    m_startUs = qMax<qint64>(0, job.startMs) * 1000;
    m_lengthUs = endMs * 1000 - m_startUs;
    m_size = QSize(job.canvas.width() & ~1, job.canvas.height() & ~1);
    if (m_lengthUs <= 0 || m_size.isEmpty()) {
        emit failed(tr("Nothing left after trimming"));
        return false;
    }
    m_canvas.setLayers(job.layers);

    setRunning(true);
    for (const Annotation &a : std::as_const(job.layers)) {
        // Clips outside the part written are left out.
        if (a.type != Annotation::Type::Video || a.endMs * 1000 <= m_startUs || a.startMs * 1000 >= endMs * 1000)
            continue;
        auto decoder = std::make_unique<Decoder>();
        Decoder *d = decoder.get();
        d->clip = a;
        d->player = new QMediaPlayer(this);
        d->sink = new QVideoSink(d->player);
        d->audio = new QAudioBufferOutput(m_audioFormat, d->player);
        d->player->setVideoSink(d->sink);
        d->player->setAudioBufferOutput(d->audio);
        connect(d->sink, &QVideoSink::videoFrameChanged, this,
                [this, d](const QVideoFrame &frame) { onFrame(d, frame); });
        connect(d->audio, &QAudioBufferOutput::audioBufferReceived, this,
                [this, d](const QAudioBuffer &buffer) { onAudio(d, buffer); });
        connect(d->player, &QMediaPlayer::mediaStatusChanged, this, [this, d](QMediaPlayer::MediaStatus status) {
            qCDebug(lcExport) << "clip" << d->clip.clip << "status" << status;
            if (!m_running)
                return;
            if (status == QMediaPlayer::LoadedMedia && !d->loaded)
                onLoaded(d);
            else if (status == QMediaPlayer::EndOfMedia && d->started) {
                d->ended = true;
                schedulePump();
            } else if (status == QMediaPlayer::InvalidMedia)
                fail(tr("%1 cannot be read").arg(QFileInfo(d->clip.source).fileName()));
        });
        connect(d->player, &QMediaPlayer::errorOccurred, this,
                [this](QMediaPlayer::Error, const QString &message) { fail(message); });
        m_decoders.push_back(std::move(decoder));
    }
    // Loaded one by one, so a file that cannot be read fails it at once.
    for (const auto &d : m_decoders)
        d->player->setSource(QUrl::fromLocalFile(d->clip.source));
    if (m_decoders.empty())
        beginEncoding();
    return true;
}

void VideoExporter::onLoaded(Decoder *decoder)
{
    decoder->loaded = true;
    decoder->hasAudio = decoder->player->hasAudio();
    decoder->frameRate = decoder->player->metaData().value(QMediaMetaData::VideoFrameRate).toDouble();
    for (const auto &d : m_decoders) {
        if (!d->loaded)
            return;
    }
    beginEncoding();
}

void VideoExporter::beginEncoding()
{
    m_frameRate = 0;
    m_hasAudio = false;
    for (const auto &d : m_decoders) {
        m_frameRate = qMax(m_frameRate, d->frameRate);
        m_hasAudio = m_hasAudio || d->hasAudio;
    }
    if (m_frameRate <= 0 || m_frameRate > 120)
        m_frameRate = 30;

    QMediaFormat format = Encoding::bestFormat();
    // An audio input without audio would have the muxer wait for it forever.
    m_session.setAudioBufferInput(m_hasAudio ? &m_audioIn : nullptr);
    if (!m_hasAudio)
        format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
    m_recorder.setMediaFormat(format);
    m_recorder.setVideoFrameRate(m_frameRate);
    m_recorder.setVideoResolution(m_size);
    Encoding::apply(m_recorder, m_size, m_frameRate, m_job.quality);
    m_recorder.setOutputLocation(QUrl::fromLocalFile(m_job.target));
    m_encoding = true;
    m_recorder.record();
    if (!m_running)
        return;
    // Sound at any other speed would come out stretched; without, the
    // clips decode faster than they play.
    for (const auto &d : m_decoders)
        d->player->setPlaybackRate(m_hasAudio ? 1.0 : 2.0);
    schedulePump();
}

void VideoExporter::onFrame(Decoder *decoder, const QVideoFrame &frame)
{
    if (!m_encoding || m_stopping || !frame.isValid() || decoder->ended)
        return;
    decoder->frames.append(frame);
    if (decoder->frames.size() > maxAhead && !decoder->held) {
        decoder->held = true;
        decoder->player->pause();
    }
    schedulePump();
}

void VideoExporter::schedulePump()
{
    if (m_pumpScheduled)
        return;
    m_pumpScheduled = true;
    QMetaObject::invokeMethod(this, [this] {
        m_pumpScheduled = false;
        pump();
    }, Qt::QueuedConnection);
}

bool VideoExporter::hasFrame(Decoder *d, qint64 sourceUs)
{
    // The newest frame that starts by then; older ones are done with.
    while (d->frames.size() >= 2 && d->frames[1].startTime() <= sourceUs)
        d->frames.removeFirst();
    if (d->frames.isEmpty())
        return d->ended;
    const QVideoFrame &first = d->frames.first();
    // Known to be the one when a later frame came, or it lasts past then.
    return d->ended || d->frames.size() >= 2 || first.startTime() >= sourceUs
        || (first.endTime() > first.startTime() && first.endTime() > sourceUs);
}

void VideoExporter::startDecoder(Decoder *d, qint64 atUs)
{
    d->started = true;
    const qint64 from = qMax(atUs, d->clip.startMs * 1000) - d->clip.startMs * 1000 + d->clip.clipIn * 1000;
    d->player->setPosition(from / 1000);
    d->player->play();
}

void VideoExporter::stopDecoder(Decoder *d)
{
    d->ended = true;
    d->frames.clear();
    d->player->stop();
}

void VideoExporter::pump()
{
    if (!m_encoding || m_stopping)
        return;
    while (m_pendingVideo.size() < 3) {
        const qint64 outUs = qint64(m_frames * 1e6 / m_frameRate);
        if (outUs >= m_lengthUs) {
            finishEncoding();
            return;
        }
        const qint64 atUs = m_startUs + outUs;
        bool ready = true;
        for (const auto &d : m_decoders) {
            const qint64 startUs = d->clip.startMs * 1000;
            const qint64 endUs = d->clip.endMs * 1000;
            if (!d->started && atUs >= startUs - leadUs && atUs < endUs)
                startDecoder(d.get(), atUs);
            if (d->started && !d->ended && atUs >= endUs)
                stopDecoder(d.get());
            if (atUs < startUs || atUs >= endUs)
                continue;
            const bool has = hasFrame(d.get(), atUs - startUs + d->clip.clipIn * 1000);
            if (has && !d->frames.isEmpty())
                m_canvas.setClipFrame(d->clip.clip, d->frames.first());
            ready = ready && has;
            // Waited for, or running low: on with decoding.
            if (d->held && (!has || d->frames.size() < maxAhead / 3)) {
                d->held = false;
                d->player->play();
            }
        }
        if (!ready)
            return;

        QImage image(m_size, QImage::Format_RGB32);
        image.fill(Qt::black);
        {
            QPainter painter(&image);
            m_canvas.setTime(int(atUs / 1000));
            m_canvas.paintLayers(painter, 1, m_job.canvas.topLeft());
        }
        QVideoFrame out = VideoFrames::toNV12(image);
        out.setStartTime(outUs);
        out.setEndTime(qint64((m_frames + 1) * 1e6 / m_frameRate));
        m_pendingVideo.append(out);
        ++m_frames;
        setProgress(qreal(outUs) / qreal(m_lengthUs));
        mixUpTo(outUs);
        flushVideo();
    }
}

void VideoExporter::onAudio(Decoder *d, const QAudioBuffer &buffer)
{
    if (!m_encoding || m_stopping || !buffer.isValid() || d->ended || !m_hasAudio)
        return;
    const QAudioFormat format = buffer.format();
    if (format.sampleRate() != sampleRate || format.channelCount() <= 0) {
        qCDebug(lcExport) << "sound at" << format.sampleRate() << "Hz left out";
        return;
    }
    // From the clip's time to the output's.
    const qint64 offsetUs = d->clip.startMs * 1000 - d->clip.clipIn * 1000 - m_startUs;
    const qint64 firstUs = buffer.startTime() + offsetUs;
    const qint64 first = framesAt(firstUs);
    const qint64 clipFrom = framesAt(qMax<qint64>(0, d->clip.startMs * 1000 - m_startUs));
    const qint64 clipTo = qMin(framesAt(d->clip.endMs * 1000 - m_startUs), framesAt(m_lengthUs));
    const qsizetype count = buffer.frameCount();
    for (qsizetype i = 0; i < count; ++i) {
        const qint64 at = first + i;
        if (at < clipFrom || at < m_audioSent || at >= clipTo)
            continue;
        const qsizetype index = (at - m_audioSent) * channels;
        if (m_mix.size() < index + channels)
            m_mix.resize(index + channels, 0.0f);
        for (int c = 0; c < channels; ++c)
            m_mix[index + c] += sampleAt(buffer, i, c);
    }
    d->audioUs = qMax(d->audioUs, firstUs + buffer.duration());
    mixUpTo(qint64(m_frames * 1e6 / m_frameRate));
}

void VideoExporter::mixUpTo(qint64 untilUs)
{
    if (!m_hasAudio)
        return;
    // Not past a clip whose sound is still to come.
    qint64 limitUs = qMin(untilUs, m_lengthUs);
    for (const auto &d : m_decoders) {
        if (!d->hasAudio || d->ended)
            continue;
        const qint64 fromUs = d->clip.startMs * 1000 - m_startUs;
        limitUs = qMin(limitUs, d->started ? qMax(d->audioUs, fromUs) : qMax<qint64>(0, fromUs));
    }
    const qint64 limit = framesAt(limitUs);
    while (m_audioSent < limit) {
        // A tenth of a second at a time.
        const qint64 frames = qMin<qint64>(limit - m_audioSent, sampleRate / 10);
        QByteArray data(frames * channels * qsizetype(sizeof(float)), Qt::Uninitialized);
        auto *out = reinterpret_cast<float *>(data.data());
        const qsizetype have = qMin<qsizetype>(m_mix.size(), frames * channels);
        for (qsizetype i = 0; i < frames * channels; ++i)
            out[i] = i < have ? qBound(-1.0f, m_mix[i], 1.0f) : 0.0f;
        m_mix.remove(0, have);
        m_pendingAudio.append(QAudioBuffer(data, m_audioFormat, m_audioSent * 1'000'000 / sampleRate));
        m_audioSent += frames;
    }
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
    else
        schedulePump();
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
    for (const auto &d : m_decoders) {
        if (!d->ended)
            stopDecoder(d.get());
    }
    if (m_cancelled) {
        m_pendingVideo.clear();
        m_pendingAudio.clear();
    } else {
        // The sound to the end, silence where no clip has any.
        mixUpTo(m_lengthUs);
    }
    m_stopping = true;
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

void VideoExporter::stopDecoders()
{
    for (const auto &d : m_decoders) {
        d->player->disconnect(this);
        d->sink->disconnect(this);
        d->audio->disconnect(this);
        d->player->stop();
        d->player->deleteLater();
    }
    m_decoders.clear();
}

void VideoExporter::fail(const QString &message)
{
    if (!m_running)
        return;
    const bool recording = m_recorder.recorderState() != QMediaRecorder::StoppedState;
    m_stopping = false;
    m_encoding = false;
    m_pendingVideo.clear();
    m_pendingAudio.clear();
    // Not from within a player's own signal.
    QTimer::singleShot(0, this, [this] { stopDecoders(); });
    if (recording)
        m_recorder.stop();
    QFile::remove(m_job.target);
    setRunning(false);
    emit failed(message);
}
