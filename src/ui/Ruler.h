#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// A ruler along the top or left of the editor, in image pixels: ticks
// spaced to suit the zoom, numbers every so often, and a mark where the
// pointer is. `origin` is the image coordinate at the ruler's start.
class Ruler : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool vertical READ vertical WRITE setVertical NOTIFY changed)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY changed)
    Q_PROPERTY(qreal origin READ origin WRITE setOrigin NOTIFY changed)
    // The pointer's image coordinate along the ruler; NaN for none.
    Q_PROPERTY(qreal pointer READ pointer WRITE setPointer NOTIFY changed)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground WRITE setForeground NOTIFY changed)

public:
    explicit Ruler(QQuickItem *parent = nullptr);

    bool vertical() const { return m_vertical; }
    void setVertical(bool vertical);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    qreal origin() const { return m_origin; }
    void setOrigin(qreal origin);
    qreal pointer() const { return m_pointer; }
    void setPointer(qreal pointer);
    QColor background() const { return m_background; }
    void setBackground(const QColor &color);
    QColor foreground() const { return m_foreground; }
    void setForeground(const QColor &color);

    // The distance between numbered ticks, in image pixels, for a zoom:
    // 1, 2, 5 times a power of ten, at least `minimum` screen pixels apart.
    static qreal labelStep(qreal zoom, qreal minimum = 60);

    void paint(QPainter *painter) override;

signals:
    void changed();

private:
    bool m_vertical = false;
    qreal m_zoom = 1;
    qreal m_origin = 0;
    qreal m_pointer = qQNaN();
    QColor m_background = QColor(0x2b, 0x2b, 0x30);
    QColor m_foreground = QColor(0xd0, 0xd0, 0xd0);
};
