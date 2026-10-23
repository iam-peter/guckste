// The document: drawing, undo, export at a scale, cropping, file names.

#include "core/AnnotationPainter.h"
#include "core/Document.h"
#include "core/FileNaming.h"
#include "core/LayerModel.h"

#include <QGuiApplication>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVideoFrame>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

QImage checkerboard(int w, int h)
{
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            image.setPixelColor(x, y, (x + y) % 2 ? Qt::white : Qt::black);
    return image;
}

} // namespace

class TestDocument : public QObject
{
    Q_OBJECT

private slots:
    void drawsAndUndoes()
    {
        Document doc;
        doc.setImage(checkerboard(40, 30));
        QSignalSpy changed(&doc, &Document::changed);

        doc.beginAnnotation(Document::Rectangle, 2, 3, Qt::red, 2);
        QVERIFY(doc.drawing());
        doc.updateAnnotation(20, 15, false);
        doc.commitAnnotation();
        QVERIFY(!doc.drawing());
        QCOMPARE(doc.annotationCount(), 1);
        QVERIFY(doc.canUndo());
        QVERIFY(!doc.canRedo());
        QVERIFY(doc.modified());
        QVERIFY(changed.count() >= 3);

        // Shift keeps it square.
        doc.beginAnnotation(Document::Rectangle, 0, 0, Qt::red, 2);
        doc.updateAnnotation(10, 4, true);
        QCOMPARE(doc.current()->rect().size(), QSizeF(10, 10));
        doc.cancelAnnotation();
        QCOMPARE(doc.annotationCount(), 1);

        doc.undo();
        QCOMPARE(doc.annotationCount(), 0);
        QVERIFY(doc.canRedo());
        doc.redo();
        QCOMPARE(doc.annotationCount(), 1);

        doc.removeAnnotation(0);
        QCOMPARE(doc.annotationCount(), 0);
        doc.undo();
        QCOMPARE(doc.annotationCount(), 1);
    }

    void emptyMarksAreDropped()
    {
        Document doc;
        doc.setImage(checkerboard(10, 10));
        doc.beginAnnotation(Document::Line, 5, 5, Qt::red, 2);
        doc.commitAnnotation();
        QCOMPARE(doc.annotationCount(), 0);
        QVERIFY(!doc.canUndo());
        doc.addText(1, 1, u"   "_s, Qt::red, 2);
        QCOMPARE(doc.annotationCount(), 0);
        doc.addText(1, 1, u"hi"_s, Qt::red, 2);
        QCOMPARE(doc.annotationCount(), 1);
        QCOMPARE(doc.annotationTool(0), int(Document::Text));
    }

    void countersNumberUp()
    {
        Document doc;
        doc.setImage(checkerboard(10, 10));
        for (int i = 0; i < 3; ++i) {
            doc.beginAnnotation(Document::Counter, i, i, Qt::blue, 3);
            doc.commitAnnotation();
        }
        QCOMPARE(doc.annotations().last().number, 3);
        QCOMPARE(doc.nextNumber(), 4);
    }

    void measureSnapsAndCounts()
    {
        Document doc;
        doc.setImage(checkerboard(100, 100));
        doc.beginAnnotation(Document::Measure, 10.4, 20.6, Qt::red, 2);
        // Held to one axis: the dominant one.
        doc.updateAnnotation(52.3, 24.2, true);
        QCOMPARE(doc.current()->points[0], QPointF(10, 21));
        QCOMPARE(doc.current()->points[1], QPointF(52, 21));
        QCOMPARE(AnnotationPainter::measureLabel(*doc.current()), u"42 px"_s);
        // Alt: any angle, rounded length marked as such.
        doc.updateAnnotation(13, 25, false);
        QCOMPARE(AnnotationPainter::measureLabel(*doc.current()), u"≈5 px"_s);
        doc.commitAnnotation();
        QCOMPARE(doc.annotationCount(), 1);
    }

    void rendersAtScale()
    {
        Document doc;
        doc.setImage(checkerboard(8, 6));
        const QImage at1 = doc.render(1);
        QCOMPARE(at1.size(), QSize(8, 6));
        QCOMPARE(at1.pixelColor(0, 0), QColor(Qt::black));
        QCOMPARE(at1.pixelColor(1, 0), QColor(Qt::white));

        // Enlarged pixel for pixel: a 4x block per source pixel.
        const QImage at4 = doc.render(4);
        QCOMPARE(at4.size(), QSize(32, 24));
        QCOMPARE(at4.pixelColor(0, 0), QColor(Qt::black));
        QCOMPARE(at4.pixelColor(3, 3), QColor(Qt::black));
        QCOMPARE(at4.pixelColor(4, 0), QColor(Qt::white));
        QCOMPARE(at4.pixelColor(7, 3), QColor(Qt::white));

        // A region.
        const QImage region = doc.render(2, QRect(1, 0, 3, 2));
        QCOMPARE(region.size(), QSize(6, 4));
        QCOMPARE(region.pixelColor(0, 0), QColor(Qt::white));
    }

    // Marks are part of the image: enlarged, a stroke and a text grow
    // with it. A Measure mark keeps its line width, to stay readable.
    void annotationsScaleWithTheImage()
    {
        Document doc;
        QImage white(60, 60, QImage::Format_ARGB32_Premultiplied);
        white.fill(Qt::white);
        doc.setImage(white);
        doc.beginAnnotation(Document::Rectangle, 10, 10, Qt::red, 2);
        doc.updateAnnotation(30, 30, false);
        doc.commitAnnotation();

        const QImage at1 = doc.render(1);
        QCOMPARE(at1.pixelColor(10, 20), QColor(Qt::red));
        QCOMPARE(at1.pixelColor(12, 20), QColor(Qt::white));

        // At 4x the 2 px stroke is 8 px wide, centered on the edge at 40.
        const QImage at4 = doc.render(4);
        QCOMPARE(at4.pixelColor(37, 80), QColor(Qt::red));
        QCOMPARE(at4.pixelColor(43, 80), QColor(Qt::red));
        QCOMPARE(at4.pixelColor(46, 80), QColor(Qt::white));

        const auto textHeight = [](const QImage &image, int fromY) {
            int top = -1, bottom = -1;
            for (int y = fromY; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (image.pixelColor(x, y).blue() < 100) {
                        if (top < 0)
                            top = y;
                        bottom = y;
                    }
            return bottom - top + 1;
        };
        doc.clearAnnotations();
        doc.addText(5, 35, u"H"_s, Qt::black, 12);
        const int small = textHeight(doc.render(1), 0);
        const int large = textHeight(doc.render(4), 0);
        QVERIFY2(std::abs(large - 4 * small) <= 4, qPrintable(u"%1 then %2"_s.arg(small).arg(large)));

        doc.clearAnnotations();
        doc.beginAnnotation(Document::Measure, 10, 50, Qt::blue, 2);
        doc.updateAnnotation(50, 50, true);
        doc.commitAnnotation();
        const auto lineWidth = [](const QImage &image, int x) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                if (image.pixelColor(x, y) == QColor(Qt::blue))
                    ++count;
            return count;
        };
        QCOMPARE(lineWidth(doc.render(1), 30), lineWidth(doc.render(4), 120));
    }

    void pixelateBlursPixels()
    {
        Document doc;
        doc.setImage(checkerboard(40, 40));
        doc.beginAnnotation(Document::Pixelate, 0, 0, Qt::red, 3);
        doc.updateAnnotation(40, 40, false);
        doc.commitAnnotation();
        const QImage out = doc.render(1);
        // Black and white averaged to grey.
        const QColor c = out.pixelColor(5, 5);
        QVERIFY(c.red() > 60 && c.red() < 200);
        QCOMPARE(c.red(), c.blue());
    }

    void hitTestsAndMoves()
    {
        Document doc;
        doc.setImage(checkerboard(100, 100));
        doc.beginAnnotation(Document::Ellipse, 10, 10, Qt::red, 2);
        doc.updateAnnotation(30, 30, false);
        doc.commitAnnotation();
        QCOMPARE(doc.hitTest(20, 20, 1), 0);
        QCOMPARE(doc.hitTest(60, 60, 1), -1);
        doc.beginMove(0);
        doc.moveBy(40, 40);
        doc.endMove();
        QCOMPARE(doc.hitTest(60, 60, 1), 0);
        QCOMPARE(doc.hitTest(20, 20, 1), -1);
        doc.undo();
        QCOMPARE(doc.hitTest(20, 20, 1), 0);
    }

    // Another screenshot goes right of the first, and the canvas takes
    // in both; the gap is transparent.
    void combinesImages()
    {
        Document doc;
        QImage white(40, 30, QImage::Format_ARGB32_Premultiplied);
        white.fill(Qt::white);
        doc.setImage(white);
        QCOMPARE(doc.canvas(), QRect(0, 0, 40, 30));
        QSignalSpy canvasChanged(&doc, &Document::canvasChanged);

        QImage blue(20, 50, QImage::Format_ARGB32_Premultiplied);
        blue.fill(Qt::blue);
        QCOMPARE(doc.addImage(blue), 0);
        QCOMPARE(doc.selectedIndex(), 0);
        QCOMPARE(doc.canvas(), QRect(0, 0, 60, 50));
        QCOMPARE(canvasChanged.count(), 1);
        QCOMPARE(doc.annotationTool(0), int(Document::Image));

        const QImage out = doc.render(1);
        QCOMPARE(out.size(), QSize(60, 50));
        QCOMPARE(out.pixelColor(5, 5), QColor(Qt::white));
        QCOMPARE(out.pixelColor(45, 45), QColor(Qt::blue));
        QCOMPARE(out.pixelColor(5, 40).alpha(), 0);
        QCOMPARE(doc.pixelAt(45, 45), QColor(Qt::blue));

        // Moved up and left: the canvas follows once the move ends, and
        // the image lands on whole pixels.
        doc.beginMove(0);
        doc.moveBy(-30.4, -10.6);
        QCOMPARE(doc.canvas(), QRect(0, 0, 60, 50));
        doc.endMove();
        QCOMPARE(doc.canvas(), QRect(0, -11, 40, 50));
        QCOMPARE(doc.annotations()[0].rect(), QRectF(10, -11, 20, 50));
        QCOMPARE(doc.render(2).size(), QSize(80, 100));

        // Pixelating over both mixes the image's pixels in.
        doc.beginAnnotation(Document::Pixelate, 0, 0, Qt::red, 10);
        doc.updateAnnotation(40, 30, false);
        doc.commitAnnotation();
        const QColor mixed = doc.render(1).pixelColor(15, 11);
        QVERIFY(mixed.blue() > mixed.red());

        doc.undo();
        doc.undo();
        QCOMPARE(doc.canvas(), QRect(0, 0, 60, 50));
        doc.undo();
        QCOMPARE(doc.annotationCount(), 0);
        QCOMPARE(doc.canvas(), QRect(0, 0, 40, 30));
    }

    void reshapesByHandles()
    {
        Document doc;
        doc.setImage(checkerboard(100, 100));

        // A line's end.
        doc.beginAnnotation(Document::Arrow, 10, 10, Qt::red, 2);
        doc.updateAnnotation(50, 10, false);
        doc.commitAnnotation();
        QCOMPARE(doc.handlePoints(0).size(), 2);
        doc.beginHandleDrag(0);
        doc.dragHandle(1, 60, 52, Qt::ShiftModifier);
        doc.endHandleDrag();
        // Snapped to 45°.
        const QPointF end = doc.annotations()[0].points[1];
        QVERIFY(std::abs((end.x() - 10) - (end.y() - 10)) < 0.01);
        QCOMPARE(doc.annotations()[0].points[0], QPointF(10, 10));

        // A rectangle by its bottom-right corner, drawn from the other end.
        doc.beginAnnotation(Document::Rectangle, 40, 40, Qt::red, 2);
        doc.updateAnnotation(20, 20, false);
        doc.commitAnnotation();
        QCOMPARE(doc.handlePoints(1).size(), 8);
        QCOMPARE(doc.handlePoints(1)[4].toPointF(), QPointF(40, 40));
        doc.beginHandleDrag(1);
        doc.dragHandle(4, 70, 50, 0);
        doc.endHandleDrag();
        QCOMPARE(doc.annotations()[1].rect(), QRectF(20, 20, 50, 30));
        // The top edge, dragged below the bottom, flips over.
        doc.beginHandleDrag(1);
        doc.dragHandle(1, 45, 60, 0);
        doc.endHandleDrag();
        QCOMPARE(doc.annotations()[1].rect(), QRectF(20, 50, 50, 10));
        // Squashed flat: nothing changes, and no undo step is left.
        const int undoBefore = doc.canUndo();
        doc.beginHandleDrag(1);
        doc.dragHandle(5, 45, 50, 0);
        doc.endHandleDrag();
        QCOMPARE(doc.annotations()[1].rect(), QRectF(20, 50, 50, 10));
        QCOMPARE(doc.canUndo(), bool(undoBefore));

        // An image keeps its shape from a corner, unless Shift is held.
        QImage image(40, 20, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::green);
        const int added = doc.addImage(image, 0, 0);
        doc.beginHandleDrag(added);
        doc.dragHandle(4, 80, 25, 0);
        doc.endHandleDrag();
        QCOMPARE(doc.annotations()[added].rect(), QRectF(0, 0, 80, 40));
        doc.beginHandleDrag(added);
        doc.dragHandle(4, 30, 30, Qt::ShiftModifier);
        doc.endHandleDrag();
        QCOMPARE(doc.annotations()[added].rect(), QRectF(0, 0, 30, 30));
    }

    void restylesAndReorders()
    {
        Document doc;
        doc.setImage(checkerboard(50, 50));
        doc.beginAnnotation(Document::Line, 0, 0, Qt::red, 2);
        doc.updateAnnotation(20, 0, false);
        doc.commitAnnotation();
        doc.beginAnnotation(Document::Ellipse, 0, 0, Qt::blue, 2);
        doc.updateAnnotation(20, 20, false);
        doc.commitAnnotation();

        // A slider dragged through sizes is one undo step.
        doc.setSelectedIndex(0);
        for (int size = 3; size <= 9; ++size)
            doc.setAnnotationSize(0, size);
        doc.setAnnotationColor(0, Qt::green);
        QCOMPARE(doc.annotationSize(0), 9.0);
        QCOMPARE(doc.annotationColor(0), QColor(Qt::green));
        doc.undo();
        QCOMPARE(doc.annotationSize(0), 2.0);
        QCOMPARE(doc.annotationColor(0), QColor(Qt::red));

        LayerModel layers;
        layers.setDocument(&doc);
        QCOMPARE(layers.rowCount(), 3);
        const auto row = [&](int r, int role) { return layers.data(layers.index(r), role); };
        QCOMPARE(row(0, LayerModel::KindRole).toInt(), int(Document::Ellipse));
        QCOMPARE(row(1, LayerModel::KindRole).toInt(), int(Document::Line));
        QVERIFY(row(2, LayerModel::IsBaseRole).toBool());
        QCOMPARE(row(2, LayerModel::IndexRole).toInt(), -1);

        QSignalSpy reset(&layers, &QAbstractItemModel::modelReset);
        QSignalSpy dataChanged(&layers, &QAbstractItemModel::dataChanged);
        doc.setAnnotationColor(0, Qt::yellow);
        QCOMPARE(reset.count(), 0);
        QVERIFY(dataChanged.count() >= 1);

        doc.moveLayer(0, 1);
        QCOMPARE(reset.count(), 1);
        QCOMPARE(doc.selectedIndex(), 1);
        QCOMPARE(row(0, LayerModel::KindRole).toInt(), int(Document::Line));
        QCOMPARE(doc.annotationTool(0), int(Document::Ellipse));
    }

    // Guides sit on whole pixels, move along with a crop, and are drawn
    // into what is exported only when asked to.
    void guidesExportOnRequest()
    {
        Document doc;
        QImage white(40, 30, QImage::Format_ARGB32_Premultiplied);
        white.fill(Qt::white);
        doc.setImage(white);
        QSignalSpy guidesChanged(&doc, &Document::guidesChanged);
        QCOMPARE(doc.addGuide(true, 10.4), 0);
        QCOMPARE(doc.addGuide(false, 20.6), 1);
        QCOMPARE(guidesChanged.count(), 2);
        QCOMPARE(doc.guides()[0].toMap()[u"position"_s].toInt(), 10);
        QCOMPARE(doc.guides()[1].toMap()[u"position"_s].toInt(), 21);

        QCOMPARE(doc.render(1).pixelColor(10, 5), QColor(Qt::white));
        doc.setExportGuides(true);
        const QImage at1 = doc.render(1);
        QCOMPARE(at1.pixelColor(10, 5), QColor(0x2d, 0x8c, 0xff));
        QCOMPARE(at1.pixelColor(5, 21), QColor(0x2d, 0x8c, 0xff));
        QCOMPARE(at1.pixelColor(11, 5), QColor(Qt::white));
        // Still 1 px wide when enlarged.
        const QImage at4 = doc.render(4);
        QCOMPARE(at4.pixelColor(40, 5), QColor(0x2d, 0x8c, 0xff));
        QCOMPARE(at4.pixelColor(41, 5), QColor(Qt::white));

        doc.moveGuide(0, 15);
        doc.crop(QRectF(5, 5, 30, 20));
        QCOMPARE(doc.guides()[0].toMap()[u"position"_s].toInt(), 10);
        QCOMPARE(doc.guides()[1].toMap()[u"position"_s].toInt(), 16);
        doc.removeGuide(0);
        QCOMPARE(doc.guides().size(), 1);
        doc.clearGuides();
        QVERIFY(doc.guides().isEmpty());
    }

    // On a recording, a mark shows from its start until its end: left out
    // of painting and hit tests otherwise, and new ones start at the time.
    void marksShowAtTheirTime()
    {
        Document doc;
        QImage white(40, 40, QImage::Format_ARGB32_Premultiplied);
        white.fill(Qt::white);
        doc.setImage(white);
        QSignalSpy timeChanged(&doc, &Document::timeChanged);
        doc.setTime(2000);
        QCOMPARE(timeChanged.count(), 1);
        doc.beginAnnotation(Document::Rectangle, 5, 5, Qt::red, 2);
        doc.updateAnnotation(30, 30, false);
        doc.commitAnnotation();
        QCOMPARE(doc.annotationStart(0), 2000);
        QCOMPARE(doc.annotationEnd(0), -1);
        QVERIFY(doc.isShown(0));
        QCOMPARE(doc.hitTest(5, 15, 1), 0);

        doc.setTime(1000);
        QVERIFY(!doc.isShown(0));
        QCOMPARE(doc.hitTest(5, 15, 1), -1);
        QVERIFY(doc.handlePoints(0).isEmpty());
        QCOMPARE(doc.render(1).pixelColor(5, 15), QColor(Qt::white));

        doc.setAnnotationTiming(0, 500, 1500);
        QVERIFY(doc.isShown(0));
        QCOMPARE(doc.render(1).pixelColor(5, 15), QColor(Qt::red));
        doc.setTime(1500);
        QVERIFY(!doc.isShown(0));
        // An end before the start is refused.
        doc.setAnnotationTiming(0, 800, 700);
        QCOMPARE(doc.annotationStart(0), 500);

        // A drag of the timing is one undo step.
        doc.beginRetime(0);
        doc.retime(600, 1600);
        doc.retime(700, 1700);
        doc.endRetime();
        QCOMPARE(doc.annotationStart(0), 700);
        doc.undo();
        QCOMPARE(doc.annotationStart(0), 500);
        QCOMPARE(doc.annotationEnd(0), 1500);

        // A screenshot shows all of them.
        doc.setTime(-1);
        QVERIFY(doc.isShown(0));
    }

    static VideoInfo video(const QString &path, QSize size, int ms, const QColor &color)
    {
        VideoInfo info;
        info.path = path;
        info.size = size;
        info.durationMs = ms;
        info.poster = QImage(size, QImage::Format_ARGB32_Premultiplied);
        info.poster.fill(color);
        return info;
    }

    // Over a clip, pixelated marks coarsen the frame shown, not the one
    // it was added with.
    void pixelatesTheFrameShown()
    {
        Document doc;
        QCOMPARE(doc.addVideo(video(u"/v/a.mp4"_s, QSize(40, 40), 1000, Qt::red), Document::Placement::Base), 0);
        doc.setTime(0);
        doc.beginAnnotation(Document::Pixelate, 0, 0, Qt::black, 4);
        doc.updateAnnotation(40, 40, false);
        doc.commitAnnotation();
        QCOMPARE(doc.render(1).pixelColor(20, 20), QColor(Qt::red));

        QSignalSpy frames(&doc, &Document::framesChanged);
        QImage blue(40, 40, QImage::Format_RGB32);
        blue.fill(Qt::blue);
        doc.setClipFrame(doc.annotations().first().clip, QVideoFrame(blue));
        QCOMPARE(frames.count(), 1);
        const QColor shown = doc.render(1).pixelColor(20, 20);
        QVERIFY2(shown.blue() > 200 && shown.red() < 50, qPrintable(shown.name()));
        // Not shown then: no repaint asked for.
        doc.setTime(2000);
        doc.setClipFrame(doc.annotations().first().clip, QVideoFrame(blue.copy()));
        QCOMPARE(frames.count(), 1);
    }

    // The screenshot an editor opens with is a layer, locked until it is
    // unlocked; then it moves and goes like any other.
    // A mark just drawn is selected, so the toolbar restyles it at once.
    void selectsWhatWasDrawn()
    {
        Document doc;
        doc.setImage(checkerboard(60, 40));
        doc.beginAnnotation(Document::Arrow, 5, 5, Qt::red, 3);
        QCOMPARE(doc.selectedIndex(), -1);
        doc.updateAnnotation(40, 30, false);
        doc.commitAnnotation();
        QCOMPARE(doc.selectedIndex(), 0);
        // Nothing worth keeping: nothing selected.
        doc.beginAnnotation(Document::Line, 5, 5, Qt::red, 3);
        doc.commitAnnotation();
        QCOMPARE(doc.annotationCount(), 1);
        QCOMPARE(doc.selectedIndex(), -1);
    }

    void liftsAndUnlocksTheBase()
    {
        Document doc;
        doc.setImage(checkerboard(60, 40));
        doc.liftBase(u"Screenshot"_s);
        QCOMPARE(doc.annotationCount(), 1);
        QCOMPARE(doc.imageWidth(), 0);
        QCOMPARE(doc.canvas(), QRect(0, 0, 60, 40));
        QVERIFY(!doc.canUndo());
        QVERIFY(!doc.modified());
        QCOMPARE(doc.render(1).pixelColor(5, 5), checkerboard(60, 40).pixelColor(5, 5));

        // Locked: clicks go through, it stays and stays put.
        QVERIFY(doc.annotationLocked(0));
        QCOMPARE(doc.hitTest(30, 20, 1), -1);
        QVERIFY(doc.handlePoints(0).isEmpty());
        doc.removeAnnotation(0);
        QCOMPARE(doc.annotationCount(), 1);

        doc.setLocked(0, false);
        QCOMPARE(doc.hitTest(30, 20, 1), 0);
        QCOMPARE(doc.handlePoints(0).size(), 8);
        doc.beginMove(0);
        doc.moveBy(10, 0);
        doc.endMove();
        QCOMPARE(doc.annotations().first().rect(), QRectF(10, 0, 60, 40));
        QCOMPARE(doc.canvas(), QRect(10, 0, 60, 40));
        doc.undo();
        doc.undo();
        QVERIFY(doc.annotationLocked(0));
        doc.setLocked(0, false);
        doc.removeAnnotation(0);
        QCOMPARE(doc.annotationCount(), 0);
        QVERIFY(doc.canvas().isEmpty());
    }

    // Clips one after the other in time, or side by side from the start;
    // a clip's start trims into its video, its end no further than the
    // video goes.
    void placesAndTrimsClips()
    {
        Document doc;
        QVERIFY(!doc.hasVideo());
        QCOMPARE(doc.duration(), 0);
        doc.addVideo(video(u"/v/a.mp4"_s, QSize(320, 240), 3000, Qt::red), Document::Placement::Base);
        QVERIFY(!doc.canUndo());
        QVERIFY(doc.annotationLocked(0));
        QCOMPARE(doc.duration(), 3000);
        QCOMPARE(doc.name(), u"a.mp4"_s);

        const int after = doc.addVideo(video(u"/v/b.mp4"_s, QSize(320, 240), 2000, Qt::green), Document::Placement::After);
        QCOMPARE(after, 1);
        QCOMPARE(doc.annotationStart(1), 3000);
        QCOMPARE(doc.annotationEnd(1), 5000);
        QCOMPARE(doc.annotations()[1].rect(), QRectF(0, 0, 320, 240));
        QCOMPARE(doc.duration(), 5000);
        QCOMPARE(doc.canvas(), QRect(0, 0, 320, 240));

        const int beside = doc.addVideo(video(u"/v/c.mp4"_s, QSize(160, 120), 1000, Qt::blue), Document::Placement::Beside);
        QCOMPARE(doc.annotationStart(beside), 0);
        QCOMPARE(doc.annotations()[beside].rect(), QRectF(320, 0, 160, 120));
        QCOMPARE(doc.canvas(), QRect(0, 0, 480, 240));

        // Its start later: the clip starts 500 ms into its video.
        doc.setAnnotationTiming(1, 3500, 5000);
        QCOMPARE(doc.annotations()[1].clipIn, 500);
        // Its start earlier than the video allows: held at the video's start.
        doc.setAnnotationTiming(1, 2000, 5000);
        QCOMPARE(doc.annotationStart(1), 3000);
        QCOMPARE(doc.annotations()[1].clipIn, 0);
        // Its end past the video's: held at the video's end.
        doc.setAnnotationTiming(1, 3000, 9000);
        QCOMPARE(doc.annotationEnd(1), 5000);
        doc.setAnnotationTiming(1, 3000, 4000);
        QCOMPARE(doc.annotationEnd(1), 4000);
        // Moved: where in its video it starts stays.
        doc.setAnnotationTiming(1, 3500, 4500);
        QCOMPARE(doc.annotations()[1].clipIn, 0);
        QCOMPARE(doc.duration(), 4500);
        // Dragged, as one undo step from where the drag began.
        doc.beginRetime(1);
        doc.retime(3700, 4500);
        doc.retime(3800, 4500);
        doc.endRetime();
        QCOMPARE(doc.annotations()[1].clipIn, 300);
        doc.undo();
        QCOMPARE(doc.annotations()[1].clipIn, 0);

        // A mark set to end after the clips makes the video longer; one
        // to the end does not.
        const int before = doc.duration();
        doc.addText(1, 1, u"x"_s, Qt::white, 10);
        QCOMPARE(doc.duration(), before);
        doc.setAnnotationTiming(doc.annotationCount() - 1, 0, before + 2000);
        QCOMPARE(doc.duration(), before + 2000);
    }

    // A frame taken out: what shows then, clips as images of their frame,
    // shown all the time.
    void takesAFrame()
    {
        Document doc;
        doc.addVideo(video(u"/v/a.mp4"_s, QSize(40, 40), 2000, Qt::red), Document::Placement::Base);
        doc.setTime(0);
        doc.addText(5, 5, u"early"_s, Qt::white, 10);
        doc.setTime(1500);
        doc.addText(5, 5, u"late"_s, Qt::white, 10);
        doc.setAnnotationTiming(1, 0, 1000);
        QImage blue(40, 40, QImage::Format_RGB32);
        blue.fill(Qt::blue);
        doc.setClipFrame(doc.annotations().first().clip, QVideoFrame(blue));

        std::unique_ptr<Document> still(doc.frameDocument());
        QCOMPARE(still->annotationCount(), 2);
        QCOMPARE(still->annotationTool(0), int(Document::Image));
        QCOMPARE(LayerModel::label(still->annotations().first()), u"a.mp4 at 0:01.5"_s);
        QCOMPARE(still->annotationText(1), u"late"_s);
        QCOMPARE(still->annotationStart(1), 0);
        QCOMPARE(still->annotationEnd(1), -1);
        QVERIFY(!still->hasVideo());
        QCOMPARE(still->render(1).pixelColor(30, 30), QColor(Qt::blue));
    }

    // A recording's lossless stills, where its clip shows them: marks on
    // the time that follow the clip, and in place of the decoded frame,
    // the pointer painted in, or a layer of its own in a frame taken.
    void findsStillsOnClips()
    {
        QTemporaryDir dir;
        QImage green(40, 40, QImage::Format_RGB32);
        green.fill(Qt::green);
        QVERIFY(green.save(dir.filePath(u"01-before-click.png"_s)));
        QImage pointer(4, 4, QImage::Format_ARGB32_Premultiplied);
        pointer.fill(Qt::white);
        Still before;
        before.path = dir.filePath(u"01-before-click.png"_s);
        before.ms = 500;
        before.kind = Still::Kind::BeforeClick;
        before.pointer = pointer;
        before.pointerRect = QRectF(10, 10, 4, 4);
        Still kept;
        kept.path = dir.filePath(u"02-kept.png"_s);
        kept.ms = 1500;

        Document doc;
        doc.addVideo(video(u"/v/rec.mp4"_s, QSize(40, 40), 2000, Qt::red), Document::Placement::Base);
        doc.addStills(u"/v/rec.mp4"_s, {kept, before});
        const QVariantList marks = doc.stillMarks();
        QCOMPARE(marks.size(), 2);
        QCOMPARE(marks[0].toMap()[u"time"_s].toInt(), 500);
        QCOMPARE(marks[0].toMap()[u"kind"_s].toString(), u"before"_s);
        QCOMPARE(marks[1].toMap()[u"time"_s].toInt(), 1500);
        QCOMPARE(marks[1].toMap()[u"kind"_s].toString(), u"kept"_s);

        doc.setTime(501);
        QVERIFY(!doc.stillShown());
        QCOMPARE(doc.clipImage(doc.annotations().first()).pixelColor(30, 30), QColor(Qt::red));
        doc.setTime(500);
        QVERIFY(doc.stillShown());
        const QImage shown = doc.clipImage(doc.annotations().first());
        QCOMPARE(shown.pixelColor(30, 30), QColor(Qt::green));
        QCOMPARE(shown.pixelColor(11, 11), QColor(Qt::white));

        std::unique_ptr<Document> frame(doc.frameDocument());
        QCOMPARE(frame->annotationCount(), 2);
        QCOMPARE(frame->annotations()[0].image.pixelColor(11, 11), QColor(Qt::green));
        QCOMPARE(LayerModel::label(frame->annotations()[1]), u"Pointer"_s);
        QCOMPARE(frame->annotations()[1].rect(), QRectF(10, 10, 4, 4));
        QCOMPARE(frame->render(1).pixelColor(11, 11), QColor(Qt::white));

        // After another clip, trimmed and moved: the stills go along.
        Document two;
        two.addVideo(video(u"/v/a.mp4"_s, QSize(40, 40), 1000, Qt::red), Document::Placement::Base);
        two.addVideo(video(u"/v/rec.mp4"_s, QSize(40, 40), 2000, Qt::red), Document::Placement::After);
        two.addStills(u"/v/rec.mp4"_s, {before, kept});
        QCOMPARE(two.stillMarks()[0].toMap()[u"time"_s].toInt(), 1500);
        two.setAnnotationTiming(1, 2000, 3000);
        QCOMPARE(two.stillMarks().size(), 1);
        QCOMPARE(two.stillMarks()[0].toMap()[u"time"_s].toInt(), 2500);
        two.setAnnotationTiming(1, 2500, 3500);
        QCOMPARE(two.stillMarks()[0].toMap()[u"time"_s].toInt(), 3000);
        two.relinkSource(u"/v/rec.mp4"_s, u"/home/rec.mp4"_s);
        QCOMPARE(two.stillMarks().size(), 1);

        // Saved into a folder, as they are, and as one picture in rows,
        // each with its pointer: the one of the kept still is missing.
        QTemporaryDir saved;
        QCOMPARE(doc.stillCount(0, 1000), 1);
        QCOMPARE(doc.saveStills(saved.path(), 0, -1), 1);
        QVERIFY(QFileInfo::exists(saved.filePath(u"01-before-click.png"_s)));
        QCOMPARE(doc.saveStills(saved.path(), 0, -1), 1);
        QVERIFY(!QFileInfo::exists(saved.filePath(u"01-before-click-2.png"_s)));
        // Another recording's still of the name stays as it is.
        QTemporaryDir other;
        QImage blue(4, 4, QImage::Format_RGB32);
        blue.fill(Qt::blue);
        QVERIFY(blue.save(other.filePath(u"01-before-click.png"_s)));
        QCOMPARE(doc.saveStills(other.path(), 0, -1), 1);
        QCOMPARE(QImage(other.filePath(u"01-before-click.png"_s)).size(), QSize(4, 4));
        QCOMPARE(QImage(other.filePath(u"01-before-click-2.png"_s)).size(), QSize(40, 40));
        QVERIFY(green.save(dir.filePath(u"02-kept.png"_s)));
        std::unique_ptr<Document> sheet(doc.stillsDocument(0, -1));
        QCOMPARE(sheet->annotationCount(), 3);
        QCOMPARE(LayerModel::label(sheet->annotations()[0]), u"1 · before a click at 0:00.5"_s);
        QCOMPARE(LayerModel::label(sheet->annotations()[1]), u"Pointer"_s);
        QCOMPARE(sheet->annotations()[2].rect(), QRectF(40 + 48, 0, 40, 40));
        QCOMPARE(sheet->canvas(), QRect(0, 0, 128, 40));

        // Added later, in their order of time.
        Still change = kept;
        change.ms = 1200;
        change.kind = Still::Kind::Change;
        doc.addStills(u"/v/rec.mp4"_s, {change});
        QCOMPARE(doc.stillMarks().size(), 3);
        QCOMPARE(doc.stillMarks()[1].toMap()[u"kind"_s].toString(), u"change"_s);
    }

    // A recording saved by moving it: its clips play it from there.
    void relinksMovedVideos()
    {
        Document doc;
        doc.addVideo(video(u"/tmp/rec.mp4"_s, QSize(40, 40), 1000, Qt::red), Document::Placement::Base);
        doc.addText(1, 1, u"x"_s, Qt::white, 10);
        doc.relinkSource(u"/tmp/rec.mp4"_s, u"/home/rec.mp4"_s);
        QCOMPARE(doc.annotations().first().source, u"/home/rec.mp4"_s);
        QCOMPARE(doc.name(), u"rec.mp4"_s);
        doc.undo();
        QCOMPARE(doc.annotations().first().source, u"/home/rec.mp4"_s);
    }

    void cropsWithAnnotations()
    {
        Document doc;
        doc.setImage(checkerboard(100, 80));
        doc.beginAnnotation(Document::Line, 30, 30, Qt::red, 2);
        doc.updateAnnotation(50, 30, false);
        doc.commitAnnotation();
        QSignalSpy imageChanged(&doc, &Document::imageChanged);
        doc.crop(QRectF(20, 20, 50, 40));
        QCOMPARE(doc.imageWidth(), 50);
        QCOMPARE(doc.imageHeight(), 40);
        QCOMPARE(imageChanged.count(), 1);
        QCOMPARE(doc.annotations().first().points[0], QPointF(10, 10));
        QVERIFY(!doc.canUndo());
    }

    void savesAndLoads()
    {
        QTemporaryDir dir;
        Document doc;
        doc.setImage(checkerboard(12, 9));
        const QString path = dir.path() + u"/sub/shot.png"_s;
        QVERIFY(doc.save(path, 3));
        Document loaded;
        QVERIFY(loaded.load(path));
        QCOMPARE(loaded.imageWidth(), 36);
        QCOMPARE(loaded.imageHeight(), 27);
        QCOMPARE(loaded.filePath(), path);
        QVERIFY(!loaded.load(dir.path() + u"/missing.png"_s));
    }

    void fileNames()
    {
        const QDateTime time(QDate(2026, 10, 21), QTime(16, 5, 9));
        QCOMPARE(FileNaming::expand(u"guckste_%Y-%m-%d_%H-%M-%S"_s, time), u"guckste_2026-10-21_16-05-09"_s);
        QCOMPARE(FileNaming::expand(u"%y%b%a 100%%"_s, time), u"26OctWed 100%"_s);
        QCOMPARE(FileNaming::expand(u"a/b"_s, time), u"a-b"_s);
        QCOMPARE(FileNaming::expand(u""_s, time), u"screenshot"_s);

        QTemporaryDir dir;
        const QString first = FileNaming::uniquePath(dir.path(), u"shot"_s, u"png"_s, time);
        QCOMPARE(first, dir.path() + u"/shot.png"_s);
        QFile file(first);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QCOMPARE(FileNaming::uniquePath(dir.path(), u"shot"_s, u"png"_s, time), dir.path() + u"/shot-2.png"_s);
        // A recording's folder, numbered the same way.
        QCOMPARE(FileNaming::uniqueFolder(dir.path(), u"rec"_s), dir.path() + u"/rec"_s);
        QVERIFY(QDir(dir.path()).mkdir(u"rec"_s));
        QCOMPARE(FileNaming::uniqueFolder(dir.path(), u"rec"_s), dir.path() + u"/rec-2"_s);
    }
};

QTEST_MAIN(TestDocument)
#include "tst_document.moc"
