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
    case Document::Image: return Annotation::Type::Image;
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
    case Annotation::Type::Image: return Document::Image;
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
    // First in line, so whoever hears `changed` sees the new revision.
    connect(this, &Document::changed, this, [this] {
        ++m_revision;
        updateCanvas();
    });
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
    updateCanvas();
}

void Document::updateCanvas()
{
    // Not while something is dragged: the view would shift under it.
    if (m_moving >= 0 || m_editing >= 0)
        return;
    QRect canvas = m_image.rect();
    for (const Annotation &a : std::as_const(m_annotations)) {
        if (a.type == Annotation::Type::Image)
            canvas |= a.rect().toAlignedRect();
    }
    if (canvas == m_canvas)
        return;
    m_canvas = canvas;
    emit canvasChanged();
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
    m_guides.clear();
    emit guidesChanged();
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
    m_restyling = -1;
    emit selectedIndexChanged();
    emit changed();
}

void Document::pushUndo()
{
    m_restyling = -1;
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
    a.startMs = qMax(0, m_time);
    a.color = color;
    a.size = size;
    QPointF p(x, y);
    if (a.type == Annotation::Type::Image)
        return;
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
    a.startMs = qMax(0, m_time);
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
        if (!m_annotations[i].shownAt(m_time))
            continue;
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
    const int moved = m_moving;
    m_moving = -1;
    snapImage(moved);
    updateCanvas();
    emit changed();
}

// An image sits on whole pixels, so it stays as sharp as the screenshot.
void Document::snapImage(int index)
{
    if (index < 0 || index >= m_annotations.size() || m_annotations[index].type != Annotation::Type::Image)
        return;
    for (QPointF &p : m_annotations[index].points)
        p = QPointF(qRound(p.x()), qRound(p.y()));
}

void Document::setAnnotationColor(int index, const QColor &color)
{
    if (index < 0 || index >= m_annotations.size() || m_annotations[index].color == color)
        return;
    if (m_restyling != index) {
        pushUndo();
        m_restyling = index;
    }
    m_annotations[index].color = color;
    emit changed();
}

void Document::setAnnotationSize(int index, qreal size)
{
    if (index < 0 || index >= m_annotations.size() || qFuzzyCompare(m_annotations[index].size, size))
        return;
    if (m_restyling != index) {
        pushUndo();
        m_restyling = index;
    }
    m_annotations[index].size = size;
    emit changed();
}

void Document::moveLayer(int from, int to)
{
    if (from < 0 || from >= m_annotations.size() || to < 0 || to >= m_annotations.size() || from == to)
        return;
    pushUndo();
    m_annotations.move(from, to);
    m_selected = to;
    emit selectedIndexChanged();
    emit changed();
}

int Document::addImage(const QImage &image)
{
    // Right of everything there is, level with its top.
    const QRect canvas = m_canvas.isEmpty() ? QRect(0, 0, 0, 0) : m_canvas;
    return addImage(image, canvas.isEmpty() ? 0 : canvas.right() + 1, canvas.top());
}

int Document::addImage(const QImage &image, qreal x, qreal y)
{
    if (image.isNull())
        return -1;
    m_current.reset();
    Annotation a;
    a.type = Annotation::Type::Image;
    a.startMs = qMax(0, m_time);
    a.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    a.points = {QPointF(qRound(x), qRound(y)), QPointF(qRound(x) + image.width(), qRound(y) + image.height())};
    pushUndo();
    m_annotations.append(a);
    m_selected = m_annotations.size() - 1;
    emit selectedIndexChanged();
    emit changed();
    return m_selected;
}

int Document::addImageFile(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        qWarning("Could not read %s: %s", qPrintable(path), qPrintable(reader.errorString()));
        return -1;
    }
    return addImage(image);
}

int Document::pasteImage()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return -1;
    QImage image;
    if (mime->hasImage())
        image = qvariant_cast<QImage>(mime->imageData());
    if (image.isNull() && mime->hasFormat(QStringLiteral("image/png")))
        image.loadFromData(mime->data(QStringLiteral("image/png")), "PNG");
    if (image.isNull() && mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile() && image.load(url.toLocalFile()))
                break;
        }
    }
    return addImage(image);
}

bool Document::clipboardHasImage() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    return mime && (mime->hasImage() || mime->hasFormat(QStringLiteral("image/png")) || mime->hasUrls());
}

QVariantList Document::handlePoints(int index) const
{
    if (index < 0 || index >= m_annotations.size() || !m_annotations[index].shownAt(m_time))
        return {};
    const Annotation &a = m_annotations[index];
    QVariantList points;
    if (a.hasEndPoints() && a.points.size() >= 2) {
        points << a.points[0] << a.points[1];
    } else if (a.hasBox() && a.points.size() >= 2) {
        const QRectF r = a.rect();
        const qreal cx = r.center().x(), cy = r.center().y();
        points << r.topLeft() << QPointF(cx, r.top()) << r.topRight() << QPointF(r.right(), cy)
               << r.bottomRight() << QPointF(cx, r.bottom()) << r.bottomLeft() << QPointF(r.left(), cy);
    }
    return points;
}

void Document::beginHandleDrag(int index)
{
    if (index < 0 || index >= m_annotations.size())
        return;
    pushUndo();
    m_editing = index;
    Annotation &a = m_annotations[index];
    // A box is worked on as its normalized rectangle.
    if (a.hasBox() && a.points.size() >= 2) {
        const QRectF r = a.rect();
        a.points = {r.topLeft(), r.bottomRight()};
    }
    m_editStart = a;
}

void Document::dragHandle(int handle, qreal x, qreal y, int modifiers)
{
    if (m_editing < 0 || m_editing >= m_annotations.size())
        return;
    const bool shift = modifiers & Qt::ShiftModifier;
    const bool alt = modifiers & Qt::AltModifier;
    Annotation &a = m_annotations[m_editing];
    QPointF p(x, y);

    if (a.hasEndPoints() && (handle == 0 || handle == 1) && a.points.size() >= 2) {
        const QPointF other = a.points[1 - handle];
        if (a.type == Annotation::Type::Measure) {
            p = QPointF(qRound(p.x()), qRound(p.y()));
            if (!alt)
                p = constrainAxis(other, p);
        } else if (shift) {
            p = constrainAngle(other, p);
        }
        a.points[handle] = p;
        emit changed();
        return;
    }
    if (!a.hasBox() || handle < 0 || handle > 7)
        return;

    const QRectF start = m_editStart.rect();
    qreal left = start.left(), top = start.top(), right = start.right(), bottom = start.bottom();
    const bool corner = handle % 2 == 0;
    if (handle == 0 || handle == 6 || handle == 7) left = p.x();
    if (handle == 2 || handle == 3 || handle == 4) right = p.x();
    if (handle == 0 || handle == 1 || handle == 2) top = p.y();
    if (handle == 4 || handle == 5 || handle == 6) bottom = p.y();

    // From a corner, an image keeps its shape unless Shift is held, and
    // the other boxes become squares with Shift.
    const bool keepShape = a.type == Annotation::Type::Image ? !shift : shift;
    if (corner && keepShape && start.height() > 0) {
        const qreal aspect = a.type == Annotation::Type::Image ? start.width() / start.height() : 1;
        const QPointF anchor(handle == 0 || handle == 6 ? start.right() : start.left(),
                             handle == 0 || handle == 2 ? start.bottom() : start.top());
        qreal w = std::abs(p.x() - anchor.x());
        qreal h = std::abs(p.y() - anchor.y());
        if (w / qMax<qreal>(h, 1e-6) > aspect)
            h = w / aspect;
        else
            w = h * aspect;
        const qreal sx = p.x() < anchor.x() ? -1 : 1;
        const qreal sy = p.y() < anchor.y() ? -1 : 1;
        const QPointF moved(anchor.x() + sx * w, anchor.y() + sy * h);
        left = qMin(anchor.x(), moved.x());
        right = qMax(anchor.x(), moved.x());
        top = qMin(anchor.y(), moved.y());
        bottom = qMax(anchor.y(), moved.y());
    }
    a.points = {QPointF(left, top), QPointF(right, bottom)};
    emit changed();
}

void Document::endHandleDrag()
{
    const int edited = m_editing;
    m_editing = -1;
    if (edited < 0 || edited >= m_annotations.size())
        return;
    Annotation &a = m_annotations[edited];
    if (a.hasBox() && a.points.size() >= 2) {
        const QRectF r = a.rect();
        a.points = {r.topLeft(), r.bottomRight()};
    }
    snapImage(edited);
    // Squashed to nothing: back to how it was, as if not dragged.
    if (a.isEmpty()) {
        a = m_editStart;
        if (!m_undo.isEmpty())
            m_undo.removeLast();
    }
    updateCanvas();
    emit changed();
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
    m_restyling = -1;
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
    m_restyling = -1;
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
    const QRect source = region.isEmpty() ? m_canvas : region & m_canvas;
    if (source.isEmpty() || scale <= 0)
        return {};
    const QSize size(qMax(1, qRound(source.width() * scale)), qMax(1, qRound(source.height() * scale)));
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    paintLayers(painter, scale, source.topLeft());
    if (m_exportGuides && !m_guides.isEmpty()) {
        // On the pixel edge, 1 px wide whatever the scale.
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(QColor(0x2d, 0x8c, 0xff), 1));
        for (const Guide &guide : std::as_const(m_guides)) {
            if (guide.vertical) {
                const int x = qMin(size.width() - 1, qRound((guide.position - source.left()) * scale));
                if (x >= 0)
                    painter.drawLine(x, 0, x, size.height());
            } else {
                const int y = qMin(size.height() - 1, qRound((guide.position - source.top()) * scale));
                if (y >= 0)
                    painter.drawLine(0, y, size.width(), y);
            }
        }
    }
    painter.end();
    return out;
}

void Document::setBackdropFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    m_backdropFrame = frame;
    m_backdropStale = true;
    for (const Annotation &a : std::as_const(m_annotations)) {
        if (a.type == Annotation::Type::Pixelate && a.shownAt(m_time)) {
            emit backdropChanged();
            return;
        }
    }
}

void Document::followVideo(QVideoSink *sink)
{
    if (m_followed == sink)
        return;
    if (m_followed)
        disconnect(m_followed, nullptr, this, nullptr);
    m_followed = sink;
    if (sink)
        connect(sink, &QVideoSink::videoFrameChanged, this, &Document::setBackdropFrame);
}

const QImage &Document::baseImage() const
{
    if (!m_backdropFrame.isValid())
        return m_image;
    if (m_backdropStale) {
        m_backdropStale = false;
        QVideoFrame frame = m_backdropFrame;
        QImage image = frame.toImage();
        // On the screenshot's pixels, which the marks are placed on.
        if (!m_image.isNull() && image.size() != m_image.size())
            image = image.scaled(m_image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        m_backdropImage = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    return m_backdropImage.isNull() ? m_image : m_backdropImage;
}

void Document::setTime(int ms)
{
    if (m_time == ms)
        return;
    m_time = ms;
    emit timeChanged();
}

bool Document::isShown(int index) const
{
    return index >= 0 && index < m_annotations.size() && m_annotations[index].shownAt(m_time);
}

int Document::annotationStart(int index) const
{
    return index >= 0 && index < m_annotations.size() ? m_annotations[index].startMs : 0;
}

int Document::annotationEnd(int index) const
{
    return index >= 0 && index < m_annotations.size() ? m_annotations[index].endMs : -1;
}

void Document::setAnnotationTiming(int index, int startMs, int endMs)
{
    if (index < 0 || index >= m_annotations.size())
        return;
    const Annotation &a = m_annotations[index];
    startMs = qMax(0, startMs);
    if (endMs >= 0 && endMs <= startMs)
        return;
    if (a.startMs == startMs && a.endMs == endMs)
        return;
    pushUndo();
    m_annotations[index].startMs = startMs;
    m_annotations[index].endMs = endMs;
    emit changed();
}

void Document::beginRetime(int index)
{
    if (index < 0 || index >= m_annotations.size())
        return;
    pushUndo();
    m_retiming = index;
}

void Document::retime(int startMs, int endMs)
{
    if (m_retiming < 0 || m_retiming >= m_annotations.size())
        return;
    startMs = qMax(0, startMs);
    if (endMs >= 0 && endMs <= startMs)
        return;
    m_annotations[m_retiming].startMs = startMs;
    m_annotations[m_retiming].endMs = endMs;
    emit changed();
}

void Document::endRetime()
{
    m_retiming = -1;
}

void Document::beginRetimeMany()
{
    pushUndo();
}

void Document::retimeMany(const QVariantList &timings)
{
    bool any = false;
    for (const QVariant &entry : timings) {
        const QVariantList t = entry.toList();
        if (t.size() != 3)
            continue;
        const int index = t[0].toInt();
        const int start = qMax(0, t[1].toInt());
        const int end = t[2].toInt();
        if (index < 0 || index >= m_annotations.size() || (end >= 0 && end <= start))
            continue;
        Annotation &a = m_annotations[index];
        if (a.startMs == start && a.endMs == end)
            continue;
        a.startMs = start;
        a.endMs = end;
        any = true;
    }
    if (any)
        emit changed();
}

void Document::setTimings(const QVariantList &timings)
{
    pushUndo();
    retimeMany(timings);
}

QVariantList Document::guides() const
{
    QVariantList list;
    for (const Guide &guide : m_guides)
        list << QVariantMap{{QStringLiteral("vertical"), guide.vertical}, {QStringLiteral("position"), guide.position}};
    return list;
}

void Document::setExportGuides(bool on)
{
    if (m_exportGuides == on)
        return;
    m_exportGuides = on;
    emit exportGuidesChanged();
}

int Document::addGuide(bool vertical, qreal position)
{
    m_guides.append({vertical, int(qRound(position))});
    setModified(true);
    emit guidesChanged();
    return m_guides.size() - 1;
}

void Document::moveGuide(int index, qreal position)
{
    if (index < 0 || index >= m_guides.size() || m_guides[index].position == qRound(position))
        return;
    m_guides[index].position = qRound(position);
    setModified(true);
    emit guidesChanged();
}

void Document::removeGuide(int index)
{
    if (index < 0 || index >= m_guides.size())
        return;
    m_guides.removeAt(index);
    setModified(true);
    emit guidesChanged();
}

void Document::clearGuides()
{
    if (m_guides.isEmpty())
        return;
    m_guides.clear();
    setModified(true);
    emit guidesChanged();
}

QRectF Document::snapBounds(int index, qreal scale) const
{
    if (index < 0 || index >= m_annotations.size())
        return {};
    const Annotation &a = m_annotations[index];
    if (a.type == Annotation::Type::Text || a.type == Annotation::Type::Counter)
        return annotationBounds(index, scale);
    return a.geometryBounds();
}

void Document::paintLayers(QPainter &painter, qreal scale, const QPointF &origin, int upTo, bool withBase, int hidden,
                           bool withCurrent) const
{
    const int count = upTo < 0 ? m_annotations.size() : qMin<int>(upTo, m_annotations.size());
    const QImage &base = withBase ? baseImage() : m_image;
    if (withBase && !base.isNull()) {
        painter.save();
        // Enlarging keeps every pixel a crisp block, shrinking averages them.
        painter.setRenderHint(QPainter::SmoothPixmapTransform, scale < 1);
        painter.drawImage(QRectF(-origin * scale, QSizeF(base.size()) * scale), base);
        painter.restore();
    }
    AnnotationPainter marks(painter, m_image, scale, origin);
    const auto paintOne = [&](const Annotation &a, int below) {
        if (a.type == Annotation::Type::Pixelate)
            marks.setBackdrop([this, below](const QRect &rect) { return backdrop(below, rect); });
        marks.paint(a);
    };
    for (int i = 0; i < count; ++i) {
        if (i != hidden && m_annotations[i].shownAt(m_time))
            paintOne(m_annotations[i], i);
    }
    if (withCurrent && m_current)
        paintOne(*m_current, m_annotations.size());
}

QImage Document::backdrop(int upTo, const QRect &rect) const
{
    if (rect.isEmpty())
        return {};
    QImage out(rect.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    paintLayers(painter, 1, rect.topLeft(), upTo);
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
    for (Guide &guide : m_guides)
        guide.position -= guide.vertical ? rect.left() : rect.top();
    if (!m_guides.isEmpty())
        emit guidesChanged();
    setImage(m_image.copy(rect));
    emit changed();
}

QColor Document::pixelAt(int x, int y) const
{
    if (!m_canvas.contains(x, y))
        return {};
    // What is seen there, added images and marks included.
    return backdrop(-1, QRect(x, y, 1, 1)).pixelColor(0, 0);
}
