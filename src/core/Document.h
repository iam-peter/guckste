#pragma once

#include "Annotation.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QRectF>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <optional>

// A screenshot and the annotations on it, with undo. QML draws by calling
// begin/update/commitAnnotation as the pointer moves; the image is shown
// through the "guckste" image provider at `imageSource`.
//
// Further images can be added as Image annotations, to combine screenshots:
// the screenshot stays at the origin as the bottom layer, and the canvas,
// what is exported, grows to take in every image wherever it is placed.
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
    // The canvas in image pixels: the screenshot and every added image.
    Q_PROPERTY(int canvasX READ canvasX NOTIFY canvasChanged)
    Q_PROPERTY(int canvasY READ canvasY NOTIFY canvasChanged)
    Q_PROPERTY(int canvasWidth READ canvasWidth NOTIFY canvasChanged)
    Q_PROPERTY(int canvasHeight READ canvasHeight NOTIFY canvasChanged)
    // Goes up with every change, for bindings that ask for derived data.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    // Mirrors Annotation::Type, with Select in front, for QML.
    // Image is no tool to draw with, only the kind of an added image.
    enum Tool { Select, Pen, Line, Arrow, Rectangle, Ellipse, Marker, Text, Pixelate, Counter, Measure, Image };
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
    QRect canvas() const { return m_canvas; }
    int canvasX() const { return m_canvas.x(); }
    int canvasY() const { return m_canvas.y(); }
    int canvasWidth() const { return m_canvas.width(); }
    int canvasHeight() const { return m_canvas.height(); }
    int revision() const { return m_revision; }

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
    // Restyling a mark: a run of changes to one mark, like dragging the
    // size slider, is one undo step.
    Q_INVOKABLE void setAnnotationColor(int index, const QColor &color);
    Q_INVOKABLE void setAnnotationSize(int index, qreal size);
    // Layer order: index 0 is drawn first, right above the screenshot.
    Q_INVOKABLE void moveLayer(int from, int to);

    // Adding images, placed right of the canvas; returns their index.
    int addImage(const QImage &image);
    Q_INVOKABLE int addImage(const QImage &image, qreal x, qreal y);
    Q_INVOKABLE int addImageFile(const QString &path);
    Q_INVOKABLE int pasteImage();
    Q_INVOKABLE bool clipboardHasImage() const;

    // The points a selected mark is reshaped by, in image pixels: a line's
    // two ends, or a box's corners and edge middles clockwise from its
    // top-left. Empty for marks that only move.
    Q_INVOKABLE QVariantList handlePoints(int index) const;
    // Reshaping: one undo step from beginHandleDrag() to endHandleDrag().
    // Shift squares a box or snaps a line to 45°; an image keeps its
    // aspect ratio unless Shift is held; a Measure mark stays on its axis
    // unless Alt is held.
    Q_INVOKABLE void beginHandleDrag(int index);
    Q_INVOKABLE void dragHandle(int handle, qreal x, qreal y, int modifiers);
    Q_INVOKABLE void endHandleDrag();

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
    // smoothing; `region` (image pixels) crops it, empty for the canvas.
    QImage render(qreal scale, const QRect &region = QRect()) const;
    // Paints the layers below `upTo` (all of them for -1) mapped by
    // (p - origin) * scale, without the screenshot unless `withBase`, and
    // leaving out `hidden`, like a text being edited. The mark being drawn
    // goes on top when `withCurrent`.
    void paintLayers(QPainter &painter, qreal scale, const QPointF &origin, int upTo = -1, bool withBase = true,
                     int hidden = -1, bool withCurrent = false) const;
    // Everything below layer `upTo` within `rect`, at 1:1.
    QImage backdrop(int upTo, const QRect &rect) const;
    Q_INVOKABLE void copyToClipboard(qreal scale, const QRectF &region = QRectF());
    Q_INVOKABLE bool save(const QString &path, qreal scale, const QRectF &region = QRectF());
    // Keeps the region only, annotations moved along.
    Q_INVOKABLE void crop(const QRectF &region);
    Q_INVOKABLE QColor pixelAt(int x, int y) const;

signals:
    void imageChanged();
    void changed();
    void canvasChanged();
    void selectedIndexChanged();
    void filePathChanged();
    void modifiedChanged();

private:
    void pushUndo();
    void setModified(bool modified);
    void updateCanvas();
    void snapImage(int index);

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
    int m_editing = -1;
    Annotation m_editStart;
    // The mark a run of restyling changes goes to, -1 for none.
    int m_restyling = -1;
    QRect m_canvas;
    int m_revision = 0;
};
