#pragma once

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <functional>

// What a video file holds, as far as placing it as a clip needs to know.
struct VideoInfo
{
    QString path;
    QSize size;
    qint64 durationMs = 0;
    double frameRate = 0;
    bool hasAudio = false;
    // Its first frame, to show until a player has one.
    QImage poster;

    bool isValid() const { return !size.isEmpty() && durationMs > 0; }
};

namespace VideoProbe {

// Opens the file in a player of its own and calls `done` once it knows,
// or after a few seconds with what it found; not if `context` is gone.
void probe(const QString &path, QObject *context, std::function<void(const VideoInfo &)> done);

} // namespace VideoProbe
