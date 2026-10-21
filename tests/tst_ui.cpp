// Loads the editor and the capture overlay offscreen and fails on any QML
// warning. Set GUCKSTE_SCREENSHOT_DIR to also save screenshots of them, and
// GUCKSTE_SITE_SHOTS to stage the screenshots of the README and the site.

#include "core/Document.h"
#include "core/Encoding.h"
#include "core/VideoFrames.h"
#include "core/Preferences.h"
#include "core/VideoExporter.h"
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
QString writeVideo(const QString &path, int frames, const QColor &color)
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
    recorder.setVideoResolution(QSize(320, 240));
    recorder.setOutputLocation(QUrl::fromLocalFile(path));
    recorder.record();
    QImage image(320, 240, QImage::Format_RGB32);
    image.fill(color);
    for (int i = 0; i < frames; ++i) {
        QVideoFrame frame = VideoFrames::toNV12(image);
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
        window->close();
        delete window;
    }

    // Play plays: the position runs on, and the button turns to pause.
    void videoEditorPlays()
    {
        QTemporaryDir dir;
        const QString clip = writeVideo(dir.path() + u"/clip.mp4"_s, 90, Qt::darkCyan);
        QVERIFY(!clip.isEmpty());
        auto *document = new Document;
        auto *exporter = new VideoExporter;
        QQmlComponent component(m_engine);
        component.loadFromModule("Guckste", "VideoEditorWindow");
        auto *window = qobject_cast<QQuickWindow *>(component.createWithInitialProperties(
            {{u"document"_s, QVariant::fromValue(document)}, {u"sourcePath"_s, clip},
             {u"exporter"_s, QVariant::fromValue(exporter)}}));
        QVERIFY2(window, qPrintable(component.errorString()));
        document->setParent(window);
        exporter->setParent(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *player = window->findChild<QMediaPlayer *>(u"player"_s);
        auto *play = window->findChild<QQuickItem *>(u"playButton"_s);
        QVERIFY(player && play);
        QTRY_VERIFY_WITH_TIMEOUT(player->duration() > 2000, 5000);
        QTest::qWait(500);

        const QPoint at = play->mapToScene(QPointF(play->width() / 2, play->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
        QTest::qWait(1000);
        QCOMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QVERIFY(player->position() > 300);

        // Again: paused where it was; once more: on from there.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        const qint64 paused = player->position();
        QTest::qWait(300);
        QVERIFY(std::abs(player->position() - paused) < 100);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
        QTest::qWait(500);
        QCOMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QVERIFY(player->position() > paused + 200);

        window->setProperty("discardConfirmed", true);
        window->close();
        QTRY_VERIFY(!window->isVisible());
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
