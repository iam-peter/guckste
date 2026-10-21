// Loads the editor and the capture overlay offscreen and fails on any QML
// warning. Set GUCKSTE_SCREENSHOT_DIR to also save screenshots of them.

#include "core/Document.h"
#include "core/Preferences.h"
#include "core/VideoExporter.h"
#include "ui/App.h"
#include "ui/DocumentImageProvider.h"

#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>

Q_IMPORT_QML_PLUGIN(Guckste_CorePlugin)
Q_IMPORT_QML_PLUGIN(Guckste_UiPlugin)
Q_IMPORT_QML_PLUGIN(GuckstePlugin)

using namespace Qt::StringLiterals;

namespace {

// Something like a desktop: a gradient with a few boxes.
QImage demoScreen(int w, int h)
{
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&image);
    QLinearGradient gradient(0, 0, w, h);
    gradient.setColorAt(0, QColor(40, 60, 90));
    gradient.setColorAt(1, QColor(120, 70, 110));
    p.fillRect(image.rect(), gradient);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 6; ++i) {
        p.setBrush(QColor(255, 255, 255, 40 + i * 25));
        p.drawRoundedRect(40 + i * 110, 60 + (i % 3) * 90, 90, 60, 8, 8);
    }
    p.end();
    return image;
}

void saveScreenshot(QQuickWindow *window, const QString &name)
{
    const QString dir = qEnvironmentVariable("GUCKSTE_SCREENSHOT_DIR");
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    window->grabWindow().save(dir + u'/' + name + u".png"_s);
}

} // namespace

class TestUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Settings go to a throwaway location, not the user's.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(u"guckste-test"_s);
        QCoreApplication::setOrganizationDomain(u"guckste.test"_s);
        QSettings().clear();
        if (qEnvironmentVariableIsEmpty("GUCKSTE_TEST_DEFAULT_RHI"))
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QQuickStyle::setStyle(u"Fusion"_s);
        m_engine = new QQmlApplicationEngine(this);
        m_engine->addImageProvider(u"guckste"_s, new DocumentImageProvider);
        m_app = new App(m_engine, this);
        connect(m_engine, &QQmlEngine::warnings, this,
                [this](const QList<QQmlError> &w) { m_warnings += w; });
    }

    void cleanup()
    {
        for (const QQmlError &warning : std::as_const(m_warnings))
            qWarning() << warning.toString();
        QVERIFY(m_warnings.isEmpty());
    }

    void editorLoads()
    {
        auto *document = new Document;
        document->setImage(demoScreen(640, 400));
        QQmlComponent component(m_engine);
        component.loadFromModule("Guckste", "EditorWindow");
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QObject *object = component.createWithInitialProperties({{u"document"_s, QVariant::fromValue(document)}});
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(object);
        QVERIFY(window);
        document->setParent(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        // Draw a few marks at 400 % and export at that zoom.
        QVERIFY(QMetaObject::invokeMethod(window, "zoomAt", Q_ARG(double, 4.0), Q_ARG(double, 100.0),
                                          Q_ARG(double, 100.0)));
        QCOMPARE(window->property("zoom").toReal(), 4.0);
        document->beginAnnotation(Document::Measure, 40, 60, Qt::red, 3);
        document->updateAnnotation(130, 60, true);
        document->commitAnnotation();
        document->beginAnnotation(Document::Arrow, 200, 200, Qt::yellow, 3);
        document->updateAnnotation(260, 150, false);
        document->commitAnnotation();
        document->addText(50, 100, u"Guckste"_s, Qt::white, 24);
        QCOMPARE(document->annotationCount(), 3);
        QCOMPARE(window->property("exportScale").toReal(), 4.0);
        QTest::qWait(50);
        saveScreenshot(window, u"editor"_s);
        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
    }

    // Two screenshots in one editor, reshaped and listed in the layer
    // panel; a text edited where it is.
    void editorCombinesAndEdits()
    {
        auto *document = new Document;
        document->setImage(demoScreen(400, 300));
        QQmlComponent component(m_engine);
        component.loadFromModule("Guckste", "EditorWindow");
        auto *window = qobject_cast<QQuickWindow *>(
            component.createWithInitialProperties({{u"document"_s, QVariant::fromValue(document)}}));
        QVERIFY2(window, qPrintable(component.errorString()));
        document->setParent(window);
        window->resize(1200, 700);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *area = window->findChild<QQuickItem *>(u"drawingArea"_s);
        QVERIFY(area);
        const auto view = [&](qreal x, qreal y) {
            QPointF p;
            QMetaObject::invokeMethod(area, "toView", Q_RETURN_ARG(QPointF, p), Q_ARG(double, x), Q_ARG(double, y));
            return area->mapToScene(p).toPoint();
        };

        const int added = document->addImage(demoScreen(200, 100));
        QCOMPARE(added, 0);
        QCOMPARE(document->canvas(), QRect(0, 0, 600, 300));
        // At 50 % all of it is in view, beside the layer panel.
        QVERIFY(QMetaObject::invokeMethod(window, "zoomAt", Q_ARG(double, 0.5), Q_ARG(double, 0.0), Q_ARG(double, 0.0)));
        QTest::qWait(50);

        // The selected image's bottom-right handle, dragged out: it keeps
        // its shape and the canvas grows with it.
        const QPoint corner = view(600, 100);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, corner);
        QTest::mouseMove(window, corner + QPoint(50, 10));
        QTest::mouseMove(window, corner + QPoint(100, 20));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, corner + QPoint(100, 20));
        QCOMPARE(document->annotations()[0].rect(), QRectF(400, 0, 400, 200));
        QCOMPARE(document->canvas(), QRect(0, 0, 800, 300));

        // Dragging the image by its middle moves it.
        const QPoint middle = view(600, 100);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, middle);
        QTest::mouseMove(window, middle + QPoint(10, 20));
        QTest::mouseMove(window, middle + QPoint(20, 40));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, middle + QPoint(20, 40));
        QCOMPARE(document->annotations()[0].rect(), QRectF(440, 80, 400, 200));

        // A text, double-clicked and retyped in place, stays on its layer.
        document->addText(40, 40, u"Old"_s, Qt::yellow, 20);
        QCOMPARE(document->annotationCount(), 2);
        QTest::qWait(30);
        const QPoint text = view(50, 50);
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, text);
        QTRY_VERIFY(area->property("editingText").toBool());
        QCOMPARE(area->property("editingIndex").toInt(), 1);
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        for (const char c : {'N', 'e', 'w'})
            QTest::keyClick(window, c);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!area->property("editingText").toBool());
        QCOMPARE(document->annotationCount(), 2);
        QCOMPARE(document->annotationText(1), u"New"_s);
        QCOMPARE(document->annotationColor(1), QColor(Qt::yellow));

        // The layer panel lists the text, the image and the screenshot.
        auto *list = window->findChild<QQuickItem *>(u"layerList"_s);
        QVERIFY(list);
        QCOMPARE(list->property("count").toInt(), 3);

        // The export takes in both screenshots.
        window->setProperty("exportAtZoom", false);
        QCOMPARE(document->render(1).size(), QSize(840, 300));
        QTest::qWait(50);
        saveScreenshot(window, u"editor-layers"_s);
        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
    }

    // A text opened for editing shows where it was drawn, and the toolbar
    // sets its size in pixels.
    void textKeepsItsPlaceAndSize()
    {
        auto *document = new Document;
        QImage black(400, 300, QImage::Format_ARGB32_Premultiplied);
        black.fill(Qt::black);
        document->setImage(black);
        QQmlComponent component(m_engine);
        component.loadFromModule("Guckste", "EditorWindow");
        auto *window = qobject_cast<QQuickWindow *>(
            component.createWithInitialProperties({{u"document"_s, QVariant::fromValue(document)}}));
        QVERIFY2(window, qPrintable(component.errorString()));
        document->setParent(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *area = window->findChild<QQuickItem *>(u"drawingArea"_s);
        auto *toolbar = window->findChild<QQuickItem *>(u"toolbar"_s);
        QVERIFY(area && toolbar);

        document->addText(40, 40, u"Hgjy"_s, QColor(255, 255, 0), 24);
        // Zoomed in, so the field has to grow with the text.
        QVERIFY(QMetaObject::invokeMethod(window, "zoomAt", Q_ARG(double, 2.0), Q_ARG(double, 0.0), Q_ARG(double, 0.0)));
        QTest::qWait(50);
        QPointF v;
        QMetaObject::invokeMethod(area, "toView", Q_RETURN_ARG(QPointF, v), Q_ARG(double, 40), Q_ARG(double, 40));
        const QPoint at = area->mapToScene(v).toPoint();
        const QRect zone(at.x() - 20, at.y() - 20, 240, 120);
        // The H's stem: the leftmost yellow column and its top.
        const auto stem = [&](const QImage &image) {
            for (int x = zone.left(); x <= zone.right(); ++x)
                for (int y = zone.top(); y <= zone.bottom(); ++y) {
                    const QColor c = image.pixelColor(x, y);
                    if (c.red() > 200 && c.green() > 200 && c.blue() < 90)
                        return QPoint(x, y);
                }
            return QPoint();
        };
        const QImage shown = window->grabWindow();
        const QPoint drawn = stem(shown);
        QVERIFY(!drawn.isNull());
        // 24 px of text at 200 % stands about 48 px tall on screen.
        int bottom = drawn.y();
        for (int y = drawn.y(); y < zone.bottom(); ++y) {
            const QColor c = shown.pixelColor(drawn.x() + 1, y);
            if (c.red() > 200 && c.green() > 200 && c.blue() < 90)
                bottom = y;
        }
        QVERIFY2(bottom - drawn.y() > 25, qPrintable(u"H is %1 px tall"_s.arg(bottom - drawn.y() + 1)));
        QMetaObject::invokeMethod(area, "editText", Q_ARG(int, 0));
        QTRY_VERIFY(area->property("editingText").toBool());
        QTest::qWait(50);
        const QPoint editing = stem(window->grabWindow());
        QVERIFY2(std::abs(editing.x() - drawn.x()) <= 1 && std::abs(editing.y() - drawn.y()) <= 1,
                 qPrintable(u"drawn at %1,%2, edited at %3,%4"_s.arg(drawn.x()).arg(drawn.y())
                                .arg(editing.x()).arg(editing.y())));
        QVERIFY(window->property("textMode").toBool());
        QMetaObject::invokeMethod(area, "cancelText");
        QTRY_VERIFY(!area->property("editingText").toBool());

        // Selected, the text shows its size and takes a new one.
        document->setSelectedIndex(0);
        QVERIFY(window->property("textMode").toBool());
        QCOMPARE(window->property("textSize").toReal(), 24.0);
        QMetaObject::invokeMethod(toolbar, "textSizePicked", Q_ARG(double, 40.0));
        QCOMPARE(document->annotationSize(0), 40.0);
        QCOMPARE(window->property("textSize").toReal(), 40.0);

        // A rectangle selected takes the stroke size again.
        document->beginAnnotation(Document::Rectangle, 100, 100, Qt::red, 3);
        document->updateAnnotation(150, 150, false);
        document->commitAnnotation();
        document->setSelectedIndex(1);
        QVERIFY(!window->property("textMode").toBool());

        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
    }

    // A guide dragged out of the top ruler; a rectangle drawn near it
    // snaps to it; the guide dragged back onto the ruler goes.
    void guidesFromRulersSnap()
    {
        auto *document = new Document;
        document->setImage(demoScreen(400, 300));
        QQmlComponent component(m_engine);
        component.loadFromModule("Guckste", "EditorWindow");
        auto *window = qobject_cast<QQuickWindow *>(
            component.createWithInitialProperties({{u"document"_s, QVariant::fromValue(document)}}));
        QVERIFY2(window, qPrintable(component.errorString()));
        document->setParent(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->setProperty("snapToGuides", true);
        auto *area = window->findChild<QQuickItem *>(u"drawingArea"_s);
        auto *ruler = window->findChild<QQuickItem *>(u"topRuler"_s);
        QVERIFY(area && ruler);
        const auto view = [&](qreal x, qreal y) {
            QPointF p;
            QMetaObject::invokeMethod(area, "toView", Q_RETURN_ARG(QPointF, p), Q_ARG(double, x), Q_ARG(double, y));
            return area->mapToScene(p).toPoint();
        };

        // Out of the ruler, down to image row 100.
        const QPoint onRuler = ruler->mapToScene(QPointF(100, 10)).toPoint();
        const QPoint target(onRuler.x(), view(0, 100).y());
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, onRuler);
        QTest::mouseMove(window, (onRuler + target) / 2);
        QTest::mouseMove(window, target);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, target);
        QCOMPARE(document->guides().size(), 1);
        QVariantMap guide = document->guides()[0].toMap();
        QCOMPARE(guide[u"vertical"_s].toBool(), false);
        QCOMPARE(guide[u"position"_s].toInt(), 100);

        // A rectangle from (50, 40) to 4 px short of the guide ends on it.
        window->setProperty("tool", int(Document::Rectangle));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, view(50, 40));
        QTest::mouseMove(window, view(120, 70));
        QTest::mouseMove(window, view(150, 96));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, view(150, 96));
        QCOMPARE(document->annotationCount(), 1);
        QCOMPARE(document->annotations()[0].rect(), QRectF(50, 40, 100, 60));

        // Moved by its middle, its top edge catches the guide.
        window->setProperty("tool", int(Document::Select));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, view(100, 70));
        QTest::mouseMove(window, view(100, 100));
        QTest::mouseMove(window, view(100, 127));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, view(100, 127));
        QCOMPARE(document->annotations()[0].rect().top(), 100.0);

        // Back onto the ruler: gone.
        const QPoint onGuide(view(300, 100).x(), view(300, 100).y());
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, onGuide);
        QTest::mouseMove(window, onGuide - QPoint(0, 40));
        QTest::mouseMove(window, QPoint(onGuide.x(), onRuler.y()));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(onGuide.x(), onRuler.y()));
        QVERIFY(document->guides().isEmpty());

        QTest::qWait(50);
        saveScreenshot(window, u"editor-guides"_s);
        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
    }

    void overlayLoads()
    {
        auto *document = new Document;
        document->setImage(demoScreen(1280, 800));
        QQmlComponent component(m_engine);
        component.loadFromModule("Guckste", "CaptureOverlay");
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QObject *object = component.createWithInitialProperties(
            {{u"document"_s, QVariant::fromValue(document)}, {u"screenGeometry"_s, QRect(0, 0, 1280, 800)}});
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(object);
        QVERIFY(window);
        document->setParent(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(window->width(), 1280);

        // Drag out a region and draw in it.
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
        QTest::mouseMove(window, QPoint(300, 200));
        QTest::mouseMove(window, QPoint(700, 500));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(700, 500));
        QVERIFY(window->property("hasSelection").toBool());
        QCOMPARE(window->property("selection").toRect(), QRect(100, 100, 600, 400));

        window->setProperty("tool", int(Document::Rectangle));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, QPoint(150, 150));
        QTest::mouseMove(window, QPoint(200, 200));
        QTest::mouseMove(window, QPoint(400, 300));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
        QCOMPARE(document->annotationCount(), 1);
        QCOMPARE(document->annotations().first().rect(), QRectF(150, 150, 250, 150));

        // Back to selecting: dragging inside moves the region.
        window->setProperty("tool", int(Document::Select));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, QPoint(600, 450));
        QTest::mouseMove(window, QPoint(610, 460));
        QTest::mouseMove(window, QPoint(620, 470));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(620, 470));
        QCOMPARE(window->property("selection").toRect(), QRect(120, 120, 600, 400));

        // The rendered region has the rectangle's stroke where it was drawn.
        const QImage out = document->render(1, window->property("selection").toRect());
        QCOMPARE(out.size(), QSize(600, 400));
        QCOMPARE(out.pixelColor(150 - 120, 200 - 120), document->annotations().first().color);

        QTest::qWait(50);
        saveScreenshot(window, u"overlay"_s);
        window->close();
        QTRY_VERIFY(!window->isVisible());
    }

    // All screens and Previous are kept like the other modes and open with
    // their region selected; switching the mode drops it again.
    void pickModesAreKept()
    {
        Preferences *prefs = Preferences::instance();
        const auto open = [this] {
            auto *document = new Document;
            document->setImage(demoScreen(1280, 800));
            QQmlComponent component(m_engine);
            component.loadFromModule("Guckste", "CaptureOverlay");
            auto *window = qobject_cast<QQuickWindow *>(component.createWithInitialProperties(
                {{u"document"_s, QVariant::fromValue(document)}, {u"screenGeometry"_s, QRect(0, 0, 1280, 800)}}));
            if (window)
                document->setParent(window);
            return window;
        };

        prefs->setPickMode(u"all"_s);
        QCOMPARE(prefs->pickMode(), u"all"_s);
        QQuickWindow *window = open();
        QVERIFY(window);
        QCOMPARE(window->property("pickMode").toString(), u"all"_s);
        QCOMPARE(window->property("selection").toRect(), QRect(0, 0, 1280, 800));
        QMetaObject::invokeMethod(window, "setPickMode", Q_ARG(QString, u"window"_s));
        QVERIFY(!window->property("hasSelection").toBool());
        QCOMPARE(prefs->pickMode(), u"window"_s);
        delete window;

        prefs->setLastRegion(QRect(10, 20, 300, 200));
        prefs->setPickMode(u"previous"_s);
        QCOMPARE(prefs->pickMode(), u"previous"_s);
        window = open();
        QVERIFY(window);
        QCOMPARE(window->property("pickMode").toString(), u"previous"_s);
        QCOMPARE(window->property("selection").toRect(), QRect(10, 20, 300, 200));
        delete window;

        prefs->setPickMode(u"region"_s);
    }

    void recordingControlsLoad()
    {
        QQmlComponent component(m_engine);
        component.loadFromModule("Guckste", "RecordingControls");
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QObject *object = component.createWithInitialProperties(
            {{u"recorder"_s, QVariant::fromValue(m_app->recorder())}});
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(object);
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(QMetaObject::invokeMethod(window, "time", Q_RETURN_ARG(QString, m_time), Q_ARG(int, 65000)));
        QCOMPARE(m_time, u"01:05"_s);
        window->close();
        delete window;
    }

    void videoEditorLoads()
    {
        auto *document = new Document;
        document->setImage(demoScreen(320, 200));
        auto *exporter = new VideoExporter;
        QQmlComponent component(m_engine);
        component.loadFromModule("Guckste", "VideoEditorWindow");
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QObject *object = component.createWithInitialProperties(
            {{u"document"_s, QVariant::fromValue(document)},
             {u"sourcePath"_s, QString()},
             {u"exporter"_s, QVariant::fromValue(exporter)}});
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(object);
        QVERIFY(window);
        document->setParent(window);
        exporter->setParent(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(window->property("videoWidth").toInt(), 320);
        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
    }

private:
    QString m_time;
    QQmlApplicationEngine *m_engine = nullptr;
    App *m_app = nullptr;
    QList<QQmlError> m_warnings;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    TestUi test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_ui.moc"
