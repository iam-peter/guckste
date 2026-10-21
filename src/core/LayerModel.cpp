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
        m_kinds = kinds();
        m_labels.clear();
        m_colors.clear();
        m_times.clear();
        for (const Annotation &a : m_document->annotations()) {
            m_labels << label(a);
            m_colors << a.color;
            m_times << std::pair(a.startMs, a.endMs);
        }
        connect(m_document, &Document::changed, this, &LayerModel::refresh);
        connect(m_document, &Document::imageChanged, this, &LayerModel::refresh);
    }
    endResetModel();
    emit documentChanged();
}

QList<int> LayerModel::kinds() const
{
    QList<int> list;
    if (!m_document)
        return list;
    for (const Annotation &a : m_document->annotations())
        list << int(a.type);
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
        m_kinds = now;
        m_labels.clear();
        m_colors.clear();
        m_times.clear();
        for (const Annotation &a : annotations) {
            m_labels << label(a);
            m_colors << a.color;
            m_times << std::pair(a.startMs, a.endMs);
        }
        endResetModel();
        return;
    }
    const int count = annotations.size();
    for (int i = 0; i < count; ++i) {
        const QString text = label(annotations[i]);
        const std::pair times(annotations[i].startMs, annotations[i].endMs);
        if (text == m_labels[i] && annotations[i].color == m_colors[i] && times == m_times[i])
            continue;
        m_labels[i] = text;
        m_colors[i] = annotations[i].color;
        m_times[i] = times;
        const QModelIndex row = index(count - 1 - i);
        emit dataChanged(row, row, {LabelRole, ColorRole, StartRole, EndRole});
    }
    // The screenshot's size, after a crop or a new image.
    if (m_includeBase) {
        const QModelIndex base = index(count);
        emit dataChanged(base, base, {LabelRole});
    }
}

int LayerModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_document)
        return 0;
    return m_kinds.size() + (m_includeBase ? 1 : 0);
}

QVariant LayerModel::data(const QModelIndex &index, int role) const
{
    if (!m_document || !index.isValid() || index.row() >= rowCount())
        return {};
    const int count = m_kinds.size();
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
    case Qt::DisplayRole:
        return data(index, LabelRole);
    }
    return {};
}

QHash<int, QByteArray> LayerModel::roleNames() const
{
    return {{IndexRole, "layerIndex"}, {KindRole, "kind"}, {LabelRole, "label"},
            {ColorRole, "layerColor"}, {IsBaseRole, "isBase"},
            {StartRole, "startMs"}, {EndRole, "endMs"}};
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
        return tr("Image %1 × %2").arg(qRound(a.rect().width())).arg(qRound(a.rect().height()));
    }
    return {};
}
