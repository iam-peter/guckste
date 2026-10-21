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
        document->addText(50, 100, u"Guckste"_s, Qt::white, 4);
        QCOMPARE(document->annotationCount(), 3);
        QCOMPARE(window->property("exportScale").toReal(), 4.0);
        QTest::qWait(50);
        saveScreenshot(window, u"editor"_s);
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
