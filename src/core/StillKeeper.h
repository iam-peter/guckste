#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>
#include <QThreadPool>

#include <atomic>

// A frame of a recording kept as it was on screen, before the video's
// encoding made it lossy: written as a PNG, and found again at its time
// in the editor.
struct Still
{
    enum class Kind { Kept, BeforeClick, AfterClick, Change };

    QString path;
    // Its time in the recording.
    int ms = 0;
    Kind kind = Kind::Kept;
    // The pointer, left out of the picture to be a layer of its own: its
    // image and where that goes, in the still's pixels. None when the
    // pointer is not recorded.
    QImage pointer;
    QRectF pointerRect;
};

// Picks the stills of a recording from its frames: the one shown when
// asked to keep one; around a click the one before it, which shows what
// the pointer hovered, and the one after, once the screen has settled on
// what the click did; and, when asked to, the one the screen settles on
// after any change big enough to be more than a blinking caret, like a
// tooltip or a hover highlight. Writing the files is left to a thread.
class StillKeeper
{
public:
    struct Frame
    {
        // Without the pointer, at `ms` of the recording.
        QImage image;
        int ms = 0;
        QImage pointer;
        QRectF pointerRect;
    };

    // How long the screen stays as it is to count as settled.
    static constexpr int settleMs = 300;
    // The same without a click, shorter: the pointer resting a moment on
    // a menu item is worth a still of its highlight.
    static constexpr int changeSettleMs = 200;
    // How long a click may take to change anything; after that it did not.
    static constexpr int waitForChangeMs = 2000;
    // The longest wait for the screen to settle, as while a video plays.
    static constexpr int maxWaitMs = 4000;
    // The fewest pixels a change turns to be kept, in logical pixels: a
    // tooltip or a button's highlight, not a caret or a clock's digits,
    // however far apart they are.
    static constexpr int minimumChangeArea = 1200;
    // Stills waiting to be written at most; one more waits for them.
    static constexpr int maxQueued = 6;

    StillKeeper();
    ~StillKeeper();

    // Starts over, the stills to go into `directory`, which is made with
    // the first.
    void begin(const QString &directory);
    // Keeps what the screen settles on after a change, judging the size
    // of a change at `devicePixelRatio`; from the next begin() on.
    void setKeepChanges(bool on, qreal devicePixelRatio = 1);
    void frame(const Frame &frame);
    // A click in the recorded region.
    void press();
    // The frame shown now, asked for.
    void keep();
    // Settles what a click still waits for and waits for the files.
    void finish();

    QList<Still> stills() const { return m_stills; }
    // How many pixels differ between two pictures of a size.
    static qint64 changedPixels(const QImage &a, const QImage &b);
    QString directory() const { return m_directory; }

private:
    // False when it is the same picture as the still kept before.
    bool add(const Frame &frame, Still::Kind kind, bool evenIfSame = false);
    // A change from what the screen settled on last worth a still.
    bool isBig(const QImage &from, const QImage &to) const;
    void followChanges(const Frame &frame);

    QString m_directory;
    QList<Still> m_stills;
    Frame m_latest;
    QImage m_lastKept;
    // A click waiting for the screen to settle.
    bool m_pending = false;
    bool m_changed = false;
    int m_pressMs = 0;
    int m_lastChangeMs = 0;
    // Changes without a click: what the screen settled on last, and since
    // when it has been changing away from it.
    bool m_keepChanges = false;
    int m_minimumArea = minimumChangeArea;
    QImage m_settled;
    bool m_changing = false;
    int m_changeStartMs = 0;
    int m_lastMoveMs = 0;
    QThreadPool m_writers;
    std::atomic<int> m_queued{0};
};
