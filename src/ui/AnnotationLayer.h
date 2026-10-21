#pragma once

#include "core/Document.h"

#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// Paints a document's annotations over the image at the view's zoom. Sized
// to the viewport, not the image, so a zoomed-in screenshot never needs a
// texture bigger than the screen; `originX/Y` is the image pixel at the
// item's top-left corner.
class AnnotationLayer : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Document *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(qreal originX READ originX WRITE setOriginX NOTIFY originChanged)
    Q_PROPERTY(qreal originY READ originY WRITE setOriginY NOTIFY originChanged)
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY showGridChanged)
    Q_PROPERTY(qreal gridMinZoom READ gridMinZoom WRITE setGridMinZoom NOTIFY showGridChanged)
    Q_PROPERTY(bool showSelection READ showSelection WRITE setShowSelection NOTIFY showSelectionChanged)
    // A mark left out, like a text while it is edited in place.
    Q_PROPERTY(int hiddenIndex READ hiddenIndex WRITE setHiddenIndex NOTIFY hiddenIndexChanged)

public:
    explicit AnnotationLayer(QQuickItem *parent = nullptr);

    Document *document() const { return m_document; }
    void setDocument(Document *document);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    qreal originX() const { return m_origin.x(); }
    void setOriginX(qreal x);
    qreal originY() const { return m_origin.y(); }
    void setOriginY(qreal y);
    bool showGrid() const { return m_showGrid; }
    void setShowGrid(bool on);
    qreal gridMinZoom() const { return m_gridMinZoom; }
    void setGridMinZoom(qreal zoom);
    bool showSelection() const { return m_showSelection; }
    void setShowSelection(bool on);
    int hiddenIndex() const { return m_hiddenIndex; }
    void setHiddenIndex(int index);

    // Between view and image coordinates.
    Q_INVOKABLE QPointF toImage(qreal x, qreal y) const;
    Q_INVOKABLE QPointF toView(qreal x, qreal y) const;

    void paint(QPainter *painter) override;

signals:
    void documentChanged();
    void zoomChanged();
    void originChanged();
    void showGridChanged();
    void showSelectionChanged();
    void hiddenIndexChanged();

private:
    Document *m_document = nullptr;
    qreal m_zoom = 1;
    QPointF m_origin;
    bool m_showGrid = false;
    qreal m_gridMinZoom = 8;
    bool m_showSelection = true;
    int m_hiddenIndex = -1;
};
