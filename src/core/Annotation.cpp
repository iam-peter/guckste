#include "Annotation.h"

#include <QLineF>
#include <cmath>

QRectF Annotation::rect() const
{
    if (points.size() < 2)
        return points.isEmpty() ? QRectF() : QRectF(points.first(), QSizeF(0, 0));
    return QRectF(points[0], points[1]).normalized();
}

QRectF Annotation::geometryBounds() const
{
    if (points.isEmpty())
        return {};
    switch (type) {
    case Type::Pen: {
        QRectF bounds(points.first(), QSizeF(0, 0));
        for (const QPointF &p : points)
            bounds |= QRectF(p, QSizeF(0, 0));
        return bounds;
    }
    case Type::Text:
    case Type::Counter:
        return QRectF(points.first(), QSizeF(0, 0));
    default:
        return rect();
    }
}

void Annotation::translate(const QPointF &delta)
{
    for (QPointF &p : points)
        p += delta;
}

bool Annotation::isEmpty() const
{
    switch (type) {
    case Type::Pen:
        return points.isEmpty();
    case Type::Text:
        return points.isEmpty() || text.trimmed().isEmpty();
    case Type::Counter:
        return points.isEmpty();
    case Type::Line:
    case Type::Arrow:
    case Type::Measure:
        return points.size() < 2 || QLineF(points[0], points[1]).length() < 1;
    case Type::Rectangle:
    case Type::Ellipse:
    case Type::Marker:
    case Type::Pixelate:
        return points.size() < 2 || rect().width() < 1 || rect().height() < 1;
    }
    return true;
}

bool Annotation::isAxisAligned(const QPointF &a, const QPointF &b)
{
    return qFuzzyCompare(a.x(), b.x()) || qFuzzyCompare(a.y(), b.y());
}

qreal Annotation::measuredLength(const QPointF &a, const QPointF &b)
{
    if (qFuzzyCompare(a.y(), b.y()))
        return std::abs(b.x() - a.x());
    if (qFuzzyCompare(a.x(), b.x()))
        return std::abs(b.y() - a.y());
    return std::round(QLineF(a, b).length());
}
