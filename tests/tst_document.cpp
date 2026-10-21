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
    }
};

QTEST_MAIN(TestDocument)
#include "tst_document.moc"
