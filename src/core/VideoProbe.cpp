#include "VideoProbe.h"

#include "VideoFrames.h"

#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVideoSink>

namespace {

// Lives until it has an answer, then deletes itself.
class Probe : public QObject
{
public:
    Probe(const QString &path, QObject *context, std::function<void(const VideoInfo &)> done)
        : m_context(context), m_done(std::move(done))
    {
        m_info.path = path;
        m_player.setVideoSink(&m_sink);
        connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::LoadedMedia && !m_loaded) {
                m_loaded = true;
                const QMediaMetaData meta = m_player.metaData();
                m_info.durationMs = m_player.duration();
                m_info.size = meta.value(QMediaMetaData::Resolution).toSize();
                m_info.frameRate = meta.value(QMediaMetaData::VideoFrameRate).toDouble();
                m_info.hasAudio = m_player.hasAudio();
                // Played, for the first frame: a player shows nothing
                // before. Without a sound output, none is heard.
                m_player.play();
            } else if (status == QMediaPlayer::InvalidMedia || status == QMediaPlayer::EndOfMedia) {
                finish();
            }
        });
        connect(&m_player, &QMediaPlayer::errorOccurred, this, [this] { finish(); });
        connect(&m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            if (!frame.isValid() || !m_info.poster.isNull())
                return;
            m_info.poster = VideoFrames::toImage(frame).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (m_info.size.isEmpty())
                m_info.size = m_info.poster.size();
            finish();
        });
        m_timeout.setSingleShot(true);
        m_timeout.setInterval(5000);
        connect(&m_timeout, &QTimer::timeout, this, [this] { finish(); });
        m_timeout.start();
        m_player.setSource(QUrl::fromLocalFile(path));
    }

private:
    void finish()
    {
        if (m_finished)
            return;
        m_finished = true;
        m_timeout.stop();
        // Not from within the player's own signal.
        QTimer::singleShot(0, this, [this] {
            m_player.stop();
            if (m_context)
                m_done(m_info);
            deleteLater();
        });
    }

    QPointer<QObject> m_context;
    std::function<void(const VideoInfo &)> m_done;
    VideoInfo m_info;
    QMediaPlayer m_player;
    QVideoSink m_sink;
    QTimer m_timeout;
    bool m_loaded = false;
    bool m_finished = false;
};

} // namespace

void VideoProbe::probe(const QString &path, QObject *context, std::function<void(const VideoInfo &)> done)
{
    new Probe(path, context, std::move(done));
}
