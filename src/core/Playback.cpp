#include "Playback.h"

#include <QAudioOutput>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoSink>

namespace {

const Annotation *findClip(const Document *document, int id)
{
    for (const Annotation &a : document->annotations()) {
        if (a.type == Annotation::Type::Video && a.clip == id)
            return &a;
    }
    return nullptr;
}

} // namespace

Playback::Playback(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(15);
    connect(&m_timer, &QTimer::timeout, this, &Playback::tick);
}

Playback::~Playback()
{
    for (auto &[id, clip] : m_clips)
        delete clip.player;
}

void Playback::setDocument(Document *document)
{
    if (m_document == document)
        return;
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (m_document)
        connect(m_document, &Document::changed, this, &Playback::sync);
    sync();
    emit documentChanged();
}

void Playback::sync()
{
    const int duration = m_document ? m_document->duration() : 0;
    if (duration != m_duration) {
        m_duration = duration;
        emit durationChanged();
    }

    // Players for new clips, none for clips gone.
    QList<int> ids;
    if (m_document) {
        for (const Annotation &a : m_document->annotations()) {
            if (a.type == Annotation::Type::Video)
                ids << a.clip;
        }
    }
    for (auto it = m_clips.begin(); it != m_clips.end();) {
        const Annotation *a = ids.contains(it->first) ? findClip(m_document, it->first) : nullptr;
        // Gone, or its video moved.
        if (!a || a->source != it->second.source) {
            it->second.player->stop();
            it->second.player->deleteLater();
            it = m_clips.erase(it);
        } else {
            ++it;
        }
    }
    for (int id : std::as_const(ids)) {
        if (m_clips.count(id))
            continue;
        const Annotation *a = findClip(m_document, id);
        Clip &clip = m_clips[id];
        clip.source = a->source;
        clip.player = new QMediaPlayer(this);
        clip.sink = new QVideoSink(clip.player);
        clip.audio = new QAudioOutput(clip.player);
        clip.player->setVideoSink(clip.sink);
        clip.player->setAudioOutput(clip.audio);
        connect(clip.sink, &QVideoSink::videoFrameChanged, this, [this, id](const QVideoFrame &frame) {
            if (m_document)
                m_document->setClipFrame(id, frame);
        });
        QMediaPlayer *player = clip.player;
        connect(player, &QMediaPlayer::mediaStatusChanged, this, [this, id, player](QMediaPlayer::MediaStatus status) {
            const auto found = m_clips.find(id);
            if (found == m_clips.end() || found->second.loaded || status != QMediaPlayer::LoadedMedia)
                return;
            found->second.loaded = true;
            const double rate = player->metaData().value(QMediaMetaData::VideoFrameRate).toDouble();
            if (rate > m_frameRate) {
                m_frameRate = rate;
                emit frameRateChanged();
            }
            // Nothing is shown until the player renders a frame, which a
            // seek makes it do; a play paused at once does not.
            player->pause();
            QTimer::singleShot(80, this, [this, id] {
                const auto clip = m_clips.find(id);
                if (clip == m_clips.end())
                    return;
                clip->second.player->setPosition(clip->second.player->position() + 1);
                follow(id, true);
            });
        });
        player->setSource(QUrl::fromLocalFile(clip.source));
    }

    if (m_position > m_duration)
        m_position = m_duration;
    updateTime();
    followAll(false);
}

// At the very end every clip is over, as each shows up to its end: the
// document is shown a moment before, on the last frames.
int Playback::shownTime() const
{
    return m_duration > 0 ? qMin(m_position, m_duration - 1) : -1;
}

void Playback::updateTime()
{
    if (m_document)
        m_document->setTime(shownTime());
}

void Playback::follow(int id, bool seek)
{
    const auto found = m_clips.find(id);
    if (!m_document || found == m_clips.end() || !found->second.loaded)
        return;
    const Annotation *a = findClip(m_document, id);
    if (!a)
        return;
    QMediaPlayer *player = found->second.player;
    const int time = shownTime();
    const bool on = time >= a->startMs && time < a->endMs;
    const bool running = player->playbackState() == QMediaPlayer::PlayingState;
    if (!on) {
        if (running)
            player->pause();
        return;
    }
    const qint64 target = time - a->startMs + a->clipIn;
    if (m_playing) {
        if (!running) {
            player->setPosition(target);
            player->play();
        } else if (std::abs(player->position() - target) > 250) {
            // Drifted off the clock.
            player->setPosition(target);
        }
        return;
    }
    if (running)
        player->pause();
    // Within a frame is close enough, unless the frame itself is asked for.
    if (seek || std::abs(player->position() - target) > 40)
        player->setPosition(target);
}

void Playback::followAll(bool seek)
{
    for (auto &[id, clip] : m_clips)
        follow(id, seek);
}

void Playback::setPosition(int ms)
{
    ms = qBound(0, ms, m_duration);
    if (m_playing) {
        m_clockStart = ms;
        m_clock.restart();
    }
    if (ms != m_position) {
        m_position = ms;
        emit positionChanged();
    }
    updateTime();
    followAll(true);
}

void Playback::setStopAt(int ms)
{
    if (m_stopAt == ms)
        return;
    m_stopAt = ms;
    emit stopAtChanged();
}

void Playback::play()
{
    if (m_playing || m_duration <= 0)
        return;
    m_playing = true;
    m_clockStart = m_position;
    m_clock.restart();
    m_timer.start();
    emit playingChanged();
    followAll(false);
}

void Playback::pause()
{
    if (!m_playing)
        return;
    m_playing = false;
    m_timer.stop();
    emit playingChanged();
    followAll(true);
}

void Playback::tick()
{
    const int end = m_stopAt >= 0 ? qMin(m_stopAt, m_duration) : m_duration;
    int now = m_clockStart + int(m_clock.elapsed());
    const bool done = now >= end;
    if (done)
        now = end;
    if (now != m_position) {
        m_position = now;
        emit positionChanged();
    }
    updateTime();
    if (done)
        pause();
    else
        followAll(false);
}
