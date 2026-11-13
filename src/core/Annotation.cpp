#include "Annotation.h"

#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
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
    case Type::Pen:
        // Not a union of the points: a rectangle without size is null to
        // QRectF, and a union passes over it.
        return QPolygonF(points).boundingRect();
    case Type::Text:
    case Type::Counter:
        return QRectF(points.first(), QSizeF(0, 0));
    case Type::Line:
    case Type::Arrow:
        if (bends()) {
            QPainterPath curve(points[0]);
            curve.quadTo(points[2], points[1]);
            return curve.boundingRect();
        }
        return rect();
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
        // Bent into a loop, its ends may meet.
        return points.size() < 2 || (QLineF(points[0], points[1]).length() < 1 && !bends());
    case Type::Measure:
        return points.size() < 2 || QLineF(points[0], points[1]).length() < 1;
    case Type::Rectangle:
    case Type::Ellipse:
    case Type::Marker:
    case Type::Pixelate:
        return points.size() < 2 || rect().width() < 1 || rect().height() < 1;
    case Type::Image:
        return image.isNull() || points.size() < 2 || rect().width() < 1 || rect().height() < 1;
    case Type::Video:
        return source.isEmpty() || points.size() < 2 || rect().width() < 1 || rect().height() < 1;
    }
    return true;
}

bool Annotation::hasEndPoints() const
{
    return type == Type::Line || type == Type::Arrow || type == Type::Measure;
}

QPointF Annotation::bendHandle() const
{
    if (points.size() < 2)
        return points.value(0);
    const QPointF straight = (points[0] + points[1]) / 2;
    // A quadratic curve's middle is halfway from there to the control point.
    return bends() ? (straight + points[2]) / 2 : straight;
}

void Annotation::bendThrough(const QPointF &p)
{
    if (!canBend() || points.size() < 2)
        return;
    const QPointF straight = (points[0] + points[1]) / 2;
    points.resize(2);
    if (QLineF(straight, p).length() >= 2)
        points << 2 * p - straight;
}

bool Annotation::hasStroke() const
{
    return type == Type::Pen || type == Type::Line || type == Type::Arrow || type == Type::Rectangle
        || type == Type::Ellipse;
}

bool Annotation::hasBox() const
{
    return type == Type::Rectangle || type == Type::Ellipse || type == Type::Marker || type == Type::Pixelate
        || isPicture();
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
