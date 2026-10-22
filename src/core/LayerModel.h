#pragma once

#include "Document.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

// A document's layers for a list, topmost first: every mark, image and
// clip, then the screenshot at the bottom while it is not a layer itself.
// Rows only reset when layers come, go or change places, so a list keeps
// its state while a mark is dragged or restyled.
class LayerModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Document *document READ document WRITE setDocument NOTIFY documentChanged)
    // The screenshot's row at the bottom; off for a recording's lanes.
    Q_PROPERTY(bool includeBase READ includeBase WRITE setIncludeBase NOTIFY includeBaseChanged)

public:
    enum Role {
        // The annotation's index in the document, -1 for the screenshot.
        IndexRole = Qt::UserRole + 1,
        // Document::Tool, Document::Select for the screenshot.
        KindRole,
        LabelRole,
        ColorRole,
        IsBaseRole,
        // When it shows on a recording, in ms; end -1 is to the end.
        StartRole,
        EndRole,
        LockedRole,
    };

    explicit LayerModel(QObject *parent = nullptr);

    Document *document() const { return m_document; }
    void setDocument(Document *document);
    bool includeBase() const { return m_includeBase; }
    void setIncludeBase(bool include);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // What a layer is called in the list.
    static QString label(const Annotation &annotation);
    static QColor layerColor(const Annotation &annotation);

signals:
    void documentChanged();
    void includeBaseChanged();

private:
    void refresh();
    void remember();
    bool hasBase() const;
    // The kinds of the layers bottom to top: what decides a reset.
    QList<int> kinds() const;

    QPointer<Document> m_document;
    QList<int> m_kinds;
    QList<QString> m_labels;
    QList<QColor> m_colors;
    QList<std::pair<int, int>> m_times;
    QList<bool> m_locked;
    bool m_includeBase = true;
};
