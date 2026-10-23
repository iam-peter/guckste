#include "StillKeeper.h"

#include <QDir>

#include <cstring>

namespace {

QString slugOf(Still::Kind kind)
{
    switch (kind) {
    case Still::Kind::BeforeClick:
        return QStringLiteral("before-click");
    case Still::Kind::AfterClick:
        return QStringLiteral("after-click");
    case Still::Kind::Change:
        return QStringLiteral("change");
    case Still::Kind::Kept:
        break;
    }
    return QStringLiteral("kept");
}

} // namespace

StillKeeper::StillKeeper()
{
    m_writers.setMaxThreadCount(2);
}

StillKeeper::~StillKeeper()
{
    m_writers.waitForDone();
}

void StillKeeper::begin(const QString &directory)
{
    m_writers.waitForDone();
    m_directory = directory;
    m_stills.clear();
    m_latest = {};
    m_lastKept = QImage();
    m_pending = false;
    m_settled = QImage();
    m_changing = false;
}

void StillKeeper::setKeepChanges(bool on, qreal devicePixelRatio)
{
    m_keepChanges = on;
    m_minimumArea = qRound(minimumChangeArea * devicePixelRatio * devicePixelRatio);
}

qint64 StillKeeper::changedPixels(const QImage &a, const QImage &b)
{
    if (a.size() != b.size() || a.format() != b.format() || a.depth() != 32)
        return a == b ? 0 : qint64(qMax(a.width(), b.width())) * qMax(a.height(), b.height());
    const int width = a.width();
    qint64 changed = 0;
    for (int y = 0; y < a.height(); ++y) {
        const auto *rowA = reinterpret_cast<const quint32 *>(a.constScanLine(y));
        const auto *rowB = reinterpret_cast<const quint32 *>(b.constScanLine(y));
        if (std::memcmp(rowA, rowB, size_t(width) * 4) == 0)
            continue;
        for (int x = 0; x < width; ++x)
            changed += rowA[x] != rowB[x];
    }
    return changed;
}

bool StillKeeper::isBig(const QImage &from, const QImage &to) const
{
    return changedPixels(from, to) >= m_minimumArea;
}

void StillKeeper::followChanges(const Frame &frame)
{
    if (m_settled.isNull())
        m_settled = frame.image;
    // A click looks after what it changes.
    if (m_pending) {
        m_changing = false;
        return;
    }
    if (m_latest.image.isNull() || frame.image == m_latest.image) {
        if (m_changing && frame.ms - m_lastMoveMs >= settleMs) {
            m_changing = false;
            if (isBig(m_settled, frame.image))
                add(frame, Still::Kind::Change);
            else
                m_settled = frame.image;
        }
        return;
    }
    m_lastMoveMs = frame.ms;
    if (!m_changing && isBig(m_settled, frame.image)) {
        m_changing = true;
        m_changeStartMs = frame.ms;
    } else if (m_changing && frame.ms - m_changeStartMs >= maxWaitMs) {
        // It keeps moving: what it shows now, and on from there.
        add(frame, Still::Kind::Change);
        m_changing = true;
        m_changeStartMs = frame.ms;
    }
}

void StillKeeper::frame(const Frame &frame)
{
    if (m_keepChanges)
        followChanges(frame);
    if (m_pending) {
        if (frame.image != m_latest.image) {
            m_changed = true;
            m_lastChangeMs = frame.ms;
        }
        const int waited = frame.ms - m_pressMs;
        if ((m_changed && frame.ms - m_lastChangeMs >= settleMs) || (m_changed && waited >= maxWaitMs)) {
            add(frame, Still::Kind::AfterClick);
            m_pending = false;
        } else if (!m_changed && waited >= waitForChangeMs) {
            m_pending = false;
        }
    }
    m_latest = frame;
}

void StillKeeper::press()
{
    if (m_latest.image.isNull())
        return;
    // Clicked again before the screen settled: what the last click did is
    // what this one starts from.
    if (m_pending && m_changed)
        add(m_latest, Still::Kind::AfterClick);
    add(m_latest, Still::Kind::BeforeClick);
    m_pending = true;
    m_changed = false;
    m_pressMs = m_latest.ms;
    m_lastChangeMs = m_latest.ms;
}

void StillKeeper::keep()
{
    if (!m_latest.image.isNull())
        add(m_latest, Still::Kind::Kept, true);
}

void StillKeeper::finish()
{
    if (m_pending && m_changed)
        add(m_latest, Still::Kind::AfterClick);
    else if (m_changing && isBig(m_settled, m_latest.image))
        add(m_latest, Still::Kind::Change);
    m_pending = false;
    m_changing = false;
    m_writers.waitForDone();
}

bool StillKeeper::add(const Frame &frame, Still::Kind kind, bool evenIfSame)
{
    if (!evenIfSame && frame.image == m_lastKept)
        return false;
    if (m_stills.isEmpty())
        QDir().mkpath(m_directory);
    Still still;
    still.path = QDir(m_directory).filePath(
        QStringLiteral("%1-%2.png").arg(m_stills.size() + 1, 2, 10, QLatin1Char('0')).arg(slugOf(kind)));
    still.ms = frame.ms;
    still.kind = kind;
    still.pointer = frame.pointer;
    still.pointerRect = frame.pointerRect;
    m_stills << still;
    m_lastKept = frame.image;
    // What changes from here on is measured from it.
    m_settled = frame.image;
    // Written fast rather than small, and not piling up: a big screen
    // takes a while, and clicks can come quicker.
    if (m_queued >= maxQueued)
        m_writers.waitForDone();
    const QImage image = frame.image;
    const QString path = still.path;
    ++m_queued;
    m_writers.start([this, image, path] {
        if (!image.save(path, "PNG", 80))
            qWarning("Could not write %s", qPrintable(path));
        --m_queued;
    });
    return true;
}
