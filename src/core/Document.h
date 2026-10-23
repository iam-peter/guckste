#pragma once

#include "Annotation.h"
#include "StillKeeper.h"
#include "VideoProbe.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QRectF>
#include <QUrl>
#include <QVariantList>
#include <QHash>
#include <QVideoFrame>
#include <QtQml/qqmlregistration.h>

#include <optional>

// A screenshot and the annotations on it, with undo. QML draws by calling
// begin/update/commitAnnotation as the pointer moves; the image is shown
// through the "guckste" image provider at `imageSource`.
//
// In an editor everything is a layer: the screenshot is lifted into an
// Image layer, locked until it is unlocked, further images are added
// beside it, and video clips too, side by side or one after the other in
// time. The canvas, what is exported, takes in every image and clip
// wherever it is placed. With a clip in it the document has a duration,
// and every layer a time it shows.
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
    // Guides, dragged out of the editor's rulers: lines on pixel edges to
    // line marks up with, as {vertical, position} in image pixels. Not
    // part of the undo history, as in GIMP.
    Q_PROPERTY(QVariantList guides READ guides NOTIFY guidesChanged)
    // The time the document is shown at, in ms, which leaves out the
    // layers not shown then; -1, as without video, shows all of them.
    // New marks start there.
    Q_PROPERTY(int time READ time WRITE setTime NOTIFY timeChanged)
    // How long the video is: to the end of the last clip, or of anything
    // set to end later; 0 without clips.
    Q_PROPERTY(int duration READ duration NOTIFY changed)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY changed)
    // The file name the document goes by: the image it was opened from,
    // or the first clip's video.
    Q_PROPERTY(QString name READ name NOTIFY changed)
    // Copies and saves draw the guides in, 1 px wide.
    Q_PROPERTY(bool exportGuides READ exportGuides WRITE setExportGuides NOTIFY exportGuidesChanged)

public:
    // Mirrors Annotation::Type, with Select in front, for QML.
    // Image and Video are no tools to draw with, only kinds of layers.
    enum Tool { Select, Pen, Line, Arrow, Rectangle, Ellipse, Marker, Text, Pixelate, Counter, Measure, Image, Video };
    Q_ENUM(Tool)
    // Where an added clip goes: as the one the document opens with, at
    // the end of the clips in time, or right of the canvas from the start.
    enum class Placement { Base, After, Beside };

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
    int time() const { return m_time; }
    void setTime(int ms);
    int duration() const;
    static int durationOf(const QList<Annotation> &layers);
    bool hasVideo() const;
    QString name() const;
    // The screenshot becomes the bottom layer, locked, called `name`; the
    // editor does this before anything is drawn, so it is no undo step.
    Q_INVOKABLE void liftBase(const QString &name);
    // A locked layer is left alone on the canvas: clicks go through it
    // and it cannot be moved, reshaped or removed.
    Q_INVOKABLE bool annotationLocked(int index) const;
    Q_INVOKABLE void setLocked(int index, bool locked);

    // Video clips. The frames a player shows go to their clip, converted
    // only when one is painted.
    int addVideo(const VideoInfo &video, Placement placement);
    // Reads the video first; videoAdded or videoFailed tells how it went.
    Q_INVOKABLE void addVideoFile(const QString &path, bool after);
    void setClipFrame(int clip, const QVideoFrame &frame);
    void setClipImage(int clip, const QImage &image);
    // What a clip shows now: its still when there is one at the time, its
    // latest frame, or the one it was added with.
    QImage clipImage(const Annotation &clip) const;
    // A new document with what is shown now, every clip as an image of
    // its frame and all of it shown all the time: a still to go on with.
    // A clip's still is taken as it is, the pointer a layer above it.
    Document *frameDocument() const;
    // The lossless stills of a video, from its recording.
    void addStills(const QString &source, const QList<Still> &stills);
    // The stills the clips show, in the document's time: {time, kind},
    // kind "kept", "before" or "after" a click, or "change"; ordered by
    // time.
    Q_INVOKABLE QVariantList stillMarks() const;
    // Whether a clip shows a still at the document's time.
    Q_INVOKABLE bool stillShown() const;
    // The stills from `fromMs` to `toMs` of the document's time (-1: to
    // the end): how many; copied into a folder, file names kept unless
    // another file has one; and as one new document, each an image with
    // its pointer above it, in rows, the first of them as many as fit
    // maxStillsInOne and maxPixelsInOne, how many in `taken`.
    Q_INVOKABLE int stillCount(int fromMs, int toMs) const;
    Q_INVOKABLE int saveStills(const QString &directory, int fromMs, int toMs) const;
    static constexpr int maxStillsInOne = 24;
    // Six 4K stills, two dozen of 1080p.
    static constexpr qint64 maxPixelsInOne = 50'000'000;
    Document *stillsDocument(int fromMs, int toMs, int *taken = nullptr) const;
    // The same for stills at the given times.
    static Document *sheetOf(QList<std::pair<int, Still>> stills, int *taken = nullptr);
    // Replaces every layer, as for an exporter's copy; no undo step.
    void setLayers(const QList<Annotation> &layers);
    // Written out as a video: nothing left unsaved.
    Q_INVOKABLE void markSaved();
    // A clip's video moved, as a recording saved by moving it: the clips
    // play it from where it is now, in the undo history too.
    Q_INVOKABLE void relinkSource(const QString &from, const QString &to);
    // Whether a mark is shown at the document's time.
    Q_INVOKABLE bool isShown(int index) const;
    Q_INVOKABLE int annotationStart(int index) const;
    Q_INVOKABLE int annotationEnd(int index) const;
    // When a mark shows on a recording, as one undo step; end -1 is to the
    // end. Dragging: one undo step from beginRetime() to endRetime().
    // A clip's length stays within its video: moving its start alone
    // trims where it starts in the video too.
    Q_INVOKABLE void setAnnotationTiming(int index, int startMs, int endMs);
    Q_INVOKABLE void beginRetime(int index);
    Q_INVOKABLE void retime(int startMs, int endMs);
    Q_INVOKABLE void endRetime();
    // Several at once, as when lanes are selected together: one undo step
    // from beginRetimeMany() to endRetime(); each timing is a list of
    // index, start and end.
    Q_INVOKABLE void beginRetimeMany();
    Q_INVOKABLE void retimeMany(const QVariantList &timings);
    Q_INVOKABLE void setTimings(const QVariantList &timings);
    QVariantList guides() const;
    bool exportGuides() const { return m_exportGuides; }
    void setExportGuides(bool on);
    Q_INVOKABLE int addGuide(bool vertical, qreal position);
    Q_INVOKABLE void moveGuide(int index, qreal position);
    Q_INVOKABLE void removeGuide(int index);
    Q_INVOKABLE void clearGuides();
    // What a mark lines up with guides by, in image pixels: its geometry
    // for shapes and images, what is painted for texts and numbers.
    Q_INVOKABLE QRectF snapBounds(int index, qreal scale) const;

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
    void guidesChanged();
    void timeChanged();
    // A clip shown now has a new frame.
    void framesChanged();
    void videoAdded(int index);
    void videoFailed(const QString &path);
    void exportGuidesChanged();
    void selectedIndexChanged();
    void filePathChanged();
    void modifiedChanged();

private:
    void pushUndo();
    void setModified(bool modified);
    void updateCanvas();
    void snapImage(int index);
    // Sets a mark's time; a clip's from how it was before, `before`.
    void applyTiming(int index, int startMs, int endMs);
    // The still a clip shows at the document's time, if any.
    const Still *stillOf(const Annotation &clip) const;
    // The stills the clips show from `fromMs` to `toMs`, with their time,
    // in its order.
    QList<std::pair<int, Still>> shownStills(int fromMs, int toMs) const;

    static int s_nextId;
    static int s_nextClip;
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
    struct Guide
    {
        bool vertical = false;
        int position = 0;
    };
    QList<Guide> m_guides;
    bool m_exportGuides = false;
    int m_time = -1;
    struct ClipFrame
    {
        QVideoFrame frame;
        QImage image;
        bool stale = false;
    };
    mutable QHash<int, ClipFrame> m_clipFrames;
    // By the video they are of.
    QHash<QString, QList<Still>> m_stills;
    // The still last shown, the pointer in it, read once.
    mutable QString m_shownStillPath;
    mutable QImage m_shownStill;
    int m_retiming = -1;
};
