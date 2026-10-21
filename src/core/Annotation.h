#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

// One mark on the screenshot, or another image placed beside or over it.
// Geometry and sizes are in image pixels, so marks zoom with the image as
// if drawn into it; only Measure marks keep their size on screen.
struct Annotation
{
    enum class Type { Pen, Line, Arrow, Rectangle, Ellipse, Marker, Text, Pixelate, Counter, Measure, Image };

    Type type = Type::Pen;
    QList<QPointF> points;
    QColor color = QColor(255, 59, 48);
    // The stroke width, or a text's font size, in image pixels; for a
    // Measure mark in screen pixels.
    qreal size = 3;
    QString text;
    int number = 0;
    // An Image's pixels, drawn into the rectangle of its two points.
    QImage image;
    // On a recording: shown from startMs until endMs, in the recording's
    // own time; endMs -1 is to the end.
    int startMs = 0;
    int endMs = -1;
    bool shownAt(qint64 ms) const { return ms < 0 || (ms >= startMs && (endMs < 0 || ms < endMs)); }

    // The normalized rectangle spanned by the first two points.
    QRectF rect() const;
    // The geometry's extent in image pixels, without stroke, labels or text.
    QRectF geometryBounds() const;
    void translate(const QPointF &delta);
    // True while there is nothing worth keeping, like a zero-length line.
    bool isEmpty() const;
    // Two end points to drag, like a line's.
    bool hasEndPoints() const;
    // A box to resize by its corners and edges, like a rectangle's.
    bool hasBox() const;

    // The distance a Measure mark shows, in image pixels: exact along an
    // axis, rounded otherwise.
    static qreal measuredLength(const QPointF &a, const QPointF &b);
    static bool isAxisAligned(const QPointF &a, const QPointF &b);
};
