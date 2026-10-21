// The recorder's pieces that need no screen: the format this Qt encodes
// and the cropping of frames.

#include "core/CursorCapture.h"
#include "core/Encoding.h"
#include "core/KeyListener.h"
#include "core/KeyOverlay.h"
#include "core/ScreenRecorder.h"
#include "core/VideoExporter.h"
#include "core/VideoFrames.h"
#include "core/WindowFinder.h"
#include "core/SingleInstance.h"

#include <QFileInfo>
#include <QMediaCaptureSession>
#include <QMediaPlayer>
#include <QTemporaryDir>
#include <QVideoFrameInput>
#include <QVideoSink>
#include <QGuiApplication>
#include <QPainter>
#include <QProcess>
#include <QSignalSpy>
#include <QMimeType>
#include <QTest>
#include <QVideoFrame>

using namespace Qt::StringLiterals;

class TestRecorder : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Encoding::quietFFmpeg();
    }

    void hasAnEncoder()
    {
        const QMediaFormat format = Encoding::bestFormat();
        qInfo("Recording as %s, suffix %s", qPrintable(format.mimeType().name()),
              qPrintable(Encoding::suffixFor(format)));
        QVERIFY(format.videoCodec() != QMediaFormat::VideoCodec::Unspecified);
        QVERIFY(!Encoding::suffixFor(format).isEmpty());
        // The chosen one was tried out, so it encodes.
        QVERIFY(Encoding::canEncode(format));
        // And the choice is kept.
        QCOMPARE(Encoding::bestFormat().videoCodec(), format.videoCodec());
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

        const QVideoFrame cropped = VideoFrames::cropped(frame, QRect(20, 10, 20, 10));
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
        const QVideoFrame edge = VideoFrames::cropped(frame, QRect(50, 40, 30, 30));
        QCOMPARE(edge.size(), QSize(14, 8));
        QVERIFY2(near(edge.toImage().pixelColor(0, 0), Qt::blue), qPrintable(edge.toImage().pixelColor(0, 0).name()));

        // Grey stays grey, white and black keep their ends.
        QImage grey(8, 8, QImage::Format_RGB32);
        grey.fill(QColor(128, 128, 128));
        QVERIFY(near(VideoFrames::toNV12(grey).toImage().pixelColor(3, 3), QColor(128, 128, 128)));
        grey.fill(Qt::white);
        QVERIFY(near(VideoFrames::toNV12(grey).toImage().pixelColor(3, 3), Qt::white));
        grey.fill(Qt::black);
        QVERIFY(near(VideoFrames::toNV12(grey).toImage().pixelColor(3, 3), Qt::black));
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
        const bool started = recorder.start(QRect(100, 100, 640, 360), path, ScreenRecorder::Options());
        QVERIFY2(started, qPrintable(errors.join(u"; "_s)));
        QCOMPARE(recorder.state(), ScreenRecorder::Recording);
        // The first moment is left out, and cannot be paused.
        QVERIFY(recorder.starting());
        QVERIFY(!recorder.canPause());
        QTRY_VERIFY_WITH_TIMEOUT(!recorder.starting(), ScreenRecorder::warmUpMs + 500);
        QVERIFY(recorder.canPause());
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
        QVERIFY(!recorder.firstFrame().isNull());
        QCOMPARE(recorder.firstFrame().size(), QSize(640, 360));
        recorder.firstFrame().save(QFileInfo(path).absolutePath() + u"/live-first.png"_s);
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

    // A short video written from frames, for the exporter.
    static QString writeVideo(const QString &path, int frames, const QColor &color)
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
            // One at a time: the input refuses a frame while it holds one.
            if (!QTest::qWaitFor([&input, &frame] { return input.sendVideoFrame(frame); }, 5000))
                return {};
        }
        QTest::qWait(300);
        recorder.stop();
        if (!QTest::qWaitFor([&recorder] { return recorder.recorderState() == QMediaRecorder::StoppedState; }, 10000))
            return {};
        return recorder.actualLocation().toLocalFile();
    }

    void exportsWithMarksAndTrim()
    {
        QTemporaryDir dir;
        const QString source = writeVideo(dir.path() + u"/source.mp4"_s, 90, Qt::blue);
        QVERIFY(QFileInfo(source).size() > 0);

        VideoExporter exporter;
        QSignalSpy finished(&exporter, &VideoExporter::finished);
        QSignalSpy failed(&exporter, &VideoExporter::failed);
        VideoExporter::Job job;
        job.source = source;
        job.target = dir.path() + u"/out.mp4"_s;
        job.startMs = 500;
        job.endMs = 1500;
        Annotation rect;
        rect.type = Annotation::Type::Rectangle;
        rect.points = {QPointF(40, 40), QPointF(200, 160)};
        rect.color = Qt::red;
        rect.size = 6;
        job.annotations = {rect};
        QVERIFY(exporter.start(job));
        QVERIFY(exporter.running());
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0 || failed.count() > 0, 30000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QVERIFY(!exporter.running());
        QVERIFY(QFileInfo(job.target).size() > 0);

        // Read it back: about a second long, with the red frame on it.
        QMediaPlayer player;
        QVideoSink sink;
        player.setVideoSink(&sink);
        QImage seen;
        connect(&sink, &QVideoSink::videoFrameChanged, this, [&seen](const QVideoFrame &f) {
            if (seen.isNull() && f.isValid())
                seen = f.toImage();
        });
        player.setSource(QUrl::fromLocalFile(job.target));
        QTRY_COMPARE_WITH_TIMEOUT(player.mediaStatus(), QMediaPlayer::LoadedMedia, 10000);
        QVERIFY2(player.duration() > 800 && player.duration() < 1300,
                 qPrintable(u"duration %1 of a %2 source"_s.arg(player.duration()).arg(QFileInfo(source).size())));
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(!seen.isNull(), 10000);
        player.stop();
        const QColor edge = seen.pixelColor(40, 100);
        const QColor inside = seen.pixelColor(120, 100);
        QVERIFY2(edge.red() > 150 && edge.blue() < 100, qPrintable(edge.name()));
        QVERIFY2(inside.blue() > 150 && inside.red() < 100, qPrintable(inside.name()));

        // Nothing to change: the file is just moved.
        const QString plain = writeVideo(dir.path() + u"/plain.mp4"_s, 30, Qt::green);
        VideoExporter mover;
        QSignalSpy moved(&mover, &VideoExporter::finished);
        VideoExporter::Job copy;
        copy.source = plain;
        copy.target = dir.path() + u"/moved.mp4"_s;
        copy.moveSource = true;
        QVERIFY(mover.start(copy));
        QCOMPARE(moved.count(), 1);
        QVERIFY(QFileInfo::exists(copy.target));
        QVERIFY(!QFileInfo::exists(plain));
    }

    // Marks timed on a recording show on their frames only: one in the
    // first second, one from the second on.
    void exportsTimedMarks()
    {
        QTemporaryDir dir;
        const QString source = writeVideo(dir.path() + u"/source.mp4"_s, 60, Qt::blue);
        QVERIFY(QFileInfo(source).size() > 0);
        VideoExporter exporter;
        QSignalSpy finished(&exporter, &VideoExporter::finished);
        QSignalSpy failed(&exporter, &VideoExporter::failed);
        VideoExporter::Job job;
        job.source = source;
        job.target = dir.path() + u"/out.mp4"_s;
        Annotation early;
        early.type = Annotation::Type::Marker;
        early.points = {QPointF(0, 0), QPointF(160, 240)};
        early.color = Qt::red;
        early.endMs = 1000;
        Annotation late = early;
        late.points = {QPointF(160, 0), QPointF(320, 240)};
        late.color = Qt::green;
        late.startMs = 1000;
        late.endMs = -1;
        job.annotations = {early, late};
        QVERIFY(exporter.start(job));
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0 || failed.count() > 0, 30000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));

        // A frame from each half.
        QMediaPlayer player;
        QVideoSink sink;
        player.setVideoSink(&sink);
        QImage first, second;
        connect(&sink, &QVideoSink::videoFrameChanged, this, [&](const QVideoFrame &f) {
            if (!f.isValid())
                return;
            if (first.isNull() && f.startTime() < 500000)
                first = f.toImage();
            if (second.isNull() && f.startTime() > 1300000)
                second = f.toImage();
        });
        player.setSource(QUrl::fromLocalFile(job.target));
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(!first.isNull() && !second.isNull(), 10000);
        player.stop();
        // Marker over blue: tinted red on the left early, green on the right late.
        const auto tinted = [](const QImage &image, int x, bool red) {
            const QColor c = image.pixelColor(x, 120);
            return red ? c.red() > 70 : c.green() > 70;
        };
        QVERIFY(tinted(first, 80, true));
        QVERIFY(!tinted(first, 240, false));
        QVERIFY(!tinted(second, 80, true));
        QVERIFY(tinted(second, 240, false));
    }

    void paintsTheCursor()
    {
        CursorCapture::Cursor cursor;
        cursor.image = QImage(8, 8, QImage::Format_ARGB32_Premultiplied);
        cursor.image.fill(Qt::red);
        cursor.hotspot = QPoint(0, 0);
        cursor.position = QPoint(110, 120);
        cursor.ok = true;
        QImage target(100, 100, QImage::Format_RGB32);
        target.fill(Qt::black);
        // The target shows the desktop from (100, 100).
        CursorCapture::paint(target, QRect(100, 100, 100, 100), 1, cursor);
        QCOMPARE(target.pixelColor(10, 20), QColor(Qt::red));
        QCOMPARE(target.pixelColor(17, 27), QColor(Qt::red));
        QCOMPARE(target.pixelColor(18, 28), QColor(Qt::black));
        // Outside: nothing.
        cursor.position = QPoint(500, 500);
        CursorCapture::paint(target, QRect(100, 100, 100, 100), 1, cursor);
        QCOMPARE(target.pixelColor(50, 50), QColor(Qt::black));
        // The real one, whatever it is here.
        QVERIFY(CursorCapture::current().ok);
    }

    void labelsKeys()
    {
        QCOMPARE(KeyListener::label(u"a"_s, false, false, false, false), u"a"_s);
        QCOMPARE(KeyListener::label(u"A"_s, true, false, false, false), u"A"_s);
        QCOMPARE(KeyListener::label(u"s"_s, false, true, false, false), u"Ctrl+S"_s);
        QCOMPARE(KeyListener::label(u"S"_s, true, true, false, false), u"Ctrl+Shift+S"_s);
        QCOMPARE(KeyListener::label(u"Return"_s, false, false, false, false), u"Enter"_s);
        QCOMPARE(KeyListener::label(u"BackSpace"_s, false, false, false, false), u"⌫"_s);
        QCOMPARE(KeyListener::label(u"Tab"_s, true, false, false, false), u"Shift+Tab"_s);
        QCOMPARE(KeyListener::label(u"KP_Enter"_s, false, false, true, false), u"Alt+Enter"_s);
#if defined(Q_OS_MACOS)
        const QString super = u"Cmd"_s;
#elif defined(Q_OS_WIN)
        const QString super = u"Win"_s;
#else
        const QString super = u"Super"_s;
#endif
        QCOMPARE(KeyListener::label(u"F5"_s, false, false, false, true), super + u"+F5"_s);
        QCOMPARE(KeyListener::label(u"adiaeresis"_s, false, false, false, false), u"ä"_s);

        KeyOverlay overlay;
        QVERIFY(overlay.isEmpty());
        overlay.add(u"Ctrl+S"_s);
        overlay.add(u"Ctrl+S"_s);
        overlay.add(u"a"_s);
        QCOMPARE(overlay.labels(), (QStringList{u"Ctrl+S ×2"_s, u"a"_s}));
        QImage frame(640, 360, QImage::Format_RGB32);
        frame.fill(Qt::white);
        QPainter painter(&frame);
        overlay.paint(painter, frame.rect());
        painter.end();
        // Chips are dark, along the bottom.
        bool dark = false;
        for (int x = 0; x < 640 && !dark; ++x)
            dark = frame.pixelColor(x, 330).lightness() < 100;
        QVERIFY(dark);
        QCOMPARE(frame.pixelColor(320, 100), QColor(Qt::white));
    }

    // The key listener with a real key press, sent with xdotool when
    // GUCKSTE_LIVE_KEY is set; it needs a display and X11.
    void listensForKeys()
    {
        if (qEnvironmentVariableIsEmpty("GUCKSTE_LIVE_KEY"))
            QSKIP("Set GUCKSTE_LIVE_KEY to press F13 with xdotool and listen for it");
        if (!KeyListener::supported())
            QSKIP("No X11");
        KeyListener listener;
        QSignalSpy pressed(&listener, &KeyListener::keyPressed);
        QVERIFY(listener.begin());
        QTest::qWait(300);
        QProcess::execute(u"xdotool"_s, {u"key"_s, u"F13"_s});
        QTRY_VERIFY_WITH_TIMEOUT(pressed.count() > 0, 3000);
        QCOMPARE(pressed.first().first().toString(), u"F13"_s);
        listener.end();
        QVERIFY(!listener.isRunning());
    }

    void listsVisibleWindows()
    {
        // Offscreen there are none, but the call has to hold up.
        const QList<QRect> windows = WindowFinder::windows();
        for (const QRect &r : windows) {
            QVERIFY(r.width() > 1 && r.height() > 1);
            qInfo("window %d,%d %dx%d", r.x(), r.y(), r.width(), r.height());
        }
    }

    void startsOnlyWithAScreen()
    {
        ScreenRecorder recorder;
        QCOMPARE(recorder.state(), ScreenRecorder::Idle);
        // An empty region cannot be recorded.
        QVERIFY(!recorder.start(QRect(), u"/nonexistent/x.mp4"_s, ScreenRecorder::Options()));
        QCOMPARE(recorder.state(), ScreenRecorder::Idle);
    }
};

QTEST_MAIN(TestRecorder)
#include "tst_recorder.moc"
