// The document: drawing, undo, export at a scale, cropping, file names.

#include "core/AnnotationPainter.h"
#include "core/Document.h"
#include "core/FileNaming.h"

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

    void annotationsAreDrawnInOutputPixels()
    {
        Document doc;
        QImage white(40, 40, QImage::Format_ARGB32_Premultiplied);
        white.fill(Qt::white);
        doc.setImage(white);
        doc.beginAnnotation(Document::Rectangle, 10, 10, Qt::red, 2);
        doc.updateAnnotation(30, 30, false);
        doc.commitAnnotation();

        const QImage at1 = doc.render(1);
        QCOMPARE(at1.pixelColor(10, 20), QColor(Qt::red));
        QCOMPARE(at1.pixelColor(20, 20), QColor(Qt::white));

        // At 4x the stroke is still 2 px wide, around the enlarged rectangle.
        const QImage at4 = doc.render(4);
        QCOMPARE(at4.pixelColor(40, 80), QColor(Qt::red));
        QCOMPARE(at4.pixelColor(80, 80), QColor(Qt::white));
        QCOMPARE(at4.pixelColor(44, 80), QColor(Qt::white));
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
