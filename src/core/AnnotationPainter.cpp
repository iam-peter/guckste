#include "AnnotationPainter.h"

#include <QFontMetricsF>
#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
#include <QtMath>

namespace {

QPen strokePen(const Annotation &a)
{
    QPen pen(a.color, a.size, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    return pen;
}

// Black on light colors, white on dark ones.
QColor contrasting(const QColor &c)
{
    const qreal luminance = 0.299 * c.redF() + 0.587 * c.greenF() + 0.114 * c.blueF();
    return luminance > 0.6 ? QColor(Qt::black) : QColor(Qt::white);
}

QPointF snapToPixelCenter(const QPointF &p)
{
    return QPointF(std::floor(p.x()) + 0.5, std::floor(p.y()) + 0.5);
}

} // namespace

AnnotationPainter::AnnotationPainter(QPainter &painter, const QImage &base, qreal scale, QPointF origin)
    : m_painter(painter), m_base(base), m_scale(scale), m_origin(origin)
{
}

QFont AnnotationPainter::textFont(qreal size)
{
    QFont font;
    font.setPixelSize(qMax(1, qRound(size)));
    return font;
}

qreal AnnotationPainter::counterRadius(qreal size)
{
    return 9 + size * 1.5;
}

int AnnotationPainter::pixelateBlock(qreal size)
{
    return qMax(2, qRound(2 + size * 2));
}

QString AnnotationPainter::measureLabel(const Annotation &a)
{
    if (a.points.size() < 2)
        return {};
    const qreal length = Annotation::measuredLength(a.points[0], a.points[1]);
    const QString prefix = Annotation::isAxisAligned(a.points[0], a.points[1]) ? QString() : QStringLiteral("≈");
    return prefix + QString::number(qRound(length)) + QStringLiteral(" px");
}

void AnnotationPainter::paint(const Annotation &a)
{
    if (a.points.isEmpty())
        return;
    m_painter.save();
    m_painter.setRenderHint(QPainter::Antialiasing, true);
    m_painter.setRenderHint(QPainter::TextAntialiasing, true);
    switch (a.type) {
    case Annotation::Type::Pen: {
        m_painter.setPen(strokePen(a));
        m_painter.setBrush(Qt::NoBrush);
        if (a.points.size() == 1) {
            m_painter.drawPoint(map(a.points.first()));
        } else {
            QPolygonF polygon;
            polygon.reserve(a.points.size());
            for (const QPointF &p : a.points)
                polygon << map(p);
            m_painter.drawPolyline(polygon);
        }
        break;
    }
    case Annotation::Type::Line:
        if (a.points.size() >= 2) {
            m_painter.setPen(strokePen(a));
            m_painter.drawLine(map(a.points[0]), map(a.points[1]));
        }
        break;
    case Annotation::Type::Arrow:
        paintArrow(a);
        break;
    case Annotation::Type::Rectangle:
        m_painter.setPen(strokePen(a));
        m_painter.setBrush(Qt::NoBrush);
        m_painter.drawRect(map(a.rect()));
        break;
    case Annotation::Type::Ellipse:
        m_painter.setPen(strokePen(a));
        m_painter.setBrush(Qt::NoBrush);
        m_painter.drawEllipse(map(a.rect()));
        break;
    case Annotation::Type::Marker: {
        QColor fill = a.color;
        fill.setAlphaF(0.4);
        m_painter.setPen(Qt::NoPen);
        m_painter.setBrush(fill);
        m_painter.drawRect(map(a.rect()));
        break;
    }
    case Annotation::Type::Text:
        paintText(a);
        break;
    case Annotation::Type::Pixelate:
        paintPixelate(a);
        break;
    case Annotation::Type::Counter:
        paintCounter(a);
        break;
    case Annotation::Type::Measure:
        paintMeasure(a);
        break;
    case Annotation::Type::Image:
        paintImage(a);
        break;
    }
    m_painter.restore();
}

void AnnotationPainter::paintArrow(const Annotation &a)
{
    if (a.points.size() < 2)
        return;
    const QPointF from = map(a.points[0]);
    const QPointF to = map(a.points[1]);
    const QLineF line(from, to);
    if (line.length() < 0.5)
        return;
    const qreal head = 6 + a.size * 2.5;
    const qreal angle = qDegreesToRadians(line.angle());
    const QPointF dir(std::cos(angle), -std::sin(angle));
    const QPointF normal(-dir.y(), dir.x());
    const QPointF base = to - dir * head;
    const QPolygonF tip{to, base + normal * head * 0.5, base - normal * head * 0.5};

    m_painter.setPen(strokePen(a));
    // Stop the shaft inside the head so round caps don't poke out of it.
    m_painter.drawLine(from, to - dir * head * 0.6);
    m_painter.setPen(Qt::NoPen);
    m_painter.setBrush(a.color);
    m_painter.drawPolygon(tip);
}

void AnnotationPainter::paintText(const Annotation &a)
{
    if (a.text.isEmpty())
        return;
    m_painter.setFont(textFont(a.size));
    m_painter.setPen(a.color);
    const QPointF at = map(a.points.first());
    m_painter.drawText(QRectF(at, QSizeF(1e5, 1e5)), Qt::AlignLeft | Qt::AlignTop, a.text);
}

void AnnotationPainter::paintPixelate(const Annotation &a)
{
    const QRect source = m_backdrop ? a.rect().toAlignedRect() : a.rect().toAlignedRect() & m_base.rect();
    if (source.isEmpty())
        return;
    const int block = pixelateBlock(a.size);
    const QImage region = m_backdrop ? m_backdrop(source) : m_base.copy(source);
    const QImage coarse = region.scaled(qMax(1, (source.width() + block - 1) / block),
                                        qMax(1, (source.height() + block - 1) / block),
                                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    m_painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    m_painter.drawImage(map(QRectF(source)), coarse);
}

void AnnotationPainter::paintImage(const Annotation &a)
{
    if (a.image.isNull())
        return;
    const QRectF rect = a.rect();
    // Enlarged with the view, pixels stay blocks like the screenshot's;
    // an image resized by hand or shrunk is smoothed.
    const bool resized = std::abs(rect.width() - a.image.width()) > 0.5 || std::abs(rect.height() - a.image.height()) > 0.5;
    m_painter.setRenderHint(QPainter::Antialiasing, false);
    m_painter.setRenderHint(QPainter::SmoothPixmapTransform, resized || m_scale < 1);
    m_painter.drawImage(map(rect), a.image);
}

void AnnotationPainter::paintCounter(const Annotation &a)
{
    const QPointF center = map(a.points.first());
    const qreal r = counterRadius(a.size);
    m_painter.setPen(Qt::NoPen);
    m_painter.setBrush(a.color);
    m_painter.drawEllipse(center, r, r);
    QFont font;
    font.setPixelSize(qRound(r * 1.15));
    font.setBold(true);
    m_painter.setFont(font);
    m_painter.setPen(contrasting(a.color));
    m_painter.drawText(QRectF(center.x() - r, center.y() - r, 2 * r, 2 * r), Qt::AlignCenter,
                       QString::number(a.number));
}

QRectF AnnotationPainter::labelRect(const Annotation &a) const
{
    // In painter pixels: the label of a Measure mark, beside its midpoint.
    QFont font;
    font.setPixelSize(12);
    font.setBold(true);
    const QFontMetricsF metrics(font);
    const QString label = measureLabel(a);
    const QSizeF textSize = metrics.size(Qt::TextSingleLine, label);
    const QSizeF boxSize(textSize.width() + 10, textSize.height() + 4);

    const QPointF p1 = snapToPixelCenter(map(a.points[0]));
    const QPointF p2 = snapToPixelCenter(map(a.points[1]));
    const QPointF mid = (p1 + p2) / 2;
    const bool horizontal = std::abs(p2.x() - p1.x()) >= std::abs(p2.y() - p1.y());
    const qreal gap = 8 + a.size;
    QPointF topLeft;
    if (horizontal)
        topLeft = QPointF(mid.x() - boxSize.width() / 2, mid.y() - gap - boxSize.height());
    else
        topLeft = QPointF(mid.x() + gap, mid.y() - boxSize.height() / 2);
    return QRectF(topLeft, boxSize);
}

void AnnotationPainter::paintMeasure(const Annotation &a)
{
    if (a.points.size() < 2)
        return;
    // Snapped to pixel centers so a 1 px line stays crisp.
    const QPointF p1 = snapToPixelCenter(map(a.points[0]));
    const QPointF p2 = snapToPixelCenter(map(a.points[1]));
    const QLineF line(p1, p2);
    if (line.length() < 0.5)
        return;
    const qreal width = qMax<qreal>(1, qRound(a.size / 2));
    const qreal tick = 4 + width * 2;
    const QLineF unit = line.unitVector();
    const QPointF normal(-unit.dy(), unit.dx());

    QPen pen(a.color, width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
    m_painter.setPen(pen);
    m_painter.setBrush(Qt::NoBrush);
    m_painter.drawLine(p1, p2);
    m_painter.drawLine(p1 + normal * tick, p1 - normal * tick);
    m_painter.drawLine(p2 + normal * tick, p2 - normal * tick);

    const QRectF box = labelRect(a);
    QFont font;
    font.setPixelSize(12);
    font.setBold(true);
    m_painter.setFont(font);
    m_painter.setPen(Qt::NoPen);
    m_painter.setBrush(a.color);
    m_painter.drawRoundedRect(box, 3, 3);
    m_painter.setPen(contrasting(a.color));
    m_painter.drawText(box, Qt::AlignCenter, measureLabel(a));
}

QRectF AnnotationPainter::bounds(const Annotation &a) const
{
    if (a.points.isEmpty())
        return {};
    // Worked out in painter pixels, where strokes and labels have their size.
    QRectF painted;
    switch (a.type) {
    case Annotation::Type::Text: {
        const QFontMetricsF metrics(textFont(a.size));
        const QRectF text = metrics.boundingRect(QRectF(0, 0, 1e5, 1e5), Qt::AlignLeft | Qt::AlignTop,
                                                 a.text.isEmpty() ? QStringLiteral("Text") : a.text);
        painted = QRectF(map(a.points.first()), text.size());
        break;
    }
    case Annotation::Type::Counter: {
        const qreal r = counterRadius(a.size);
        painted = QRectF(map(a.points.first()) - QPointF(r, r), QSizeF(2 * r, 2 * r));
        break;
    }
    case Annotation::Type::Measure: {
        if (a.points.size() < 2)
            return {};
        painted = QRectF(map(a.points[0]), map(a.points[1])).normalized();
        const qreal tick = 4 + qMax<qreal>(1, qRound(a.size / 2)) * 2;
        painted.adjust(-tick, -tick, tick, tick);
        painted |= labelRect(a);
        break;
    }
    case Annotation::Type::Image:
        return a.rect();
    case Annotation::Type::Arrow: {
        painted = map(a.geometryBounds());
        const qreal head = 6 + a.size * 2.5;
        painted.adjust(-head, -head, head, head);
        break;
    }
    default: {
        painted = map(a.geometryBounds());
        const qreal half = a.size / 2 + 1;
        painted.adjust(-half, -half, half, half);
        break;
    }
    }
    return QRectF(unmap(painted.topLeft()), painted.size() / m_scale);
}

void AnnotationPainter::paintSelection(const Annotation &a)
{
    const QRectF frame = map(bounds(a)).adjusted(-3, -3, 3, 3);
    m_painter.save();
    m_painter.setRenderHint(QPainter::Antialiasing, false);
    m_painter.setBrush(Qt::NoBrush);
    m_painter.setPen(QPen(Qt::white, 1));
    m_painter.drawRect(frame);
    QPen dashed(Qt::black, 1, Qt::DashLine);
    m_painter.setPen(dashed);
    m_painter.drawRect(frame);
    m_painter.restore();
}
