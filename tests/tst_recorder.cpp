// The recorder's pieces that need no screen: the format this Qt encodes
// and the cropping of frames.

#include "core/ScreenRecorder.h"
#include "core/SingleInstance.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QMimeType>
#include <QTest>
#include <QVideoFrame>

using namespace Qt::StringLiterals;

class TestRecorder : public QObject
{
    Q_OBJECT

private slots:
    void hasAnEncoder()
    {
        const QMediaFormat format = ScreenRecorder::bestFormat();
        qInfo("Recording as %s, suffix %s", qPrintable(format.mimeType().name()),
              qPrintable(ScreenRecorder::suffixFor(format)));
        QVERIFY(format.videoCodec() != QMediaFormat::VideoCodec::Unspecified);
        QVERIFY(!ScreenRecorder::suffixFor(format).isEmpty());
        // The chosen one was tried out, so it encodes.
        QVERIFY(ScreenRecorder::canEncode(format));
        // And the choice is kept.
        QCOMPARE(ScreenRecorder::bestFormat().videoCodec(), format.videoCodec());
    }

    void cropsFrames()
    {
        QImage image(64, 48, QImage::Format_RGBA8888);
        image.fill(Qt::blue);
        for (int y = 10; y < 20; ++y)
            for (int x = 20; x < 40; ++x)
                image.setPixelColor(x, y, Qt::red);
        QVideoFrame frame(image);
        frame.setStartTime(1000);
        frame.setEndTime(2000);

        const QVideoFrame cropped = ScreenRecorder::cropped(frame, QRect(20, 10, 20, 10));
        QCOMPARE(cropped.size(), QSize(20, 10));
        QCOMPARE(cropped.pixelFormat(), QVideoFrameFormat::Format_NV12);
        QCOMPARE(cropped.startTime(), 1000);
        QCOMPARE(cropped.endTime(), 2000);
        // Back from YUV the colors are near, not exact.
        auto near = [](const QColor &a, const QColor &b) {
            return qAbs(a.red() - b.red()) < 8 && qAbs(a.green() - b.green()) < 8 && qAbs(a.blue() - b.blue()) < 8;
        };
        const QImage out = cropped.toImage();
        QVERIFY2(near(out.pixelColor(0, 0), Qt::red), qPrintable(out.pixelColor(0, 0).name()));
        QVERIFY2(near(out.pixelColor(19, 9), Qt::red), qPrintable(out.pixelColor(19, 9).name()));

        // Beyond the frame: clipped to it, and to even sizes.
        const QVideoFrame edge = ScreenRecorder::cropped(frame, QRect(50, 40, 30, 30));
        QCOMPARE(edge.size(), QSize(14, 8));
        QVERIFY2(near(edge.toImage().pixelColor(0, 0), Qt::blue), qPrintable(edge.toImage().pixelColor(0, 0).name()));

        // Grey stays grey, white and black keep their ends.
        QImage grey(8, 8, QImage::Format_RGB32);
        grey.fill(QColor(128, 128, 128));
        QVERIFY(near(ScreenRecorder::toNV12(grey).toImage().pixelColor(3, 3), QColor(128, 128, 128)));
        grey.fill(Qt::white);
        QVERIFY(near(ScreenRecorder::toNV12(grey).toImage().pixelColor(3, 3), Qt::white));
        grey.fill(Qt::black);
        QVERIFY(near(ScreenRecorder::toNV12(grey).toImage().pixelColor(3, 3), Qt::black));
    }

    // A real recording of a screen region, when GUCKSTE_LIVE_RECORD names
    // a file to write; it needs a display, so it is skipped otherwise.
    void recordsTheScreen()
    {
        const QString path = qEnvironmentVariable("GUCKSTE_LIVE_RECORD");
        if (path.isEmpty())
            QSKIP("Set GUCKSTE_LIVE_RECORD to a file path to record the screen");
        ScreenRecorder recorder;
        QStringList errors;
        connect(&recorder, &ScreenRecorder::errorOccurred, this, [&errors](const QString &m) { errors << m; });
        QSignalSpy finished(&recorder, &ScreenRecorder::finished);
        const bool started = recorder.start(QRect(100, 100, 640, 360), path, 30, false);
        QVERIFY2(started, qPrintable(errors.join(u"; "_s)));
        QCOMPARE(recorder.state(), ScreenRecorder::Recording);
        QTest::qWait(1500);
        recorder.pause();
        QCOMPARE(recorder.state(), ScreenRecorder::Paused);
        QTest::qWait(500);
        recorder.resume();
        QTest::qWait(1500);
        QVERIFY(recorder.duration() > 1000);
        // The pause is left out: about 3 s of the 3.5 s.
        QVERIFY2(recorder.duration() < 3300, qPrintable(QString::number(recorder.duration())));
        recorder.stop();
        QTRY_COMPARE_WITH_TIMEOUT(recorder.state(), ScreenRecorder::Idle, 10000);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(u"; "_s)));
        QCOMPARE(finished.count(), 1);
        QVERIFY(QFileInfo(path).size() > 1000);
        qInfo("Recorded %lld ms to %s (%lld bytes)", recorder.duration(), qPrintable(path), QFileInfo(path).size());
    }

    void oneInstanceAtATime()
    {
        qputenv("GUCKSTE_INSTANCE", "test-" + QByteArray::number(QCoreApplication::applicationPid()));
        QVERIFY(!SingleInstance::isRunning());
        SingleInstance first;
        QVERIFY(first.claim());
        QVERIFY(first.claim());
        QVERIFY(SingleInstance::isRunning());
        SingleInstance second;
        QVERIFY(!second.claim());
        first.release();
        QVERIFY(second.claim());
        second.release();
        QVERIFY(!SingleInstance::isRunning());
        // Nobody to send to.
        QVERIFY(!SingleInstance::sendToRunning(u"gui"_s, 300));
        qunsetenv("GUCKSTE_INSTANCE");
    }

    void startsOnlyWithAScreen()
    {
        ScreenRecorder recorder;
        QCOMPARE(recorder.state(), ScreenRecorder::Idle);
        // An empty region cannot be recorded.
        QVERIFY(!recorder.start(QRect(), u"/nonexistent/x.mp4"_s, 30, false));
        QCOMPARE(recorder.state(), ScreenRecorder::Idle);
    }
};

QTEST_MAIN(TestRecorder)
#include "tst_recorder.moc"
