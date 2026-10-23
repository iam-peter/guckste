// Loads the editor and the capture overlay offscreen and fails on any QML
// warning. Set GUCKSTE_SCREENSHOT_DIR to also save screenshots of them, and
// GUCKSTE_SITE_SHOTS to stage the screenshots of the README and the site.

#include "core/Document.h"
#include "core/Encoding.h"
#include "core/VideoFrames.h"
#include "core/Preferences.h"
#include "core/VideoExporter.h"
#include "core/VideoProbe.h"
#include "ui/App.h"
#include "ui/DocumentImageProvider.h"

#include <QApplication>
#include <QDir>
#include <QMediaCaptureSession>
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QTemporaryDir>
#include <QVideoFrameInput>
#include <QPainter>
#include <QPainterPath>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QWheelEvent>

#include <functional>
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


// A window as a desktop shows it: a title bar above the content.
QImage framed(const QImage &content, const QString &title)
{
    const int bar = 30;
    QImage image(content.width(), content.height() + bar, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(image.rect()), 8, 8);
    p.setClipPath(shape);
    p.fillRect(QRect(0, 0, image.width(), bar), QColor(0x2b, 0x2d, 0x33));
    p.drawImage(0, bar, content);
    p.setPen(QColor(0xe6, 0xe6, 0xe6));
    QFont font;
    font.setPixelSize(13);
    font.setBold(true);
    p.setFont(font);
    p.drawText(QRect(0, 0, image.width(), bar), Qt::AlignCenter, title);
    const QColor dots[] = {QColor(0xff, 0x5f, 0x57), QColor(0xfe, 0xbc, 0x2e), QColor(0x28, 0xc8, 0x40)};
    for (int i = 0; i < 3; ++i) {
        p.setPen(Qt::NoPen);
        p.setBrush(dots[i]);
        p.drawEllipse(QPointF(image.width() - 20 - i * 20, bar / 2.0), 6, 6);
    }
    return image;
}

// A terminal with a few commands in it.
QImage terminal(int w, int h)
{
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(0x16, 0x18, 0x1d));
    QPainter p(&image);
    QFont font(u"monospace"_s);
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(14);
    p.setFont(font);
    const struct { QString text; QColor color; } lines[] = {
        {u"$ guckste gui"_s, QColor(0x8a, 0xe2, 0x34)},
        {u"Guckste is already running; taking a screenshot"_s, QColor(0xd0, 0xd0, 0xd0)},
        {u"$ guckste record"_s, QColor(0x8a, 0xe2, 0x34)},
        {u"$ ls ~/Pictures/Screenshots"_s, QColor(0x8a, 0xe2, 0x34)},
        {u"guckste_2026-10-21_19-28-13.png"_s, QColor(0x72, 0x9f, 0xcf)},
        {u"guckste_2026-10-21_19-41-55.mp4"_s, QColor(0xad, 0x7f, 0xa8)},
        {u"guckste_2026-10-21_20-03-07.png"_s, QColor(0x72, 0x9f, 0xcf)},
        {u"$ "_s, QColor(0x8a, 0xe2, 0x34)},
    };
    int y = 26;
    for (const auto &line : lines) {
        p.setPen(line.color);
        p.drawText(16, y, line.text);
        y += 22;
    }
    p.fillRect(QRect(36, y - 37, 9, 17), QColor(0xd0, 0xd0, 0xd0));
    return image;
}

// A desktop: a wallpaper, a panel, and windows with shadows.
QImage desktop(QSize size, const QList<std::pair<QImage, QPoint>> &windows)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, size.width(), size.height());
    sky.setColorAt(0, QColor(0x1d, 0x2b, 0x53));
    sky.setColorAt(0.55, QColor(0x5a, 0x2b, 0x6e));
    sky.setColorAt(1, QColor(0xc4, 0x4b, 0x52));
    p.fillRect(image.rect(), sky);
    for (const auto &[center, radius, color] : {std::tuple{QPointF(size.width() * 0.8, size.height() * 0.25), 380.0, QColor(255, 140, 80, 90)},
                                                std::tuple{QPointF(size.width() * 0.15, size.height() * 0.85), 420.0, QColor(80, 200, 255, 70)}}) {
        QRadialGradient glow(center, radius);
        glow.setColorAt(0, color);
        glow.setColorAt(1, Qt::transparent);
        p.fillRect(image.rect(), glow);
    }
    p.fillRect(QRect(0, 0, size.width(), 26), QColor(0x12, 0x13, 0x17, 230));
    p.setPen(QColor(0xe0, 0xe0, 0xe0));
    QFont font;
    font.setPixelSize(13);
    p.setFont(font);
    p.drawText(QRect(12, 0, 200, 26), Qt::AlignVCenter, u"Applications"_s);
    p.drawText(QRect(0, 0, size.width() - 12, 26), Qt::AlignVCenter | Qt::AlignRight, u"Tue 21 Oct   21:42"_s);
    const QImage icon = QImage(u":/qt/qml/Guckste/logo/guckste.svg"_s);
    if (!icon.isNull())
        p.drawImage(QRect(size.width() - 160, 4, 18, 18), icon);
    for (const auto &[window, at] : windows) {
        for (int i = 6; i >= 1; --i) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 14));
            p.drawRoundedRect(QRectF(at + QPoint(0, 6), window.size()).adjusted(-i * 2, -i * 2, i * 2, i * 2), 10 + i, 10 + i);
        }
        p.drawImage(at, window);
    }
    return image;
}

// A short video of one color, written from frames.
QString writeVideoOf(const QString &path, const QList<QImage> &images);

QString writeVideo(const QString &path, int frames, const QColor &color)
{
    QImage image(320, 240, QImage::Format_RGB32);
    image.fill(color);
    return writeVideoOf(path, QList<QImage>(frames, image));
}

// A video of the images, one a frame at 30 fps, as big as the first.
QString writeVideoOf(const QString &path, const QList<QImage> &images)
{
    QMediaCaptureSession session;
    QVideoFrameInput input;
    QMediaRecorder recorder;
    session.setVideoFrameInput(&input);
    session.setRecorder(&recorder);
    QMediaFormat format = Encoding::bestFormat();
    format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
    recorder.setMediaFormat(format);
    recorder.setVideoFrameRate(30);
    recorder.setVideoResolution(images.value(0).size());
    recorder.setOutputLocation(QUrl::fromLocalFile(path));
    recorder.record();
    for (int i = 0; i < images.size(); ++i) {
        QVideoFrame frame = VideoFrames::toNV12(images[i]);
        frame.setStartTime(i * 33333);
        frame.setEndTime((i + 1) * 33333);
        if (!QTest::qWaitFor([&input, &frame] { return input.sendVideoFrame(frame); }, 5000))
            return {};
    }
    QTest::qWait(300);
    recorder.stop();
    if (!QTest::qWaitFor([&recorder] { return recorder.recorderState() == QMediaRecorder::StoppedState; }, 10000))
        return {};
    return recorder.actualLocation().toLocalFile();
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
        Encoding::quietFFmpeg();
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

        // Closing asks first, in a dialog window of its own.
        window->close();
        QWindow *question = nullptr;
        QTRY_VERIFY((question = [] {
            for (QWindow *w : QGuiApplication::topLevelWindows())
                if (w->isVisible() && w->title() == u"Discard changes?"_s)
                    return w;
            return static_cast<QWindow *>(nullptr);
        }()));
        QVERIFY(question->flags() & Qt::Dialog);
        QCOMPARE(question->transientParent(), window);
        QVERIFY(window->isVisible());
        saveScreenshot(qobject_cast<QQuickWindow *>(question), u"discard"_s);
        // Cancelled: it is modal, and would block every window after it.
        auto *dialog = window->findChild<QObject *>(u"discardDialog"_s);
        QVERIFY(dialog);
        QMetaObject::invokeMethod(dialog, "reject");
        QTRY_VERIFY(!question->isVisible());
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
        // Within an image pixel, 2 px at 200 %: on Windows the field shows
        // the text an image pixel up and left of where it is drawn.
        QVERIFY2(std::abs(editing.x() - drawn.x()) <= 2 && std::abs(editing.y() - drawn.y()) <= 2,
                 qPrintable(u"drawn at %1,%2, edited at %3,%4"_s.arg(drawn.x()).arg(drawn.y())
                                .arg(editing.x()).arg(editing.y())));
        QVERIFY(window->property("textMode").toBool());
        QMetaObject::invokeMethod(area, "cancelText");
        QTRY_VERIFY(!area->property("editingText").toBool());

        // Selected, the text shows its size and takes a new one.
        document->setSelectedIndex(0);
        QVERIFY(window->property("textMode").toBool());
        QCOMPARE(window->property("textSize").toReal(), 24.0);
        QMetaObject::invokeMethod(toolbar, "pickTextSize", Q_ARG(double, 40.0));
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

    // At 100 % every image pixel lands on one screen pixel, whatever the
    // window's size: half-pixel offsets used to double some and drop others.
    void editorShowsPixelsExactly()
    {
        auto *document = new Document;
        QImage checker(120, 80, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < checker.height(); ++y)
            for (int x = 0; x < checker.width(); ++x)
                checker.setPixelColor(x, y, (x + y) % 2 ? Qt::white : Qt::black);
        document->setImage(checker);
        QQmlComponent component(m_engine);
        component.loadFromModule("Guckste", "EditorWindow");
        auto *window = qobject_cast<QQuickWindow *>(
            component.createWithInitialProperties({{u"document"_s, QVariant::fromValue(document)}}));
        QVERIFY2(window, qPrintable(component.errorString()));
        document->setParent(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *area = window->findChild<QQuickItem *>(u"drawingArea"_s);
        for (int width : {901, 902, 903, 1000, 1001}) {
            window->resize(width, 601 + width % 2);
            QTest::qWait(60);
            QPointF p;
            QMetaObject::invokeMethod(area, "toView", Q_RETURN_ARG(QPointF, p), Q_ARG(double, 0.0), Q_ARG(double, 0.0));
            const QPointF at = area->mapToScene(p);
            QCOMPARE(at, QPointF(at.toPoint()));
            const QImage shown = window->grabWindow().copy(QRect(at.toPoint(), checker.size()));
            QVERIFY2(shown.convertToFormat(QImage::Format_RGB32) == checker.convertToFormat(QImage::Format_RGB32),
                     qPrintable(u"not pixel for pixel at width %1"_s.arg(width)));
        }
        window->close();
    }

    // A narrow editor moves what does not fit on its toolbar behind a
    // more button; widened again, all of it comes back.
    void editorToolbarOverflows()
    {
        auto *document = new Document;
        document->setImage(demoScreen(300, 200));
        QQmlComponent component(m_engine);
        component.loadFromModule("Guckste", "EditorWindow");
        auto *window = qobject_cast<QQuickWindow *>(
            component.createWithInitialProperties({{u"document"_s, QVariant::fromValue(document)}}));
        QVERIFY2(window, qPrintable(component.errorString()));
        document->setParent(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *toolbar = window->findChild<QQuickItem *>(u"toolbar"_s);
        auto *more = window->findChild<QQuickItem *>(u"moreButton"_s);
        QVERIFY(toolbar && more);
        const qreal full = toolbar->property("fullWidth").toReal();
        QVERIFY(full > 800);

        window->resize(int(full) + 60, 600);
        QTRY_VERIFY(!more->isVisible());

        window->resize(640, 600);
        QTRY_VERIFY(more->isVisible());
        QTest::qWait(50);
        // Nothing sticks out of the bar.
        const QRectF bar = toolbar->mapRectToScene(QRectF(0, 0, toolbar->width(), toolbar->height()));
        const QRectF button = more->mapRectToScene(QRectF(0, 0, more->width(), more->height()));
        QVERIFY2(button.right() <= bar.right() + 0.5, qPrintable(u"more ends at %1 of %2"_s.arg(button.right()).arg(bar.right())));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, button.center().toPoint());
        auto *popup = window->findChild<QObject *>(u"morePopup"_s);
        QTRY_VERIFY(popup->property("opened").toBool());
        // As wide as what it holds, not wider.
        const qreal held = toolbar->property("overflowWidth").toReal();
        QVERIFY(held > 0);
        QCOMPARE(popup->property("width").toReal(), qMin(toolbar->width() - 8, held + 12));
        QTest::qWait(50);
        saveScreenshot(window, u"editor-narrow"_s);

        // Just too narrow: only the last few go, in a small menu.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, button.center().toPoint());
        QTRY_VERIFY(!popup->property("opened").toBool());
        window->resize(int(full) - 30, 600);
        QTRY_VERIFY(more->isVisible());
        QTest::qWait(50);
        const QRectF moreAt = more->mapRectToScene(QRectF(0, 0, more->width(), more->height()));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, moreAt.center().toPoint());
        QTRY_VERIFY(popup->property("opened").toBool());
        const qreal few = toolbar->property("overflowWidth").toReal();
        QVERIFY2(few < 120, qPrintable(QString::number(few)));
        QCOMPARE(popup->property("width").toReal(), few + 12);
        QTest::qWait(50);
        saveScreenshot(window, u"editor-few"_s);

        window->resize(int(full) + 60, 600);
        QTRY_VERIFY(!more->isVisible());
        QTRY_VERIFY(!popup->property("opened").toBool());
        window->close();
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

        // Its button, clicked again, stays checked: one mode is always shown.
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const std::function<QQuickItem *(QQuickItem *, const QString &)> find
            = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (QQuickItem *child : item->childItems()) {
                if (QQuickItem *found = find(child, name))
                    return found;
            }
            return nullptr;
        };
        QQuickItem *button = nullptr;
        QTRY_VERIFY((button = find(window->contentItem(), u"mode-window"_s)) && button->isVisible());
        QVERIFY(button->property("checked").toBool());
        const QPoint middle = button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, middle);
        QCOMPARE(window->property("pickMode").toString(), u"window"_s);
        QVERIFY(button->property("checked").toBool());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, middle);
        QVERIFY(button->property("checked").toBool());
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

    // The screenshots of the README and the site, staged on a made-up
    // desktop: the capture overlay, the editor combining two screenshots,
    // and the editor measuring pixels.
    void siteScreenshots()
    {
        const QString dir = qEnvironmentVariable("GUCKSTE_SITE_SHOTS");
        if (dir.isEmpty())
            QSKIP("GUCKSTE_SITE_SHOTS is not set");
        QDir().mkpath(dir);
        Preferences::instance()->setPickMode(u"region"_s);
        Preferences::instance()->setLastRegion(QRect());
        Preferences::instance()->setSnapToGuides(true);
        Preferences::instance()->setSaveDirectory(u"/home/you/Pictures/Screenshots"_s);

        // The settings window, as it is.
        QQmlComponent settingsComponent(m_engine);
        settingsComponent.loadFromModule("Guckste", "SettingsWindow");
        auto *settings = qobject_cast<QQuickWindow *>(settingsComponent.create());
        QVERIFY2(settings, qPrintable(settingsComponent.errorString()));
        settings->show();
        QVERIFY(QTest::qWaitForWindowExposed(settings));
        QTest::qWait(100);
        const QImage settingsShot = settings->grabWindow();
        settings->close();
        delete settings;

        const QImage settingsWindow = framed(settingsShot, u"Guckste Settings"_s);
        const QImage terminalWindow = framed(terminal(520, 210), u"you@workstation: ~"_s);
        const QPoint settingsAt(140, 70);
        const QPoint terminalAt(700, 470);
        const QImage screen = desktop(QSize(1280, 800), {{settingsWindow, settingsAt}, {terminalWindow, terminalAt}});

        // 1. The overlay: a region over the settings, with marks.
        {
            auto *document = new Document;
            document->setImage(screen);
            QQmlComponent component(m_engine);
            component.loadFromModule("Guckste", "CaptureOverlay");
            auto *window = qobject_cast<QQuickWindow *>(component.createWithInitialProperties(
                {{u"document"_s, QVariant::fromValue(document)}, {u"screenGeometry"_s, QRect(0, 0, 1280, 800)},
                 {u"pointerScreen"_s, QRectF(0, 0, 1280, 800)}}));
            QVERIFY2(window, qPrintable(component.errorString()));
            document->setParent(window);
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
            const QRect region(settingsAt.x() - 20, settingsAt.y() + 30, settingsWindow.width() + 200, 330);
            QMetaObject::invokeMethod(window, "setSelection", Q_ARG(double, region.x()), Q_ARG(double, region.y()),
                                      Q_ARG(double, region.width()), Q_ARG(double, region.height()));
            // Around the save folder row, numbered, and pointed at.
            const QColor red(0xff, 0x3b, 0x30);
            const int row = settingsAt.y() + 30 + 32;
            document->beginAnnotation(Document::Rectangle, settingsAt.x() + 12, row - 17, red, 3);
            document->updateAnnotation(settingsAt.x() + 452, row + 17, false);
            document->commitAnnotation();
            document->beginAnnotation(Document::Counter, settingsAt.x() + 12, row - 17, red, 4);
            document->commitAnnotation();
            document->beginAnnotation(Document::Arrow, settingsAt.x() + 600, row + 104, red, 4);
            document->updateAnnotation(settingsAt.x() + 462, row + 12, false);
            document->commitAnnotation();
            document->addText(settingsAt.x() + 520, row + 112, u"Screenshots go here"_s, QColor(0xff, 0xcc, 0x00), 22);
            window->setProperty("tool", int(Document::Arrow));
            QTest::qWait(150);
            window->grabWindow().save(dir + u"/overlay.png"_s);
            window->close();
            delete window;
        }

        // 2. The editor: the settings and the terminal combined, marked up,
        // with layers and a guide.
        {
            auto *document = new Document;
            const QRect settingsPart(settingsAt, settingsWindow.size());
            document->setImage(screen.copy(settingsPart.adjusted(-16, -16, 16, 16)));
            QQmlComponent component(m_engine);
            component.loadFromModule("Guckste", "EditorWindow");
            auto *window = qobject_cast<QQuickWindow *>(
                component.createWithInitialProperties({{u"document"_s, QVariant::fromValue(document)}}));
            QVERIFY2(window, qPrintable(component.errorString()));
            document->setParent(window);
            window->resize(1400, 900);
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
            const QImage term = screen.copy(QRect(terminalAt, terminalWindow.size()).adjusted(-16, -16, 16, 16));
            const int added = document->addImage(term, document->imageWidth() + 24, 120);
            // The save folder row circled, the listing it fills pointed at,
            // the window measured, a guide along the terminal's top.
            const QColor red(0xff, 0x3b, 0x30);
            const int width = document->imageWidth();
            const int row = 16 + 30 + 32;
            document->beginAnnotation(Document::Ellipse, 22, row - 22, red, 3);
            document->updateAnnotation(476, row + 22, false);
            document->commitAnnotation();
            document->beginAnnotation(Document::Counter, 26, row - 22, red, 4);
            document->commitAnnotation();
            document->beginAnnotation(Document::Arrow, width + 70, 230, red, 4);
            document->updateAnnotation(400, row + 14, false);
            document->commitAnnotation();
            document->beginAnnotation(Document::Counter, width + 12, 236, red, 4);
            document->commitAnnotation();
            document->addText(width + 40, 70, u"Saved where the setting says"_s, red, 22);
            document->beginAnnotation(Document::Measure, 16, document->imageHeight() - 6, QColor(0x00, 0x7a, 0xff), 2);
            document->updateAnnotation(width - 16, document->imageHeight() - 6, true);
            document->commitAnnotation();
            document->addGuide(false, 120);
            document->setSelectedIndex(added);
            QTest::qWait(50);
            QMetaObject::invokeMethod(window, "zoomToFit");
            QTest::qWait(200);
            window->grabWindow().save(dir + u"/editor.png"_s);
            window->setProperty("discardConfirmed", true);
            window->close();
            delete window;
        }

        // 4. Stills: a recording of a click on the settings' stills box,
        // with the stills before and after it, in the editor, and the one
        // after opened as an image, the pointer a layer of its own.
        {
            QQmlComponent component(m_engine);
            component.loadFromModule("Guckste", "SettingsWindow");
            auto *settingsAgain = qobject_cast<QQuickWindow *>(component.create());
            QVERIFY2(settingsAgain, qPrintable(component.errorString()));
            settingsAgain->show();
            QVERIFY(QTest::qWaitForWindowExposed(settingsAgain));
            // Shown as on X11: offscreen has nothing to listen to keys and
            // clicks with, and leaves those boxes out.
            QQuickItem *box = nullptr;
            const std::function<void(QQuickItem *)> find = [&](QQuickItem *item) {
                const QString text = item->property("text").toString();
                if (item->inherits("QQuickCheckBox") && (text.contains(u"keys"_s) || text.contains(u"each click"_s))) {
                    item->setVisible(true);
                    if (text.contains(u"each click"_s))
                        box = item;
                }
                for (QQuickItem *child : item->childItems())
                    find(child);
            };
            find(settingsAgain->contentItem());
            QVERIFY(box);
            QTest::qWait(100);
            const QPointF boxAt = box->mapToScene(QPointF(12, box->height() / 2));
            box->setProperty("hoverEnabled", true);
            QTest::mouseMove(settingsAgain, boxAt.toPoint());
            QTest::qWait(150);
            const QImage hovered = settingsAgain->grabWindow();
            Preferences::instance()->setStillsOnClicks(true);
            QTest::qWait(150);
            const QImage checked = settingsAgain->grabWindow();
            Preferences::instance()->setStillsOnClicks(false);
            settingsAgain->close();
            delete settingsAgain;

            // The region recorded: the settings window and a margin.
            const QRect region = QRect(settingsAt, settingsWindow.size()).adjusted(-24, -24, 24, 24);
            const auto shot = [&](const QImage &content) {
                const QImage screen = desktop(QSize(1280, 800), {{framed(content, u"Guckste Settings"_s), settingsAt},
                                                                {terminalWindow, terminalAt}});
                return screen.copy(QRect(region.topLeft(), QSize(region.width() & ~1, region.height() & ~1)));
            };
            const QImage before = shot(hovered);
            const QImage after = shot(checked);
            QImage pointer(18, 26, QImage::Format_ARGB32_Premultiplied);
            pointer.fill(Qt::transparent);
            {
                QPainter p(&pointer);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(QPen(Qt::white, 1.5));
                p.setBrush(Qt::black);
                p.drawPolygon(QPolygonF({{1, 1}, {1, 20}, {6, 15.5}, {9.5, 23.5}, {12.5, 22}, {9, 14.5}, {15, 14.5}}));
            }
            const QPointF tip = boxAt + QPointF(0, 30) + settingsAt - region.topLeft();
            const QRectF pointerRect(tip - QPointF(1, 1), QSizeF(pointer.size()));
            const auto withPointer = [&](QImage image) {
                QPainter(&image).drawImage(pointerRect, pointer);
                return image;
            };
            QList<QImage> frames;
            for (int i = 0; i < 90; ++i)
                frames << withPointer(i < 45 ? before : after);
            QTemporaryDir videoDir;
            const QString clip = writeVideoOf(videoDir.path() + u"/Recording.mp4"_s, frames);
            QVERIFY(!clip.isEmpty());
            const auto msOf = [](int frame) { return int((frame * 33333LL + 999) / 1000); };
            Still beforeStill;
            beforeStill.path = videoDir.path() + u"/01-before-click.png"_s;
            beforeStill.ms = msOf(44);
            beforeStill.kind = Still::Kind::BeforeClick;
            beforeStill.pointer = pointer;
            beforeStill.pointerRect = pointerRect;
            Still afterStill = beforeStill;
            afterStill.path = videoDir.path() + u"/02-after-click.png"_s;
            afterStill.ms = msOf(45 + 9);
            afterStill.kind = Still::Kind::AfterClick;
            QVERIFY(before.save(beforeStill.path) && after.save(afterStill.path));

            Document *document = nullptr;
            QQuickWindow *window = videoEditor(clip, document);
            QVERIFY(window);
            document->addStills(clip, {beforeStill, afterStill});
            window->resize(1400, 900);
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
            auto *playback = window->findChild<QObject *>(u"playback"_s);
            QVERIFY(playback);
            QTRY_VERIFY(playback->property("duration").toInt() > 0);
            playback->setProperty("position", afterStill.ms);
            QTRY_VERIFY(window->property("atStill").toBool());
            QMetaObject::invokeMethod(window, "zoomToFit");
            QTest::qWait(300);
            window->grabWindow().save(dir + u"/stills-editor.png"_s);

            // Close up: the box's label in a frame of the video, as the
            // player decodes it, and in the still.
            QSignalSpy decodedFrames(document, &Document::framesChanged);
            playback->setProperty("position", afterStill.ms + 100);
            QTRY_VERIFY(!window->property("atStill").toBool());
            QTRY_VERIFY(decodedFrames.count() > 0);
            QTest::qWait(300);
            const QImage decoded = document->clipImage(document->annotations().first());
            const QRect label(qRound(tip.x()) + 16, qRound(tip.y()) - 11, 150, 22);
            const int zoom = 4;
            const int caption = 30;
            QImage compare(label.width() * zoom, 2 * (label.height() * zoom + caption), QImage::Format_RGB32);
            compare.fill(QColor(0xfb, 0xf8, 0xf7));
            {
                QPainter p(&compare);
                QFont font = p.font();
                font.setPixelSize(15);
                p.setFont(font);
                p.setPen(QColor(0x22, 0x17, 0x1c));
                const auto part = [&](const QImage &from, int top, const QString &text) {
                    p.drawText(QRect(0, top, compare.width(), caption), Qt::AlignVCenter, text);
                    p.drawImage(QRect(0, top + caption, label.width() * zoom, label.height() * zoom),
                                from.copy(label).scaled(label.size() * zoom));
                };
                part(decoded, 0, u"A frame of the video"_s);
                part(after, label.height() * zoom + caption, u"The lossless still"_s);
            }
            compare.save(dir + u"/stills-compare.png"_s);
            playback->setProperty("position", afterStill.ms);
            QTRY_VERIFY(window->property("atStill").toBool());

            const QList<QWindow *> others = QGuiApplication::topLevelWindows();
            QVERIFY(QMetaObject::invokeMethod(window, "takeFrame"));
            QQuickWindow *still = nullptr;
            QTRY_VERIFY((still = [&]() -> QQuickWindow * {
                for (QWindow *w : QGuiApplication::topLevelWindows()) {
                    if (!others.contains(w) && w->property("videoMode").isValid() && w->isVisible())
                        return qobject_cast<QQuickWindow *>(w);
                }
                return nullptr;
            }()));
            auto *frame = still->property("document").value<Document *>();
            QVERIFY(frame);
            QCOMPARE(frame->annotationCount(), 2);
            frame->setLocked(0, false);
            frame->setSelectedIndex(1);
            still->resize(1400, 900);
            QTest::qWait(200);
            QMetaObject::invokeMethod(still, "zoomToFit");
            QTest::qWait(300);
            still->grabWindow().save(dir + u"/stills-frame.png"_s);
            still->setProperty("discardConfirmed", true);
            still->close();

            // Both stills as one picture, a number on each and an arrow
            // across to what the click led to.
            const QList<QWindow *> beforeSheet = QGuiApplication::topLevelWindows();
            m_app->openStills(document, 0, -1);
            QQuickWindow *sheet = nullptr;
            QTRY_VERIFY((sheet = [&]() -> QQuickWindow * {
                for (QWindow *w : QGuiApplication::topLevelWindows()) {
                    if (!beforeSheet.contains(w) && w->property("videoMode").isValid() && w->isVisible())
                        return qobject_cast<QQuickWindow *>(w);
                }
                return nullptr;
            }()));
            auto *flow = sheet->property("document").value<Document *>();
            QVERIFY(flow);
            const QColor red(0xff, 0x3b, 0x30);
            const QRectF second = flow->annotations()[2].rect();
            flow->beginAnnotation(Document::Counter, 16, 16, red, 6);
            flow->commitAnnotation();
            flow->beginAnnotation(Document::Counter, second.x() + 16, 16, red, 6);
            flow->commitAnnotation();
            flow->beginAnnotation(Document::Arrow, second.x() - 70, tip.y(), red, 6);
            flow->updateAnnotation(second.x() + tip.x() - 14, tip.y(), false);
            flow->commitAnnotation();
            flow->setSelectedIndex(-1);
            sheet->resize(1400, 900);
            QTest::qWait(200);
            QMetaObject::invokeMethod(sheet, "zoomToFit");
            QTest::qWait(300);
            sheet->grabWindow().save(dir + u"/stills-sheet.png"_s);
            sheet->setProperty("discardConfirmed", true);
            sheet->close();
            window->setProperty("discardConfirmed", true);
            window->close();
            QTRY_VERIFY(!window->isVisible());
            delete window;

            // The settings around the box, its neighbours above and below.
            checked.copy(QRect(0, qRound(boxAt.y()) - 110, checked.width(), 170)).save(dir + u"/stills-settings.png"_s);
        }

        // 3. Measuring: a corner of the settings at 800 %, the pixel grid
        // on, marks counting the pixels.
        {
            auto *document = new Document;
            document->setImage(settingsShot.copy(QRect(10, 10, 140, 90)));
            QQmlComponent component(m_engine);
            component.loadFromModule("Guckste", "EditorWindow");
            auto *window = qobject_cast<QQuickWindow *>(
                component.createWithInitialProperties({{u"document"_s, QVariant::fromValue(document)}}));
            QVERIFY2(window, qPrintable(component.errorString()));
            document->setParent(window);
            window->setProperty("showLayers", false);
            window->setProperty("showGrid", true);
            window->resize(1100, 720);
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
            QMetaObject::invokeMethod(window, "zoomAt", Q_ARG(double, 8.0), Q_ARG(double, 0.0), Q_ARG(double, 0.0));
            // Found in the pixels: the "Save to" label's width, and the gap
            // between the two fields.
            const QImage crop = document->image();
            const auto dark = [&](int x, int y) { return qGray(crop.pixel(x, y)) < 110; };
            int left = crop.width(), right = -1, bottom = -1;
            for (int y = 10; y < 32; ++y)
                for (int x = 0; x < 64; ++x)
                    if (dark(x, y)) {
                        left = qMin(left, x);
                        right = qMax(right, x);
                        bottom = qMax(bottom, y);
                    }
            const auto border = [&](int x, int y) {
                const int g = qGray(crop.pixel(x, y));
                return g > 120 && g < 200;
            };
            int firstBottom = -1, secondTop = -1;
            for (int y = 20; y < crop.height() - 1 && secondTop < 0; ++y) {
                if (firstBottom < 0 && border(110, y) && !border(110, y + 1))
                    firstBottom = y + 1;
                else if (firstBottom >= 0 && y > firstBottom + 1 && border(110, y))
                    secondTop = y;
            }
            QVERIFY(right > left && secondTop > firstBottom && firstBottom > 0);
            const QColor red(0xff, 0x3b, 0x30);
            document->beginAnnotation(Document::Measure, left, bottom + 4, red, 2);
            document->updateAnnotation(right + 1, bottom + 4, true);
            document->commitAnnotation();
            document->beginAnnotation(Document::Measure, 100, firstBottom, QColor(0x00, 0x7a, 0xff), 2);
            document->updateAnnotation(100, secondTop, true);
            document->commitAnnotation();
            window->setProperty("tool", int(Document::Measure));
            QTest::qWait(200);
            window->grabWindow().save(dir + u"/measure.png"_s);
            window->setProperty("discardConfirmed", true);
            window->close();
            delete window;
        }
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
        // Stills are kept while recording only.
        auto *keepStill = window->findChild<QQuickItem *>(u"keepStill"_s);
        QVERIFY(keepStill);
        QVERIFY(!keepStill->isEnabled());
        // A tooltip in a window of its own, beside the panel on the side
        // away from the region, and not over its buttons.
        auto *stop = window->findChild<QQuickItem *>(u"stop"_s);
        QVERIFY(stop);
        // Hovering is on with a mouse, which offscreen does not say.
        stop->setProperty("hoverEnabled", true);
        QTest::mouseMove(window, stop->mapToScene(QPointF(stop->width() / 2, stop->height() / 2)).toPoint());
        QObject *tip = tipOf(stop);
        QVERIFY(tip);
        QTRY_VERIFY(tip->property("opened").toBool());
        QCOMPARE(tip->property("popupType").toInt(), 1); // Popup.Window
        if (window->property("tipsBelow").toBool())
            QVERIFY(tip->property("y").toReal() >= stop->height());
        else
            QVERIFY(tip->property("y").toReal() + tip->property("height").toReal() <= 0);
        window->close();
        delete window;
    }

    // The tooltip of an item, as Tip.qml makes one.
    static QObject *tipOf(QObject *item)
    {
        for (QObject *child : item->findChildren<QObject *>()) {
            if (child->inherits("QQuickToolTip"))
                return child;
        }
        return nullptr;
    }

    // What a video file holds, read as the app reads it.
    static VideoInfo probed(const QString &path)
    {
        VideoInfo info;
        bool done = false;
        QObject context;
        VideoProbe::probe(path, &context, [&](const VideoInfo &video) {
            info = video;
            done = true;
        });
        if (!QTest::qWaitFor([&done] { return done; }, 10000))
            qWarning("No answer about %s", qPrintable(path));
        return info;
    }

    // The editor on a video, as the app opens one.
    QQuickWindow *videoEditor(const QString &clip, Document *&document)
    {
        document = new Document;
        if (document->addVideo(probed(clip), Document::Placement::Base) < 0) {
            delete document;
            return nullptr;
        }
        QQmlComponent component(m_engine);
        component.loadFromModule("Guckste", "EditorWindow");
        auto *window = qobject_cast<QQuickWindow *>(
            component.createWithInitialProperties({{u"document"_s, QVariant::fromValue(document)}}));
        if (!window) {
            qWarning("%s", qPrintable(component.errorString()));
            delete document;
            return nullptr;
        }
        document->setParent(window);
        return window;
    }

    // Play plays: the position runs on, and the button turns to pause.
    void videoEditorPlays()
    {
        QTemporaryDir dir;
        // A real recording instead, to look at: GUCKSTE_TEST_VIDEO.
        const QString given = qEnvironmentVariable("GUCKSTE_TEST_VIDEO");
        const QString clip = !given.isEmpty() ? given : writeVideo(dir.path() + u"/clip.mp4"_s, 90, Qt::darkCyan);
        QVERIFY(!clip.isEmpty());
        Document *document = nullptr;
        QQuickWindow *window = videoEditor(clip, document);
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *playback = window->findChild<QObject *>(u"playback"_s);
        auto *play = window->findChild<QQuickItem *>(u"playButton"_s);
        QVERIFY(playback && play);
        QVERIFY(window->property("videoMode").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(playback->property("duration").toInt() > 2000, 5000);
        QTest::qWait(500);
        // The first frame is on screen before anything plays.
        QTRY_VERIFY_WITH_TIMEOUT(document->clipImage(document->annotations().first()).pixelColor(10, 10)
                                     != QColor(Qt::black), 5000);

        const QPoint at = play->mapToScene(QPointF(play->width() / 2, play->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
        QTest::qWait(1000);
        QVERIFY(playback->property("playing").toBool());
        QVERIFY(playback->property("position").toInt() > 300);
        QCOMPARE(document->time(), playback->property("position").toInt());
        // What is on screen is the clip, drawn whole: its frames are not
        // converted on a GPU context of their own, which with OpenGL
        // threw the window's drawing into disarray.
        auto *area = window->findChild<QQuickItem *>(u"drawingArea"_s);
        QVERIFY(area);
        const auto shownAt = [&](qreal x, qreal y) {
            QPointF p;
            QMetaObject::invokeMethod(area, "toView", Q_RETURN_ARG(QPointF, p), Q_ARG(double, x), Q_ARG(double, y));
            return window->grabWindow().pixelColor(area->mapToScene(p).toPoint());
        };
        saveScreenshot(window, u"video-playing"_s);
        // Held playing, to be looked at on screen.
        if (const int hold = qEnvironmentVariableIntValue("GUCKSTE_TEST_HOLD"); hold > 0)
            QTest::qWait(hold);
        for (const QPointF at : given.isEmpty() ? QList<QPointF>{QPointF(20, 20), QPointF(160, 120), QPointF(300, 220)} : QList<QPointF>()) {
            const QColor seen = shownAt(at.x(), at.y());
            QVERIFY2(std::abs(seen.red() - 0) < 40 && std::abs(seen.green() - 128) < 40 && std::abs(seen.blue() - 128) < 40,
                     qPrintable(seen.name()));
        }

        // Again: paused where it was; once more: on from there.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
        QTRY_VERIFY(!playback->property("playing").toBool());
        const int paused = playback->property("position").toInt();
        QTest::qWait(300);
        QCOMPARE(playback->property("position").toInt(), paused);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
        QTest::qWait(500);
        QVERIFY(playback->property("playing").toBool());
        QVERIFY(playback->property("position").toInt() > paused + 200);

        // At the very end the clip is still on screen, on its last frame.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
        const int duration = playback->property("duration").toInt();
        playback->setProperty("position", duration);
        QCOMPARE(playback->property("position").toInt(), duration);
        QCOMPARE(document->time(), duration - 1);
        QVERIFY(document->isShown(0));

        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
        delete window;
    }

    // Each mark on a recording gets a lane: its bar's edges set when it
    // shows, the bar moves it in time, and it shows only then.
    void videoEditorLanesTimeMarks()
    {
        QTemporaryDir dir;
        const QString clip = writeVideo(dir.path() + u"/clip.mp4"_s, 120, Qt::darkCyan);
        QVERIFY(!clip.isEmpty());
        Document *document = nullptr;
        QQuickWindow *window = videoEditor(clip, document);
        QVERIFY(window);
        window->resize(1000, 700);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *playback = window->findChild<QObject *>(u"playback"_s);
        auto *lanes = window->findChild<QQuickItem *>(u"lanes"_s);
        auto *timeline = window->findChild<QQuickItem *>(u"timeline"_s);
        QVERIFY(playback && lanes && timeline);
        QTRY_VERIFY_WITH_TIMEOUT(playback->property("duration").toInt() > 3500, 5000);
        QTRY_COMPARE(document->time(), 0);

        // The clip is layer 0; the marks go above it.
        document->beginAnnotation(Document::Rectangle, 20, 20, Qt::red, 3);
        document->updateAnnotation(120, 100, false);
        document->commitAnnotation();
        document->addText(150, 40, u"Later"_s, Qt::yellow, 24);
        QTRY_COMPARE(lanes->property("count").toInt(), 3);
        // Laid out, so the bars are where they will be pressed.
        QTest::qWait(100);
        QCOMPARE(document->annotationStart(1), 0);
        QCOMPARE(document->annotationEnd(1), -1);

        // Lane rows are topmost first: the rectangle's is the second.
        const auto at = [&](int ms, int row) {
            qreal x = 0;
            QMetaObject::invokeMethod(lanes, "msToX", Q_RETURN_ARG(qreal, x), Q_ARG(int, ms));
            const qreal y = (row + 0.5) * lanes->property("laneHeight").toReal();
            return lanes->mapToScene(QPointF(x, y)).toPoint();
        };
        const int duration = playback->property("duration").toInt();

        // The rectangle's out edge, dragged in to 1.5 s.
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at(duration, 1) - QPoint(2, 0));
        QTest::mouseMove(window, at(2500, 1));
        QTest::mouseMove(window, at(1500, 1));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at(1500, 1));
        QVERIFY2(std::abs(document->annotationEnd(1) - 1500) < 60, qPrintable(QString::number(document->annotationEnd(1))));
        QCOMPARE(document->annotationStart(1), 0);
        QCOMPARE(document->selectedIndex(), 1);

        // At 2 s it is gone, the text is still there.
        playback->setProperty("position", 2000);
        QTRY_COMPARE(document->time(), 2000);
        QVERIFY(!document->isShown(1));
        QVERIFY(document->isShown(2));

        // The rectangle's bar moved a second on by its middle: it keeps
        // its length, and shows at 2 s again.
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at(750, 1));
        QTest::mouseMove(window, at(1250, 1));
        QTest::mouseMove(window, at(1750, 1));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at(1750, 1));
        QVERIFY2(std::abs(document->annotationStart(1) - 1000) < 60, qPrintable(QString::number(document->annotationStart(1))));
        QVERIFY(std::abs((document->annotationEnd(1) - document->annotationStart(1)) - 1500) < 60);
        QVERIFY(document->isShown(1));
        QTest::qWait(50);
        saveScreenshot(window, u"video-lanes"_s);

        // Right-click: the whole time again.
        document->setAnnotationTiming(1, 0, -1);
        QVERIFY(document->isShown(1));

        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
        delete window;
    }

    // Dragged past the end, a mark or a clip makes the video longer: the
    // timeline has room there, and more when a drag nears its edge.
    void videoEditorTimelineGrows()
    {
        QTemporaryDir dir;
        const QString clip = writeVideo(dir.path() + u"/clip.mp4"_s, 60, Qt::darkCyan);
        QVERIFY(!clip.isEmpty());
        Document *document = nullptr;
        QQuickWindow *window = videoEditor(clip, document);
        QVERIFY(window);
        window->resize(1000, 700);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *playback = window->findChild<QObject *>(u"playback"_s);
        auto *lanes = window->findChild<QQuickItem *>(u"lanes"_s);
        QVERIFY(playback && lanes);
        QTRY_VERIFY_WITH_TIMEOUT(playback->property("duration").toInt() > 1500, 5000);
        const int length = playback->property("duration").toInt();
        // Room past the end.
        QVERIFY(lanes->property("span").toInt() > length);

        document->setLocked(0, false);
        document->beginAnnotation(Document::Rectangle, 20, 20, Qt::red, 3);
        document->updateAnnotation(120, 100, false);
        document->commitAnnotation();
        QTRY_COMPARE(lanes->property("count").toInt(), 2);
        QTest::qWait(100);
        const auto at = [&](int ms, int row) {
            qreal x = 0;
            QMetaObject::invokeMethod(lanes, "msToX", Q_RETURN_ARG(qreal, x), Q_ARG(int, ms));
            const qreal y = (row + 0.5) * lanes->property("laneHeight").toReal();
            return lanes->mapToScene(QPointF(x, y)).toPoint();
        };

        // The mark's end, from the end of the video to the end of the room
        // and on: its end is past the video's, which grows to it.
        const QPoint edge = at(length, 0) - QPoint(2, 0);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, edge);
        const int span = lanes->property("span").toInt();
        const QPoint end = at(span, 0) - QPoint(4, 0);
        QTest::mouseMove(window, at(span, 0) - QPoint(30, 0));
        // Moving about at the edge makes no room by itself; held there,
        // a tenth or so a second, at least 100 ms each 100 ms, give or
        // take a tick.
        for (int i = 0; i < 10; ++i)
            QTest::mouseMove(window, end - QPoint(i % 2, 0));
        QVERIFY(lanes->property("span").toInt() < span * 1.05);
        QTest::qWait(1000);
        const int grown = lanes->property("span").toInt();
        QVERIFY2(grown > span * 1.05 && grown < span * 1.3, qPrintable(u"%1 to %2"_s.arg(span).arg(grown)));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY2(document->annotationEnd(1) > length + 500, qPrintable(QString::number(document->annotationEnd(1))));
        QCOMPARE(document->duration(), document->annotationEnd(1));
        QTRY_COMPARE(playback->property("duration").toInt(), document->annotationEnd(1));
        QCOMPARE(lanes->property("heldSpan").toInt(), 0);

        // The clip, moved a second later by its bar: it starts there, as
        // long as before.
        QTest::qWait(50);
        const QPoint middle = at(length / 2, 1);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, middle);
        QTest::mouseMove(window, at(length / 2 + 500, 1));
        QTest::mouseMove(window, at(length / 2 + 1000, 1));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at(length / 2 + 1000, 1));
        QVERIFY2(std::abs(document->annotationStart(0) - 1000) < 80, qPrintable(QString::number(document->annotationStart(0))));
        QCOMPARE(document->annotationEnd(0) - document->annotationStart(0), length);
        QVERIFY(document->duration() >= length + 900);
        QTest::qWait(50);
        saveScreenshot(window, u"video-timeline-grown"_s);

        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
        delete window;
    }

    // Lanes selected together move and stretch together; many lanes
    // scroll; the divider above the panel gives it more room.
    void videoEditorLanesTogether()
    {
        QTemporaryDir dir;
        const QString clip = writeVideo(dir.path() + u"/clip.mp4"_s, 120, Qt::darkCyan);
        QVERIFY(!clip.isEmpty());
        Document *document = nullptr;
        QQuickWindow *window = videoEditor(clip, document);
        QVERIFY(window);
        window->resize(1000, 700);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *playback = window->findChild<QObject *>(u"playback"_s);
        auto *lanes = window->findChild<QQuickItem *>(u"lanes"_s);
        auto *panel = window->findChild<QQuickItem *>(u"bottomPanel"_s);
        QVERIFY(playback && lanes && panel);
        QTRY_VERIFY_WITH_TIMEOUT(playback->property("duration").toInt() > 3500, 5000);
        QTRY_COMPARE(document->time(), 0);

        // Three marks over the clip, layers 1 to 3, each shown from 0.5 s
        // to 1.5 s.
        for (int i = 1; i <= 3; ++i) {
            document->beginAnnotation(Document::Rectangle, 10 + i * 30, 10, Qt::red, 3);
            document->updateAnnotation(30 + i * 30, 40, false);
            document->commitAnnotation();
            document->setAnnotationTiming(i, 500, 1500);
        }
        QTRY_COMPARE(lanes->property("count").toInt(), 4);
        QTest::qWait(100);
        const auto at = [&](int ms, int row) {
            qreal x = 0;
            QMetaObject::invokeMethod(lanes, "msToX", Q_RETURN_ARG(qreal, x), Q_ARG(int, ms));
            const qreal y = (row + 0.5) * lanes->property("laneHeight").toReal() - lanes->property("contentY").toReal();
            return lanes->mapToScene(QPointF(x, y)).toPoint();
        };

        // Rows 0 and 2 (marks 3 and 1), picked with Ctrl, moved a second on.
        QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, at(1000, 0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, at(1000, 2));
        QCOMPARE(lanes->property("chosen").toList().size(), 2);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at(1000, 0));
        QTest::mouseMove(window, at(1500, 0));
        QTest::mouseMove(window, at(2000, 0));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at(2000, 0));
        const auto near = [](int a, int b) { return std::abs(a - b) < 60; };
        QVERIFY2(near(document->annotationStart(3), 1500), qPrintable(QString::number(document->annotationStart(3))));
        QVERIFY(near(document->annotationStart(1), 1500));
        QCOMPARE(document->annotationStart(2), 500);
        // One undo step for both.
        document->undo();
        QCOMPARE(document->annotationStart(3), 500);
        QCOMPARE(document->annotationStart(1), 500);

        // Undo reset nothing about the selection; both stretched by their
        // out edges to 2.5 s.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at(1000, 0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, at(1000, 1));
        QCOMPARE(lanes->property("chosen").toList().size(), 2);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at(1500, 1) - QPoint(2, 0));
        QTest::mouseMove(window, at(2000, 1));
        QTest::mouseMove(window, at(2500, 1));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at(2500, 1));
        QVERIFY2(near(document->annotationEnd(3), 2500), qPrintable(QString::number(document->annotationEnd(3))));
        QVERIFY(near(document->annotationEnd(2), 2500));
        QCOMPARE(document->annotationStart(2), 500);
        QCOMPARE(document->annotationEnd(1), 1500);

        // Many marks: the lanes scroll.
        for (int i = 0; i < 6; ++i)
            document->addText(10, 100 + i * 10, u"More"_s, Qt::yellow, 12);
        QTRY_COMPARE(lanes->property("count").toInt(), 10);
        QTest::qWait(100);
        QVERIFY(lanes->property("contentHeight").toReal() > lanes->height());
        const QPoint inLanes = lanes->mapToScene(QPointF(lanes->width() / 2, lanes->height() / 2)).toPoint();
        QWheelEvent wheel(inLanes, window->mapToGlobal(inLanes), QPoint(), QPoint(0, -240), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(window, &wheel);
        QTRY_VERIFY(lanes->property("contentY").toReal() > 0);

        // The divider, dragged up: a taller panel.
        const qreal before = panel->height();
        const QPoint divider = panel->mapToScene(QPointF(panel->width() / 2, -2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, divider);
        QTest::mouseMove(window, divider - QPoint(0, 40));
        QTest::mouseMove(window, divider - QPoint(0, 80));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, divider - QPoint(0, 80));
        QTRY_VERIFY2(panel->height() > before + 40, qPrintable(u"%1 then %2"_s.arg(before).arg(panel->height())));
        QTest::qWait(50);
        saveScreenshot(window, u"video-lanes-many"_s);

        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
        delete window;
    }

    // In every window, a selected mark takes the color and size picked
    // on the toolbar, and selecting one shows its own there.
    void toolbarRestylesTheSelection()
    {
        QTemporaryDir dir;
        const QString clip = writeVideo(dir.path() + u"/clip.mp4"_s, 30, Qt::darkCyan);
        QVERIFY(!clip.isEmpty());
        const VideoInfo video = probed(clip);
        for (const QString kind : {u"EditorWindow"_s, u"video"_s, u"CaptureOverlay"_s}) {
            auto *document = new Document;
            document->setImage(demoScreen(320, 240));
            QVariantMap properties{{u"document"_s, QVariant::fromValue(document)}};
            const QString type = kind == u"video"_s ? u"EditorWindow"_s : kind;
            if (kind == u"CaptureOverlay"_s)
                properties[u"screenGeometry"_s] = QRect(0, 0, 320, 240);
            QQmlComponent component(m_engine);
            component.loadFromModule("Guckste", type.toUtf8());
            auto *window = qobject_cast<QQuickWindow *>(component.createWithInitialProperties(properties));
            QVERIFY2(window, qPrintable(component.errorString()));
            document->setParent(window);
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
            auto *toolbar = window->findChild<QQuickItem *>(u"toolbar"_s);
            QVERIFY2(toolbar, qPrintable(type));

            document->beginAnnotation(Document::Arrow, 20, 20, Qt::red, 3);
            document->updateAnnotation(120, 80, false);
            document->commitAnnotation();
            document->addText(40, 120, u"Note"_s, Qt::yellow, 20);
            // With a clip beside, the editor is one for video.
            if (kind == u"video"_s) {
                QVERIFY(document->addVideo(video, Document::Placement::Beside) == 2);
                QTRY_VERIFY(window->property("videoMode").toBool());
            }
            QVERIFY(document->annotationCount() >= 2);

            // Selected, the arrow shows its color and takes new ones.
            document->setSelectedIndex(0);
            QTRY_COMPARE(toolbar->property("color").value<QColor>(), QColor(Qt::red));
            QMetaObject::invokeMethod(toolbar, "pickColor", Q_ARG(QColor, QColor(Qt::green)));
            QMetaObject::invokeMethod(toolbar, "pickSize", Q_ARG(double, 9.0));
            QCOMPARE(document->annotationColor(0), QColor(Qt::green));
            QCOMPARE(document->annotationSize(0), 9.0);
            QVERIFY(!toolbar->property("showsTextSize").toBool());

            // The text: its size in pixels, its color.
            document->setSelectedIndex(1);
            QTRY_COMPARE(toolbar->property("textSize").toReal(), 20.0);
            QVERIFY(toolbar->property("showsTextSize").toBool());
            QMetaObject::invokeMethod(toolbar, "pickTextSize", Q_ARG(double, 36.0));
            QMetaObject::invokeMethod(toolbar, "pickColor", Q_ARG(QColor, QColor(Qt::blue)));
            QCOMPARE(document->annotationSize(1), 36.0);
            QCOMPARE(document->annotationColor(1), QColor(Qt::blue));
            // The arrow is left as it was.
            QCOMPARE(document->annotationColor(0), QColor(Qt::green));

            // Nothing selected: only the next mark's style changes.
            document->setSelectedIndex(-1);
            QMetaObject::invokeMethod(toolbar, "pickColor", Q_ARG(QColor, QColor(Qt::magenta)));
            QCOMPARE(document->annotationColor(1), QColor(Qt::blue));

            window->setProperty("discardConfirmed", true);
            window->close();
            delete window;
        }
    }

    // One editor for both: with a clip in it, the timeline shows and a
    // frame can be taken out as an image of its own; the clip it opened
    // with is locked until it is unlocked in the layer list.
    void editorTakesAFrame()
    {
        QTemporaryDir dir;
        const QString clip = writeVideo(dir.path() + u"/clip.mp4"_s, 60, Qt::darkCyan);
        QVERIFY(!clip.isEmpty());
        Document *document = nullptr;
        QQuickWindow *window = videoEditor(clip, document);
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *panel = window->findChild<QQuickItem *>(u"bottomPanel"_s);
        auto *frameButton = window->findChild<QQuickItem *>(u"frameButton"_s);
        QVERIFY(panel && frameButton);
        QVERIFY(panel->isVisible());
        // On the toolbar, or behind its more button when narrow.
        QVERIFY(frameButton->property("available").toBool());
        QVERIFY(document->annotationLocked(0));
        QCOMPARE(document->hitTest(100, 100, 1), -1);

        // Unlocked from its row in the layer list, whose rows are items
        // of the list rather than its children.
        const std::function<QQuickItem *(QQuickItem *)> lockIn = [&](QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == u"lockButton"_s)
                return item;
            for (QQuickItem *child : item->childItems()) {
                if (QQuickItem *found = lockIn(child))
                    return found;
            }
            return nullptr;
        };
        QQuickItem *lock = nullptr;
        QTRY_VERIFY((lock = lockIn(window->contentItem())));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          lock->mapToScene(QPointF(lock->width() / 2, lock->height() / 2)).toPoint());
        QVERIFY(!document->annotationLocked(0));
        QCOMPARE(document->hitTest(100, 100, 1), 0);

        // A mark, and the frame taken out: a new editor with no video.
        document->addText(20, 20, u"Still"_s, Qt::yellow, 20);
        QTRY_VERIFY_WITH_TIMEOUT(document->clipImage(document->annotations().first()).pixelColor(10, 10)
                                     != QColor(Qt::black), 5000);
        const QList<QWindow *> before = QGuiApplication::topLevelWindows();
        QVERIFY(QMetaObject::invokeMethod(window, "takeFrame"));
        QQuickWindow *still = nullptr;
        QTRY_VERIFY((still = [&]() -> QQuickWindow * {
            for (QWindow *w : QGuiApplication::topLevelWindows()) {
                if (!before.contains(w) && w->property("videoMode").isValid() && w->isVisible())
                    return qobject_cast<QQuickWindow *>(w);
            }
            return nullptr;
        }()));
        auto *frame = still->property("document").value<Document *>();
        QVERIFY(frame);
        QVERIFY(!frame->hasVideo());
        QVERIFY(!still->property("videoMode").toBool());
        QCOMPARE(frame->annotationCount(), 2);
        QCOMPARE(frame->annotationTool(0), int(Document::Image));
        QCOMPARE(frame->annotationText(1), u"Still"_s);
        QTest::qWait(100);
        saveScreenshot(still, u"editor-frame"_s);
        const QColor seen = frame->render(1).pixelColor(200, 200);
        QVERIFY2(seen.blue() > 80 && seen.red() < 60, qPrintable(seen.name()));
        still->setProperty("discardConfirmed", true);
        still->close();

        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
        delete window;
    }

    // A recording's stills: marks on the timeline, which a click close by
    // and Ctrl+. and Ctrl+, go to, shown there in place of the decoded
    // frame, and opened lossless as an image.
    void editorFindsStills()
    {
        QTemporaryDir dir;
        const QString clip = writeVideo(dir.path() + u"/clip.mp4"_s, 60, Qt::darkCyan);
        QVERIFY(!clip.isEmpty());
        QImage magenta(320, 240, QImage::Format_RGB32);
        magenta.fill(Qt::magenta);
        QVERIFY(magenta.save(dir.path() + u"/01-kept.png"_s));
        Still kept;
        kept.path = dir.path() + u"/01-kept.png"_s;
        kept.ms = 500;
        Still before = kept;
        before.ms = 1000;
        before.kind = Still::Kind::BeforeClick;
        Document *document = nullptr;
        QQuickWindow *window = videoEditor(clip, document);
        QVERIFY(window);
        document->addStills(clip, {kept, before});
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *timeline = window->findChild<QQuickItem *>(u"timeline"_s);
        auto *playback = window->findChild<QObject *>(u"playback"_s);
        QVERIFY(timeline && playback);
        QTRY_COMPARE(timeline->property("stills").toList().size(), 2);
        int marks = 0;
        const std::function<void(QQuickItem *)> count = [&](QQuickItem *item) {
            marks += item->objectName() == u"still"_s;
            for (QQuickItem *child : item->childItems())
                count(child);
        };
        count(timeline);
        QCOMPARE(marks, 2);
        // The time under the pointer, over it rather than mid-timeline.
        const QPointF hover = timeline->mapToScene(QPointF(timeline->width() * 0.2, timeline->height() / 2));
        QTest::mouseMove(window, hover.toPoint());
        QObject *tip = nullptr;
        QTRY_VERIFY((tip = tipOf(timeline)) && tip->property("opened").toBool());
        const qreal tipMiddle = timeline->mapToScene(
            QPointF(tip->property("x").toReal() + tip->property("width").toReal() / 2, 0)).x();
        QVERIFY2(qAbs(tipMiddle - hover.x()) < 2, qPrintable(u"%1 for %2"_s.arg(tipMiddle).arg(hover.x())));

        // A click a little off the mark lands on it.
        qreal x = 0;
        QMetaObject::invokeMethod(timeline, "msToX", Q_RETURN_ARG(qreal, x), Q_ARG(int, 500));
        const qreal y = timeline->property("trackY").toReal() + 4;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, timeline->mapToScene(QPointF(x + 3, y)).toPoint());
        QTRY_COMPARE(playback->property("position").toInt(), 500);
        QTRY_VERIFY(window->property("atStill").toBool());
        QCOMPARE(document->clipImage(document->annotations().first()).pixelColor(100, 100), QColor(Qt::magenta));
        QTest::keyClick(window, Qt::Key_Period, Qt::ControlModifier);
        QTRY_COMPARE(playback->property("position").toInt(), 1000);
        QTest::keyClick(window, Qt::Key_Comma, Qt::ControlModifier);
        QTRY_COMPARE(playback->property("position").toInt(), 500);
        QTest::keyClick(window, Qt::Key_Period);
        QTRY_VERIFY(!window->property("atStill").toBool());
        QTest::keyClick(window, Qt::Key_Comma);
        QTRY_VERIFY(window->property("atStill").toBool());

        const QList<QWindow *> others = QGuiApplication::topLevelWindows();
        QVERIFY(QMetaObject::invokeMethod(window, "takeFrame"));
        QQuickWindow *still = nullptr;
        QTRY_VERIFY((still = [&]() -> QQuickWindow * {
            for (QWindow *w : QGuiApplication::topLevelWindows()) {
                if (!others.contains(w) && w->property("videoMode").isValid() && w->isVisible())
                    return qobject_cast<QQuickWindow *>(w);
            }
            return nullptr;
        }()));
        auto *frame = still->property("document").value<Document *>();
        QVERIFY(frame);
        QCOMPARE(frame->render(1).pixelColor(100, 100), QColor(Qt::magenta));
        still->setProperty("discardConfirmed", true);
        still->close();

        // The stills of a recording saved into its folder, and opened as
        // one picture.
        QTemporaryDir saveDir;
        const QString savedTo = Preferences::instance()->saveDirectory();
        Preferences::instance()->setSaveDirectory(saveDir.path());
        window->setProperty("recordingName", u"rec"_s);
        QCOMPARE(window->property("keptStills").toInt(), 2);
        QVERIFY(window->findChild<QQuickItem *>(u"saveStillsButton"_s)->property("available").toBool());
        QVERIFY(QMetaObject::invokeMethod(window, "saveStills"));
        QVERIFY(QFileInfo::exists(saveDir.path() + u"/rec/01-kept.png"_s));
        QCOMPARE(window->property("videoTarget").isValid(), false);
        QString target;
        QVERIFY(QMetaObject::invokeMethod(window, "videoTarget", Q_RETURN_ARG(QString, target)));
        QCOMPARE(target, saveDir.path() + u"/rec/rec."_s + m_app->recordingSuffix());
        Preferences::instance()->setSaveDirectory(savedTo);
        const QList<QWindow *> beforeSheet = QGuiApplication::topLevelWindows();
        m_app->openStills(document, 0, -1);
        QQuickWindow *sheet = nullptr;
        QTRY_VERIFY((sheet = [&]() -> QQuickWindow * {
            for (QWindow *w : QGuiApplication::topLevelWindows()) {
                if (!beforeSheet.contains(w) && w->property("videoMode").isValid() && w->isVisible())
                    return qobject_cast<QQuickWindow *>(w);
            }
            return nullptr;
        }()));
        QCOMPARE(sheet->property("document").value<Document *>()->annotationCount(), 2);
        sheet->setProperty("discardConfirmed", true);
        sheet->close();

        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
        delete window;
    }

    // A video added to a screenshot: the editor turns to one for video.
    void editorAddsVideo()
    {
        QTemporaryDir dir;
        const QString clip = writeVideo(dir.path() + u"/clip.mp4"_s, 30, Qt::darkCyan);
        QVERIFY(!clip.isEmpty());
        auto *document = new Document;
        document->setImage(demoScreen(320, 200));
        document->liftBase(u"Screenshot"_s);
        QQmlComponent component(m_engine);
        component.loadFromModule("Guckste", "EditorWindow");
        auto *window = qobject_cast<QQuickWindow *>(
            component.createWithInitialProperties({{u"document"_s, QVariant::fromValue(document)}}));
        QVERIFY2(window, qPrintable(component.errorString()));
        document->setParent(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *panel = window->findChild<QQuickItem *>(u"bottomPanel"_s);
        auto *frameButton = window->findChild<QQuickItem *>(u"frameButton"_s);
        auto *toolbar = window->findChild<QQuickItem *>(u"toolbar"_s);
        QVERIFY(panel && frameButton && toolbar);
        QVERIFY(!window->property("videoMode").toBool());
        QVERIFY(!panel->isVisible());
        QVERIFY(!frameButton->isVisible());
        const auto units = [&] { return toolbar->property("units").toList().size(); };
        const int imageUnits = units();
        QCOMPARE(document->time(), -1);

        QSignalSpy added(document, &Document::videoAdded);
        document->addVideoFile(clip, true);
        QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 10000);
        QTRY_VERIFY(window->property("videoMode").toBool());
        QVERIFY(panel->isVisible());
        QTRY_COMPARE(units(), imageUnits + 1);
        QCOMPARE(document->annotations()[1].rect(), QRectF(320, 0, 320, 240));
        QCOMPARE(document->time(), 0);
        // The screenshot shows the whole time, beside the clip.
        QVERIFY(document->isShown(0));
        QTest::qWait(300);
        saveScreenshot(window, u"editor-video"_s);

        // Closing asks, about the video now.
        window->close();
        auto *dialog = window->findChild<QObject *>(u"discardDialog"_s);
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("message").toString(), u"The changes to this video are not saved."_s);
        QMetaObject::invokeMethod(dialog, "reject");
        QTRY_VERIFY(!dialog->property("visible").toBool());

        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
        delete window;
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
