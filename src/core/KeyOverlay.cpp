#include "KeyOverlay.h"

#include <QFontMetricsF>

KeyOverlay::KeyOverlay(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
}

void KeyOverlay::add(const QString &label)
{
    expire();
    const qint64 now = m_clock.elapsed();
    // The same key again counts up rather than lining up.
    if (!m_keys.isEmpty() && m_keys.last().label == label && label.size() > 1) {
        m_keys.last().times++;
        m_keys.last().at = now;
    } else {
        m_keys.append({label, now, 1});
    }
    while (m_keys.size() > maxKeys)
        m_keys.removeFirst();
}

void KeyOverlay::clear()
{
    m_keys.clear();
}

void KeyOverlay::expire() const
{
    const qint64 now = m_clock.elapsed();
    while (!m_keys.isEmpty() && now - m_keys.first().at > lingerMs)
        m_keys.removeFirst();
}

bool KeyOverlay::isEmpty() const
{
    expire();
    return m_keys.isEmpty();
}

QStringList KeyOverlay::labels() const
{
    expire();
    QStringList out;
    for (const Key &k : std::as_const(m_keys))
        out << (k.times > 1 ? QStringLiteral("%1 ×%2").arg(k.label).arg(k.times) : k.label);
    return out;
}

void KeyOverlay::paint(QPainter &painter, const QRect &frame) const
{
    const QStringList texts = labels();
    if (texts.isEmpty())
        return;
    QFont font;
    font.setPixelSize(qBound(14, frame.height() / 30, 28));
    font.setBold(true);
    const QFontMetricsF metrics(font);
    const qreal padX = font.pixelSize() * 0.6, padY = font.pixelSize() * 0.35, gap = font.pixelSize() * 0.4;
    qreal total = 0;
    QList<qreal> widths;
    for (const QString &t : texts) {
        const qreal w = metrics.horizontalAdvance(t) + 2 * padX;
        widths << w;
        total += w + gap;
    }
    total -= gap;
    const qreal height = metrics.height() + 2 * padY;
    qreal x = frame.x() + (frame.width() - total) / 2;
    const qreal y = frame.y() + frame.height() - height - frame.height() / 16.0;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(font);
    const qint64 now = m_clock.elapsed();
    for (int i = 0; i < texts.size(); ++i) {
        // Fading out over the last part of its time.
        const qint64 age = now - m_keys[i].at;
        const qreal alpha = age > lingerMs - 600 ? qMax<qreal>(0.15, (lingerMs - age) / 600.0) : 1.0;
        painter.setOpacity(alpha);
        const QRectF chip(x, y, widths[i], height);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 20, 24, 200));
        painter.drawRoundedRect(chip, 6, 6);
        painter.setPen(Qt::white);
        painter.drawText(chip, Qt::AlignCenter, texts[i]);
        x += widths[i] + gap;
    }
    painter.restore();
}
