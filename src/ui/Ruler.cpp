#include "Ruler.h"

#include <QFont>
#include <QPainter>
#include <cmath>

Ruler::Ruler(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(false);
}

void Ruler::setVertical(bool vertical)
{
    if (m_vertical == vertical)
        return;
    m_vertical = vertical;
    emit changed();
    update();
}

void Ruler::setZoom(qreal zoom)
{
    if (qFuzzyCompare(m_zoom, zoom) || zoom <= 0)
        return;
    m_zoom = zoom;
    emit changed();
    update();
}

void Ruler::setOrigin(qreal origin)
{
    if (qFuzzyCompare(m_origin, origin))
        return;
    m_origin = origin;
    emit changed();
    update();
}

void Ruler::setPointer(qreal pointer)
{
    if (m_pointer == pointer || (qIsNaN(m_pointer) && qIsNaN(pointer)))
        return;
    m_pointer = pointer;
    emit changed();
    update();
}

void Ruler::setBackground(const QColor &color)
{
    if (m_background == color)
        return;
    m_background = color;
    emit changed();
    update();
}

void Ruler::setForeground(const QColor &color)
{
    if (m_foreground == color)
        return;
    m_foreground = color;
    emit changed();
    update();
}

qreal Ruler::labelStep(qreal zoom, qreal minimum)
{
    for (qreal power = 1;; power *= 10) {
        for (qreal factor : {1.0, 2.0, 5.0}) {
            if (factor * power * zoom >= minimum)
                return factor * power;
        }
        if (power > 1e7)
            return power;
    }
}

void Ruler::paint(QPainter *painter)
{
    const qreal length = m_vertical ? height() : width();
    const qreal thickness = m_vertical ? width() : height();
    painter->fillRect(QRectF(0, 0, width(), height()), m_background);

    // Along the ruler, image coordinate v sits at (v - origin) * zoom.
    if (m_vertical) {
        // Drawn as if horizontal, turned so its numbers read upwards.
        painter->translate(0, height());
        painter->rotate(-90);
    }
    // Turned, the ruler runs bottom to top; counting from the top again:
    const auto at = [&](qreal v) {
        const qreal p = (v - m_origin) * m_zoom;
        return m_vertical ? length - p : p;
    };

    const qreal step = labelStep(m_zoom);
    // Minor ticks: tenths, fifths or halves of a step, as far as they fit.
    qreal minor = step;
    for (qreal parts : {10.0, 5.0, 2.0}) {
        if (step / parts >= 1 && step / parts * m_zoom >= 5) {
            minor = step / parts;
            break;
        }
    }

    QPen pen(m_foreground, 1);
    painter->setPen(pen);
    QFont font;
    font.setPixelSize(10);
    painter->setFont(font);

    const qreal first = m_origin;
    const qreal last = m_origin + length / m_zoom;
    for (qreal v = std::floor(first / minor) * minor; v <= last; v += minor) {
        const qreal p = std::round(at(v)) + 0.5;
        const bool major = std::abs(std::remainder(v, step)) < minor / 2;
        const bool half = !major && std::abs(std::remainder(v, step / 2)) < minor / 2;
        const qreal tick = major ? thickness : (half ? thickness * 0.45 : thickness * 0.25);
        painter->drawLine(QPointF(p, thickness - tick), QPointF(p, thickness));
        if (major) {
            const QString label = QString::number(qRound(v));
            // Beside its tick, reading along the ruler.
            if (m_vertical)
                painter->drawText(QRectF(p - 2 - 60, 0, 60, thickness - 6), Qt::AlignRight | Qt::AlignTop, label);
            else
                painter->drawText(QRectF(p + 3, 0, 60, thickness - 6), Qt::AlignLeft | Qt::AlignTop, label);
        }
    }

    // The edge towards the canvas.
    painter->drawLine(QPointF(0, thickness - 0.5), QPointF(length, thickness - 0.5));

    if (!qIsNaN(m_pointer)) {
        const qreal p = std::round(at(m_pointer)) + 0.5;
        painter->setPen(QPen(QColor(0xff, 0x8a, 0x3d), 1));
        painter->drawLine(QPointF(p, 0), QPointF(p, thickness));
    }
}
