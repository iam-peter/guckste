#pragma once

#include <QColor>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

// One mark on the screenshot. Geometry is in image pixels so marks stay
// put when the view is zoomed; `size` is a stroke width in output pixels,
// so a 3 px pen stays 3 px thin on a 400 % export, which is what makes
// marks on an enlarged screenshot readable.
struct Annotation
{
    enum class Type { Pen, Line, Arrow, Rectangle, Ellipse, Marker, Text, Pixelate, Counter, Measure };

    Type type = Type::Pen;
    QList<QPointF> points;
    QColor color = QColor(255, 59, 48);
    qreal size = 3;
    QString text;
    int number = 0;

    // The normalized rectangle spanned by the first two points.
    QRectF rect() const;
    // The geometry's extent in image pixels, without stroke, labels or text.
    QRectF geometryBounds() const;
    void translate(const QPointF &delta);
    // True while there is nothing worth keeping, like a zero-length line.
    bool isEmpty() const;

    // The distance a Measure mark shows, in image pixels: exact along an
    // axis, rounded otherwise.
    static qreal measuredLength(const QPointF &a, const QPointF &b);
    static bool isAxisAligned(const QPointF &a, const QPointF &b);
};
