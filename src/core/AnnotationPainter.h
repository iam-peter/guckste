#pragma once

#include "Annotation.h"

#include <QFont>
#include <QImage>
#include <QPainter>

// Draws annotations with a QPainter. Image coordinates are mapped to the
// painter's as (p - origin) * scale, and stroke widths, text and labels are
// in the painter's pixels: the same painter fills the on-screen layer at the
// view's zoom and the exported image at its scale, so what is exported is
// what was seen.
class AnnotationPainter
{
public:
    AnnotationPainter(QPainter &painter, const QImage &base, qreal scale, QPointF origin = {});

    void paint(const Annotation &annotation);
    // A dashed frame around an annotation, for the one that is selected.
    void paintSelection(const Annotation &annotation);

    // The annotation's extent in image pixels, stroke and labels included.
    QRectF bounds(const Annotation &annotation) const;

    QPointF map(const QPointF &p) const { return (p - m_origin) * m_scale; }
    QRectF map(const QRectF &r) const { return QRectF(map(r.topLeft()), r.size() * m_scale); }
    QPointF unmap(const QPointF &p) const { return p / m_scale + m_origin; }

    static QFont textFont(qreal size);
    static qreal counterRadius(qreal size);
    static QString measureLabel(const Annotation &annotation);
    static int pixelateBlock(qreal size);

private:
    void paintArrow(const Annotation &a);
    void paintText(const Annotation &a);
    void paintPixelate(const Annotation &a);
    void paintCounter(const Annotation &a);
    void paintMeasure(const Annotation &a);
    QRectF labelRect(const Annotation &a) const;

    QPainter &m_painter;
    const QImage &m_base;
    qreal m_scale;
    QPointF m_origin;
};
