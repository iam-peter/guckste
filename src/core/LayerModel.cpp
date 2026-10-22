#include "LayerModel.h"

#include <QCoreApplication>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("LayerModel", text);
}

} // namespace

LayerModel::LayerModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void LayerModel::setIncludeBase(bool include)
{
    if (m_includeBase == include)
        return;
    beginResetModel();
    m_includeBase = include;
    if (m_document)
        remember();
    endResetModel();
    emit includeBaseChanged();
}

void LayerModel::setDocument(Document *document)
{
    if (m_document == document)
        return;
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    beginResetModel();
    m_document = document;
    m_kinds.clear();
    if (m_document) {
        remember();
        connect(m_document, &Document::changed, this, &LayerModel::refresh);
        connect(m_document, &Document::imageChanged, this, &LayerModel::refresh);
    }
    endResetModel();
    emit documentChanged();
}

void LayerModel::remember()
{
    m_kinds = kinds();
    m_labels.clear();
    m_colors.clear();
    m_times.clear();
    m_locked.clear();
    for (const Annotation &a : m_document->annotations()) {
        m_labels << label(a);
        m_colors << layerColor(a);
        m_times << std::pair(a.startMs, a.endMs);
        m_locked << a.locked;
    }
}

bool LayerModel::hasBase() const
{
    return m_includeBase && m_document && m_document->imageWidth() > 0;
}

QList<int> LayerModel::kinds() const
{
    QList<int> list;
    if (!m_document)
        return list;
    for (const Annotation &a : m_document->annotations())
        list << int(a.type);
    // The screenshot coming or going as a row of its own.
    list << (hasBase() ? 1 : 0);
    return list;
}

void LayerModel::refresh()
{
    if (!m_document)
        return;
    const QList<Annotation> &annotations = m_document->annotations();
    const QList<int> now = kinds();
    if (now != m_kinds) {
        beginResetModel();
        remember();
        endResetModel();
        return;
    }
    const int count = annotations.size();
    for (int i = 0; i < count; ++i) {
        const Annotation &a = annotations[i];
        const QString text = label(a);
        const std::pair times(a.startMs, a.endMs);
        if (text == m_labels[i] && layerColor(a) == m_colors[i] && times == m_times[i] && a.locked == m_locked[i])
            continue;
        m_labels[i] = text;
        m_colors[i] = layerColor(a);
        m_times[i] = times;
        m_locked[i] = a.locked;
        const QModelIndex row = index(count - 1 - i);
        emit dataChanged(row, row, {LabelRole, ColorRole, StartRole, EndRole, LockedRole});
    }
    // The screenshot's size, after a crop or a new image.
    if (hasBase()) {
        const QModelIndex base = index(count);
        emit dataChanged(base, base, {LabelRole});
    }
}

int LayerModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_document)
        return 0;
    return m_labels.size() + (hasBase() ? 1 : 0);
}

QVariant LayerModel::data(const QModelIndex &index, int role) const
{
    if (!m_document || !index.isValid() || index.row() >= rowCount())
        return {};
    const int count = m_labels.size();
    const bool base = index.row() == count;
    const int annotation = base ? -1 : count - 1 - index.row();
    switch (role) {
    case IndexRole:
        return annotation;
    case IsBaseRole:
        return base;
    case KindRole:
        return base ? int(Document::Select) : m_document->annotationTool(annotation);
    case LabelRole:
        if (base)
            return tr("Screenshot %1 × %2").arg(m_document->imageWidth()).arg(m_document->imageHeight());
        return m_labels.value(annotation);
    case ColorRole:
        return base ? QColor() : m_colors.value(annotation);
    case StartRole:
        return base ? 0 : m_times.value(annotation).first;
    case EndRole:
        return base ? -1 : m_times.value(annotation, std::pair(0, -1)).second;
    case LockedRole:
        return base || m_locked.value(annotation);
    case Qt::DisplayRole:
        return data(index, LabelRole);
    }
    return {};
}

QHash<int, QByteArray> LayerModel::roleNames() const
{
    return {{IndexRole, "layerIndex"}, {KindRole, "kind"}, {LabelRole, "label"},
            {ColorRole, "layerColor"}, {IsBaseRole, "isBase"},
            {StartRole, "startMs"}, {EndRole, "endMs"}, {LockedRole, "locked"}};
}

QString LayerModel::label(const Annotation &a)
{
    switch (a.type) {
    case Annotation::Type::Pen: return tr("Pen");
    case Annotation::Type::Line: return tr("Line");
    case Annotation::Type::Arrow: return tr("Arrow");
    case Annotation::Type::Rectangle: return tr("Rectangle");
    case Annotation::Type::Ellipse: return tr("Ellipse");
    case Annotation::Type::Marker: return tr("Marker");
    case Annotation::Type::Pixelate: return tr("Pixelated");
    case Annotation::Type::Counter: return tr("Number %1").arg(a.number);
    case Annotation::Type::Measure:
        if (a.points.size() >= 2)
            return tr("Measure %1 px").arg(qRound(Annotation::measuredLength(a.points[0], a.points[1])));
        return tr("Measure");
    case Annotation::Type::Text: {
        QString line = a.text.section(u'\n', 0, 0).trimmed();
        if (line.size() > 28)
            line = line.left(27) + u'…';
        return tr("Text “%1”").arg(line);
    }
    case Annotation::Type::Image:
        if (!a.name.isEmpty())
            return a.name;
        return tr("Image %1 × %2").arg(qRound(a.rect().width())).arg(qRound(a.rect().height()));
    case Annotation::Type::Video:
        return a.name;
    }
    return {};
}

QColor LayerModel::layerColor(const Annotation &a)
{
    // Pictures have no color of their own; their bars on the timeline
    // get a quiet one.
    return a.isPicture() ? QColor(0x5b, 0x7b, 0xa8) : a.color;
}
