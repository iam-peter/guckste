#pragma once

#include "Document.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <map>

class QAudioOutput;
class QMediaPlayer;
class QVideoSink;

// Plays a document's video clips together. A clock runs the document's
// time, and every clip has a player of its own that follows it: playing
// while the clip is on, paused on the frame of the time otherwise, and
// sending its frames to the document, which draws them with the rest.
class Playback : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Document *document READ document WRITE setDocument NOTIFY documentChanged)
    // The document's time, in ms.
    Q_PROPERTY(int position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(int duration READ duration NOTIFY durationChanged)
    // The highest of the clips', for stepping a frame at a time.
    Q_PROPERTY(double frameRate READ frameRate NOTIFY frameRateChanged)
    // Where playing stops, as at the end of a trim; -1 for the end.
    Q_PROPERTY(int stopAt READ stopAt WRITE setStopAt NOTIFY stopAtChanged)

public:
    explicit Playback(QObject *parent = nullptr);
    ~Playback() override;

    Document *document() const { return m_document; }
    void setDocument(Document *document);
    int position() const { return m_position; }
    void setPosition(int ms);
    bool playing() const { return m_playing; }
    int duration() const { return m_duration; }
    double frameRate() const { return m_frameRate; }
    int stopAt() const { return m_stopAt; }
    void setStopAt(int ms);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();

signals:
    void documentChanged();
    void positionChanged();
    void playingChanged();
    void durationChanged();
    void frameRateChanged();
    void stopAtChanged();

private:
    struct Clip
    {
        QString source;
        QMediaPlayer *player = nullptr;
        QVideoSink *sink = nullptr;
        QAudioOutput *audio = nullptr;
        bool loaded = false;
    };

    // A player for every clip of the document, and none for any other.
    void sync();
    // Brings a clip's player to the document's time; `seek` asks for the
    // exact frame even if it is close.
    void follow(int id, bool seek);
    void followAll(bool seek);
    void tick();
    void updateTime();
    int shownTime() const;

    QPointer<Document> m_document;
    std::map<int, Clip> m_clips;
    int m_position = 0;
    int m_duration = 0;
    bool m_playing = false;
    double m_frameRate = 30;
    int m_stopAt = -1;
    QElapsedTimer m_clock;
    int m_clockStart = 0;
    QTimer m_timer;
};
