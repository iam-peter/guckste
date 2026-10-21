#include "Document.h"

#include "AnnotationPainter.h"

#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImageReader>
#include <QLineF>
#include <QMimeData>
#include <QPainter>
#include <QtMath>

namespace {

QHash<int, Document *> &registry()
{
    static QHash<int, Document *> documents;
    return documents;
}

Annotation::Type toType(int tool)
{
    switch (static_cast<Document::Tool>(tool)) {
    case Document::Pen: return Annotation::Type::Pen;
    case Document::Line: return Annotation::Type::Line;
    case Document::Arrow: return Annotation::Type::Arrow;
    case Document::Rectangle: return Annotation::Type::Rectangle;
    case Document::Ellipse: return Annotation::Type::Ellipse;
    case Document::Marker: return Annotation::Type::Marker;
    case Document::Text: return Annotation::Type::Text;
    case Document::Pixelate: return Annotation::Type::Pixelate;
    case Document::Counter: return Annotation::Type::Counter;
    case Document::Measure: return Annotation::Type::Measure;
    case Document::Select: break;
    }
    return Annotation::Type::Pen;
}

int toTool(Annotation::Type type)
{
    switch (type) {
    case Annotation::Type::Pen: return Document::Pen;
    case Annotation::Type::Line: return Document::Line;
    case Annotation::Type::Arrow: return Document::Arrow;
    case Annotation::Type::Rectangle: return Document::Rectangle;
    case Annotation::Type::Ellipse: return Document::Ellipse;
    case Annotation::Type::Marker: return Document::Marker;
    case Annotation::Type::Text: return Document::Text;
    case Annotation::Type::Pixelate: return Document::Pixelate;
    case Annotation::Type::Counter: return Document::Counter;
    case Annotation::Type::Measure: return Document::Measure;
    }
    return Document::Pen;
}

// Snaps the end of a line to the nearest of 8 directions.
QPointF constrainAngle(const QPointF &from, const QPointF &to)
{
    const QLineF line(from, to);
    const qreal angle = qRound(line.angle() / 45.0) * 45.0;
    QLineF snapped = QLineF::fromPolar(line.length(), angle);
    snapped.translate(from);
    return snapped.p2();
}

QPointF constrainAxis(const QPointF &from, const QPointF &to)
{
    if (std::abs(to.x() - from.x()) >= std::abs(to.y() - from.y()))
        return QPointF(to.x(), from.y());
    return QPointF(from.x(), to.y());
}

QPointF constrainSquare(const QPointF &from, const QPointF &to)
{
    const qreal side = qMax(std::abs(to.x() - from.x()), std::abs(to.y() - from.y()));
    return QPointF(from.x() + (to.x() < from.x() ? -side : side), from.y() + (to.y() < from.y() ? -side : side));
}

} // namespace

int Document::s_nextId = 1;

Document::Document(QObject *parent)
    : QObject(parent), m_id(s_nextId++)
{
    registry().insert(m_id, this);
}

Document::~Document()
{
    registry().remove(m_id);
}

Document *Document::byId(int id)
{
    return registry().value(id);
}

void Document::setImage(const QImage &image)
{
    m_image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    ++m_imageVersion;
    emit imageChanged();
}

QUrl Document::imageSource() const
{
    if (m_image.isNull())
        return {};
    return QUrl(QStringLiteral("image://guckste/%1/%2").arg(m_id).arg(m_imageVersion));
}

bool Document::load(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        qWarning("Could not read %s: %s", qPrintable(path), qPrintable(reader.errorString()));
        return false;
    }
    m_annotations.clear();
    m_undo.clear();
    m_redo.clear();
    m_current.reset();
    m_selected = -1;
    setImage(image);
    m_filePath = QFileInfo(path).absoluteFilePath();
    emit filePathChanged();
    setModified(false);
    emit changed();
    emit selectedIndexChanged();
    return true;
}

void Document::setSelectedIndex(int index)
{
    if (index < -1 || index >= m_annotations.size())
        index = -1;
    if (m_selected == index)
        return;
    m_selected = index;
    emit selectedIndexChanged();
    emit changed();
}

void Document::pushUndo()
{
    m_undo.append(m_annotations);
    m_redo.clear();
    setModified(true);
}

void Document::setModified(bool modified)
{
    if (m_modified == modified)
        return;
    m_modified = modified;
    emit modifiedChanged();
}

void Document::beginAnnotation(int tool, qreal x, qreal y, const QColor &color, qreal size)
{
    if (tool == Select)
        return;
    Annotation a;
    a.type = toType(tool);
    a.color = color;
    a.size = size;
    QPointF p(x, y);
    if (a.type == Annotation::Type::Measure)
        p = QPointF(qRound(x), qRound(y));
    a.points = {p};
    if (a.type == Annotation::Type::Counter)
        a.number = nextNumber();
    m_current = a;
    setSelectedIndex(-1);
    emit changed();
}

void Document::updateAnnotation(qreal x, qreal y, bool constrain)
{
    if (!m_current)
        return;
    Annotation &a = *m_current;
    QPointF p(x, y);
    const QPointF from = a.points.first();
    switch (a.type) {
    case Annotation::Type::Pen:
        a.points.append(p);
        break;
    case Annotation::Type::Counter:
    case Annotation::Type::Text:
        a.points = {p};
        break;
    case Annotation::Type::Line:
    case Annotation::Type::Arrow:
        if (constrain)
            p = constrainAngle(from, p);
        a.points = {from, p};
        break;
    case Annotation::Type::Measure:
        p = QPointF(qRound(p.x()), qRound(p.y()));
        if (constrain)
            p = constrainAxis(from, p);
        a.points = {from, p};
        break;
    default:
        if (constrain)
            p = constrainSquare(from, p);
        a.points = {from, p};
        break;
    }
    emit changed();
}

void Document::commitAnnotation()
{
    if (!m_current)
        return;
    Annotation a = *m_current;
    m_current.reset();
    if (!a.isEmpty()) {
        pushUndo();
        m_annotations.append(a);
    }
    emit changed();
}

void Document::cancelAnnotation()
{
    if (!m_current)
        return;
    m_current.reset();
    emit changed();
}

void Document::addText(qreal x, qreal y, const QString &text, const QColor &color, qreal size)
{
    if (text.trimmed().isEmpty())
        return;
    Annotation a;
    a.type = Annotation::Type::Text;
    a.points = {QPointF(x, y)};
    a.text = text;
    a.color = color;
    a.size = size;
    pushUndo();
    m_annotations.append(a);
    emit changed();
}

void Document::setAnnotationText(int index, const QString &text)
{
    if (index < 0 || index >= m_annotations.size())
        return;
    pushUndo();
    if (text.trimmed().isEmpty()) {
        m_annotations.removeAt(index);
        setSelectedIndex(-1);
    } else {
        m_annotations[index].text = text;
    }
    emit changed();
}

QString Document::annotationText(int index) const
{
    return index >= 0 && index < m_annotations.size() ? m_annotations[index].text : QString();
}

QPointF Document::annotationPosition(int index) const
{
    if (index < 0 || index >= m_annotations.size() || m_annotations[index].points.isEmpty())
        return {};
    return m_annotations[index].points.first();
}

int Document::annotationTool(int index) const
{
    return index >= 0 && index < m_annotations.size() ? toTool(m_annotations[index].type) : Select;
}

qreal Document::annotationSize(int index) const
{
    return index >= 0 && index < m_annotations.size() ? m_annotations[index].size : 3;
}

QColor Document::annotationColor(int index) const
{
    return index >= 0 && index < m_annotations.size() ? m_annotations[index].color : QColor();
}

int Document::nextNumber() const
{
    int highest = 0;
    for (const Annotation &a : m_annotations) {
        if (a.type == Annotation::Type::Counter)
            highest = qMax(highest, a.number);
    }
    return highest + 1;
}

int Document::hitTest(qreal x, qreal y, qreal scale) const
{
    QImage none;
    QPainter dummy;
    AnnotationPainter painter(dummy, none, scale);
    const QPointF p(x, y);
    for (int i = m_annotations.size() - 1; i >= 0; --i) {
        const qreal margin = 4 / scale;
        if (painter.bounds(m_annotations[i]).adjusted(-margin, -margin, margin, margin).contains(p))
            return i;
    }
    return -1;
}

QRectF Document::annotationBounds(int index, qreal scale) const
{
    if (index < 0 || index >= m_annotations.size())
        return {};
    QImage none;
    QPainter dummy;
    AnnotationPainter painter(dummy, none, scale);
    return painter.bounds(m_annotations[index]);
}

void Document::beginMove(int index)
{
    if (index < 0 || index >= m_annotations.size())
        return;
    pushUndo();
    m_moving = index;
}

void Document::moveBy(qreal dx, qreal dy)
{
    if (m_moving < 0 || m_moving >= m_annotations.size())
        return;
    m_annotations[m_moving].translate(QPointF(dx, dy));
    emit changed();
}

void Document::endMove()
{
    m_moving = -1;
}

void Document::removeAnnotation(int index)
{
    if (index < 0 || index >= m_annotations.size())
        return;
    pushUndo();
    m_annotations.removeAt(index);
    if (m_selected == index)
        setSelectedIndex(-1);
    emit changed();
}

void Document::undo()
{
    if (m_undo.isEmpty())
        return;
    m_current.reset();
    m_redo.append(m_annotations);
    m_annotations = m_undo.takeLast();
    setSelectedIndex(-1);
    setModified(true);
    emit changed();
}

void Document::redo()
{
    if (m_redo.isEmpty())
        return;
    m_current.reset();
    m_undo.append(m_annotations);
    m_annotations = m_redo.takeLast();
    setSelectedIndex(-1);
    setModified(true);
    emit changed();
}

void Document::clearAnnotations()
{
    if (m_annotations.isEmpty() && !m_current)
        return;
    m_current.reset();
    if (!m_annotations.isEmpty()) {
        pushUndo();
        m_annotations.clear();
    }
    setSelectedIndex(-1);
    emit changed();
}

QImage Document::render(qreal scale, const QRect &region) const
{
    const QRect source = (region.isEmpty() ? m_image.rect() : region) & m_image.rect();
    if (source.isEmpty() || scale <= 0)
        return {};
    const QSize size(qMax(1, qRound(source.width() * scale)), qMax(1, qRound(source.height() * scale)));
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    // Enlarging keeps every pixel a crisp block, shrinking averages them.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, scale < 1);
    painter.drawImage(QRectF(QPointF(0, 0), QSizeF(size)), m_image, QRectF(source));
    AnnotationPainter annotations(painter, m_image, scale, source.topLeft());
    for (const Annotation &a : m_annotations)
        annotations.paint(a);
    painter.end();
    return out;
}

void Document::copyToClipboard(qreal scale, const QRectF &region)
{
    const QImage image = render(scale, region.toAlignedRect());
    if (image.isNull())
        return;
    auto *mime = new QMimeData;
    mime->setImageData(image);
    // Browsers and chat apps want PNG rather than the formats Qt converts to.
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    mime->setData(QStringLiteral("image/png"), png);
    QGuiApplication::clipboard()->setMimeData(mime);
}

bool Document::save(const QString &path, qreal scale, const QRectF &region)
{
    const QImage image = render(scale, region.toAlignedRect());
    if (image.isNull())
        return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!image.save(path)) {
        qWarning("Could not save %s", qPrintable(path));
        return false;
    }
    if (region.isEmpty() && qFuzzyCompare(scale, 1.0)) {
        m_filePath = QFileInfo(path).absoluteFilePath();
        emit filePathChanged();
        setModified(false);
    }
    return true;
}

void Document::crop(const QRectF &region)
{
    const QRect rect = region.toAlignedRect() & m_image.rect();
    if (rect.isEmpty() || rect == m_image.rect())
        return;
    m_current.reset();
    m_undo.clear();
    m_redo.clear();
    for (Annotation &a : m_annotations)
        a.translate(-rect.topLeft());
    setImage(m_image.copy(rect));
    emit changed();
}

QColor Document::pixelAt(int x, int y) const
{
    if (!m_image.rect().contains(x, y))
        return {};
    return m_image.pixelColor(x, y);
}
