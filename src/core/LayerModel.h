#pragma once

#include "Document.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

// A document's layers for a list, topmost first: every mark and added
// image, then the screenshot at the bottom. Rows only reset when layers
// come, go or change places, so a list keeps its state while a mark is
// dragged or restyled.
class LayerModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Document *document READ document WRITE setDocument NOTIFY documentChanged)

public:
    enum Role {
        // The annotation's index in the document, -1 for the screenshot.
        IndexRole = Qt::UserRole + 1,
        // Document::Tool, Document::Select for the screenshot.
        KindRole,
        LabelRole,
        ColorRole,
        IsBaseRole,
    };

    explicit LayerModel(QObject *parent = nullptr);

    Document *document() const { return m_document; }
    void setDocument(Document *document);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // What a layer is called in the list.
    static QString label(const Annotation &annotation);

signals:
    void documentChanged();

private:
    void refresh();
    // The kinds of the layers bottom to top: what decides a reset.
    QList<int> kinds() const;

    QPointer<Document> m_document;
    QList<int> m_kinds;
    QList<QString> m_labels;
    QList<QColor> m_colors;
};
