#include "AnnotationLayer.h"

#include "core/AnnotationPainter.h"
#include "core/Document.h"

#include <QPainter>
#include <cmath>

AnnotationLayer::AnnotationLayer(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setFillColor(Qt::transparent);
}

void AnnotationLayer::setDocument(Document *document)
{
    if (m_document == document)
        return;
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (m_document) {
        connect(m_document, &Document::changed, this, [this] { update(); });
        connect(m_document, &Document::imageChanged, this, [this] { update(); });
        connect(m_document, &QObject::destroyed, this, [this] {
            m_document = nullptr;
            emit documentChanged();
        });
    }
    emit documentChanged();
    update();
}

void AnnotationLayer::setZoom(qreal zoom)
{
    if (qFuzzyCompare(m_zoom, zoom) || zoom <= 0)
        return;
    m_zoom = zoom;
    emit zoomChanged();
    update();
}

void AnnotationLayer::setOriginX(qreal x)
{
    if (qFuzzyCompare(m_origin.x(), x))
        return;
    m_origin.setX(x);
    emit originChanged();
    update();
}

void AnnotationLayer::setOriginY(qreal y)
{
    if (qFuzzyCompare(m_origin.y(), y))
        return;
    m_origin.setY(y);
    emit originChanged();
    update();
}

void AnnotationLayer::setShowGrid(bool on)
{
    if (m_showGrid == on)
        return;
    m_showGrid = on;
    emit showGridChanged();
    update();
}

void AnnotationLayer::setGridMinZoom(qreal zoom)
{
    if (qFuzzyCompare(m_gridMinZoom, zoom))
        return;
    m_gridMinZoom = zoom;
    emit showGridChanged();
    update();
}

void AnnotationLayer::setShowSelection(bool on)
{
    if (m_showSelection == on)
        return;
    m_showSelection = on;
    emit showSelectionChanged();
    update();
}

QPointF AnnotationLayer::toImage(qreal x, qreal y) const
{
    return QPointF(x / m_zoom + m_origin.x(), y / m_zoom + m_origin.y());
}

QPointF AnnotationLayer::toView(qreal x, qreal y) const
{
    return QPointF((x - m_origin.x()) * m_zoom, (y - m_origin.y()) * m_zoom);
}

void AnnotationLayer::paint(QPainter *painter)
{
    if (!m_document)
        return;
    const QImage &base = m_document->image();

    if (m_showGrid && m_zoom >= m_gridMinZoom && !base.isNull()) {
        // A line on every pixel edge of the image that is in view.
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(QColor(128, 128, 128, 140), 1));
        const qreal firstX = std::max<qreal>(0, std::ceil(m_origin.x()));
        const qreal lastX = std::min<qreal>(base.width(), m_origin.x() + width() / m_zoom);
        for (qreal ix = firstX; ix <= lastX; ix += 1) {
            const int x = qRound((ix - m_origin.x()) * m_zoom);
            painter->drawLine(x, 0, x, qRound(height()));
        }
        const qreal firstY = std::max<qreal>(0, std::ceil(m_origin.y()));
        const qreal lastY = std::min<qreal>(base.height(), m_origin.y() + height() / m_zoom);
        for (qreal iy = firstY; iy <= lastY; iy += 1) {
            const int y = qRound((iy - m_origin.y()) * m_zoom);
            painter->drawLine(0, y, qRound(width()), y);
        }
        painter->restore();
    }

    AnnotationPainter annotations(*painter, base, m_zoom, m_origin);
    const QList<Annotation> &list = m_document->annotations();
    for (const Annotation &a : list)
        annotations.paint(a);
    if (const Annotation *current = m_document->current())
        annotations.paint(*current);
    const int selected = m_document->selectedIndex();
    if (m_showSelection && selected >= 0 && selected < list.size())
        annotations.paintSelection(list[selected]);
}
