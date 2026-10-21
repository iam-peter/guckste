#pragma once

#include "Annotation.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QRectF>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <optional>

// A screenshot and the annotations on it, with undo. QML draws by calling
// begin/update/commitAnnotation as the pointer moves; the image is shown
// through the "guckste" image provider at `imageSource`.
class Document : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY imageChanged)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY imageChanged)
    Q_PROPERTY(QUrl imageSource READ imageSource NOTIFY imageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(int annotationCount READ annotationCount NOTIFY changed)
    Q_PROPERTY(bool drawing READ drawing NOTIFY changed)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)

public:
    // Mirrors Annotation::Type, with Select in front, for QML.
    enum Tool { Select, Pen, Line, Arrow, Rectangle, Ellipse, Marker, Text, Pixelate, Counter, Measure };
    Q_ENUM(Tool)

    explicit Document(QObject *parent = nullptr);
    ~Document() override;

    int id() const { return m_id; }
    static Document *byId(int id);

    QImage image() const { return m_image; }
    void setImage(const QImage &image);
    int imageWidth() const { return m_image.width(); }
    int imageHeight() const { return m_image.height(); }
    QUrl imageSource() const;
    Q_INVOKABLE bool load(const QString &path);
    QString filePath() const { return m_filePath; }
    bool modified() const { return m_modified; }

    const QList<Annotation> &annotations() const { return m_annotations; }
    const Annotation *current() const { return m_current ? &*m_current : nullptr; }
    int annotationCount() const { return m_annotations.size(); }
    bool drawing() const { return m_current.has_value(); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    int selectedIndex() const { return m_selected; }
    void setSelectedIndex(int index);

    // Drawing: begin at a point, update with the pointer, commit on release.
    Q_INVOKABLE void beginAnnotation(int tool, qreal x, qreal y, const QColor &color, qreal size);
    // `constrain` holds a line to 45° steps, a rectangle square, and a
    // Measure mark to one axis.
    Q_INVOKABLE void updateAnnotation(qreal x, qreal y, bool constrain);
    Q_INVOKABLE void commitAnnotation();
    Q_INVOKABLE void cancelAnnotation();
    Q_INVOKABLE void addText(qreal x, qreal y, const QString &text, const QColor &color, qreal size);
    Q_INVOKABLE void setAnnotationText(int index, const QString &text);
    Q_INVOKABLE QString annotationText(int index) const;
    Q_INVOKABLE QPointF annotationPosition(int index) const;
    Q_INVOKABLE int annotationTool(int index) const;
    Q_INVOKABLE qreal annotationSize(int index) const;
    Q_INVOKABLE QColor annotationColor(int index) const;
    Q_INVOKABLE int nextNumber() const;

    // The topmost annotation at an image point, -1 for none. `scale` says
    // how big strokes and labels are on screen, in image pixels.
    Q_INVOKABLE int hitTest(qreal x, qreal y, qreal scale) const;
    Q_INVOKABLE QRectF annotationBounds(int index, qreal scale) const;
    // Moving: one undo step from beginMove() to endMove().
    Q_INVOKABLE void beginMove(int index);
    Q_INVOKABLE void moveBy(qreal dx, qreal dy);
    Q_INVOKABLE void endMove();
    Q_INVOKABLE void removeAnnotation(int index);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearAnnotations();

    // The image with its annotations at `scale`, pixels enlarged without
    // smoothing; `region` (image pixels) crops it, empty for all of it.
    QImage render(qreal scale, const QRect &region = QRect()) const;
    Q_INVOKABLE void copyToClipboard(qreal scale, const QRectF &region = QRectF());
    Q_INVOKABLE bool save(const QString &path, qreal scale, const QRectF &region = QRectF());
    // Keeps the region only, annotations moved along.
    Q_INVOKABLE void crop(const QRectF &region);
    Q_INVOKABLE QColor pixelAt(int x, int y) const;

signals:
    void imageChanged();
    void changed();
    void selectedIndexChanged();
    void filePathChanged();
    void modifiedChanged();

private:
    void pushUndo();
    void setModified(bool modified);

    static int s_nextId;
    int m_id;
    int m_imageVersion = 0;
    QImage m_image;
    QString m_filePath;
    bool m_modified = false;
    QList<Annotation> m_annotations;
    std::optional<Annotation> m_current;
    QList<QList<Annotation>> m_undo;
    QList<QList<Annotation>> m_redo;
    int m_selected = -1;
    int m_moving = -1;
};
