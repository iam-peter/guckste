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

void AnnotationLayer::setHiddenIndex(int index)
{
    if (m_hiddenIndex == index)
        return;
    m_hiddenIndex = index;
    emit hiddenIndexChanged();
    update();
}

void AnnotationLayer::paint(QPainter *painter)
{
    if (!m_document)
        return;
    const QRect canvas = m_document->canvas();

    if (m_showGrid && m_zoom >= m_gridMinZoom && !canvas.isEmpty()) {
        // A line on every pixel edge of the canvas that is in view.
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(QColor(128, 128, 128, 140), 1));
        const qreal top = std::max<qreal>(0, (canvas.top() - m_origin.y()) * m_zoom);
        const qreal bottom = std::min<qreal>(height(), (canvas.bottom() + 1 - m_origin.y()) * m_zoom);
        const qreal left = std::max<qreal>(0, (canvas.left() - m_origin.x()) * m_zoom);
        const qreal right = std::min<qreal>(width(), (canvas.right() + 1 - m_origin.x()) * m_zoom);
        const qreal firstX = std::max<qreal>(canvas.left(), std::ceil(m_origin.x()));
        const qreal lastX = std::min<qreal>(canvas.right() + 1, m_origin.x() + width() / m_zoom);
        for (qreal ix = firstX; ix <= lastX; ix += 1) {
            const int x = qRound((ix - m_origin.x()) * m_zoom);
            painter->drawLine(QPointF(x, top), QPointF(x, bottom));
        }
        const qreal firstY = std::max<qreal>(canvas.top(), std::ceil(m_origin.y()));
        const qreal lastY = std::min<qreal>(canvas.bottom() + 1, m_origin.y() + height() / m_zoom);
        for (qreal iy = firstY; iy <= lastY; iy += 1) {
            const int y = qRound((iy - m_origin.y()) * m_zoom);
            painter->drawLine(QPointF(left, y), QPointF(right, y));
        }
        painter->restore();
    }

    // The screenshot itself is the Image item below; this draws what is
    // on it, added images included.
    m_document->paintLayers(*painter, m_zoom, m_origin, -1, false, m_hiddenIndex, true);
    const QList<Annotation> &list = m_document->annotations();
    const int selected = m_document->selectedIndex();
    if (m_showSelection && selected >= 0 && selected < list.size() && selected != m_hiddenIndex) {
        AnnotationPainter frame(*painter, m_document->image(), m_zoom, m_origin);
        frame.paintSelection(list[selected]);
    }
}
